#include "atem/AtemClient.h"
#include "color/ColorAnalysis.h"
#include "color/ColorMatching.h"
#include "platform/AtemAppSettings.h"
#include "platform/AtemPathValidation.h"
#include "platform/AtemRuntime.h"
#include "platform/AtemSetup.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#ifdef ENABLE_GUI
#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include "gui/AtemPathsSetupDialog.h"
#include "gui/MainWindow.h"
#endif

#ifdef _WIN32
#include "platform/WindowsBundledPaths.h"
#endif

#ifdef HAVE_GSTREAMER
#include "capture/GstFrameCapture.h"
#endif

Q_LOGGING_CATEGORY(lc, "atem")

static void logInputs(const QVector<AtemInputInfo> &inputs)
{
    qCInfo(lc) << "Inputs:" << inputs.size();
    for (const AtemInputInfo &in : inputs) {
        qCInfo(lc).noquote()
            << QStringLiteral("  id=%1 short=\"%2\" long=\"%3\" port=%4 cameraControl=%5")
                   .arg(in.id)
                   .arg(in.shortName)
                   .arg(in.longName)
                   .arg(in.portType)
                   .arg(in.cameraControlSupported ? "yes" : "no");
    }
}

static int runAnalyzeCli(const QStringList &paths, bool jsonOutput, float damping)
{
    if (paths.size() < 2) {
        qCCritical(lc) << "analyze requires at least reference.png + one candidate";
        return 2;
    }

    QImage refImage;
    const QString refLoad = ColorAnalysis::loadImage(paths.first(), refImage);
    if (!refLoad.isEmpty()) {
        qCCritical(lc) << refLoad;
        return 3;
    }
    const FrameColorAnalysis refAnalysis = ColorAnalysis::analyze(refImage);

    QVector<QPair<QString, FrameColorAnalysis>> candidates;
    for (int i = 1; i < paths.size(); ++i) {
        QImage img;
        const QString err = ColorAnalysis::loadImage(paths[i], img);
        if (!err.isEmpty()) {
            qCCritical(lc) << err;
            return 3;
        }
        candidates.push_back({paths[i], ColorAnalysis::analyze(img)});
    }

    ColorMatchOptions opt;
    opt.damping = damping;
    const QVector<CameraMatchResult> results = ColorMatching::matchAll(refAnalysis, candidates, opt);

    if (jsonOutput) {
        QJsonObject root;
        root.insert(QStringLiteral("reference"), paths.first());
        QJsonArray cams;
        for (const CameraMatchResult &r : results) {
            QJsonObject o;
            o.insert(QStringLiteral("source"), r.label);
            o.insert(QStringLiteral("metrics"),
                     QJsonObject{{QStringLiteral("deltaLuma"), r.metrics.meanDeltaLuma},
                                 {QStringLiteral("histogramL1Rgb"), r.metrics.histogramL1Rgb}});
            o.insert(QStringLiteral("correction"), r.damped.toJson());
            cams.append(o);
        }
        root.insert(QStringLiteral("cameras"), cams);
        const QByteArray out = QJsonDocument(root).toJson(QJsonDocument::Indented);
        fprintf(stdout, "%s\n", out.constData());
    } else {
        printf("Reference: %s\n", qPrintable(paths.first()));
        for (const CameraMatchResult &r : results) {
            printf("\n%s\n", qPrintable(r.label));
            printf("  deltaLuma=%.2f  L1rgb=%.4f  gainY=%.3f  WB=%d tint=%d  gainDb=%d\n",
                   r.metrics.meanDeltaLuma, r.metrics.histogramL1Rgb, r.damped.gainY,
                   r.damped.whiteBalanceKelvin, r.damped.whiteBalanceTint, r.damped.gainDb);
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
#ifdef ENABLE_GUI
    QApplication app(argc, argv);
#else
    QCoreApplication app(argc, argv);
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("Gafana"));
    QCoreApplication::setApplicationName(QStringLiteral("atem-color-ai"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

#ifdef _WIN32
    applyWindowsBundledRuntimePaths();
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ATEM camera color analysis / matching and optional GStreamer capture"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption ipOption(
        QStringList{QStringLiteral("i"), QStringLiteral("ip")},
        QStringLiteral("ATEM switcher IP address (default: 192.168.10.240)"),
        QStringLiteral("address"),
        QStringLiteral("192.168.10.240"));
    parser.addOption(ipOption);

    QCommandLineOption listOnlyOption(
        QStringLiteral("list-only"),
        QStringLiteral("Only list ATEM inputs and exit"));
    parser.addOption(listOnlyOption);

    QCommandLineOption captureSourceOption(
        QStringLiteral("capture"),
        QStringLiteral("Video source URI for single-frame PNG capture"),
        QStringLiteral("uri"));
    parser.addOption(captureSourceOption);

    QCommandLineOption outputOption(
        QStringLiteral("output"),
        QStringLiteral("Output PNG path for capture (default: frame.png)"),
        QStringLiteral("path"),
        QStringLiteral("frame.png"));
    parser.addOption(outputOption);

    QCommandLineOption analyzeOption(
        QStringLiteral("analyze"),
        QStringLiteral("Analyze PNGs (positional): first = reference, rest = candidates"));
    parser.addOption(analyzeOption);
    parser.addPositionalArgument(QStringLiteral("png"),
                                 QStringLiteral("PNG paths used with --analyze"));

    QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("JSON output for --analyze"));
    parser.addOption(jsonOption);

    QCommandLineOption dampingOption(
        QStringLiteral("damping"),
        QStringLiteral("Partial correction factor 0..1 (default 0.35)"),
        QStringLiteral("factor"),
        QStringLiteral("0.35"));
    parser.addOption(dampingOption);

    QCommandLineOption sdkPathOption(
        QStringLiteral("sdk-path"),
        QStringLiteral("Path to Blackmagic ATEM SDK folder (saved to user settings)"),
        QStringLiteral("dir"));
    parser.addOption(sdkPathOption);

    QCommandLineOption atemControlPathOption(
        QStringLiteral("atem-control-path"),
        QStringLiteral("Path to ATEM Software Control install folder or .exe (saved to settings)"),
        QStringLiteral("path"));
    parser.addOption(atemControlPathOption);

    QCommandLineOption resetSettingsOption(
        QStringLiteral("reset-settings"), QStringLiteral("Delete saved ATEM path settings"));
    parser.addOption(resetSettingsOption);

#ifdef ENABLE_GUI
    QCommandLineOption guiOption(QStringLiteral("gui"), QStringLiteral("Open Qt Widgets UI"));
    parser.addOption(guiOption);
#endif

    parser.process(app);

    AtemAppSettings::applyFromCommandLine(parser.value(sdkPathOption),
                                          parser.value(atemControlPathOption),
                                          parser.isSet(resetSettingsOption));

    auto warnMissingAtemRuntime = [&]() {
#ifdef HAVE_ATEM_SDK
        if (AtemRuntime::isBlackmagicAtemRuntimeAvailable())
            return;
        const QString text = AtemRuntime::missingRuntimeUserMessage();
        qCWarning(lc).noquote() << text;
#else
        Q_UNUSED(parser);
#endif
    };

    AtemSetup::logConfigurationWarnings();

#ifdef ENABLE_GUI
    if (parser.isSet(guiOption)) {
        warnMissingAtemRuntime();

        if (AtemAppSettings::isFirstRunPending()) {
            AtemPathsSetupDialog setup;
            setup.setSoftwareControlPath(AtemAppSettings::softwareControlPath());
            setup.applyAutoDetection();
            const int dlgResult = setup.exec();
            if (dlgResult == QDialog::Accepted) {
                const QString control =
                    AtemPathValidation::normalizeSoftwareControlPath(setup.softwareControlPath());
                AtemAppSettings::save(AtemAppSettings::sdkPath(), control, true);
            } else {
                AtemAppSettings::markFirstRunCompleted();
            }
        }

        auto client = createAtemClient();
        const QString ip = parser.value(ipOption);
        const QString connectError = client->connectTo(ip);
        if (!connectError.isEmpty())
            qCWarning(lc) << "ATEM connect failed (UI still opens):" << connectError;

        MainWindow window(std::move(client));
        window.show();
        return app.exec();
    }
#endif

    if (parser.isSet(analyzeOption)) {
        const QStringList paths = parser.positionalArguments();
        const float damping = parser.value(dampingOption).toFloat();
        return runAnalyzeCli(paths, parser.isSet(jsonOption), damping);
    }

    const QString ip = parser.value(ipOption);
    warnMissingAtemRuntime();
    auto client = createAtemClient();
    qCInfo(lc) << "Backend:" << client->backendName();
    qCInfo(lc) << "Connecting to ATEM at" << ip << "...";

    const QString connectError = client->connectTo(ip);
    if (!connectError.isEmpty()) {
        qCCritical(lc) << "Connection failed:" << connectError;
        return 1;
    }

    qCInfo(lc) << "Connected.";
    logInputs(client->listInputs());
    client->disconnect();

    if (parser.isSet(listOnlyOption) && !parser.isSet(captureSourceOption))
        return 0;

    if (parser.isSet(captureSourceOption)) {
#ifndef HAVE_GSTREAMER
        qCCritical(lc) << "GStreamer support was not built";
        return 2;
#else
        const QString source = parser.value(captureSourceOption);
        const QString outPath = parser.value(outputOption);
        const QString capErr = GstFrameCapture::capturePng(source, outPath);
        if (!capErr.isEmpty()) {
            qCCritical(lc) << "Capture failed:" << capErr;
            return 3;
        }
        qCInfo(lc) << "Saved" << outPath;
#endif
    }

    return 0;
}