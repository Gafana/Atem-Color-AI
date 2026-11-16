#pragma once

#include "ColorScience.h"

#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>

/** Reference chart presets with published CIELAB (D50) patch values. */
struct ChartPatchRef
{
    const char *name;
    double L;
    double a;
    double b;
};

struct ChartPreset
{
    QString id;
    QString name;
    int cols = 6; ///< patches per row in the chart's native orientation
    int rows = 4;
    QVector<ChartPatchRef> patches; ///< row-major in native orientation
    QVector<int> neutral;           ///< indexes of neutral (gray) patches, light -> dark
    QString source;
};

struct ChartPatchSample
{
    int index = 0;
    QString name;
    ColorScience::Rgb measured;  ///< encoded 0..1
    ColorScience::Rgb reference; ///< encoded 0..1 for the selected transfer
    double deltaE = 0.0;         ///< CIEDE2000, filled by the matcher
    bool neutral = false;
};

namespace ColorCharts {

QVector<ChartPreset> presets();
ChartPreset byId(const QString &id);

/** Encoded reference RGB (0..1) of each patch, scaled so the chart's lightest neutral sits at
 *  @p whiteLinear (scene-linear) before encoding with @p transfer. Pass whiteLinear <= 0 to keep
 *  the absolute reflectance (white patch ~0.9). */
QVector<ColorScience::Rgb> referenceEncoded(const ChartPreset &chart, ColorScience::Transfer transfer);

/**
 * Sample all patches inside the quad @p corners (image pixels, TL, TR, BR, BL as the user marked).
 * Orientation is detected automatically (0/90/180/270 deg) by choosing the arrangement whose
 * neutral patches are the most neutral and ordered light->dark; @p rotationOut receives it.
 */
QVector<ChartPatchSample> sample(const QImage &image, const QVector<QPointF> &corners,
                                 const ChartPreset &chart, ColorScience::Transfer transfer,
                                 int *rotationOut = nullptr);

/** Trimmed mean (10% each side) of encoded RGB inside a rectangle. */
ColorScience::Rgb trimmedMean(const QImage &image, const QRect &rect);

} // namespace ColorCharts
