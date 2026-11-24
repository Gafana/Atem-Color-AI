#include "atem/CameraControlProtocol.h"
#include "color/CorrectionParameterMask.h"
#include "control/VmixClient.h"
#include "control/VmixProtocol.h"
#include "color/FrameAdvisor.h"
#include "platform/AtemAppSettings.h"
#include "platform/CameraPresetStore.h"
#include "support/FakeVmixServer.h"

#include <QFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QtTest>

using namespace VmixProtocol;

class VmixTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void functionQueryEncodes();
    void ccCommandsOnlyChangedAndMasked();
    void ccCommandsClampToDocumentedRanges();
    void parseDocumentedXmlExample();
    void parseCcWhenPresent();
    void parseAddressDefaults();
    void routeQueries();
    void clientAppliesCcThroughHttp();
    void clientRoutesOutputsAndRestores();
    void clientCameraOnlyActions();
    void clientReadsCcFromXmlWhenExposed();
    void clientCachePersists();
    void clientReportsUnreachable();
    void settingsPerBackend();
    void presetsPerBackend();
    void aiTipsForVmix();
};

void VmixTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(AtemAppSettings::settingsFilePath());
    QFile::remove(CameraPresetStore::filePath());
}

void VmixTest::functionQueryEncodes()
{
    QCOMPARE(functionQuery(QStringLiteral("SetCCGainR"), QStringLiteral("2"), QStringLiteral("1.05")),
             QStringLiteral("Function=SetCCGainR&Input=2&Value=1.05"));
    QCOMPARE(functionQuery(QStringLiteral("PreviewInput"), QStringLiteral("CAM 1")),
             QStringLiteral("Function=PreviewInput&Input=CAM%201"));
    QCOMPARE(functionQuery(QStringLiteral("SetOutput2"), QString(), QStringLiteral("Output")),
             QStringLiteral("Function=SetOutput2&Value=Output"));
}

void VmixTest::ccCommandsOnlyChangedAndMasked()
{
    CameraCorrectionBundle a, b;
    QVERIFY(ccCommands(a, b).isEmpty());
    b.gainR = 1.1f;
    b.liftB = -0.05f;
    b.saturation = 1.2f; // vMix 0.2
    b.hue = 0.1f;
    b.whiteBalanceKelvin = 3200; // not a vMix parameter: ignored
    b.gainDb = 18;
    QVector<Command> c = ccCommands(a, b);
    QStringList s;
    for (const Command &x : c)
        s << x.function + QLatin1Char('=') + x.value;
    QCOMPARE(s, QStringList({QStringLiteral("SetCCLiftB=-0.05"), QStringLiteral("SetCCGainR=1.1"),
                             QStringLiteral("SetCCSaturation=0.2"), QStringLiteral("SetCCHue=0.1")}));
    CorrectionParameterMask m = CorrectionParameterMask::allEnabled();
    m.lift = false;
    m.hue = false;
    c = ccCommands(a, b, &m);
    QCOMPARE(c.size(), 2);
    QCOMPARE(c.at(0).function, QStringLiteral("SetCCGainR"));
    QCOMPARE(c.at(1).function, QStringLiteral("SetCCSaturation"));
}

void VmixTest::ccCommandsClampToDocumentedRanges()
{
    CameraCorrectionBundle a, b;
    b.gainG = 3.5f;   // vMix gain 0..2
    b.gammaR = -2.f;  // vMix gamma -1..1
    b.saturation = 0.f; // greyscale = -1
    b.liftY = 0.25f;
    b.gainY = 0.8f;
    QStringList s;
    for (const Command &x : ccCommands(a, b))
        s << x.function + QLatin1Char('=') + x.value;
    QCOMPARE(s, QStringList({QStringLiteral("SetCCLiftY=0.25"), QStringLiteral("SetCCGammaR=-1"),
                             QStringLiteral("SetCCGainG=2"), QStringLiteral("SetCCGainY=0.8"),
                             QStringLiteral("SetCCSaturation=-1")}));
}

void VmixTest::parseDocumentedXmlExample()
{
    // verbatim structure of the example in https://www.vmix.com/help28/DeveloperAPI.html
    const QByteArray xml =
        "<vmix> <version>11.0.0.16</version> <inputs> <input key=\"26cae087-b7b6-4d45-98e4-de03ab4feb6b\" "
        "number=\"1\" type=\"Xaml\" title=\"NewsHD.xaml\" state=\"Paused\" position=\"0\" duration=\"0\" "
        "muted=\"True\" loop=\"False\" selectedIndex=\"0\"> NewsHD.xaml <text index=\"0\" name=\"Headline\">Hello</text> "
        "<text index=\"1\" name=\"Description\">Hello</text> </input> <input key=\"55cbe357-a801-4d54-8ff2-08ee68766fae\" "
        "number=\"2\" type=\"VirtualSet\" title=\"LateNightNews\" state=\"Paused\" position=\"0\" duration=\"0\" "
        "muted=\"True\" loop=\"False\" selectedIndex=\"0\"> LateNightNews <overlay index=\"0\" "
        "key=\"2fe8ff9d-e400-4504-85ab-df7c17a1edd4\"/> <overlay index=\"1\" key=\"20e4ee9a-05cc-4f58-bb69-cd179e1c1958\"/> "
        "</input> </inputs> <overlays> <overlay number=\"1\"/> <overlay number=\"2\">1</overlay> </overlays> "
        "<preview>1</preview> <active>2</active> <fadeToBlack>False</fadeToBlack> <transitions> <transition "
        "number=\"1\" effect=\"Fade\" duration=\"500\"/> </transitions> <recording>False</recording> "
        "<external>False</external> <streaming>False</streaming> <playList>False</playList> "
        "<multiCorder>False</multiCorder> </vmix>";
    const State st = parseState(xml);
    QVERIFY(st.ok);
    QCOMPARE(st.version, QStringLiteral("11.0.0.16"));
    QCOMPARE(st.inputs.size(), 2);
    QCOMPARE(st.inputs.at(0).number, 1);
    QCOMPARE(st.inputs.at(0).title, QStringLiteral("NewsHD.xaml"));
    QCOMPARE(st.inputs.at(1).key, QStringLiteral("55cbe357-a801-4d54-8ff2-08ee68766fae"));
    QCOMPARE(st.inputs.at(1).type, QStringLiteral("VirtualSet"));
    QCOMPARE(st.preview, 1);
    QCOMPARE(st.active, 2);
    QVERIFY(!st.inputs.at(0).hasCc && !st.inputs.at(1).hasCc);
    QVERIFY(!parseState("<html>nope</html>").ok);
    QVERIFY(!parseState("garbage").ok);
}

void VmixTest::parseCcWhenPresent()
{
    const QByteArray xml = "<vmix><version>28</version><inputs>"
                           "<input key=\"k1\" number=\"1\" type=\"Capture\" title=\"CAM1\">CAM1"
                           "<cc LiftR=\"0.1\" GammaB=\"-0.2\" GainY=\"1.3\" Saturation=\"0.5\" Hue=\"-0.1\"/></input>"
                           "<input key=\"k2\" number=\"2\" type=\"Capture\" title=\"CAM2\">CAM2"
                           "<colourCorrection><gainR>1.2</gainR><liftG>-0.05</liftG></colourCorrection></input>"
                           "</inputs><preview>2</preview><active>1</active></vmix>";
    const State st = parseState(xml);
    QCOMPARE(st.inputs.size(), 2);
    QVERIFY(st.inputs.at(0).hasCc);
    QCOMPARE(st.inputs.at(0).cc.liftR, 0.1f);
    QCOMPARE(st.inputs.at(0).cc.gammaB, -0.2f);
    QCOMPARE(st.inputs.at(0).cc.gainY, 1.3f);
    QCOMPARE(st.inputs.at(0).cc.saturation, 1.5f); // vMix 0.5 -> bundle 1.5
    QCOMPARE(st.inputs.at(0).cc.hue, -0.1f);
    QVERIFY(st.inputs.at(1).hasCc);
    QCOMPARE(st.inputs.at(1).cc.gainR, 1.2f);
    QCOMPARE(st.inputs.at(1).cc.liftG, -0.05f);
    QCOMPARE(st.preview, 2);
}

void VmixTest::parseAddressDefaults()
{
    QCOMPARE(parseAddress(QString()), qMakePair(QStringLiteral("localhost"), 8088));
    QCOMPARE(parseAddress(QStringLiteral("localhost")), qMakePair(QStringLiteral("localhost"), 8088));
    QCOMPARE(parseAddress(QStringLiteral("192.168.2.50:8090")), qMakePair(QStringLiteral("192.168.2.50"), 8090));
    QCOMPARE(parseAddress(QStringLiteral("http://pc-vmix:8088/api")), qMakePair(QStringLiteral("pc-vmix"), 8088));
}

void VmixTest::routeQueries()
{
    QCOMPARE(routeQuery(2, 3), QStringLiteral("Function=SetOutput2&Value=Input&Input=3"));
    QCOMPARE(routeQuery(102, kSourceProgram), QStringLiteral("Function=SetOutputExternal2&Value=Output"));
    QCOMPARE(routeQuery(201, kSourcePreview), QStringLiteral("Function=SetOutputFullscreen&Value=Preview"));
    QString err;
    QVERIFY(routeQuery(77, 1, &err).isEmpty());
    QVERIFY(!err.isEmpty());
}

void VmixTest::clientAppliesCcThroughHttp()
{
    FakeVmixServer srv;
    const quint16 port = srv.startAndWait();
    QVERIFY(port != 0);
    VmixClient c;
    QCOMPARE(c.connectTo(QStringLiteral("127.0.0.1:%1").arg(port)), QString());
    QVERIFY(c.isConnected());
    QCOMPARE(c.backendName(), QStringLiteral("vMix 28.0.0.42"));
    const QVector<AtemInputInfo> ins = c.listInputs();
    QCOMPARE(ins.size(), 3);
    QCOMPARE(ins.at(1).id, qint64(2));
    QCOMPARE(ins.at(1).longName, QStringLiteral("CAM2"));
    QVERIFY(ins.at(1).cameraControlSupported);

    CameraCorrectionBundle want;
    want.gainR = 1.08f;
    want.gainB = 0.93f;
    want.gammaY = 0.1f;
    want.saturation = 0.9f;
    want.whiteBalanceKelvin = 3200; // must not produce any call
    want.irisFStopAv = 4.0f;
    CameraCorrectionBundle prev;
    QCOMPARE(c.applyCorrectionBundle(2, want, &prev), QString());
    QCOMPARE(srv.calls(), QStringList({QStringLiteral("Function=SetCCGammaY&Input=2&Value=0.1"),
                                       QStringLiteral("Function=SetCCGainR&Input=2&Value=1.08"),
                                       QStringLiteral("Function=SetCCGainB&Input=2&Value=0.93"),
                                       QStringLiteral("Function=SetCCSaturation&Input=2&Value=-0.1")}));
    QCOMPARE(prev.gainR, 1.0f);
    QCOMPARE(srv.cc(2, QStringLiteral("GainR")), 1.08);
    QCOMPARE(srv.cc(2, QStringLiteral("Saturation")), -0.1);
    QCOMPARE(srv.cc(1, QStringLiteral("GainR")), 1.0); // other input untouched

    // same values again: nothing to send; read-back comes from the cache (XML has no CC)
    srv.clearCalls();
    QCOMPARE(c.applyCorrectionBundle(2, want), QString());
    QVERIFY(srv.calls().isEmpty());
    CameraCorrectionBundle now;
    QCOMPARE(c.readCorrectionBundle(2, now), QString());
    QVERIFY(!c.ccFromXml(2));
    QCOMPARE(now.gainR, 1.08f);
    QCOMPARE(now.saturation, 0.9f);
    QCOMPARE(now.whiteBalanceKelvin, CameraCorrectionBundle().whiteBalanceKelvin);

    // rollback = back to the values before
    QCOMPARE(c.rollbackCorrection(2, prev), QString());
    QCOMPARE(srv.cc(2, QStringLiteral("GainR")), 1.0);
    QCOMPARE(srv.cc(2, QStringLiteral("GammaY")), 0.0);
    QCOMPARE(srv.cc(2, QStringLiteral("Saturation")), 0.0);
    QCOMPARE(c.readCorrectionBundle(9, now).isEmpty(), false); // no such input
}

void VmixTest::clientRoutesOutputsAndRestores()
{
    FakeVmixServer srv;
    const quint16 port = srv.startAndWait();
    VmixClient c;
    QCOMPARE(c.connectTo(QStringLiteral("localhost:%1").arg(port)), QString());
    QVERIFY(c.listAuxOutputs().size() >= 4);
    qint64 src = 0;
    QCOMPARE(c.auxSource(2, &src), QString());
    QCOMPARE(src, kSourceProgram);
    QCOMPARE(c.setAuxSource(2, 1), QString());
    QCOMPARE(srv.output(QStringLiteral("SetOutput2")), QStringLiteral("Input:1"));
    QCOMPARE(c.auxSource(2, &src), QString());
    QCOMPARE(src, qint64(1));
    QCOMPARE(c.setAuxSource(2, kSourceProgram), QString());
    QCOMPARE(srv.output(QStringLiteral("SetOutput2")), QStringLiteral("Output"));
    QVERIFY(!c.setAuxSource(2, 42).isEmpty()); // fake answers 500 for an unknown input
    qint64 pgm = 0;
    QCOMPARE(c.programInput(&pgm), QString());
    QCOMPARE(pgm, qint64(1));
    QCOMPARE(c.setProgramInput(2), QString());
    QCOMPARE(c.programInput(&pgm), QString());
    QCOMPARE(pgm, qint64(2));
}

void VmixTest::clientCameraOnlyActions()
{
    FakeVmixServer srv;
    const quint16 port = srv.startAndWait();
    VmixClient c;
    QCOMPARE(c.connectTo(QStringLiteral("localhost:%1").arg(port)), QString());
    QVERIFY(!c.setCameraGainDb(1, 12).isEmpty());
    QVERIFY(!c.setCameraFloats(1, 0, 3, {2.8f}).isEmpty());
    QVERIFY(!c.triggerCameraAction(1, BmdCameraProtocol::kCategoryLens, BmdCameraProtocol::kLensAutoAperture).isEmpty());
    QCOMPARE(c.triggerCameraAction(1, BmdCameraProtocol::kCategoryVideo, BmdCameraProtocol::kVideoAutoWhiteBalance),
             QString());
    QCOMPARE(srv.calls().last(), QStringLiteral("Function=ColourCorrectionAuto&Input=1"));
    QCOMPARE(srv.cc(1, QStringLiteral("GainB")), 0.95); // the fake's "auto" changed something
    // rollback after auto: the values vMix set are unknown, so every CC value is sent explicitly
    srv.clearCalls();
    QCOMPARE(c.rollbackCorrection(1, CameraCorrectionBundle()), QString());
    QCOMPARE(srv.calls().size(), 14);
    QVERIFY(srv.calls().contains(QStringLiteral("Function=SetCCGainB&Input=1&Value=1")));
    QCOMPARE(srv.cc(1, QStringLiteral("GainB")), 1.0);
    // values known again: an identical apply sends nothing
    srv.clearCalls();
    QCOMPARE(c.applyCorrectionBundle(1, CameraCorrectionBundle()), QString());
    QVERIFY(srv.calls().isEmpty());
}

void VmixTest::clientReadsCcFromXmlWhenExposed()
{
    FakeVmixServer srv(0, true);
    const quint16 port = srv.startAndWait();
    VmixClient c;
    QCOMPARE(c.connectTo(QStringLiteral("localhost:%1").arg(port)), QString());
    CameraCorrectionBundle b;
    b.liftR = 0.05f;
    QCOMPARE(c.applyCorrectionBundle(1, b), QString());
    VmixClient fresh; // no cache: the value must come from the XML
    QCOMPARE(fresh.connectTo(QStringLiteral("localhost:%1").arg(port)), QString());
    CameraCorrectionBundle r;
    QCOMPARE(fresh.readCorrectionBundle(1, r), QString());
    QVERIFY(fresh.ccFromXml(1));
    QCOMPARE(r.liftR, 0.05f);
}

void VmixTest::clientCachePersists()
{
    FakeVmixServer srv;
    const quint16 port = srv.startAndWait();
    QByteArray store;
    {
        VmixClient c([&] { return store; }, [&](const QByteArray &d) { store = d; });
        QCOMPARE(c.connectTo(QStringLiteral("localhost:%1").arg(port)), QString());
        CameraCorrectionBundle b;
        b.gainG = 1.2f;
        QCOMPARE(c.applyCorrectionBundle(2, b), QString());
    }
    QVERIFY(store.contains("55cbe357-a801-4d54-8ff2-08ee68766fae")); // keyed by the input GUID
    VmixClient again([&] { return store; }, [&](const QByteArray &d) { store = d; });
    QCOMPARE(again.connectTo(QStringLiteral("localhost:%1").arg(port)), QString());
    CameraCorrectionBundle r;
    QCOMPARE(again.readCorrectionBundle(2, r), QString());
    QCOMPARE(r.gainG, 1.2f);
}

void VmixTest::clientReportsUnreachable()
{
    VmixClient c({}, {}, 300);
    QTcpServer probe; // find a free port, then close it
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();
    const QString err = c.connectTo(QStringLiteral("127.0.0.1:%1").arg(port));
    QVERIFY(!err.isEmpty());
    QVERIFY(!c.isConnected());
    QVERIFY(!c.applyCorrectionBundle(1, CameraCorrectionBundle()).isEmpty());
}

void VmixTest::settingsPerBackend()
{
    AtemAppSettings::setControlBackend(QStringLiteral("atem"));
    QCOMPARE(AtemAppSettings::controlBackend(), QStringLiteral("atem"));
    CameraBinding a;
    a.label = QStringLiteral("CAM ATEM");
    a.atemId = 1;
    AtemAppSettings::setCameraMap({a});
    AtemAppSettings::setAuxOutputId(8001);

    AtemAppSettings::setControlBackend(QStringLiteral("vmix"));
    QVERIFY(AtemAppSettings::isVmixBackend());
    QCOMPARE(AtemAppSettings::vmixAddress(), QStringLiteral("localhost:8088"));
    QVERIFY(AtemAppSettings::cameraMap().isEmpty());
    QCOMPARE(AtemAppSettings::auxOutputId(), qint64(0));
    CameraBinding v;
    v.label = QStringLiteral("CAM vMix");
    v.atemId = 2;
    AtemAppSettings::setCameraMap({v});
    AtemAppSettings::setAuxOutputId(2);
    AtemAppSettings::setVmixAddress(QStringLiteral("192.168.1.20:8088"));
    QCOMPARE(AtemAppSettings::cameraMap().value(0).label, QStringLiteral("CAM vMix"));

    AtemAppSettings::setControlBackend(QStringLiteral("atem"));
    QCOMPARE(AtemAppSettings::cameraMap().size(), 1);
    QCOMPARE(AtemAppSettings::cameraMap().value(0).label, QStringLiteral("CAM ATEM"));
    QCOMPARE(AtemAppSettings::auxOutputId(), qint64(8001));
    AtemAppSettings::setControlBackend(QStringLiteral("vmix"));
    QCOMPARE(AtemAppSettings::auxOutputId(), qint64(2));
    QCOMPARE(AtemAppSettings::vmixAddress(), QStringLiteral("192.168.1.20:8088"));
    AtemAppSettings::setControlBackend(QStringLiteral("nonsense"));
    QCOMPARE(AtemAppSettings::controlBackend(), QStringLiteral("atem"));
}

void VmixTest::presetsPerBackend()
{
    QFile::remove(CameraPresetStore::filePath());
    AtemAppSettings::setControlBackend(QStringLiteral("atem"));
    CameraPreset p;
    p.name = QStringLiteral("День");
    p.saved = QDateTime::currentDateTime();
    CameraPresetEntry e;
    e.atemId = 1;
    e.label = QStringLiteral("CAM1");
    e.bundle.whiteBalanceKelvin = 4300;
    p.cameras = {e};
    QCOMPARE(CameraPresetStore::save(p), QString());

    AtemAppSettings::setControlBackend(QStringLiteral("vmix"));
    QVERIFY(CameraPresetStore::names().isEmpty()); // ATEM preset is not offered in vMix mode
    QVERIFY(!CameraPresetStore::find(QStringLiteral("День"), nullptr));
    e.bundle = CameraCorrectionBundle();
    e.bundle.gainR = 1.1f;
    p.cameras = {e};
    QCOMPARE(CameraPresetStore::save(p), QString()); // same name, other backend: both kept
    CameraPreset got;
    QVERIFY(CameraPresetStore::find(QStringLiteral("День"), &got));
    QCOMPARE(got.backend, QStringLiteral("vmix"));
    QCOMPARE(got.cameras.value(0).bundle.gainR, 1.1f);
    QCOMPARE(CameraPresetStore::loadAllBackends().size(), 2);
    QCOMPARE(CameraPresetStore::rename(QStringLiteral("День"), QStringLiteral("Вечер")), QString());

    AtemAppSettings::setControlBackend(QStringLiteral("atem"));
    QCOMPARE(CameraPresetStore::names(), QStringList({QStringLiteral("День")}));
    QVERIFY(CameraPresetStore::find(QStringLiteral("День"), &got));
    QCOMPARE(got.backend, QStringLiteral("atem"));
    QCOMPARE(got.cameras.value(0).bundle.whiteBalanceKelvin, 4300);
    QCOMPARE(CameraPresetStore::remove(QStringLiteral("День")), QString());
    QVERIFY(CameraPresetStore::names().isEmpty());
    AtemAppSettings::setControlBackend(QStringLiteral("vmix"));
    QCOMPARE(CameraPresetStore::names(), QStringList({QStringLiteral("Вечер")}));

    // a file from an older version (no "backend" field) holds ATEM presets
    QFile f(CameraPresetStore::filePath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("{\"presets\":[{\"name\":\"Old\",\"cameras\":[]}]}");
    f.close();
    QVERIFY(CameraPresetStore::names().isEmpty());
    AtemAppSettings::setControlBackend(QStringLiteral("atem"));
    QCOMPARE(CameraPresetStore::names(), QStringList({QStringLiteral("Old")}));
}

void VmixTest::aiTipsForVmix()
{
    QVector<FrameAdvisor::Issue> issues(5);
    issues[0].code = QStringLiteral("clip");
    issues[0].advice = QStringLiteral("Закройте диафрагму");
    issues[1].code = QStringLiteral("noise");
    issues[1].advice = QStringLiteral("Снизьте Gain");
    issues[2].code = QStringLiteral("cast");
    issues[2].advice = QStringLiteral("увеличьте K");
    issues[3].code = QStringLiteral("oversat");
    issues[3].advice = QStringLiteral("Уменьшите Saturation");
    issues[4].code = QStringLiteral("exposure");
    issues[4].advice = QStringLiteral("Кадр тёмный: откройте диафрагму");
    FrameAdvisor::adaptForVmix(issues);
    QVERIFY(issues[0].advice.startsWith(QStringLiteral("В vMix не исправить, крутите камеру")));
    QVERIFY(issues[1].advice.startsWith(QStringLiteral("В vMix не исправить, крутите камеру")));
    QVERIFY(issues[2].advice.contains(QStringLiteral("Gain R/G/B")));
    QCOMPARE(issues[3].advice, QStringLiteral("Уменьшите Saturation")); // vMix can do this one
    QVERIFY(issues[4].advice.startsWith(QStringLiteral("В vMix не исправить, крутите камеру")));
}

QTEST_MAIN(VmixTest)
#include "tst_vmix.moc"
