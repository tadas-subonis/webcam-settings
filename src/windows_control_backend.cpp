#include "control_backend.h"

#include <dshow.h>
#include <windows.h>

#include <array>

namespace {
struct Mapping {
    CameraControlId id;
    bool cameraControl;
    long nativeId;
};

constexpr std::array<Mapping, 17> kMappings{{
    {CameraControlId::Pan, true, CameraControl_Pan},
    {CameraControlId::Tilt, true, CameraControl_Tilt},
    {CameraControlId::Roll, true, CameraControl_Roll},
    {CameraControlId::Zoom, true, CameraControl_Zoom},
    {CameraControlId::Exposure, true, CameraControl_Exposure},
    {CameraControlId::Iris, true, CameraControl_Iris},
    {CameraControlId::Focus, true, CameraControl_Focus},
    {CameraControlId::Brightness, false, VideoProcAmp_Brightness},
    {CameraControlId::Contrast, false, VideoProcAmp_Contrast},
    {CameraControlId::Hue, false, VideoProcAmp_Hue},
    {CameraControlId::Saturation, false, VideoProcAmp_Saturation},
    {CameraControlId::Sharpness, false, VideoProcAmp_Sharpness},
    {CameraControlId::Gamma, false, VideoProcAmp_Gamma},
    {CameraControlId::ColorEnable, false, VideoProcAmp_ColorEnable},
    {CameraControlId::WhiteBalance, false, VideoProcAmp_WhiteBalance},
    {CameraControlId::BacklightCompensation, false, VideoProcAmp_BacklightCompensation},
    {CameraControlId::Gain, false, VideoProcAmp_Gain},
}};

QString hresultMessage(const QString &operation, const HRESULT result)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(result), 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const QString detail = length > 0
        ? QString::fromWCharArray(buffer, static_cast<qsizetype>(length)).trimmed()
        : QStringLiteral("HRESULT 0x%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
    if (buffer) {
        LocalFree(buffer);
    }
    return QStringLiteral("%1: %2").arg(operation, detail);
}

void setError(QString *target, const QString &message)
{
    if (target) {
        *target = message;
    }
}

template<typename T>
void releaseCom(T *&value)
{
    if (value) {
        value->Release();
        value = nullptr;
    }
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
} // namespace

class WindowsControlBackend final : public CameraControlBackend {
public:
    WindowsControlBackend()
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        shouldUninitialize_ = result == S_OK || result == S_FALSE;
    }

    ~WindowsControlBackend() override
    {
        close();
        if (shouldUninitialize_) {
            CoUninitialize();
        }
    }

    bool open(const QByteArray &deviceId, const QString &deviceName, QString *error) override
    {
        close();

        ICreateDevEnum *deviceEnumerator = nullptr;
        HRESULT result = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&deviceEnumerator));
        if (FAILED(result)) {
            setError(error, hresultMessage(QStringLiteral("Could not create the video device enumerator"), result));
            return false;
        }

        IEnumMoniker *monikerEnumerator = nullptr;
        result = deviceEnumerator->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,
                                                         &monikerEnumerator, 0);
        releaseCom(deviceEnumerator);
        if (result != S_OK || !monikerEnumerator) {
            setError(error, QStringLiteral("Windows reported no DirectShow video devices."));
            return false;
        }

        IBaseFilter *fallback = nullptr;
        IMoniker *moniker = nullptr;
        while (monikerEnumerator->Next(1, &moniker, nullptr) == S_OK) {
            QString friendlyName;
            QString devicePath;
            IPropertyBag *propertyBag = nullptr;
            if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&propertyBag)))) {
                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(propertyBag->Read(L"FriendlyName", &value, nullptr)) && value.vt == VT_BSTR) {
                    friendlyName = QString::fromWCharArray(value.bstrVal);
                }
                VariantClear(&value);
                VariantInit(&value);
                if (SUCCEEDED(propertyBag->Read(L"DevicePath", &value, nullptr)) && value.vt == VT_BSTR) {
                    devicePath = QString::fromWCharArray(value.bstrVal);
                }
                VariantClear(&value);
                releaseCom(propertyBag);
            }

            IBaseFilter *candidate = nullptr;
            if (SUCCEEDED(moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&candidate)))) {
                const QString qtId = QString::fromUtf8(deviceId);
                const bool idMatches = !devicePath.isEmpty()
                    && (qtId.contains(devicePath, Qt::CaseInsensitive)
                        || devicePath.contains(qtId, Qt::CaseInsensitive));
                if (idMatches || friendlyName == deviceName) {
                    filter_ = candidate;
                    moniker->Release();
                    break;
                }
                if (!fallback && friendlyName.compare(deviceName, Qt::CaseInsensitive) == 0) {
                    fallback = candidate;
                } else {
                    candidate->Release();
                }
            }
            moniker->Release();
            moniker = nullptr;
        }

        releaseCom(monikerEnumerator);
        if (!filter_ && fallback) {
            filter_ = fallback;
            fallback = nullptr;
        }
        releaseCom(fallback);

        if (!filter_) {
            setError(error, QStringLiteral("Could not match “%1” to a DirectShow camera.").arg(deviceName));
            return false;
        }
        return true;
    }

    void close() override
    {
        releaseCom(filter_);
    }

    [[nodiscard]] bool isOpen() const override
    {
        return filter_ != nullptr;
    }

    [[nodiscard]] QVector<CameraControl> controls(QString *error) const override
    {
        QVector<CameraControl> result;
        if (!filter_) {
            setError(error, QStringLiteral("No camera is open."));
            return result;
        }

        IAMCameraControl *cameraControl = nullptr;
        IAMVideoProcAmp *videoProcAmp = nullptr;
        filter_->QueryInterface(IID_PPV_ARGS(&cameraControl));
        filter_->QueryInterface(IID_PPV_ARGS(&videoProcAmp));

        for (const auto &mapping : kMappings) {
            long minimum = 0;
            long maximum = 0;
            long step = 0;
            long defaultValue = 0;
            long capabilities = 0;
            long value = 0;
            long flags = 0;
            HRESULT rangeResult = E_NOINTERFACE;
            HRESULT valueResult = E_NOINTERFACE;

            if (mapping.cameraControl && cameraControl) {
                rangeResult = cameraControl->GetRange(mapping.nativeId, &minimum, &maximum, &step,
                                                      &defaultValue, &capabilities);
                valueResult = cameraControl->Get(mapping.nativeId, &value, &flags);
            } else if (!mapping.cameraControl && videoProcAmp) {
                rangeResult = videoProcAmp->GetRange(mapping.nativeId, &minimum, &maximum, &step,
                                                     &defaultValue, &capabilities);
                valueResult = videoProcAmp->Get(mapping.nativeId, &value, &flags);
            }

            if (SUCCEEDED(rangeResult) && SUCCEEDED(valueResult)) {
                result.push_back({
                    mapping.id,
                    controlDisplayName(mapping.id),
                    static_cast<int>(minimum),
                    static_cast<int>(maximum),
                    static_cast<int>(step),
                    static_cast<int>(defaultValue),
                    static_cast<int>(value),
                    (capabilities & CameraControl_Flags_Auto) != 0,
                    (flags & CameraControl_Flags_Auto) != 0,
                });
            }
        }

        releaseCom(cameraControl);
        releaseCom(videoProcAmp);
        return result;
    }

    bool setControl(const CameraControlId id, const int value, const bool automatic,
                    QString *error) override
    {
        if (!filter_) {
            setError(error, QStringLiteral("No camera is open."));
            return false;
        }
        const auto *mapping = mappingFor(id);
        if (!mapping) {
            setError(error, QStringLiteral("Unknown camera control."));
            return false;
        }

        const long flags = automatic ? CameraControl_Flags_Auto : CameraControl_Flags_Manual;
        HRESULT result = E_NOINTERFACE;
        if (mapping->cameraControl) {
            IAMCameraControl *control = nullptr;
            if (SUCCEEDED(filter_->QueryInterface(IID_PPV_ARGS(&control)))) {
                result = control->Set(mapping->nativeId, value, flags);
                releaseCom(control);
            }
        } else {
            IAMVideoProcAmp *control = nullptr;
            if (SUCCEEDED(filter_->QueryInterface(IID_PPV_ARGS(&control)))) {
                result = control->Set(mapping->nativeId, value, flags);
                releaseCom(control);
            }
        }

        if (FAILED(result)) {
            setError(error, hresultMessage(QStringLiteral("Could not set %1").arg(controlDisplayName(id)), result));
            return false;
        }
        return true;
    }

private:
    IBaseFilter *filter_{};
    bool shouldUninitialize_{false};
};

std::unique_ptr<CameraControlBackend> createPlatformBackend()
{
    return std::make_unique<WindowsControlBackend>();
}

