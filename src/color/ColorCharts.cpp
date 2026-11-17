#include "ColorCharts.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

using namespace ColorScience;

namespace {

QVector<ChartPatchRef> colorCheckerClassic2014()
{
    // X-Rite ColorChecker Classic, "after November 2014" formulation, CIELAB D50.
    // Source: X-Rite published data as distributed by colour-science
    // (colour.CCS_COLOURCHECKERS['ColorChecker24 - After November 2014']).
    return {
        {"Dark skin", 37.54, 14.37, 14.92},     {"Light skin", 64.66, 19.27, 17.50},
        {"Blue sky", 49.32, -3.82, -22.54},     {"Foliage", 43.46, -12.74, 22.72},
        {"Blue flower", 54.94, 9.61, -24.79},   {"Bluish green", 70.48, -32.26, -0.37},
        {"Orange", 62.73, 35.83, 56.50},        {"Purplish blue", 39.43, 10.75, -45.17},
        {"Moderate red", 50.57, 48.64, 16.67},  {"Purple", 30.10, 22.54, -20.87},
        {"Yellow green", 71.77, -24.13, 58.19}, {"Orange yellow", 71.51, 18.24, 67.37},
        {"Blue", 28.37, 15.42, -49.80},         {"Green", 54.38, -39.72, 32.27},
        {"Red", 42.43, 51.05, 28.62},           {"Yellow", 81.80, 2.67, 80.41},
        {"Magenta", 50.63, 51.28, -14.12},      {"Cyan", 49.57, -29.71, -28.32},
        {"White 9.5", 95.19, -1.03, 2.93},      {"Neutral 8", 81.29, -0.57, 0.44},
        {"Neutral 6.5", 66.89, -0.75, -0.06},   {"Neutral 5", 50.76, -0.13, 0.14},
        {"Neutral 3.5", 35.63, -0.46, -0.48},   {"Black 2", 20.64, 0.07, -0.46},
    };
}

QVector<ChartPatchRef> spyderCheckr24()
{
    // Datacolor SpyderCHECKR 24 (columns E..H of the SpyderCHECKR), CIELAB D50.
    // Source: Datacolor "SpyderCheckr reference data" PDF (2012). Native layout: 4 columns
    // (E, F, G, H) x 6 rows (1..6), card white at top-left. Stored row-major.
    const ChartPatchRef E[6] = {{"Card White", 96.04, 2.16, 2.60}, {"20% Gray", 80.44, 1.17, 2.05},
                                {"40% Gray", 65.52, 0.69, 1.86},   {"60% Gray", 49.62, 0.58, 1.56},
                                {"80% Gray", 33.55, 0.35, 1.40},   {"Card Black", 16.91, 1.43, -0.81}};
    const ChartPatchRef F[6] = {{"Primary Cyan", 47.12, -32.50, -28.75}, {"Primary Magenta", 50.49, 53.45, -13.55},
                                {"Primary Yellow", 83.61, 3.36, 87.02},  {"Primary Red", 41.05, 60.75, 31.17},
                                {"Primary Green", 54.14, -40.80, 34.75}, {"Primary Blue", 24.75, 13.78, -49.48}};
    const ChartPatchRef G[6] = {{"Primary Orange", 60.94, 38.21, 61.31}, {"Blueprint", 37.80, 7.30, -43.04},
                                {"Pink", 49.81, 48.50, 15.76},           {"Violet", 28.88, 19.36, -24.48},
                                {"Apple Green", 72.45, -23.60, 60.47},   {"Sunflower", 71.65, 23.74, 72.28}};
    const ChartPatchRef H[6] = {{"Aqua", 70.19, -31.90, 1.98},          {"Lavender", 54.38, 8.84, -25.71},
                                {"Evergreen", 42.03, -15.80, 22.93},     {"Steel Blue", 48.82, -5.11, -23.08},
                                {"Classic Light Skin", 65.10, 18.14, 18.68}, {"Classic Dark Skin", 36.13, 14.15, 15.78}};
    QVector<ChartPatchRef> out;
    for (int r = 0; r < 6; ++r) {
        out << E[r] << F[r] << G[r] << H[r];
    }
    return out;
}

double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

QPointF bilinear(const QVector<QPointF> &q, double u, double v)
{
    const QPointF top = q[0] * (1.0 - u) + q[1] * u;
    const QPointF bottom = q[3] * (1.0 - u) + q[2] * u;
    return top * (1.0 - v) + bottom * v;
}

} // namespace

namespace ColorCharts {

QVector<ChartPreset> presets()
{
    QVector<ChartPreset> out;
    {
        ChartPreset p;
        p.id = QStringLiteral("cc24");
        p.name = QStringLiteral("Calibrite / X-Rite ColorChecker Classic 24 (и Passport, Mini, Nano)");
        p.cols = 6;
        p.rows = 4;
        p.patches = colorCheckerClassic2014();
        p.neutral = {18, 19, 20, 21, 22, 23};
        p.source = QStringLiteral("X-Rite, данные после ноября 2014, CIELAB D50");
        out << p;
    }
    {
        ChartPreset p;
        p.id = QStringLiteral("spyder24");
        p.name = QStringLiteral("Datacolor SpyderCHECKR 24");
        p.cols = 4;
        p.rows = 6;
        p.patches = spyderCheckr24();
        p.neutral = {0, 4, 8, 12, 16, 20};
        p.source = QStringLiteral("Datacolor SpyderCheckr reference data (2012), CIELAB D50");
        out << p;
    }
    return out;
}

ChartPreset byId(const QString &id)
{
    const QVector<ChartPreset> all = presets();
    for (const ChartPreset &p : all)
        if (p.id == id)
            return p;
    return all.first();
}

QVector<Rgb> referenceEncoded(const ChartPreset &chart, Transfer transfer)
{
    QVector<Rgb> out;
    out.reserve(chart.patches.size());
    for (const ChartPatchRef &p : chart.patches) {
        const Rgb lin = labD50ToLinear709({p.L, p.a, p.b});
        out << Rgb{clamp01(encode(lin.r, transfer)), clamp01(encode(lin.g, transfer)),
                   clamp01(encode(lin.b, transfer))};
    }
    return out;
}

Rgb trimmedMean(const QImage &image, const QRect &rectIn)
{
    const QRect rect = rectIn.intersected(image.rect());
    if (rect.isEmpty())
        return {};
    QVector<int> rs, gs, bs;
    rs.reserve(rect.width() * rect.height());
    gs.reserve(rect.width() * rect.height());
    bs.reserve(rect.width() * rect.height());
    const QImage img = image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32
                           ? image
                           : image.convertToFormat(QImage::Format_RGB32);
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = rect.left(); x <= rect.right(); ++x) {
            rs << qRed(row[x]);
            gs << qGreen(row[x]);
            bs << qBlue(row[x]);
        }
    }
    auto tm = [](QVector<int> &v) {
        std::sort(v.begin(), v.end());
        const int n = v.size();
        const int cut = n / 10;
        double s = 0.0;
        int c = 0;
        for (int i = cut; i < n - cut; ++i) {
            s += v[i];
            ++c;
        }
        return c > 0 ? s / c / 255.0 : 0.0;
    };
    return {tm(rs), tm(gs), tm(bs)};
}

QVector<ChartPatchSample> sample(const QImage &image, const QVector<QPointF> &corners,
                                 const ChartPreset &chart, Transfer transfer, int *rotationOut)
{
    QVector<ChartPatchSample> best;
    if (image.isNull() || corners.size() != 4 || chart.patches.isEmpty())
        return best;
    const QVector<Rgb> refs = referenceEncoded(chart, transfer);
    double bestScore = 1e18;
    int bestRot = 0;
    for (int rot = 0; rot < 4; ++rot) {
        // Rotate the corner list: rot = k means the chart's native top-left is at user corner k.
        QVector<QPointF> q(4);
        for (int i = 0; i < 4; ++i)
            q[i] = corners[(i + rot) % 4];
        // Grid of the native chart mapped into the rotated quad.
        const int cols = chart.cols;
        const int rows = chart.rows;
        // Skip arrangements whose aspect does not fit the marked quad (by > 1.6x).
        const double qw = 0.5 * (std::hypot(q[1].x() - q[0].x(), q[1].y() - q[0].y())
                                 + std::hypot(q[2].x() - q[3].x(), q[2].y() - q[3].y()));
        const double qh = 0.5 * (std::hypot(q[3].x() - q[0].x(), q[3].y() - q[0].y())
                                 + std::hypot(q[2].x() - q[1].x(), q[2].y() - q[1].y()));
        if (qh > 1.0 && qw > 1.0) {
            const double aspectQuad = qw / qh;
            const double aspectChart = static_cast<double>(cols) / rows;
            const double ratio = aspectQuad > aspectChart ? aspectQuad / aspectChart : aspectChart / aspectQuad;
            if (ratio > 1.6)
                continue;
        }
        const double cellW = qw / cols;
        const double cellH = qh / rows;
        const int half = std::max(1, static_cast<int>(0.2 * std::min(cellW, cellH)));
        QVector<ChartPatchSample> samples;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int idx = r * cols + c;
                const QPointF center = bilinear(q, (c + 0.5) / cols, (r + 0.5) / rows);
                const QRect box(qRound(center.x()) - half, qRound(center.y()) - half, 2 * half + 1, 2 * half + 1);
                ChartPatchSample s;
                s.index = idx;
                s.name = QString::fromUtf8(chart.patches[idx].name);
                s.measured = trimmedMean(image, box);
                s.reference = refs[idx];
                s.neutral = chart.neutral.contains(idx);
                samples << s;
            }
        }
        // Score: neutral patches should be neutral (low chroma spread) and monotonic light->dark;
        // colour patches should correlate with references after a per-channel gain.
        double score = 0.0;
        for (int k = 0; k < chart.neutral.size(); ++k) {
            const Rgb m = samples[chart.neutral[k]].measured;
            const double mean = (m.r + m.g + m.b) / 3.0;
            score += 4.0 * (std::fabs(m.r - mean) + std::fabs(m.g - mean) + std::fabs(m.b - mean));
            if (k > 0) {
                const Rgb p = samples[chart.neutral[k - 1]].measured;
                const double pm = (p.r + p.g + p.b) / 3.0;
                if (mean > pm)
                    score += 2.0 * (mean - pm) + 0.2;
            }
        }
        for (int ch = 0; ch < 3; ++ch) {
            double sxy = 0.0, sxx = 0.0;
            for (const ChartPatchSample &s : samples) {
                sxy += s.measured[ch] * s.reference[ch];
                sxx += s.measured[ch] * s.measured[ch];
            }
            const double g = sxx > 1e-9 ? sxy / sxx : 1.0;
            for (const ChartPatchSample &s : samples)
                score += std::fabs(g * s.measured[ch] - s.reference[ch]) / samples.size();
        }
        if (score < bestScore) {
            bestScore = score;
            best = samples;
            bestRot = rot;
        }
    }
    if (rotationOut)
        *rotationOut = bestRot;
    return best;
}

} // namespace ColorCharts
