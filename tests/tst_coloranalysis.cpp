#include "color/ColorAnalysis.h"
#include "color/ColorMatching.h"

#include <QImage>
#include <QtTest>

class TestColorAnalysis : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void solidRedHistogramPeak();
    void matchingBrightensDarkFrame();
};

void TestColorAnalysis::solidRedHistogramPeak()
{
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(qRgb(200, 40, 40));
    const FrameColorAnalysis a = ColorAnalysis::analyze(img);
    QCOMPARE_GT(a.histR[200], a.histG[200]);
    QCOMPARE_GT(a.statsR.mean, a.statsG.mean);
    QVERIFY(a.vectorscopeUv[0] >= 0);
}

void TestColorAnalysis::matchingBrightensDarkFrame()
{
    QImage ref(32, 32, QImage::Format_RGB32);
    ref.fill(qRgb(180, 180, 180));
    QImage dark(32, 32, QImage::Format_RGB32);
    dark.fill(qRgb(90, 90, 90));

    const FrameColorAnalysis refA = ColorAnalysis::analyze(ref);
    const FrameColorAnalysis darkA = ColorAnalysis::analyze(dark);
    const CameraMatchResult m = ColorMatching::matchToReference(refA, darkA, QStringLiteral("dark"));
    QVERIFY(m.damped.gainY > 1.0f);
    QVERIFY(m.damped.gainDb >= 0);
}

QTEST_MAIN(TestColorAnalysis)
#include "tst_coloranalysis.moc"