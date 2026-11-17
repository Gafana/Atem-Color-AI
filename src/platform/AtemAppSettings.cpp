#include "AtemAppSettings.h"

#include <QDir>
#include <QVariant>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

namespace {

QString groupValue(const char *group, const char *key)
{
    QString value;
    withSettings([&](QSettings &settings) {
        settings.beginGroup(QString::fromLatin1(group));
        value = settings.value(QString::fromLatin1(key)).toString();
        settings.endGroup();
    });
    return value;
}

void setGroupValue(const char *group, const char *key, const QVariant &value)
{
    withSettings([&](QSettings &settings) {
        settings.beginGroup(QString::fromLatin1(group));
        settings.setValue(QString::fromLatin1(key), value);
        settings.endGroup();
        settings.sync();
    });
}

} // namespace

QVariant value(const QString &key, const QVariant &defaultValue)
{
    QVariant v;
    withSettings([&](QSettings &settings) { v = settings.value(key, defaultValue); });
    return v;
}

void setValue(const QString &key, const QVariant &v)
{
    withSettings([&](QSettings &settings) {
        settings.setValue(key, v);
        settings.sync();
    });
}

QString ip()
{
    return groupValue(kGroup, "ip").trimmed();
}

void setIp(const QString &address)
{
    setGroupValue(kGroup, "ip", address.trimmed());
}

QString videoDeviceId()
{
    return groupValue("capture", "deviceId");
}

void setVideoDeviceId(const QString &id)
{
    setGroupValue("capture", "deviceId", id);
}

QString videoInputKey()
{
    return groupValue("capture", "inputKey");
}

void setVideoInputKey(const QString &key)
{
    setGroupValue("capture", "inputKey", key);
}

QString videoModeKey()
{
    return groupValue("capture", "modeKey");
}

void setVideoModeKey(const QString &key)
{
    setGroupValue("capture", "modeKey", key);
}

QString captureRoute()
{
    return groupValue("capture", "route").trimmed();
}

void setCaptureRoute(const QString &route)
{
    setGroupValue("capture", "route", route.trimmed());
}

qint64 auxOutputId()
{
    return groupValue("capture", "auxId").toLongLong();
}

void setAuxOutputId(qint64 id)
{
    setGroupValue("capture", "auxId", id);
}

QString multiviewLayout()
{
    return groupValue("capture", "mvLayout").trimmed();
}

void setMultiviewLayout(const QString &layoutId)
{
    setGroupValue("capture", "mvLayout", layoutId.trimmed());
}

QVector<CameraBinding> cameraMap()
{
    QVector<CameraBinding> rows;
    const QJsonDocument doc = QJsonDocument::fromJson(groupValue("mapping", "cameras").toUtf8());
    if (!doc.isArray())
        return rows;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject object = value.toObject();
        CameraBinding row;
        row.label = object.value(QStringLiteral("label")).toString();
        row.atemId = static_cast<qint64>(object.value(QStringLiteral("atemId")).toDouble());
        row.multiviewWindow = object.value(QStringLiteral("window")).toInt();
        if (row.label.isEmpty())
            row.label = QStringLiteral("Камера %1").arg(row.atemId);
        rows.push_back(row);
    }
    return rows;
}

void setCameraMap(const QVector<CameraBinding> &rows)
{
    QJsonArray array;
    for (const CameraBinding &row : rows) {
        QJsonObject object;
        object.insert(QStringLiteral("label"), row.label);
        object.insert(QStringLiteral("atemId"), static_cast<double>(row.atemId));
        object.insert(QStringLiteral("window"), row.multiviewWindow);
        array.append(object);
    }
    setGroupValue("mapping", "cameras", QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

QString tuningMode()
{
    const QString value = groupValue("tuning", "mode").trimmed();
  return value.isEmpty() ? QStringLiteral("reference_camera") : value;
}

void setTuningMode(const QString &modeId)
{
    setGroupValue("tuning", "mode", modeId.trimmed());
}

unsigned tuningParamMask()
{
    return groupValue("tuning", "paramMask").toUInt();
}

void setTuningParamMask(unsigned bits)
{
    setGroupValue("tuning", "paramMask", bits);
}

int grayTargetLuma()
{
    bool ok = false;
    const int v = groupValue("tuning", "grayTargetLuma").toInt(&ok);
    return ok ? v : 110;
}

void setGrayTargetLuma(int luma)
{
    setGroupValue("tuning", "grayTargetLuma", qBound(40, luma, 200));
}

QString referenceFramePath()
{
    return groupValue("tuning", "referenceFrame").trimmed();
}

void setReferenceFramePath(const QString &path)
{
    setGroupValue("tuning", "referenceFrame", path.trimmed());
}

QString cameraRoiJson()
{
    return groupValue("tuning", "cameraRois").trimmed();
}

void setCameraRoiJson(const QString &json)
{
    setGroupValue("tuning", "cameraRois", json);
}

ScopeLayoutState scopeLayoutState()
{
    ScopeLayoutState state;
    state.layout = scopeLayoutFromId(groupValue("scopes", "layout"));
    const int count = scopeLayoutCellCount(state.layout);
    state.cells.resize(count);
    const QJsonDocument doc = QJsonDocument::fromJson(groupValue("scopes", "cells").toUtf8());
    if (doc.isArray()) {
        const QJsonArray array = doc.array();
        for (int i = 0; i < qMin(count, array.size()); ++i) {
            const QJsonObject o = array.at(i).toObject();
            state.cells[i].kind = scopeKindFromId(o.value(QStringLiteral("kind")).toString());
            const QJsonObject s = o.value(QStringLiteral("settings")).toObject();
            state.cells[i].settings.traceBrightness =
                static_cast<float>(s.value(QStringLiteral("trace")).toDouble(0.85));
            state.cells[i].settings.refreshFps = s.value(QStringLiteral("refreshFps")).toInt(25);
            state.cells[i].settings.vectorscopeZoom = s.value(QStringLiteral("vsZoom")).toInt(1);
            state.cells[i].settings.zebraThresholdIre =
                static_cast<float>(s.value(QStringLiteral("zebraIre")).toDouble(95.0));
            state.cells[i].settings.ireScale100 = s.value(QStringLiteral("ire100")).toBool(true);
        }
    }
    return state;
}

void setScopeLayoutState(const ScopeLayoutState &state)
{
    setGroupValue("scopes", "layout", scopeLayoutId(state.layout));
    QJsonArray array;
    for (const ScopeCellState &cell : state.cells) {
        QJsonObject o;
        o.insert(QStringLiteral("kind"), scopeKindId(cell.kind));
        QJsonObject s;
        s.insert(QStringLiteral("trace"), cell.settings.traceBrightness);
        s.insert(QStringLiteral("refreshFps"), cell.settings.refreshFps);
        s.insert(QStringLiteral("vsZoom"), cell.settings.vectorscopeZoom);
        s.insert(QStringLiteral("zebraIre"), cell.settings.zebraThresholdIre);
        s.insert(QStringLiteral("ire100"), cell.settings.ireScale100);
        o.insert(QStringLiteral("settings"), s);
        array.append(o);
    }
    setGroupValue("scopes", "cells",
                  QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

QStringList scopePresetNames()
{
    QStringList names;
    const QJsonDocument doc = QJsonDocument::fromJson(groupValue("scopes", "presets").toUtf8());
    if (!doc.isObject())
        return names;
    names = doc.object().keys();
    names.sort();
    return names;
}

bool loadScopePreset(const QString &name, ScopeLayoutState *out)
{
    if (!out)
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(groupValue("scopes", "presets").toUtf8());
    if (!doc.isObject())
        return false;
    const QJsonObject presets = doc.object();
    if (!presets.contains(name))
        return false;
    const QJsonObject root = presets.value(name).toObject();
    out->layout = scopeLayoutFromId(root.value(QStringLiteral("layout")).toString());
    out->cells.clear();
    const QJsonArray array = root.value(QStringLiteral("cells")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        ScopeCellState cell;
        cell.kind = scopeKindFromId(o.value(QStringLiteral("kind")).toString());
        const QJsonObject s = o.value(QStringLiteral("settings")).toObject();
        cell.settings.traceBrightness = static_cast<float>(s.value(QStringLiteral("trace")).toDouble(0.85));
        cell.settings.refreshFps = s.value(QStringLiteral("refreshFps")).toInt(25);
        out->cells.push_back(cell);
    }
    return true;
}

void saveScopePreset(const QString &name, const ScopeLayoutState &state)
{
    QJsonObject presets;
    const QJsonDocument existing = QJsonDocument::fromJson(groupValue("scopes", "presets").toUtf8());
    if (existing.isObject())
        presets = existing.object();
    QJsonObject root;
    root.insert(QStringLiteral("layout"), scopeLayoutId(state.layout));
    QJsonArray array;
    for (const ScopeCellState &cell : state.cells) {
        QJsonObject o;
        o.insert(QStringLiteral("kind"), scopeKindId(cell.kind));
        QJsonObject s;
        s.insert(QStringLiteral("trace"), cell.settings.traceBrightness);
        s.insert(QStringLiteral("refreshFps"), cell.settings.refreshFps);
        o.insert(QStringLiteral("settings"), s);
        array.append(o);
    }
    root.insert(QStringLiteral("cells"), array);
    presets.insert(name, root);
    setGroupValue("scopes", "presets",
                  QString::fromUtf8(QJsonDocument(presets).toJson(QJsonDocument::Compact)));
}

QString scopeLayoutPreset()
{
    const QString v = groupValue("scopes", "layoutPreset").trimmed();
    return v.isEmpty() ? QStringLiteral("home") : v;
}

void setScopeLayoutPreset(const QString &presetId)
{
    setGroupValue("scopes", "layoutPreset", presetId.trimmed());
}

QByteArray mainWindowDockState()
{
    return QByteArray::fromBase64(groupValue("scopes", "dockState").toLatin1());
}

void setMainWindowDockState(const QByteArray &state)
{
    setGroupValue("scopes", "dockState", QString::fromLatin1(state.toBase64()));
}

void saveDockLayoutPreset(const QString &name, const QHash<QString, QString> &kinds,
                          const QByteArray &windowState)
{
    QJsonObject presets;
    const QJsonDocument existing = QJsonDocument::fromJson(groupValue("scopes", "dockPresets").toUtf8());
    if (existing.isObject())
        presets = existing.object();
    QJsonObject entry;
    QJsonArray kindsArr;
    for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) {
        QJsonObject row;
        row.insert(QStringLiteral("id"), it.key());
        row.insert(QStringLiteral("kind"), it.value());
        kindsArr.append(row);
    }
    entry.insert(QStringLiteral("kinds"), kindsArr);
    entry.insert(QStringLiteral("state"), QString::fromLatin1(windowState.toBase64()));
    presets.insert(name, entry);
    setGroupValue("scopes", "dockPresets",
                  QString::fromUtf8(QJsonDocument(presets).toJson(QJsonDocument::Compact)));
}

bool loadDockLayoutPreset(const QString &name, QHash<QString, QString> *kinds, QByteArray *windowState)
{
    if (!kinds)
        return false;
    kinds->clear();
    const QJsonDocument doc = QJsonDocument::fromJson(groupValue("scopes", "dockPresets").toUtf8());
    if (!doc.isObject() || !doc.object().contains(name))
        return false;
    const QJsonObject entry = doc.object().value(name).toObject();
    for (const QJsonValue &value : entry.value(QStringLiteral("kinds")).toArray()) {
        const QJsonObject row = value.toObject();
        kinds->insert(row.value(QStringLiteral("id")).toString(),
                      row.value(QStringLiteral("kind")).toString());
    }
    if (windowState)
        *windowState = QByteArray::fromBase64(entry.value(QStringLiteral("state")).toString().toLatin1());
    return true;
}

QStringList dockLayoutPresetNames()
{
    QStringList names;
    const QJsonDocument doc = QJsonDocument::fromJson(groupValue("scopes", "dockPresets").toUtf8());
    if (doc.isObject()) {
        for (const QString &key : doc.object().keys())
            if (!key.startsWith(QLatin1String("__")))
                names << key; // "__..." are internal (last session)
    }
    names.sort();
    return names;
}

void removeDockLayoutPreset(const QString &name)
{
    const QJsonDocument doc = QJsonDocument::fromJson(groupValue("scopes", "dockPresets").toUtf8());
    if (!doc.isObject())
        return;
    QJsonObject presets = doc.object();
    presets.remove(name);
    setGroupValue("scopes", "dockPresets",
                  QString::fromUtf8(QJsonDocument(presets).toJson(QJsonDocument::Compact)));
}

float qcIreLow()
{
    bool ok = false;
    const float v = groupValue("scopes", "qcIreLow").toFloat(&ok);
    return ok ? v : 0.0f;
}

void setQcIreLow(float ire)
{
    setGroupValue("scopes", "qcIreLow", ire);
}

float qcIreHigh()
{
    const QString v = groupValue("scopes", "qcIreHigh");
    return v.isEmpty() ? 100.0f : v.toFloat();
}

void setQcIreHigh(float ire)
{
    setGroupValue("scopes", "qcIreHigh", ire);
}

} // namespace AtemAppSettings