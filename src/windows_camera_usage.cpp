#include "camera_usage.h"

#include <QCoreApplication>

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <memory>
#include <limits>

namespace {
struct RegistryCloser {
    void operator()(HKEY key) const { RegCloseKey(key); }
};
using RegistryKey = std::unique_ptr<std::remove_pointer_t<HKEY>, RegistryCloser>;

QString windowsError(const DWORD code)
{
    wchar_t *message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
    const QString result = length ? QString::fromWCharArray(message, static_cast<qsizetype>(length)).trimmed()
                                  : QStringLiteral("Windows error %1").arg(code);
    if (message) {
        LocalFree(message);
    }
    return result;
}

bool readQword(HKEY key, const wchar_t *name, quint64 *value)
{
    DWORD type = 0;
    DWORD size = sizeof(*value);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(value), &size) == ERROR_SUCCESS
        && type == REG_QWORD;
}

void collectActiveLeaves(HKEY key, const QString &path, QVector<QString> *active)
{
    quint64 start = 0;
    quint64 stop = 0;
    if (readQword(key, L"LastUsedTimeStart", &start) && start != 0
        && readQword(key, L"LastUsedTimeStop", &stop) && stop == 0) {
        active->push_back(path);
    }

    DWORD index = 0;
    wchar_t name[512];
    while (true) {
        DWORD nameLength = static_cast<DWORD>(std::size(name));
        const LONG result = RegEnumKeyExW(key, index++, name, &nameLength, nullptr, nullptr, nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (result != ERROR_SUCCESS) {
            continue;
        }
        HKEY childRaw = nullptr;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &childRaw) != ERROR_SUCCESS) {
            continue;
        }
        RegistryKey child(childRaw);
        const QString childName = QString::fromWCharArray(name, static_cast<qsizetype>(nameLength));
        collectActiveLeaves(child.get(), path.isEmpty() ? childName : path + QLatin1Char('\\') + childName,
                            active);
    }
}

QString decodeExecutable(const QString &registryPath)
{
    const QString marker = QStringLiteral("NonPackaged\\");
    const qsizetype position = registryPath.indexOf(marker, 0, Qt::CaseInsensitive);
    if (position < 0) {
        return {};
    }
    QString path = registryPath.mid(position + marker.size());
    path.replace(QLatin1Char('#'), QLatin1Char('\\'));
    return QDir::toNativeSeparators(path);
}

QVector<CameraUser> runningProcesses(const QSet<QString> &wantedPaths)
{
    QVector<CameraUser> output;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return output;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == static_cast<DWORD>(QCoreApplication::applicationPid())) {
                continue;
            }
            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) {
                continue;
            }
            wchar_t buffer[32768];
            DWORD length = static_cast<DWORD>(std::size(buffer));
            if (QueryFullProcessImageNameW(process, 0, buffer, &length)) {
                const QString executable = QDir::toNativeSeparators(
                    QString::fromWCharArray(buffer, static_cast<qsizetype>(length)));
                if (wantedPaths.contains(executable.toCaseFolded())) {
                    output.push_back({static_cast<qint64>(entry.th32ProcessID),
                                      QFileInfo(executable).completeBaseName(), executable,
                                      QStringLiteral("Windows camera privacy activity (Windows does not identify the physical camera)")});
                }
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return output;
}
} // namespace

QVector<CameraUser> CameraUsageInspector::users(const QByteArray &, QString *error)
{
    if (error) {
        error->clear();
    }
    constexpr auto keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam";
    HKEY rootRaw = nullptr;
    const LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &rootRaw);
    if (result == ERROR_FILE_NOT_FOUND) {
        return {};
    }
    if (result != ERROR_SUCCESS) {
        if (error) {
            *error = QStringLiteral("Could not inspect Windows camera activity: %1")
                         .arg(windowsError(static_cast<DWORD>(result)));
        }
        return {};
    }
    RegistryKey root(rootRaw);
    QVector<QString> activeLeaves;
    collectActiveLeaves(root.get(), {}, &activeLeaves);

    QSet<QString> paths;
    for (const QString &leaf : activeLeaves) {
        const QString executable = decodeExecutable(leaf);
        if (!executable.isEmpty()) {
            paths.insert(executable.toCaseFolded());
        }
    }
    return runningProcesses(paths);
}

bool CameraUsageInspector::terminate(const qint64 pid, QString *error)
{
    if (error) {
        error->clear();
    }
    if (pid <= 0 || pid > std::numeric_limits<DWORD>::max()
        || pid == QCoreApplication::applicationPid()) {
        if (error) {
            *error = QStringLiteral("Refusing to terminate an invalid or current-process PID.");
        }
        return false;
    }
    const HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        if (error) {
            *error = QStringLiteral("Could not open PID %1: %2").arg(pid).arg(windowsError(GetLastError()));
        }
        return false;
    }
    const bool terminated = TerminateProcess(process, 1) != FALSE;
    const DWORD failure = terminated ? ERROR_SUCCESS : GetLastError();
    if (terminated) {
        WaitForSingleObject(process, 3000);
    }
    CloseHandle(process);
    if (!terminated && error) {
        *error = QStringLiteral("Could not terminate PID %1: %2").arg(pid).arg(windowsError(failure));
    }
    return terminated;
}
