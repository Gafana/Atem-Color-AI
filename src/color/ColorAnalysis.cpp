#include "ColorAnalysis.h"

#include <QFile>
#include <algorithm>
#include <cmath>

namespace {

inline double clamp01(double v)
{
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

void accumulateStats(const std::array<quint32, kHistogramBins> &hist, quint64 total,
                     ChannelStats &stats)
{
    if (total == 0)
        return;

    double sum = 0.0;
  for (int i = 0; i < kHistogramBins; ++i)
        sum += static_cast<double>(i) * hist[static_cast<size_t>(i)];
    stats.mean = sum / static_cast<double>(total);

    const auto percentile = [&](double p) {
        const quint64 target = static_cast<quint64>(std::llround(p * static_cast<double>(total - 1)));
        quint64 acc = 0;
        for (int i = 0; i < kHistogramBins; ++i) {
            acc += hist[static_cast<size_t>(i)];
            if (acc > target)
                return static_cast<double>(i);
        }
        return 255.0;
    };

    stats.median = percentile(0.5);
    stats.p2 = percentile(0.02);
    stats.p98 = percentile(0.98);
}

void rgbToYuvBt709(int r, int g, int b, int &y, int &u, int &v)
{
    const double rd = r / 255.0;
    const double gd = g / 255.0;
    const double bd = b / 255.0;

    const double Y = 0.2126 * rd + 0.7152 * gd + 0.0722 * bd;
    const double U = -0.1146 * rd - 0.3854 * gd + 0.5 * bd + 0.5;
    const double V = 0.5 * rd - 0.4542 * gd - 0.0458 * bd + 0.5;

    y = static_cast<int>(std::lround(clamp01(Y) * 255.0));
    u = static_cast<int>(std::lround(clamp01(U) * 255.0));
    v = static_cast<int>(std::lround(clamp01(V) * 255.0));
}

} // namespace

QString ColorAnalysis::loadImage(const QString &path, QImage &out)
{
    if (!QFile::exists(path))
        return QStringLiteral("File not found: %1").arg(path);
    QImage img(path);
    if (img.isNull())
        return QStringLiteral("Failed to load image: %1").arg(path);
    out = img.convertToFormat(QImage::Format_RGB32);
    return {};
}

FrameColorAnalysis ColorAnalysis::analyze(const QImage &image)
{
    FrameColorAnalysis result;
    if (image.isNull())
        return result;

    const QImage rgb = image.format() == QImage::Format_RGB32
                           ? image
                           : image.convertToFormat(QImage::Format_RGB32);

    result.width = rgb.width();
    result.height = rgb.height();

    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb px = row[x];
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);

            ++result.histR[static_cast<size_t>(r)];
            ++result.histG[static_cast<size_t>(g)];
            ++result.histB[static_cast<size_t>(b)];

            int yy = 0;
            int uu = 0;
            int vv = 0;
            rgbToYuvBt709(r, g, b, yy, uu, vv);

            ++result.histY[static_cast<size_t>(yy)];
            ++result.histU[static_cast<size_t>(uu)];
            ++result.histV[static_cast<size_t>(vv)];

            const int us = (uu * (kVectorscopeSize - 1)) / 255;
            const int vs = (vv * (kVectorscopeSize - 1)) / 255;
            const size_t vsIdx = static_cast<size_t>(vs * kVectorscopeSize + us);
            ++result.vectorscopeUv[vsIdx];
            ++result.sampleCount;
        }
    }

    accumulateStats(result.histR, result.sampleCount, result.statsR);
    accumulateStats(result.histG, result.sampleCount, result.statsG);
    accumulateStats(result.histB, result.sampleCount, result.statsB);
    accumulateStats(result.histY, result.sampleCount, result.statsY);
    accumulateStats(result.histU, result.sampleCount, result.statsU);
    accumulateStats(result.histV, result.sampleCount, result.statsV);

    return result;
}

double ColorAnalysis::histogramL1(const std::array<quint32, kHistogramBins> &a,
                                  const std::array<quint32, kHistogramBins> &b)
{
    quint64 totalA = 0;
    quint64 totalB = 0;
    for (int i = 0; i < kHistogramBins; ++i) {
        totalA += a[static_cast<size_t>(i)];
        totalB += b[static_cast<size_t>(i)];
    }
    if (totalA == 0 || totalB == 0)
        return 1.0;

    double dist = 0.0;
    for (int i = 0; i < kHistogramBins; ++i) {
        const double pa = static_cast<double>(a[static_cast<size_t>(i)]) / static_cast<double>(totalA);
        const double pb = static_cast<double>(b[static_cast<size_t>(i)]) / static_cast<double>(totalB);
        dist += std::abs(pa - pb);
    }
    return dist;
}

FrameComparisonMetrics ColorAnalysis::compare(const FrameColorAnalysis &reference,
                                              const FrameColorAnalysis &candidate)
{
    FrameComparisonMetrics m;
    m.meanDeltaLuma = candidate.statsY.mean - reference.statsY.mean;
    m.histogramL1Rgb = (histogramL1(reference.histR, candidate.histR)
                        + histogramL1(reference.histG, candidate.histG)
                        + histogramL1(reference.histB, candidate.histB))
                       / 3.0;

    const double refSpread = reference.statsY.p98 - reference.statsY.p2;
    const double candSpread = candidate.statsY.p98 - candidate.statsY.p2;
    m.percentileSpreadDelta = candSpread - refSpread;

    const double refUv =
        reference.statsU.mean * reference.statsU.mean + reference.statsV.mean * reference.statsV.mean;
    const double candUv =
        candidate.statsU.mean * candidate.statsU.mean + candidate.statsV.mean * candidate.statsV.mean;
    if (refUv > 1e-6)
        m.uvEnergyRatio = std::sqrt(candUv / refUv);
    else
        m.uvEnergyRatio = 1.0;

    return m;
}