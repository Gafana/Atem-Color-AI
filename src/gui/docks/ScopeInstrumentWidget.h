#pragma once

#include "gui/scopes/ScopeCellView.h"
#include "scopes/ScopeTypes.h"

#include <QWidget>

class ScopeInstrumentWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeInstrumentWidget(const QString &instanceId, ScopeKind kind, QWidget *parent = nullptr);

    QString instanceId() const { return m_instanceId; }
    ScopeKind kind() const { return m_kind; }
    void setKind(ScopeKind kind);
    ScopeDisplaySettings &settings() { return m_settings; }

    void setScopeImage(const QImage &image);
    void setSnapshotOverlay(const QImage &overlay, bool enabled);
    float wipePosition() const;
    /** Size the scope image should be rendered at (device-independent pixels of the plot area). */
    QSize plotSize() const;

Q_SIGNALS:
    void settingsRequested(const QString &instanceId);
    void wipeChanged(float pos);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void refreshPlotPixmap();

    QString m_instanceId;
    ScopeKind m_kind;
    ScopeDisplaySettings m_settings;
    ScopePlotLabel *m_plot = nullptr;
    QImage m_image;
    QImage m_snapshotOverlay;
    bool m_snapshotOn = false;
    bool m_dragWipe = false;
    float m_wipePos = 0.5f;
};