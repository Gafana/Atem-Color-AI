#include "AtemClient.h"
#include "AtemCorrectionApply.h"
#include "CameraControlProtocol.h"

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
        if (m_switcherSupportsCameraControl) {
            m_switcher->QueryInterface(IID_IBMDSwitcherCameraControl,
                                       reinterpret_cast<void **>(&m_cameraControl));
        }
        m_address = address;
        return {};
    }

    void disconnect() override
    {
        m_switcherSupportsCameraControl = false;
        if (m_cameraControl) {
            m_cameraControl->Release();
            m_cameraControl = nullptr;
        }
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

    QString readCorrectionBundle(qint64 destinationId, CameraCorrectionBundle &out) override
    {
        if (!m_cameraControl)
            return QStringLiteral("Camera Control not available on switcher");

        using namespace BmdCameraProtocol;
        const uint32_t dest = static_cast<uint32_t>(destinationId);

        auto readFloat4 = [&](uint8_t param, float &a, float &b, float &c, float &d) -> QString {
            uint32_t count = 4;
            double values[4] = {};
            const HRESULT hr =
                m_cameraControl->GetFloats(dest, kCategoryColorCorrection, param, &count, values);
            if (FAILED(hr))
                return QStringLiteral("GetFloats failed cat=8 param=%1 hr=0x%2")
                    .arg(param)
                    .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
            if (count >= 4) {
                a = static_cast<float>(values[0]);
                b = static_cast<float>(values[1]);
                c = static_cast<float>(values[2]);
                d = static_cast<float>(values[3]);
            }
            return {};
        };

        QString err;
        err += readFloat4(kCcLift, out.liftR, out.liftG, out.liftB, out.liftY);
        err += readFloat4(kCcGamma, out.gammaR, out.gammaG, out.gammaB, out.gammaY);
        err += readFloat4(kCcGain, out.gainR, out.gainG, out.gainB, out.gainY);

        uint32_t count = 2;
        double colorVals[2] = {};
        if (SUCCEEDED(m_cameraControl->GetFloats(dest, kCategoryColorCorrection, kCcColorAdjust,
                                                  &count, colorVals))
            && count >= 2) {
            out.hue = static_cast<float>(colorVals[0]);
            out.saturation = static_cast<float>(colorVals[1]);
        }

        count = 2;
        int16_t wb[2] = {};
        if (SUCCEEDED(m_cameraControl->GetInt16s(dest, kCategoryVideo, kVideoManualWhiteBalance,
                                                 &count, wb))
            && count >= 2) {
            out.whiteBalanceKelvin = wb[0];
            out.whiteBalanceTint = wb[1];
        }

        count = 1;
        int8_t gain = 0;
        if (SUCCEEDED(m_cameraControl->GetInt8s(dest, kCategoryVideo, kVideoGainDb, &count, &gain))
            && count >= 1)
            out.gainDb = gain;

        out.clamp();
        return err;
    }

    QString applyCorrectionBundle(qint64 destinationId,
                                  const CameraCorrectionBundle &bundle,
                                  CameraCorrectionBundle *previousOut) override
    {
        if (!m_cameraControl)
            return QStringLiteral("Camera Control not available on switcher");
        return applyCorrectionBundleToClient(*this, destinationId, bundle, previousOut);
    }

    QString rollbackCorrection(qint64 destinationId,
                               const CameraCorrectionBundle &previous) override
    {
        return applyCorrectionBundle(destinationId, previous, nullptr);
    }

    QString setCameraFloats(qint64 destinationId,
                            uint8_t category,
                            uint8_t parameter,
                            const QVector<float> &values) override
    {
        if (!m_cameraControl)
            return QStringLiteral("Camera Control not available");
        if (values.isEmpty())
            return QStringLiteral("No float values");

        QVector<double> d(values.size());
        for (int i = 0; i < values.size(); ++i)
            d[i] = static_cast<double>(values[i]);

        const HRESULT hr = m_cameraControl->SetFloats(static_cast<uint32_t>(destinationId), category,
                                                       parameter, static_cast<uint32_t>(d.size()),
                                                       d.constData());
        if (FAILED(hr))
            return QStringLiteral("SetFloats failed hr=0x%1")
                .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
        return {};
    }

    QString setCameraInt16Pair(qint64 destinationId,
                               uint8_t category,
                               uint8_t parameter,
                               int value0,
                               int value1) override
    {
        if (!m_cameraControl)
            return QStringLiteral("Camera Control not available");
        const int16_t values[2] = {static_cast<int16_t>(value0), static_cast<int16_t>(value1)};
        const HRESULT hr = m_cameraControl->SetInt16s(static_cast<uint32_t>(destinationId), category,
                                                         parameter, 2, values);
        if (FAILED(hr))
            return QStringLiteral("SetInt16s failed hr=0x%1")
                .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
        return {};
    }

    QString setCameraGainDb(qint64 destinationId, int gainDb) override
    {
        if (!m_cameraControl)
            return QStringLiteral("Camera Control not available");
        const int8_t value = static_cast<int8_t>(qBound(-128, gainDb, 127));
        const HRESULT hr = m_cameraControl->SetInt8s(static_cast<uint32_t>(destinationId),
                                                      BmdCameraProtocol::kCategoryVideo,
                                                      BmdCameraProtocol::kVideoGainDb, 1, &value);
        if (FAILED(hr))
            return QStringLiteral("SetInt8s gain failed hr=0x%1")
                .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
        return {};
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
    IBMDSwitcherCameraControl *m_cameraControl = nullptr;
    bool m_switcherSupportsCameraControl = false;
    QString m_address;
};

std::unique_ptr<AtemClient> createAtemClientSdk()
{
    return std::make_unique<AtemClientSdk>();
}

#endif // HAVE_ATEM_SDK