#include "control_backend.h"

#import <AVFoundation/AVFoundation.h>

#include <algorithm>
#include <cmath>

namespace {
void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}

QString nsErrorMessage(const QString &operation, NSError *error)
{
    if (!error) {
        return operation;
    }
    return QStringLiteral("%1: %2")
        .arg(operation, QString::fromUtf8(error.localizedDescription.UTF8String));
}
} // namespace

class MacOSControlBackend final : public CameraControlBackend {
public:
    ~MacOSControlBackend() override { close(); }

    bool open(const QByteArray &deviceId, const QString &deviceName, QString *error) override
    {
        close();
        @autoreleasepool {
            const QString requestedId = QString::fromUtf8(deviceId);
            NSArray<AVCaptureDevice *> *devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
            for (AVCaptureDevice *candidate in devices) {
                const QString uniqueId = QString::fromUtf8(candidate.uniqueID.UTF8String);
                const QString name = QString::fromUtf8(candidate.localizedName.UTF8String);
                if (uniqueId == requestedId || requestedId.contains(uniqueId) || name == deviceName) {
                    device_ = [candidate retain];
                    return true;
                }
            }
        }
        setError(error, QStringLiteral("Could not match “%1” to an AVFoundation device.").arg(deviceName));
        return false;
    }

    void close() override
    {
        if (device_) {
            [device_ release];
            device_ = nil;
        }
    }

    [[nodiscard]] bool isOpen() const override { return device_ != nil; }

    [[nodiscard]] QVector<CameraControl> controls(QString *error) const override
    {
        QVector<CameraControl> result;
        if (!device_) {
            setError(error, QStringLiteral("No camera is open."));
            return result;
        }

        @autoreleasepool {
            const CGFloat maximumZoom = device_.activeFormat.videoMaxZoomFactor;
            if (maximumZoom > 1.001) {
                result.push_back({
                    CameraControlId::Zoom,
                    controlDisplayName(CameraControlId::Zoom),
                    100,
                    static_cast<int>(std::floor(maximumZoom * 100.0)),
                    1,
                    100,
                    static_cast<int>(std::lround(device_.videoZoomFactor * 100.0)),
                    false,
                    false,
                });
            }

            const bool supportsLocked = [device_ isFocusModeSupported:AVCaptureFocusModeLocked];
            const bool supportsContinuous =
                [device_ isFocusModeSupported:AVCaptureFocusModeContinuousAutoFocus];
            if (supportsLocked || supportsContinuous) {
                const bool automatic = device_.focusMode == AVCaptureFocusModeAutoFocus
                    || device_.focusMode == AVCaptureFocusModeContinuousAutoFocus;
                result.push_back({
                    CameraControlId::Focus,
                    controlDisplayName(CameraControlId::Focus),
                    0,
                    1000,
                    1,
                    500,
                    static_cast<int>(std::lround(device_.lensPosition * 1000.0)),
                    supportsContinuous,
                    automatic,
                });
            }
        }
        return result;
    }

    bool setControl(const CameraControlId id, const int value, const bool automatic,
                    QString *error) override
    {
        if (!device_) {
            setError(error, QStringLiteral("No camera is open."));
            return false;
        }

        @autoreleasepool {
            NSError *lockError = nil;
            if (![device_ lockForConfiguration:&lockError]) {
                setError(error, nsErrorMessage(QStringLiteral("Could not lock the camera"), lockError));
                return false;
            }

            bool supported = true;
            if (id == CameraControlId::Zoom) {
                const CGFloat maximum = device_.activeFormat.videoMaxZoomFactor;
                device_.videoZoomFactor = std::clamp(static_cast<CGFloat>(value) / 100.0,
                                                     static_cast<CGFloat>(1.0), maximum);
            } else if (id == CameraControlId::Focus) {
                if (automatic
                    && [device_ isFocusModeSupported:AVCaptureFocusModeContinuousAutoFocus]) {
                    device_.focusMode = AVCaptureFocusModeContinuousAutoFocus;
                } else if ([device_ isFocusModeSupported:AVCaptureFocusModeLocked]) {
                    const float position = std::clamp(value / 1000.0f, 0.0f, 1.0f);
                    [device_ setFocusModeLockedWithLensPosition:position completionHandler:nil];
                } else {
                    supported = false;
                }
            } else {
                supported = false;
            }
            [device_ unlockForConfiguration];

            if (!supported) {
                setError(error, QStringLiteral("%1 is not available through AVFoundation for this camera.")
                                    .arg(controlDisplayName(id)));
                return false;
            }
        }
        return true;
    }

private:
    AVCaptureDevice *device_{nil};
};

std::unique_ptr<CameraControlBackend> createPlatformBackend()
{
    return std::make_unique<MacOSControlBackend>();
}

