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
    QDateTime saved;
    QVector<CameraPresetEntry> cameras;
};

namespace CameraPresetStore {

QString filePath();
QVector<CameraPreset> loadAll();
QStringList names();
bool find(const QString &name, CameraPreset *out);
/** Insert or replace by name. Returns empty string on success. */
QString save(const CameraPreset &preset);
QString remove(const QString &name);
QString rename(const QString &from, const QString &to);

} // namespace CameraPresetStore
