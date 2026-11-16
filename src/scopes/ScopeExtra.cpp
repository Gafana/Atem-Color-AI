#include "ScopeRender.h"
#include "ScopeMath.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace ScopeRender {

static void xyToUv(double x, double y, double *u, double *v)
{
    const double denom = -2.0 * x + 12.0 * y + 3.0;
    if (std::abs(denom) < 1e-9) {
        *u = 0.0;
        *v = 0.0;
        return;
    }
    *u = 4.0 * x / denom;
    *v = 9.0 * y / denom;
}

// vectorscope / histograms live in ScopeWaveform.cpp

static QColor falseColorForIre(double ire, const ScopeDisplaySettings &settings)
{
    if (ire < settings.falseColorLowIre + 5)
        return QColor(20, 20, 120);
    if (ire < settings.falseColorMidIre - 5)
        return QColor(20, 160, 40);
    if (ire < settings.falseColorMidIre + 5)
        return QColor(220, 220, 60);
    if (ire < settings.falseColorHighIre - 5)
        return QColor(220, 120, 40);
    return QColor(220, 40, 40);
}

ScopeFrameOutput falseColor(const QImage &src, const ScopeFrameInput &input)
{
    ScopeFrameOutput out;
    out.kind = ScopeKind::FalseColor;
    out.image = src.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < out.image.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.image.scanLine(y));
        for (int x = 0; x < out.image.width(); ++x) {
            const int l = ScopeMath::lumaBt709(qRed(row[x]), qGreen(row[x]), qBlue(row[x]));
            const double ire = ScopeMath::lumaToIre(l, input.settings.ireScale100);
            const QColor c = falseColorForIre(ire, input.settings);
            row[x] = qRgb(c.red(), c.green(), c.blue());
        }
    }
    return out;
}

ScopeFrameOutput zebra(const QImage &src, const ScopeFrameInput &input)
{
    ScopeFrameOutput out;
    out.kind = ScopeKind::Zebra;
    out.image = src.convertToFormat(QImage::Format_RGB32);
    const double threshold = input.settings.zebraThresholdIre;
    for (int y = 0; y < out.image.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.image.scanLine(y));
        for (int x = 0; x < out.image.width(); ++x) {
            const int l = ScopeMath::lumaBt709(qRed(row[x]), qGreen(row[x]), qBlue(row[x]));
            const double ire = ScopeMath::lumaToIre(l, input.settings.ireScale100);
            if (ire >= threshold)
                row[x] = ((x + y) % 16 < 8) ? qRgb(240, 240, 80) : qRgb(40, 40, 40);
        }
    }
    return out;
}

static void drawTriangle(QImage &img, const QVector<QPointF> &pts, const QColor &color)
{
    for (int i = 0; i < pts.size(); ++i) {
        const QPointF a = pts[i];
        const QPointF b = pts[(i + 1) % pts.size()];
        const int steps = 80;
        for (int s = 0; s <= steps; ++s) {
            const double t = static_cast<double>(s) / steps;
            const QPointF p = a * (1.0 - t) + b * t;
            const int x = qBound(0, static_cast<int>(p.x()), img.width() - 1);
            const int y = qBound(0, static_cast<int>(p.y()), img.height() - 1);
            img.setPixel(x, y, color.rgba());
        }
    }
}

ScopeFrameOutput cie1931(const QImage &src, const ScopeFrameInput &input)
{
    ScopeFrameOutput out;
    out.kind = ScopeKind::Cie1931;
    const int w = std::max(32, input.plotWidth);
    const int h = std::max(32, input.plotHeight);
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(QColor(12, 12, 14));
    // Equal scale on x and y (the diagram must not be stretched): x 0..0.8, y 0..0.9.
    const double scale = std::min((w - 30) / 0.8, (h - 24) / 0.9);
    const double ox = (w - 0.8 * scale) / 2.0 + 6;
    const double oy = (h + 0.9 * scale) / 2.0;
    auto map = [&](double x, double y) { return QPointF(ox + x * scale, oy - y * scale); };

    // CIE 1931 2-degree spectral locus (nm, x, y).
    static const double locus[][3] = {
        {380, 0.1741, 0.0050}, {460, 0.1440, 0.0297}, {470, 0.1241, 0.0578}, {475, 0.1096, 0.0868},
        {480, 0.0913, 0.1327}, {485, 0.0687, 0.2007}, {490, 0.0454, 0.2950}, {495, 0.0235, 0.4127},
        {500, 0.0082, 0.5384}, {505, 0.0039, 0.6548}, {510, 0.0139, 0.7502}, {515, 0.0389, 0.8120},
        {520, 0.0743, 0.8338}, {525, 0.1142, 0.8262}, {530, 0.1547, 0.8059}, {540, 0.2296, 0.7543},
        {550, 0.3016, 0.6923}, {560, 0.3731, 0.6245}, {570, 0.4441, 0.5547}, {580, 0.5125, 0.4866},
        {590, 0.5752, 0.4242}, {600, 0.6270, 0.3725}, {610, 0.6658, 0.3340}, {620, 0.6915, 0.3083},
        {630, 0.7079, 0.2920}, {650, 0.7260, 0.2740}, {700, 0.7347, 0.2653}};
    QPainterPath locusPath;
    for (size_t i = 0; i < sizeof(locus) / sizeof(locus[0]); ++i) {
        const QPointF p = map(locus[i][1], locus[i][2]);
        if (i == 0)
            locusPath.moveTo(p);
        else
            locusPath.lineTo(p);
    }
    locusPath.closeSubpath(); // purple line

    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(40, 40, 46), 1));
        for (int i = 1; i <= 8; ++i) {
            const double t = i / 10.0;
            p.drawLine(map(t, 0), map(t, 0.9));
            if (t <= 0.9)
                p.drawLine(map(0, t), map(0.8, t));
        }
        p.fillPath(locusPath, QColor(28, 28, 34));
        p.setPen(QPen(QColor(150, 150, 160), 1.2));
        p.drawPath(locusPath);
    }

    // Chromaticity density of the picture (linear light, BT.709 primaries).
    QVector<quint32> acc(w * h, 0);
    QVector<QRgb> colAcc(w * h, 0);
    static double lin[256];
    static bool linInit = false;
    if (!linInit) {
        for (int i = 0; i < 256; ++i)
            lin[i] = std::pow(i / 255.0, 2.4);
        linInit = true;
    }
    double sumX = 0, sumY = 0;
    quint64 n = 0;
    const int step = std::max(1, src.width() / 360);
    for (int sy = 0; sy < src.height(); sy += step) {
        const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(sy));
        for (int sx = 0; sx < src.width(); sx += step) {
            const double R = lin[qRed(row[sx])], G = lin[qGreen(row[sx])], B = lin[qBlue(row[sx])];
            const double X = 0.4124 * R + 0.3576 * G + 0.1805 * B;
            const double Y = 0.2126 * R + 0.7152 * G + 0.0722 * B;
            const double Z = 0.0193 * R + 0.1192 * G + 0.9505 * B;
            const double sum = X + Y + Z;
            if (sum < 0.003)
                continue; // near black: chromaticity is noise
            const double x = X / sum, y = Y / sum;
            sumX += x;
            sumY += y;
            ++n;
            const QPointF pt = map(x, y);
            const int px = static_cast<int>(pt.x()), py = static_cast<int>(pt.y());
            if (px < 0 || py < 0 || px >= w || py >= h)
                continue;
            acc[py * w + px]++;
            colAcc[py * w + px] = row[sx];
        }
    }
    quint64 total = 0, nz = 0;
    for (quint32 v : acc)
        if (v) {
            total += v;
            ++nz;
        }
    const double k = nz ? (0.6 + 2.0 * input.settings.traceBrightness) / (static_cast<double>(total) / nz) : 1.0;
    for (int i = 0; i < w * h; ++i) {
        if (!acc[i])
            continue;
        const double a = 0.35 + 0.65 * (1.0 - std::exp(-k * acc[i]));
        const QColor c(colAcc[i]);
        // brighten the pixel colour so dark tones remain visible
        const double m = std::max({double(c.redF()), double(c.greenF()), double(c.blueF()), 0.05});
        img.setPixel(i % w, i / w,
                     qRgb(qBound(0, int(255 * a * c.redF() / m), 255), qBound(0, int(255 * a * c.greenF() / m), 255),
                          qBound(0, int(255 * a * c.blueF() / m), 255)));
    }

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QVector<QPointF> rec709 = {map(0.64, 0.33), map(0.30, 0.60), map(0.15, 0.06)};
    const QVector<QPointF> rec2020 = {map(0.708, 0.292), map(0.170, 0.797), map(0.131, 0.046)};
    p.setPen(QPen(QColor(100, 200, 255), 1.3));
    p.drawPolygon(rec709.constData(), 3);
    p.setPen(QPen(QColor(120, 255, 180, 150), 1, Qt::DashLine));
    p.drawPolygon(rec2020.constData(), 3);
    const QPointF d65 = map(0.3127, 0.3290);
    p.setPen(QPen(Qt::white, 1));
    p.drawLine(d65 - QPointF(5, 0), d65 + QPointF(5, 0));
    p.drawLine(d65 - QPointF(0, 5), d65 + QPointF(0, 5));
    QFont f = p.font();
    f.setPixelSize(std::max(9, std::min(13, h / 28)));
    p.setFont(f);
    p.setPen(QColor(160, 160, 170));
    p.drawText(d65 + QPointF(6, -4), QStringLiteral("D65"));
    p.setPen(QColor(100, 200, 255));
    p.drawText(QPointF(8, 14), QStringLiteral("Rec.709"));
    p.setPen(QColor(120, 255, 180));
    p.drawText(QPointF(8, 28), QStringLiteral("Rec.2020"));
    if (n > 0) {
        const QPointF m = map(sumX / n, sumY / n);
        p.setPen(QPen(QColor(255, 210, 60), 2));
        p.drawEllipse(m, 4, 4);
        p.drawText(QPointF(8, h - 8),
                   QStringLiteral("среднее x=%1 y=%2").arg(sumX / n, 0, 'f', 4).arg(sumY / n, 0, 'f', 4));
    }
    out.image = img;
    return out;
}

ScopeFrameOutput compareWipe(const QImage &a, const QImage &b, float wipePos)
{
    ScopeFrameOutput out;
    out.kind = ScopeKind::CompareWipe;
    const QImage left = a.isNull() ? QImage(320, 180, QImage::Format_RGB32) : a;
    QImage right = b.isNull() ? left : b;
    right = right.scaled(left.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    out.image = left.convertToFormat(QImage::Format_RGB32);
    const int cut = static_cast<int>(qBound(0.0f, wipePos, 1.0f) * out.image.width());
    for (int y = 0; y < out.image.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.image.scanLine(y));
        const QRgb *src = reinterpret_cast<const QRgb *>(right.constScanLine(y));
        for (int x = cut; x < out.image.width(); ++x)
            row[x] = src[x];
    }
    return out;
}

ScopeFrameOutput compareSideBySide(const QImage &a, const QImage &b)
{
    ScopeFrameOutput out;
    out.kind = ScopeKind::CompareSideBySide;
    const QImage left = a.isNull() ? QImage(320, 180, QImage::Format_RGB32) : a;
    QImage right = b.isNull() ? left : b;
    const int h = qMax(left.height(), right.height());
    const int w = left.width() + right.width();
    out.image = QImage(w, h, QImage::Format_RGB32);
    out.image.fill(Qt::black);
    QPainter p(&out.image);
    p.drawImage(0, 0, left);
    p.drawImage(left.width(), 0, right.scaledToHeight(h, Qt::FastTransformation));
    p.end();
    return out;
}

} // namespace ScopeRender