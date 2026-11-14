#pragma once

#include "ColorAnalysis.h"
#include "atem/CameraCorrection.h"

#include <QPair>
#include <QVector>

struct ColorMatchOptions
{
    float damping = 0.35f;
    bool useVideoGain = true;
};

struct CameraMatchResult
{
    QString label;
    FrameColorAnalysis analysis;
    FrameComparisonMetrics metrics;
    CameraCorrectionBundle suggested;
    CameraCorrectionBundle damped;
};

class ColorMatching
{
public:
    static CameraMatchResult matchToReference(const FrameColorAnalysis &reference,
                                              const FrameColorAnalysis &candidate,
                                              const QString &label = {},
                                              const ColorMatchOptions &options = {});

    static QVector<CameraMatchResult> matchAll(const FrameColorAnalysis &reference,
                                               const QVector<QPair<QString, FrameColorAnalysis>> &cams,
                                               const ColorMatchOptions &options = {});
};