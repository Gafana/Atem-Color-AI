#include "WindowsBundledPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

void applyWindowsBundledRuntimePaths()
{
#ifdef _WIN32
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString gstRoot = appDir + QStringLiteral("/gstreamer");
    const QDir gstDir(gstRoot);
    if (!gstDir.exists())
        return;

    const QString gstBin = gstRoot + QStringLiteral("/bin");
    const QString gstPlugins = gstRoot + QStringLiteral("/lib/gstreamer-1.0");

    if (QFileInfo::exists(gstPlugins))
        qputenv("GST_PLUGIN_PATH", QFile::encodeName(gstPlugins));

    if (QFileInfo::exists(gstBin)) {
        const QByteArray path = qgetenv("PATH");
        const QByteArray prefix = QFile::encodeName(gstBin + QLatin1Char(';'));
        if (!path.contains(gstBin.toUtf8()))
            qputenv("PATH", prefix + path);
    }

    const QString gstLibexec = gstRoot + QStringLiteral("/libexec/gstreamer-1.0");
    if (QFileInfo::exists(gstLibexec))
        qputenv("GST_PLUGIN_SCANNER", QFile::encodeName(gstLibexec + QStringLiteral("/gst-plugin-scanner.exe")));

    qputenv("GST_REGISTRY", QFile::encodeName(appDir + QStringLiteral("/gstreamer-registry.bin")));
#endif
}