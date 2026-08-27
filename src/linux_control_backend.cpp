#include "control_backend.h"

#include <linux/videodev2.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <QRegularExpression>

#include <algorithm>
#include <array>

namespace {
enum class AutoKind { None, Boolean, ExposureMenu };

struct Mapping {
    CameraControlId id;
    quint32 control;
    quint32 autoControl;
    AutoKind autoKind;
};

constexpr std::array<Mapping, 17> kMappings{{
    {CameraControlId::Pan, V4L2_CID_PAN_ABSOLUTE, 0, AutoKind::None},
    {CameraControlId::Tilt, V4L2_CID_TILT_ABSOLUTE, 0, AutoKind::None},
    {CameraControlId::Roll, V4L2_CID_ROTATE, 0, AutoKind::None},
    {CameraControlId::Zoom, V4L2_CID_ZOOM_ABSOLUTE, 0, AutoKind::None},
    {CameraControlId::Exposure, V4L2_CID_EXPOSURE_ABSOLUTE, V4L2_CID_EXPOSURE_AUTO, AutoKind::ExposureMenu},
    {CameraControlId::Iris, V4L2_CID_IRIS_ABSOLUTE, 0, AutoKind::None},
    {CameraControlId::Focus, V4L2_CID_FOCUS_ABSOLUTE, V4L2_CID_FOCUS_AUTO, AutoKind::Boolean},
    {CameraControlId::Brightness, V4L2_CID_BRIGHTNESS, 0, AutoKind::None},
    {CameraControlId::Contrast, V4L2_CID_CONTRAST, 0, AutoKind::None},
    {CameraControlId::Hue, V4L2_CID_HUE, V4L2_CID_HUE_AUTO, AutoKind::Boolean},
    {CameraControlId::Saturation, V4L2_CID_SATURATION, 0, AutoKind::None},
    {CameraControlId::Sharpness, V4L2_CID_SHARPNESS, 0, AutoKind::None},
    {CameraControlId::Gamma, V4L2_CID_GAMMA, 0, AutoKind::None},
    {CameraControlId::ColorEnable, V4L2_CID_COLORFX, 0, AutoKind::None},
    {CameraControlId::WhiteBalance, V4L2_CID_WHITE_BALANCE_TEMPERATURE,
     V4L2_CID_AUTO_WHITE_BALANCE, AutoKind::Boolean},
    {CameraControlId::BacklightCompensation, V4L2_CID_BACKLIGHT_COMPENSATION, 0, AutoKind::None},
    {CameraControlId::Gain, V4L2_CID_GAIN, V4L2_CID_AUTOGAIN, AutoKind::Boolean},
}};

void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}

QString systemError(const QString &operation)
{
    return QStringLiteral("%1: %2").arg(operation, QString::fromLocal8Bit(std::strerror(errno)));
}

int xioctl(const int descriptor, const unsigned long request, void *argument)
{
    int result;
    do {
        result = ioctl(descriptor, request, argument);
    } while (result < 0 && errno == EINTR);
    return result;
}

const Mapping *mappingFor(const CameraControlId id)
{
    for (const auto &mapping : kMappings) {
        if (mapping.id == id) {
            return &mapping;
        }
    }
    return nullptr;
}

bool queryControl(const int descriptor, const quint32 id, v4l2_queryctrl *query)
{
    *query = {};
    query->id = id;
    return xioctl(descriptor, VIDIOC_QUERYCTRL, query) == 0
        && (query->flags & V4L2_CTRL_FLAG_DISABLED) == 0;
}

bool getValue(const int descriptor, const quint32 id, int *value)
{
    v4l2_control control{};
    control.id = id;
    if (xioctl(descriptor, VIDIOC_G_CTRL, &control) != 0) {
        return false;
    }
    *value = control.value;
    return true;
}

bool setValue(const int descriptor, const quint32 id, const int value)
{
    v4l2_control control{};
    control.id = id;
    control.value = value;
    return xioctl(descriptor, VIDIOC_S_CTRL, &control) == 0;
}
} // namespace

class LinuxControlBackend final : public CameraControlBackend {
public:
    ~LinuxControlBackend() override { close(); }

    bool open(const QByteArray &deviceId, const QString &, QString *error) override
    {
        close();
        QByteArray path = deviceId;
        const auto match = QRegularExpression(QStringLiteral("(/dev/video[0-9]+)"))
                               .match(QString::fromUtf8(deviceId));
        if (match.hasMatch()) {
            path = match.captured(1).toUtf8();
        }
        descriptor_ = ::open(path.constData(), O_RDWR | O_NONBLOCK);
        if (descriptor_ < 0) {
            setError(error, systemError(QStringLiteral("Could not open %1").arg(QString::fromUtf8(path))));
            return false;
        }
        devicePath_ = path;
        return true;
    }

    void close() override
    {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
        devicePath_.clear();
    }

    [[nodiscard]] bool isOpen() const override { return descriptor_ >= 0; }

    [[nodiscard]] QVector<CameraControl> controls(QString *error) const override
    {
        QVector<CameraControl> result;
        if (descriptor_ < 0) {
            setError(error, QStringLiteral("No camera is open."));
            return result;
        }

        for (const auto &mapping : kMappings) {
            v4l2_queryctrl query{};
            int current = 0;
            if (!queryControl(descriptor_, mapping.control, &query)
                || !getValue(descriptor_, mapping.control, &current)) {
                continue;
            }

            bool supportsAuto = false;
            bool automatic = false;
            if (mapping.autoKind != AutoKind::None && mapping.autoControl != 0) {
                v4l2_queryctrl autoQuery{};
                int autoValue = 0;
                supportsAuto = queryControl(descriptor_, mapping.autoControl, &autoQuery)
                    && getValue(descriptor_, mapping.autoControl, &autoValue);
                if (supportsAuto) {
                    automatic = mapping.autoKind == AutoKind::ExposureMenu
                        ? autoValue == V4L2_EXPOSURE_AUTO
                        : autoValue != 0;
                }
            }

            result.push_back({
                mapping.id,
                controlDisplayName(mapping.id),
                query.minimum,
                query.maximum,
                std::max(1, query.step),
                query.default_value,
                current,
                supportsAuto,
                automatic,
            });
        }
        return result;
    }

    bool setControl(const CameraControlId id, const int value, const bool automatic,
                    QString *error) override
    {
        if (descriptor_ < 0) {
            setError(error, QStringLiteral("No camera is open."));
            return false;
        }
        const auto *mapping = mappingFor(id);
        if (!mapping) {
            setError(error, QStringLiteral("Unknown camera control."));
            return false;
        }

        if (mapping->autoKind != AutoKind::None && mapping->autoControl != 0) {
            const int autoValue = mapping->autoKind == AutoKind::ExposureMenu
                ? (automatic ? V4L2_EXPOSURE_AUTO : V4L2_EXPOSURE_MANUAL)
                : (automatic ? 1 : 0);
            if (!setValue(descriptor_, mapping->autoControl, autoValue)) {
                setError(error, systemError(QStringLiteral("Could not change automatic %1")
                                                .arg(controlDisplayName(id))));
                return false;
            }
            if (automatic) {
                return true;
            }
        }

        if (!setValue(descriptor_, mapping->control, value)) {
            setError(error, systemError(QStringLiteral("Could not set %1").arg(controlDisplayName(id))));
            return false;
        }
        return true;
    }

private:
    int descriptor_{-1};
    QByteArray devicePath_;
};

std::unique_ptr<CameraControlBackend> createPlatformBackend()
{
    return std::make_unique<LinuxControlBackend>();
}
