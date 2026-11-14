#include "AtemAppSettings.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <functional>

namespace AtemAppSettings {

namespace {

constexpr auto kGroup = "atem";
constexpr auto kSdkPath = "sdkPath";
constexpr auto kSoftwareControlPath = "softwareControlPath";
constexpr auto kFirstRunDone = "firstRunDone";

void withSettings(const std::function<void(QSettings &)> &fn)
{
    const QString iniPath = settingsFilePath();
    QSettings settings(iniPath, QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    fn(settings);
}

} // namespace

QString settingsFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("settings.ini"));
}

void load(QString *sdkPath, QString *softwareControlPath, bool *firstRunDone)
{
    withSettings([&](QSettings &settings) {
        settings.beginGroup(QString::fromLatin1(kGroup));
        if (sdkPath)
            *sdkPath = settings.value(QString::fromLatin1(kSdkPath)).toString();
        if (softwareControlPath)
            *softwareControlPath = settings.value(QString::fromLatin1(kSoftwareControlPath)).toString();
        if (firstRunDone)
            *firstRunDone = settings.value(QString::fromLatin1(kFirstRunDone), false).toBool();
        settings.endGroup();
    });
}

void save(const QString &sdkPath, const QString &softwareControlPath, bool firstRunDone)
{
    withSettings([&](QSettings &settings) {
        settings.beginGroup(QString::fromLatin1(kGroup));
        settings.setValue(QString::fromLatin1(kSdkPath), sdkPath.trimmed());
        settings.setValue(QString::fromLatin1(kSoftwareControlPath), softwareControlPath.trimmed());
        settings.setValue(QString::fromLatin1(kFirstRunDone), firstRunDone);
        settings.endGroup();
        settings.sync();
    });
}

void reset()
{
    const QString path = settingsFilePath();
    QSettings settings(path, QSettings::IniFormat);
    settings.clear();
    settings.sync();
    QFile::remove(path);
}

void applyFromCommandLine(const QString &sdkPath, const QString &softwareControlPath, bool doReset)
{
    if (doReset) {
        reset();
        if (sdkPath.isEmpty() && softwareControlPath.isEmpty())
            return;
    }

    QString curSdk;
    QString curControl;
    bool firstRunDone = false;
    if (!doReset)
        load(&curSdk, &curControl, &firstRunDone);

    if (!sdkPath.isEmpty())
        curSdk = sdkPath;
    if (!softwareControlPath.isEmpty())
        curControl = softwareControlPath;
    if (!sdkPath.isEmpty() || !softwareControlPath.isEmpty())
        firstRunDone = true;

    save(curSdk, curControl, firstRunDone);
}

bool isFirstRunPending()
{
    bool firstRunDone = false;
    load(nullptr, nullptr, &firstRunDone);
    return !firstRunDone;
}

void markFirstRunCompleted()
{
    QString sdk;
    QString control;
    bool done = false;
    load(&sdk, &control, &done);
    Q_UNUSED(done);
    save(sdk, control, true);
}

QString sdkPath()
{
    QString p;
    load(&p, nullptr, nullptr);
    return p;
}

QString softwareControlPath()
{
    QString p;
    load(nullptr, &p, nullptr);
    return p;
}

} // namespace AtemAppSettings