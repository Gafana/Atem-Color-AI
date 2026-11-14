#include "atem/AtemClient.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QLoggingCategory>

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

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("atem-color-ai"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ATEM camera listing and optional GStreamer frame capture"));
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
        QStringLiteral("Video source URI for single-frame PNG capture "
                       "(srt://..., ndi:Name, decklink:N, v4l2:/dev/video0, videotestsrc)"),
        QStringLiteral("uri"));
    parser.addOption(captureSourceOption);

    QCommandLineOption outputOption(
        QStringLiteral("output"),
        QStringLiteral("Output PNG path for capture (default: frame.png)"),
        QStringLiteral("path"),
        QStringLiteral("frame.png"));
    parser.addOption(outputOption);

    parser.process(app);

    const QString ip = parser.value(ipOption);
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

    if (parser.isSet(captureSourceOption)) {
#ifndef HAVE_GSTREAMER
        qCCritical(lc) << "GStreamer support was not built (install dev packages, reconfigure with "
                          "-DENABLE_GSTREAMER=ON)";
        return 2;
#else
        const QString source = parser.value(captureSourceOption);
        const QString outPath = parser.value(outputOption);
        qCInfo(lc) << "Capturing one frame from" << source << "->" << outPath;
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