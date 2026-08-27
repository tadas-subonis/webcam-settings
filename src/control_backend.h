#pragma once

#include "camera_control.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <memory>

class CameraControlBackend {
public:
    virtual ~CameraControlBackend() = default;

    virtual bool open(const QByteArray &deviceId, const QString &deviceName, QString *error) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    [[nodiscard]] virtual QVector<CameraControl> controls(QString *error) const = 0;
    virtual bool setControl(const CameraControlValue &requested,
                            CameraControlValue *accepted,
                            QString *error) = 0;
};

[[nodiscard]] std::unique_ptr<CameraControlBackend> createPlatformBackend();
