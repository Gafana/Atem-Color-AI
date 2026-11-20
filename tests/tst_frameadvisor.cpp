#include "color/FrameAdvisor.h"

#include <QImage>
#include <QRandomGenerator>
#include <QtTest>

using namespace FrameAdvisor;

class FrameAdvisorTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanFrameHasNoWarnings();
    void clippedHighlightsFoundWithRegion();
    void crushedBlacksFound();
    void warmCastSuggestsLowerKelvin();
    void zoneIgnoresProblemsOutside();
    void noiseMeasured();
    void oversaturationFound();
    void skinToneOffLine();
    void compareFindsLevelAndCast();
    void blackFrameIsOneProblem();
};

namespace {

QImage gradient(int w = 320, int h = 180)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int v = 50 + (x * 150) / w; // 20..78 IRE, neutral
            img.setPixel(x, y, qRgb(v, v, v));
        }
    return img;
}

void fill(QImage &img, const QRect &r, QRgb c)
{
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x)
            img.setPixel(x, y, c);
}

const Issue *find(const QVector<Issue> &v, const QString &code)
{
    for (const Issue &i : v)
        if (i.code == code)
            return &i;
    return nullptr;
}

} // namespace

void FrameAdvisorTest::cleanFrameHasNoWarnings()
{
    const Advice a = analyze(gradient());
    for (const Issue &i : a.issues)
        QVERIFY2(i.severity < Severity::Warning, qPrintable(i.text));
    QCOMPARE(a.verdict(), QStringLiteral("норма"));
    QVERIFY(a.stats.clipAnyPct < 0.01);
    QVERIFY(std::abs(a.stats.castA) < 1.0 && std::abs(a.stats.castB) < 1.0);
}

void FrameAdvisorTest::clippedHighlightsFoundWithRegion()
{
    QImage img = gradient();
    fill(img, QRect(240, 0, 80, 45), qRgb(255, 255, 255)); // top-right corner, 6.25 %
    const Advice a = analyze(img);
    const Issue *c = find(a.issues, QStringLiteral("clip"));
    QVERIFY(c);
    QCOMPARE(c->severity, Severity::Critical);
    QVERIFY(a.stats.clipAnyPct > 5.5 && a.stats.clipAnyPct < 7.0);
    QVERIFY(!c->regions.isEmpty());
    const QRectF r = c->regions.first();
    QVERIFY(r.left() > 0.7 && r.top() < 0.05 && r.bottom() < 0.35);
    QVERIFY(c->advice.contains(QStringLiteral("Закройте")));
    // only red clipped -> channel-specific advice
    QImage red = gradient();
    fill(red, QRect(0, 0, 60, 60), qRgb(255, 120, 100));
    const Issue *rc = find(analyze(red).issues, QStringLiteral("clip"));
    QVERIFY(rc);
    QVERIFY(rc->advice.contains(QStringLiteral("канал R")));
}

void FrameAdvisorTest::crushedBlacksFound()
{
    QImage img = gradient();
    fill(img, QRect(0, 120, 320, 60), qRgb(0, 0, 0)); // bottom third
    const Advice a = analyze(img);
    const Issue *c = find(a.issues, QStringLiteral("crush"));
    QVERIFY(c);
    QCOMPARE(c->severity, Severity::Critical);
    QVERIFY(c->regions.first().top() > 0.6);
}

void FrameAdvisorTest::warmCastSuggestsLowerKelvin()
{
    QImage img(320, 180, QImage::Format_RGB32);
    img.fill(qRgb(140, 128, 112));
    const Advice a = analyze(img);
    QVERIFY(a.stats.castB > 4.0);
    const Issue *c = find(a.issues, QStringLiteral("cast"));
    QVERIFY(c);
    QVERIFY(c->text.contains(QStringLiteral("тёплый")));
    QVERIFY(c->advice.contains(QStringLiteral("уменьшите цветовую температуру")));
}

void FrameAdvisorTest::zoneIgnoresProblemsOutside()
{
    QImage img = gradient();
    fill(img, QRect(0, 0, 100, 180), qRgb(255, 255, 255)); // clipped left part
    const QRectF zone(0.5, 0.25, 0.4, 0.5);
    const Advice a = analyze(img, zone);
    QVERIFY(a.zone);
    QVERIFY(!find(a.issues, QStringLiteral("clip")));
    // a problem inside the zone is reported in whole-frame coordinates, inside the zone
    fill(img, QRect(250, 60, 30, 30), qRgb(255, 255, 255));
    const Advice b = analyze(img, zone);
    const Issue *c = find(b.issues, QStringLiteral("clip"));
    QVERIFY(c);
    const QRectF r = c->regions.first();
    QVERIFY(zone.adjusted(-0.01, -0.01, 0.01, 0.01).contains(r));
    QVERIFY(r.left() > 0.7);
}

void FrameAdvisorTest::noiseMeasured()
{
    QImage img(320, 180, QImage::Format_RGB32);
    QRandomGenerator rng(7);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            // ~uniform noise +-14 codes -> std ~8 codes ~3.2 IRE
            const int v = qBound(0, 120 + int(rng.bounded(29)) - 14, 255);
            img.setPixel(x, y, qRgb(v, v, v));
        }
    Options o;
    o.gainDb = 24;
    const Advice a = analyze(img, {}, o);
    QVERIFY2(a.stats.noiseIre > 2.5 && a.stats.noiseIre < 4.0, qPrintable(QString::number(a.stats.noiseIre)));
    const Issue *n = find(a.issues, QStringLiteral("noise"));
    QVERIFY(n);
    QVERIFY(n->advice.contains(QStringLiteral("24 dB")));
    QVERIFY(analyze(gradient()).stats.noiseIre < 1.0);
}

void FrameAdvisorTest::oversaturationFound()
{
    QImage img = gradient();
    fill(img, QRect(0, 0, 160, 90), qRgb(255, 0, 0));
    const Advice a = analyze(img);
    const Issue *o = find(a.issues, QStringLiteral("oversat"));
    QVERIFY(o);
    QVERIFY(a.stats.oversatPct > 20);
    QVERIFY(o->regions.first().right() <= 0.55);
}

void FrameAdvisorTest::skinToneOffLine()
{
    QImage ok = gradient();
    fill(ok, QRect(100, 40, 120, 100), qRgb(220, 170, 140)); // on the skin line (~128°)
    QVERIFY(!find(analyze(ok).issues, QStringLiteral("skin")));
    QImage yellow = gradient();
    fill(yellow, QRect(100, 40, 120, 100), qRgb(215, 175, 120)); // ~145°: towards yellow
    const Advice a = analyze(yellow);
    const Issue *s = find(a.issues, QStringLiteral("skin"));
    QVERIFY(s);
    QVERIFY(s->text.contains(QStringLiteral("жёлтый")));
    QVERIFY(a.stats.skinHueDeg > 135 && a.stats.skinHueDeg < 155);
}

void FrameAdvisorTest::compareFindsLevelAndCast()
{
    QImage ref(320, 180, QImage::Format_RGB32);
    ref.fill(qRgb(120, 120, 120));
    QImage cam(320, 180, QImage::Format_RGB32);
    cam.fill(qRgb(150, 146, 128)); // brighter and warmer
    double de = 0;
    const QVector<Issue> v = compare(analyze(cam), analyze(ref), &de);
    QVERIFY2(de > 3, qPrintable(QString::number(de)));
    const Issue *l = find(v, QStringLiteral("match_level"));
    QVERIFY(l);
    QVERIFY(l->advice.contains(QStringLiteral("Закройте")));
    QVERIFY(find(v, QStringLiteral("match_wb")));
    const Issue *d = find(v, QStringLiteral("match_de"));
    QVERIFY(d && d->severity >= Severity::Info);
    double same = 1;
    const QVector<Issue> s = compare(analyze(ref), analyze(ref), &same);
    QVERIFY(same < 0.01);
    QCOMPARE(s.size(), 1);
    QCOMPARE(s.first().severity, Severity::Ok);
}

void FrameAdvisorTest::blackFrameIsOneProblem()
{
    QImage img(320, 180, QImage::Format_RGB32);
    img.fill(qRgb(0, 0, 0));
    const Advice a = analyze(img);
    QCOMPARE(a.issues.size(), 1);
    QCOMPARE(a.issues.first().code, QStringLiteral("black"));
    QCOMPARE(a.issues.first().severity, Severity::Critical);
    QImage gray(320, 180, QImage::Format_RGB32);
    gray.fill(qRgb(120, 120, 120));
    QVERIFY(compare(a, analyze(gray)).isEmpty()); // nothing to compare against a black picture
}

QTEST_MAIN(FrameAdvisorTest)
#include "tst_frameadvisor.moc"
