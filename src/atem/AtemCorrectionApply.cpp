#include "AtemCorrectionApply.h"
#include "CameraControlProtocol.h"
#include "color/CorrectionParameterMask.h"

#include <QLoggingCategory>
#include <QThread>
#include <cmath>

Q_LOGGING_CATEGORY(lcCamApply, "atem.camera")

using namespace BmdCameraProtocol;

namespace {

bool differs(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) > eps;
}

bool differs4(float a0, float a1, float a2, float a3, float b0, float b1, float b2, float b3)
{
    return differs(a0, b0) || differs(a1, b1) || differs(a2, b2) || differs(a3, b3);
}

} // namespace

QString applyCorrectionBundleToClient(AtemClient &client,
                                    qint64 destinationId,
                                    const CameraCorrectionBundle &bundle,
                                    CameraCorrectionBundle *previousOut,
                                    const CorrectionParameterMask *mask,
                                    const QString &tuningMode)
{
    CameraCorrectionBundle previous;
    const QString readErr = client.readCorrectionBundle(destinationId, previous);
    const bool havePrevious = readErr.isEmpty();
    if (!havePrevious)
        qCWarning(lcCamApply).noquote() << QStringLiteral("dest=%1 read current values failed: %2")
                                              .arg(destinationId)
                                              .arg(readErr);

    if (previousOut)
        *previousOut = previous;

    QString err;
    const auto values4 = [](float a, float b, float c, float d) {
        return QStringLiteral("%1,%2,%3,%4")
            .arg(a, 0, 'f', 4)
            .arg(b, 0, 'f', 4)
            .arg(c, 0, 'f', 4)
            .arg(d, 0, 'f', 4);
    };
    const QString modeTag =
        tuningMode.isEmpty() ? QString() : QStringLiteral("mode=%1 ").arg(tuningMode);

    int sentCount = 0;
    const auto send = [&](const QString &name, const QString &values, const QString &stepErr) {
        ++sentCount;
        const QString line = QStringLiteral("dest=%1 %2 %3")
                                 .arg(QString::number(destinationId), name, values);
        if (stepErr.isEmpty()) {
            qCInfo(lcCamApply).noquote() << QStringLiteral("sent ") + modeTag + line;
            QThread::msleep(40); // pace camera control commands (the camera may drop a burst)
            return;
        }
        qCWarning(lcCamApply).noquote() << QStringLiteral("send failed ") + line + QStringLiteral(": ") + stepErr;
        if (!err.isEmpty())
            err += QStringLiteral("; ");
        err += name + QStringLiteral(": ") + stepErr;
    };
    // When the current values could not be read, everything enabled is sent (cannot compare).
    const auto changed = [&](bool diff) { return !havePrevious || diff; };

    const CorrectionParameterMask m = mask ? *mask : CorrectionParameterMask::allEnabled();
    const CameraCorrectionBundle &b = bundle;
    const CameraCorrectionBundle &p = previous;

    // Light controls first. Live test (Micro Studio 4K G2 via ATEM): with CC gain + white balance + sensor
    // gain sent back to back, the sensor gain was sometimes not applied; commands are also paced.
    if (m.level && changed(b.gainDb != p.gainDb))
        send(QStringLiteral("gainDb"), QString::number(b.gainDb), client.setCameraGainDb(destinationId, b.gainDb));

    if (m.iris && b.irisFStopAv >= 0.0f && changed(differs(b.irisFStopAv, p.irisFStopAv, 0.01f)))
        send(QStringLiteral("iris"), QStringLiteral("AV=%1").arg(b.irisFStopAv, 0, 'f', 3),
             client.setCameraFloats(destinationId, kCategoryLens, kLensApertureFStop, {b.irisFStopAv}));

    if (m.lift && changed(differs4(b.liftR, b.liftG, b.liftB, b.liftY, p.liftR, p.liftG, p.liftB, p.liftY)))
        send(QStringLiteral("lift"), values4(b.liftR, b.liftG, b.liftB, b.liftY),
             client.setCameraFloats(destinationId, kCategoryColorCorrection, kCcLift,
                                    {b.liftR, b.liftG, b.liftB, b.liftY}));
    if (m.gamma
        && changed(differs4(b.gammaR, b.gammaG, b.gammaB, b.gammaY, p.gammaR, p.gammaG, p.gammaB, p.gammaY)))
        send(QStringLiteral("gamma"), values4(b.gammaR, b.gammaG, b.gammaB, b.gammaY),
             client.setCameraFloats(destinationId, kCategoryColorCorrection, kCcGamma,
                                    {b.gammaR, b.gammaG, b.gammaB, b.gammaY}));
    if (m.gain && changed(differs4(b.gainR, b.gainG, b.gainB, b.gainY, p.gainR, p.gainG, p.gainB, p.gainY)))
        send(QStringLiteral("gain"), values4(b.gainR, b.gainG, b.gainB, b.gainY),
             client.setCameraFloats(destinationId, kCategoryColorCorrection, kCcGain,
                                    {b.gainR, b.gainG, b.gainB, b.gainY}));
    if (m.offset
        && changed(differs4(b.offsetR, b.offsetG, b.offsetB, b.offsetY, p.offsetR, p.offsetG, p.offsetB, p.offsetY)))
        send(QStringLiteral("offset"), values4(b.offsetR, b.offsetG, b.offsetB, b.offsetY),
             client.setCameraFloats(destinationId, kCategoryColorCorrection, kCcOffset,
                                    {b.offsetR, b.offsetG, b.offsetB, b.offsetY}));
    if (m.contrast && changed(differs(b.contrastPivot, p.contrastPivot) || differs(b.contrastAdj, p.contrastAdj)))
        send(QStringLiteral("contrast"),
             QStringLiteral("pivot=%1,adj=%2").arg(b.contrastPivot, 0, 'f', 4).arg(b.contrastAdj, 0, 'f', 4),
             client.setCameraFloats(destinationId, kCategoryColorCorrection, kCcContrast,
                                    {b.contrastPivot, b.contrastAdj}));
    if (m.contrast && changed(differs(b.lumaMix, p.lumaMix)))
        send(QStringLiteral("lumaMix"), QString::number(b.lumaMix, 'f', 4),
             client.setCameraFloats(destinationId, kCategoryColorCorrection, kCcLumaMix, {b.lumaMix}));
    if (m.saturation || m.hue) {
        CameraCorrectionBundle colorOnly = previous;
        if (m.hue)
            colorOnly.hue = b.hue;
        if (m.saturation)
            colorOnly.saturation = b.saturation;
        if (changed(differs(colorOnly.hue, p.hue) || differs(colorOnly.saturation, p.saturation)))
            send(QStringLiteral("color"),
                 QStringLiteral("hue=%1,sat=%2").arg(colorOnly.hue, 0, 'f', 4).arg(colorOnly.saturation, 0, 'f', 4),
                 client.setCameraFloats(destinationId, kCategoryColorCorrection, kCcColorAdjust,
                                        {colorOnly.hue, colorOnly.saturation}));
    }
    if (m.whiteBalance || m.tint) {
        const int kelvin = m.whiteBalance ? b.whiteBalanceKelvin : p.whiteBalanceKelvin;
        const int tint = m.tint ? b.whiteBalanceTint : p.whiteBalanceTint;
        if (changed(kelvin != p.whiteBalanceKelvin || tint != p.whiteBalanceTint))
            send(QStringLiteral("wb"), QStringLiteral("kelvin=%1,tint=%2").arg(kelvin).arg(tint),
                 client.setCameraInt16Pair(destinationId, kCategoryVideo, kVideoManualWhiteBalance, kelvin, tint));
    }

    if (sentCount == 0)
        qCInfo(lcCamApply).noquote() << QStringLiteral("no changes ") + modeTag
                                            + QStringLiteral("dest=%1 (nothing sent)").arg(destinationId);
    return err;
}
