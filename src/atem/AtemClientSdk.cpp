#include "AtemClient.h"

#ifdef HAVE_ATEM_SDK

#ifdef _WIN32
#include <windows.h>
#include <comdef.h>
#endif

#include "BMDSwitcherAPI.h"

#include <QDebug>

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
    case bmdSwitcherPortTypeMixEffect:
        return QStringLiteral("MixEffect");
    case bmdSwitcherPortTypeAuxOutput:
        return QStringLiteral("AuxOutput");
    case bmdSwitcherPortTypeMultiView:
        return QStringLiteral("MultiView");
    case bmdSwitcherPortTypeExternalDirect:
        return QStringLiteral("ExternalDirect");
    case bmdSwitcherPortTypeFrameSync:
        return QStringLiteral("FrameSync");
    case bmdSwitcherPortTypeInput:
        return QStringLiteral("Input");
    default:
        return QStringLiteral("Unknown");
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
QString fromUtf8(const char *utf8)
{
    if (!utf8)
        return {};
    return QString::fromUtf8(utf8);
}
#endif

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
        hr = m_discovery->ConnectTo(_bstr_t(address.toStdWString().c_str()),
                                    &m_switcher, &failReason);
#else
        hr = m_discovery->ConnectTo(address.toUtf8().constData(), &m_switcher, &failReason);
#endif
        if (FAILED(hr) || !m_switcher) {
            return QStringLiteral("ConnectTo failed: %1 (HRESULT 0x%2)")
                .arg(connectFailureString(failReason))
                .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
        }

        m_address = address;
        return {};
    }

    void disconnect() override
    {
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
                info.portType = portTypeToString(portType);

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
                info.shortName = fromUtf8(CFStringGetCStringPtr(shortName, kCFStringEncodingUTF8));
                CFRelease(shortName);
            }
            if (SUCCEEDED(input->GetLongName(&longName)) && longName) {
                info.longName = fromUtf8(CFStringGetCStringPtr(longName, kCFStringEncodingUTF8));
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
        case bmdSwitcherConnectToFailureStateSyncTimedout:
            return QStringLiteral("state sync timed out");
        default:
            return QStringLiteral("unknown");
        }
    }

    static bool checkCameraControl(IBMDSwitcherInput *input)
    {
        if (!input)
            return false;
        IBMDSwitcherCameraControl *cameraControl = nullptr;
        HRESULT hr = input->QueryInterface(IID_IBMDSwitcherCameraControl,
                                           reinterpret_cast<void **>(&cameraControl));
        if (SUCCEEDED(hr) && cameraControl) {
            cameraControl->Release();
            return true;
        }
        return false;
    }

    IBMDSwitcherDiscovery *m_discovery = nullptr;
    IBMDSwitcher *m_switcher = nullptr;
    QString m_address;
};

std::unique_ptr<AtemClient> createAtemClientSdk()
{
    return std::make_unique<AtemClientSdk>();
}

#endif // HAVE_ATEM_SDK