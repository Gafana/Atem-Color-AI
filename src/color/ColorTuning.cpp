#include "ColorTuning.h"

#include "ColorAnalysis.h"
#include "ColorCheckerData.h"

#include <QtMath>

namespace {

FrameColorAnalysis accumulateHist(const FrameColorAnalysis &acc, const FrameColorAnalysis &add)
{
    FrameColorAnalysis out = acc;
    if (add.sampleCount == 0)
        return out;
    for (int i = 0; i < kHistogramBins; ++i) {
        const size_t idx = static_cast<size_t>(i);
        out.histR[idx] += add.histR[idx];
        out.histG[idx] += add.histG[idx];
        out.histB[idx] += add.histB[idx];
        out.histY[idx] += add.histY[idx];
        out.histU[idx] += add.histU[idx];
        out.histV[idx] += add.histV[idx];
    }
    out.sampleCount += add.sampleCount;
    out.width = add.width;
    out.height = add.height;
    return out;
}

void finalizeStats(FrameColorAnalysis &analysis)
{
    if (analysis.sampleCount == 0)
        return;
    auto stats = [&](const std::array<quint32, kHistogramBins> &hist, ChannelStats &target) {
        double sum = 0.0;
        for (int i = 0; i < kHistogramBins; ++i)
            sum += static_cast<double>(i) * hist[static_cast<size_t>(i)];
        target.mean = sum / static_cast<double>(analysis.sampleCount);
        const auto percentile = [&](double p) {
            const quint64 targetCount =
                static_cast<quint64>(std::llround(p * static_cast<double>(analysis.sampleCount - 1)));
            quint64 acc = 0;
            for (int i = 0; i < kHistogramBins; ++i) {
                acc += hist[static_cast<size_t>(i)];
                if (acc > targetCount)
                    return static_cast<double>(i);
            }
            return 255.0;
        };
        target.median = percentile(0.5);
        target.p2 = percentile(0.02);
        target.p98 = percentile(0.98);
    };
    stats(analysis.histR, analysis.statsR);
    stats(analysis.histG, analysis.statsG);
    stats(analysis.histB, analysis.statsB);
    stats(analysis.histY, analysis.statsY);
    stats(analysis.histU, analysis.statsU);
    stats(analysis.histV, analysis.statsV);
}

CameraMatchResult bundleToResult(const QString &label, const FrameColorAnalysis &analysis,
                                 const CameraCorrectionBundle &corr, float damping)
{
    CameraMatchResult out;
    out.label = label;
    out.analysis = analysis;
    out.suggested = corr;
    out.suggested.clamp();
    out.damped = out.suggested.damped(damping);
    return out;
}

} // namespace

FrameColorAnalysis ColorTuning::averageAnalyses(const QVector<FrameColorAnalysis> &samples)
{
    FrameColorAnalysis acc;
    for (const FrameColorAnalysis &sample : samples)
        acc = ColorAnalysis::mergeAccumulate(acc, sample);
    ColorAnalysis::finalizeMerged(acc);
    return acc;
}

FrameColorAnalysis analyzeNormalizedRoi(const QImage &frame, const QRectF &normalizedRoi)
{
    if (frame.isNull() || normalizedRoi.isEmpty())
        return {};
    QRect rect(static_cast<int>(normalizedRoi.x() * frame.width()),
               static_cast<int>(normalizedRoi.y() * frame.height()),
               static_cast<int>(normalizedRoi.width() * frame.width()),
               static_cast<int>(normalizedRoi.height() * frame.height()));
    rect = rect.intersected(frame.rect());
    if (rect.isEmpty())
        return {};
    return ColorAnalysis::analyze(frame, rect);
}

FrameColorAnalysis ColorTuning::syntheticNeutralGrayReference(double luma)
{
    QImage img(64, 64, QImage::Format_RGB32);
    const int y = static_cast<int>(qBound(0.0, luma, 255.0));
    img.fill(qRgb(y, y, y));
    return ColorAnalysis::analyze(img);
}

CameraCorrectionBundle ColorTuning::correctionFromGrayCard(const FrameColorAnalysis &roiAnalysis,
                                                           const GrayCardTuningOptions &opts,
                                                           const ColorMatchOptions &matchOpts)
{
    Q_UNUSED(matchOpts);
    const double r = roiAnalysis.statsR.mean;
    const double g = roiAnalysis.statsG.mean;
    const double b = roiAnalysis.statsB.mean;
  const double avg = (r + g + b) / 3.0;
    if (avg < 1.0)
        return {};

    CameraCorrectionBundle corr;
    corr.gainR = static_cast<float>(qBound(0.25, avg / qMax(1.0, r), 4.0));
    corr.gainG = static_cast<float>(qBound(0.25, avg / qMax(1.0, g), 4.0));
    corr.gainB = static_cast<float>(qBound(0.25, avg / qMax(1.0, b), 4.0));
    corr.gainY = static_cast<float>(qBound(0.25, 1.0, 4.0));

    const double dR = avg - r;
    const double dB = avg - b;
    corr.whiteBalanceKelvin = 5600 + static_cast<int>(qRound((dB - dR) / 255.0 * 2500.0));
    corr.whiteBalanceTint = static_cast<int>(qBound(-50.0, -(g - avg) / 255.0 * 80.0, 50.0));

    if (opts.matchExposure) {
        const double deltaY = opts.targetLuma - roiAnalysis.statsY.mean;
        corr.gainDb = static_cast<int>(qBound(-12.0, deltaY / 255.0 * 18.0, 12.0));
        corr.liftY = static_cast<float>(qBound(-2.0, deltaY / 255.0 * 0.5, 2.0));
    }
    corr.clamp();
    return corr;
}

QVector<CameraMatchResult> ColorTuning::matchToReferenceFrame(
    const FrameColorAnalysis &reference,
    const QVector<QPair<QString, FrameColorAnalysis>> &cams,
    const ColorMatchOptions &options)
{
    return ColorMatching::matchAll(reference, cams, options);
}

QVector<CameraMatchResult> ColorTuning::matchCamerasToAverage(
    const QVector<QPair<QString, FrameColorAnalysis>> &included,
    const ColorMatchOptions &options)
{
    QVector<FrameColorAnalysis> parts;
    parts.reserve(included.size());
    for (const auto &pair : included)
        parts.push_back(pair.second);
    const FrameColorAnalysis mean = averageAnalyses(parts);

    QVector<CameraMatchResult> out;
    out.reserve(included.size());
    for (const auto &pair : included)
        out.push_back(ColorMatching::matchToReference(mean, pair.second, pair.first, options));
    return out;
}

CameraMatchResult ColorTuning::matchGrayCard(const QString &label, const FrameColorAnalysis &roi,
                                             const GrayCardTuningOptions &opts)
{
    ColorMatchOptions matchOpts;
    matchOpts.damping = 0.35f;
    const CameraCorrectionBundle corr = correctionFromGrayCard(roi, opts, matchOpts);
    return bundleToResult(label, roi, corr, matchOpts.damping);
}

CameraMatchResult ColorTuning::matchColorChecker(const QString &label,
                                                 const QVector<ColorCheckerPatch> &patches,
                                                 const ColorMatchOptions &options)
{
    FrameColorAnalysis measured;
    FrameColorAnalysis reference;
    if (patches.size() >= 24) {
        for (int i = 18; i < 24; ++i) {
            const QColor m = patches.at(i).measured;
            const QColor r = patches.at(i).reference;
            QImage mi(4, 4, QImage::Format_RGB32);
            QImage ri(4, 4, QImage::Format_RGB32);
            mi.fill(m);
            ri.fill(r);
            measured = accumulateHist(measured, ColorAnalysis::analyze(mi));
            reference = accumulateHist(reference, ColorAnalysis::analyze(ri));
        }
        finalizeStats(measured);
        finalizeStats(reference);
    }
    if (measured.sampleCount == 0 || reference.sampleCount == 0) {
        CameraMatchResult empty;
        empty.label = label;
        return empty;
    }
    return ColorMatching::matchToReference(reference, measured, label, options);
}

CameraMatchResult ColorTuning::matchNeutral(const QString &label, const QImage &frameImage,
                                            const NeutralBalanceOptions &opts)
{
    const FrameColorAnalysis analysis = ColorAnalysis::analyze(frameImage);
    const CameraCorrectionBundle corr = ColorAnalysis::neutralBalanceCorrection(frameImage, opts);
    CameraMatchResult out = bundleToResult(label, analysis, corr, 0.35f);
    out.suggested.saturation = 1.0f;
    out.suggested.liftR = out.suggested.liftG = out.suggested.liftB = out.suggested.liftY = 0.0f;
    out.suggested.gammaR = out.suggested.gammaG = out.suggested.gammaB = out.suggested.gammaY = 0.0f;
    out.suggested.gainR = out.suggested.gainG = out.suggested.gainB = out.suggested.gainY = 1.0f;
    out.suggested.gainDb = 0;
    out.damped = out.suggested.damped(0.35f);
    return out;
}