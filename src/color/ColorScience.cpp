#include "ColorScience.h"

#include <algorithm>
#include <cmath>

namespace ColorScience {

namespace {
constexpr double kPi = 3.14159265358979323846;

double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

// D65 / D50 reference whites (CIE 1931 2 deg, Y = 1)
constexpr double kD65[3] = {0.95047, 1.0, 1.08883};
constexpr double kD50[3] = {0.96422, 1.0, 0.82521};

double labF(double t)
{
    constexpr double d = 6.0 / 29.0;
    return t > d * d * d ? std::cbrt(t) : t / (3.0 * d * d) + 4.0 / 29.0;
}
double labFInv(double t)
{
    constexpr double d = 6.0 / 29.0;
    return t > d ? t * t * t : 3.0 * d * d * (t - 4.0 / 29.0);
}
} // namespace

double encode(double v, Transfer t)
{
    v = std::max(0.0, v);
    switch (t) {
    case Transfer::Bt709Oetf:
        return v < 0.018 ? 4.5 * v : 1.099 * std::pow(v, 0.45) - 0.099;
    case Transfer::Gamma24:
        return std::pow(v, 1.0 / 2.4);
    case Transfer::Srgb:
        return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
    }
    return v;
}

double decode(double e, Transfer t)
{
    e = std::max(0.0, e);
    switch (t) {
    case Transfer::Bt709Oetf:
        return e < 0.081 ? e / 4.5 : std::pow((e + 0.099) / 1.099, 1.0 / 0.45);
    case Transfer::Gamma24:
        return std::pow(e, 2.4);
    case Transfer::Srgb:
        return e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4);
    }
    return e;
}

Rgb encode(const Rgb &c, Transfer t) { return {encode(c.r, t), encode(c.g, t), encode(c.b, t)}; }
Rgb decode(const Rgb &c, Transfer t) { return {decode(c.r, t), decode(c.g, t), decode(c.b, t)}; }

std::array<double, 3> linearToXyz(const Rgb &c)
{
    return {0.4124564 * c.r + 0.3575761 * c.g + 0.1804375 * c.b,
            0.2126729 * c.r + 0.7151522 * c.g + 0.0721750 * c.b,
            0.0193339 * c.r + 0.1191920 * c.g + 0.9503041 * c.b};
}

Rgb xyzToLinear(const std::array<double, 3> &x)
{
    return {3.2404542 * x[0] - 1.5371385 * x[1] - 0.4985314 * x[2],
            -0.9692660 * x[0] + 1.8760108 * x[1] + 0.0415560 * x[2],
            0.0556434 * x[0] - 0.2040259 * x[1] + 1.0572252 * x[2]};
}

Lab xyzToLabD65(const std::array<double, 3> &x)
{
    const double fx = labF(x[0] / kD65[0]);
    const double fy = labF(x[1] / kD65[1]);
    const double fz = labF(x[2] / kD65[2]);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

Rgb labD50ToLinear709(const Lab &lab)
{
    const double fy = (lab.L + 16.0) / 116.0;
    const double fx = fy + lab.a / 500.0;
    const double fz = fy - lab.b / 200.0;
    const double X = kD50[0] * labFInv(fx);
    const double Y = kD50[1] * labFInv(fy);
    const double Z = kD50[2] * labFInv(fz);
    // Bradford D50 -> D65
    const std::array<double, 3> d65 = {0.9555766 * X - 0.0230393 * Y + 0.0631636 * Z,
                                       -0.0282895 * X + 1.0099416 * Y + 0.0210077 * Z,
                                       0.0122982 * X - 0.0204830 * Y + 1.3299098 * Z};
    return xyzToLinear(d65);
}

Lab linear709ToLabD65(const Rgb &lin) { return xyzToLabD65(linearToXyz(lin)); }

double deltaE76(const Lab &a, const Lab &b)
{
    return std::sqrt((a.L - b.L) * (a.L - b.L) + (a.a - b.a) * (a.a - b.a) + (a.b - b.b) * (a.b - b.b));
}

double deltaE2000(const Lab &lab1, const Lab &lab2)
{
    // Sharma, Wu, Dalal (2005) implementation notes.
    const double C1 = std::hypot(lab1.a, lab1.b);
    const double C2 = std::hypot(lab2.a, lab2.b);
    const double Cb = 0.5 * (C1 + C2);
    const double Cb7 = std::pow(Cb, 7.0);
    const double G = 0.5 * (1.0 - std::sqrt(Cb7 / (Cb7 + std::pow(25.0, 7.0))));
    const double a1 = (1.0 + G) * lab1.a;
    const double a2 = (1.0 + G) * lab2.a;
    const double C1p = std::hypot(a1, lab1.b);
    const double C2p = std::hypot(a2, lab2.b);
    auto hue = [](double b, double a) {
        if (a == 0.0 && b == 0.0)
            return 0.0;
        double h = std::atan2(b, a) * 180.0 / kPi;
        return h < 0.0 ? h + 360.0 : h;
    };
    const double h1 = hue(lab1.b, a1);
    const double h2 = hue(lab2.b, a2);
    const double dL = lab2.L - lab1.L;
    const double dC = C2p - C1p;
    double dh = 0.0;
    if (C1p * C2p != 0.0) {
        dh = h2 - h1;
        if (dh > 180.0)
            dh -= 360.0;
        else if (dh < -180.0)
            dh += 360.0;
    }
    const double dH = 2.0 * std::sqrt(C1p * C2p) * std::sin(dh * kPi / 360.0);
    const double Lb = 0.5 * (lab1.L + lab2.L);
    const double Cbp = 0.5 * (C1p + C2p);
    double hb = h1 + h2;
    if (C1p * C2p != 0.0) {
        if (std::fabs(h1 - h2) <= 180.0)
            hb = 0.5 * (h1 + h2);
        else
            hb = (h1 + h2 < 360.0) ? 0.5 * (h1 + h2 + 360.0) : 0.5 * (h1 + h2 - 360.0);
    }
    const double T = 1.0 - 0.17 * std::cos((hb - 30.0) * kPi / 180.0) + 0.24 * std::cos(2.0 * hb * kPi / 180.0)
                     + 0.32 * std::cos((3.0 * hb + 6.0) * kPi / 180.0)
                     - 0.20 * std::cos((4.0 * hb - 63.0) * kPi / 180.0);
    const double dTheta = 30.0 * std::exp(-((hb - 275.0) / 25.0) * ((hb - 275.0) / 25.0));
    const double Cbp7 = std::pow(Cbp, 7.0);
    const double Rc = 2.0 * std::sqrt(Cbp7 / (Cbp7 + std::pow(25.0, 7.0)));
    const double Sl = 1.0 + 0.015 * (Lb - 50.0) * (Lb - 50.0) / std::sqrt(20.0 + (Lb - 50.0) * (Lb - 50.0));
    const double Sc = 1.0 + 0.045 * Cbp;
    const double Sh = 1.0 + 0.015 * Cbp * T;
    const double Rt = -std::sin(2.0 * dTheta * kPi / 180.0) * Rc;
    const double tl = dL / Sl;
    const double tc = dC / Sc;
    const double th = dH / Sh;
    return std::sqrt(tl * tl + tc * tc + th * th + Rt * tc * th);
}

Rgb planckianWhite(double kelvin)
{
    const double T = std::min(25000.0, std::max(1667.0, kelvin));
    double x;
    if (T <= 4000.0)
        x = -0.2661239e9 / (T * T * T) - 0.2343589e6 / (T * T) + 0.8776956e3 / T + 0.179910;
    else
        x = -3.0258469e9 / (T * T * T) + 2.1070379e6 / (T * T) + 0.2226347e3 / T + 0.240390;
    double y;
    if (T <= 2222.0)
        y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
    else if (T <= 4000.0)
        y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
    else
        y = 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483;
    const std::array<double, 3> xyz = {x / y, 1.0, (1.0 - x - y) / y};
    Rgb c = xyzToLinear(xyz);
    c.r = std::max(1e-6, c.r);
    c.b = std::max(1e-6, c.b);
    const double g = std::max(1e-6, c.g);
    return {c.r / g, 1.0, c.b / g};
}

double estimateKelvin(const Rgb &w)
{
    if (w.r <= 0.0 || w.b <= 0.0)
        return 5600.0;
    const double target = std::log(w.b / w.r);
    double best = 5600.0;
    double bestErr = 1e9;
    for (double k = 2000.0; k <= 12000.0; k += 10.0) {
        const Rgb p = planckianWhite(k);
        const double err = std::fabs(std::log(p.b / p.r) - target);
        if (err < bestErr) {
            bestErr = err;
            best = k;
        }
    }
    return best;
}

} // namespace ColorScience
