#include "camera_control.h"

#include <algorithm>
#include <array>

namespace {
struct ControlName {
    CameraControlId id;
    const char *key;
    const char *label;
};

constexpr std::array<ControlName, 17> kNames{{
    {CameraControlId::Pan, "pan", "Pan"},
    {CameraControlId::Tilt, "tilt", "Tilt"},
    {CameraControlId::Roll, "roll", "Roll"},
    {CameraControlId::Zoom, "zoom", "Zoom"},
    {CameraControlId::Exposure, "exposure", "Exposure"},
    {CameraControlId::Iris, "iris", "Iris"},
    {CameraControlId::Focus, "focus", "Focus"},
    {CameraControlId::Brightness, "brightness", "Brightness"},
    {CameraControlId::Contrast, "contrast", "Contrast"},
    {CameraControlId::Hue, "hue", "Hue"},
    {CameraControlId::Saturation, "saturation", "Saturation"},
    {CameraControlId::Sharpness, "sharpness", "Sharpness"},
    {CameraControlId::Gamma, "gamma", "Gamma"},
    {CameraControlId::ColorEnable, "color-enable", "Color enable"},
    {CameraControlId::WhiteBalance, "white-balance", "White balance"},
    {CameraControlId::BacklightCompensation, "backlight-compensation", "Backlight compensation"},
    {CameraControlId::Gain, "gain", "Gain"},
}};
} // namespace

int CameraControl::normalizedStep() const
{
    return std::max(1, step);
}

int CameraControl::clamped(const int candidate) const
{
    const int bounded = std::clamp(candidate, minimum, maximum);
    const int offset = bounded - minimum;
    return minimum + (offset / normalizedStep()) * normalizedStep();
}

QString controlKey(const CameraControlId id)
{
    for (const auto &entry : kNames) {
        if (entry.id == id) {
            return QString::fromLatin1(entry.key);
        }
    }
    return {};
}

QString controlDisplayName(const CameraControlId id)
{
    for (const auto &entry : kNames) {
        if (entry.id == id) {
            return QString::fromLatin1(entry.label);
        }
    }
    return QStringLiteral("Unknown");
}

bool controlIdFromKey(const QString &key, CameraControlId *id)
{
    for (const auto &entry : kNames) {
        if (key.compare(QString::fromLatin1(entry.key), Qt::CaseInsensitive) == 0) {
            if (id) {
                *id = entry.id;
            }
            return true;
        }
    }
    return false;
}

