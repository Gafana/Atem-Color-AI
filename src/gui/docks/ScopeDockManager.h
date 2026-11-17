#pragma once

#include "scopes/ScopeTypes.h"

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>

class QDockWidget;
class QMainWindow;
class PreviewOverlayWidget;
class ScopeInstrumentWidget;

struct ScopeDockInstance
{
    QString id;
    ScopeKind kind = ScopeKind::None;
    QDockWidget *dock = nullptr;
    ScopeInstrumentWidget *widget = nullptr;
    ScopeDisplaySettings settings;
    QSize plotSize;
    QImage lastSnapshot;
};

class ScopeDockManager final : public QObject
{
    Q_OBJECT

public:
    explicit ScopeDockManager(QMainWindow *window, PreviewOverlayWidget *preview, QObject *parent = nullptr);

    void ensurePreviewDock();
    PreviewOverlayWidget *previewWidget() const { return m_preview; }
    float compareWipe() const;
    int refreshFps() const { return m_refreshFps; }

    void addInstrument(ScopeKind kind, const QString &instanceId = QString(),
                       const ScopeDisplaySettings *settings = nullptr);
    void removeInstrument(const QString &instanceId);
    QVector<ScopeDockInstance> visibleInstruments() const;

    bool takePendingCompute();
    struct PendingCompute {
        QImage frameA;
        QImage frameB;
        QVector<ScopeDockInstance> instruments;
        int plotWidth = 640;
        int plotHeight = 360;
        float compareWipe = 0.5f;
    };
    PendingCompute pendingRequest() const { return m_pending; }

    void submitFrame(const QImage &frameA, const QImage &frameB);
    void applyLayoutPreset(const QString &presetId);
    void saveCustomLayout(const QString &name);
    bool loadCustomLayout(const QString &name);
    /** Name used to persist the last session (instances + settings + dock state). */
    static QString sessionLayoutName() { return QStringLiteral("__last_session"); }
    QDockWidget *previewDock() const { return m_previewDock; }

    QByteArray saveWindowState() const;
    void restoreWindowState(const QByteArray &state);

    QString takeSnapshot(const QImage &sourceFrame, const QString &snapshotRoot);
    void enableSnapshotOverlay(bool on);
    bool snapshotOverlayEnabled() const { return m_snapshotOverlayOn; }
    void setInstrumentSettings(const QString &instanceId, const ScopeDisplaySettings &settings);

Q_SIGNALS:
    void needCompute();
    void previewOverlay(const QImage &overlay, bool enabled);
    void instrumentSettings(const QString &instanceId);
    void scopeFpsUpdated(double fps);

public Q_SLOTS:
    void onInstrumentResult(const QString &instanceId, const QImage &image, ScopeKind kind);

private Q_SLOTS:
    void onDockClosed();

private:
    QString newInstanceId() const;
    void clearInstruments();
    ScopeDockInstance *find(const QString &id);
    const ScopeDockInstance *find(const QString &id) const;
    void queueCompute();
    void styleDock(QDockWidget *dock);
    void updatePreviewOverlay();

    QMainWindow *m_window = nullptr;
    PreviewOverlayWidget *m_preview = nullptr;
    QDockWidget *m_previewDock = nullptr;
    QVector<ScopeDockInstance> m_instances;
    PendingCompute m_pending;
    bool m_computeQueued = false;
    qint64 m_lastComputeMs = 0;
    int m_refreshFps = 25;
    QImage m_frameA;
    QImage m_frameB;
    QImage m_previewOverlay;
    QImage m_lastBundleSnapshot;
    bool m_snapshotOverlayOn = false;
    int m_scopeFrames = 0;
    qint64 m_scopeFpsStamp = 0;
};