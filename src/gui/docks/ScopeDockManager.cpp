#include "ScopeDockManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

#include "ScopeInstrumentWidget.h"
#include "SnapshotManager.h"
#include "gui/PreviewOverlayWidget.h"
#include "platform/AtemAppSettings.h"

#include <QDateTime>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QStyle>
#include <QToolButton>
#include <QUuid>

namespace {

QString dockTitle(ScopeKind kind, int serial)
{
    const QString label = scopeKindLabel(kind);
    return serial > 1 ? QStringLiteral("%1 #%2").arg(label).arg(serial) : label;
}

int countKind(const QVector<ScopeDockInstance> &list, ScopeKind kind)
{
    int n = 0;
    for (const ScopeDockInstance &inst : list) {
        if (inst.kind == kind)
            ++n;
    }
    return n;
}

/** Dock title bar: the title is a dropdown of all instruments (swap in place), plus float / close. */
class InstrumentTitleBar final : public QWidget
{
public:
    InstrumentTitleBar(QDockWidget *dock, const std::function<void(ScopeKind)> &onPick)
        : QWidget(dock)
    {
        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(4, 1, 2, 1);
        lay->setSpacing(2);
        m_title = new QToolButton(this);
        m_title->setPopupMode(QToolButton::InstantPopup);
        m_title->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_title->setAutoRaise(true);
        m_title->setToolTip(QStringLiteral("Нажмите, чтобы заменить прибор в этой панели"));
        m_title->setStyleSheet(QStringLiteral("QToolButton{padding:1px 4px;} QToolButton::menu-indicator{image:none;width:0px;}"));
        auto *menu = new QMenu(m_title);
        for (ScopeKind k : allScopeKinds()) {
            QAction *a = menu->addAction(scopeKindLabel(k));
            QObject::connect(a, &QAction::triggered, this, [onPick, k] { onPick(k); });
        }
        m_title->setMenu(menu);
        lay->addWidget(m_title);
        lay->addStretch(1);
        auto *flt = new QToolButton(this);
        flt->setAutoRaise(true);
        flt->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
        flt->setToolTip(QStringLiteral("Открепить / закрепить"));
        QObject::connect(flt, &QToolButton::clicked, dock, [dock] { dock->setFloating(!dock->isFloating()); });
        lay->addWidget(flt);
        auto *cls = new QToolButton(this);
        cls->setAutoRaise(true);
        cls->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
        cls->setToolTip(QStringLiteral("Закрыть"));
        QObject::connect(cls, &QToolButton::clicked, dock, &QDockWidget::close);
        lay->addWidget(cls);
        setText(dock->windowTitle());
        QObject::connect(dock, &QDockWidget::windowTitleChanged, this, [this](const QString &t) { setText(t); });
    }
    void setText(const QString &t) { m_title->setText(t + QStringLiteral("  ") + QChar(0x25BE)); }

private:
    QToolButton *m_title = nullptr;
};

} // namespace

ScopeDockManager::ScopeDockManager(QMainWindow *window, PreviewOverlayWidget *preview, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_preview(preview)
{
}

void ScopeDockManager::styleDock(QDockWidget *dock)
{
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                      | QDockWidget::DockWidgetFloatable);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
}

void ScopeDockManager::ensurePreviewDock()
{
    if (m_previewDock || !m_window || !m_preview)
        return;
    m_previewDock = new QDockWidget(QStringLiteral("Превью источника"), m_window);
    m_previewDock->setObjectName(QStringLiteral("dock_preview"));
    styleDock(m_previewDock);
    m_previewDock->setWidget(m_preview);
    m_window->addDockWidget(Qt::LeftDockWidgetArea, m_previewDock);
}

QString ScopeDockManager::newInstanceId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void ScopeDockManager::clearInstruments()
{
    for (ScopeDockInstance &inst : m_instances) {
        if (!inst.dock)
            continue;
        // Detach immediately so restoreState() never matches a dock that is about to be deleted.
        disconnect(inst.dock, nullptr, this, nullptr);
        if (m_window)
            m_window->removeDockWidget(inst.dock);
        inst.dock->setObjectName(QString());
        inst.dock->deleteLater();
    }
    m_instances.clear();
}

QWidget *ScopeDockManager::createContent(ScopeDockInstance &inst, const ScopeDisplaySettings *settings)
{
    inst.widget = nullptr;
    if (inst.kind == ScopeKind::AiTips) {
        inst.content = m_aiTipsFactory ? m_aiTipsFactory() : new QLabel(QStringLiteral("ИИ-подсказки недоступны"));
        return inst.content;
    }
    inst.widget = new ScopeInstrumentWidget(inst.id, inst.kind);
    if (settings)
        inst.settings = *settings;
    else if (inst.settings.refreshFps <= 0)
        inst.settings.refreshFps = m_refreshFps;
    inst.widget->settings() = inst.settings;
    connect(inst.widget, &ScopeInstrumentWidget::settingsRequested, this, &ScopeDockManager::instrumentSettings);
    connect(inst.widget, &ScopeInstrumentWidget::wipeChanged, this, [this](float) { queueCompute(); });
    inst.content = inst.widget;
    return inst.content;
}

void ScopeDockManager::installTitleBar(ScopeDockInstance &inst)
{
    const QString id = inst.id;
    inst.dock->setTitleBarWidget(new InstrumentTitleBar(inst.dock, [this, id](ScopeKind k) {
        // queued: the menu that triggered this belongs to the title bar of the dock being changed
        QMetaObject::invokeMethod(this, [this, id, k] { replaceInstrument(id, k); }, Qt::QueuedConnection);
    }));
}

void ScopeDockManager::addInstrument(ScopeKind kind, const QString &instanceId, const ScopeDisplaySettings *settings)
{
    if (!m_window || kind == ScopeKind::None)
        return;
    if (kind == ScopeKind::AiTips) {
        for (ScopeDockInstance &other : m_instances)
            if (other.kind == ScopeKind::AiTips && other.dock) { // one panel is enough
                other.dock->show();
                other.dock->raise();
                return;
            }
    }
    ScopeDockInstance inst;
    inst.id = (instanceId.isEmpty() || find(instanceId)) ? newInstanceId() : instanceId;
    inst.kind = kind;
    inst.settings.refreshFps = m_refreshFps;
    createContent(inst, settings);
    const int serial = countKind(m_instances, kind) + 1;
    inst.dock = new QDockWidget(dockTitle(kind, serial), m_window);
    styleDock(inst.dock);
    inst.dock->setObjectName(QStringLiteral("scope_%1_%2").arg(scopeKindId(kind), inst.id));
    inst.dock->setWidget(inst.content);
    installTitleBar(inst);
    connect(inst.dock, &QDockWidget::destroyed, this, &ScopeDockManager::onDockClosed);
    m_instances.push_back(inst);
    m_window->addDockWidget(Qt::RightDockWidgetArea, inst.dock);
    if (m_instances.size() > 1) {
        auto *prev = m_instances.at(m_instances.size() - 2).dock;
        if (prev)
            m_window->splitDockWidget(prev, inst.dock, Qt::Vertical);
    }
    queueCompute();
}

void ScopeDockManager::replaceInstrument(const QString &instanceId, ScopeKind kind)
{
    ScopeDockInstance *inst = find(instanceId);
    if (!inst || !inst->dock || kind == ScopeKind::None || kind == inst->kind)
        return;
    if (kind == ScopeKind::AiTips) {
        // only one «ИИ-подсказки» panel: the other one is closed, this dock takes its place
        for (int i = 0; i < m_instances.size(); ++i)
            if (m_instances.at(i).kind == ScopeKind::AiTips && m_instances.at(i).id != instanceId) {
                QDockWidget *d = m_instances.at(i).dock;
                m_instances.removeAt(i);
                if (d) {
                    disconnect(d, nullptr, this, nullptr);
                    m_window->removeDockWidget(d);
                    d->deleteLater();
                }
                break;
            }
        inst = find(instanceId);
        if (!inst)
            return;
    }
    QWidget *old = inst->content;
    inst->kind = kind;
    ScopeDisplaySettings keep = inst->widget ? inst->widget->settings() : inst->settings;
    createContent(*inst, &keep);
    inst->dock->setWidget(inst->content);
    if (old)
        old->deleteLater();
    inst->dock->setObjectName(QStringLiteral("scope_%1_%2").arg(scopeKindId(kind), inst->id));
    int serial = 0;
    for (const ScopeDockInstance &o : m_instances) {
        if (o.kind == kind)
            ++serial;
        if (o.id == instanceId)
            break;
    }
    inst->dock->setWindowTitle(dockTitle(kind, qMax(1, serial)));
    updatePreviewOverlay();
    queueCompute();
    saveCustomLayout(sessionLayoutName());
    AtemAppSettings::setMainWindowDockState(saveWindowState());
}

ScopeDockInstance *ScopeDockManager::find(const QString &id)
{
    for (ScopeDockInstance &inst : m_instances) {
        if (inst.id == id)
            return &inst;
    }
    return nullptr;
}

const ScopeDockInstance *ScopeDockManager::find(const QString &id) const
{
    for (const ScopeDockInstance &inst : m_instances) {
        if (inst.id == id)
            return &inst;
    }
    return nullptr;
}

void ScopeDockManager::removeInstrument(const QString &instanceId)
{
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances.at(i).id != instanceId)
            continue;
        if (m_instances.at(i).dock)
            m_instances.at(i).dock->deleteLater();
        m_instances.removeAt(i);
        break;
    }
    updatePreviewOverlay();
}

void ScopeDockManager::onDockClosed()
{
    auto *dock = qobject_cast<QDockWidget *>(sender());
    if (!dock)
        return;
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances.at(i).dock == dock) {
            m_instances.removeAt(i);
            break;
        }
    }
    updatePreviewOverlay();
}

QVector<ScopeDockInstance> ScopeDockManager::visibleInstruments() const
{
    QVector<ScopeDockInstance> vis;
    for (const ScopeDockInstance &inst : m_instances) {
        if (!inst.dock || !inst.dock->isVisible() || !inst.widget)
            continue;
        vis.push_back(inst);
    }
    return vis;
}

float ScopeDockManager::compareWipe() const
{
    for (const ScopeDockInstance &inst : m_instances) {
        if (inst.kind == ScopeKind::CompareWipe && inst.widget)
            return inst.widget->wipePosition();
    }
    return 0.5f;
}

bool ScopeDockManager::takePendingCompute()
{
    if (!m_computeQueued)
        return false;
    m_computeQueued = false;
    return true;
}

void ScopeDockManager::submitFrame(const QImage &frameA, const QImage &frameB)
{
    m_frameA = frameA;
    m_frameB = frameB;
    queueCompute();
}

void ScopeDockManager::queueCompute()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_refreshFps > 0) {
        const qint64 interval = 1000 / m_refreshFps;
        if (now - m_lastComputeMs < interval)
            return;
    }
    m_lastComputeMs = now;
    m_pending.frameA = m_frameA;
    m_pending.frameB = m_frameB;
    m_pending.instruments.clear();
    for (ScopeDockInstance &inst : m_instances) {
        if (!inst.dock || !inst.dock->isVisible() || !inst.widget)
            continue;
        inst.settings = inst.widget->settings();
        inst.plotSize = inst.widget->plotSize();
        if (inst.plotSize.width() < 16 || inst.plotSize.height() < 16)
            continue;
        m_pending.instruments.push_back(inst);
    }
    m_pending.compareWipe = compareWipe();
    if (m_pending.instruments.isEmpty())
        return;
    m_computeQueued = true;
    Q_EMIT needCompute();
}

void ScopeDockManager::onInstrumentResult(const QString &instanceId, const QImage &image, ScopeKind kind)
{
    ScopeDockInstance *inst = find(instanceId);
    if (!inst || !inst->widget || inst->kind != kind) // late result for a swapped instrument
        return;
    inst->widget->setScopeImage(image);
    ++m_scopeFrames;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_scopeFpsStamp == 0)
        m_scopeFpsStamp = now;
    if (now - m_scopeFpsStamp >= 1000) {
        const double fps = m_scopeFrames * 1000.0 / static_cast<double>(now - m_scopeFpsStamp);
        Q_EMIT scopeFpsUpdated(fps);
        m_scopeFrames = 0;
        m_scopeFpsStamp = now;
    }
    if (kind == ScopeKind::FalseColor || kind == ScopeKind::Zebra) {
        m_previewOverlay = image;
        Q_EMIT previewOverlay(m_previewOverlay, true);
    } else
        updatePreviewOverlay();
    if (m_snapshotOverlayOn && !m_lastBundleSnapshot.isNull())
        inst->widget->setSnapshotOverlay(m_lastBundleSnapshot, true);
}

void ScopeDockManager::updatePreviewOverlay()
{
    bool on = false;
    for (const ScopeDockInstance &inst : m_instances) {
        if (inst.kind == ScopeKind::FalseColor || inst.kind == ScopeKind::Zebra) {
            on = true;
            break;
        }
    }
    if (!on)
        m_previewOverlay = QImage();
    Q_EMIT previewOverlay(m_previewOverlay, on);
}

void ScopeDockManager::applyLayoutPreset(const QString &presetId)
{
    clearInstruments();
    ensurePreviewDock();
    if (m_previewDock) {
        m_previewDock->setFloating(false);
        m_window->addDockWidget(Qt::LeftDockWidgetArea, m_previewDock);
        m_previewDock->show();
    }
    auto add = [this](ScopeKind k) { addInstrument(k); };
    if (presetId == QLatin1String("home")) {
        add(ScopeKind::Vectorscope);
        add(ScopeKind::RgbParade);
    } else if (presetId == QLatin1String("camera_match")) {
        add(ScopeKind::RgbParade);
        add(ScopeKind::Vectorscope);
    } else if (presetId == QLatin1String("exposure")) {
        add(ScopeKind::WaveformLuma);
        add(ScopeKind::FalseColor);
    } else if (presetId == QLatin1String("full")) {
        add(ScopeKind::WaveformLuma);
        add(ScopeKind::RgbParade);
        add(ScopeKind::Vectorscope);
        add(ScopeKind::HistogramLuma);
        add(ScopeKind::Cie1931);
        add(ScopeKind::Zebra);
    }
    AtemAppSettings::setScopeLayoutPreset(presetId);
}

namespace {

QJsonObject settingsToJson(const ScopeDisplaySettings &s)
{
    QJsonObject o;
    o.insert(QStringLiteral("trace"), s.traceBrightness);
    o.insert(QStringLiteral("zoom"), s.vectorscopeZoom);
    o.insert(QStringLiteral("zebra"), s.zebraThresholdIre);
    o.insert(QStringLiteral("fps"), s.refreshFps);
    o.insert(QStringLiteral("mono"), s.waveformMonochrome);
    o.insert(QStringLiteral("tolLo"), s.ireToleranceLow);
    o.insert(QStringLiteral("tolHi"), s.ireToleranceHigh);
    o.insert(QStringLiteral("zones"), s.vectorscopeLumaZones);
    o.insert(QStringLiteral("zoneLo"), s.zoneLowMaxPct);
    o.insert(QStringLiteral("zoneMid"), s.zoneMidMaxPct);
    o.insert(QStringLiteral("fcLo"), s.falseColorLowIre);
    o.insert(QStringLiteral("fcMid"), s.falseColorMidIre);
    o.insert(QStringLiteral("fcHi"), s.falseColorHighIre);
    return o;
}

ScopeDisplaySettings settingsFromJson(const QJsonObject &o)
{
    ScopeDisplaySettings s;
    s.traceBrightness = static_cast<float>(o.value(QStringLiteral("trace")).toDouble(s.traceBrightness));
    s.vectorscopeZoom = o.value(QStringLiteral("zoom")).toInt(s.vectorscopeZoom);
    s.zebraThresholdIre = static_cast<float>(o.value(QStringLiteral("zebra")).toDouble(s.zebraThresholdIre));
    s.refreshFps = o.value(QStringLiteral("fps")).toInt(s.refreshFps);
    s.waveformMonochrome = o.value(QStringLiteral("mono")).toBool(s.waveformMonochrome);
    s.ireToleranceLow = static_cast<float>(o.value(QStringLiteral("tolLo")).toDouble(s.ireToleranceLow));
    s.ireToleranceHigh = static_cast<float>(o.value(QStringLiteral("tolHi")).toDouble(s.ireToleranceHigh));
    s.vectorscopeLumaZones = o.value(QStringLiteral("zones")).toBool(s.vectorscopeLumaZones);
    s.zoneLowMaxPct = static_cast<float>(o.value(QStringLiteral("zoneLo")).toDouble(s.zoneLowMaxPct));
    s.zoneMidMaxPct = static_cast<float>(o.value(QStringLiteral("zoneMid")).toDouble(s.zoneMidMaxPct));
    s.falseColorLowIre = static_cast<float>(o.value(QStringLiteral("fcLo")).toDouble(s.falseColorLowIre));
    s.falseColorMidIre = static_cast<float>(o.value(QStringLiteral("fcMid")).toDouble(s.falseColorMidIre));
    s.falseColorHighIre = static_cast<float>(o.value(QStringLiteral("fcHi")).toDouble(s.falseColorHighIre));
    return s;
}

} // namespace

void ScopeDockManager::saveCustomLayout(const QString &name)
{
    // value = JSON {kind, order, settings}; the dock object names keep the instance id so that
    // QMainWindow::restoreState() can put every dock back where it was.
    QHash<QString, QString> data;
    int order = 0;
    for (const ScopeDockInstance &inst : m_instances) {
        QJsonObject o;
        o.insert(QStringLiteral("kind"), scopeKindId(inst.kind));
        o.insert(QStringLiteral("order"), order++);
        o.insert(QStringLiteral("settings"),
                 settingsToJson(inst.widget ? inst.widget->settings() : inst.settings));
        data.insert(inst.id, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    }
    AtemAppSettings::saveDockLayoutPreset(name, data, saveWindowState());
}

bool ScopeDockManager::loadCustomLayout(const QString &name)
{
    QHash<QString, QString> kinds;
    QByteArray state;
    if (!AtemAppSettings::loadDockLayoutPreset(name, &kinds, &state))
        return false;
    clearInstruments();
    ensurePreviewDock();
    struct Item
    {
        QString id;
        ScopeKind kind;
        int order;
        bool hasSettings;
        ScopeDisplaySettings settings;
    };
    QVector<Item> items;
    for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) {
        Item item{it.key(), ScopeKind::None, 1000 + int(items.size()), false, {}};
        const QString v = it.value();
        if (v.startsWith(QLatin1Char('{'))) {
            const QJsonObject o = QJsonDocument::fromJson(v.toUtf8()).object();
            item.kind = scopeKindFromId(o.value(QStringLiteral("kind")).toString());
            item.order = o.value(QStringLiteral("order")).toInt(item.order);
            if (o.contains(QStringLiteral("settings"))) {
                item.hasSettings = true;
                item.settings = settingsFromJson(o.value(QStringLiteral("settings")).toObject());
            }
        } else {
            item.kind = scopeKindFromId(v); // old format
        }
        items.push_back(item);
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.order < b.order; });
    for (const Item &item : items)
        addInstrument(item.kind, item.id, item.hasSettings ? &item.settings : nullptr);
    if (!state.isEmpty() && m_window)
        m_window->restoreState(state);
    queueCompute();
    return true;
}

QByteArray ScopeDockManager::saveWindowState() const
{
    return m_window ? m_window->saveState(0) : QByteArray();
}

void ScopeDockManager::restoreWindowState(const QByteArray &state)
{
    if (m_window && !state.isEmpty())
        m_window->restoreState(state);
    else {
        const QByteArray saved = AtemAppSettings::mainWindowDockState();
        if (m_window && !saved.isEmpty())
            m_window->restoreState(saved);
    }
}

QString ScopeDockManager::takeSnapshot(const QImage &sourceFrame, const QString &snapshotRoot)
{
    QHash<QString, QPair<QString, QImage>> imgs;
    for (const ScopeDockInstance &inst : m_instances) {
        if (!inst.widget)
            continue;
        const QPixmap pm = inst.widget->grab();
        imgs.insert(inst.id, {scopeKindLabel(inst.kind), pm.toImage()});
    }
    const SnapshotBundle bundle = SnapshotManager::capture(sourceFrame, imgs);
    if (!bundle.source.isNull())
        m_lastBundleSnapshot = bundle.source;
    return bundle.directory;
}

void ScopeDockManager::setInstrumentSettings(const QString &instanceId, const ScopeDisplaySettings &settings)
{
    if (ScopeDockInstance *inst = find(instanceId)) {
        inst->settings = settings;
        if (inst->widget)
            inst->widget->settings() = settings;
        queueCompute();
    }
}

void ScopeDockManager::enableSnapshotOverlay(bool on)
{
    m_snapshotOverlayOn = on;
    for (ScopeDockInstance &inst : m_instances) {
        if (inst.widget)
            inst.widget->setSnapshotOverlay(m_lastBundleSnapshot, on && !m_lastBundleSnapshot.isNull());
    }
}