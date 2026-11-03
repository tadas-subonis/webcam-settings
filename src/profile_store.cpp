#include "profile_store.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <limits>

namespace {
void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}
} // namespace

bool ProfileStore::save(const QString &path,
                        const QByteArray &deviceId,
                        const QString &deviceName,
                        const QVector<CameraControlValue> &values,
                        QString *error)
{
    QJsonArray controls;
    for (const auto &value : values) {
        controls.append(QJsonObject{
            {QStringLiteral("control"), value.id},
            {QStringLiteral("value"), value.value},
            {QStringLiteral("auto"), value.automatic},
        });
    }

    const QJsonObject root{
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("deviceId"), QString::fromUtf8(deviceId.toBase64())},
        {QStringLiteral("deviceName"), deviceName},
        {QStringLiteral("controls"), controls},
    };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Could not open %1: %2").arg(path, file.errorString()));
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        setError(error, QStringLiteral("Could not save %1: %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

bool ProfileStore::load(const QString &path,
                        QVector<CameraControlValue> *values,
                        QString *deviceName,
                        QString *error)
{
    if (!values) {
        setError(error, QStringLiteral("No destination was provided for the profile."));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Could not open %1: %2").arg(path, file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Invalid profile: %1").arg(parseError.errorString()));
        return false;
    }

    const auto root = document.object();
    if (root.value(QStringLiteral("formatVersion")).toInt() != 1
        || !root.value(QStringLiteral("controls")).isArray()) {
        setError(error, QStringLiteral("Unsupported or incomplete profile format."));
        return false;
    }

    QVector<CameraControlValue> parsed;
    QSet<QString> seen;
    for (const auto &item : root.value(QStringLiteral("controls")).toArray()) {
        if (!item.isObject()) {
            setError(error, QStringLiteral("Invalid profile control entry."));
            return false;
        }
        const auto object = item.toObject();
        const QString id = normalizedControlId(object.value(QStringLiteral("control")).toString());
        const auto number = object.value(QStringLiteral("value"));
        const auto automatic = object.value(QStringLiteral("auto"));
        // Different fallbacks distinguish invalid/fractional numbers from every
        // valid qint64, including its minimum and maximum values.
        const qint64 value = number.toInteger(std::numeric_limits<qint64>::min());
        if (id.isEmpty() || seen.contains(id) || !number.isDouble()
            || value != number.toInteger(std::numeric_limits<qint64>::max())
            || (!automatic.isUndefined() && !automatic.isBool())) {
            setError(error, QStringLiteral("Invalid or duplicate profile control: %1.").arg(id));
            return false;
        }
        seen.insert(id);
        parsed.push_back({id, value, automatic.toBool(false)});
    }

    if (parsed.isEmpty()) {
        setError(error, QStringLiteral("The profile contains no recognized controls."));
        return false;
    }

    *values = std::move(parsed);
    if (deviceName) {
        *deviceName = root.value(QStringLiteral("deviceName")).toString();
    }
    return true;
}
