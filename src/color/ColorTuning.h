#pragma once

#include "ColorCheckerData.h"
#include "ColorMatching.h"
#include "ColorMatchMode.h"
#include "CorrectionParameterMask.h"

#include <QRectF>
#include <QVector>

class ColorTuning
{
public:
    static FrameColorAnalysis averageAnalyses(const QVector<FrameColorAnalysis> &samples);

    static CameraCorrectionBundle correctionFromGrayCard(const FrameColorAnalysis &roiAnalysis,
                                                         const GrayCardTuningOptions &opts,
                                                         const ColorMatchOptions &matchOpts = {});

    static QVector<CameraMatchResult> matchToReferenceFrame(
        const FrameColorAnalysis &reference,
        const QVector<QPair<QString, FrameColorAnalysis>> &cams,
        const ColorMatchOptions &options = {});

    static QVector<CameraMatchResult> matchCamerasToAverage(
        const QVector<QPair<QString, FrameColorAnalysis>> &included,
        const ColorMatchOptions &options = {});

    static CameraMatchResult matchGrayCard(const QString &label, const FrameColorAnalysis &roi,
                                           const GrayCardTuningOptions &opts);

    static CameraMatchResult matchColorChecker(const QString &label, const QVector<ColorCheckerPatch> &patches,
                                               const ColorMatchOptions &options = {});

    static CameraMatchResult matchNeutral(const QString &label, const QImage &frameImage,
                                          const NeutralBalanceOptions &opts = {});

    /** Build target stats for gray-card white reference (R=G=B at neutral). */
    static FrameColorAnalysis syntheticNeutralGrayReference(double luma);
};

FrameColorAnalysis analyzeNormalizedRoi(const QImage &frame, const QRectF &normalizedRoi);