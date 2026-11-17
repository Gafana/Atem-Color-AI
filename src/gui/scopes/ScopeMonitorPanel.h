#pragma once

#include "ScopeComputeWorker.h"
#include "scopes/ScopeTypes.h"

#include <QImage>
#include <QWidget>

class QGridLayout;
class QToolButton;
class ScopeCellView;

class ScopeMonitorPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeMonitorPanel(QWidget *parent = nullptr);

    ScopeLayoutState layoutState() const;
    void setLayoutState(const ScopeLayoutState &state);

    int activeCellIndex() const { return m_activeCell; }
    void setActiveCell(int index);
    void assignScopeToActive(ScopeKind kind);
    void assignHotkeyDigit(int digit1to9);
    void cycleActiveCell();
    void toggleFullscreenCell();

    int refreshFps() const { return m_globalRefreshFps; }
    float compareWipe() const;
    QVector<ScopeCellState> visibleCells() const;

    bool takePendingCompute();
    ScopeComputeRequest pendingRequest() const { return m_pending; }

Q_SIGNALS:
    void layoutChanged();
    void needCompute();
    void previewOverlay(const QImage &overlay, bool enabled);

public Q_SLOTS:
    void onCellResult(int cellIndex, const QImage &image, ScopeKind kind);
    void submitFrame(const QImage &frameA, const QImage &frameB);

private Q_SLOTS:
    void onLayoutPicked();
    void onCellKindChanged(int index, ScopeKind kind);
    void onDetachCell(int index);
    void onCellSettings(int index);

private:
    void rebuildGrid();
    void saveState();
    void queueCompute();
    void updateOverlays();

    ScopeLayoutState m_state;
    QVector<ScopeCellView *> m_cells;
    QGridLayout *m_grid = nullptr;
    QWidget *m_gridHost = nullptr;
    int m_activeCell = 0;
    int m_globalRefreshFps = 25;
    bool m_fullscreenOne = false;
    int m_fullscreenCell = -1;
    ScopeComputeRequest m_pending;
    bool m_computeQueued = false;
    qint64 m_lastComputeMs = 0;
    QImage m_frameA;
    QImage m_frameB;
    QVector<ScopeCellView *> m_detached;
    QImage m_previewOverlay;
};