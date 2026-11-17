#pragma once

#include <array>

/**
 * Small, dependency-free colour science helpers used by the matching engine.
 *
 * Conventions (see docs/color-matching.md):
 *  - "encoded" values are normalised video code values 0..1 of an RGB frame produced by
 *    GStreamer videoconvert from limited-range Y'CbCr, i.e. 0.0 = 0 IRE (black, code 16),
 *    1.0 = 100 IRE (white, code 235). Super-white is clipped at 1.0 by the conversion.
 *  - "linear" values are scene-linear Rec.709/sRGB primaries, D65 white.
 */
namespace ColorScience {

struct Rgb
{
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;

    double operator[](int i) const { return i == 0 ? r : (i == 1 ? g : b); }
    double &operator[](int i) { return i == 0 ? r : (i == 1 ? g : b); }
};

struct Lab
{
    double L = 0.0;
    double a = 0.0;
    double b = 0.0;
};

enum class Transfer
{
    Bt709Oetf, ///< ITU-R BT.709 camera OETF (what a Blackmagic camera in "Video" mode approximates)
    Gamma24,   ///< BT.1886 display gamma 2.4 (DaVinci Resolve "Rec.709 Gamma 2.4" target)
    Srgb,
};

constexpr double kLumaR = 0.2126;
constexpr double kLumaG = 0.7152;
constexpr double kLumaB = 0.0722;

inline double luma(const Rgb &c) { return kLumaR * c.r + kLumaG * c.g + kLumaB * c.b; }

double encode(double linear, Transfer t);
double decode(double encoded, Transfer t);
Rgb encode(const Rgb &linear, Transfer t);
Rgb decode(const Rgb &encoded, Transfer t);

/** Linear Rec.709 (D65) -> CIE XYZ (D65, Y=1 for white). */
std::array<double, 3> linearToXyz(const Rgb &lin);
Rgb xyzToLinear(const std::array<double, 3> &xyz);

/** CIELAB relative to D65 white. */
Lab xyzToLabD65(const std::array<double, 3> &xyz);
/** Reference data is usually CIELAB D50: convert with Bradford adaptation to linear Rec.709 D65. */
Rgb labD50ToLinear709(const Lab &lab);
Lab linear709ToLabD65(const Rgb &lin);

double deltaE2000(const Lab &a, const Lab &b);
double deltaE76(const Lab &a, const Lab &b);

/**
 * Linear Rec.709 RGB of a Planckian (blackbody) radiator at @p kelvin, normalised to G = 1.
 * Kim et al. (2002) cubic CCT->xy approximation, valid 1667..25000 K.
 */
Rgb planckianWhite(double kelvin);

/** Correlated colour temperature estimate for a linear RGB white, searched along planckianWhite(). */
double estimateKelvin(const Rgb &linearWhite);

} // namespace ColorScience
