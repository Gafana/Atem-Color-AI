#pragma once

#include "CameraCorrection.h"

struct CorrectionParameterMask;

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

struct AtemInputInfo
{
    qint64 id = 0;
    QString shortName;
    QString longName;
    QString portType; // HDMI, SDI, etc.
    bool cameraControlSupported = false;
};

struct AtemAuxOutput
{
    qint64 id = 0;
    QString name;
};

class AtemClient
{
public:
    virtual ~AtemClient() = default;

    /** @return Human-readable backend name (SDK / Mock). */
    virtual QString backendName() const = 0;

    /**
     * Connect to switcher at @p address (IPv4/hostname).
     * @return Empty string on success, otherwise error description.
     */
    virtual QString connectTo(const QString &address) = 0;

    virtual void disconnect() = 0;

    virtual bool isConnected() const = 0;

    /** Enumerate switcher inputs (cameras / sources). */
    virtual QVector<AtemInputInfo> listInputs() = 0;

    /** Aux / output ports that can be routed to a capture card. */
    virtual QVector<AtemAuxOutput> listAuxOutputs() = 0;

    /** Read or set the source routed to an aux output. Empty string on success. */
    virtual QString auxSource(qint64 auxId, qint64 *sourceId) = 0;
    virtual QString setAuxSource(qint64 auxId, qint64 sourceId) = 0;

    /** Program bus of the first mix-effect block. */
    virtual QString programInput(qint64 *inputId) = 0;
    virtual QString setProgramInput(qint64 inputId) = 0;

    /**
     * Read current correction-related camera parameters when supported.
     * @return empty on success.
     */
    virtual QString readCorrectionBundle(qint64 destinationId, CameraCorrectionBundle &out) = 0;

    /**
     * Apply correction bundle with partial damping (values should already be damped).
     * Saves previous values to @p previousOut when non-null.
     */
    virtual QString applyCorrectionBundle(qint64 destinationId,
                                          const CameraCorrectionBundle &bundle,
                                          CameraCorrectionBundle *previousOut = nullptr,
                                          const CorrectionParameterMask *mask = nullptr,
                                          const QString &tuningMode = QString()) = 0;

    /** Restore bundle previously read into @p previous. */
    virtual QString rollbackCorrection(qint64 destinationId, const CameraCorrectionBundle &previous) = 0;

    /** Low-level float parameter write (fixed16 groups). */
    virtual QString setCameraFloats(qint64 destinationId,
                                    uint8_t category,
                                    uint8_t parameter,
                                    const QVector<float> &values) = 0;

    virtual QString setCameraInt16Pair(qint64 destinationId,
                                       uint8_t category,
                                       uint8_t parameter,
                                       int value0,
                                       int value1) = 0;

    virtual QString setCameraGainDb(qint64 destinationId, int gainDb) = 0;

    /** Trigger a "void" camera command, e.g. auto white balance (1.3) or auto aperture (0.5). */
    virtual QString triggerCameraAction(qint64 destinationId, uint8_t category, uint8_t parameter) = 0;
};

std::unique_ptr<AtemClient> createAtemClient();