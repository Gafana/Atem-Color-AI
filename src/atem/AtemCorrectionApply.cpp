#include "AtemClient.h"
#include "CameraControlProtocol.h"

#include <QLoggingCategory>
#include <cstring>

Q_LOGGING_CATEGORY(lcCamApply, "atem.camera")

using namespace BmdCameraProtocol;

namespace {

QVector<float> liftGammaGain4(const CameraCorrectionBundle &b, int which)
{
    switch (which) {
    case 0:
        return {b.liftR, b.liftG, b.liftB, b.liftY};
    case 1:
        return {b.gammaR, b.gammaG, b.gammaB, b.gammaY};
    default:
        return {b.gainR, b.gainG, b.gainB, b.gainY};
    }
}

} // namespace

QString applyCorrectionBundleToClient(AtemClient &client,
                                    qint64 destinationId,
                                    const CameraCorrectionBundle &bundle,
                                    CameraCorrectionBundle *previousOut)
{
    CameraCorrectionBundle previous;
    const QString readErr = client.readCorrectionBundle(destinationId, previous);
    if (!readErr.isEmpty())
        qCWarning(lcCamApply) << "readCorrectionBundle:" << readErr;

    if (previousOut)
        *previousOut = previous;

    QString err;
    auto fail = [&](const QString &step, const QString &e) {
        if (!err.isEmpty())
            err += QStringLiteral("; ");
        err += step + QStringLiteral(": ") + e;
    };

    fail(QStringLiteral("lift"), client.setCameraFloats(destinationId, kCategoryColorCorrection,
                                                         kCcLift, liftGammaGain4(bundle, 0)));
    fail(QStringLiteral("gamma"), client.setCameraFloats(destinationId, kCategoryColorCorrection,
                                                          kCcGamma, liftGammaGain4(bundle, 1)));
    fail(QStringLiteral("gain"), client.setCameraFloats(destinationId, kCategoryColorCorrection,
                                                        kCcGain, liftGammaGain4(bundle, 2)));
    fail(QStringLiteral("color"),
         client.setCameraFloats(destinationId, kCategoryColorCorrection, kCcColorAdjust,
                                {bundle.hue, bundle.saturation}));
    fail(QStringLiteral("wb"),
         client.setCameraInt16Pair(destinationId, kCategoryVideo, kVideoManualWhiteBalance,
                                   bundle.whiteBalanceKelvin, bundle.whiteBalanceTint));
    fail(QStringLiteral("gainDb"), client.setCameraGainDb(destinationId, bundle.gainDb));

    if (bundle.irisFStopAv >= 0.0f) {
        fail(QStringLiteral("iris"), client.setCameraFloats(destinationId, kCategoryLens,
                                                               kLensApertureFStop, {bundle.irisFStopAv}));
    }

    return err;
}