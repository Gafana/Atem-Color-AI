#include "AtemClient.h"

#ifdef HAVE_ATEM_SDK

#ifdef _WIN32
#include <windows.h>
#include <comdef.h>
#else
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "BMDSwitcherAPI.h"

namespace {

QString portTypeToString(BMDSwitcherPortType type)
{
    switch (type) {
    case bmdSwitcherPortTypeExternal:
        return QStringLiteral("External");
    case bmdSwitcherPortTypeBlack:
        return QStringLiteral("Black");
    case bmdSwitcherPortTypeColorBars:
        return QStringLiteral("ColorBars");
    case bmdSwitcherPortTypeColorGenerator:
        return QStringLiteral("ColorGenerator");
    case bmdSwitcherPortTypeMediaPlayerFill:
        return QStringLiteral("MediaPlayerFill");
    case bmdSwitcherPortTypeMediaPlayerCut:
        return QStringLiteral("MediaPlayerCut");
    case bmdSwitcherPortTypeSuperSource:
        return QStringLiteral("SuperSource");
    case bmdSwitcherPortTypeMixEffectBlockOutput:
        return QStringLiteral("MixEffectBlockOutput");
    case bmdSwitcherPortTypeAuxOutput:
        return QStringLiteral("AuxOutput");
    case bmdSwitcherPortTypeKeyCutOutput:
        return QStringLiteral("KeyCutOutput");
    case bmdSwitcherPortTypeMultiview:
        return QStringLiteral("Multiview");
    case bmdSwitcherPortTypeAudioMonitor:
        return QStringLiteral("AudioMonitor");
    case bmdSwitcherPortTypeExternalDirect:
        return QStringLiteral("ExternalDirect");
    default:
        return QStringLiteral("Unknown");
    }
}

QString externalPortTypeToString(BMDSwitcherExternalPortType type)
{
    switch (type) {
    case bmdSwitcherExternalPortTypeSDI:
        return QStringLiteral("SDI");
    case bmdSwitcherExternalPortTypeHDMI:
        return QStringLiteral("HDMI");
    case bmdSwitcherExternalPortTypeComponent:
        return QStringLiteral("Component");
    case bmdSwitcherExternalPortTypeComposite:
        return QStringLiteral("Composite");
    case bmdSwitcherExternalPortTypeSVideo:
        return QStringLiteral("S-Video");
    case bmdSwitcherExternalPortTypeInternal:
        return QStringLiteral("Internal");
    case bmdSwitcherExternalPortTypeXLR:
        return QStringLiteral("XLR");
    case bmdSwitcherExternalPortTypeAESEBU:
        return QStringLiteral("AES/EBU");
    case bmdSwitcherExternalPortTypeRCA:
        return QStringLiteral("RCA");
    case bmdSwitcherExternalPortTypeTSJack:
        return QStringLiteral("TS Jack");
    case bmdSwitcherExternalPortTypeMADI:
        return QStringLiteral("MADI");
    case bmdSwitcherExternalPortTypeTRS:
        return QStringLiteral("TRS");
    default:
        return QStringLiteral("External");
    }
}

#ifdef _WIN32
QString fromBstr(BSTR bstr)
{
    if (!bstr)
        return {};
    return QString::fromWCharArray(bstr);
}
#else
QString fromCfString(CFStringRef value)
{
    if (!value)
        return {};
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QString::fromCFString(value);
#else
    char buffer[512];
    if (CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8))
        return QString::fromUtf8(buffer);
    return {};
#endif
}
#endif

QString formatPortLabel(BMDSwitcherPortType portType, IBMDSwitcherInput *input)
{
    QString label = portTypeToString(portType);
    if (portType == bmdSwitcherPortTypeExternal && input) {
        BMDSwitcherExternalPortType ext = {};
        if (SUCCEEDED(input->GetCurrentExternalPortType(&ext)))
            label += QStringLiteral(" (%1)").arg(externalPortTypeToString(ext));
    }
    return label;
}

} // namespace

class AtemClientSdk final : public AtemClient
{
public:
    AtemClientSdk()
    {
#ifdef _WIN32
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    }

    ~AtemClientSdk() override
    {
        disconnect();
#ifdef _WIN32
        CoUninitialize();
#endif
    }

    QString backendName() const override { return QStringLiteral("Blackmagic ATEM SDK"); }

    QString connectTo(const QString &address) override
    {
        disconnect();

        HRESULT hr = S_OK;
#ifdef _WIN32
        hr = CoCreateInstance(CLSID_CBMDSwitcherDiscovery, nullptr, CLSCTX_ALL,
                              IID_IBMDSwitcherDiscovery, reinterpret_cast<void **>(&m_discovery));
#else
        m_discovery = CreateBMDSwitcherDiscoveryInstance();
        hr = m_discovery ? S_OK : E_FAIL;
#endif
        if (FAILED(hr) || !m_discovery) {
            return QStringLiteral("Failed to create IBMDSwitcherDiscovery (HRESULT 0x%1)")
                .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
        }

        BMDSwitcherConnectToFailure failReason = bmdSwitcherConnectToFailureNoResponse;
#ifdef _WIN32
        hr = m_discovery->ConnectTo(_bstr_t(address.toStdWString().c_str()), &m_switcher, &failReason);
#else
        CFStringRef cfAddress =
            CFStringCreateWithCString(kCFAllocatorDefault, address.toUtf8().constData(),
                                      kCFStringEncodingUTF8);
        hr = m_discovery->ConnectTo(cfAddress, &m_switcher, &failReason);
        if (cfAddress)
            CFRelease(cfAddress);
#endif
        if (FAILED(hr) || !m_switcher) {
            return QStringLiteral("ConnectTo failed: %1 (HRESULT 0x%2)")
                .arg(connectFailureString(failReason))
                .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
        }

        m_switcherSupportsCameraControl = querySwitcherCameraControl();
        m_address = address;
        return {};
    }

    void disconnect() override
    {
        m_switcherSupportsCameraControl = false;
        if (m_switcher) {
            m_switcher->Release();
            m_switcher = nullptr;
        }
        if (m_discovery) {
            m_discovery->Release();
            m_discovery = nullptr;
        }
        m_address.clear();
    }

    bool isConnected() const override { return m_switcher != nullptr; }

    QVector<AtemInputInfo> listInputs() override
    {
        QVector<AtemInputInfo> out;
        if (!m_switcher)
            return out;

        IBMDSwitcherInputIterator *iterator = nullptr;
        HRESULT hr = m_switcher->CreateIterator(IID_IBMDSwitcherInputIterator,
                                                reinterpret_cast<void **>(&iterator));
        if (FAILED(hr) || !iterator)
            return out;

        IBMDSwitcherInput *input = nullptr;
        while (iterator->Next(&input) == S_OK && input) {
            AtemInputInfo info;
            BMDSwitcherInputId inputId = 0;
            if (SUCCEEDED(input->GetInputId(&inputId)))
                info.id = static_cast<qint64>(inputId);

            BMDSwitcherPortType portType = bmdSwitcherPortTypeExternal;
            if (SUCCEEDED(input->GetPortType(&portType)))
                info.portType = formatPortLabel(portType, input);

#ifdef _WIN32
            BSTR shortName = nullptr;
            BSTR longName = nullptr;
            if (SUCCEEDED(input->GetShortName(&shortName)))
                info.shortName = fromBstr(shortName);
            if (SUCCEEDED(input->GetLongName(&longName)))
                info.longName = fromBstr(longName);
            SysFreeString(shortName);
            SysFreeString(longName);
#else
            CFStringRef shortName = nullptr;
            CFStringRef longName = nullptr;
            if (SUCCEEDED(input->GetShortName(&shortName)) && shortName) {
                info.shortName = fromCfString(shortName);
                CFRelease(shortName);
            }
            if (SUCCEEDED(input->GetLongName(&longName)) && longName) {
                info.longName = fromCfString(longName);
                CFRelease(longName);
            }
#endif

            info.cameraControlSupported = checkCameraControl(input);
            out.push_back(info);
            input->Release();
            input = nullptr;
        }

        iterator->Release();
        return out;
    }

private:
    bool querySwitcherCameraControl()
    {
        IBMDSwitcherCameraControl *cameraControl = nullptr;
        const HRESULT hr =
            m_switcher->QueryInterface(IID_IBMDSwitcherCameraControl,
                                       reinterpret_cast<void **>(&cameraControl));
        if (SUCCEEDED(hr) && cameraControl) {
            cameraControl->Release();
            return true;
        }
        return false;
    }

    static QString connectFailureString(BMDSwitcherConnectToFailure reason)
    {
        switch (reason) {
        case bmdSwitcherConnectToFailureNoResponse:
            return QStringLiteral("no response");
        case bmdSwitcherConnectToFailureIncompatibleFirmware:
            return QStringLiteral("incompatible firmware");
        case bmdSwitcherConnectToFailureCorruptData:
            return QStringLiteral("corrupt data");
        case bmdSwitcherConnectToFailureStateSync:
            return QStringLiteral("state sync failed");
        case bmdSwitcherConnectToFailureStateSyncTimedOut:
            return QStringLiteral("state sync timed out");
        default:
            return QStringLiteral("unknown");
        }
    }

    bool checkCameraControl(IBMDSwitcherInput *input) const
    {
        if (!input || !m_switcherSupportsCameraControl)
            return false;
        bool supportsModel = false;
        if (SUCCEEDED(input->DoesSupportCameraModel(&supportsModel)) && supportsModel)
            return true;
        return false;
    }

    IBMDSwitcherDiscovery *m_discovery = nullptr;
    IBMDSwitcher *m_switcher = nullptr;
    bool m_switcherSupportsCameraControl = false;
    QString m_address;
};

std::unique_ptr<AtemClient> createAtemClientSdk()
{
    return std::make_unique<AtemClientSdk>();
}

#endif // HAVE_ATEM_SDK