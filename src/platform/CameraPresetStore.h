#pragma once

#include "atem/CameraCorrection.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

/** Camera presets ("День", "Вечер"): parameters of all cameras, stored in AppData as JSON. */
struct CameraPresetEntry
{
    qint64 atemId = 0;
    QString label;
    CameraCorrectionBundle bundle;
};

struct CameraPreset
{
    QString name;
    /** "atem" or "vmix": presets are only listed / applied for the backend they were saved with
     *  (old files without the field are ATEM presets). Empty on save = current backend. */
    QString backend;
    QDateTime saved;
    QVector<CameraPresetEntry> cameras;
};

namespace CameraPresetStore {

QString filePath();
/** Presets of the current control backend (AtemAppSettings::controlBackend()). */
QVector<CameraPreset> loadAll();
/** Every stored preset, all backends. */
QVector<CameraPreset> loadAllBackends();
QStringList names();
bool find(const QString &name, CameraPreset *out);
/** Insert or replace by name (within the current backend). Returns empty string on success. */
QString save(const CameraPreset &preset);
QString remove(const QString &name);
QString rename(const QString &from, const QString &to);

} // namespace CameraPresetStore
