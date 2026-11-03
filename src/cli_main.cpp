#include "control_backend.h"
#include "camera_usage.h"
#include "profile_store.h"
#ifdef Q_OS_WIN
#include "windows_camera_rotation.h"
#endif

#include <QCameraDevice>
#include <QCameraFormat>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QTextStream>
#include <QVideoFrameFormat>

#include <algorithm>

namespace {
QTextStream qout(stdout);
QTextStream qerr(stderr);

struct OpenCamera {
    QCameraDevice device;
    std::unique_ptr<CameraControlBackend> backend;
};

QCameraDevice selectDevice(const QString &selector)
{
    const auto devices = QMediaDevices::videoInputs();
    if (devices.isEmpty()) {
        return {};
    }
    if (!selector.isEmpty()) {
        const auto match = std::find_if(devices.cbegin(), devices.cend(), [&selector](const auto &device) {
            return device.description().contains(selector, Qt::CaseInsensitive)
                || QString::fromUtf8(device.id()).contains(selector, Qt::CaseInsensitive);
        });
        return match == devices.cend() ? QCameraDevice{} : *match;
    }
    const auto ugreen = std::find_if(devices.cbegin(), devices.cend(), [](const auto &device) {
        return device.description().contains(QStringLiteral("UGREEN"), Qt::CaseInsensitive);
    });
    return ugreen == devices.cend() ? devices.first() : *ugreen;
}

bool openSelectedCamera(const QString &selector, OpenCamera *camera)
{
    camera->device = selectDevice(selector);
    if (camera->device.isNull()) {
        qerr << (selector.isEmpty()
                     ? QStringLiteral("No camera was found.\n")
                     : QStringLiteral("No camera matched “%1”.\n").arg(selector));
        return false;
    }
    camera->backend = createPlatformBackend();
    QString error;
    if (!camera->backend->open(camera->device.id(), camera->device.description(), &error)) {
        qerr << error << '\n';
        return false;
    }
    return true;
}

QJsonObject controlJson(const CameraControl &control)
{
    QJsonArray options;
    for (const auto &option : control.options) {
        options.append(QJsonObject{{QStringLiteral("value"), option.value},
                                   {QStringLiteral("label"), option.label}});
    }
    return {
        {QStringLiteral("control"), control.id},
        {QStringLiteral("name"), control.name},
        {QStringLiteral("category"), control.category},
        {QStringLiteral("type"), cameraControlTypeKey(control.type)},
        {QStringLiteral("value"), control.value},
        {QStringLiteral("auto"), control.automatic},
        {QStringLiteral("supportsAuto"), control.supportsAuto},
        {QStringLiteral("minimum"), control.minimum},
        {QStringLiteral("maximum"), control.maximum},
        {QStringLiteral("step"), control.normalizedStep()},
        {QStringLiteral("default"), control.defaultValue},
        {QStringLiteral("readOnly"), control.readOnly},
        {QStringLiteral("options"), options},
    };
}

void printControls(const OpenCamera &camera, const QVector<CameraControl> &controls, bool json)
{
    if (json) {
        QJsonArray values;
        for (const auto &control : controls) {
            values.append(controlJson(control));
        }
        qout << QJsonDocument(QJsonObject{
                    {QStringLiteral("device"), camera.device.description()},
                    {QStringLiteral("deviceId"), QString::fromUtf8(camera.device.id())},
                    {QStringLiteral("controls"), values},
                }).toJson(QJsonDocument::Indented);
        return;
    }

    qout << camera.device.description() << '\n';
    for (const auto &control : controls) {
        qout << "  " << control.id << '=';
        if (control.automatic) {
            qout << "auto (current " << control.value << ')';
        } else {
            qout << control.displayValue(control.value);
        }
        if (control.type == CameraControlType::Integer) {
            qout << "  [" << control.minimum << ".." << control.maximum
                 << ", step " << control.normalizedStep() << ", default " << control.defaultValue << ']';
        } else {
            qout << "  [" << cameraControlTypeKey(control.type) << ']';
        }
        if (control.supportsAuto) {
            qout << "  auto-supported";
        }
        qout << '\n';
    }
}

QVector<CameraControlValue> valuesFrom(const QVector<CameraControl> &controls)
{
    QVector<CameraControlValue> values;
    values.reserve(controls.size());
    for (const auto &control : controls) {
        if (!control.readOnly && control.type != CameraControlType::Button)
            values.push_back({control.id, control.value, control.automatic});
    }
    return values;
}

int commandDevices(bool json)
{
    const auto devices = QMediaDevices::videoInputs();
    if (json) {
        QJsonArray output;
        for (const auto &device : devices) {
            output.append(QJsonObject{
                {QStringLiteral("name"), device.description()},
                {QStringLiteral("id"), QString::fromUtf8(device.id())},
                {QStringLiteral("default"), device.isDefault()},
            });
        }
        qout << QJsonDocument(output).toJson(QJsonDocument::Indented);
    } else {
        for (const auto &device : devices) {
            qout << (device.isDefault() ? "* " : "  ") << device.description() << '\n';
        }
    }
    return devices.isEmpty() ? 2 : 0;
}

int commandControls(const QString &selector, bool json)
{
    OpenCamera camera;
    if (!openSelectedCamera(selector, &camera)) {
        return 2;
    }
    QString error;
    const auto controls = camera.backend->controls(&error);
    if (!error.isEmpty()) {
        qerr << error << '\n';
        return 3;
    }
    printControls(camera, controls, json);
    return 0;
}

int commandFormats(const QString &selector, bool json)
{
    const QCameraDevice device = selectDevice(selector);
    if (device.isNull()) {
        qerr << "No matching camera was found.\n";
        return 2;
    }
    if (json) {
        QJsonArray formats;
        for (const auto &format : device.videoFormats()) {
            formats.append(QJsonObject{
                {QStringLiteral("width"), format.resolution().width()},
                {QStringLiteral("height"), format.resolution().height()},
                {QStringLiteral("minimumFrameRate"), format.minFrameRate()},
                {QStringLiteral("maximumFrameRate"), format.maxFrameRate()},
                {QStringLiteral("pixelFormat"), static_cast<int>(format.pixelFormat())},
            });
        }
        qout << QJsonDocument(formats).toJson(QJsonDocument::Indented);
    } else {
        qout << device.description() << '\n';
        for (const auto &format : device.videoFormats()) {
            qout << "  " << format.resolution().width() << 'x' << format.resolution().height()
                 << "  " << format.minFrameRate() << "-" << format.maxFrameRate() << " fps"
                 << "  pixel-format=" << QVideoFrameFormat::pixelFormatToString(format.pixelFormat())
                 << " (" << static_cast<int>(format.pixelFormat()) << ")\n";
        }
    }
    return 0;
}

int commandUsers(const QString &selector, bool json)
{
    const QCameraDevice device = selectDevice(selector);
    if (device.isNull()) {
        qerr << "No matching camera was found.\n";
        return 2;
    }
    QString error;
    const QVector<CameraUser> users = CameraUsageInspector::users(device.id(), &error);
    if (!error.isEmpty() && users.isEmpty()) {
        qerr << error << '\n';
        return 3;
    }
    if (json) {
        QJsonArray output;
        for (const CameraUser &user : users) {
            output.append(QJsonObject{
                {QStringLiteral("pid"), user.pid},
                {QStringLiteral("name"), user.name},
                {QStringLiteral("executable"), user.executable},
                {QStringLiteral("detail"), user.detail},
            });
        }
        qout << QJsonDocument(QJsonObject{
                    {QStringLiteral("device"), device.description()},
                    {QStringLiteral("users"), output},
                }).toJson(QJsonDocument::Indented);
    } else if (users.isEmpty()) {
        qout << "No other active camera process detected.\n";
    } else {
        qout << device.description() << '\n';
        for (const CameraUser &user : users) {
            qout << "  PID " << user.pid << "  " << user.name;
            if (!user.executable.isEmpty()) qout << "  " << user.executable;
            qout << '\n';
        }
    }
    return 0;
}

int commandTerminate(const QString &pidText, bool confirmed)
{
    if (!confirmed) {
        qerr << "Refusing to terminate a process without --yes. Unsaved work may be lost.\n";
        return 1;
    }
    bool valid = false;
    const qint64 pid = pidText.toLongLong(&valid);
    if (!valid || pid <= 0) {
        qerr << "terminate requires a positive numeric PID.\n";
        return 1;
    }
    QString error;
    if (!CameraUsageInspector::terminate(pid, &error)) {
        qerr << error << '\n';
        return 4;
    }
    qout << "Terminated PID " << pid << ".\n";
    return 0;
}

int commandSave(const QString &selector, const QString &path)
{
    OpenCamera camera;
    if (!openSelectedCamera(selector, &camera)) {
        return 2;
    }
    QString error;
    const auto controls = camera.backend->controls(&error);
    if (!error.isEmpty()) {
        qerr << error << '\n';
        return 3;
    }
    if (!ProfileStore::save(path, camera.device.id(), camera.device.description(),
                            valuesFrom(controls), &error)) {
        qerr << error << '\n';
        return 4;
    }
    qout << "Saved " << controls.size() << " controls to " << path << '\n';
    return 0;
}

int commandRestore(const QString &selector, const QString &path)
{
    OpenCamera camera;
    if (!openSelectedCamera(selector, &camera)) {
        return 2;
    }
    QVector<CameraControlValue> values;
    QString profileDevice;
    QString error;
    if (!ProfileStore::load(path, &values, &profileDevice, &error)) {
        qerr << error << '\n';
        return 4;
    }
    int failures = 0;
    for (const auto &value : values) {
        CameraControlValue accepted;
        if (!camera.backend->setControl(value, &accepted, &error)) {
            qerr << value.id << ": " << error << '\n';
            ++failures;
        }
    }
    qout << "Restored " << (values.size() - failures) << '/' << values.size()
         << " controls from " << path << '\n';
    return failures == 0 ? 0 : 5;
}

int commandSet(const QString &selector, const QStringList &assignments)
{
    OpenCamera camera;
    if (!openSelectedCamera(selector, &camera)) {
        return 2;
    }
    QString error;
    const auto controls = camera.backend->controls(&error);
    if (!error.isEmpty()) {
        qerr << error << '\n';
        return 3;
    }

    int failures = 0;
    for (const QString &assignment : assignments) {
        const qsizetype equals = assignment.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            qerr << "Expected control=value, got “" << assignment << "”.\n";
            ++failures;
            continue;
        }
        const QString key = normalizedControlId(assignment.left(equals));
        const auto supported = std::find_if(controls.cbegin(), controls.cend(), [&key](const auto &control) {
            return control.id.compare(key, Qt::CaseInsensitive) == 0;
        });
        if (supported == controls.cend()) {
            qerr << key << " is not supported by this camera.\n";
            ++failures;
            continue;
        }
        if (supported->readOnly) {
            qerr << key << " is read-only.\n";
            ++failures;
            continue;
        }

        const QString requested = assignment.mid(equals + 1).trimmed();
        const bool automatic = requested.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
        bool numberOk = false;
        qint64 value = automatic ? supported->value : requested.toLongLong(&numberOk);
        if (!numberOk && supported->type == CameraControlType::Boolean) {
            if (requested.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0
                || requested.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
                value = 1;
                numberOk = true;
            } else if (requested.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0
                       || requested.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
                value = 0;
                numberOk = true;
            }
        }
        if (!numberOk && supported->type == CameraControlType::Menu) {
            const auto option = std::find_if(supported->options.cbegin(), supported->options.cend(),
                                             [&requested](const auto &item) {
                return item.label.compare(requested, Qt::CaseInsensitive) == 0;
            });
            if (option != supported->options.cend()) {
                value = option->value;
                numberOk = true;
            }
        }
        if (!automatic && !numberOk) {
            qerr << "Invalid value for " << key << ": “" << requested << "”.\n";
            ++failures;
            continue;
        }
        if (automatic && !supported->supportsAuto) {
            qerr << key << " does not support automatic mode.\n";
            ++failures;
            continue;
        }
        value = supported->clamped(value);
        CameraControlValue accepted;
        if (!camera.backend->setControl({supported->id, value, automatic}, &accepted, &error)) {
            qerr << key << ": " << error << '\n';
            ++failures;
            continue;
        }
        qout << key << '=' << (accepted.automatic ? QStringLiteral("auto")
                                                   : supported->displayValue(accepted.value));
        if (!automatic && accepted.value != value) qout << "  (camera adjusted from " << value << ')';
        qout << '\n';
    }
    return failures == 0 ? 0 : 5;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("webcam-settings-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral(WEBCAM_SETTINGS_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Inspect, set, save, and restore physical webcam controls."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption deviceOption({QStringLiteral("d"), QStringLiteral("device")},
                                           QStringLiteral("Select a camera by name or device-id substring."),
                                           QStringLiteral("selector"));
    const QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("Emit JSON output."));
#ifdef Q_OS_WIN
    const QCommandLineOption deviceIdOption(QStringLiteral("device-id"),
        QStringLiteral("Exact device interface for the Windows rotate command."), QStringLiteral("id"));
    parser.addOption(deviceIdOption);
#endif
    const QCommandLineOption yesOption({QStringLiteral("y"), QStringLiteral("yes")},
                                        QStringLiteral("Confirm process termination."));
    parser.addOption(deviceOption);
    parser.addOption(jsonOption);
    parser.addOption(yesOption);
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("devices, formats, controls, users, terminate, save, set, restore, or rotate (Windows)"));
    parser.addPositionalArgument(QStringLiteral("arguments"),
                                 QStringLiteral("Command-specific path or control=value arguments"),
                                 QStringLiteral("[arguments...]"));
    parser.process(application);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        parser.showHelp(1);
    }
    const QString command = positional.first().toLower();
    const QString selector = parser.value(deviceOption);
    const bool json = parser.isSet(jsonOption);

#ifdef Q_OS_WIN
    if (command == QStringLiteral("rotate")) {
        bool valid = false;
        const qint64 degrees = positional.size() == 2 ? positional.at(1).toLongLong(&valid) : -1;
        if (!valid || (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)) {
            qerr << "rotate requires 0, 90, 180, or 270 clockwise degrees.\n";
            return 1;
        }
        QByteArray deviceId = parser.value(deviceIdOption).toUtf8();
        if (deviceId.isEmpty()) {
            const auto device = selectDevice(selector);
            if (device.isNull()) {
                qerr << "No matching camera was found.\n";
                return 2;
            }
            deviceId = device.id();
        }
        QString error;
        if (!WindowsCameraRotation::write(deviceId, degrees, &error)) {
            qerr << error << '\n';
            return 5;
        }
        qout << "Saved video rotation: " << degrees
             << " degrees clockwise. Reopen the camera in applications to apply it.\n";
        return 0;
    }
#endif

    if (command == QStringLiteral("devices")) {
        return commandDevices(json);
    }
    if (command == QStringLiteral("controls") || command == QStringLiteral("get")) {
        return commandControls(selector, json);
    }
    if (command == QStringLiteral("formats")) {
        return commandFormats(selector, json);
    }
    if (command == QStringLiteral("users")) {
        return commandUsers(selector, json);
    }
    if ((command == QStringLiteral("terminate") || command == QStringLiteral("kill"))
        && positional.size() == 2) {
        return commandTerminate(positional.at(1), parser.isSet(yesOption));
    }
    if ((command == QStringLiteral("save") || command == QStringLiteral("restore"))
        && positional.size() != 2) {
        qerr << command << " requires exactly one profile path.\n";
        return 1;
    }
    if (command == QStringLiteral("save")) {
        return commandSave(selector, positional.at(1));
    }
    if (command == QStringLiteral("restore")) {
        return commandRestore(selector, positional.at(1));
    }
    if (command == QStringLiteral("set") && positional.size() >= 2) {
        return commandSet(selector, positional.mid(1));
    }

    qerr << "Unknown or incomplete command “" << command << "”. Use --help.\n";
    return 1;
}
