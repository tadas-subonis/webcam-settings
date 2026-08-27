#include "control_backend.h"

namespace {
void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}
} // namespace

class MacOSControlBackend final : public CameraControlBackend {
public:
    bool open(const QByteArray &deviceId, const QString &deviceName, QString *error) override
    {
        open_ = !deviceId.isEmpty() || !deviceName.isEmpty();
        if (error) {
            error->clear();
        }
        if (!open_) {
            setError(error, QStringLiteral("No macOS camera was selected."));
        }
        return open_;
    }

    void close() override { open_ = false; }
    [[nodiscard]] bool isOpen() const override { return open_; }

    [[nodiscard]] QVector<CameraControl> controls(QString *error) const override
    {
        if (error) {
            error->clear();
        }
        if (!open_) {
            setError(error, QStringLiteral("No camera is open."));
        }
        // AVFoundation's macOS API does not expose the physical zoom, focus,
        // or general UVC controls available in its iOS API. Qt still provides
        // camera discovery and preview; unsupported settings stay hidden.
        return {};
    }

    bool setControl(const CameraControlValue &requested, CameraControlValue *, QString *error) override
    {
        setError(error, QStringLiteral("%1 is not exposed by AVFoundation on macOS.")
                            .arg(requested.id));
        return false;
    }

private:
    bool open_{};
};

std::unique_ptr<CameraControlBackend> createPlatformBackend()
{
    return std::make_unique<MacOSControlBackend>();
}
