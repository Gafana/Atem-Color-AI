#include "color/ColorAnalysis.h"
#include "color/ColorTuning.h"

#include <QImage>
#include <QtTest>

class TestColorTuning : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void grayCardBalancesWarmPatch();
    void neutralBalanceOnWarmFrame();
    void averageBlendsTwoFrames();
};

void TestColorTuning::grayCardBalancesWarmPatch()
{
    QImage warm(48, 48, QImage::Format_RGB32);
    warm.fill(qRgb(140, 118, 110));
    const FrameColorAnalysis roi = ColorAnalysis::analyze(warm);
    GrayCardTuningOptions opts;
    opts.matchExposure = false;
    const CameraCorrectionBundle corr = ColorTuning::correctionFromGrayCard(roi, opts);
    QVERIFY(corr.gainB > corr.gainR);
    QVERIFY(corr.whiteBalanceKelvin >= 5600);
}

void TestColorTuning::neutralBalanceOnWarmFrame()
{
    QImage warm(64, 64, QImage::Format_RGB32);
    warm.fill(qRgb(188, 202, 214));
    NeutralBalanceOptions opts;
    opts.neutralChromaMax = 32;
    const CameraCorrectionBundle corr = ColorAnalysis::neutralBalanceCorrection(warm, opts);
    QVERIFY(corr.whiteBalanceKelvin > 5600);
}

void TestColorTuning::averageBlendsTwoFrames()
{
    QImage dark(16, 16, QImage::Format_RGB32);
    dark.fill(qRgb(40, 40, 40));
    QImage bright(16, 16, QImage::Format_RGB32);
    bright.fill(qRgb(200, 200, 200));
    const FrameColorAnalysis mean =
        ColorTuning::averageAnalyses({ColorAnalysis::analyze(dark), ColorAnalysis::analyze(bright)});
    QVERIFY(mean.statsR.mean > 40.0);
    QVERIFY(mean.statsR.mean < 200.0);
}

QTEST_MAIN(TestColorTuning)
#include "tst_colortuning.moc"