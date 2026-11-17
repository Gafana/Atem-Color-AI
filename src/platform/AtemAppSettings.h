#pragma once

#include <QVariant>

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "scopes/ScopeTypes.h"

class QSettings;

struct CameraBinding
{
    QString label;
    qint64 atemId = 0;
    int multiviewWindow = 0;
    bool includeInTuning = true; ///< «В подстройке» (persisted)
};

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

/** Switcher address stored as settings.ini key atem/ip. */
QString ip();
void setIp(const QString &ip);

QString videoDeviceId();
void setVideoDeviceId(const QString &id);
QString videoInputKey();
void setVideoInputKey(const QString &key);
QString videoModeKey();
void setVideoModeKey(const QString &key);

/** "aux", "multiview" or "pgm". Empty when unset. */
QString captureRoute();
void setCaptureRoute(const QString &route);

qint64 auxOutputId();
void setAuxOutputId(qint64 id);

QString multiviewLayout();
void setMultiviewLayout(const QString &layoutId);

QVector<CameraBinding> cameraMap();
void setCameraMap(const QVector<CameraBinding> &rows);

QString tuningMode();
void setTuningMode(const QString &modeId);

unsigned tuningParamMask();
void setTuningParamMask(unsigned bits);

int grayTargetLuma();
void setGrayTargetLuma(int luma);

QString referenceFramePath();
void setReferenceFramePath(const QString &path);

QString cameraRoiJson();
void setCameraRoiJson(const QString &json);

ScopeLayoutState scopeLayoutState();
void setScopeLayoutState(const ScopeLayoutState &state);

QStringList scopePresetNames();
bool loadScopePreset(const QString &name, ScopeLayoutState *out);
void saveScopePreset(const QString &name, const ScopeLayoutState &state);

QString scopeLayoutPreset();
void setScopeLayoutPreset(const QString &presetId);

QByteArray mainWindowDockState();
void setMainWindowDockState(const QByteArray &state);

void saveDockLayoutPreset(const QString &name, const QHash<QString, QString> &kinds,
                          const QByteArray &windowState);
bool loadDockLayoutPreset(const QString &name, QHash<QString, QString> *kinds, QByteArray *windowState);
QStringList dockLayoutPresetNames();
void removeDockLayoutPreset(const QString &name);

/** Generic access for small UI options ("group/key"). */
QVariant value(const QString &key, const QVariant &defaultValue = QVariant());
void setValue(const QString &key, const QVariant &value);

float qcIreLow();
void setQcIreLow(float ire);
float qcIreHigh();
void setQcIreHigh(float ire);

} // namespace AtemAppSettings