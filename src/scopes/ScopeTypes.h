#pragma once

#include <QImage>
#include <QMetaType>
#include <QString>
#include <QVector>

enum class ScopeKind
{
    None = 0,
    WaveformLuma,
    RgbParade,
    YcbcrParade,
    RgbOverlayWaveform,
    Vectorscope,
    HistogramLuma,
    HistogramRgb,
    HistogramRgbOverlay,
    FalseColor,
    Zebra,
    Cie1931,
    CompareWipe,
    CompareSideBySide,
};

enum class ScopeLayoutId
{
    Single,
    DualHorizontal,
    Grid2x2,
    OnePlusThree,
};

struct ScopeDisplaySettings
{
    float traceBrightness = 0.85f;
    bool ireScale100 = true;
    bool showIre1023 = false;
    int vectorscopeZoom = 1;
    float zebraThresholdIre = 95.0f;
    float falseColorLowIre = 0.0f;
    float falseColorMidIre = 45.0f;
    float falseColorHighIre = 100.0f;
    int refreshFps = 25;
    bool waveformMonochrome = false;
    float ireToleranceLow = 0.0f;
    float ireToleranceHigh = 100.0f;
    bool vectorscopeLumaZones = false;
    float zoneLowMaxPct = 33.0f;
    float zoneMidMaxPct = 66.0f;
    bool snapshotOverlay = false;
    float snapshotOverlayAlpha = 0.45f;
};

struct ScopeQcResult
{
  double minIre = 0.0;
  double maxIre = 0.0;
  int pixelsBelowTolerance = 0;
  int pixelsAboveTolerance = 0;
  int highlightClipped = 0;
  int totalSamples = 0;

  double outOfTolerancePercent() const
  {
      if (totalSamples <= 0)
          return 0.0;
      const int bad = pixelsBelowTolerance + pixelsAboveTolerance;
      return 100.0 * static_cast<double>(bad) / static_cast<double>(totalSamples);
  }
  bool hasIssues(float ireLow, float ireHigh) const
  {
      return minIre < ireLow - 0.1 || maxIre > ireHigh + 0.1 || outOfTolerancePercent() > 0.05;
  }
};

struct ScopeCellState
{
    ScopeKind kind = ScopeKind::WaveformLuma;
    ScopeDisplaySettings settings;
};

struct ScopeLayoutState
{
    ScopeLayoutId layout = ScopeLayoutId::DualHorizontal;
    QVector<ScopeCellState> cells;
};

QString scopeKindId(ScopeKind kind);
ScopeKind scopeKindFromId(const QString &id);
QString scopeKindLabel(ScopeKind kind);

QVector<ScopeKind> allScopeKinds();

QString scopeLayoutId(ScopeLayoutId layout);
ScopeLayoutId scopeLayoutFromId(const QString &id);
QString scopeLayoutLabel(ScopeLayoutId layout);

int scopeLayoutCellCount(ScopeLayoutId layout);

struct ScopeFrameInput
{
    QImage frameA;
    QImage frameB;
    int plotWidth = 640;
    int plotHeight = 360;
    ScopeDisplaySettings settings;
};

struct ScopeFrameOutput
{
    ScopeKind kind = ScopeKind::None;
    QImage image;
    QString subtitle;
};

Q_DECLARE_METATYPE(ScopeKind)