#pragma once

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <limits>

/**
 * «ИИ-подсказки»: rule-based analysis of one frame (or of a measurement zone in it) with plain-language
 * recommendations for the camera operator, plus a cross-camera comparison against the reference.
 *
 * Frames are 8-bit RGB where 0 = 0 IRE and 255 = 100 IRE (see ColorScience.h conventions).
 * Problem regions are returned in normalized coordinates of the WHOLE frame so the preview can mark them.
 */
namespace FrameAdvisor {

enum class Severity
{
    Ok,
    Info,
    Warning,
    Critical,
};

struct Issue
{
    Severity severity = Severity::Info;
    QString code;            ///< "clip", "crush", "exposure", "cast", "oversat", "skin", "noise", "gain", "match_*"
    QString text;            ///< what is wrong (Russian)
    QString advice;          ///< what to do (Russian)
    QVector<QRectF> regions; ///< normalized (0..1) in the whole frame
};

struct Stats
{
    double area = 0;            ///< analysed pixels (after downscale)
    double clipPct[3] = {0, 0, 0}; ///< % of area with R / G / B at 100 IRE
    double clipAnyPct = 0;
    double crushPct = 0;        ///< % of area with all channels <= ~1 IRE
    double medianIre = 0;
    double meanIre = 0;
    double p98Ire = 0;
    double neutralPct = 0;      ///< % of area that is near-neutral midtones (used for the cast)
    double castA = 0;           ///< Lab a*/b* mean of the near-neutral areas
    double castB = 0;
    double meanL = 0, meanA = 0, meanB = 0; ///< mean Lab of the analysed area (cross-camera ΔE)
    double oversatPct = 0;      ///< % with C* > kOversatChroma or a clipped + empty channel pair
    double skinPct = 0;
    double skinHueDeg = std::numeric_limits<double>::quiet_NaN(); ///< vectorscope angle of skin pixels
    double noiseIre = std::numeric_limits<double>::quiet_NaN();   ///< luma std in flat areas
};

struct Options
{
    double targetIre = 45.0;                                      ///< typical mid-scene level
    double gainDb = std::numeric_limits<double>::quiet_NaN();     ///< known camera sensor gain
    QString label;                                                ///< camera name in the texts
};

struct Advice
{
    QString label;
    bool zone = false;   ///< analysed a measurement zone, not the whole frame
    Stats stats;
    QVector<Issue> issues;
    QString verdict() const; ///< one line: «норма» or the worst problems
};

constexpr double kSkinLineDeg = 123.0;   ///< I-line (skin tone line) angle on a Cb/Cr vectorscope
constexpr double kOversatChroma = 75.0;

/** Analyse @p frame (inside @p roi when set). */
Advice analyze(const QImage &frame, const QRectF &roi = {}, const Options &opt = {});

/** Cross-camera notes: what differs between @p cam and @p ref, and what to change on @p cam. */
QVector<Issue> compare(const Advice &cam, const Advice &ref, double *deltaE = nullptr);

QString severityLabel(Severity s);

} // namespace FrameAdvisor
