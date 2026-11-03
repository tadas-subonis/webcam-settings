#pragma once

#include <QByteArray>
#include <QString>

namespace WindowsCameraRotation {
inline constexpr auto controlId = "windows:rotation";

// Clockwise output rotation. Windows' sensor mounting offset uses the inverse.
[[nodiscard]] bool read(const QByteArray &deviceId, qint64 *degrees, QString *error = nullptr);
[[nodiscard]] bool write(const QByteArray &deviceId, qint64 degrees, QString *error = nullptr);
// GUI only: retry an access-denied write through the installed CLI with UAC.
[[nodiscard]] bool writeWithElevation(const QByteArray &deviceId, qint64 degrees, QString *error);
}
