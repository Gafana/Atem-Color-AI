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
    Auto,         ///< white balance K/tint for the bulk cast, RGB CC gain for the residual
};

enum class ExposureVia
{
    CcGain,     ///< colour correction gain (all channels)
    SensorGain, ///< video gain in dB (1.13)
    Iris,       ///< lens aperture (0.2), needs a lens with electronic iris
    Auto,       ///< light first: iris (sensible range), then sensor gain dB (kept low), Gain Y only to trim
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
    /** Live-safe per-pass limits (closed loop converges in several passes instead of one wild jump):
     *  colour balance −15/+18 % per channel, level ≈ ±0.9 stop, lift ±0.05, gamma ±0.15, saturation ±15 %,
     *  camera white balance ±1000 K / tint ±10, exposure ±1 stop. When off (test mode «Без ограничений»)
     *  only the camera's own parameter ranges apply. */
    bool liveLimits = false;
    /** Auto level may use the lens iris (false when the lens is not controllable / did not follow). */
    bool irisUsable = true;
    ColorScience::Transfer transfer = ColorScience::Transfer::Bt709Oetf;
    /** Unwinding: lift and gamma can trade off over the measured range (live: a blue lift hid behind a
     *  blue gamma -0.3 and stayed). When set, the per-channel fit is pulled toward the change that would
     *  bring the camera's absolute R/G/B lift / gamma to these targets (the reference camera's values),
     *  so the ambiguity resolves toward the reference instead of an equal-looking compensating pair. */
    bool unwind = false;
    double unwindLift[3] = {0, 0, 0};  ///< wanted delta lift per channel (reference - current)
    double unwindGamma[3] = {0, 0, 0}; ///< wanted delta gamma per channel
    /** Same for white balance vs CC gain (BalanceVia::Auto): both tint the picture, a skewed CC gain was
     *  left in place and the white balance drifted to compensate (live: gain B 0.61 + 3450 K). The CC
     *  part of the change first moves the camera's R/G and B/G CC gain ratios by these factors (toward
     *  the reference's, ±25 % per pass), the white balance takes the rest. */
    bool unwindGain = false;
    double unwindGainRB[2] = {1.0, 1.0};
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

/** Auto exposure split: how a level change (in stops of light, + = brighter) is spread over the
 *  lens iris and the sensor gain. Order when brightening: negative dB back to 0, iris open to f/2,
 *  gain up to +12 dB, iris to f/1.4, gain up to +24 dB (+36 with @p extended). When darkening:
 *  gain down to 0 dB (less noise), iris closed to f/11 (f/16 with @p extended). What is left
 *  (and small changes below @p trimStops) stays for CC Gain Y. */
struct LevelPlan
{
    double irisAv = -1;     ///< new aperture value (Av = log2 N²), -1 = unknown / unchanged
    int gainDb = 0;
    double appliedStops = 0;///< light change achieved by iris + gain
    double residualStops = 0;
};
/** Micro Studio Camera 4K G2 applies sensor gain in 6 dB steps (live test: 18 = 21 dB, 24 = 25 = 27 dB). */
LevelPlan planAutoLevel(double stops, double currentAv, int currentDb, bool irisUsable, bool extended,
                        double trimStops = 0.2, int dbStep = 6);

/** Did the lens follow an iris change? @p meanBefore / @p meanAfter are encoded mean lumas,
 *  @p expectedIrisStops the expected brightening from the iris, @p otherStops the known change from
 *  other controls (gain dB, Gain Y) between the two captures. */
bool irisResponded(double meanBefore, double meanAfter, double expectedIrisStops, double otherStops,
                   ColorScience::Transfer transfer = ColorScience::Transfer::Bt709Oetf);

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

/** Signal sanity of a captured frame. "No signal" means an essentially constant frame (black input,
 *  empty ATEM source, frozen colour) - a dark but real scene still has detail and sensor noise. */
struct SignalCheck
{
    bool noSignal = false;
    bool lowLight = false;   ///< real picture, but dark: matching is less reliable
    double meanLuma = 0;     ///< encoded 0..1
    double p95Luma = 0;
    double p99Luma = 0;
    double stdLuma = 0;      ///< encoded 0..1
    double detail = 0;       ///< mean absolute neighbour difference, encoded 0..1
    QString reason;          ///< human readable reason when noSignal / lowLight
};
SignalCheck checkSignal(const QImage &image);

/** Robust, scene-independent statistics of one camera picture. */
struct SceneStats
{
    bool valid = false;
    Rgb illuminant{1.0, 1.0, 1.0};    ///< estimated white (linear, luma 1), confidence-limited (absolute use)
    Rgb illuminantRaw{1.0, 1.0, 1.0}; ///< same estimate without the confidence pull (camera-to-camera use)
    double neutralFraction = 0;
    double keyLinear = 0;          ///< log-average luma (linear)
    double p25Linear = 0, p50Linear = 0, p75Linear = 0, p90Linear = 0, p98Linear = 0;
    double p02Linear = 0, p05Linear = 0, p10Linear = 0, p95Linear = 0; ///< shadows / contrast
    double medianChroma = 0;       ///< median chroma (encoded) of mid-tone pixels after neutralising the light
    /** Colour of near-neutral surfaces per tonal zone (0 shadows p2..p30, 1 mids p30..p70,
     *  2 highlights p70..p98), linear, luma 1 - a different tint in the shadows than in the highlights
     *  shows up here. Falls back to illuminantRaw when a zone has too few neutral pixels. */
    Rgb zoneIll[3] = {{1.0, 1.0, 1.0}, {1.0, 1.0, 1.0}, {1.0, 1.0, 1.0}};
    int zoneCount[3] = {0, 0, 0};
    /** Saturation: CIELAB chroma C* of mid-tone pixels (L* 25..85) after neutralising the light and
     *  normalising the exposure (median -> L* 50) - comparable between cameras and exposures. */
    double chromaP50 = 0, chromaP75 = 0, chromaP90 = 0;
    int chromaCount = 0;
    /** Per-channel histogram percentiles (encoded) at kChannelQuantiles - the channel transfer curve of
     *  the camera is fitted by matching them to the reference (robust to small framing differences). */
    static constexpr int kChannelQ = 12;
    double chQ[3][kChannelQ] = {};
};

/** How different two pictures look: tonal-zone colour, shadows, contrast and saturation. */
struct SceneDiff
{
    bool valid = false;
    double zoneDe[5] = {0, 0, 0, 0, 0}; ///< ΔE2000 of the zone colours at p5/p25/p50/p75/p95
    double zoneDeMean = 0;
    double shadowIre[2] = {0, 0};       ///< p5 luma, camera / reference (IRE)
    double contrastIre[2] = {0, 0};     ///< p95 - p5 luma, camera / reference (IRE)
    double chroma75[2] = {0, 0};        ///< C* p75, camera / reference
    double sceneDe = 0;                 ///< sqrt(zoneDeMean² + (ΔC*75 / 2)²) - single number for the loop
};
SceneDiff compareScenes(const SceneStats &cam, const SceneStats &ref, ColorScience::Transfer transfer);
SceneStats sceneStats(const QImage &image, ColorScience::Transfer transfer, QStringList *notes = nullptr);

struct ReferenceMatch
{
    QVector<Pair> pairs;
    double saturation = 1.0; ///< relative saturation change (only meaningful when settings.saturation)
};

/** Reference camera / reference frame: cameras usually see different parts of the scene, so whole-frame
 *  histograms are not comparable. Instead the colour of the light (white balance + tint), then the
 *  exposure (median / log-average level) and only then - if enabled - tone (lift/gamma from luma
 *  percentiles) and saturation (median chroma) are matched, each conservatively. */
/** @p toneCamera / @p toneReference: optional whole-frame statistics for tone (lift/gamma), per-zone tint
 *  and saturation when @p camera / @p reference describe a small marked zone (e.g. a white sheet: one
 *  level, no shadows) - the zone then sets only white balance and level. */
ReferenceMatch referenceMatch(const SceneStats &camera, const SceneStats &reference, const Settings &settings,
                              QStringList *notes = nullptr, const SceneStats *toneCamera = nullptr,
                              const SceneStats *toneReference = nullptr);

/** Automatic mode: pairs that neutralise the estimated illuminant and bring the scene key to
 *  @p targetGreyIre (mid-grey level, e.g. 41 IRE for 18 % grey through BT.709), protecting
 *  highlights. */
QVector<Pair> autoPairs(const QImage &image, const Settings &settings, double targetGreyIre,
                        AutoEstimate *estimate = nullptr, QStringList *notes = nullptr);

} // namespace MatchEngine
