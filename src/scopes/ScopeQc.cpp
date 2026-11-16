#include "ScopeQc.h"

#include "ScopeMath.h"

namespace ScopeQc {

ScopeQcResult analyzeFrame(const QImage &rgb32, float ireLow, float ireHigh)
{
    ScopeQcResult r;
    if (rgb32.isNull())
        return r;
    QImage src = rgb32;
    if (src.format() != QImage::Format_RGB32)
        src = src.convertToFormat(QImage::Format_RGB32);
    const int step = qMax(1, src.width() / 640);
    r.minIre = 200.0;
    r.maxIre = -200.0;
    for (int y = 0; y < src.height(); y += step) {
        const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < src.width(); x += step) {
            const int lu = ScopeMath::lumaBt709(qRed(row[x]), qGreen(row[x]), qBlue(row[x]));
            const double ire = ScopeMath::lumaToIre(lu, true);
            r.minIre = qMin(r.minIre, ire);
            r.maxIre = qMax(r.maxIre, ire);
            ++r.totalSamples;
            if (ire < ireLow)
                ++r.pixelsBelowTolerance;
            else if (ire > ireHigh)
                ++r.pixelsAboveTolerance;
            if (lu >= 252)
                ++r.highlightClipped;
        }
    }
    if (r.totalSamples == 0) {
        r.minIre = 0.0;
        r.maxIre = 0.0;
    }
    return r;
}

} // namespace ScopeQc