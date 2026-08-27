#pragma once

#include <QString>
#include <QVector>

enum class CameraControlId {
    Pan,
    Tilt,
    Roll,
    Zoom,
    Exposure,
    Iris,
    Focus,
    Brightness,
    Contrast,
    Hue,
    Saturation,
    Sharpness,
    Gamma,
    ColorEnable,
    WhiteBalance,
    BacklightCompensation,
    Gain,
};

struct CameraControl {
    CameraControlId id{};
    QString name;
    int minimum{};
    int maximum{};
    int step{1};
    int defaultValue{};
    int value{};
    bool supportsAuto{false};
    bool automatic{false};

    [[nodiscard]] int normalizedStep() const;
    [[nodiscard]] int clamped(int candidate) const;
};

struct CameraControlValue {
    CameraControlId id{};
    int value{};
    bool automatic{false};
};

[[nodiscard]] QString controlKey(CameraControlId id);
[[nodiscard]] QString controlDisplayName(CameraControlId id);
[[nodiscard]] bool controlIdFromKey(const QString &key, CameraControlId *id);

