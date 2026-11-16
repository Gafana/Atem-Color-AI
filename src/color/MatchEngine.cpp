#include "MatchEngine.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

using namespace ColorScience;

namespace MatchEngine {

namespace {

constexpr double kMidGreyEncoded = 0.409; // BT.709 OETF of 18 % grey
constexpr double kTintPerUnit = 0.006;    // assumed relative green change per tint step (see docs)

double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

Rgb applySaturation(const Rgb &c, double s)
{
    if (std::fabs(s - 1.0) < 1e-9)
        return c;
    const double y = luma(c);
    return {y + s * (c.r - y), y + s * (c.g - y), y + s * (c.b - y)};
}

struct Sample
{
    double x;
    double t;
    double w;
};

/** 3x3 symmetric solve (Cramer); returns false when singular. */
bool solve3(double A[3][3], const double b[3], double out[3])
{
    const double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
                       - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
                       + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (std::fabs(det) < 1e-18)
        return false;
    for (int k = 0; k < 3; ++k) {
        double M[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                M[i][j] = (j == k) ? b[i] : A[i][j];
        out[k] = (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
                  - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
                  + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]))
                 / det;
    }
    return true;
}

/** Weighted LM fit of ChannelCurve to samples; disabled params stay at identity.
 *  Parameters: p0 = ln(gain), p1 = lift, p2 = gamma. Ridge priors keep ill-determined params near 0. */
ChannelCurve fitCurve(const QVector<Sample> &s, bool fitLift, bool fitGamma)
{
    ChannelCurve c;
    if (s.isEmpty())
        return c;
    // Closed-form gain as a start.
    double sxt = 0.0, sxx = 0.0;
    for (const Sample &k : s) {
        sxt += k.w * k.x * k.t;
        sxx += k.w * k.x * k.x;
    }
    double p[3] = {sxx > 1e-12 ? std::log(std::max(0.05, sxt / sxx)) : 0.0, 0.0, 0.0};
    if (!fitLift && !fitGamma) {
        c.gain = std::exp(p[0]);
        return c;
    }
    const bool en[3] = {true, fitLift, fitGamma};
    double wsum = 0.0;
    for (const Sample &k : s)
        wsum += k.w;
    const double prior[3] = {1e-4 * wsum, 0.02 * wsum, 0.01 * wsum};
    auto model = [](const double *q, double x) {
        ChannelCurve cc;
        cc.gain = std::exp(q[0]);
        cc.lift = q[1];
        cc.gamma = q[2];
        return cc.apply(x);
    };
    auto cost = [&](const double *q) {
        double e = 0.0;
        for (const Sample &k : s) {
            const double r = model(q, k.x) - k.t;
            e += k.w * r * r;
        }
        for (int i = 0; i < 3; ++i)
            e += prior[i] * q[i] * q[i];
        return e;
    };
    double lambda = 1e-3;
    double current = cost(p);
    for (int iter = 0; iter < 80; ++iter) {
        double JtJ[3][3] = {};
        double Jtr[3] = {};
        for (const Sample &k : s) {
            const double f0 = model(p, k.x);
            double J[3] = {};
            for (int i = 0; i < 3; ++i) {
                if (!en[i])
                    continue;
                double q[3] = {p[0], p[1], p[2]};
                const double h = 1e-5;
                q[i] += h;
                J[i] = (model(q, k.x) - f0) / h;
            }
            const double r = f0 - k.t;
            for (int i = 0; i < 3; ++i) {
                Jtr[i] += k.w * J[i] * r;
                for (int j = 0; j < 3; ++j)
                    JtJ[i][j] += k.w * J[i] * J[j];
            }
        }
        double A[3][3];
        double b[3];
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j)
                A[i][j] = JtJ[i][j] + (i == j ? prior[i] : 0.0);
            A[i][i] *= (1.0 + lambda);
            b[i] = -(Jtr[i] + prior[i] * p[i]);
            if (!en[i]) {
                for (int j = 0; j < 3; ++j)
                    A[i][j] = A[j][i] = 0.0;
                A[i][i] = 1.0;
                b[i] = 0.0;
            }
        }
        double step[3];
        if (!solve3(A, b, step))
            break;
        double q[3] = {p[0] + step[0], p[1] + step[1], p[2] + step[2]};
        q[1] = std::max(-0.5, std::min(0.5, q[1]));
        q[2] = std::max(-2.0, std::min(2.0, q[2]));
        const double next = cost(q);
        if (next < current) {
            std::copy(q, q + 3, p);
            const double gainRel = std::fabs(current - next) / std::max(1e-12, current);
            current = next;
            lambda = std::max(1e-7, lambda * 0.3);
            if (gainRel < 1e-10)
                break;
        } else {
            lambda *= 10.0;
            if (lambda > 1e8)
                break;
        }
    }
    c.gain = std::exp(p[0]);
    c.lift = p[1];
    c.gamma = p[2];
    return c;
}

double rmsIre(const QVector<Pair> &pairs, const Delta *d)
{
    double e = 0.0, w = 0.0;
    for (const Pair &p : pairs) {
        const Rgb y = d ? d->apply(p.measured) : p.measured;
        for (int c = 0; c < 3; ++c) {
            const double r = (y[c] - p.target[c]) * 100.0;
            e += p.weight * r * r;
            w += p.weight;
        }
    }
    return w > 0.0 ? std::sqrt(e / w) : 0.0;
}

double meanDeltaE(const QVector<Pair> &pairs, const Delta *d, Transfer t)
{
    double sum = 0.0;
    int n = 0;
    for (const Pair &p : pairs) {
        const Rgb y = d ? d->apply(p.measured) : p.measured;
        const Rgb yc{clamp01(y.r), clamp01(y.g), clamp01(y.b)};
        sum += deltaE2000(linear709ToLabD65(decode(yc, t)), linear709ToLabD65(decode(p.target, t)));
        ++n;
    }
    return n > 0 ? sum / n : -1.0;
}

double linearRatioOfEncodedGain(double g, Transfer t)
{
    const double a = decode(kMidGreyEncoded, t);
    const double b = decode(clamp01(kMidGreyEncoded * g), t);
    if (kMidGreyEncoded * g >= 1.0)
        return std::pow(g, 1.0 / 0.45);
    return a > 1e-9 ? b / a : 1.0;
}

} // namespace

double ChannelCurve::apply(double x) const
{
    const double v = x * gain + lift * (1.0 - x);
    if (std::fabs(gamma) < 1e-12)
        return v;
    const double e = std::pow(2.0, -gamma);
    return v >= 0.0 ? std::pow(v, e) : -std::pow(-v, e);
}

Rgb Delta::apply(const Rgb &x) const
{
    Rgb y{ch[0].apply(x.r), ch[1].apply(x.g), ch[2].apply(x.b)};
    return applySaturation(y, saturation);
}

Delta solve(const QVector<Pair> &pairsIn, const Settings &st)
{
    Delta d;
    QVector<Pair> pairs;
    for (Pair p : pairsIn) {
        if (p.weight <= 0.0)
            continue;
        const double mx = std::max({p.measured.r, p.measured.g, p.measured.b});
        const double mn = std::min({p.measured.r, p.measured.g, p.measured.b});
        if (mx >= 0.985 || mn <= 0.01)
            p.weight *= 0.1; // clipped samples carry little information
        pairs << p;
    }
    d.pairCount = pairs.size();
    if (pairs.isEmpty()) {
        d.notes << QStringLiteral("нет данных для расчёта");
        return d;
    }
    // Distinct measured levels determine how many curve parameters are identifiable.
    QVector<double> levels;
    for (const Pair &p : pairs) {
        const double y = luma(p.measured);
        bool seen = false;
        for (double l : levels)
            if (std::fabs(l - y) < 0.04)
                seen = true;
        if (!seen)
            levels << y;
    }
    bool fitLift = st.lift && levels.size() >= 2;
    bool fitGamma = st.gamma && levels.size() >= 3;
    if (st.lift && !fitLift)
        d.notes << QStringLiteral("лифт не считается: в образце один уровень яркости");
    if (st.gamma && !fitGamma)
        d.notes << QStringLiteral("гамма не считается: нужно ≥3 разных уровня яркости");

    if (st.balance) {
        for (int c = 0; c < 3; ++c) {
            QVector<Sample> s;
            for (const Pair &p : pairs)
                s << Sample{p.measured[c], p.target[c], p.weight * (p.neutral ? 3.0 : 1.0)};
            d.ch[c] = fitCurve(s, fitLift, fitGamma);
        }
        if (!st.exposure) {
            // keep mid-grey luminance unchanged: only the balance between channels moves
            for (int it = 0; it < 4; ++it) {
                const Rgb y{d.ch[0].apply(kMidGreyEncoded), d.ch[1].apply(kMidGreyEncoded),
                            d.ch[2].apply(kMidGreyEncoded)};
                const double e = luma(y) / kMidGreyEncoded;
                if (e <= 1e-6)
                    break;
                for (int c = 0; c < 3; ++c) {
                    const double expo = std::pow(2.0, -d.ch[c].gamma);
                    d.ch[c].gain /= std::pow(e, 1.0 / std::max(0.2, expo));
                }
            }
        }
    } else if (st.exposure || fitLift || fitGamma) {
        // joint fit: one curve for all channels (no colour change)
        QVector<Sample> s;
        for (const Pair &p : pairs)
            for (int c = 0; c < 3; ++c)
                s << Sample{p.measured[c], p.target[c], p.weight};
        ChannelCurve joint = fitCurve(s, fitLift, fitGamma);
        if (!st.exposure)
            joint.gain = 1.0;
        for (int c = 0; c < 3; ++c)
            d.ch[c] = joint;
    }

    if (st.saturation) {
        double num = 0.0, den = 0.0;
        int chromatic = 0;
        for (const Pair &p : pairs) {
            if (p.neutral)
                continue;
            const Rgb y{d.ch[0].apply(p.measured.r), d.ch[1].apply(p.measured.g), d.ch[2].apply(p.measured.b)};
            const double ym = luma(y);
            const double yt = luma(p.target);
            const double cm[3] = {y.r - ym, y.g - ym, y.b - ym};
            const double ct[3] = {p.target.r - yt, p.target.g - yt, p.target.b - yt};
            const double mag = std::sqrt(cm[0] * cm[0] + cm[1] * cm[1] + cm[2] * cm[2]);
            if (mag < 0.02)
                continue;
            ++chromatic;
            num += p.weight * (cm[0] * ct[0] + cm[1] * ct[1] + cm[2] * ct[2]);
            den += p.weight * mag * mag;
        }
        if (chromatic >= 4 && den > 1e-9)
            d.saturation = std::max(0.6, std::min(1.6, num / den));
        else
            d.notes << QStringLiteral("насыщенность не считается: мало цветных образцов");
    }

    // strength
    const double s = std::max(0.0, std::min(1.0, st.strength));
    for (int c = 0; c < 3; ++c) {
        d.ch[c].gain = std::pow(d.ch[c].gain, s);
        d.ch[c].lift *= s;
        d.ch[c].gamma *= s;
    }
    d.saturation = std::pow(d.saturation, s);

    d.rmsBeforeIre = rmsIre(pairs, nullptr);
    d.rmsAfterIre = rmsIre(pairs, &d);
    d.deBefore = meanDeltaE(pairs, nullptr, st.transfer);
    d.deAfter = meanDeltaE(pairs, &d, st.transfer);
    d.valid = true;
    return d;
}

CameraCorrectionBundle compose(const CameraCorrectionBundle &cur, const Delta &d, const Settings &st,
                               QStringList *notes)
{
    CameraCorrectionBundle out = cur;
    if (!d.valid)
        return out;
    double g[3] = {d.ch[0].gain, d.ch[1].gain, d.ch[2].gain};

    // Separate overall level (luma-weighted common factor) from the colour balance.
    const double level = kLumaR * g[0] + kLumaG * g[1] + kLumaB * g[2];
    double bal[3] = {g[0] / level, g[1] / level, g[2] / level};
    double levelForCc = level;

    if (st.exposure && st.exposureVia != ExposureVia::CcGain && std::fabs(level - 1.0) > 1e-4) {
        const double linear = linearRatioOfEncodedGain(level, st.transfer);
        const double stops = std::log2(std::max(1e-6, linear));
        if (st.exposureVia == ExposureVia::SensorGain) {
            const int db = static_cast<int>(std::lround(cur.gainDb + 6.02 * stops));
            out.gainDb = qBound(-12, db, 36);
            const double appliedStops = (out.gainDb - cur.gainDb) / 6.02;
            levelForCc = level / std::pow(2.0, appliedStops * 0.45);
            if (notes)
                *notes << QStringLiteral("экспозиция через усиление: %1 dB → %2 dB")
                              .arg(cur.gainDb)
                              .arg(out.gainDb);
        } else if (st.exposureVia == ExposureVia::Iris) {
            if (cur.irisFStopAv >= 0.0f) {
                out.irisFStopAv = static_cast<float>(qBound(0.0, cur.irisFStopAv - stops, 16.0));
                const double appliedStops = cur.irisFStopAv - out.irisFStopAv;
                levelForCc = level / std::pow(2.0, appliedStops * 0.45);
                if (notes)
                    *notes << QStringLiteral("экспозиция через диафрагму: f/%1 → f/%2")
                                  .arg(std::sqrt(std::pow(2.0, cur.irisFStopAv)), 0, 'f', 1)
                                  .arg(std::sqrt(std::pow(2.0, out.irisFStopAv)), 0, 'f', 1);
            } else if (notes) {
                *notes << QStringLiteral("диафрагма камеры неизвестна — уровень через CC Gain");
            }
        }
    }

    if (st.balance && st.balanceVia == BalanceVia::WhiteBalance) {
        // Express the balance as a change of the camera's white balance (linear sensor gains).
        Rgb m{linearRatioOfEncodedGain(bal[0] / bal[1], st.transfer), 1.0,
              linearRatioOfEncodedGain(bal[2] / bal[1], st.transfer)};
        const double k0 = cur.whiteBalanceKelvin > 0 ? cur.whiteBalanceKelvin : 5600.0;
        const Rgb w0 = planckianWhite(k0);
        const Rgb wantWhite{w0.r / m.r, 1.0, w0.b / m.b};
        const double k1 = estimateKelvin(wantWhite);
        const Rgb w1 = planckianWhite(k1);
        // achieved linear multipliers a = w0 / w1; residual green/magenta goes to tint
        const double ar = w0.r / w1.r;
        const double ab = w0.b / w1.b;
        const double rho = std::sqrt((m.r / ar) * (m.b / ab));
        const int tint = static_cast<int>(std::lround(cur.whiteBalanceTint + std::log(rho) / kTintPerUnit));
        out.whiteBalanceKelvin = qBound(2500, static_cast<int>(std::lround(k1 / 50.0) * 50), 10000);
        out.whiteBalanceTint = qBound(-50, tint, 50);
        bal[0] = bal[1] = bal[2] = 1.0;
        if (notes)
            *notes << QStringLiteral("баланс через ББ камеры: %1 K / tint %2 → %3 K / tint %4")
                          .arg(cur.whiteBalanceKelvin)
                          .arg(cur.whiteBalanceTint)
                          .arg(out.whiteBalanceKelvin)
                          .arg(out.whiteBalanceTint);
    }
    if (!st.balance)
        bal[0] = bal[1] = bal[2] = 1.0;
    if (!st.exposure)
        levelForCc = 1.0;

    out.gainR = static_cast<float>(cur.gainR * bal[0] * levelForCc);
    out.gainG = static_cast<float>(cur.gainG * bal[1] * levelForCc);
    out.gainB = static_cast<float>(cur.gainB * bal[2] * levelForCc);
    out.liftR = static_cast<float>(cur.liftR + d.ch[0].lift);
    out.liftG = static_cast<float>(cur.liftG + d.ch[1].lift);
    out.liftB = static_cast<float>(cur.liftB + d.ch[2].lift);
    out.gammaR = static_cast<float>(cur.gammaR + d.ch[0].gamma);
    out.gammaG = static_cast<float>(cur.gammaG + d.ch[1].gamma);
    out.gammaB = static_cast<float>(cur.gammaB + d.ch[2].gamma);
    if (st.saturation)
        out.saturation = static_cast<float>(cur.saturation * d.saturation);
    out.clamp();
    return out;
}

QStringList sanityWarnings(const CameraCorrectionBundle &cur, const CameraCorrectionBundle &p)
{
    QStringList w;
    const auto ratio = [](float a, float b) { return b > 1e-6f ? a / b : 1.0f; };
    const float gains[3] = {ratio(p.gainR, cur.gainR), ratio(p.gainG, cur.gainG), ratio(p.gainB, cur.gainB)};
    const char *names[3] = {"R", "G", "B"};
    for (int i = 0; i < 3; ++i)
        if (gains[i] < 0.6f || gains[i] > 1.6f)
            w << QStringLiteral("Gain %1 меняется в %2 раза").arg(QLatin1String(names[i])).arg(gains[i], 0, 'f', 2);
    const float lifts[3] = {p.liftR - cur.liftR, p.liftG - cur.liftG, p.liftB - cur.liftB};
    for (int i = 0; i < 3; ++i)
        if (std::fabs(lifts[i]) > 0.15f)
            w << QStringLiteral("Lift %1 сдвигается на %2").arg(QLatin1String(names[i])).arg(lifts[i], 0, 'f', 3);
    const float gammas[3] = {p.gammaR - cur.gammaR, p.gammaG - cur.gammaG, p.gammaB - cur.gammaB};
    for (int i = 0; i < 3; ++i)
        if (std::fabs(gammas[i]) > 0.35f)
            w << QStringLiteral("Gamma %1 сдвигается на %2").arg(QLatin1String(names[i])).arg(gammas[i], 0, 'f', 3);
    if (std::abs(p.whiteBalanceKelvin - cur.whiteBalanceKelvin) > 1500)
        w << QStringLiteral("ББ меняется на %1 K").arg(p.whiteBalanceKelvin - cur.whiteBalanceKelvin);
    if (std::abs(p.whiteBalanceTint - cur.whiteBalanceTint) > 20)
        w << QStringLiteral("Tint меняется на %1").arg(p.whiteBalanceTint - cur.whiteBalanceTint);
    if (std::abs(p.gainDb - cur.gainDb) > 9)
        w << QStringLiteral("Усиление меняется на %1 dB").arg(p.gainDb - cur.gainDb);
    if (cur.irisFStopAv >= 0.0f && p.irisFStopAv >= 0.0f && std::fabs(p.irisFStopAv - cur.irisFStopAv) > 1.5f)
        w << QStringLiteral("Диафрагма меняется на %1 ст.").arg(p.irisFStopAv - cur.irisFStopAv, 0, 'f', 1);
    const float sat = ratio(p.saturation, cur.saturation);
    if (sat < 0.7f || sat > 1.4f)
        w << QStringLiteral("Насыщенность меняется в %1 раза").arg(sat, 0, 'f', 2);
    return w;
}

double histogramQuantile(const std::array<quint32, kHistogramBins> &hist, double p)
{
    // All bins are counted (excluding clipped bins would shift the quantiles of a brighter camera
    // relative to a darker one); clipped quantiles are down-weighted later in solve().
    quint64 total = 0;
    for (int i = 0; i < kHistogramBins; ++i)
        total += hist[static_cast<size_t>(i)];
    if (total == 0)
        return -1.0;
    const double target = p * static_cast<double>(total);
    double acc = 0.0;
    for (int i = 0; i < kHistogramBins; ++i) {
        const double h = hist[static_cast<size_t>(i)];
        if (acc + h >= target && h > 0) {
            const double frac = (target - acc) / h;
            return std::max(0.0, std::min(1.0, (i - 0.5 + frac) / 255.0));
        }
        acc += h;
    }
    return 1.0;
}

QVector<Pair> quantilePairs(const FrameColorAnalysis &m, const FrameColorAnalysis &t)
{
    QVector<Pair> out;
    const double ps[] = {0.05, 0.15, 0.30, 0.50, 0.70, 0.85, 0.95};
    const double ws[] = {0.6, 0.8, 1.0, 1.2, 1.0, 0.8, 0.6};
    for (int i = 0; i < 7; ++i) {
        Pair p;
        p.measured = {histogramQuantile(m.histR, ps[i]), histogramQuantile(m.histG, ps[i]),
                      histogramQuantile(m.histB, ps[i])};
        p.target = {histogramQuantile(t.histR, ps[i]), histogramQuantile(t.histG, ps[i]),
                    histogramQuantile(t.histB, ps[i])};
        if (p.measured.r < 0 || p.measured.g < 0 || p.measured.b < 0 || p.target.r < 0 || p.target.g < 0
            || p.target.b < 0)
            continue;
        p.weight = ws[i];
        out << p;
    }
    return out;
}

Rgb regionMean(const QImage &imageIn, const QRect &rectIn)
{
    if (imageIn.isNull())
        return {};
    const QImage image = imageIn.format() == QImage::Format_RGB32 ? imageIn : imageIn.convertToFormat(QImage::Format_RGB32);
    const QRect rect = rectIn.isEmpty() ? image.rect() : rectIn.intersected(image.rect());
    if (rect.isEmpty())
        return {};
    const int step = std::max(1, static_cast<int>(std::sqrt(rect.width() * static_cast<double>(rect.height()) / 40000.0)));
    std::array<QVector<int>, 3> v;
    for (int y = rect.top(); y <= rect.bottom(); y += step) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = rect.left(); x <= rect.right(); x += step) {
            v[0] << qRed(row[x]);
            v[1] << qGreen(row[x]);
            v[2] << qBlue(row[x]);
        }
    }
    Rgb out;
    for (int c = 0; c < 3; ++c) {
        std::sort(v[c].begin(), v[c].end());
        const int n = v[c].size();
        const int cut = n / 10;
        double s = 0.0;
        int k = 0;
        for (int i = cut; i < n - cut; ++i) {
            s += v[c][i];
            ++k;
        }
        out[c] = k > 0 ? s / k / 255.0 : 0.0;
    }
    return out;
}

QVector<Pair> autoPairs(const QImage &imageIn, const Settings &st, double targetGreyIre, AutoEstimate *est,
                        QStringList *notes)
{
    QVector<Pair> out;
    if (imageIn.isNull())
        return out;
    QImage image = imageIn.width() > 480 ? imageIn.scaledToWidth(480, Qt::SmoothTransformation) : imageIn;
    if (image.format() != QImage::Format_RGB32)
        image = image.convertToFormat(QImage::Format_RGB32);
    struct Px
    {
        Rgb lin;
        double y;
    };
    QVector<Px> valid;
    QVector<double> lumas;
    double logSum = 0.0;
    int logN = 0;
    for (int yy = 0; yy < image.height(); ++yy) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(yy));
        for (int x = 0; x < image.width(); ++x) {
            const Rgb e{qRed(row[x]) / 255.0, qGreen(row[x]) / 255.0, qBlue(row[x]) / 255.0};
            const Rgb lin = decode(e, st.transfer);
            const double y = luma(lin);
            lumas << y;
            logSum += std::log(std::max(1e-4, y));
            ++logN;
            const double mx = std::max({e.r, e.g, e.b});
            const double ey = luma(e);
            if (mx < 0.97 && ey > 0.08 && ey < 0.95)
                valid << Px{lin, y};
        }
    }
    if (lumas.isEmpty())
        return out;
    // A black / no-signal frame must not produce a correction (it would ask for +4 stops).
    if (valid.size() < lumas.size() / 100) {
        if (notes)
            *notes << QStringLiteral("кадр почти чёрный или пересвечен — нет сигнала? поправка не рассчитана");
        return out;
    }
    std::sort(lumas.begin(), lumas.end());
    const double key = std::exp(logSum / std::max(1, logN));
    const double p98 = lumas[static_cast<int>(0.98 * (lumas.size() - 1))];

    // Illuminant: bright-weighted mean of near-neutral pixels, refined twice (grey-edge/white-patch hybrid).
    Rgb ill{1.0, 1.0, 1.0};
    double neutralFraction = 0.0;
    for (int pass = 0; pass < 3; ++pass) {
        double sr = 0.0, sg = 0.0, sb = 0.0, sw = 0.0;
        int n = 0;
        const double chromaMax = pass == 0 ? 1e9 : (pass == 1 ? 0.35 : 0.18);
        for (const Px &p : valid) {
            const Rgb q{p.lin.r / ill.r, p.lin.g / ill.g, p.lin.b / ill.b};
            const double mx = std::max({q.r, q.g, q.b});
            const double mn = std::min({q.r, q.g, q.b});
            if (mx <= 1e-6 || (mx - mn) / mx > chromaMax)
                continue;
            const double w = p.y; // bright pixels describe the illuminant better
            sr += w * p.lin.r;
            sg += w * p.lin.g;
            sb += w * p.lin.b;
            sw += w;
            ++n;
        }
        if (n < 50 || sw <= 0.0)
            break;
        const Rgb next{sr / sw, sg / sw, sb / sw};
        const double ny = luma(next);
        if (ny <= 1e-9)
            break;
        ill = {next.r / ny, next.g / ny, next.b / ny};
        neutralFraction = static_cast<double>(n) / std::max(1, static_cast<int>(valid.size()));
    }
    // Confidence: few neutral pixels (coloured stage light, saturated scene) means the estimate is
    // mostly "grey world" and unreliable -> pull it towards neutral instead of over-correcting.
    const double confidence = std::clamp(neutralFraction / 0.10, 0.25, 1.0);
    if (neutralFraction < 0.02 && notes)
        *notes << QStringLiteral("в кадре мало нейтральных участков — баланс ослаблен (менее надёжно)");
    {
        bool limited = false;
        for (int c = 0; c < 3; ++c) {
            double v = std::pow(ill[c], confidence);
            // physically plausible range of a white illuminant around D65 (~2500K..10000K)
            const double lim = std::clamp(v, 0.55, 1.8);
            limited = limited || lim != v;
            ill[c] = lim;
        }
        const double ny = luma(ill);
        for (int c = 0; c < 3; ++c)
            ill[c] /= ny;
        if (limited && notes)
            *notes << QStringLiteral("оценка цвета света вне правдоподобного диапазона — поправка ограничена");
    }

    const double targetLin = decode(targetGreyIre / 100.0, st.transfer);
    double factor = key > 1e-6 ? targetLin / key : 1.0;
    if (factor > 1.0 && p98 * factor > 0.9) {
        factor = std::max(1.0, 0.9 / std::max(1e-6, p98));
        if (notes)
            *notes << QStringLiteral("уровень ограничен, чтобы не пересветить светлые участки");
    }
    // Live-safe step: at most ±1 stop of linear light per pass; repeated passes converge.
    if (factor > 2.0 || factor < 0.5) {
        factor = std::clamp(factor, 0.5, 2.0);
        if (notes)
            *notes << QStringLiteral("уровень ограничен ±1 ступенью за один проход — повторите захват");
    }
    if (!st.exposure)
        factor = 1.0;
    // Pair at mid-grey: measured = what a neutral grey of the scene looks like now; target = neutral.
    const double ref = decode(kMidGreyEncoded, st.transfer) / std::max(1e-6, factor);
    Pair mid;
    mid.measured = encode(Rgb{ill.r * ref, ill.g * ref, ill.b * ref}, st.transfer);
    mid.target = encode(Rgb{ref * factor, ref * factor, ref * factor}, st.transfer);
    if (!st.balance)
        mid.target = encode(Rgb{ill.r * ref * factor, ill.g * ref * factor, ill.b * ref * factor}, st.transfer);
    mid.neutral = true;
    out << mid;
    if (est) {
        est->illuminantLinear = ill;
        est->keyLinear = key;
        est->p98Linear = p98;
        est->neutralFraction = neutralFraction;
    }
    return out;
}

} // namespace MatchEngine
