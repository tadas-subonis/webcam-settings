#include "control_backend.h"

#include <linux/videodev2.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
struct KnownControl { quint32 nativeId; const char *id; };

const KnownControl kKnownControls[] = {
    {V4L2_CID_PAN_ABSOLUTE, "pan"}, {V4L2_CID_TILT_ABSOLUTE, "tilt"},
    {V4L2_CID_ROTATE, "roll"}, {V4L2_CID_ZOOM_ABSOLUTE, "zoom"},
    {V4L2_CID_EXPOSURE_ABSOLUTE, "exposure"}, {V4L2_CID_IRIS_ABSOLUTE, "iris"},
    {V4L2_CID_FOCUS_ABSOLUTE, "focus"}, {V4L2_CID_BRIGHTNESS, "brightness"},
    {V4L2_CID_CONTRAST, "contrast"}, {V4L2_CID_HUE, "hue"},
    {V4L2_CID_SATURATION, "saturation"}, {V4L2_CID_SHARPNESS, "sharpness"},
    {V4L2_CID_GAMMA, "gamma"}, {V4L2_CID_COLORFX, "color-enable"},
    {V4L2_CID_WHITE_BALANCE_TEMPERATURE, "white-balance"},
    {V4L2_CID_BACKLIGHT_COMPENSATION, "backlight-compensation"},
    {V4L2_CID_GAIN, "gain"}, {V4L2_CID_FOCUS_AUTO, "focus-auto"},
    {V4L2_CID_EXPOSURE_AUTO, "exposure-auto"},
    {V4L2_CID_AUTO_WHITE_BALANCE, "white-balance-auto"},
    {V4L2_CID_AUTOGAIN, "gain-auto"}, {V4L2_CID_HUE_AUTO, "hue-auto"},
};

void setError(QString *target, const QString &message)
{
    if (target) *target = message;
}

QString systemError(const QString &operation)
{
    return QStringLiteral("%1: %2").arg(operation, QString::fromLocal8Bit(std::strerror(errno)));
}

int xioctl(const int descriptor, const unsigned long request, void *argument)
{
    int result;
    do result = ioctl(descriptor, request, argument); while (result < 0 && errno == EINTR);
    return result;
}

QString keyForNativeId(const quint32 nativeId)
{
    for (const auto &known : kKnownControls) {
        if (known.nativeId == nativeId) return QString::fromLatin1(known.id);
    }
    return QStringLiteral("v4l2:0x%1").arg(nativeId, 8, 16, QLatin1Char('0'));
}

bool nativeIdForKey(const QString &key, quint32 *nativeId)
{
    for (const auto &known : kKnownControls) {
        if (key.compare(QString::fromLatin1(known.id), Qt::CaseInsensitive) == 0) {
            *nativeId = known.nativeId;
            return true;
        }
    }
    const auto match = QRegularExpression(QStringLiteral("^v4l2:0x([0-9a-f]{1,8})$"),
                                          QRegularExpression::CaseInsensitiveOption).match(key);
    if (!match.hasMatch()) return false;
    bool ok = false;
    *nativeId = match.captured(1).toUInt(&ok, 16);
    return ok;
}

QString className(const quint32 id)
{
    switch (V4L2_CTRL_ID2WHICH(id)) {
    case V4L2_CTRL_CLASS_USER: return QStringLiteral("Image");
    case V4L2_CTRL_CLASS_CAMERA: return QStringLiteral("Camera");
    case V4L2_CTRL_CLASS_CODEC: return QStringLiteral("Codec");
    case V4L2_CTRL_CLASS_JPEG: return QStringLiteral("JPEG");
    case V4L2_CTRL_CLASS_FLASH: return QStringLiteral("Flash");
    case V4L2_CTRL_CLASS_IMAGE_SOURCE: return QStringLiteral("Image source");
    case V4L2_CTRL_CLASS_IMAGE_PROC: return QStringLiteral("Image processing");
    default: return QStringLiteral("Vendor / device");
    }
}

bool getValue(const int descriptor, const quint32 id, const quint32 type, qint64 *value)
{
    v4l2_ext_control item{};
    item.id = id;
    v4l2_ext_controls controls{};
    controls.which = V4L2_CTRL_ID2WHICH(id);
    controls.count = 1;
    controls.controls = &item;
    if (xioctl(descriptor, VIDIOC_G_EXT_CTRLS, &controls) == 0) {
        *value = type == V4L2_CTRL_TYPE_INTEGER64 ? item.value64 : item.value;
        return true;
    }
    v4l2_control legacy{};
    legacy.id = id;
    if (xioctl(descriptor, VIDIOC_G_CTRL, &legacy) != 0) return false;
    *value = legacy.value;
    return true;
}

bool setValue(const int descriptor, const quint32 id, const quint32 type, const qint64 value)
{
    v4l2_ext_control item{};
    item.id = id;
    if (type == V4L2_CTRL_TYPE_INTEGER64) item.value64 = value;
    else item.value = static_cast<qint32>(value);
    v4l2_ext_controls controls{};
    controls.which = V4L2_CTRL_ID2WHICH(id);
    controls.count = 1;
    controls.controls = &item;
    if (xioctl(descriptor, VIDIOC_S_EXT_CTRLS, &controls) == 0) return true;
    if (type == V4L2_CTRL_TYPE_INTEGER64) return false;
    v4l2_control legacy{};
    legacy.id = id;
    legacy.value = static_cast<qint32>(value);
    return xioctl(descriptor, VIDIOC_S_CTRL, &legacy) == 0;
}

CameraControlType typeFor(const quint32 nativeType)
{
    if (nativeType == V4L2_CTRL_TYPE_BOOLEAN) return CameraControlType::Boolean;
    if (nativeType == V4L2_CTRL_TYPE_MENU || nativeType == V4L2_CTRL_TYPE_INTEGER_MENU)
        return CameraControlType::Menu;
    if (nativeType == V4L2_CTRL_TYPE_BUTTON) return CameraControlType::Button;
    return CameraControlType::Integer;
}

QByteArray pipeWireV4l2Path(const QByteArray &deviceId, const QString &deviceName)
{
    QProcess process;
    process.start(QStringLiteral("pw-dump"), {});
    if (!process.waitForStarted(500) || !process.waitForFinished(1500)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }
    const auto document = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (!document.isArray()) return {};
    const QString qtId = QString::fromUtf8(deviceId);
    for (const auto &entry : document.array()) {
        const auto props = entry.toObject().value(QStringLiteral("info")).toObject()
                               .value(QStringLiteral("props")).toObject();
        if (!props.value(QStringLiteral("media.class")).toString().startsWith(QStringLiteral("Video/Source")))
            continue;
        const QString description = props.value(QStringLiteral("node.description")).toString();
        const QString nick = props.value(QStringLiteral("node.nick")).toString();
        const QString nodeName = props.value(QStringLiteral("node.name")).toString();
        const QString objectId = props.value(QStringLiteral("object.id")).toVariant().toString();
        const bool matches = description.contains(deviceName, Qt::CaseInsensitive)
            || nick.contains(deviceName, Qt::CaseInsensitive)
            || qtId.contains(nodeName, Qt::CaseInsensitive)
            || (!objectId.isEmpty() && qtId.contains(objectId));
        if (!matches) continue;
        const QString path = props.value(QStringLiteral("api.v4l2.path")).toString();
        if (path.startsWith(QStringLiteral("/dev/video"))) return path.toUtf8();
    }
    return {};
}
} // namespace

class LinuxControlBackend final : public CameraControlBackend {
public:
    ~LinuxControlBackend() override { close(); }

    bool open(const QByteArray &deviceId, const QString &deviceName, QString *error) override
    {
        if (descriptor_ >= 0 && deviceId == originalDeviceId_) return true;
        close();
        QByteArray path = deviceId;
        const auto match = QRegularExpression(QStringLiteral("(/dev/video[0-9]+)"))
                               .match(QString::fromUtf8(deviceId));
        if (match.hasMatch()) path = match.captured(1).toUtf8();
        else if (!path.startsWith("/dev/video")) {
            const QByteArray resolved = pipeWireV4l2Path(deviceId, deviceName);
            if (!resolved.isEmpty()) path = resolved;
        }
        descriptor_ = ::open(path.constData(), O_RDWR | O_NONBLOCK);
        if (descriptor_ < 0) {
            setError(error, systemError(QStringLiteral("Could not open %1").arg(QString::fromUtf8(path))));
            return false;
        }
        originalDeviceId_ = deviceId;
        devicePath_ = path;
        return true;
    }

    void close() override
    {
        if (descriptor_ >= 0) ::close(descriptor_);
        descriptor_ = -1;
        originalDeviceId_.clear();
        devicePath_.clear();
    }

    [[nodiscard]] bool isOpen() const override { return descriptor_ >= 0; }

    [[nodiscard]] QVector<CameraControl> controls(QString *error) const override
    {
        QVector<CameraControl> result;
        if (descriptor_ < 0) {
            setError(error, QStringLiteral("No camera is open."));
            return result;
        }

        v4l2_query_ext_ctrl query{};
        query.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
        while (xioctl(descriptor_, VIDIOC_QUERY_EXT_CTRL, &query) == 0) {
            const quint32 currentId = query.id;
            const bool unsupportedType = query.type == V4L2_CTRL_TYPE_CTRL_CLASS
                || query.type == V4L2_CTRL_TYPE_STRING
                || query.type == V4L2_CTRL_TYPE_U8
                || query.type == V4L2_CTRL_TYPE_U16
                || query.type == V4L2_CTRL_TYPE_U32
                || query.dims[0] != 0;
            if (!(query.flags & V4L2_CTRL_FLAG_DISABLED) && !unsupportedType) {
                qint64 current = query.default_value;
                const bool readable = query.type == V4L2_CTRL_TYPE_BUTTON
                    || getValue(descriptor_, currentId, query.type, &current);
                if (readable) {
                    CameraControl control{
                        keyForNativeId(currentId),
                        QString::fromUtf8(reinterpret_cast<const char *>(query.name)),
                        className(currentId), typeFor(query.type),
                        query.minimum, query.maximum, query.step, query.default_value, current,
                        {}, false, false,
                        (query.flags & (V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_INACTIVE)) != 0,
                    };
                    if (control.type == CameraControlType::Menu) {
                        for (qint64 index = query.minimum; index <= query.maximum; ++index) {
                            v4l2_querymenu menu{};
                            menu.id = currentId;
                            menu.index = static_cast<quint32>(index);
                            if (xioctl(descriptor_, VIDIOC_QUERYMENU, &menu) == 0) {
                                control.options.push_back({
                                    index, // Both menu types are set using the item index.
                                    query.type == V4L2_CTRL_TYPE_INTEGER_MENU
                                        ? QString::number(menu.value)
                                        : QString::fromUtf8(reinterpret_cast<const char *>(menu.name)),
                                });
                            }
                        }
                    }
                    result.push_back(std::move(control));
                }
            }
            query = {};
            query.id = currentId | V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
        }
        if (error) error->clear();
        return result;
    }

    bool setControl(const CameraControlValue &requested, CameraControlValue *accepted,
                    QString *error) override
    {
        if (descriptor_ < 0) {
            setError(error, QStringLiteral("No camera is open."));
            return false;
        }
        quint32 nativeId = 0;
        if (!nativeIdForKey(requested.id, &nativeId)) {
            setError(error, QStringLiteral("Unknown V4L2 control %1.").arg(requested.id));
            return false;
        }
        v4l2_query_ext_ctrl query{};
        query.id = nativeId;
        if (xioctl(descriptor_, VIDIOC_QUERY_EXT_CTRL, &query) != 0) {
            setError(error, systemError(QStringLiteral("Could not query %1").arg(requested.id)));
            return false;
        }
        if (!setValue(descriptor_, nativeId, query.type, requested.value)) {
            setError(error, systemError(QStringLiteral("Could not set %1").arg(requested.id)));
            return false;
        }
        qint64 actual = requested.value;
        if (query.type != V4L2_CTRL_TYPE_BUTTON
            && !getValue(descriptor_, nativeId, query.type, &actual)) {
            setError(error, systemError(QStringLiteral("Set %1 but could not verify it").arg(requested.id)));
            return false;
        }
        if (accepted) *accepted = {requested.id, actual, false};
        if (error) error->clear();
        return true;
    }

private:
    int descriptor_{-1};
    QByteArray originalDeviceId_;
    QByteArray devicePath_;
};

std::unique_ptr<CameraControlBackend> createPlatformBackend()
{
    return std::make_unique<LinuxControlBackend>();
}
