#include "MatchEngine.h"

#include <QtGlobal>

#include <algorithm>
#include <array>
#include <vector>
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

    if (st.liveLimits) {
        bool limited = false;
        const auto lim = [&](double v, double lo, double hi) {
            const double c = std::clamp(v, lo, hi);
            limited = limited || std::fabs(c - v) > 1e-9;
            return c;
        };
        const double level = kLumaR * d.ch[0].gain + kLumaG * d.ch[1].gain + kLumaB * d.ch[2].gain;
        const double lvl = lim(level, 0.75, 1.33); // encoded ≈ ±0.9 stop
        for (int c = 0; c < 3; ++c) {
            const double bal = level > 1e-6 ? d.ch[c].gain / level : 1.0;
            d.ch[c].gain = lim(bal, 0.85, 1.18) * lvl;
            d.ch[c].lift = lim(d.ch[c].lift, -0.05, 0.05);
            d.ch[c].gamma = lim(d.ch[c].gamma, -0.15, 0.15);
        }
        d.saturation = lim(d.saturation, 0.85, 1.15);
        if (limited)
            d.notes << QStringLiteral("шаг ограничен (безопасно для эфира) — следующий проход уточнит");
    }

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
        if (st.liveLimits) {
            const int kBase = static_cast<int>(k0);
            out.whiteBalanceKelvin = qBound(kBase - 1000, out.whiteBalanceKelvin, kBase + 1000);
            out.whiteBalanceTint = qBound(cur.whiteBalanceTint - 10, out.whiteBalanceTint, cur.whiteBalanceTint + 10);
        }
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

    // Blackmagic cameras (Luma Mix 100 %, the default) keep the luminance when R/G/B lift/gamma/gain
    // change - verified live on Micro Studio Camera 4K G2: R/G/B gain 1.23 -> 1.5 left the picture
    // level unchanged, Y gain 1.0 -> 1.3 raised it. So colour goes to R/G/B, level/tone to Y.
    out.gainR = static_cast<float>(cur.gainR * bal[0]);
    out.gainG = static_cast<float>(cur.gainG * bal[1]);
    out.gainB = static_cast<float>(cur.gainB * bal[2]);
    out.gainY = static_cast<float>(cur.gainY * levelForCc);
    const double liftCommon = kLumaR * d.ch[0].lift + kLumaG * d.ch[1].lift + kLumaB * d.ch[2].lift;
    const double gammaCommon = kLumaR * d.ch[0].gamma + kLumaG * d.ch[1].gamma + kLumaB * d.ch[2].gamma;
    out.liftR = static_cast<float>(cur.liftR + d.ch[0].lift - liftCommon);
    out.liftG = static_cast<float>(cur.liftG + d.ch[1].lift - liftCommon);
    out.liftB = static_cast<float>(cur.liftB + d.ch[2].lift - liftCommon);
    out.liftY = static_cast<float>(cur.liftY + liftCommon);
    out.gammaR = static_cast<float>(cur.gammaR + d.ch[0].gamma - gammaCommon);
    out.gammaG = static_cast<float>(cur.gammaG + d.ch[1].gamma - gammaCommon);
    out.gammaB = static_cast<float>(cur.gammaB + d.ch[2].gamma - gammaCommon);
    out.gammaY = static_cast<float>(cur.gammaY + gammaCommon);
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
    const float gy = ratio(p.gainY, cur.gainY);
    if (gy < 0.6f || gy > 1.6f)
        w << QStringLiteral("Gain Y (уровень) меняется в %1 раза").arg(gy, 0, 'f', 2);
    if (std::fabs(p.liftY - cur.liftY) > 0.15f)
        w << QStringLiteral("Lift Y сдвигается на %1").arg(p.liftY - cur.liftY, 0, 'f', 3);
    if (std::fabs(p.gammaY - cur.gammaY) > 0.35f)
        w << QStringLiteral("Gamma Y сдвигается на %1").arg(p.gammaY - cur.gammaY, 0, 'f', 3);
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

SignalCheck checkSignal(const QImage &imageIn)
{
    SignalCheck s;
    if (imageIn.isNull()) {
        s.noSignal = true;
        s.reason = QStringLiteral("нет кадра");
        return s;
    }
    // Fast (nearest) downscale keeps the sensor noise that tells a dark scene from a dead input.
    QImage image = imageIn.width() > 480 ? imageIn.scaledToWidth(480, Qt::FastTransformation) : imageIn;
    if (image.format() != QImage::Format_RGB32)
        image = image.convertToFormat(QImage::Format_RGB32);
    const int w = image.width();
    const int h = image.height();
    std::array<qint64, 256> hist{};
    double sum = 0.0, sum2 = 0.0, detail = 0.0;
    qint64 n = 0, dn = 0;
    QVector<int> prev(w, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        int left = 0;
        for (int x = 0; x < w; ++x) {
            const int l = (54 * qRed(row[x]) + 183 * qGreen(row[x]) + 19 * qBlue(row[x]) + 128) >> 8;
            ++hist[static_cast<size_t>(std::clamp(l, 0, 255))];
            sum += l;
            sum2 += static_cast<double>(l) * l;
            ++n;
            if (x > 0) {
                detail += std::abs(l - left);
                ++dn;
            }
            if (y > 0) {
                detail += std::abs(l - prev[x]);
                ++dn;
            }
            left = l;
            prev[x] = l;
        }
    }
    if (n == 0) {
        s.noSignal = true;
        s.reason = QStringLiteral("нет кадра");
        return s;
    }
    const auto quant = [&](double p) {
        const double target = p * static_cast<double>(n);
        qint64 acc = 0;
        for (int i = 0; i < 256; ++i) {
            acc += hist[static_cast<size_t>(i)];
            if (acc >= target)
                return i / 255.0;
        }
        return 1.0;
    };
    const double mean = sum / static_cast<double>(n);
    s.meanLuma = mean / 255.0;
    s.stdLuma = std::sqrt(std::max(0.0, sum2 / static_cast<double>(n) - mean * mean)) / 255.0;
    s.detail = dn > 0 ? detail / static_cast<double>(dn) / 255.0 : 0.0;
    s.p95Luma = quant(0.95);
    s.p99Luma = quant(0.99);
    const bool flat = s.stdLuma < 0.8 / 255.0 && s.detail < 0.5 / 255.0;
    const bool black = s.p99Luma < 4.0 / 255.0;
    if (flat || black) {
        s.noSignal = true;
        s.reason = black ? QStringLiteral("чёрный кадр без деталей — нет сигнала на входе?")
                         : QStringLiteral("однородный кадр без деталей — нет сигнала на входе?");
        return s;
    }
    if (s.meanLuma < 0.12 || s.p95Luma < 0.30) {
        s.lowLight = true;
        s.reason = QStringLiteral("мало света (средний уровень %1 IRE) — расчёт менее точный")
                       .arg(s.meanLuma * 100.0, 0, 'f', 0);
    }
    return s;
}

SceneStats sceneStats(const QImage &imageIn, Transfer transfer, QStringList *notes)
{
    SceneStats st;
    if (imageIn.isNull())
        return st;
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
    lumas.reserve(image.width() * image.height());
    double logSum = 0.0;
    int logN = 0;
    for (int yy = 0; yy < image.height(); ++yy) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(yy));
        for (int x = 0; x < image.width(); ++x) {
            const Rgb e{qRed(row[x]) / 255.0, qGreen(row[x]) / 255.0, qBlue(row[x]) / 255.0};
            const Rgb lin = decode(e, transfer);
            const double y = luma(lin);
            lumas << y;
            logSum += std::log(std::max(1e-4, y));
            ++logN;
            const double mx = std::max({e.r, e.g, e.b});
            const double ey = luma(e);
            // ≥3 % code: dark (dim room) pictures still contribute; noise floor and clipping excluded
            if (mx < 0.97 && ey > 0.03 && ey < 0.95)
                valid << Px{lin, y};
        }
    }
    if (lumas.isEmpty())
        return st;
    if (valid.size() < lumas.size() / 200) {
        if (notes)
            *notes << QStringLiteral("кадр почти чёрный или пересвечен — поправка не рассчитана");
        return st;
    }
    std::sort(lumas.begin(), lumas.end());
    const auto q = [&](double p) { return lumas[static_cast<int>(p * (lumas.size() - 1))]; };
    st.keyLinear = std::exp(logSum / std::max(1, logN));
    st.p25Linear = q(0.25);
    st.p50Linear = q(0.50);
    st.p75Linear = q(0.75);
    st.p90Linear = q(0.90);
    st.p98Linear = q(0.98);

    // Illuminant. Neutral surfaces (walls, grey/white objects) all share the chromaticity of the light,
    // so they form the densest compact cluster in log-chroma space (u = ln R/G, v = ln B/G) close to the
    // Planckian locus; coloured objects are spread out. The peak of a luminance-weighted 2-D histogram
    // restricted to a band around the locus is found first, then refined by the mean of the pixels
    // around it. Falls back to a grey-world start when no candidate exists (coloured stage light).
    Rgb ill{1.0, 1.0, 1.0};
    double neutralFraction = 0.0;
    {
        constexpr int kBins = 64;
        constexpr double kLo = -1.6, kHi = 1.6;
        constexpr double kStep = (kHi - kLo) / kBins;
        static const QVector<std::pair<double, double>> locus = [] {
            QVector<std::pair<double, double>> pts;
            const Rgb ref = planckianWhite(6500.0);
            for (double k = 2000.0; k <= 15000.0; k += 250.0) {
                const Rgb w = planckianWhite(k);
                pts.push_back({std::log((w.r / ref.r) / (w.g / ref.g)), std::log((w.b / ref.b) / (w.g / ref.g))});
            }
            return pts;
        }();
        const auto nearLocus = [&](double u, double v) {
            double best = 1e9;
            for (const auto &pt : locus)
                best = std::min(best, std::hypot(u - pt.first, v - pt.second));
            return best < 0.25;
        };
        std::vector<double> hist(static_cast<size_t>(kBins * kBins), 0.0);
        std::vector<double> us(static_cast<size_t>(valid.size())), vs(static_cast<size_t>(valid.size()));
        for (int i = 0; i < valid.size(); ++i) {
            const Px &p = valid.at(i);
            const double g = std::max(1e-5, p.lin.g);
            const double u = std::log(std::max(1e-5, p.lin.r) / g);
            const double v = std::log(std::max(1e-5, p.lin.b) / g);
            us[static_cast<size_t>(i)] = u;
            vs[static_cast<size_t>(i)] = v;
            const int bu = static_cast<int>((u - kLo) / kStep);
            const int bv = static_cast<int>((v - kLo) / kStep);
            if (bu < 0 || bv < 0 || bu >= kBins || bv >= kBins)
                continue;
            hist[static_cast<size_t>(bv * kBins + bu)] += std::sqrt(p.y);
        }
        double bestScore = 0.0;
        double peakU = 0.0, peakV = 0.0;
        bool havePeak = false;
        for (int bv = 1; bv < kBins - 1; ++bv)
            for (int bu = 1; bu < kBins - 1; ++bu) {
                const double cu = kLo + (bu + 0.5) * kStep;
                const double cv = kLo + (bv + 0.5) * kStep;
                double score = 0.0;
                for (int dv = -1; dv <= 1; ++dv)
                    for (int du = -1; du <= 1; ++du)
                        score += hist[static_cast<size_t>((bv + dv) * kBins + bu + du)];
                if (score > bestScore && nearLocus(cu, cv)) {
                    bestScore = score;
                    peakU = cu;
                    peakV = cv;
                    havePeak = true;
                }
            }
        if (havePeak) {
            // refine twice: mean of the cluster around the peak (bright-weighted)
            for (int pass = 0; pass < 2; ++pass) {
                double su = 0.0, sv = 0.0, sw = 0.0;
                int n = 0;
                const double radius = pass == 0 ? 0.12 : 0.08;
                for (int i = 0; i < valid.size(); ++i) {
                    const double du = us[static_cast<size_t>(i)] - peakU;
                    const double dv = vs[static_cast<size_t>(i)] - peakV;
                    if (du * du + dv * dv > radius * radius)
                        continue;
                    const double w = valid.at(i).y;
                    su += w * us[static_cast<size_t>(i)];
                    sv += w * vs[static_cast<size_t>(i)];
                    sw += w;
                    ++n;
                }
                if (n < 30 || sw <= 0.0)
                    break;
                peakU = su / sw;
                peakV = sv / sw;
                neutralFraction = static_cast<double>(n) / std::max(1, static_cast<int>(valid.size()));
            }
            const Rgb est{std::exp(peakU), 1.0, std::exp(peakV)};
            const double ny = luma(est);
            ill = {est.r / ny, est.g / ny, est.b / ny};
        } else {
            double sr = 0.0, sg = 0.0, sb = 0.0;
            for (const Px &p : valid) {
                sr += p.lin.r;
                sg += p.lin.g;
                sb += p.lin.b;
            }
            const Rgb gw{sr, sg, sb};
            const double ny = luma(gw);
            if (ny > 1e-9)
                ill = {gw.r / ny, gw.g / ny, gw.b / ny};
            neutralFraction = 0.0;
        }
    }
    st.illuminantRaw = ill;
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
    st.illuminant = ill;
    st.neutralFraction = neutralFraction;

    // Median chroma of mid-tones once the light is neutralised (for a conservative saturation match).
    QVector<double> chroma;
    for (const Px &p : valid) {
        if (p.y < 0.02 || p.y > 0.7)
            continue;
        const Rgb e = encode(Rgb{p.lin.r / ill.r, p.lin.g / ill.g, p.lin.b / ill.b}, transfer);
        chroma << (std::max({e.r, e.g, e.b}) - std::min({e.r, e.g, e.b}));
    }
    if (chroma.size() >= 50) {
        std::nth_element(chroma.begin(), chroma.begin() + chroma.size() / 2, chroma.end());
        st.medianChroma = chroma[chroma.size() / 2];
    }
    st.valid = true;
    return st;
}

ReferenceMatch referenceMatch(const SceneStats &cam, const SceneStats &ref, const Settings &st, QStringList *notes)
{
    ReferenceMatch out;
    if (!cam.valid || !ref.valid)
        return out;
    // 1) colour of the light: the camera should render white the way the reference renders it.
    // Raw estimates: both cameras see the same light, the confidence pull must not bias one of them.
    const Rgb camIll = cam.illuminantRaw;
    const Rgb wantIll = st.balance ? ref.illuminantRaw : camIll;
    // 2) exposure: geometric mean of median and log-average ratios (robust to a few bright objects).
    double factor = 1.0;
    if (st.exposure) {
        const double rm = cam.p50Linear > 1e-5 ? ref.p50Linear / cam.p50Linear : 1.0;
        const double rk = cam.keyLinear > 1e-5 ? ref.keyLinear / cam.keyLinear : 1.0;
        factor = std::sqrt(std::max(1e-3, rm) * std::max(1e-3, rk));
        if (factor > 1.0 && cam.p98Linear * factor > 0.9) {
            factor = std::max(1.0, 0.9 / std::max(1e-6, cam.p98Linear));
            if (notes)
                *notes << QStringLiteral("уровень ограничен, чтобы не пересветить светлые участки");
        }
        if (factor > 2.0 || factor < 0.5) {
            factor = std::clamp(factor, 0.5, 2.0);
            if (notes)
                *notes << QStringLiteral("уровень ограничен ±1 ступенью за проход");
        }
    }
    const double grey = decode(kMidGreyEncoded, st.transfer);
    const auto lightPair = [&](double level, double targetLevel, double weight) {
        Pair p;
        p.measured = encode(Rgb{camIll.r * level, camIll.g * level, camIll.b * level}, st.transfer);
        p.target = encode(Rgb{wantIll.r * targetLevel, wantIll.g * targetLevel, wantIll.b * targetLevel},
                          st.transfer);
        p.neutral = true;
        p.weight = weight;
        return p;
    };
    // Grey seen by this camera vs the same grey seen by the reference (scene independent).
    out.pairs << lightPair(grey / std::max(1e-6, factor), grey, 3.0);
    // 3) tone (only when lift/gamma are enabled): luma percentiles, low weight - exact only if both
    //    cameras see a similar scene.
    if (st.lift || st.gamma) {
        const double cq[4] = {cam.p25Linear, cam.p50Linear, cam.p75Linear, cam.p90Linear};
        const double rq[4] = {ref.p25Linear, ref.p50Linear, ref.p75Linear, ref.p90Linear};
        for (int i = 0; i < 4; ++i)
            if (cq[i] > 1e-4 && rq[i] > 1e-4 && cq[i] < 0.95 && rq[i] < 0.95)
                out.pairs << lightPair(cq[i], st.exposure ? rq[i] : cq[i], 0.5);
        if (notes)
            *notes << QStringLiteral("лифт/гамма по распределению яркости — точны, только если камеры видят похожую сцену");
    }
    // 4) saturation: median chroma ratio, conservative.
    if (st.saturation && cam.medianChroma > 0.01 && ref.medianChroma > 0.01)
        out.saturation = std::clamp(ref.medianChroma / cam.medianChroma, 0.85, 1.15);
    return out;
}

QVector<Pair> autoPairs(const QImage &imageIn, const Settings &st, double targetGreyIre, AutoEstimate *est,
                        QStringList *notes)
{
    QVector<Pair> out;
    if (imageIn.isNull())
        return out;
    const SignalCheck signal = checkSignal(imageIn);
    if (signal.noSignal) {
        if (notes)
            *notes << signal.reason + QStringLiteral(" Поправка не рассчитана.");
        return out;
    }
    const SceneStats scene = sceneStats(imageIn, st.transfer, notes);
    if (!scene.valid)
        return out;
    const Rgb ill = scene.illuminant;
    const double key = scene.keyLinear;
    const double p98 = scene.p98Linear;

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
        est->neutralFraction = scene.neutralFraction;
    }
    return out;
}

} // namespace MatchEngine
