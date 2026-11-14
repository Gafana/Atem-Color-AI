#include "ColorMatching.h"

#include <QtMath>

namespace {

float channelGainFromMean(double refMean, double candMean)
{
    if (candMean < 1.0)
        candMean = 1.0;
    const double ratio = refMean / candMean;
    return static_cast<float>(qBound(0.25, ratio, 4.0));
}

float liftFromShadowDelta(double refP2, double candP2)
{
    const double delta = (refP2 - candP2) / 255.0;
    return static_cast<float>(qBound(-2.0, delta * 4.0, 2.0));
}

float gammaFromMidtone(double refMean, double candMean)
{
    if (candMean < 1.0)
        return 0.0f;
    const double ratio = refMean / candMean;
    const double logRatio = std::log(ratio) / std::log(2.0);
    return static_cast<float>(qBound(-4.0, logRatio * 2.0, 4.0));
}

int kelvinShiftFromBalance(double deltaR, double deltaB)
{
    const double axis = (deltaB - deltaR) / 255.0;
    return static_cast<int>(qRound(axis * 2500.0));
}

int tintShiftFromGreen(double deltaG)
{
    return static_cast<int>(qBound(-50.0, -deltaG / 255.0 * 80.0, 50.0));
}

int gainDbFromLuma(double deltaY)
{
    const double db = deltaY / 255.0 * 18.0;
    return static_cast<int>(qBound(-12.0, db, 12.0));
}

} // namespace

CameraMatchResult ColorMatching::matchToReference(const FrameColorAnalysis &reference,
                                                  const FrameColorAnalysis &candidate,
                                                  const QString &label,
                                                  const ColorMatchOptions &options)
{
    CameraMatchResult out;
    out.label = label;
    out.analysis = candidate;
    out.metrics = ColorAnalysis::compare(reference, candidate);

    CameraCorrectionBundle corr;

    corr.gainR = channelGainFromMean(reference.statsR.mean, candidate.statsR.mean);
    corr.gainG = channelGainFromMean(reference.statsG.mean, candidate.statsG.mean);
    corr.gainB = channelGainFromMean(reference.statsB.mean, candidate.statsB.mean);
    corr.gainY = channelGainFromMean(reference.statsY.mean, candidate.statsY.mean);

    corr.liftR = liftFromShadowDelta(reference.statsR.p2, candidate.statsR.p2);
    corr.liftG = liftFromShadowDelta(reference.statsG.p2, candidate.statsG.p2);
    corr.liftB = liftFromShadowDelta(reference.statsB.p2, candidate.statsB.p2);
    corr.liftY = liftFromShadowDelta(reference.statsY.p2, candidate.statsY.p2);

    corr.gammaR = gammaFromMidtone(reference.statsR.mean, candidate.statsR.mean);
    corr.gammaG = gammaFromMidtone(reference.statsG.mean, candidate.statsG.mean);
    corr.gammaB = gammaFromMidtone(reference.statsB.mean, candidate.statsB.mean);
    corr.gammaY = gammaFromMidtone(reference.statsY.mean, candidate.statsY.mean);

    corr.saturation = static_cast<float>(qBound(0.5, 1.0 / out.metrics.uvEnergyRatio, 1.5));

    const double dR = reference.statsR.mean - candidate.statsR.mean;
    const double dG = reference.statsG.mean - candidate.statsG.mean;
    const double dB = reference.statsB.mean - candidate.statsB.mean;

    corr.whiteBalanceKelvin = 5600 + kelvinShiftFromBalance(dR, dB);
    corr.whiteBalanceTint = tintShiftFromGreen(dG);
    if (options.useVideoGain)
        corr.gainDb = gainDbFromLuma(-out.metrics.meanDeltaLuma);

    corr.clamp();
    out.suggested = corr;
    out.damped = corr.damped(options.damping);
    return out;
}

QVector<CameraMatchResult> ColorMatching::matchAll(
    const FrameColorAnalysis &reference,
    const QVector<QPair<QString, FrameColorAnalysis>> &cams,
    const ColorMatchOptions &options)
{
    QVector<CameraMatchResult> results;
    results.reserve(cams.size());
    for (const auto &pair : cams)
        results.push_back(matchToReference(reference, pair.second, pair.first, options));
    return results;
}