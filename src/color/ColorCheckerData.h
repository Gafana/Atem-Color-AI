#pragma once

#include <QImage>
#include <QColor>
#include <QPointF>
#include <QVector>

/** Classic ColorChecker 24 reference sRGB (8-bit), row-major 6×4, Rec.709 display values. */
struct ColorCheckerPatch
{
    int index = 0;
    QString name;
    QColor reference;
    QColor measured;
    double deltaE = 0.0;
};

QVector<QColor> colorCheckerReferenceSrgb();

/** Sample 24 patches from @p image inside quad @p corners (image coords, clockwise TL,TR,BR,BL). */
QVector<ColorCheckerPatch> sampleColorChecker24(const QImage &image, const QVector<QPointF> &corners);

double deltaE76(const QColor &a, const QColor &b);