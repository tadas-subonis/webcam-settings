#pragma once

#include <QString>
#include <QVector>

enum class CameraControlType {
    Integer,
    Boolean,
    Menu,
    Button,
};

struct CameraControlOption {
    qint64 value{};
    QString label;
};

struct CameraControl {
    QString id;
    QString name;
    QString category;
    CameraControlType type{CameraControlType::Integer};
    qint64 minimum{};
    qint64 maximum{};
    qint64 step{1};
    qint64 defaultValue{};
    qint64 value{};
    QVector<CameraControlOption> options;
    bool supportsAuto{false};
    bool automatic{false};
    bool readOnly{false};

    [[nodiscard]] qint64 normalizedStep() const;
    [[nodiscard]] qint64 clamped(qint64 candidate) const;
    [[nodiscard]] QString displayValue(qint64 candidate) const;
};

struct CameraControlValue {
    QString id;
    qint64 value{};
    bool automatic{false};
};

[[nodiscard]] QString cameraControlTypeKey(CameraControlType type);
[[nodiscard]] QString legacyControlDisplayName(const QString &id);
[[nodiscard]] QString normalizedControlId(const QString &id);
