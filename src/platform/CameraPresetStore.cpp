#include "CameraPresetStore.h"

#include "AtemAppSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace CameraPresetStore {

namespace {

QString writeAll(const QVector<CameraPreset> &all)
{
    QJsonArray arr;
    for (const CameraPreset &p : all) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), p.name);
        o.insert(QStringLiteral("backend"), p.backend.isEmpty() ? QStringLiteral("atem") : p.backend);
        o.insert(QStringLiteral("saved"), p.saved.toString(Qt::ISODate));
        QJsonArray cams;
        for (const CameraPresetEntry &e : p.cameras) {
            QJsonObject c;
            c.insert(QStringLiteral("atemId"), QString::number(e.atemId));
            c.insert(QStringLiteral("label"), e.label);
            c.insert(QStringLiteral("values"), e.bundle.toJson());
            cams.append(c);
        }
        o.insert(QStringLiteral("cameras"), cams);
        arr.append(o);
    }
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly))
        return QStringLiteral("Не удалось записать %1").arg(filePath());
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("presets"), arr}}).toJson(QJsonDocument::Indented));
    if (!f.commit())
        return QStringLiteral("Не удалось сохранить %1").arg(filePath());
    return {};
}

} // namespace

QString filePath()
{
    return QFileInfo(AtemAppSettings::settingsFilePath()).absolutePath() + QStringLiteral("/camera-presets.json");
}

namespace {
QString current()
{
    return AtemAppSettings::controlBackend();
}
} // namespace

QVector<CameraPreset> loadAll()
{
    QVector<CameraPreset> out;
    for (const CameraPreset &p : loadAllBackends())
        if (p.backend == current())
            out.push_back(p);
    return out;
}

QVector<CameraPreset> loadAllBackends()
{
    QVector<CameraPreset> out;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("presets")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        CameraPreset p;
        p.name = o.value(QStringLiteral("name")).toString();
        p.backend = o.value(QStringLiteral("backend")).toString(QStringLiteral("atem"));
        if (p.backend.isEmpty())
            p.backend = QStringLiteral("atem");
        p.saved = QDateTime::fromString(o.value(QStringLiteral("saved")).toString(), Qt::ISODate);
        for (const QJsonValue &cv : o.value(QStringLiteral("cameras")).toArray()) {
            const QJsonObject c = cv.toObject();
            CameraPresetEntry e;
            e.atemId = c.value(QStringLiteral("atemId")).toString().toLongLong();
            e.label = c.value(QStringLiteral("label")).toString();
            e.bundle = CameraCorrectionBundle::fromJson(c.value(QStringLiteral("values")).toObject());
            p.cameras.push_back(e);
        }
        if (!p.name.isEmpty())
            out.push_back(p);
    }
    return out;
}

QStringList names()
{
    QStringList n;
    for (const CameraPreset &p : loadAll())
        n << p.name;
    return n;
}

bool find(const QString &name, CameraPreset *out)
{
    for (const CameraPreset &p : loadAll()) {
        if (p.name == name) {
            if (out)
                *out = p;
            return true;
        }
    }
    return false;
}

QString save(const CameraPreset &preset)
{
    if (preset.name.trimmed().isEmpty())
        return QStringLiteral("Пустое имя пресета");
    CameraPreset stored = preset;
    if (stored.backend.isEmpty())
        stored.backend = current();
    QVector<CameraPreset> all = loadAllBackends();
    bool replaced = false;
    for (CameraPreset &p : all) {
        if (p.name == stored.name && p.backend == stored.backend) {
            p = stored;
            replaced = true;
        }
    }
    if (!replaced)
        all.push_back(stored);
    return writeAll(all);
}

QString remove(const QString &name)
{
    QVector<CameraPreset> all = loadAllBackends();
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).name == name && all.at(i).backend == current()) {
            all.removeAt(i);
            return writeAll(all);
        }
    }
    return QStringLiteral("Пресет не найден");
}

QString rename(const QString &from, const QString &to)
{
    if (to.trimmed().isEmpty())
        return QStringLiteral("Пустое имя");
    QVector<CameraPreset> all = loadAllBackends();
    for (const CameraPreset &p : all)
        if (p.name == to && p.backend == current())
            return QStringLiteral("Пресет «%1» уже есть").arg(to);
    for (CameraPreset &p : all) {
        if (p.name == from && p.backend == current()) {
            p.name = to;
            return writeAll(all);
        }
    }
    return QStringLiteral("Пресет не найден");
}

} // namespace CameraPresetStore
