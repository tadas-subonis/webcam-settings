#include "windows_camera_rotation.h"
#include "windows_camera_rotation_p.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <shellapi.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {
void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

QString systemError(const DWORD code)
{
    wchar_t *buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                       | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const QString message = buffer ? QString::fromWCharArray(buffer).trimmed()
                                   : QStringLiteral("Windows error %1").arg(code);
    if (buffer) LocalFree(buffer);
    return message;
}

LSTATUS openKey(const QByteArray &deviceId, const REGSAM access, HKEY *key)
{
    const QString path = QString::fromUtf8(deviceId);
    // Limit this control to external USB video camera interfaces. Resolve the
    // interface through Configuration Manager rather than constructing registry paths.
    if (!path.startsWith(QStringLiteral("\\\\?\\usb#"), Qt::CaseInsensitive)
        || !path.contains(QStringLiteral("#{e5323777-f976-4f5b-9b55-b94699c46e44}\\"),
                          Qt::CaseInsensitive)
        || path.contains(QChar::Null)) return ERROR_NOT_SUPPORTED;
    const CONFIGRET result = CM_Open_Device_Interface_KeyW(
        reinterpret_cast<LPCWSTR>(path.utf16()), access, RegDisposition_OpenExisting, key, 0);
    return result == CR_SUCCESS ? ERROR_SUCCESS : CM_MapCrToWin32Err(result, ERROR_GEN_FAILURE);
}

bool validDegrees(const qint64 degrees)
{
    return degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270;
}

LSTATUS writeRotation(const QByteArray &deviceId, const qint64 degrees)
{
    if (!validDegrees(degrees)) return ERROR_INVALID_PARAMETER;
    qint64 current = 0;
    if (WindowsCameraRotation::read(deviceId, &current) && current == degrees)
        return ERROR_SUCCESS;
    HKEY key = nullptr;
    LSTATUS result = openKey(deviceId, KEY_SET_VALUE | KEY_QUERY_VALUE, &key);
    if (result != ERROR_SUCCESS) return result;
    result = WindowsCameraRotation::detail::writeKey(key, degrees);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) return result;
    qint64 actual = -1;
    if (!WindowsCameraRotation::read(deviceId, &actual) || actual != degrees)
        return ERROR_WRITE_FAULT;
    return ERROR_SUCCESS;
}

// CommandLineToArgvW-compatible quoting, including trailing backslashes.
QString quoteArgument(const QString &argument)
{
    QString quoted = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar ch : argument) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
        } else {
            quoted += QString(backslashes * (ch == QLatin1Char('"') ? 2 : 1), QLatin1Char('\\'));
            if (ch == QLatin1Char('"')) quoted += QLatin1Char('\\');
            quoted += ch;
            backslashes = 0;
        }
    }
    return quoted + QString(backslashes * 2, QLatin1Char('\\')) + QLatin1Char('"');
}
} // namespace

LSTATUS WindowsCameraRotation::detail::readKey(HKEY key, qint64 *degrees)
{
    DWORD offset = 0;
    DWORD size = sizeof(offset);
    const LSTATUS result = RegGetValueW(key, nullptr, L"FSSensorOrientation", RRF_RT_REG_DWORD,
                                        nullptr, &offset, &size);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) return result;
    if (degrees) *degrees = validDegrees(offset) ? (360 - offset) % 360 : 0;
    return ERROR_SUCCESS;
}

LSTATUS WindowsCameraRotation::detail::writeKey(HKEY key, const qint64 degrees)
{
    if (!validDegrees(degrees)) return ERROR_INVALID_PARAMETER;
    // FSSensorOrientation declares a clockwise sensor offset. The pipeline
    // counter-rotates it: https://learn.microsoft.com/windows-hardware/drivers/stream/camera-device-orientation
    const DWORD offset = static_cast<DWORD>((360 - degrees) % 360);
    return RegSetValueExW(key, L"FSSensorOrientation", 0, REG_DWORD,
                          reinterpret_cast<const BYTE *>(&offset), sizeof(offset));
}

bool WindowsCameraRotation::read(const QByteArray &deviceId, qint64 *degrees, QString *error)
{
    HKEY key = nullptr;
    LSTATUS result = openKey(deviceId, KEY_QUERY_VALUE, &key);
    if (result == ERROR_SUCCESS) {
        result = detail::readKey(key, degrees);
        RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS) {
        setError(error, systemError(result));
        return false;
    }
    if (error) error->clear();
    return true;
}

bool WindowsCameraRotation::write(const QByteArray &deviceId, const qint64 degrees, QString *error)
{
    const LSTATUS result = writeRotation(deviceId, degrees);
    if (result != ERROR_SUCCESS) {
        setError(error, result == ERROR_ACCESS_DENIED
            ? QStringLiteral("Changing system camera rotation requires administrator access. "
                             "Run Webcam Settings as administrator, or change rotation in the desktop app.")
            : systemError(result));
        return false;
    }
    if (error) error->clear();
    return true;
}

bool WindowsCameraRotation::writeWithElevation(const QByteArray &deviceId, const qint64 degrees,
                                               QString *error)
{
    const LSTATUS result = writeRotation(deviceId, degrees);
    if (result != ERROR_ACCESS_DENIED) {
        if (result == ERROR_SUCCESS) {
            if (error) error->clear();
            return true;
        }
        setError(error, systemError(result));
        return false;
    }
    const QString executable = QDir::toNativeSeparators(
        QCoreApplication::applicationDirPath() + QStringLiteral("/webcam-settings-cli.exe"));
    if (!QFileInfo::exists(executable)) {
        setError(error, QStringLiteral("Rotation needs administrator access, but webcam-settings-cli.exe "
                                      "is missing. Keep it beside Webcam Settings or run the app as administrator."));
        return false;
    }
    const QString arguments = QStringLiteral("rotate %1 --device-id %2")
                                  .arg(degrees).arg(quoteArgument(QString::fromUtf8(deviceId)));
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = reinterpret_cast<LPCWSTR>(executable.utf16());
    info.lpParameters = reinterpret_cast<LPCWSTR>(arguments.utf16());
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        const DWORD code = GetLastError();
        setError(error, code == ERROR_CANCELLED
            ? QStringLiteral("Rotation was not changed because the administrator prompt was cancelled.")
            : systemError(code));
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(info.hProcess, 30000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0) GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    qint64 actual = -1;
    if (waitResult != WAIT_OBJECT_0 || exitCode != 0 || !read(deviceId, &actual) || actual != degrees) {
        setError(error, QStringLiteral("Windows could not verify the requested camera rotation. "
                                      "Refresh the controls and try again."));
        return false;
    }
    if (error) error->clear();
    return true;
}
