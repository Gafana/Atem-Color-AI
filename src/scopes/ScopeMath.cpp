#include "ScopeMath.h"

#include <QtMath>

namespace ScopeMath {

double lumaToIre(int y, bool legalRange)
{
    // Frames reach the scopes as full-range RGB produced by GStreamer videoconvert from
    // limited-range Y'CbCr: code 0 = 0 IRE (video black), 255 = 100 IRE (video white).
    // The former "legal range" remapping (16..235) stretched levels and clipped 0..7 IRE.
    Q_UNUSED(legalRange);
    return static_cast<double>(y) / 255.0 * 100.0;
}

int ireToPlotRow(double ire, int plotHeight, bool ire100)
{
    const double maxIre = ire100 ? 100.0 : 100.0;
    const double t = qBound(0.0, ire / maxIre, 1.0);
    return static_cast<int>((1.0 - t) * static_cast<double>(plotHeight - 1));
}

void normalizeFrameRgb32(QImage &image)
{
    if (image.isNull())
        return;
    if (image.format() != QImage::Format_RGB32)
        image = image.convertToFormat(QImage::Format_RGB32);
}

QImage scaledRgb32(const QImage &image, int maxWidth)
{
    if (image.isNull() || maxWidth <= 0)
        return {};
    QImage rgb = image.format() == QImage::Format_RGB32 ? image : image.convertToFormat(QImage::Format_RGB32);
    if (rgb.width() <= maxWidth)
        return rgb;
    return rgb.scaledToWidth(maxWidth, Qt::FastTransformation);
}

} // namespace ScopeMath