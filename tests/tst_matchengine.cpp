// Synthetic end-to-end tests of the matching engine: a simulated Blackmagic-like camera renders a
// scene, the engine measures the picture and proposes new camera values, the simulated camera is
// updated and the picture is measured again. A correct engine must move the picture toward the target.
#include "color/ColorAnalysis.h"
#include "color/ColorCharts.h"
#include "color/ColorScience.h"
#include "color/MatchEngine.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

#include <cmath>

using namespace ColorScience;
using MatchEngine::Pair;

namespace {

struct Scene
{
    QVector<Rgb> patchesLinear; ///< reflectances under a neutral D65-balanced reference
    QVector<int> neutral;
};

Scene chartScene()
{
    Scene s;
    const ChartPreset cc = ColorCharts::byId(QStringLiteral("cc24"));
    for (const ChartPatchRef &p : cc.patches)
        s.patchesLinear << labD50ToLinear709({p.L, p.a, p.b});
    s.neutral = cc.neutral;
    return s;
}

/** Simulated camera: sensor WB (linear, Planckian), exposure, BT.709 OETF, then the primary grade. */
struct SimCamera
{
    double lightKelvin = 5600.0; ///< colour of the light on set
    double exposureStops = 0.0;  ///< scene over/under exposure
    CameraCorrectionBundle state; ///< what the camera is set to

    Rgb shoot(const Rgb &reflectance) const
    {
        const Rgb light = planckianWhite(lightKelvin);
        const Rgb wb = planckianWhite(state.whiteBalanceKelvin);
        const double green = std::exp(-0.006 * state.whiteBalanceTint);
        const double expo = std::pow(2.0, exposureStops + state.gainDb / 6.02);
        Rgb lin{reflectance.r * light.r / wb.r * expo, reflectance.g * green * expo,
                reflectance.b * light.b / wb.b * expo};
        Rgb e = encode(lin, Transfer::Bt709Oetf);
        const MatchEngine::ChannelCurve cr{state.gainR, state.liftR, state.gammaR};
        const MatchEngine::ChannelCurve cg{state.gainG, state.liftG, state.gammaG};
        const MatchEngine::ChannelCurve cb{state.gainB, state.liftB, state.gammaB};
        Rgb y{cr.apply(e.r), cg.apply(e.g), cb.apply(e.b)};
        const double l = luma(y);
        y = {l + state.saturation * (y.r - l), l + state.saturation * (y.g - l), l + state.saturation * (y.b - l)};
        for (int c = 0; c < 3; ++c)
            y[c] = std::min(1.0, std::max(0.0, y[c]));
        return y;
    }

    QImage render(const Scene &scene, int w = 360, int h = 240) const
    {
        QImage img(w, h, QImage::Format_RGB32);
        const int cols = 6, rows = 4;
        for (int y = 0; y < h; ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int idx = (y * rows / h) * cols + (x * cols / w);
                const Rgb c = shoot(scene.patchesLinear[idx]);
                row[x] = qRgb(qRound(c.r * 255), qRound(c.g * 255), qRound(c.b * 255));
            }
        }
        return img;
    }
};

QVector<Pair> chartPairs(const SimCamera &cand, const SimCamera &target, const Scene &scene)
{
    QVector<Pair> pairs;
    for (int i = 0; i < scene.patchesLinear.size(); ++i) {
        Pair p;
        p.measured = cand.shoot(scene.patchesLinear[i]);
        p.target = target.shoot(scene.patchesLinear[i]);
        p.neutral = scene.neutral.contains(i);
        pairs << p;
    }
    return pairs;
}

double neutralError(const SimCamera &cam, const Scene &scene)
{
    double worst = 0.0;
    for (int i : scene.neutral) {
        const Rgb c = cam.shoot(scene.patchesLinear[i]);
        if (std::max({c.r, c.g, c.b}) > 0.98)
            continue;
        const double m = (c.r + c.g + c.b) / 3.0;
        worst = std::max({worst, std::fabs(c.r - m) * 100, std::fabs(c.g - m) * 100, std::fabs(c.b - m) * 100});
    }
    return worst;
}

} // namespace

class TestMatchEngine : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void deltaE2000Reference();
    void planckianIsMonotonic();
    void referenceCameraRecoversInverseGain();
    void chartRecoversLiftGammaGain();
    void whiteBalanceSignAndConvergence();
    void modelMismatchConvergesIteratively();
    void autoModeNeutralizesCast();
    void grayCardRoiDirection();
    void sanityWarningsFlagLargeChanges();
    void chartSamplingDetectsRotation();
    void composeIsRelativeToCurrentState();
    void autoRejectsBlackFrame();
    void autoLimitsColouredStageLight();
    void autoLimitsExposureStepPerPass();
};

void TestMatchEngine::deltaE2000Reference()
{
    // Sharma et al. 2005, test pair 1
    const double de = deltaE2000({50.0, 2.6772, -79.7751}, {50.0, 0.0, -82.7485});
    QVERIFY2(std::fabs(de - 2.0425) < 1e-3, qPrintable(QString::number(de)));
    const double de2 = deltaE2000({50.0, 2.5, 0.0}, {73.0, 25.0, -18.0});
    QVERIFY2(std::fabs(de2 - 27.1492) < 1e-3, qPrintable(QString::number(de2)));
}

void TestMatchEngine::planckianIsMonotonic()
{
    const Rgb warm = planckianWhite(3200);
    const Rgb d65 = planckianWhite(6500);
    const Rgb cool = planckianWhite(9000);
    QVERIFY(warm.r > d65.r && d65.r > cool.r);
    QVERIFY(warm.b < d65.b && d65.b < cool.b);
    QVERIFY(std::fabs(d65.r - 1.0) < 0.06 && std::fabs(d65.b - 1.0) < 0.12);
    QVERIFY(std::fabs(estimateKelvin(planckianWhite(4300)) - 4300) < 30);
}

void TestMatchEngine::referenceCameraRecoversInverseGain()
{
    const Scene scene = chartScene();
    SimCamera ref;
    SimCamera cand;
    cand.state.gainR = 1.12f;
    cand.state.gainB = 0.86f;
    const FrameColorAnalysis a = ColorAnalysis::analyze(cand.render(scene));
    const FrameColorAnalysis b = ColorAnalysis::analyze(ref.render(scene));
    MatchEngine::Settings st;
    const MatchEngine::Delta d = MatchEngine::solve(MatchEngine::quantilePairs(a, b), st);
    QVERIFY(d.valid);
    QVERIFY(d.rmsAfterIre < d.rmsBeforeIre);
    const CameraCorrectionBundle next = MatchEngine::compose(cand.state, d, st);
    QVERIFY2(std::fabs(next.gainR - 1.0f) < 0.04f, qPrintable(QString::number(next.gainR)));
    QVERIFY2(std::fabs(next.gainG - 1.0f) < 0.04f, qPrintable(QString::number(next.gainG)));
    QVERIFY2(std::fabs(next.gainB - 1.0f) < 0.04f, qPrintable(QString::number(next.gainB)));
}

void TestMatchEngine::chartRecoversLiftGammaGain()
{
    const Scene scene = chartScene();
    SimCamera ref;
    SimCamera cand;
    cand.state.liftR = 0.03f;
    cand.state.liftB = -0.02f;
    cand.state.gammaR = 0.10f;
    cand.state.gammaB = -0.08f;
    cand.state.gainR = 1.05f;
    cand.state.gainB = 0.95f;
    MatchEngine::Settings st;
    st.lift = true;
    st.gamma = true;
    const MatchEngine::Delta d = MatchEngine::solve(chartPairs(cand, ref, scene), st);
    QVERIFY(d.valid);
    SimCamera fixed = cand;
    fixed.state = MatchEngine::compose(cand.state, d, st);
    const MatchEngine::Delta after = MatchEngine::solve(chartPairs(fixed, ref, scene), st);
    QVERIFY2(after.deBefore < 1.0, qPrintable(QStringLiteral("dE after %1 (before %2)").arg(after.deBefore).arg(d.deBefore)));
    QVERIFY(after.deBefore < d.deBefore * 0.25);
}

void TestMatchEngine::whiteBalanceSignAndConvergence()
{
    const Scene scene = chartScene();
    SimCamera cam;
    cam.lightKelvin = 3200; // tungsten light, camera still at 5600 -> picture too warm
    MatchEngine::Settings st;
    st.balanceVia = MatchEngine::BalanceVia::WhiteBalance;
    st.exposure = false;
    const double before = neutralError(cam, scene);
    for (int pass = 0; pass < 3; ++pass) {
        QVector<Pair> pairs;
        for (int i : scene.neutral) {
            Pair p;
            p.measured = cam.shoot(scene.patchesLinear[i]);
            const double y = luma(p.measured);
            p.target = {y, y, y};
            p.neutral = true;
            pairs << p;
        }
        const MatchEngine::Delta d = MatchEngine::solve(pairs, st);
        const int oldK = cam.state.whiteBalanceKelvin;
        cam.state = MatchEngine::compose(cam.state, d, st);
        if (pass == 0)
            QVERIFY2(cam.state.whiteBalanceKelvin < oldK, "warm picture must lower the camera Kelvin");
    }
    QVERIFY2(std::abs(cam.state.whiteBalanceKelvin - 3200) <= 200,
             qPrintable(QString::number(cam.state.whiteBalanceKelvin)));
    QVERIFY2(neutralError(cam, scene) < 1.5, qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(neutralError(cam, scene))));
}

void TestMatchEngine::modelMismatchConvergesIteratively()
{
    // Camera error is physical (wrong light colour + overexposure, applied in linear light) but we
    // correct with CC gain in the encoded domain: one pass is approximate, a second pass converges.
    const Scene scene = chartScene();
    SimCamera ref;
    SimCamera cam;
    cam.lightKelvin = 4200;
    cam.exposureStops = 0.6;
    MatchEngine::Settings st;
    st.lift = true;
    st.gamma = true;
    double de0 = -1, de1 = -1;
    for (int pass = 0; pass < 3; ++pass) {
        const MatchEngine::Delta d = MatchEngine::solve(chartPairs(cam, ref, scene), st);
        if (pass == 0)
            de0 = d.deBefore;
        cam.state = MatchEngine::compose(cam.state, d, st);
    }
    de1 = MatchEngine::solve(chartPairs(cam, ref, scene), st).deBefore;
    QVERIFY2(de1 < 2.0 && de1 < de0 * 0.3, qPrintable(QStringLiteral("dE %1 -> %2").arg(de0).arg(de1)));
}

void TestMatchEngine::autoModeNeutralizesCast()
{
    const Scene scene = chartScene();
    SimCamera cam;
    cam.lightKelvin = 4300;
    MatchEngine::Settings st;
    st.exposure = false;
    const double before = neutralError(cam, scene);
    for (int pass = 0; pass < 2; ++pass) {
        const QImage frame = cam.render(scene);
        const QVector<Pair> pairs = MatchEngine::autoPairs(frame, st, 41.0);
        QVERIFY(!pairs.isEmpty());
        cam.state = MatchEngine::compose(cam.state, MatchEngine::solve(pairs, st), st);
    }
    const double after = neutralError(cam, scene);
    QVERIFY2(after < before * 0.4 && after < 3.0, qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(after)));
}

void TestMatchEngine::grayCardRoiDirection()
{
    QImage warm(64, 64, QImage::Format_RGB32);
    warm.fill(qRgb(140, 118, 100));
    const Rgb m = MatchEngine::regionMean(warm);
    Pair p;
    p.measured = m;
    const double y = luma(m);
    p.target = {y, y, y};
    p.neutral = true;
    MatchEngine::Settings st;
    st.exposure = false;
    const CameraCorrectionBundle out = MatchEngine::compose(CameraCorrectionBundle{}, MatchEngine::solve({p}, st), st);
    QVERIFY(out.gainR < 1.0f);
    QVERIFY(out.gainB > 1.0f);
    st.balanceVia = MatchEngine::BalanceVia::WhiteBalance;
    const CameraCorrectionBundle wb = MatchEngine::compose(CameraCorrectionBundle{}, MatchEngine::solve({p}, st), st);
    QVERIFY2(wb.whiteBalanceKelvin < 5600, "warm card -> lower Kelvin");
}

void TestMatchEngine::sanityWarningsFlagLargeChanges()
{
    CameraCorrectionBundle cur;
    CameraCorrectionBundle p = cur;
    QVERIFY(MatchEngine::sanityWarnings(cur, p).isEmpty());
    p.gainR = 2.0f;
    p.whiteBalanceKelvin = 3000;
    QCOMPARE(MatchEngine::sanityWarnings(cur, p).size(), 2);
}

void TestMatchEngine::chartSamplingDetectsRotation()
{
    const ChartPreset cc = ColorCharts::byId(QStringLiteral("cc24"));
    const QVector<Rgb> ref = ColorCharts::referenceEncoded(cc, Transfer::Bt709Oetf);
    QImage img(640, 480, QImage::Format_RGB32);
    img.fill(Qt::black);
    const QRect area(80, 90, 480, 320); // 6x4 patches of 80x80
    QPainter painter(&img);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 6; ++c) {
            const Rgb v = ref[r * 6 + c];
            painter.fillRect(area.left() + c * 80 + 4, area.top() + r * 80 + 4, 72, 72,
                             QColor::fromRgbF(v.r, v.g, v.b));
        }
    painter.end();
    // The image is upside down relative to how the user marked it.
    const QImage rotated = img.transformed(QTransform().rotate(180));
    const QVector<QPointF> corners = {QPointF(80, 70), QPointF(560, 70), QPointF(560, 390), QPointF(80, 390)};
    int rot = -1;
    const QVector<ChartPatchSample> s = ColorCharts::sample(rotated, corners, cc, Transfer::Bt709Oetf, &rot);
    QCOMPARE(s.size(), 24);
    QCOMPARE(rot, 2);
    for (const ChartPatchSample &p : s)
        QVERIFY2(std::fabs(p.measured.g - p.reference.g) < 0.01, qPrintable(p.name));
}

void TestMatchEngine::composeIsRelativeToCurrentState()
{
    CameraCorrectionBundle cur;
    cur.gainR = 1.3f;
    cur.gammaG = 0.2f;
    MatchEngine::Delta d;
    d.valid = true; // identity delta
    MatchEngine::Settings st;
    st.lift = st.gamma = true;
    const CameraCorrectionBundle out = MatchEngine::compose(cur, d, st);
    QCOMPARE(out.gainR, 1.3f);
    QCOMPARE(out.gammaG, 0.2f);
}

void TestMatchEngine::autoRejectsBlackFrame()
{
    QImage black(320, 180, QImage::Format_RGB32);
    black.fill(qRgb(2, 2, 3));
    QStringList notes;
    const QVector<Pair> pairs = MatchEngine::autoPairs(black, MatchEngine::Settings(), 41.0, nullptr, &notes);
    QVERIFY(pairs.isEmpty());
    QVERIFY(!notes.isEmpty());
}

void TestMatchEngine::autoLimitsColouredStageLight()
{
    // Saturated blue/magenta stage light, no neutral surfaces: must not ask for extreme gains.
    QImage img(320, 180, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, (x / 40) % 2 ? qRgb(60, 30, 200) : qRgb(150, 20, 170));
    MatchEngine::Settings st;
    st.exposure = false;
    QStringList notes;
    const QVector<Pair> pairs = MatchEngine::autoPairs(img, st, 41.0, nullptr, &notes);
    QVERIFY(!pairs.isEmpty());
    const CameraCorrectionBundle out = MatchEngine::compose(CameraCorrectionBundle(), MatchEngine::solve(pairs, st), st);
    for (float g : {out.gainR, out.gainG, out.gainB})
        QVERIFY2(g > 0.45f && g < 2.2f, qPrintable(QStringLiteral("gain %1").arg(g)));
}

void TestMatchEngine::autoLimitsExposureStepPerPass()
{
    // Very dark but valid neutral scene (~8 % code): one pass must not jump more than ±1 stop.
    QImage img(320, 180, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const int v = 14 + ((x / 20 + y / 20) % 4) * 6;
            img.setPixel(x, y, qRgb(v, v, v));
        }
    MatchEngine::Settings st;
    st.exposure = true;
    QStringList notes;
    const QVector<Pair> pairs = MatchEngine::autoPairs(img, st, 41.0, nullptr, &notes);
    QVERIFY(!pairs.isEmpty());
    const CameraCorrectionBundle out = MatchEngine::compose(CameraCorrectionBundle(), MatchEngine::solve(pairs, st), st);
    qInfo() << "dark-scene gains" << out.gainR << out.gainG << out.gainB << notes;
    for (float g : {out.gainR, out.gainG, out.gainB})
        QVERIFY2(g > 1.0f && g < 2.05f, qPrintable(QStringLiteral("gain %1").arg(g)));
    QVERIFY(notes.join(' ').contains(QStringLiteral("±1 ступенью")));
}

QTEST_MAIN(TestMatchEngine)
#include "tst_matchengine.moc"
