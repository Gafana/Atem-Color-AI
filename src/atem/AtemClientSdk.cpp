#include "AtemClient.h"
#include "AtemCorrectionApply.h"
#include "CameraControlProtocol.h"
#include "platform/AtemRuntime.h"

#ifdef HAVE_ATEM_SDK

#ifdef _WIN32
#include <windows.h>
#include <comdef.h>
#else
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "BMDSwitcherAPI.h"

#include <atomic>

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

QString hrText(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
}

} // namespace

/** Receives switcher events; only "disconnected" matters (cable / power loss of the ATEM). */
class SwitcherLinkCallback final : public IBMDSwitcherCallback
{
public:
    explicit SwitcherLinkCallback(std::atomic<bool> *lost) : m_lost(lost) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (IsEqualGUID(iid, IID_IBMDSwitcherCallback) || IsEqualGUID(iid, IID_IUnknown)) {
            *ppv = static_cast<IBMDSwitcherCallback *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --m_ref;
        if (r == 0)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Notify(BMDSwitcherEventType eventType, BMDSwitcherVideoMode) override
    {
        if (eventType == bmdSwitcherEventTypeDisconnected && m_lost)
            m_lost->store(true);
        return S_OK;
    }

private:
    std::atomic<bool> *m_lost = nullptr;
    std::atomic<ULONG> m_ref{1};
};

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
            if (!AtemRuntime::isBlackmagicAtemRuntimeAvailable())
                return AtemRuntime::missingRuntimeUserMessage();
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

        m_lost.store(false);
        m_linkCallback = new SwitcherLinkCallback(&m_lost);
        if (FAILED(m_switcher->AddCallback(m_linkCallback))) {
            m_linkCallback->Release();
            m_linkCallback = nullptr;
        }

        m_cameraControlHr = m_switcher->QueryInterface(IID_IBMDSwitcherCameraControl,
                                                        reinterpret_cast<void **>(&m_cameraControl));
        if (FAILED(m_cameraControlHr))
            m_cameraControl = nullptr;
        m_switcherSupportsCameraControl = m_cameraControl != nullptr;
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
            if (m_linkCallback)
                m_switcher->RemoveCallback(m_linkCallback);
            m_switcher->Release();
            m_switcher = nullptr;
        }
        if (m_linkCallback) {
            m_linkCallback->Release();
            m_linkCallback = nullptr;
        }
        if (m_discovery) {
            m_discovery->Release();
            m_discovery = nullptr;
        }
        m_address.clear();
    }

    bool isConnected() const override { return m_switcher != nullptr && !m_lost.load(); }

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

    QVector<AtemAuxOutput> listAuxOutputs() override
    {
        QVector<AtemAuxOutput> outputs;
        if (!m_switcher)
            return outputs;

        IBMDSwitcherInputIterator *iterator = nullptr;
        const HRESULT hr = m_switcher->CreateIterator(IID_IBMDSwitcherInputIterator,
                                                      reinterpret_cast<void **>(&iterator));
        if (FAILED(hr) || !iterator)
            return outputs;

        IBMDSwitcherInput *input = nullptr;
        while (iterator->Next(&input) == S_OK && input) {
            IBMDSwitcherInputAux *aux = nullptr;
            const HRESULT qi =
                input->QueryInterface(IID_IBMDSwitcherInputAux, reinterpret_cast<void **>(&aux));
            if (SUCCEEDED(qi) && aux) {
                AtemAuxOutput info;
                BMDSwitcherInputId inputId = 0;
                if (SUCCEEDED(input->GetInputId(&inputId)))
                    info.id = static_cast<qint64>(inputId);
#ifdef _WIN32
                BSTR shortName = nullptr;
                if (SUCCEEDED(input->GetShortName(&shortName)))
                    info.name = fromBstr(shortName);
                SysFreeString(shortName);
#else
                CFStringRef shortName = nullptr;
                if (SUCCEEDED(input->GetShortName(&shortName)) && shortName) {
                    info.name = fromCfString(shortName);
                    CFRelease(shortName);
                }
#endif
                if (info.name.isEmpty())
                    info.name = QStringLiteral("AUX %1").arg(info.id);
                outputs.push_back(info);
                aux->Release();
            }
            input->Release();
            input = nullptr;
        }
        iterator->Release();
        return outputs;
    }

    QString auxSource(qint64 auxId, qint64 *sourceId) override
    {
        return withAux(auxId, [&](IBMDSwitcherInputAux *aux) {
            BMDSwitcherInputId source = 0;
            const HRESULT hr = aux->GetInputSource(&source);
            if (FAILED(hr)) {
                return QStringLiteral("Не удалось прочитать источник AUX %1 (HRESULT %2)")
                    .arg(auxId)
                    .arg(hrText(hr));
            }
            if (sourceId)
                *sourceId = static_cast<qint64>(source);
            return QString();
        });
    }

    QString setAuxSource(qint64 auxId, qint64 sourceId) override
    {
        return withAux(auxId, [&](IBMDSwitcherInputAux *aux) {
            const HRESULT hr = aux->SetInputSource(static_cast<BMDSwitcherInputId>(sourceId));
            if (FAILED(hr)) {
                return QStringLiteral("Не удалось переключить AUX %1 на вход %2 (HRESULT %3)")
                    .arg(auxId)
                    .arg(sourceId)
                    .arg(hrText(hr));
            }
            return QString();
        });
    }

    QString programInput(qint64 *inputId) override
    {
        return withFirstMixEffect([&](IBMDSwitcherMixEffectBlock *block) {
            BMDSwitcherInputId source = 0;
            const HRESULT hr = block->GetProgramInput(&source);
            if (FAILED(hr)) {
                return QStringLiteral("Не удалось прочитать программный выход (HRESULT %1)")
                    .arg(hrText(hr));
            }
            if (inputId)
                *inputId = static_cast<qint64>(source);
            return QString();
        });
    }

    QString setProgramInput(qint64 inputId) override
    {
        return withFirstMixEffect([&](IBMDSwitcherMixEffectBlock *block) {
            const HRESULT hr = block->SetProgramInput(static_cast<BMDSwitcherInputId>(inputId));
            if (FAILED(hr)) {
                return QStringLiteral("Не удалось переключить PGM на вход %1 (HRESULT %2)")
                    .arg(inputId)
                    .arg(hrText(hr));
            }
            return QString();
        });
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
        readFloat4(kCcOffset, out.offsetR, out.offsetG, out.offsetB, out.offsetY); // optional

        {
            uint32_t n = 2;
            double v[2] = {};
            if (SUCCEEDED(m_cameraControl->GetFloats(dest, kCategoryColorCorrection, kCcContrast, &n, v)) && n >= 2) {
                out.contrastPivot = static_cast<float>(v[0]);
                out.contrastAdj = static_cast<float>(v[1]);
            }
            n = 1;
            if (SUCCEEDED(m_cameraControl->GetFloats(dest, kCategoryColorCorrection, kCcLumaMix, &n, v)) && n >= 1)
                out.lumaMix = static_cast<float>(v[0]);
            n = 1;
            if (SUCCEEDED(m_cameraControl->GetFloats(dest, kCategoryLens, kLensApertureFStop, &n, v)) && n >= 1)
                out.irisFStopAv = static_cast<float>(v[0]);
        }

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
                                  CameraCorrectionBundle *previousOut,
                                  const CorrectionParameterMask *mask,
                                  const QString &tuningMode) override
    {
        if (!m_cameraControl) {
            return QStringLiteral("Camera Control недоступен на пульте (HRESULT %1)")
                .arg(hrText(m_cameraControlHr));
        }
        return applyCorrectionBundleToClient(*this, destinationId, bundle, previousOut, mask, tuningMode);
    }

    QString rollbackCorrection(qint64 destinationId,
                               const CameraCorrectionBundle &previous) override
    {
        return applyCorrectionBundle(destinationId, previous, nullptr, nullptr, QString());
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

    QString triggerCameraAction(qint64 destinationId, uint8_t category, uint8_t parameter) override
    {
        if (!m_cameraControl)
            return QStringLiteral("Camera Control not available");
        // "void" parameters (auto WB 1.3, auto aperture 0.5) are triggered by a flag write.
        const BOOL on = TRUE;
        HRESULT hr = m_cameraControl->SetFlags(static_cast<uint32_t>(destinationId), category, parameter, 1, &on);
        if (FAILED(hr))
            hr = m_cameraControl->SetFlags(static_cast<uint32_t>(destinationId), category, parameter, 0, nullptr);
        if (FAILED(hr))
            return QStringLiteral("SetFlags failed hr=0x%1").arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
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
        // DoesSupportCameraModel() is about selecting a VISCA/PTZ camera model and is FALSE on
        // ATEM Mini for HDMI inputs, although Blackmagic CCU over HDMI works there. A regular
        // external input of a switcher with Camera Control is treated as CCU-capable;
        // ExternalDirect (DIR passthrough) and internal sources are not.
        BMDSwitcherPortType portType = bmdSwitcherPortTypeBlack;
        if (SUCCEEDED(input->GetPortType(&portType)) && portType == bmdSwitcherPortTypeExternal)
            return true;
        BOOL supportsModel = FALSE;
        if (SUCCEEDED(input->DoesSupportCameraModel(&supportsModel)) && supportsModel)
            return true;
        return false;
    }

    IBMDSwitcherDiscovery *m_discovery = nullptr;
    IBMDSwitcher *m_switcher = nullptr;
    IBMDSwitcherCameraControl *m_cameraControl = nullptr;
    SwitcherLinkCallback *m_linkCallback = nullptr;
    std::atomic<bool> m_lost{false};
    template <typename Fn>
    QString withAux(qint64 auxId, Fn &&fn)
    {
        if (!m_switcher)
            return QStringLiteral("Нет подключения к ATEM");

        IBMDSwitcherInputIterator *iterator = nullptr;
        HRESULT hr = m_switcher->CreateIterator(IID_IBMDSwitcherInputIterator,
                                                reinterpret_cast<void **>(&iterator));
        if (FAILED(hr) || !iterator) {
            return QStringLiteral("Не удалось перечислить выходы ATEM (HRESULT %1)").arg(hrText(hr));
        }

        QString result = QStringLiteral("Выход AUX id %1 не найден на пульте").arg(auxId);
        IBMDSwitcherInput *input = nullptr;
        while (iterator->Next(&input) == S_OK && input) {
            BMDSwitcherInputId inputId = 0;
            const bool match = SUCCEEDED(input->GetInputId(&inputId))
                               && static_cast<qint64>(inputId) == auxId;
            if (!match) {
                input->Release();
                input = nullptr;
                continue;
            }
            IBMDSwitcherInputAux *aux = nullptr;
            hr = input->QueryInterface(IID_IBMDSwitcherInputAux, reinterpret_cast<void **>(&aux));
            input->Release();
            input = nullptr;
            if (FAILED(hr) || !aux) {
                result = QStringLiteral("Выход id %1 нельзя переключить (HRESULT %2)")
                             .arg(auxId)
                             .arg(hrText(hr));
                break;
            }
            result = fn(aux);
            aux->Release();
            break;
        }
        iterator->Release();
        return result;
    }

    template <typename Fn>
    QString withFirstMixEffect(Fn &&fn)
    {
        if (!m_switcher)
            return QStringLiteral("Нет подключения к ATEM");

        IBMDSwitcherMixEffectBlockIterator *iterator = nullptr;
        HRESULT hr = m_switcher->CreateIterator(IID_IBMDSwitcherMixEffectBlockIterator,
                                                reinterpret_cast<void **>(&iterator));
        if (FAILED(hr) || !iterator) {
            return QStringLiteral("Не удалось получить программный выход (HRESULT %1)").arg(hrText(hr));
        }

        IBMDSwitcherMixEffectBlock *block = nullptr;
        hr = iterator->Next(&block);
        iterator->Release();
        if (FAILED(hr) || !block) {
            return QStringLiteral("На пульте нет блока Mix Effect (HRESULT %1)").arg(hrText(hr));
        }
        const QString result = fn(block);
        block->Release();
        return result;
    }

    bool m_switcherSupportsCameraControl = false;
    HRESULT m_cameraControlHr = static_cast<HRESULT>(0x80004002);
    QString m_address;
};

std::unique_ptr<AtemClient> createAtemClientSdk()
{
    return std::make_unique<AtemClientSdk>();
}

#endif // HAVE_ATEM_SDK