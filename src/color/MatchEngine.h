#pragma once

#include "ColorScience.h"
#include "ColorTypes.h"
#include "atem/CameraCorrection.h"

#include <QImage>
#include <QRect>
#include <QStringList>
#include <QVector>

/**
 * Colour matching engine (see docs/color-matching.md).
 *
 * All modes reduce to a set of correspondences "measured encoded RGB -> target encoded RGB".
 * A per-channel primary-grade model (gain / lift / gamma, same structure as the camera's
 * Color Correction group 8) is fitted to those pairs by weighted least squares, then expressed
 * as a *relative* change and composed onto the camera's current settings read from the ATEM.
 */
namespace MatchEngine {

using ColorScience::Rgb;

struct Pair
{
    Rgb measured;
    Rgb target;
    double weight = 1.0;
    bool neutral = false;
};

enum class BalanceVia
{
    CcGain,       ///< colour correction RGB gain (8.2) - exact, predictable
    WhiteBalance, ///< camera white balance Kelvin / tint (1.2) - physically on the sensor
};

enum class ExposureVia
{
    CcGain,     ///< colour correction gain (all channels)
    SensorGain, ///< video gain in dB (1.13)
    Iris,       ///< lens aperture (0.2), needs a lens with electronic iris
};

struct Settings
{
    bool balance = true;    ///< correct colour balance
    bool exposure = true;   ///< correct overall level
    bool lift = false;      ///< fit lift (shadows) per channel
    bool gamma = false;     ///< fit gamma (midtones) per channel
    bool saturation = false;///< fit saturation (needs coloured patches)
    BalanceVia balanceVia = BalanceVia::CcGain;
    ExposureVia exposureVia = ExposureVia::CcGain;
    double strength = 1.0;  ///< 0..1 fraction of the computed change that is applied
    ColorScience::Transfer transfer = ColorScience::Transfer::Bt709Oetf;
};

/** Model of one channel of the camera primary grade on encoded signal x in [0,1]:
 *  y = (x*gain + lift*(1-x)) ^ (2^-gamma). Lift/gamma scale of the real camera is not documented
 *  by Blackmagic; this model is exact for gain and sign-correct for lift/gamma. */
struct ChannelCurve
{
    double gain = 1.0;
    double lift = 0.0;
    double gamma = 0.0;
    double apply(double x) const;
};

struct Delta
{
    ChannelCurve ch[3];
    double saturation = 1.0;
    double rmsBeforeIre = 0.0; ///< predicted residual on the pairs, IRE
    double rmsAfterIre = 0.0;
    double deBefore = -1.0;    ///< mean CIEDE2000 on the pairs (if meaningful)
    double deAfter = -1.0;
    int pairCount = 0;
    bool valid = false;
    QStringList notes;

    Rgb apply(const Rgb &x) const;
};

Delta solve(const QVector<Pair> &pairs, const Settings &settings);

/** New absolute camera values = current camera values composed with the delta. */
CameraCorrectionBundle compose(const CameraCorrectionBundle &current, const Delta &delta,
                               const Settings &settings, QStringList *notes = nullptr);

/** Human readable reasons why a proposed change looks suspicious (empty = looks sane). */
QStringList sanityWarnings(const CameraCorrectionBundle &current, const CameraCorrectionBundle &proposed);

// ---- measurement builders -------------------------------------------------

/** Quantile correspondences (p5..p95) per channel between two histogram analyses. */
QVector<Pair> quantilePairs(const FrameColorAnalysis &measured, const FrameColorAnalysis &target);

/** Quantile of a 256-bin histogram, linear interpolation inside the bin, returns 0..1.
 */
double histogramQuantile(const std::array<quint32, kHistogramBins> &hist, double p);

/** Trimmed mean RGB (encoded 0..1) of a region (whole image when @p rect is empty). */
Rgb regionMean(const QImage &image, const QRect &rect = QRect());

struct AutoEstimate
{
    Rgb illuminantLinear;  ///< estimated white, normalised to luma 1
    double keyLinear = 0;  ///< log-average scene luma (linear)
    double p98Linear = 0;  ///< 98th percentile luma (linear)
    double neutralFraction = 0;
    double estimatedKelvinShift = 0;
};

/** Automatic mode: pairs that neutralise the estimated illuminant and bring the scene key to
 *  @p targetGreyIre (mid-grey level, e.g. 41 IRE for 18 % grey through BT.709), protecting
 *  highlights. */
QVector<Pair> autoPairs(const QImage &image, const Settings &settings, double targetGreyIre,
                        AutoEstimate *estimate = nullptr, QStringList *notes = nullptr);

} // namespace MatchEngine
