#include "FrameAdvisor.h"

#include "ColorScience.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>

namespace FrameAdvisor {

namespace {

using namespace ColorScience;

constexpr int kClipCode = 252;  // ~99 IRE: treated as clipped
constexpr int kCrushCode = 3;   // ~1 IRE: treated as crushed black
constexpr int kGridCols = 24;
constexpr int kGridRows = 14;
constexpr int kMaxAnalyseWidth = 360;

enum Flag
{
    FClip,
    FCrush,
    FOversat,
    FNeutral,
    FSkin,
    FCount,
};

struct Cell
{
    int n = 0;
    int f[FCount] = {};
};

double pct(double part, double whole) { return whole > 0 ? 100.0 * part / whole : 0.0; }

double stopsBetween(double fromIre, double toIre)
{
    const double a = decode(qBound(0.005, fromIre / 100.0, 1.0), Transfer::Bt709Oetf);
    const double b = decode(qBound(0.005, toIre / 100.0, 1.0), Transfer::Bt709Oetf);
    return std::log2(b / a);
}

QString fmt(double v, int prec = 1) { return QString::number(v, 'f', prec); }

/** Connected flagged cells -> bounding rects (normalized in the whole frame), biggest first. */
QVector<QRectF> regionsFor(const std::array<Cell, kGridCols * kGridRows> &grid, int flag, double minFrac,
                           const QRectF &area)
{
    bool on[kGridCols * kGridRows] = {};
    for (int i = 0; i < kGridCols * kGridRows; ++i)
        on[i] = grid[i].n > 0 && grid[i].f[flag] >= std::max(1.0, minFrac * grid[i].n);
    int label[kGridCols * kGridRows];
    std::fill(std::begin(label), std::end(label), -1);
    struct Box
    {
        int x0, y0, x1, y1, cells;
    };
    QVector<Box> boxes;
    for (int i = 0; i < kGridCols * kGridRows; ++i) {
        if (!on[i] || label[i] >= 0)
            continue;
        Box b{kGridCols, kGridRows, -1, -1, 0};
        QVector<int> stack{i};
        label[i] = boxes.size();
        while (!stack.isEmpty()) {
            const int c = stack.takeLast();
            const int cx = c % kGridCols, cy = c / kGridCols;
            b.x0 = std::min(b.x0, cx);
            b.y0 = std::min(b.y0, cy);
            b.x1 = std::max(b.x1, cx);
            b.y1 = std::max(b.y1, cy);
            ++b.cells;
            const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto &d : nb) {
                const int nx = cx + d[0], ny = cy + d[1];
                if (nx < 0 || ny < 0 || nx >= kGridCols || ny >= kGridRows)
                    continue;
                const int ni = ny * kGridCols + nx;
                if (on[ni] && label[ni] < 0) {
                    label[ni] = label[i];
                    stack.push_back(ni);
                }
            }
        }
        boxes.push_back(b);
    }
    std::sort(boxes.begin(), boxes.end(), [](const Box &a, const Box &b) { return a.cells > b.cells; });
    QVector<QRectF> out;
    const double cw = area.width() / kGridCols, ch = area.height() / kGridRows;
    for (const Box &b : boxes) {
        out.push_back(QRectF(area.left() + b.x0 * cw, area.top() + b.y0 * ch, (b.x1 - b.x0 + 1) * cw,
                             (b.y1 - b.y0 + 1) * ch));
        if (out.size() >= 6)
            break;
    }
    return out;
}

QString castWords(double a, double b)
{
    QStringList w;
    if (b > 1.5)
        w << QStringLiteral("жёлтый/тёплый");
    else if (b < -1.5)
        w << QStringLiteral("синий/холодный");
    if (a > 1.5)
        w << QStringLiteral("пурпурный");
    else if (a < -1.5)
        w << QStringLiteral("зелёный");
    return w.isEmpty() ? QStringLiteral("слабый") : w.join(QStringLiteral(" + "));
}

QString castFix(double a, double b)
{
    QStringList w;
    if (b > 1.5)
        w << QStringLiteral("уменьшите цветовую температуру баланса белого (K)");
    else if (b < -1.5)
        w << QStringLiteral("увеличьте цветовую температуру баланса белого (K)");
    if (a > 1.5)
        w << QStringLiteral("сдвиньте Tint к зелёному (−)");
    else if (a < -1.5)
        w << QStringLiteral("сдвиньте Tint к пурпурному (+)");
    if (w.isEmpty())
        return QStringLiteral("подстройте баланс белого по белому листу");
    return w.join(QStringLiteral(", ")) + QStringLiteral(" — или «Авто до допуска» по белому листу");
}

} // namespace

QString severityLabel(Severity s)
{
    switch (s) {
    case Severity::Ok:
        return QStringLiteral("норма");
    case Severity::Info:
        return QStringLiteral("инфо");
    case Severity::Warning:
        return QStringLiteral("внимание");
    case Severity::Critical:
        return QStringLiteral("проблема");
    }
    return {};
}

QString Advice::verdict() const
{
    QStringList bad;
    for (const Issue &i : issues)
        if (i.severity >= Severity::Warning)
            bad << i.text.section(QLatin1Char(':'), 0, 0);
    return bad.isEmpty() ? QStringLiteral("норма") : bad.join(QStringLiteral("; "));
}

Advice analyze(const QImage &frameIn, const QRectF &roi, const Options &opt)
{
    Advice adv;
    adv.label = opt.label;
    if (frameIn.isNull())
        return adv;
    const QImage frame = frameIn.convertToFormat(QImage::Format_RGB32);
    QRectF area(0, 0, 1, 1);
    QRect px(QPoint(0, 0), frame.size());
    if (!roi.isEmpty()) {
        const QRectF c = roi.intersected(QRectF(0, 0, 1, 1));
        const QRect r(qFloor(c.left() * frame.width()), qFloor(c.top() * frame.height()),
                      qCeil(c.width() * frame.width()), qCeil(c.height() * frame.height()));
        const QRect cr = r.intersected(px);
        if (cr.width() >= 4 && cr.height() >= 4) {
            px = cr;
            area = QRectF(double(px.left()) / frame.width(), double(px.top()) / frame.height(),
                          double(px.width()) / frame.width(), double(px.height()) / frame.height());
            adv.zone = true;
        }
    }
    QImage img = frame.copy(px);
    if (img.width() > kMaxAnalyseWidth)
        img = img.scaledToWidth(kMaxAnalyseWidth, Qt::FastTransformation);

    std::array<double, 256> lin{};
    for (int i = 0; i < 256; ++i)
        lin[i] = decode(i / 255.0, Transfer::Bt709Oetf);

    std::array<Cell, kGridCols * kGridRows> grid{};
    Stats &st = adv.stats;
    int hist[256] = {};
    double sumY = 0, clip[3] = {0, 0, 0}, clipAny = 0, crush = 0, oversat = 0;
    double nN = 0, nA = 0, nB = 0, sL = 0, sA = 0, sB = 0;
    QVector<double> skinAngles;
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        const int gy = std::min(kGridRows - 1, y * kGridRows / h);
        for (int x = 0; x < w; ++x) {
            const int r = qRed(row[x]), g = qGreen(row[x]), b = qBlue(row[x]);
            Cell &cell = grid[gy * kGridCols + std::min(kGridCols - 1, x * kGridCols / w)];
            ++cell.n;
            const double Y = kLumaR * r + kLumaG * g + kLumaB * b;
            ++hist[qBound(0, int(Y + 0.5), 255)];
            sumY += Y;
            const bool cr = r >= kClipCode, cg = g >= kClipCode, cb = b >= kClipCode;
            clip[0] += cr;
            clip[1] += cg;
            clip[2] += cb;
            if (cr || cg || cb) {
                ++clipAny;
                ++cell.f[FClip];
            }
            if (r <= kCrushCode && g <= kCrushCode && b <= kCrushCode) {
                ++crush;
                ++cell.f[FCrush];
            }
            const Lab lab = linear709ToLabD65(Rgb{lin[r], lin[g], lin[b]});
            sL += lab.L;
            sA += lab.a;
            sB += lab.b;
            const double C = std::hypot(lab.a, lab.b);
            const bool emptyChannel = std::min({r, g, b}) <= 2 && std::max({r, g, b}) >= kClipCode;
            if (C > kOversatChroma || emptyChannel) {
                ++oversat;
                ++cell.f[FOversat];
            }
            if (C < 15.0 && lab.L > 25.0 && lab.L < 95.0 && !(cr || cg || cb)) {
                ++nN;
                nA += lab.a;
                nB += lab.b;
                ++cell.f[FNeutral];
            }
            // skin: midtone, moderately saturated, hue near the I-line on a Cb/Cr vectorscope
            const double Cb = (b - Y) / 1.8556, Cr = (r - Y) / 1.5748;
            const double cm = std::hypot(Cb, Cr);
            if (Y > 60 && Y < 235 && cm > 8 && cm < 70) {
                const double ang = qRadiansToDegrees(std::atan2(Cr, Cb));
                if (std::abs(ang - kSkinLineDeg) < 28.0) {
                    skinAngles.push_back(ang);
                    ++cell.f[FSkin];
                }
            }
        }
    }
    const double n = double(w) * h;
    st.area = n;
    for (int c = 0; c < 3; ++c)
        st.clipPct[c] = pct(clip[c], n);
    st.clipAnyPct = pct(clipAny, n);
    st.crushPct = pct(crush, n);
    st.meanIre = sumY / n / 2.55;
    {
        double acc = 0;
        bool med = false;
        for (int i = 0; i < 256; ++i) {
            acc += hist[i];
            if (!med && acc >= n * 0.5) {
                st.medianIre = i / 2.55;
                med = true;
            }
            if (acc >= n * 0.98) {
                st.p98Ire = i / 2.55;
                break;
            }
        }
    }
    st.meanL = sL / n;
    st.meanA = sA / n;
    st.meanB = sB / n;
    st.neutralPct = pct(nN, n);
    if (nN > 0) {
        st.castA = nA / nN;
        st.castB = nB / nN;
    }
    st.oversatPct = pct(oversat, n);
    st.skinPct = pct(skinAngles.size(), n);
    if (!skinAngles.isEmpty()) {
        std::nth_element(skinAngles.begin(), skinAngles.begin() + skinAngles.size() / 2, skinAngles.end());
        st.skinHueDeg = skinAngles.at(skinAngles.size() / 2);
    }

    // noise: luma std of 8x8 blocks at full resolution, 20th percentile (= flat areas)
    {
        QVector<double> stds;
        const int bs = 8;
        const int stepX = std::max(bs, px.width() / 48), stepY = std::max(bs, px.height() / 27);
        for (int by = px.top(); by + bs <= px.bottom() + 1; by += stepY) {
            for (int bx = px.left(); bx + bs <= px.right() + 1; bx += stepX) {
                double s = 0, s2 = 0;
                for (int yy = 0; yy < bs; ++yy) {
                    const QRgb *row = reinterpret_cast<const QRgb *>(frame.constScanLine(by + yy));
                    for (int xx = 0; xx < bs; ++xx) {
                        const QRgb p = row[bx + xx];
                        const double Y = kLumaR * qRed(p) + kLumaG * qGreen(p) + kLumaB * qBlue(p);
                        s += Y;
                        s2 += Y * Y;
                    }
                }
                const double m = s / (bs * bs);
                if (m < 12 || m > 240) // clipped / crushed blocks have no visible noise
                    continue;
                stds.push_back(std::sqrt(std::max(0.0, s2 / (bs * bs) - m * m)) / 2.55);
            }
        }
        if (stds.size() >= 8) {
            std::sort(stds.begin(), stds.end());
            st.noiseIre = stds.at(stds.size() / 5);
        }
    }

    // ---- rules -> issues
    if (st.p98Ire < 3.0) {
        // nothing to judge: a black picture (lens cap, no light, iris closed, wrong source)
        Issue is;
        is.code = QStringLiteral("black");
        is.severity = Severity::Critical;
        is.text = QStringLiteral("Чёрный кадр: изображения нет (98%% пикселей ниже 3 IRE)");
        is.text.replace(QStringLiteral("%%"), QStringLiteral("%"));
        is.advice = QStringLiteral("Проверьте крышку объектива, свет, диафрагму и что на AUX выбрана нужная камера");
        is.regions = {area};
        adv.issues.push_back(is);
        return adv;
    }
    const QString where = adv.zone ? QStringLiteral(" зоны") : QStringLiteral(" кадра");
    if (st.clipAnyPct >= 0.5) {
        Issue is;
        is.code = QStringLiteral("clip");
        is.severity = st.clipAnyPct >= 5 ? Severity::Critical : Severity::Warning;
        const double mx = std::max({st.clipPct[0], st.clipPct[1], st.clipPct[2]});
        const double mn = std::min({st.clipPct[0], st.clipPct[1], st.clipPct[2]});
        is.text = QStringLiteral("Пересвет: %1% площади%2 (R %3%, G %4%, B %5%)")
                      .arg(fmt(st.clipAnyPct), where, fmt(st.clipPct[0]), fmt(st.clipPct[1]), fmt(st.clipPct[2]));
        if (mx > 0.5 && mn < mx * 0.3) {
            const int c = st.clipPct[0] == mx ? 0 : (st.clipPct[1] == mx ? 1 : 2);
            const char *names[] = {"R", "G", "B"};
            is.advice = QStringLiteral("Клипует в основном канал %1 — цвет в светах уплывает. Закройте диафрагму на "
                                       "⅓–½ ступени или уменьшите усиление %1 (Gain %1 / баланс белого)")
                            .arg(QLatin1String(names[c]));
        } else {
            is.advice = st.clipAnyPct >= 5 ? QStringLiteral("Закройте диафрагму примерно на 1 ступень (или снизьте Gain / ND)")
                                           : QStringLiteral("Закройте диафрагму на ⅓–½ ступени, если в пересвете важные детали");
        }
        is.regions = regionsFor(grid, FClip, 0.25, area);
        adv.issues.push_back(is);
    }
    if (st.crushPct >= 3.0) {
        Issue is;
        is.code = QStringLiteral("crush");
        is.severity = st.crushPct >= 20 ? Severity::Critical : Severity::Warning;
        is.text = QStringLiteral("Завал чёрного: %1% площади%2 на 0 IRE").arg(fmt(st.crushPct), where);
        is.advice = QStringLiteral("Поднимите Lift (чёрный) на 0,01–0,03 или откройте диафрагму; проверьте Contrast");
        is.regions = regionsFor(grid, FCrush, 0.3, area);
        adv.issues.push_back(is);
    }
    if (!adv.zone) {
        const double target = opt.targetIre;
        const double d = st.medianIre - target;
        if (std::abs(d) >= 12.0) {
            Issue is;
            is.code = QStringLiteral("exposure");
            const double stops = stopsBetween(st.medianIre, target);
            is.severity = std::abs(stops) >= 1.0 ? Severity::Warning : Severity::Info;
            is.text = QStringLiteral("Экспозиция: медиана %1 IRE при ориентире ~%2 IRE")
                          .arg(fmt(st.medianIre, 0), fmt(target, 0));
            is.advice = stops > 0 ? QStringLiteral("Кадр тёмный: откройте диафрагму примерно на %1 ступ. (или добавьте свет; Gain — в последнюю очередь)")
                                        .arg(fmt(std::abs(stops)))
                                  : QStringLiteral("Кадр светлый: закройте диафрагму примерно на %1 ступ.")
                                        .arg(fmt(std::abs(stops)));
            adv.issues.push_back(is);
        }
    } else {
        Issue is;
        is.code = QStringLiteral("exposure");
        is.severity = Severity::Info;
        is.text = QStringLiteral("Уровень зоны: медиана %1 IRE, 98%% — %2 IRE").arg(fmt(st.medianIre, 0), fmt(st.p98Ire, 0));
        is.text.replace(QStringLiteral("%%"), QStringLiteral("%"));
        is.advice = st.neutralPct > 60 && st.medianIre > 50
                        ? QStringLiteral("Для белого листа удобно 75–90 IRE: светлее — риск пересвета, темнее — шум")
                        : QStringLiteral("Сравните с эталонной камерой по той же зоне");
        adv.issues.push_back(is);
    }
    if (st.neutralPct >= 2.0) {
        const double cast = std::hypot(st.castA, st.castB);
        if (cast >= 4.0) {
            Issue is;
            is.code = QStringLiteral("cast");
            is.severity = cast >= 10 ? Severity::Critical : Severity::Warning;
            is.text = QStringLiteral("Баланс белого: %1 оттенок на нейтральных участках (a* %2, b* %3)")
                          .arg(castWords(st.castA, st.castB), fmt(st.castA), fmt(st.castB));
            is.advice = castFix(st.castA, st.castB);
            is.regions = regionsFor(grid, FNeutral, 0.5, area);
            adv.issues.push_back(is);
        }
    }
    if (st.oversatPct >= 1.5) {
        Issue is;
        is.code = QStringLiteral("oversat");
        is.severity = st.oversatPct >= 8 ? Severity::Critical : Severity::Warning;
        is.text = QStringLiteral("Перенасыщение / выход за гамут: %1% площади%2").arg(fmt(st.oversatPct), where);
        is.advice = QStringLiteral("Уменьшите Saturation (≈ на 0,1–0,2) или проверьте подсветку цветных объектов");
        is.regions = regionsFor(grid, FOversat, 0.3, area);
        adv.issues.push_back(is);
    }
    if (st.skinPct >= 3.0 && std::isfinite(st.skinHueDeg)) {
        const double dev = st.skinHueDeg - kSkinLineDeg;
        if (std::abs(dev) >= 8.0) {
            Issue is;
            is.code = QStringLiteral("skin");
            is.severity = std::abs(dev) >= 15 ? Severity::Warning : Severity::Info;
            is.text = QStringLiteral("Тон кожи: %1° от линии телесных тонов (%2)")
                          .arg(fmt(dev, 0), dev > 0 ? QStringLiteral("в жёлтый") : QStringLiteral("в красный/пурпурный"));
            is.advice = QStringLiteral("Если в кадре лица — сначала баланс белого, затем Hue на несколько градусов %1")
                            .arg(dev > 0 ? QStringLiteral("к красному") : QStringLiteral("к жёлтому"));
            is.regions = regionsFor(grid, FSkin, 0.3, area);
            adv.issues.push_back(is);
        }
    }
    const bool highGain = std::isfinite(opt.gainDb) && opt.gainDb >= 18.0;
    if (std::isfinite(st.noiseIre) && st.noiseIre >= 1.5) {
        Issue is;
        is.code = QStringLiteral("noise");
        is.severity = st.noiseIre >= 3.0 ? Severity::Warning : Severity::Info;
        is.text = QStringLiteral("Шум: ~%1 IRE на ровных участках").arg(fmt(st.noiseIre));
        is.advice = highGain ? QStringLiteral("Снизьте Gain (сейчас %1 dB) до ≤ 12 dB, компенсируя диафрагмой/светом")
                                   .arg(fmt(opt.gainDb, 0))
                             : QStringLiteral("Снизьте Gain / добавьте света; проверьте шумоподавление камеры");
        adv.issues.push_back(is);
    } else if (highGain) {
        Issue is;
        is.code = QStringLiteral("gain");
        is.severity = Severity::Info;
        is.text = QStringLiteral("Высокое усиление: %1 dB").arg(fmt(opt.gainDb, 0));
        is.advice = QStringLiteral("Если хватает света — снизьте Gain до ≤ 12 dB и откройте диафрагму");
        adv.issues.push_back(is);
    }
    std::stable_sort(adv.issues.begin(), adv.issues.end(),
                     [](const Issue &a, const Issue &b) { return int(a.severity) > int(b.severity); });
    return adv;
}

QVector<Issue> compare(const Advice &cam, const Advice &ref, double *deltaE)
{
    QVector<Issue> out;
    if (cam.stats.area <= 0 || ref.stats.area <= 0 || cam.stats.p98Ire < 3.0 || ref.stats.p98Ire < 3.0)
        return out; // a black picture cannot be compared
    const Lab a{cam.stats.meanL, cam.stats.meanA, cam.stats.meanB};
    const Lab b{ref.stats.meanL, ref.stats.meanA, ref.stats.meanB};
    const double de = deltaE2000(a, b);
    if (deltaE)
        *deltaE = de;
    const QString scope = cam.zone && ref.zone ? QStringLiteral("по зонам")
                                               : QStringLiteral("по всему кадру — точнее с зоной на общем объекте");
    const double dl = cam.stats.medianIre - ref.stats.medianIre;
    if (std::abs(dl) >= 3.0) {
        Issue is;
        is.code = QStringLiteral("match_level");
        is.severity = std::abs(dl) >= 8 ? Severity::Warning : Severity::Info;
        const double stops = stopsBetween(cam.stats.medianIre, ref.stats.medianIre);
        is.text = QStringLiteral("Уровень %1 эталона на %2 IRE").arg(dl > 0 ? QStringLiteral("выше") : QStringLiteral("ниже"),
                                                                      fmt(std::abs(dl), 0));
        is.advice = QStringLiteral("%1 диафрагму примерно на %2 ступ. (или Gain Y)")
                        .arg(stops < 0 ? QStringLiteral("Закройте") : QStringLiteral("Откройте"), fmt(std::abs(stops)));
        out.push_back(is);
    }
    if (cam.stats.neutralPct >= 2.0 && ref.stats.neutralPct >= 2.0) {
        const double da = cam.stats.castA - ref.stats.castA, db = cam.stats.castB - ref.stats.castB;
        if (std::hypot(da, db) >= 3.0) {
            Issue is;
            is.code = QStringLiteral("match_wb");
            is.severity = std::hypot(da, db) >= 8 ? Severity::Warning : Severity::Info;
            is.text = QStringLiteral("Баланс белого отличается от эталона: %1 (Δa* %2, Δb* %3)")
                          .arg(castWords(da, db), fmt(da), fmt(db));
            is.advice = castFix(da, db);
            out.push_back(is);
        }
    }
    Issue is;
    is.code = QStringLiteral("match_de");
    is.severity = de >= 5 ? Severity::Warning : (de >= 2.5 ? Severity::Info : Severity::Ok);
    is.text = QStringLiteral("ΔE2000 к эталону %1 (%2)").arg(fmt(de), scope);
    is.advice = de >= 2.5 ? QStringLiteral("Запустите «Авто до допуска» с этой камерой")
                          : QStringLiteral("Совпадает с эталоном");
    out.push_back(is);
    std::stable_sort(out.begin(), out.end(), [](const Issue &x, const Issue &y) { return int(x.severity) > int(y.severity); });
    return out;
}

void adaptForVmix(QVector<Issue> &issues)
{
    const QString cam = QStringLiteral("В vMix не исправить, крутите камеру: ");
    for (Issue &is : issues) {
        const QString c = is.code;
        if (c == QLatin1String("clip")) {
            is.advice = cam + QStringLiteral("пересвет обрезан уже в камере — закройте диафрагму / снизьте Gain / ND. ")
                        + QStringLiteral("(%1)").arg(is.advice);
        } else if (c == QLatin1String("crush")) {
            is.advice = cam + QStringLiteral("завал чёрного не вернуть — откройте диафрагму / добавьте света. "
                                             "Lift Y в vMix только приподнимет чёрное без деталей");
        } else if (c == QLatin1String("exposure") && !is.advice.startsWith(QLatin1String("Для белого"))
                   && !is.advice.startsWith(QLatin1String("Сравните"))) {
            is.advice = cam + is.advice + QStringLiteral(". Gain Y в vMix — только точная подстройка");
        } else if (c == QLatin1String("noise") || c == QLatin1String("gain") || c == QLatin1String("black")) {
            is.advice = cam + is.advice;
            is.advice.replace(QStringLiteral("на AUX"), QStringLiteral("на выход vMix"));
        } else if (c == QLatin1String("cast") || c == QLatin1String("match_wb")) {
            is.advice += QStringLiteral(". В vMix: Gain R/G/B входа («Применить» / «Авто до допуска»); "
                                        "баланс белого K/tint — только на камере");
        } else if (c == QLatin1String("match_level")) {
            is.advice = QStringLiteral("В vMix: Gain Y входа («Авто до допуска»); при разнице больше ступени — "
                                       "диафрагма на камере (в vMix не исправить без шума/пересвета)");
        }
    }
}

void adaptForVmix(Advice &advice)
{
    adaptForVmix(advice.issues);
}

} // namespace FrameAdvisor
