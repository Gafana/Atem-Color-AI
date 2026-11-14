#pragma once

#include <QString>

class QSettings;

namespace AtemAppSettings {

QString settingsFilePath();

void load(QString *sdkPath, QString *softwareControlPath, bool *firstRunDone);
void save(const QString &sdkPath, const QString &softwareControlPath, bool firstRunDone);
void reset();

void applyFromCommandLine(const QString &sdkPath, const QString &softwareControlPath, bool reset);

bool isFirstRunPending();
void markFirstRunCompleted();

QString sdkPath();
QString softwareControlPath();

} // namespace AtemAppSettings