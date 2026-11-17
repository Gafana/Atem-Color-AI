#pragma once

#include <QImage>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <array>

constexpr int kHistogramBins = 256;
constexpr int kVectorscopeSize = 128;

struct ChannelStats
{
    double mean = 0.0;
    double median = 0.0;
    double p2 = 0.0;
    double p98 = 0.0;
};

struct FrameColorAnalysis
{
    std::array<quint32, kHistogramBins> histR{};
    std::array<quint32, kHistogramBins> histG{};
    std::array<quint32, kHistogramBins> histB{};
    std::array<quint32, kHistogramBins> histY{};
    std::array<quint32, kHistogramBins> histU{};
    std::array<quint32, kHistogramBins> histV{};

    ChannelStats statsR;
    ChannelStats statsG;
    ChannelStats statsB;
    ChannelStats statsY;
    ChannelStats statsU;
    ChannelStats statsV;

    /** UV plane accumulation (row-major), scaled for display in widgets. */
    std::array<quint32, kVectorscopeSize * kVectorscopeSize> vectorscopeUv{};

    quint64 sampleCount = 0;
    int width = 0;
    int height = 0;
};

Q_DECLARE_METATYPE(FrameColorAnalysis)

struct FrameComparisonMetrics
{
    double meanDeltaLuma = 0.0;
    double histogramL1Rgb = 0.0;
    double percentileSpreadDelta = 0.0;
    double uvEnergyRatio = 1.0;
};

struct GrayCardTuningOptions
{
    double targetLuma = 110.0;
    bool matchExposure = true;
};

struct NeutralBalanceOptions
{
    double highlightLumaMin = 200.0;
    double neutralChromaMax = 18.0;
    int kelvinBase = 5600;
};