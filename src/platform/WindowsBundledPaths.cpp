#include "WindowsBundledPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

QString moduleDirPath()
{
    wchar_t wpath[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, wpath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    const QString full = QString::fromWCharArray(wpath);
    return QFileInfo(full).absolutePath();
}

void primeBundledGStreamerEnv(const QString &appDir, bool configureRegistry)
{
    if (appDir.isEmpty())
        return;

    // Two bundled layouts:
    //  * installer (NSIS): <prefix>/bin/app.exe + <prefix>/lib/gstreamer-1.0 + <prefix>/libexec/gstreamer-1.0
    //  * local build deploy (deploy-local.cmd): build/app.exe + build/gstreamer-1.0 (+ scanner inside)
    QString prefix;
    QString plugins = appDir + QStringLiteral("/gstreamer-1.0");
    QString scanner = plugins + QStringLiteral("/gst-plugin-scanner.exe");
    if (!QDir(plugins).exists()) {
        QDir prefixDir(appDir);
        if (!prefixDir.cdUp())
            return;
        prefix = prefixDir.absolutePath();
        plugins = prefix + QStringLiteral("/lib/gstreamer-1.0");
        scanner = prefix + QStringLiteral("/libexec/gstreamer-1.0/gst-plugin-scanner.exe");
        if (!QDir(plugins).exists())
            return;
    } else {
        prefix = appDir;
    }

    const QByteArray pluginsUtf8 = QFile::encodeName(QDir::toNativeSeparators(plugins));
    qputenv("GST_PLUGIN_PATH", pluginsUtf8);
    qputenv("GST_PLUGIN_SYSTEM_PATH", pluginsUtf8);

    if (QFileInfo::exists(scanner))
        qputenv("GST_PLUGIN_SCANNER", QFile::encodeName(QDir::toNativeSeparators(scanner)));

    if (!configureRegistry)
        return;

    QString registryDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (registryDir.isEmpty())
        registryDir = prefix;
    QDir().mkpath(registryDir);
    qputenv("GST_REGISTRY",
            QFile::encodeName(QDir::toNativeSeparators(registryDir + QStringLiteral("/gstreamer-registry.bin"))));
}

struct BundledGStreamerEarlyInit
{
    BundledGStreamerEarlyInit() { primeBundledGStreamerEnv(moduleDirPath(), false); }
};

} // namespace

// Before main (and before gst_init): plugin paths for bundled NSIS layout (bin + lib/gstreamer-1.0).
static BundledGStreamerEarlyInit g_bundledGStreamerEarlyInit;

#endif // _WIN32

void applyWindowsBundledRuntimePaths()
{
#ifdef _WIN32
    const QString appDir = QCoreApplication::applicationDirPath();
    primeBundledGStreamerEnv(appDir.isEmpty() ? moduleDirPath() : appDir, true);
#endif
}