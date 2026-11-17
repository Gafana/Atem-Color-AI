#pragma once

#include <QImage>
#include <QtGlobal>

namespace ScopeMath {

inline int clamp8(int v)
{
    return qBound(0, v, 255);
}

inline int lumaBt709(int r, int g, int b)
{
    return clamp8(static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5));
}

inline void rgbToYCbCr709(int r, int g, int b, int *y, int *cb, int *cr)
{
    const double Y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double Cb = -0.1146 * r - 0.3854 * g + 0.5 * b + 128.0;
    const double Cr = 0.5 * r - 0.4542 * g - 0.0458 * b + 128.0;
    *y = clamp8(static_cast<int>(Y + 0.5));
    *cb = clamp8(static_cast<int>(Cb + 0.5));
    *cr = clamp8(static_cast<int>(Cr + 0.5));
}

double lumaToIre(int y, bool legalRange);
int ireToPlotRow(double ire, int plotHeight, bool ire100);

void normalizeFrameRgb32(QImage &image);
QImage scaledRgb32(const QImage &image, int maxWidth);

} // namespace ScopeMath