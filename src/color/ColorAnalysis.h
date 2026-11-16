#pragma once

#include "ColorTypes.h"
#include "atem/CameraCorrection.h"

class ColorAnalysis
{
public:
    static QString loadImage(const QString &path, QImage &out);
    static FrameColorAnalysis analyze(const QImage &image);
    static FrameColorAnalysis analyze(const QImage &image, const QRect &region);
    static FrameColorAnalysis mergeAccumulate(const FrameColorAnalysis &accumulator,
                                              const FrameColorAnalysis &sample);
    static void finalizeMerged(FrameColorAnalysis &accumulator);

    static CameraCorrectionBundle neutralBalanceCorrection(const QImage &image,
                                                           const NeutralBalanceOptions &opts);

    static FrameComparisonMetrics compare(const FrameColorAnalysis &reference,
                                          const FrameColorAnalysis &candidate);
    static double histogramL1(const std::array<quint32, kHistogramBins> &a,
                              const std::array<quint32, kHistogramBins> &b);
};