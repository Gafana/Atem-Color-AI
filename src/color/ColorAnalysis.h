#pragma once

#include "ColorTypes.h"

class ColorAnalysis
{
public:
    static QString loadImage(const QString &path, QImage &out);
    static FrameColorAnalysis analyze(const QImage &image);
    static FrameComparisonMetrics compare(const FrameColorAnalysis &reference,
                                          const FrameColorAnalysis &candidate);
    static double histogramL1(const std::array<quint32, kHistogramBins> &a,
                              const std::array<quint32, kHistogramBins> &b);
};