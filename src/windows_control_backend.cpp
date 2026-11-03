#include "control_backend.h"
#include "windows_camera_rotation.h"

#include <dshow.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <mfapi.h>
#include <mfidl.h>
#include <windows.h>

#include <QByteArray>

#include <array>

namespace {
struct StandardSpec {
    const char *id;
    const char *name;
    bool cameraControl;
    long nativeId;
};

constexpr std::array<StandardSpec, 17> kStandardSpecs{{
    {"pan", "Pan", true, CameraControl_Pan},
    {"tilt", "Tilt", true, CameraControl_Tilt},
    {"roll", "Roll", true, CameraControl_Roll},
    {"zoom", "Zoom", true, CameraControl_Zoom},
    {"exposure", "Exposure", true, CameraControl_Exposure},
    {"iris", "Iris", true, CameraControl_Iris},
    {"focus", "Focus", true, CameraControl_Focus},
    {"brightness", "Brightness", false, VideoProcAmp_Brightness},
    {"contrast", "Contrast", false, VideoProcAmp_Contrast},
    {"hue", "Hue", false, VideoProcAmp_Hue},
    {"saturation", "Saturation", false, VideoProcAmp_Saturation},
    {"sharpness", "Sharpness", false, VideoProcAmp_Sharpness},
    {"gamma", "Gamma", false, VideoProcAmp_Gamma},
    {"color-enable", "Color enable", false, VideoProcAmp_ColorEnable},
    {"white-balance", "White balance", false, VideoProcAmp_WhiteBalance},
    {"backlight-compensation", "Backlight compensation", false, VideoProcAmp_BacklightCompensation},
    {"gain", "Gain", false, VideoProcAmp_Gain},
}};

struct ExtendedSpec {
    ULONG propertyId;
    const char *id;
    const char *name;
    QVector<CameraControlOption> options;
};

const QVector<ExtendedSpec> &extendedSpecs()
{
    static const QVector<ExtendedSpec> specs{
        {KSPROPERTY_CAMERACONTROL_EXTENDED_FOCUSPRIORITY,
         "windows:extended:focus-priority", "Focus priority",
         {{KSCAMERA_EXTENDEDPROP_FOCUSPRIORITY_OFF, QStringLiteral("Off")},
          {KSCAMERA_EXTENDEDPROP_FOCUSPRIORITY_ON, QStringLiteral("On")}}},
        {KSPROPERTY_CAMERACONTROL_EXTENDED_VIDEOSTABILIZATION,
         "windows:extended:video-stabilization", "Video stabilization",
         {{KSCAMERA_EXTENDEDPROP_VIDEOSTABILIZATION_OFF, QStringLiteral("Off")},
          {KSCAMERA_EXTENDEDPROP_VIDEOSTABILIZATION_ON, QStringLiteral("On")},
          {KSCAMERA_EXTENDEDPROP_VIDEOSTABILIZATION_AUTO, QStringLiteral("Auto")}}},
        {KSPROPERTY_CAMERACONTROL_EXTENDED_VIDEOHDR,
         "windows:extended:video-hdr", "Video HDR",
         {{KSCAMERA_EXTENDEDPROP_VIDEOHDR_OFF, QStringLiteral("Off")},
          {KSCAMERA_EXTENDEDPROP_VIDEOHDR_ON, QStringLiteral("On")},
          {KSCAMERA_EXTENDEDPROP_VIDEOHDR_AUTO, QStringLiteral("Auto")}}},
        {KSPROPERTY_CAMERACONTROL_EXTENDED_EYEGAZECORRECTION,
         "windows:extended:eye-gaze-correction", "Eye-gaze correction",
         {{KSCAMERA_EXTENDEDPROP_EYEGAZECORRECTION_OFF, QStringLiteral("Off")},
          {KSCAMERA_EXTENDEDPROP_EYEGAZECORRECTION_ON, QStringLiteral("On")}}},
        {KSPROPERTY_CAMERACONTROL_EXTENDED_BACKGROUNDSEGMENTATION,
         "windows:extended:background-segmentation", "Background effect",
         {{KSCAMERA_EXTENDEDPROP_BACKGROUNDSEGMENTATION_OFF, QStringLiteral("Off")},
          {KSCAMERA_EXTENDEDPROP_BACKGROUNDSEGMENTATION_BLUR, QStringLiteral("Blur")},
          {KSCAMERA_EXTENDEDPROP_BACKGROUNDSEGMENTATION_MASK, QStringLiteral("Mask")}}},
        {KSPROPERTY_CAMERACONTROL_EXTENDED_DIGITALWINDOW,
         "windows:extended:auto-framing", "Automatic framing",
         {{KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_MANUAL, QStringLiteral("Off / manual")},
          {KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_AUTOFACEFRAMING, QStringLiteral("On")}}},
    };
    return specs;
}

constexpr GUID kExtendedCameraControlSet{
    0x1cb79112, 0xc0d2, 0x4213, {0x9c, 0xa6, 0xcd, 0x4f, 0xdb, 0x92, 0x79, 0x72}
};

void setError(QString *target, const QString &message)
{
    if (target) *target = message;
}

QString hresultMessage(const QString &operation, const HRESULT result)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(result), 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const QString detail = length > 0
        ? QString::fromWCharArray(buffer, static_cast<qsizetype>(length)).trimmed()
        : QStringLiteral("HRESULT 0x%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
    if (buffer) LocalFree(buffer);
    return QStringLiteral("%1: %2").arg(operation, detail);
}

template<typename T>
void releaseCom(T *&value)
{
    if (value) value->Release();
    value = nullptr;
}

const StandardSpec *standardSpec(const QString &id)
{
    for (const auto &spec : kStandardSpecs) {
        if (id.compare(QString::fromLatin1(spec.id), Qt::CaseInsensitive) == 0) return &spec;
    }
    return nullptr;
}

const ExtendedSpec *extendedSpec(const QString &id)
{
    for (const auto &spec : extendedSpecs()) {
        if (id.compare(QString::fromLatin1(spec.id), Qt::CaseInsensitive) == 0) return &spec;
    }
    return nullptr;
}

IKsControl *findKsControl(IBaseFilter *filter)
{
    IKsControl *control = nullptr;
    if (SUCCEEDED(filter->QueryInterface(__uuidof(IKsControl), reinterpret_cast<void **>(&control))))
        return control;
    IEnumPins *pins = nullptr;
    if (FAILED(filter->EnumPins(&pins))) return nullptr;
    IPin *pin = nullptr;
    while (pins->Next(1, &pin, nullptr) == S_OK) {
        if (SUCCEEDED(pin->QueryInterface(__uuidof(IKsControl), reinterpret_cast<void **>(&control)))) {
            pin->Release();
            break;
        }
        pin->Release();
    }
    pins->Release();
    return control;
}

IKsControl *findMediaFoundationKsControl(const QByteArray &deviceId, const QString &deviceName,
                                        IMFMediaSource **retainedSource)
{
    IMFAttributes *search = nullptr;
    if (FAILED(MFCreateAttributes(&search, 1))) return nullptr;
    search->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate **devices = nullptr;
    UINT32 count = 0;
    const HRESULT enumResult = MFEnumDeviceSources(search, &devices, &count);
    search->Release();
    if (FAILED(enumResult)) return nullptr;

    IKsControl *result = nullptr;
    const QString qtId = QString::fromUtf8(deviceId);
    for (UINT32 index = 0; index < count && !result; ++index) {
        WCHAR *name = nullptr;
        WCHAR *symbolicLink = nullptr;
        UINT32 nameLength = 0;
        UINT32 linkLength = 0;
        devices[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                           &name, &nameLength);
        devices[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                           &symbolicLink, &linkLength);
        const QString candidateName = name ? QString::fromWCharArray(name, nameLength) : QString();
        const QString candidateLink = symbolicLink
            ? QString::fromWCharArray(symbolicLink, linkLength) : QString();
        CoTaskMemFree(name);
        CoTaskMemFree(symbolicLink);
        const bool matches = candidateName.compare(deviceName, Qt::CaseInsensitive) == 0
            || (!candidateLink.isEmpty()
                && (qtId.contains(candidateLink, Qt::CaseInsensitive)
                    || candidateLink.contains(qtId, Qt::CaseInsensitive)));
        if (!matches) continue;

        IMFMediaSource *source = nullptr;
        if (FAILED(devices[index]->ActivateObject(IID_PPV_ARGS(&source)))) continue;
        IMFMediaSourceEx *sourceEx = nullptr;
        IMFAttributes *sourceAttributes = nullptr;
        if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&sourceEx)))
            && SUCCEEDED(sourceEx->GetSourceAttributes(&sourceAttributes))) {
            sourceAttributes->GetUnknown(MF_DEVICESTREAM_FILTER_KSCONTROL,
                                         __uuidof(IKsControl),
                                         reinterpret_cast<void **>(&result));
        }
        releaseCom(sourceAttributes);
        releaseCom(sourceEx);
        if (result) *retainedSource = source;
        else source->Release();
    }
    for (UINT32 index = 0; index < count; ++index) devices[index]->Release();
    CoTaskMemFree(devices);
    return result;
}

bool getExtended(IKsControl *control, const ULONG propertyId, QByteArray *data)
{
    if (!control) return false;
    KSPROPERTY request{kExtendedCameraControlSet, propertyId, KSPROPERTY_TYPE_GET};
    data->fill('\0', 4096);
    ULONG returned = 0;
    const HRESULT result = control->KsProperty(&request, sizeof(request), data->data(),
                                               static_cast<ULONG>(data->size()), &returned);
    if (FAILED(result) || returned < sizeof(KSCAMERA_EXTENDEDPROP_HEADER)) return false;
    const auto *header = reinterpret_cast<const KSCAMERA_EXTENDEDPROP_HEADER *>(data->constData());
    const ULONG size = header->Size >= sizeof(*header) && header->Size <= static_cast<ULONG>(data->size())
        ? header->Size : returned;
    data->resize(static_cast<qsizetype>(size));
    return true;
}

bool setExtended(IKsControl *control, const ULONG propertyId, QByteArray *data, QString *error)
{
    KSPROPERTY request{kExtendedCameraControlSet, propertyId, KSPROPERTY_TYPE_SET};
    ULONG returned = 0;
    const HRESULT result = control->KsProperty(&request, sizeof(request), data->data(),
                                               static_cast<ULONG>(data->size()), &returned);
    if (FAILED(result)) {
        setError(error, hresultMessage(QStringLiteral("Could not set extended camera control"), result));
        return false;
    }
    return true;
}

constexpr qint64 kQ24One = 0x01000000;

qint64 q24ToPercent(const LONG value)
{
    return (static_cast<qint64>(value) * 100 + (value >= 0 ? kQ24One / 2 : -kQ24One / 2))
        / kQ24One;
}

LONG percentToQ24(const qint64 value)
{
    return static_cast<LONG>((value * kQ24One) / 100);
}
} // namespace

class WindowsControlBackend final : public CameraControlBackend {
public:
    WindowsControlBackend()
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        shouldUninitialize_ = result == S_OK || result == S_FALSE;
        mediaFoundationStarted_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    }

    ~WindowsControlBackend() override
    {
        close();
        if (mediaFoundationStarted_) MFShutdown();
        if (shouldUninitialize_) CoUninitialize();
    }

    bool open(const QByteArray &deviceId, const QString &deviceName, QString *error) override
    {
        if (isOpen() && deviceId == deviceId_) return true;
        close();
        ICreateDevEnum *devices = nullptr;
        HRESULT result = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&devices));
        if (FAILED(result)) {
            setError(error, hresultMessage(QStringLiteral("Could not create the camera enumerator"), result));
            return false;
        }
        IEnumMoniker *items = nullptr;
        result = devices->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &items, 0);
        releaseCom(devices);
        if (result != S_OK || !items) {
            setError(error, QStringLiteral("Windows reported no DirectShow video devices."));
            return false;
        }

        const QString qtId = QString::fromUtf8(deviceId);
        IBaseFilter *fallback = nullptr;
        IMoniker *moniker = nullptr;
        while (items->Next(1, &moniker, nullptr) == S_OK) {
            QString friendlyName;
            QString devicePath;
            IPropertyBag *bag = nullptr;
            if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag)))) {
                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(bag->Read(L"FriendlyName", &value, nullptr)) && value.vt == VT_BSTR)
                    friendlyName = QString::fromWCharArray(value.bstrVal);
                VariantClear(&value);
                VariantInit(&value);
                if (SUCCEEDED(bag->Read(L"DevicePath", &value, nullptr)) && value.vt == VT_BSTR)
                    devicePath = QString::fromWCharArray(value.bstrVal);
                VariantClear(&value);
                releaseCom(bag);
            }
            IBaseFilter *candidate = nullptr;
            if (SUCCEEDED(moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&candidate)))) {
                const bool pathMatches = !devicePath.isEmpty()
                    && (qtId.contains(devicePath, Qt::CaseInsensitive)
                        || devicePath.contains(qtId, Qt::CaseInsensitive));
                if (pathMatches || friendlyName == deviceName) {
                    filter_ = candidate;
                    moniker->Release();
                    break;
                }
                if (!fallback && friendlyName.compare(deviceName, Qt::CaseInsensitive) == 0)
                    fallback = candidate;
                else candidate->Release();
            }
            moniker->Release();
            moniker = nullptr;
        }
        releaseCom(items);
        if (!filter_ && fallback) {
            filter_ = fallback;
            fallback = nullptr;
        }
        releaseCom(fallback);
        if (filter_) ksControl_ = findKsControl(filter_);
        if (!ksControl_ && mediaFoundationStarted_)
            ksControl_ = findMediaFoundationKsControl(deviceId, deviceName, &mediaSource_);
        if (!filter_ && !ksControl_) {
            setError(error, QStringLiteral("Could not match “%1” to a Windows camera.").arg(deviceName));
            return false;
        }
        deviceId_ = deviceId;
        deviceName_ = deviceName;
        if (error) error->clear();
        return true;
    }

    void close() override
    {
        releaseCom(ksControl_);
        if (mediaSource_) mediaSource_->Shutdown();
        releaseCom(mediaSource_);
        releaseCom(filter_);
        deviceId_.clear();
        deviceName_.clear();
    }

    [[nodiscard]] bool isOpen() const override { return filter_ != nullptr || ksControl_ != nullptr; }

    [[nodiscard]] QVector<CameraControl> controls(QString *error) const override
    {
        QVector<CameraControl> found;
        if (!filter_ && !ksControl_) {
            setError(error, QStringLiteral("No camera is open."));
            return found;
        }
        IAMCameraControl *camera = nullptr;
        IAMVideoProcAmp *image = nullptr;
        if (filter_) {
            filter_->QueryInterface(IID_PPV_ARGS(&camera));
            filter_->QueryInterface(IID_PPV_ARGS(&image));
        }
        for (const auto &spec : kStandardSpecs) {
            long minimum = 0, maximum = 0, step = 0, defaultValue = 0;
            long capabilities = 0, value = 0, flags = 0;
            HRESULT rangeResult = E_NOINTERFACE, getResult = E_NOINTERFACE;
            if (spec.cameraControl && camera) {
                rangeResult = camera->GetRange(spec.nativeId, &minimum, &maximum, &step,
                                               &defaultValue, &capabilities);
                getResult = camera->Get(spec.nativeId, &value, &flags);
            } else if (!spec.cameraControl && image) {
                rangeResult = image->GetRange(spec.nativeId, &minimum, &maximum, &step,
                                              &defaultValue, &capabilities);
                getResult = image->Get(spec.nativeId, &value, &flags);
            }
            if (SUCCEEDED(rangeResult) && SUCCEEDED(getResult)) {
                found.push_back({
                    QString::fromLatin1(spec.id), QString::fromLatin1(spec.name),
                    spec.cameraControl ? QStringLiteral("Camera") : QStringLiteral("Image"),
                    CameraControlType::Integer, minimum, maximum, step, defaultValue, value, {},
                    (capabilities & CameraControl_Flags_Auto) != 0,
                    (flags & CameraControl_Flags_Auto) != 0, false,
                });
            }
        }
        releaseCom(camera);
        releaseCom(image);

        qint64 rotation = 0;
        if (WindowsCameraRotation::read(deviceId_, &rotation)) {
            found.push_back({
                QString::fromLatin1(WindowsCameraRotation::controlId),
                QStringLiteral("Video rotation"), QStringLiteral("Windows camera pipeline"),
                CameraControlType::Menu, 0, 270, 90, 0, rotation,
                {{0, QStringLiteral("No rotation")}, {90, QStringLiteral("Right 90°")},
                 {180, QStringLiteral("180°")}, {270, QStringLiteral("Left 90°")}},
            });
        }

        for (const auto &spec : extendedSpecs()) {
            QByteArray data;
            if (!getExtended(ksControl_, spec.propertyId, &data)) continue;
            const auto *header = reinterpret_cast<const KSCAMERA_EXTENDEDPROP_HEADER *>(data.constData());
            QVector<CameraControlOption> supported;
            for (const auto &option : spec.options) {
                if (option.value == 0 || (header->Capability & static_cast<ULONGLONG>(option.value)) != 0)
                    supported.push_back(option);
            }
            if (supported.size() < 2) continue;
            found.push_back({
                QString::fromLatin1(spec.id), QString::fromLatin1(spec.name),
                QStringLiteral("Windows extended camera controls"), CameraControlType::Menu,
                0, 0, 1, supported.first().value, static_cast<qint64>(header->Flags),
                supported, false, false, false,
            });
        }

        QByteArray fovCapsData;
        QByteArray fovData;
        if (getExtended(ksControl_, KSPROPERTY_CAMERACONTROL_EXTENDED_FIELDOFVIEW2_CONFIGCAPS,
                        &fovCapsData)
            && getExtended(ksControl_, KSPROPERTY_CAMERACONTROL_EXTENDED_FIELDOFVIEW2, &fovData)
            && fovCapsData.size() >= static_cast<qsizetype>(sizeof(KSCAMERA_EXTENDEDPROP_HEADER)
                                                            + sizeof(KSCAMERA_EXTENDEDPROP_FIELDOFVIEW2_CONFIGCAPS))
            && fovData.size() >= static_cast<qsizetype>(sizeof(KSCAMERA_EXTENDEDPROP_HEADER)
                                                        + sizeof(KSCAMERA_EXTENDEDPROP_VALUE))) {
            const auto *caps = reinterpret_cast<const KSCAMERA_EXTENDEDPROP_FIELDOFVIEW2_CONFIGCAPS *>(
                fovCapsData.constData() + sizeof(KSCAMERA_EXTENDEDPROP_HEADER));
            const auto *value = reinterpret_cast<const KSCAMERA_EXTENDEDPROP_VALUE *>(
                fovData.constData() + sizeof(KSCAMERA_EXTENDEDPROP_HEADER));
            QVector<CameraControlOption> options;
            const int stopCount = qMin<int>(caps->DiscreteFoVStopsCount, 360);
            for (int index = 0; index < stopCount; ++index) {
                options.push_back({caps->DiscreteFoVStops[index],
                                   QStringLiteral("%1°").arg(caps->DiscreteFoVStops[index])});
            }
            if (!options.isEmpty()) {
                found.push_back({
                    QStringLiteral("windows:extended:field-of-view"), QStringLiteral("Field of view"),
                    QStringLiteral("Windows extended camera controls"), CameraControlType::Menu,
                    0, 0, 1, caps->DefaultDiagonalFieldOfViewInDegrees, value->Value.ul,
                    options, false, false, false,
                });
            }
        }

        QByteArray windowData;
        if (getExtended(ksControl_, KSPROPERTY_CAMERACONTROL_EXTENDED_DIGITALWINDOW, &windowData)
            && windowData.size() >= static_cast<qsizetype>(sizeof(KSCAMERA_EXTENDEDPROP_HEADER)
                                                           + sizeof(KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_SETTING))) {
            const auto *window = reinterpret_cast<const KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_SETTING *>(
                windowData.constData() + sizeof(KSCAMERA_EXTENDEDPROP_HEADER));
            qint64 minSize = 1, maxSize = 100;
            qint64 minX = 0, maxX = 100 - q24ToPercent(window->WindowSize);
            qint64 minY = 0, maxY = maxX;
            QByteArray capsData;
            if (getExtended(ksControl_, KSPROPERTY_CAMERACONTROL_EXTENDED_DIGITALWINDOW_CONFIGCAPS,
                            &capsData)
                && capsData.size() >= static_cast<qsizetype>(sizeof(KSCAMERA_EXTENDEDPROP_HEADER)
                    + sizeof(KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_CONFIGCAPSHEADER)
                    + sizeof(KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_CONFIGCAPS))) {
                const auto *caps = reinterpret_cast<const KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_CONFIGCAPS *>(
                    capsData.constData() + sizeof(KSCAMERA_EXTENDEDPROP_HEADER)
                    + sizeof(KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_CONFIGCAPSHEADER));
                minSize = q24ToPercent(caps->MinWindowSize);
                maxSize = q24ToPercent(caps->MaxWindowSize);
                minX = q24ToPercent(caps->PorchLeft);
                maxX = q24ToPercent(caps->PorchRight - window->WindowSize);
                minY = q24ToPercent(caps->PorchTop);
                maxY = q24ToPercent(caps->PorchBottom - window->WindowSize);
            }
            found.push_back({QStringLiteral("windows:extended:digital-window-x"),
                             QStringLiteral("Digital crop X"),
                             QStringLiteral("Windows extended camera controls"), CameraControlType::Integer,
                             minX, maxX, 1, 0, q24ToPercent(window->OriginX)});
            found.push_back({QStringLiteral("windows:extended:digital-window-y"),
                             QStringLiteral("Digital crop Y"),
                             QStringLiteral("Windows extended camera controls"), CameraControlType::Integer,
                             minY, maxY, 1, 0, q24ToPercent(window->OriginY)});
            found.push_back({QStringLiteral("windows:extended:digital-window-size"),
                             QStringLiteral("Digital crop size (%)"),
                             QStringLiteral("Windows extended camera controls"), CameraControlType::Integer,
                             minSize, maxSize, 1, 100, q24ToPercent(window->WindowSize)});
        }
        if (error) error->clear();
        return found;
    }

    bool setControl(const CameraControlValue &requested, CameraControlValue *accepted,
                    QString *error) override
    {
        if (!filter_ && !ksControl_) {
            setError(error, QStringLiteral("No camera is open."));
            return false;
        }
        if (requested.id == QLatin1String(WindowsCameraRotation::controlId)) {
            if (requested.automatic) {
                setError(error, QStringLiteral("Video rotation does not support automatic mode."));
                return false;
            }
            if (!WindowsCameraRotation::write(deviceId_, requested.value, error)) return false;
            if (accepted) *accepted = {requested.id, requested.value, false};
            return true;
        }
        if (const auto *spec = standardSpec(requested.id)) {
            const long flags = requested.automatic ? CameraControl_Flags_Auto : CameraControl_Flags_Manual;
            long actual = static_cast<long>(requested.value);
            long actualFlags = flags;
            HRESULT result = E_NOINTERFACE;
            if (spec->cameraControl) {
                IAMCameraControl *control = nullptr;
                if (SUCCEEDED(filter_->QueryInterface(IID_PPV_ARGS(&control)))) {
                    result = control->Set(spec->nativeId, static_cast<long>(requested.value), flags);
                    if (SUCCEEDED(result)) result = control->Get(spec->nativeId, &actual, &actualFlags);
                    releaseCom(control);
                }
            } else {
                IAMVideoProcAmp *control = nullptr;
                if (SUCCEEDED(filter_->QueryInterface(IID_PPV_ARGS(&control)))) {
                    result = control->Set(spec->nativeId, static_cast<long>(requested.value), flags);
                    if (SUCCEEDED(result)) result = control->Get(spec->nativeId, &actual, &actualFlags);
                    releaseCom(control);
                }
            }
            if (FAILED(result)) {
                setError(error, hresultMessage(QStringLiteral("Could not set and verify %1")
                                                   .arg(QString::fromLatin1(spec->name)), result));
                return false;
            }
            if (accepted) *accepted = {requested.id, actual,
                                       (actualFlags & CameraControl_Flags_Auto) != 0};
            if (error) error->clear();
            return true;
        }
        if (requested.id == QStringLiteral("windows:extended:field-of-view")) {
            QByteArray data;
            if (!getExtended(ksControl_, KSPROPERTY_CAMERACONTROL_EXTENDED_FIELDOFVIEW2, &data)
                || data.size() < static_cast<qsizetype>(sizeof(KSCAMERA_EXTENDEDPROP_HEADER)
                                                       + sizeof(KSCAMERA_EXTENDEDPROP_VALUE))) {
                setError(error, QStringLiteral("Field-of-view control is unavailable."));
                return false;
            }
            auto *value = reinterpret_cast<KSCAMERA_EXTENDEDPROP_VALUE *>(
                data.data() + sizeof(KSCAMERA_EXTENDEDPROP_HEADER));
            value->Value.ul = static_cast<ULONG>(requested.value);
            if (!setExtended(ksControl_, KSPROPERTY_CAMERACONTROL_EXTENDED_FIELDOFVIEW2,
                             &data, error)) return false;
            QByteArray verified;
            if (!getExtended(ksControl_, KSPROPERTY_CAMERACONTROL_EXTENDED_FIELDOFVIEW2, &verified)
                || verified.size() < static_cast<qsizetype>(sizeof(KSCAMERA_EXTENDEDPROP_HEADER)
                                                           + sizeof(KSCAMERA_EXTENDEDPROP_VALUE))) {
                setError(error, QStringLiteral("Set field of view but could not read it back."));
                return false;
            }
            const auto *actual = reinterpret_cast<const KSCAMERA_EXTENDEDPROP_VALUE *>(
                verified.constData() + sizeof(KSCAMERA_EXTENDEDPROP_HEADER));
            if (accepted) *accepted = {requested.id, actual->Value.ul, false};
            if (error) error->clear();
            return true;
        }
        if (requested.id.startsWith(QStringLiteral("windows:extended:digital-window-"))) {
            QByteArray data;
            if (!getExtended(ksControl_, KSPROPERTY_CAMERACONTROL_EXTENDED_DIGITALWINDOW, &data)
                || data.size() < static_cast<qsizetype>(sizeof(KSCAMERA_EXTENDEDPROP_HEADER)
                    + sizeof(KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_SETTING))) {
                setError(error, QStringLiteral("Digital-window control is unavailable."));
                return false;
            }
            auto *header = reinterpret_cast<KSCAMERA_EXTENDEDPROP_HEADER *>(data.data());
            auto *window = reinterpret_cast<KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_SETTING *>(
                data.data() + sizeof(KSCAMERA_EXTENDEDPROP_HEADER));
            header->Flags = KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_MANUAL;
            if (requested.id.endsWith(QStringLiteral("-x"))) window->OriginX = percentToQ24(requested.value);
            else if (requested.id.endsWith(QStringLiteral("-y"))) window->OriginY = percentToQ24(requested.value);
            else window->WindowSize = percentToQ24(requested.value);
            if (!setExtended(ksControl_, KSPROPERTY_CAMERACONTROL_EXTENDED_DIGITALWINDOW,
                             &data, error)) return false;
            QByteArray verified;
            if (!getExtended(ksControl_, KSPROPERTY_CAMERACONTROL_EXTENDED_DIGITALWINDOW, &verified)
                || verified.size() < data.size()) {
                setError(error, QStringLiteral("Set digital window but could not read it back."));
                return false;
            }
            const auto *actual = reinterpret_cast<const KSCAMERA_EXTENDEDPROP_DIGITALWINDOW_SETTING *>(
                verified.constData() + sizeof(KSCAMERA_EXTENDEDPROP_HEADER));
            const LONG actualQ24 = requested.id.endsWith(QStringLiteral("-x")) ? actual->OriginX
                : requested.id.endsWith(QStringLiteral("-y")) ? actual->OriginY : actual->WindowSize;
            if (accepted) *accepted = {requested.id, q24ToPercent(actualQ24), false};
            if (error) error->clear();
            return true;
        }
        const auto *spec = extendedSpec(requested.id);
        QByteArray data;
        if (!spec || !getExtended(ksControl_, spec->propertyId, &data)) {
            setError(error, QStringLiteral("Extended camera control %1 is unavailable.").arg(requested.id));
            return false;
        }
        auto *header = reinterpret_cast<KSCAMERA_EXTENDEDPROP_HEADER *>(data.data());
        header->Flags = static_cast<ULONGLONG>(requested.value);
        if (!setExtended(ksControl_, spec->propertyId, &data, error)) return false;
        QByteArray verified;
        if (!getExtended(ksControl_, spec->propertyId, &verified)) {
            setError(error, QStringLiteral("Set %1 but could not read it back.").arg(requested.id));
            return false;
        }
        const auto *actual = reinterpret_cast<const KSCAMERA_EXTENDEDPROP_HEADER *>(verified.constData());
        if (accepted) *accepted = {requested.id, static_cast<qint64>(actual->Flags), false};
        if (error) error->clear();
        return true;
    }

private:
    IBaseFilter *filter_{};
    IKsControl *ksControl_{};
    IMFMediaSource *mediaSource_{};
    QByteArray deviceId_;
    QString deviceName_;
    bool shouldUninitialize_{false};
    bool mediaFoundationStarted_{false};
};

std::unique_ptr<CameraControlBackend> createPlatformBackend()
{
    return std::make_unique<WindowsControlBackend>();
}
