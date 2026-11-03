#include "camera_usage.h"

#include <QCoreApplication>

#include <limits>

#include <cerrno>
#include <csignal>
#include <cstring>

QVector<CameraUser> CameraUsageInspector::users(const QByteArray &, QString *error)
{
    if (error) {
        *error = QStringLiteral("macOS does not expose the process that currently owns an AVFoundation camera.");
    }
    return {};
}

bool CameraUsageInspector::terminate(const qint64 pid, QString *error)
{
    if (error) error->clear();
    if (pid <= 0 || pid > std::numeric_limits<pid_t>::max()
        || pid == QCoreApplication::applicationPid()) {
        if (error) *error = QStringLiteral("Refusing to terminate an invalid or current-process PID.");
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), SIGTERM) == 0) return true;
    if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
}
