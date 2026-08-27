#include "camera_control.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace {
const QHash<QString, QString> kLegacyNames{
    {QStringLiteral("pan"), QStringLiteral("Pan")},
    {QStringLiteral("tilt"), QStringLiteral("Tilt")},
    {QStringLiteral("roll"), QStringLiteral("Roll")},
    {QStringLiteral("zoom"), QStringLiteral("Zoom")},
    {QStringLiteral("exposure"), QStringLiteral("Exposure")},
    {QStringLiteral("iris"), QStringLiteral("Iris")},
    {QStringLiteral("focus"), QStringLiteral("Focus")},
    {QStringLiteral("brightness"), QStringLiteral("Brightness")},
    {QStringLiteral("contrast"), QStringLiteral("Contrast")},
    {QStringLiteral("hue"), QStringLiteral("Hue")},
    {QStringLiteral("saturation"), QStringLiteral("Saturation")},
    {QStringLiteral("sharpness"), QStringLiteral("Sharpness")},
    {QStringLiteral("gamma"), QStringLiteral("Gamma")},
    {QStringLiteral("color-enable"), QStringLiteral("Color enable")},
    {QStringLiteral("white-balance"), QStringLiteral("White balance")},
    {QStringLiteral("backlight-compensation"), QStringLiteral("Backlight compensation")},
    {QStringLiteral("gain"), QStringLiteral("Gain")},
};
} // namespace

qint64 CameraControl::normalizedStep() const
{
    return std::max<qint64>(1, step);
}

qint64 CameraControl::clamped(const qint64 candidate) const
{
    if (type == CameraControlType::Boolean) {
        return candidate == 0 ? 0 : 1;
    }
    if (type == CameraControlType::Menu && !options.isEmpty()) {
        const auto exact = std::find_if(options.cbegin(), options.cend(), [candidate](const auto &option) {
            return option.value == candidate;
        });
        return exact == options.cend() ? options.first().value : exact->value;
    }
    const qint64 bounded = std::clamp(candidate, minimum, maximum);
    return minimum + ((bounded - minimum) / normalizedStep()) * normalizedStep();
}

QString CameraControl::displayValue(const qint64 candidate) const
{
    if (type == CameraControlType::Boolean) {
        return candidate == 0 ? QStringLiteral("off") : QStringLiteral("on");
    }
    for (const auto &option : options) {
        if (option.value == candidate) {
            return option.label;
        }
    }
    return QString::number(candidate);
}

QString cameraControlTypeKey(const CameraControlType type)
{
    switch (type) {
    case CameraControlType::Integer: return QStringLiteral("integer");
    case CameraControlType::Boolean: return QStringLiteral("boolean");
    case CameraControlType::Menu: return QStringLiteral("menu");
    case CameraControlType::Button: return QStringLiteral("button");
    }
    return QStringLiteral("unknown");
}

QString legacyControlDisplayName(const QString &id)
{
    return kLegacyNames.value(id.toLower(), id);
}

QString normalizedControlId(const QString &id)
{
    QString result = id.trimmed().toLower();
    result.replace(QRegularExpression(QStringLiteral("[^a-z0-9_.:-]+")), QStringLiteral("-"));
    result.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return result;
}
