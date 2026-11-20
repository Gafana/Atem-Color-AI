#include "ScopeMath.h"

#include <QtMath>

#include <cmath>

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

QRect roiPixelRect(const QSize &size, const QRectF &n)
{
    if (n.isEmpty() || size.isEmpty())
        return {};
    const QRectF c = n.intersected(QRectF(0, 0, 1, 1));
    if (c.isEmpty())
        return {};
    const int x0 = qBound(0, static_cast<int>(std::floor(c.left() * size.width() + 1e-6)), size.width() - 1);
    const int y0 = qBound(0, static_cast<int>(std::floor(c.top() * size.height() + 1e-6)), size.height() - 1);
    const int x1 = qBound(x0 + 1, static_cast<int>(std::ceil(c.right() * size.width() - 1e-6)), size.width());
    const int y1 = qBound(y0 + 1, static_cast<int>(std::ceil(c.bottom() * size.height() - 1e-6)), size.height());
    if (x1 - x0 < 2 || y1 - y0 < 2)
        return {};
    return QRect(x0, y0, x1 - x0, y1 - y0);
}

QImage cropToRoi(const QImage &image, const QRectF &n)
{
    if (image.isNull())
        return image;
    const QRect r = roiPixelRect(image.size(), n);
    return r.isEmpty() ? image : image.copy(r);
}

void dimOutsideRoi(QImage &image, const QRectF &n, double keep)
{
    const QRect r = roiPixelRect(image.size(), n);
    if (r.isEmpty())
        return;
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_RGB32);
    const int k = qBound(0, static_cast<int>(keep * 256), 256);
    for (int y = 0; y < image.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        const bool rowIn = y >= r.top() && y <= r.bottom();
        for (int x = 0; x < image.width(); ++x) {
            if (rowIn && x >= r.left() && x <= r.right())
                continue;
            const QRgb p = row[x];
            row[x] = qRgba(qRed(p) * k >> 8, qGreen(p) * k >> 8, qBlue(p) * k >> 8, qAlpha(p));
        }
    }
}

} // namespace ScopeMath
