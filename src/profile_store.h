#pragma once

#include "camera_control.h"

#include <QByteArray>
#include <QString>
#include <QVector>

class ProfileStore {
public:
    static bool save(const QString &path,
                     const QByteArray &deviceId,
                     const QString &deviceName,
                     const QVector<CameraControlValue> &values,
                     QString *error);

    static bool load(const QString &path,
                     QVector<CameraControlValue> *values,
                     QString *deviceName,
                     QString *error);
};

