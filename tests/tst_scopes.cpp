#include "scopes/ScopeMath.h"
#include "scopes/ScopeProcessor.h"
#include "scopes/ScopeRender.h"

#include <QImage>
#include <QtTest>

class ScopesTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void waveformVerticalGradient();
    void rgbParadeColorBars();
    void cieInsideGamut();
    void roiPixelRectClampsAndRejects();
    void cropToRoiTakesOnlyZone();
    void dimOutsideRoiKeepsZone();
    void aiTipsKindRoundTrip();
};

static QImage grayGradientBars(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        const int v = (y * 255) / qMax(1, h - 1);
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgb(v, v, v));
    }
    return img;
}

static QImage smpteBars(int w, int h)
{
    const QColor cols[] = {Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue};
    QImage img(w, h, QImage::Format_RGB32);
    const int bw = w / 7;
    for (int i = 0; i < 7; ++i) {
        const QRect r(i * bw, 0, (i == 6) ? w - 6 * bw : bw, h);
        for (int y = r.top(); y <= r.bottom(); ++y)
            for (int x = r.left(); x <= r.right(); ++x)
                img.setPixel(x, y, cols[i].rgb());
    }
    return img;
}

void ScopesTest::waveformVerticalGradient()
{
    const QImage src = grayGradientBars(320, 180);
    ScopeFrameInput in;
    in.plotWidth = 320;
    in.plotHeight = 180;
    const ScopeFrameOutput out = ScopeRender::waveformLuma(src, in);
    QVERIFY(!out.image.isNull());
    int lit = 0;
    for (int y = 0; y < out.image.height(); ++y)
        for (int x = 0; x < out.image.width(); ++x)
            if (qGray(out.image.pixel(x, y)) > 10)
                ++lit;
    QVERIFY(lit > 100);
}

void ScopesTest::rgbParadeColorBars()
{
    const QImage src = smpteBars(350, 200);
    ScopeFrameInput in;
    in.plotWidth = 350;
    in.plotHeight = 200;
    const ScopeFrameOutput out = ScopeRender::rgbParade(src, in);
    QVERIFY(!out.image.isNull());
    QCOMPARE(out.image.width(), in.plotWidth);
}

void ScopesTest::cieInsideGamut()
{
    const QImage src = smpteBars(200, 120);
    ScopeFrameInput in;
    in.plotWidth = 240;
    in.plotHeight = 240;
    in.frameA = src;
    const ScopeFrameOutput out = ScopeProcessor::render(ScopeKind::Cie1931, in);
    QVERIFY(!out.image.isNull());
}

// QPainter text rendering (IRE labels) needs a QGuiApplication; APPLESS crashed (0xC0000409).
void ScopesTest::roiPixelRectClampsAndRejects()
{
    const QSize sz(200, 100);
    QCOMPARE(ScopeMath::roiPixelRect(sz, QRectF()), QRect());
    QCOMPARE(ScopeMath::roiPixelRect(sz, QRectF(0.25, 0.5, 0.5, 0.5)), QRect(50, 50, 100, 50));
    // partly outside -> clipped to the frame
    QCOMPARE(ScopeMath::roiPixelRect(sz, QRectF(0.9, -0.2, 0.5, 0.5)), QRect(180, 0, 20, 30));
    // smaller than 2x2 pixels -> no zone (whole frame)
    QCOMPARE(ScopeMath::roiPixelRect(sz, QRectF(0.5, 0.5, 0.001, 0.001)), QRect());
}

void ScopesTest::cropToRoiTakesOnlyZone()
{
    // left half red, right half blue: the zone on the right gives a pure blue picture
    QImage img(100, 50, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, x < 50 ? qRgb(200, 0, 0) : qRgb(0, 0, 200));
    const QImage z = ScopeMath::cropToRoi(img, QRectF(0.6, 0.1, 0.3, 0.8));
    QCOMPARE(z.size(), QSize(30, 40));
    for (int y = 0; y < z.height(); ++y)
        for (int x = 0; x < z.width(); ++x)
            QCOMPARE(z.pixel(x, y), qRgb(0, 0, 200));
    QCOMPARE(ScopeMath::cropToRoi(img, QRectF()).size(), img.size()); // no zone -> whole frame
    // the scope of the zone differs from the whole-frame scope (red trace gone)
    ScopeFrameInput in;
    in.plotWidth = 256;
    in.plotHeight = 256;
    in.frameA = img;
    const QImage full = ScopeProcessor::render(ScopeKind::RgbParade, in).image;
    in.frameA = z;
    const QImage zone = ScopeProcessor::render(ScopeKind::RgbParade, in).image;
    QVERIFY(!full.isNull() && !zone.isNull());
    QVERIFY(full != zone);
}

void ScopesTest::dimOutsideRoiKeepsZone()
{
    QImage img(40, 20, QImage::Format_RGB32);
    img.fill(qRgb(200, 200, 200));
    ScopeMath::dimOutsideRoi(img, QRectF(0.5, 0.0, 0.5, 1.0), 0.25);
    QCOMPARE(img.pixel(30, 10), qRgb(200, 200, 200));
    QVERIFY(qRed(img.pixel(5, 10)) <= 51);
    QImage same(40, 20, QImage::Format_RGB32);
    same.fill(qRgb(200, 200, 200));
    ScopeMath::dimOutsideRoi(same, QRectF(), 0.25); // no zone: untouched
    QCOMPARE(same.pixel(5, 10), qRgb(200, 200, 200));
}

void ScopesTest::aiTipsKindRoundTrip()
{
    QCOMPARE(scopeKindFromId(scopeKindId(ScopeKind::AiTips)), ScopeKind::AiTips);
    QVERIFY(allScopeKinds().contains(ScopeKind::AiTips));
    QCOMPARE(scopeKindLabel(ScopeKind::AiTips), QStringLiteral("ИИ-подсказки"));
}

QTEST_MAIN(ScopesTest)
#include "tst_scopes.moc"