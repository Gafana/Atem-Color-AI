// Waveform / parade / vectorscope / histogram renderers.
// Input frames are full-range RGB32 (0 = 0 IRE, 255 = 100 IRE). Output images are rendered at the
// requested plot size, graticule and labels included, so the widget only blits them.
#include "ScopeRender.h"

#include "ScopeMath.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>

namespace ScopeRender {
namespace {

constexpr int kLevels = 256;
constexpr double kPi = 3.14159265358979323846;
const QColor kBg(14, 14, 16);
const QColor kGrid(48, 48, 54);
const QColor kGridMajor(92, 92, 100);
const QColor kLabel(150, 150, 160);
const QColor kTol(220, 70, 70);
const QColor kColR(255, 80, 72);
const QColor kColG(80, 235, 100);
const QColor kColB(90, 140, 255);

struct Frame
{
    int w = 0;
    int h = 0;
    QImage img;
};

/** Plot geometry for waveform-type scopes: left margin for IRE labels. */
struct WaveGeom
{
    QRect plot;
    double topIre = 104.0;
    double botIre = -4.0;
    double rowOf(double ire) const
    {
        return plot.top() + (topIre - ire) / (topIre - botIre) * (plot.height() - 1);
    }
};

WaveGeom waveGeom(int w, int h)
{
    WaveGeom g;
    const int left = w >= 260 ? 30 : 22;
    g.plot = QRect(left, 4, std::max(16, w - left - 4), std::max(16, h - 8));
    return g;
}

QFont smallFont(int h)
{
    QFont f;
    f.setPixelSize(h < 220 ? 9 : 11);
    return f;
}

void drawWaveGraticule(QPainter &p, const WaveGeom &g, const ScopeDisplaySettings &st)
{
    p.setFont(smallFont(g.plot.height()));
    for (int ire = 0; ire <= 100; ire += 10) {
        const int y = qRound(g.rowOf(ire));
        const bool major = ire == 0 || ire == 100 || ire == 50;
        p.setPen(QPen(major ? kGridMajor : kGrid, 1));
        p.drawLine(g.plot.left(), y, g.plot.right(), y);
        p.setPen(kLabel);
        p.drawText(QRect(0, y - 7, g.plot.left() - 4, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(ire));
    }
    const auto tolLine = [&](double ire) {
        if (std::fabs(ire) < 0.01 || std::fabs(ire - 100.0) < 0.01)
            return; // coincides with 0 / 100 graticule; out-of-range numbers turn red instead
        const int y = qRound(g.rowOf(ire));
        p.setPen(QPen(kTol, 1, Qt::DashLine));
        p.drawLine(g.plot.left(), y, g.plot.right(), y);
    };
    tolLine(st.ireToleranceLow);
    tolLine(st.ireToleranceHigh);
}

/** Robust min / max (0.05 % / 99.95 %) of a 256-bin histogram, in IRE. */
void robustExtents(const std::array<quint32, kLevels> &hist, quint64 total, double *mn, double *mx)
{
    *mn = 0.0;
    *mx = 0.0;
    if (total == 0)
        return;
    const quint64 cut = std::max<quint64>(1, total / 2000);
    quint64 acc = 0;
    for (int i = 0; i < kLevels; ++i) {
        acc += hist[static_cast<size_t>(i)];
        if (acc >= cut) {
            *mn = i / 255.0 * 100.0;
            break;
        }
    }
    acc = 0;
    for (int i = kLevels - 1; i >= 0; --i) {
        acc += hist[static_cast<size_t>(i)];
        if (acc >= cut) {
            *mx = i / 255.0 * 100.0;
            break;
        }
    }
}

/** Density -> intensity with an exposure-like curve so sparse detail stays visible. */
struct DensityMap
{
    double k = 1.0;
    explicit DensityMap(const QVector<quint32> &acc, float brightness)
    {
        quint64 sum = 0;
        quint64 nz = 0;
        for (quint32 v : acc) {
            if (v) {
                sum += v;
                ++nz;
            }
        }
        const double mean = nz ? static_cast<double>(sum) / nz : 1.0;
        k = (0.35 + 2.5 * brightness) / std::max(1.0, mean);
    }
    double operator()(quint32 c) const { return c ? 1.0 - std::exp(-k * c) : 0.0; }
};

/** Accumulates one channel into a column range of the plot (levels 0..255 -> rows). */
void accumulate(const QImage &src, QVector<quint32> &acc, const WaveGeom &g, int x0, int colW, int which)
{
    const int pw = g.plot.width();
    // Each code level covers the rows half way to its neighbours, so a plot taller than 256 rows
    // shows a continuous trace instead of striped "combing" between 8-bit levels.
    QVector<int> rowA(kLevels), rowB(kLevels);
    const int ph = g.plot.height();
    for (int v = 0; v < kLevels; ++v) {
        const int c = qBound(0, qRound(g.rowOf(v / 255.0 * 100.0)) - g.plot.top(), ph - 1);
        int a = qBound(0, qRound(g.rowOf((v + 0.5) / 255.0 * 100.0)) - g.plot.top() + 1, ph - 1);
        int b = qBound(0, qRound(g.rowOf((v - 0.5) / 255.0 * 100.0)) - g.plot.top(), ph - 1);
        if (a > c)
            a = c;
        if (b < c)
            b = c;
        rowA[v] = a;
        rowB[v] = b;
    }
    const int sw = src.width();
    for (int sy = 0; sy < src.height(); ++sy) {
        const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(sy));
        for (int sx = 0; sx < sw; ++sx) {
            const QRgb px = row[sx];
            int v;
            switch (which) {
            case 0: v = qRed(px); break;
            case 1: v = qGreen(px); break;
            case 2: v = qBlue(px); break;
            case 3: v = ScopeMath::lumaBt709(qRed(px), qGreen(px), qBlue(px)); break;
            case 4: { int y, cb, cr; ScopeMath::rgbToYCbCr709(qRed(px), qGreen(px), qBlue(px), &y, &cb, &cr); v = cb; break; }
            default: { int y, cb, cr; ScopeMath::rgbToYCbCr709(qRed(px), qGreen(px), qBlue(px), &y, &cb, &cr); v = cr; break; }
            }
            const int xa = x0 + qMin(colW - 1, sx * colW / sw);
            const int xb = x0 + qMin(colW - 1, qMax(sx * colW / sw, (sx + 1) * colW / sw - 1));
            for (int r = rowA[v]; r <= rowB[v]; ++r)
                for (int x = xa; x <= xb; ++x)
                    acc[r * pw + x]++;
        }
    }
}

void channelHistogram(const QImage &src, int which, std::array<quint32, kLevels> &hist, quint64 *total)
{
    hist.fill(0);
    *total = 0;
    for (int sy = 0; sy < src.height(); ++sy) {
        const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(sy));
        for (int sx = 0; sx < src.width(); ++sx) {
            const QRgb px = row[sx];
            const int v = which == 0 ? qRed(px)
                          : which == 1 ? qGreen(px)
                          : which == 2 ? qBlue(px)
                                       : ScopeMath::lumaBt709(qRed(px), qGreen(px), qBlue(px));
            hist[static_cast<size_t>(v)]++;
            ++*total;
        }
    }
}

void blitTrace(QImage &img, const WaveGeom &g, const QVector<quint32> &acc, const DensityMap &dm, const QColor &tint,
               int x0, int x1, bool additive)
{
    const int pw = g.plot.width();
    for (int y = 0; y < g.plot.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(g.plot.top() + y));
        for (int x = x0; x < x1; ++x) {
            const quint32 c = acc[y * pw + x];
            if (!c)
                continue;
            const double t = dm(c);
            // core colour + a white-hot centre for dense areas
            const double hot = t * t * 0.55;
            int r = static_cast<int>(tint.red() * t + 255 * hot);
            int gg = static_cast<int>(tint.green() * t + 255 * hot);
            int b = static_cast<int>(tint.blue() * t + 255 * hot);
            QRgb &dst = row[g.plot.left() + x];
            if (additive) {
                r += qRed(dst);
                gg += qGreen(dst);
                b += qBlue(dst);
            } else {
                r = std::max(r, qRed(dst));
                gg = std::max(gg, qGreen(dst));
                b = std::max(b, qBlue(dst));
            }
            dst = qRgb(qMin(255, r), qMin(255, gg), qMin(255, b));
        }
    }
}

void drawExtents(QPainter &p, const WaveGeom &g, int x0, int x1, double mn, double mx, const QColor &col,
                 const ScopeDisplaySettings &st, const QString &name)
{
    QFont f = smallFont(g.plot.height());
    f.setBold(true);
    p.setFont(f);
    const QRect top(g.plot.left() + x0 + 2, g.plot.top() + 1, x1 - x0 - 4, 14);
    const QRect bot(g.plot.left() + x0 + 2, g.plot.bottom() - 15, x1 - x0 - 4, 14);
    const bool hiBad = mx > st.ireToleranceHigh + 0.05 || mx >= 99.9;
    const bool loBad = mn < st.ireToleranceLow - 0.05 || mn <= 0.1;
    p.setPen(hiBad ? kTol : col);
    p.drawText(top, Qt::AlignRight | Qt::AlignTop, QStringLiteral("%1").arg(mx, 0, 'f', 1));
    p.setPen(loBad ? kTol : col);
    p.drawText(bot, Qt::AlignRight | Qt::AlignBottom, QStringLiteral("%1").arg(mn, 0, 'f', 1));
    if (!name.isEmpty()) {
        p.setPen(col);
        p.drawText(top, Qt::AlignLeft | Qt::AlignTop, name);
    }
}

QImage newCanvas(int w, int h)
{
    QImage img(std::max(32, w), std::max(32, h), QImage::Format_RGB32);
    img.fill(kBg);
    return img;
}

ScopeFrameOutput waveformMulti(const QImage &src, const ScopeFrameInput &input, ScopeKind kind)
{
    ScopeFrameOutput out;
    out.kind = kind;
    const int w = input.plotWidth;
    const int h = input.plotHeight;
    QImage img = newCanvas(w, h);
    const WaveGeom g = waveGeom(img.width(), img.height());
    const ScopeDisplaySettings &st = input.settings;
    {
        QPainter p(&img);
        drawWaveGraticule(p, g, st);
    }
    const int pw = g.plot.width();
    struct Ch
    {
        int which;
        QColor col;
        QString name;
    };
    QVector<Ch> chans;
    bool parade = true;
    bool additive = false;
    const bool mono = st.waveformMonochrome;
    switch (kind) {
    case ScopeKind::WaveformLuma:
        chans = {{3, mono ? QColor(210, 210, 210) : QColor(120, 255, 150), QStringLiteral("Y")}};
        parade = false;
        break;
    case ScopeKind::RgbParade:
        chans = {{0, mono ? QColor(210, 210, 210) : kColR, QStringLiteral("R")},
                 {1, mono ? QColor(210, 210, 210) : kColG, QStringLiteral("G")},
                 {2, mono ? QColor(210, 210, 210) : kColB, QStringLiteral("B")}};
        break;
    case ScopeKind::YcbcrParade:
        chans = {{3, QColor(220, 220, 220), QStringLiteral("Y")},
                 {4, QColor(110, 150, 255), QStringLiteral("Cb")},
                 {5, QColor(255, 110, 110), QStringLiteral("Cr")}};
        break;
    default: // overlay
        chans = {{0, kColR, QStringLiteral("R")}, {1, kColG, QStringLiteral("G")}, {2, kColB, QStringLiteral("B")}};
        parade = false;
        additive = true;
        break;
    }
    const int n = chans.size();
    QVector<double> mins(n), maxs(n);
    QVector<int> x0s(n), x1s(n);
    for (int i = 0; i < n; ++i) {
        const int x0 = parade ? i * pw / n : 0;
        const int x1 = parade ? (i + 1) * pw / n : pw;
        x0s[i] = x0;
        x1s[i] = x1;
        QVector<quint32> acc(pw * g.plot.height(), 0);
        accumulate(src, acc, g, x0 + (parade ? 2 : 0), (x1 - x0) - (parade ? 4 : 0), chans[i].which);
        const DensityMap dm(acc, st.traceBrightness);
        blitTrace(img, g, acc, dm, chans[i].col, x0, x1, additive);
        if (chans[i].which <= 3) {
            std::array<quint32, kLevels> hist;
            quint64 total = 0;
            channelHistogram(src, chans[i].which, hist, &total);
            robustExtents(hist, total, &mins[i], &maxs[i]);
        } else {
            mins[i] = maxs[i] = -1.0;
        }
    }
    QPainter p(&img);
    if (parade) {
        p.setPen(QPen(kGridMajor, 1));
        for (int i = 1; i < n; ++i)
            p.drawLine(g.plot.left() + x0s[i], g.plot.top(), g.plot.left() + x0s[i], g.plot.bottom());
    }
    if (parade) {
        for (int i = 0; i < n; ++i)
            if (maxs[i] >= 0.0)
                drawExtents(p, g, x0s[i], x1s[i], mins[i], maxs[i], chans[i].col, st, chans[i].name);
            else
                drawExtents(p, g, x0s[i], x1s[i], 0, 0, chans[i].col, st, chans[i].name);
    } else if (kind == ScopeKind::WaveformLuma) {
        drawExtents(p, g, 0, pw, mins[0], maxs[0], chans[0].col, st, QString());
    } else {
        // overlay: three numbers side by side
        for (int i = 0; i < n; ++i)
            drawExtents(p, g, i * pw / 3, (i + 1) * pw / 3, mins[i], maxs[i], chans[i].col, st, chans[i].name);
    }
    out.image = img;
    return out;
}

} // namespace

ScopeFrameOutput waveformLuma(const QImage &src, const ScopeFrameInput &input)
{
    return waveformMulti(src, input, ScopeKind::WaveformLuma);
}

ScopeFrameOutput rgbParade(const QImage &src, const ScopeFrameInput &input)
{
    return waveformMulti(src, input, ScopeKind::RgbParade);
}

ScopeFrameOutput ycbcrParade(const QImage &src, const ScopeFrameInput &input)
{
    return waveformMulti(src, input, ScopeKind::YcbcrParade);
}

ScopeFrameOutput rgbOverlay(const QImage &src, const ScopeFrameInput &input)
{
    return waveformMulti(src, input, ScopeKind::RgbOverlayWaveform);
}

// ---------------------------------------------------------------------------------------------
// Vectorscope (BT.709 Cb/Cr). 100 % saturation primaries sit on their target boxes.

namespace {

struct VecGeom
{
    QPointF c;
    double radius = 100; // radius of the 100 % graticule circle (Cb/Cr magnitude 0.5 * 0.9)
    double zoom = 1.0;
    QPointF map(double cb, double cr) const
    {
        // cb/cr in -0.5..0.5; 0.45 maps to the graticule circle (outer targets ~ 0.47)
        const double s = radius / 0.45 * zoom;
        return QPointF(c.x() + cb * s, c.y() - cr * s);
    }
};

void cbcrOf(double r, double g, double b, double *cb, double *cr)
{
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    *cb = (b - y) / 1.8556;
    *cr = (r - y) / 1.5748;
}

void drawVectorGraticule(QPainter &p, const VecGeom &g, bool labels)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    // hue rim (own style): thin ring of hue colours just outside the 100 % circle
    const double rimR = g.radius * 1.06;
    for (int a = 0; a < 360; a += 3) {
        const double rad = a * kPi / 180.0;
        // hue at this Cb/Cr angle: reconstruct an RGB with that chroma direction
        const double cb = 0.3 * std::cos(rad);
        const double cr = 0.3 * std::sin(rad);
        double R = 0.5 + 1.5748 * cr;
        double B = 0.5 + 1.8556 * cb;
        double G = (0.5 - 0.2126 * R - 0.0722 * B) / 0.7152;
        const double mx = std::max({R, G, B, 1e-6});
        const double mn = std::min({R, G, B});
        R = (R - mn) / (mx - mn);
        G = (G - mn) / (mx - mn);
        B = (B - mn) / (mx - mn);
        p.setPen(QPen(QColor::fromRgbF(R * 0.75, G * 0.75, B * 0.75), std::max(2.0, g.radius * 0.035)));
        p.drawArc(QRectF(g.c.x() - rimR, g.c.y() - rimR, 2 * rimR, 2 * rimR), a * 16, 3 * 16 + 4);
    }
    p.setBrush(Qt::NoBrush);
    // With zoom > 1 the scaled graticule (rings, targets, skin line) would spill past the rim and
    // over neighbouring zone panels: clip everything zoom-dependent to the inside of the rim.
    p.save();
    if (g.zoom > 1.0) {
        QPainterPath clip;
        clip.addEllipse(g.c, g.radius * 1.03, g.radius * 1.03);
        p.setClipPath(clip);
    }
    for (double f : {0.25, 0.5, 0.75}) {
        p.setPen(QPen(kGrid, 1));
        p.drawEllipse(g.c, g.radius * f * g.zoom, g.radius * f * g.zoom);
    }
    p.setPen(QPen(kGridMajor, 1));
    p.drawEllipse(g.c, g.radius, g.radius);
    p.drawLine(QPointF(g.c.x() - g.radius, g.c.y()), QPointF(g.c.x() + g.radius, g.c.y()));
    p.drawLine(QPointF(g.c.x(), g.c.y() - g.radius), QPointF(g.c.x(), g.c.y() + g.radius));
    // skin tone ("I") line at 123 degrees
    {
        const double a = 123.0 * kPi / 180.0;
        p.setPen(QPen(QColor(230, 170, 120, 170), 1, Qt::DashLine));
        p.drawLine(g.c, QPointF(g.c.x() + std::cos(a) * g.radius, g.c.y() - std::sin(a) * g.radius));
    }
    struct T
    {
        const char *name;
        double r, gg, b;
        QColor col;
    };
    const T targets[] = {{"R", 1, 0, 0, kColR}, {"Mg", 1, 0, 1, QColor(255, 90, 255)}, {"B", 0, 0, 1, kColB},
                         {"Cy", 0, 1, 1, QColor(80, 240, 240)}, {"G", 0, 1, 0, kColG}, {"Yl", 1, 1, 0, QColor(250, 240, 80)}};
    QFont f = smallFont(static_cast<int>(g.radius * 2));
    p.setFont(f);
    for (const T &t : targets) {
        for (double level : {1.0, 0.75}) {
            double cb, cr;
            cbcrOf(t.r * level, t.gg * level, t.b * level, &cb, &cr);
            const QPointF pt = g.map(cb, cr);
            const double s = std::max(3.0, g.radius * (level == 1.0 ? 0.045 : 0.035));
            p.setPen(QPen(level == 1.0 ? t.col : t.col.darker(150), 1));
            p.drawRect(QRectF(pt.x() - s, pt.y() - s, 2 * s, 2 * s));
            const bool inside = std::hypot(pt.x() - g.c.x(), pt.y() - g.c.y()) < g.radius * 1.03;
            if (labels && level == 1.0 && (g.zoom <= 1.0 || inside)) {
                const QPointF dir = (pt - g.c);
                const double len = std::hypot(dir.x(), dir.y());
                const QPointF lp = len > 1 ? pt + dir / len * (s + 9) : pt;
                p.setPen(t.col);
                p.drawText(QRectF(lp.x() - 12, lp.y() - 7, 24, 14), Qt::AlignCenter, QString::fromLatin1(t.name));
            }
        }
    }
    p.restore();
}

void drawVectorTrace(QImage &img, const QImage &src, const VecGeom &g, float brightness, int zone,
                     const ScopeDisplaySettings &st)
{
    const int box = static_cast<int>(std::ceil(g.radius * 2.3));
    const int ox = static_cast<int>(g.c.x()) - box / 2;
    const int oy = static_cast<int>(g.c.y()) - box / 2;
    QVector<quint32> cnt(box * box, 0);
    QVector<float> sr(box * box, 0), sg(box * box, 0), sb(box * box, 0);
    const double lo = st.zoneLowMaxPct / 100.0;
    const double mid = st.zoneMidMaxPct / 100.0;
    for (int y = 0; y < src.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const double r = qRed(row[x]) / 255.0, gg = qGreen(row[x]) / 255.0, b = qBlue(row[x]) / 255.0;
            if (zone >= 0) {
                const double l = 0.2126 * r + 0.7152 * gg + 0.0722 * b;
                const int z = l < lo ? 0 : (l < mid ? 1 : 2);
                if (z != zone)
                    continue;
            }
            double cb, cr;
            cbcrOf(r, gg, b, &cb, &cr);
            const QPointF pt = g.map(cb, cr);
            if (g.zoom > 1.0
                && std::hypot(pt.x() - g.c.x(), pt.y() - g.c.y()) > g.radius * 1.03)
                continue; // zoomed: trace is clipped at the rim like on a hardware scope
            const int px = static_cast<int>(pt.x()) - ox;
            const int py = static_cast<int>(pt.y()) - oy;
            if (px < 0 || py < 0 || px >= box || py >= box)
                continue;
            const int i = py * box + px;
            cnt[i]++;
            sr[i] += static_cast<float>(r);
            sg[i] += static_cast<float>(gg);
            sb[i] += static_cast<float>(b);
        }
    }
    const DensityMap dm(cnt, brightness);
    for (int y = 0; y < box; ++y) {
        const int iy = oy + y;
        if (iy < 0 || iy >= img.height())
            continue;
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(iy));
        for (int x = 0; x < box; ++x) {
            const int ix = ox + x;
            const int i = y * box + x;
            if (ix < 0 || ix >= img.width() || !cnt[i])
                continue;
            const double t = dm(cnt[i]);
            // colourised trace: average pixel colour normalised to full brightness
            double r = sr[i] / cnt[i], gg = sg[i] / cnt[i], b = sb[i] / cnt[i];
            const double mx = std::max({r, gg, b, 1e-3});
            r = 0.35 + 0.65 * r / mx;
            gg = 0.35 + 0.65 * gg / mx;
            b = 0.35 + 0.65 * b / mx;
            const double hot = t * t * 0.5;
            row[ix] = qRgb(qMin(255, static_cast<int>(255 * (r * t + hot))),
                           qMin(255, static_cast<int>(255 * (gg * t + hot))),
                           qMin(255, static_cast<int>(255 * (b * t + hot))));
        }
    }
}

} // namespace

ScopeFrameOutput vectorscope(const QImage &src, const ScopeFrameInput &input)
{
    ScopeFrameOutput out;
    out.kind = ScopeKind::Vectorscope;
    const ScopeDisplaySettings &st = input.settings;
    QImage img = newCanvas(input.plotWidth, input.plotHeight);
    const double zoom = std::max(1, st.vectorscopeZoom);
    if (!st.vectorscopeLumaZones) {
        VecGeom g;
        g.c = QPointF(img.width() / 2.0, img.height() / 2.0);
        g.radius = std::min(img.width(), img.height()) * 0.37; // rim (1.06R) + 100% targets + labels must fit
        g.zoom = zoom;
        drawVectorTrace(img, src, g, st.traceBrightness, -1, st);
        QPainter p(&img);
        drawVectorGraticule(p, g, true);
        if (zoom > 1.0) {
            p.setPen(kLabel);
            p.drawText(QRect(4, 4, 60, 14), Qt::AlignLeft, QStringLiteral("x%1").arg(zoom));
        }
    } else {
        const char *names[3] = {"Тени", "Средние", "Света"};
        const int cell = img.width() / 3;
        for (int z = 0; z < 3; ++z) {
            VecGeom g;
            g.c = QPointF(cell * z + cell / 2.0, img.height() / 2.0 + 6);
            g.radius = std::min(cell, img.height() - 16) * 0.40;
            g.zoom = zoom;
            drawVectorTrace(img, src, g, st.traceBrightness, z, st);
            QPainter p(&img);
            drawVectorGraticule(p, g, g.radius > 60);
            p.setPen(kLabel);
            p.setFont(smallFont(img.height()));
            p.drawText(QRect(cell * z, 2, cell, 14), Qt::AlignHCenter, QString::fromUtf8(names[z]));
        }
        if (zoom > 1.0) {
            QPainter p(&img);
            p.setPen(kLabel);
            p.setFont(smallFont(img.height()));
            p.drawText(QRect(4, 2, 40, 14), Qt::AlignLeft, QStringLiteral("x%1").arg(zoom));
        }
    }
    out.image = img;
    return out;
}

// ---------------------------------------------------------------------------------------------
// Histograms (x = level 0..100 IRE)

namespace {

void drawHistogramPane(QPainter &p, const QRect &r, const std::array<quint32, kLevels> &hist, const QColor &col,
                       bool fill, double peak)
{
    QPainterPath path;
    path.moveTo(r.left(), r.bottom());
    for (int i = 0; i < kLevels; ++i) {
        const double x = r.left() + i * (r.width() - 1) / 255.0;
        const double v = std::sqrt(hist[static_cast<size_t>(i)] / std::max(1.0, peak));
        path.lineTo(x, r.bottom() - std::min(1.0, v) * (r.height() - 2));
    }
    path.lineTo(r.right(), r.bottom());
    path.closeSubpath();
    QColor c = col;
    c.setAlpha(fill ? 110 : 0);
    p.setBrush(fill ? QBrush(c) : Qt::NoBrush);
    p.setPen(QPen(col, 1.2));
    p.drawPath(path);
}

void drawHistGrid(QPainter &p, const QRect &r, bool labels)
{
    p.setFont(smallFont(r.height()));
    for (int ire = 0; ire <= 100; ire += 10) {
        const int x = r.left() + ire * (r.width() - 1) / 100;
        p.setPen(QPen(ire % 50 == 0 ? kGridMajor : kGrid, 1));
        p.drawLine(x, r.top(), x, r.bottom());
        if (labels && ire % 20 == 0) {
            p.setPen(kLabel);
            p.drawText(QRect(x - 14, r.bottom() + 1, 28, 12), Qt::AlignCenter, QString::number(ire));
        }
    }
}

double peakOf(const std::array<quint32, kLevels> &h)
{
    // ignore the two end bins so clipped areas do not flatten the rest
    quint32 pk = 1;
    for (int i = 1; i < kLevels - 1; ++i)
        pk = std::max(pk, h[static_cast<size_t>(i)]);
    return pk;
}

} // namespace

ScopeFrameOutput histogramLuma(const QImage &src, const ScopeFrameInput &input)
{
    ScopeFrameOutput out;
    out.kind = ScopeKind::HistogramLuma;
    QImage img = newCanvas(input.plotWidth, input.plotHeight);
    std::array<quint32, kLevels> h;
    quint64 total = 0;
    channelHistogram(src, 3, h, &total);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r(8, 6, img.width() - 16, img.height() - 22);
    drawHistGrid(p, r, true);
    drawHistogramPane(p, r, h, QColor(220, 220, 220), true, peakOf(h));
    double mn, mx;
    robustExtents(h, total, &mn, &mx);
    p.setPen(mx >= 99.9 || mn <= 0.1 ? kTol : kLabel);
    p.drawText(r.adjusted(4, 2, -4, 0), Qt::AlignRight | Qt::AlignTop,
               QStringLiteral("Y %1–%2 IRE").arg(mn, 0, 'f', 1).arg(mx, 0, 'f', 1));
    out.image = img;
    return out;
}

ScopeFrameOutput histogramRgb(const QImage &src, const ScopeFrameInput &input, bool overlay)
{
    ScopeFrameOutput out;
    out.kind = overlay ? ScopeKind::HistogramRgbOverlay : ScopeKind::HistogramRgb;
    QImage img = newCanvas(input.plotWidth, input.plotHeight);
    std::array<quint32, kLevels> h[3];
    quint64 total = 0;
    for (int c = 0; c < 3; ++c)
        channelHistogram(src, c, h[c], &total);
    const QColor cols[3] = {kColR, kColG, kColB};
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    if (overlay) {
        const QRect r(8, 6, img.width() - 16, img.height() - 22);
        drawHistGrid(p, r, true);
        const double pk = std::max({peakOf(h[0]), peakOf(h[1]), peakOf(h[2])});
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        for (int c = 0; c < 3; ++c)
            drawHistogramPane(p, r, h[c], cols[c], true, pk);
    } else {
        const int paneH = (img.height() - 16) / 3;
        for (int c = 0; c < 3; ++c) {
            const QRect r(8, 4 + c * paneH, img.width() - 16, paneH - 6);
            drawHistGrid(p, r, c == 2);
            drawHistogramPane(p, r, h[c], cols[c], true, peakOf(h[c]));
        }
    }
    out.image = img;
    return out;
}

} // namespace ScopeRender
