#include "ColorCheckerData.h"

#include <QtMath>

#include <cmath>

namespace {

QColor patch(int r, int g, int b)
{
    return QColor(r, g, b);
}

QPointF bilinear(const QPointF &tl, const QPointF &tr, const QPointF &br, const QPointF &bl, double u,
                 double v)
{
    const QPointF top = tl * (1.0 - u) + tr * u;
    const QPointF bottom = bl * (1.0 - u) + br * u;
    return top * (1.0 - v) + bottom * v;
}

QColor averagePatch(const QImage &image, const QPointF &center, int halfSize)
{
    const int cx = static_cast<int>(std::lround(center.x()));
    const int cy = static_cast<int>(std::lround(center.y()));
    qint64 sr = 0;
    qint64 sg = 0;
    qint64 sb = 0;
    quint64 n = 0;
    for (int y = cy - halfSize; y <= cy + halfSize; ++y) {
        if (y < 0 || y >= image.height())
            continue;
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = cx - halfSize; x <= cx + halfSize; ++x) {
            if (x < 0 || x >= image.width())
                continue;
            const QRgb px = row[x];
            sr += qRed(px);
            sg += qGreen(px);
            sb += qBlue(px);
            ++n;
        }
    }
    if (n == 0)
        return {};
    return QColor(static_cast<int>(sr / static_cast<qint64>(n)), static_cast<int>(sg / static_cast<qint64>(n)),
                  static_cast<int>(sb / static_cast<qint64>(n)));
}

} // namespace

QVector<QColor> colorCheckerReferenceSrgb()
{
    return {
        patch(115, 82, 68),   patch(194, 150, 130), patch(98, 122, 157),  patch(87, 108, 67),
        patch(133, 128, 177), patch(103, 189, 170), patch(214, 126, 44),  patch(80, 91, 166),
        patch(193, 90, 99),   patch(94, 60, 108),   patch(157, 188, 64),  patch(224, 163, 46),
        patch(56, 61, 150),   patch(70, 148, 73),   patch(175, 54, 60),   patch(231, 199, 31),
        patch(187, 86, 149),  patch(8, 133, 161),   patch(243, 243, 242), patch(200, 200, 200),
        patch(160, 160, 160), patch(122, 122, 121), patch(85, 85, 85),   patch(52, 52, 52),
    };
}

double deltaE76(const QColor &a, const QColor &b)
{
    const double dr = a.red() - b.red();
    const double dg = a.green() - b.green();
    const double db = a.blue() - b.blue();
    return std::sqrt(dr * dr + dg * dg + db * db);
}

QVector<ColorCheckerPatch> sampleColorChecker24(const QImage &image, const QVector<QPointF> &corners)
{
    QVector<ColorCheckerPatch> out;
    if (image.isNull() || corners.size() != 4)
        return out;

    const QVector<QColor> refs = colorCheckerReferenceSrgb();
    const QPointF tl = corners.at(0);
    const QPointF tr = corners.at(1);
    const QPointF br = corners.at(2);
    const QPointF bl = corners.at(3);

    const int half = qMax(2, qMin(image.width(), image.height()) / 80);
    out.reserve(24);
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 6; ++col) {
            const double u = (static_cast<double>(col) + 0.5) / 6.0;
            const double v = (static_cast<double>(row) + 0.5) / 4.0;
            const QPointF center = bilinear(tl, tr, br, bl, u, v);
            ColorCheckerPatch patchInfo;
            patchInfo.index = row * 6 + col;
            patchInfo.name = QStringLiteral("P%1").arg(patchInfo.index + 1);
            patchInfo.reference = refs.at(patchInfo.index);
            patchInfo.measured = averagePatch(image, center, half);
            patchInfo.deltaE = deltaE76(patchInfo.reference, patchInfo.measured);
            out.push_back(patchInfo);
        }
    }
    return out;
}