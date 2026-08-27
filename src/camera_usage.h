#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct CameraUser {
    qint64 pid{};
    QString name;
    QString executable;
    QString detail;
};

class CameraUsageInspector final {
public:
    [[nodiscard]] static QVector<CameraUser> users(const QByteArray &deviceId, QString *error = nullptr);
    [[nodiscard]] static bool terminate(qint64 pid, QString *error = nullptr);
};
