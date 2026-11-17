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
QTEST_MAIN(ScopesTest)
#include "tst_scopes.moc"