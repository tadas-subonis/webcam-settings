#include "camera_usage.h"

#include <QCoreApplication>

#include <limits>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <cerrno>
#include <csignal>
#include <cstring>

QVector<CameraUser> CameraUsageInspector::users(const QByteArray &deviceId, QString *error)
{
    if (error) {
        error->clear();
    }
    const QString id = QString::fromUtf8(deviceId);
    const auto match = QRegularExpression(QStringLiteral("(/dev/video[0-9]+)"),
                                           QRegularExpression::CaseInsensitiveOption).match(id);
    if (!match.hasMatch()) {
        if (error) {
            *error = QStringLiteral("Could not map this camera to a /dev/video device.");
        }
        return {};
    }
    const QString device = QFileInfo(match.captured(1)).canonicalFilePath();
    QVector<CameraUser> output;
    QSet<qint64> seen;
    const QDir proc(QStringLiteral("/proc"));
    for (const QString &pidText : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool valid = false;
        const qint64 pid = pidText.toLongLong(&valid);
        if (!valid || pid == QCoreApplication::applicationPid()) {
            continue;
        }
        const QDir fds(QStringLiteral("/proc/%1/fd").arg(pid));
        bool owns = false;
        for (const QString &fd : fds.entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
            if (QFileInfo(fds.filePath(fd)).symLinkTarget() == device) {
                owns = true;
                break;
            }
        }
        if (!owns || seen.contains(pid)) {
            continue;
        }
        seen.insert(pid);
        const QString executable = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
        QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
        QString name;
        if (comm.open(QIODevice::ReadOnly | QIODevice::Text)) {
            name = QString::fromUtf8(comm.readAll()).trimmed();
        }
        output.push_back({pid, name.isEmpty() ? QFileInfo(executable).fileName() : name,
                          executable, QStringLiteral("Open file descriptor for %1").arg(device)});
    }
    return output;
}

bool CameraUsageInspector::terminate(const qint64 pid, QString *error)
{
    if (error) {
        error->clear();
    }
    if (pid <= 0 || pid > std::numeric_limits<pid_t>::max()
        || pid == QCoreApplication::applicationPid()) {
        if (error) *error = QStringLiteral("Refusing to terminate an invalid or current-process PID.");
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), SIGTERM) == 0) {
        return true;
    }
    if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
}
