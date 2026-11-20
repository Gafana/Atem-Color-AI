#pragma once

#include "atem/AtemClient.h"
#include "control/VmixProtocol.h"

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <functional>

/**
 * vMix as the control backend (HTTP Web API, default http://localhost:8088/api).
 * «Cameras» are vMix inputs (id = input number); «AUX outputs» are vMix outputs (Output 2/3/4, External 2,
 * Fullscreen) that feed the capture card / virtual camera. Only input colour correction is controlled.
 *
 * vMix does not document CC values in the XML state: they are read from the XML when present, otherwise the
 * last values this program sent are used (cache, persisted through the callbacks; keyed by input GUID).
 */
class VmixClient final : public AtemClient
{
public:
    using CacheLoad = std::function<QByteArray()>;
    using CacheSave = std::function<void(const QByteArray &)>;

    explicit VmixClient(CacheLoad load = {}, CacheSave save = {}, int timeoutMs = 1500);

    QString backendName() const override;
    QString connectTo(const QString &address) override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }
    QVector<AtemInputInfo> listInputs() override;
    QVector<AtemAuxOutput> listAuxOutputs() override;
    QString auxSource(qint64 auxId, qint64 *sourceId) override;
    QString setAuxSource(qint64 auxId, qint64 sourceId) override;
    QString programInput(qint64 *inputId) override;
    QString setProgramInput(qint64 inputId) override;
    QString readCorrectionBundle(qint64 destinationId, CameraCorrectionBundle &out) override;
    QString applyCorrectionBundle(qint64 destinationId, const CameraCorrectionBundle &bundle,
                                  CameraCorrectionBundle *previousOut = nullptr,
                                  const CorrectionParameterMask *mask = nullptr,
                                  const QString &tuningMode = QString()) override;
    QString rollbackCorrection(qint64 destinationId, const CameraCorrectionBundle &previous) override;
    QString setCameraFloats(qint64, uint8_t, uint8_t, const QVector<float> &) override;
    QString setCameraInt16Pair(qint64, uint8_t, uint8_t, int, int) override;
    QString setCameraGainDb(qint64, int) override;
    QString triggerCameraAction(qint64 destinationId, uint8_t category, uint8_t parameter) override;

    /** One function call (query without leading '?'). Empty on success. */
    QString callFunction(const QString &query);
    /** GET /api state. */
    VmixProtocol::State fetchState(QString *error = nullptr);
    /** Whether CC of @p input came from the vMix XML (true) or from this program's cache (false). */
    bool ccFromXml(qint64 input) const { return m_ccFromXml.value(input, false); }

private:
    QString httpGet(const QString &pathAndQuery, QByteArray *body);
    QString apply(qint64 destinationId, const CameraCorrectionBundle &bundle, CameraCorrectionBundle *previousOut,
                  const CorrectionParameterMask *mask, bool sendAll);
    QString inputKey(qint64 number);
    void loadCache();
    void saveCache();

    QString m_host;
    int m_port = VmixProtocol::kDefaultHttpPort;
    int m_timeoutMs = 1500;
    bool m_connected = false;
    QString m_version;
    VmixProtocol::State m_state;
    QHash<qint64, qint64> m_routes;            ///< output id -> source set by this program
    QHash<QString, CameraCorrectionBundle> m_cc; ///< input GUID (or "#n") -> last known CC
    QHash<qint64, bool> m_ccFromXml;
    QSet<QString> m_ccUnknown; ///< inputs whose CC vMix changed itself (ColourCorrectionAuto): send everything
    CacheLoad m_load;
    CacheSave m_save;
};

std::unique_ptr<AtemClient> createVmixClient(VmixClient::CacheLoad load, VmixClient::CacheSave save);
