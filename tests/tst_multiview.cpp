#include "capture/CaptureRoute.h"
#include "capture/MultiviewGeometry.h"

#include <QtTest>

class TestMultiview : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void layoutsExposeExpectedWindowCounts();
    void gridCells();
    void programTopCells();
    void pixelRectStaysInsideFrame();
    void routeSwitchIsExclusive();
};

void TestMultiview::layoutsExposeExpectedWindowCounts()
{
    QCOMPARE(multiviewWindowCount(QStringLiteral("2x2")), 4);
    QCOMPARE(multiviewWindowCount(QStringLiteral("3x3")), 9);
    QCOMPARE(multiviewWindowCount(QStringLiteral("4x4")), 16);
    QCOMPARE(multiviewWindowCount(QStringLiteral("program-top")), 9);
    QCOMPARE(multiviewWindowCount(QStringLiteral("program-right")), 9);
    QCOMPARE(multiviewWindowCount(QStringLiteral("nope")), 0);
    QVERIFY(multiviewLayoutIds().contains(QStringLiteral("2x2")));
}

void TestMultiview::gridCells()
{
    const MultiviewCell topLeft = multiviewCell(QStringLiteral("2x2"), 0);
    QVERIFY(topLeft.valid);
    QCOMPARE(topLeft.x, 0.0);
    QCOMPARE(topLeft.y, 0.0);
    QCOMPARE(topLeft.w, 0.5);
    QCOMPARE(topLeft.h, 0.5);

    const MultiviewCell bottomRight = multiviewCell(QStringLiteral("2x2"), 3);
    QVERIFY(bottomRight.valid);
    QCOMPARE(bottomRight.x, 0.5);
    QCOMPARE(bottomRight.y, 0.5);

    QVERIFY(!multiviewCell(QStringLiteral("2x2"), 4).valid);
    QVERIFY(!multiviewCell(QStringLiteral("4x4"), 16).valid);
    QVERIFY(multiviewCell(QStringLiteral("4x4"), 15).valid);

    const MultiviewCell center = multiviewCell(QStringLiteral("3x3"), 4);
    QVERIFY(center.valid);
    QVERIFY(qAbs(center.x - (1.0 / 3.0)) < 1e-9);
    QVERIFY(qAbs(center.y - (1.0 / 3.0)) < 1e-9);
}

void TestMultiview::programTopCells()
{
    const MultiviewCell large = multiviewCell(QStringLiteral("program-top"), 0);
    QVERIFY(large.valid);
    QCOMPARE(large.x, 0.0);
    QCOMPARE(large.y, 0.0);
    QCOMPARE(large.w, 1.0);
    QCOMPARE(large.h, 0.5);

    const MultiviewCell firstSmall = multiviewCell(QStringLiteral("program-top"), 1);
    QVERIFY(firstSmall.valid);
    QCOMPARE(firstSmall.x, 0.0);
    QCOMPARE(firstSmall.y, 0.5);
    QCOMPARE(firstSmall.w, 0.25);
    QCOMPARE(firstSmall.h, 0.25);

    const MultiviewCell lastSmall = multiviewCell(QStringLiteral("program-top"), 8);
    QVERIFY(lastSmall.valid);
    QCOMPARE(lastSmall.x, 0.75);
    QCOMPARE(lastSmall.y, 0.75);
    QVERIFY(!multiviewCell(QStringLiteral("program-top"), 9).valid);

    const MultiviewCell leftLarge = multiviewCell(QStringLiteral("program-left"), 0);
    QCOMPARE(leftLarge.w, 0.5);
    QCOMPARE(leftLarge.h, 1.0);
    const MultiviewCell rightSmall = multiviewCell(QStringLiteral("program-left"), 2);
    QCOMPARE(rightSmall.x, 0.75);
    QCOMPARE(rightSmall.y, 0.0);
}

void TestMultiview::pixelRectStaysInsideFrame()
{
    const QRect rect = multiviewPixelRect(QStringLiteral("2x2"), 0, 1920, 1080);
    QVERIFY(!rect.isEmpty());
    QVERIFY(QRect(0, 0, 1920, 1080).contains(rect));
    QVERIFY(rect.left() > 0);
    QVERIFY(rect.top() > 0);
    QVERIFY(rect.width() < 960);
    QVERIFY(rect.height() < 540);

    QVERIFY(multiviewPixelRect(QStringLiteral("2x2"), 9, 1920, 1080).isEmpty());
    QVERIFY(multiviewPixelRect(QStringLiteral("missing"), 0, 1920, 1080).isEmpty());
}

void TestMultiview::routeSwitchIsExclusive()
{
    QCOMPARE(captureRouteKind(QStringLiteral("aux")), CaptureRouteKind::Aux);
    QCOMPARE(captureRouteKind(QStringLiteral("multiview")), CaptureRouteKind::Multiview);
    QCOMPARE(captureRouteKind(QStringLiteral("pgm")), CaptureRouteKind::Program);
    QCOMPARE(captureRouteKind(QStringLiteral("")), CaptureRouteKind::Invalid);

    QVERIFY(maySwitchAux(CaptureRouteKind::Aux));
    QVERIFY(!maySwitchAux(CaptureRouteKind::Multiview));
    QVERIFY(!maySwitchAux(CaptureRouteKind::Program));
    QVERIFY(!maySwitchAux(CaptureRouteKind::Invalid));

    QVERIFY(!maySwitchProgram(CaptureRouteKind::Program, false));
    QVERIFY(maySwitchProgram(CaptureRouteKind::Program, true));
    QVERIFY(!maySwitchProgram(CaptureRouteKind::Aux, true));
    QVERIFY(!maySwitchProgram(CaptureRouteKind::Multiview, true));
}

QTEST_MAIN(TestMultiview)
#include "tst_multiview.moc"
