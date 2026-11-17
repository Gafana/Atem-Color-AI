#include "ScopeMonitorPanel.h"

#include "ScopeCellView.h"
#include "ScopeSettingsDialog.h"
#include "platform/AtemAppSettings.h"

#include <QDateTime>
#include <QDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QInputDialog>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

ScopeKind hotkeyScope(int digit1to9)
{
    const QVector<ScopeKind> keys = {ScopeKind::WaveformLuma, ScopeKind::RgbParade,
                                     ScopeKind::YcbcrParade,   ScopeKind::RgbOverlayWaveform,
                                     ScopeKind::Vectorscope,   ScopeKind::HistogramLuma,
                                     ScopeKind::HistogramRgb,  ScopeKind::FalseColor,
                                     ScopeKind::Zebra};
    if (digit1to9 < 1 || digit1to9 > keys.size())
        return ScopeKind::None;
    return keys.at(digit1to9 - 1);
}

} // namespace

ScopeMonitorPanel::ScopeMonitorPanel(QWidget *parent)
    : QWidget(parent)
{
    const int cells = scopeLayoutCellCount(m_state.layout);
    m_state.cells.resize(cells);
    if (cells > 0)
        m_state.cells[0].kind = ScopeKind::WaveformLuma;
    if (cells > 1)
        m_state.cells[1].kind = ScopeKind::Vectorscope;

    auto *root = new QVBoxLayout(this);
    auto *toolbar = new QHBoxLayout();
    auto addBtn = [this, toolbar](const QString &text, ScopeLayoutId id) -> QToolButton * {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setProperty("layoutId", scopeLayoutId(id));
        b->setCheckable(true);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, &ScopeMonitorPanel::onLayoutPicked);
        toolbar->addWidget(b);
        if (id == m_state.layout)
            b->setChecked(true);
        return b;
    };
    addBtn(QStringLiteral("1"), ScopeLayoutId::Single);
    addBtn(QStringLiteral("2"), ScopeLayoutId::DualHorizontal);
    addBtn(QStringLiteral("2×2"), ScopeLayoutId::Grid2x2);
    addBtn(QStringLiteral("1+3"), ScopeLayoutId::OnePlusThree);
    toolbar->addStretch(1);
    auto *presetSave = new QToolButton(this);
    presetSave->setText(QStringLiteral("Сохранить пресет"));
    connect(presetSave, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Пресет приборов"),
                                                   QStringLiteral("Имя:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty())
            AtemAppSettings::saveScopePreset(name.trimmed(), layoutState());
    });
    toolbar->addWidget(presetSave);
    auto *presetLoad = new QToolButton(this);
    presetLoad->setText(QStringLiteral("Загрузить пресет"));
    connect(presetLoad, &QToolButton::clicked, this, [this] {
        const QStringList names = AtemAppSettings::scopePresetNames();
        if (names.isEmpty())
            return;
        bool ok = false;
        const QString picked = QInputDialog::getItem(this, QStringLiteral("Пресет приборов"),
                                                     QStringLiteral("Имя:"), names, 0, false, &ok);
        if (ok) {
            ScopeLayoutState st;
            if (AtemAppSettings::loadScopePreset(picked, &st))
                setLayoutState(st);
        }
    });
    toolbar->addWidget(presetLoad);
    root->addLayout(toolbar);

    m_gridHost = new QWidget(this);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setSpacing(4);
    root->addWidget(m_gridHost, 1);

    const ScopeLayoutState saved = AtemAppSettings::scopeLayoutState();
    if (!saved.cells.isEmpty())
        setLayoutState(saved);
    else
        rebuildGrid();
}

ScopeLayoutState ScopeMonitorPanel::layoutState() const
{
    ScopeLayoutState st = m_state;
    st.cells.clear();
    for (ScopeCellView *cell : m_cells) {
        ScopeCellState cs;
        cs.kind = cell->kind();
        cs.settings = cell->settings();
        st.cells.push_back(cs);
    }
    return st;
}

void ScopeMonitorPanel::setLayoutState(const ScopeLayoutState &state)
{
    m_state.layout = state.layout;
  const int need = scopeLayoutCellCount(state.layout);
    while (m_state.cells.size() < need)
        m_state.cells.push_back(ScopeCellState{});
    m_state.cells.resize(need);
    for (int i = 0; i < need && i < state.cells.size(); ++i)
        m_state.cells[i] = state.cells.at(i);
    rebuildGrid();
    saveState();
}

void ScopeMonitorPanel::setActiveCell(int index)
{
    if (index < 0 || index >= m_cells.size())
        return;
    m_activeCell = index;
    for (int i = 0; i < m_cells.size(); ++i)
        m_cells.at(i)->setActive(i == m_activeCell);
}

void ScopeMonitorPanel::assignHotkeyDigit(int digit1to9)
{
    assignScopeToActive(hotkeyScope(digit1to9));
}

void ScopeMonitorPanel::assignScopeToActive(ScopeKind kind)
{
    if (m_activeCell < 0 || m_activeCell >= m_cells.size())
        return;
    m_cells.at(m_activeCell)->setKind(kind);
    m_state.cells[m_activeCell].kind = kind;
    saveState();
    queueCompute();
}

void ScopeMonitorPanel::cycleActiveCell()
{
    if (m_cells.isEmpty())
        return;
    setActiveCell((m_activeCell + 1) % m_cells.size());
}

void ScopeMonitorPanel::toggleFullscreenCell()
{
    m_fullscreenOne = !m_fullscreenOne;
    m_fullscreenCell = m_fullscreenOne ? m_activeCell : -1;
    rebuildGrid();
}

QVector<ScopeCellState> ScopeMonitorPanel::visibleCells() const
{
    QVector<ScopeCellState> out;
    for (ScopeCellView *c : m_cells)
        out.push_back(ScopeCellState{c->kind(), c->settings()});
    return out;
}

float ScopeMonitorPanel::compareWipe() const
{
    for (ScopeCellView *c : m_cells) {
        if (c->kind() == ScopeKind::CompareWipe)
            return c->wipePosition();
    }
    return 0.5f;
}

bool ScopeMonitorPanel::takePendingCompute()
{
    if (!m_computeQueued)
        return false;
    m_computeQueued = false;
    return true;
}

void ScopeMonitorPanel::onLayoutPicked()
{
    auto *btn = qobject_cast<QToolButton *>(sender());
    if (!btn)
        return;
    m_state.layout = scopeLayoutFromId(btn->property("layoutId").toString());
    const int need = scopeLayoutCellCount(m_state.layout);
    while (m_state.cells.size() < need)
        m_state.cells.push_back(ScopeCellState{});
    m_state.cells.resize(need);
    rebuildGrid();
    saveState();
    Q_EMIT layoutChanged();
}

void ScopeMonitorPanel::rebuildGrid()
{
    for (ScopeCellView *c : m_cells)
        c->deleteLater();
    m_cells.clear();
    while (QLayoutItem *item = m_grid->takeAt(0))
        delete item;

    int count = scopeLayoutCellCount(m_state.layout);
    if (m_fullscreenOne && m_fullscreenCell >= 0 && m_fullscreenCell < count)
        count = 1;

    for (int i = 0; i < count; ++i) {
        const int srcIndex = (m_fullscreenOne && m_fullscreenCell >= 0) ? m_fullscreenCell : i;
        auto *cell = new ScopeCellView(srcIndex, m_gridHost);
        if (srcIndex < m_state.cells.size())
            cell->setKind(m_state.cells.at(srcIndex).kind);
        cell->setTitle(scopeKindLabel(cell->kind()));
        cell->settings() = srcIndex < m_state.cells.size() ? m_state.cells.at(srcIndex).settings
                                                               : ScopeDisplaySettings{};
        connect(cell, &ScopeCellView::kindChanged, this, &ScopeMonitorPanel::onCellKindChanged);
        connect(cell, &ScopeCellView::detachRequested, this, &ScopeMonitorPanel::onDetachCell);
        connect(cell, &ScopeCellView::settingsRequested, this, &ScopeMonitorPanel::onCellSettings);
        m_cells.push_back(cell);
    }

    if (m_fullscreenOne) {
        m_grid->addWidget(m_cells.at(0), 0, 0);
    } else if (m_state.layout == ScopeLayoutId::Single) {
        m_grid->addWidget(m_cells.at(0), 0, 0);
    } else if (m_state.layout == ScopeLayoutId::DualHorizontal) {
        m_grid->addWidget(m_cells.at(0), 0, 0);
        m_grid->addWidget(m_cells.at(1), 0, 1);
    } else if (m_state.layout == ScopeLayoutId::Grid2x2) {
        for (int i = 0; i < qMin(4, m_cells.size()); ++i)
            m_grid->addWidget(m_cells.at(i), i / 2, i % 2);
    } else {
        if (m_cells.size() >= 4) {
            m_grid->addWidget(m_cells.at(0), 0, 0, 3, 1);
            m_grid->addWidget(m_cells.at(1), 0, 1);
            m_grid->addWidget(m_cells.at(2), 1, 1);
            m_grid->addWidget(m_cells.at(3), 2, 1);
        }
    }
    setActiveCell(qBound(0, m_activeCell, m_cells.size() - 1));
}

void ScopeMonitorPanel::onCellKindChanged(int index, ScopeKind kind)
{
    if (index < m_state.cells.size())
        m_state.cells[index].kind = kind;
    if (index < m_cells.size())
        m_cells.at(index)->setTitle(scopeKindLabel(kind));
    saveState();
    queueCompute();
    updateOverlays();
}

void ScopeMonitorPanel::onDetachCell(int index)
{
    auto *dlg = new QDialog(this, Qt::Window);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(scopeKindLabel(index < m_state.cells.size() ? m_state.cells.at(index).kind
                                                                  : ScopeKind::None));
    auto *lay = new QVBoxLayout(dlg);
    auto *view = new ScopeCellView(index, dlg);
    if (index < m_state.cells.size())
        view->setKind(m_state.cells.at(index).kind);
    lay->addWidget(view);
    m_detached.push_back(view);
    connect(dlg, &QDialog::destroyed, this, [this, view]() {
        m_detached.removeAll(view);
    });
    dlg->resize(640, 400);
    dlg->show();
}

void ScopeMonitorPanel::onCellSettings(int index)
{
    if (index < 0 || index >= m_cells.size())
        return;
    ScopeSettingsDialog dlg(m_cells.at(index)->kind(), m_cells.at(index)->settings(), this);
    if (dlg.exec() == QDialog::Accepted) {
        m_cells.at(index)->settings() = dlg.result();
        if (index < m_state.cells.size())
            m_state.cells[index].settings = dlg.result();
        if (dlg.result().refreshFps > 0)
            m_globalRefreshFps = dlg.result().refreshFps;
        saveState();
        queueCompute();
    }
}

void ScopeMonitorPanel::saveState()
{
    AtemAppSettings::setScopeLayoutState(layoutState());
}

void ScopeMonitorPanel::submitFrame(const QImage &frameA, const QImage &frameB)
{
    m_frameA = frameA;
    m_frameB = frameB;
    queueCompute();
}

void ScopeMonitorPanel::queueCompute()
{
    const int fps = m_globalRefreshFps <= 0 ? 0 : m_globalRefreshFps;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (fps > 0) {
        const qint64 interval = 1000 / fps;
        if (now - m_lastComputeMs < interval)
            return;
    }
    m_lastComputeMs = now;

    m_pending.frameA = m_frameA;
    m_pending.frameB = m_frameB;
    m_pending.cells = visibleCells();
    m_pending.plotWidth = 640;
    m_pending.plotHeight = 360;
    m_pending.compareWipe = compareWipe();
    m_computeQueued = true;
    Q_EMIT needCompute();
}

void ScopeMonitorPanel::onCellResult(int cellIndex, const QImage &image, ScopeKind kind)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size())
        return;
    m_cells.at(cellIndex)->setScopeImage(image);
    m_cells.at(cellIndex)->setTitle(scopeKindLabel(kind));
    const int logicalIndex =
        cellIndex >= 0 && cellIndex < m_cells.size() ? m_cells.at(cellIndex)->index() : cellIndex;
    for (ScopeCellView *det : m_detached) {
        if (det->index() == logicalIndex)
            det->setScopeImage(image);
    }
    if (kind == ScopeKind::FalseColor || kind == ScopeKind::Zebra) {
        m_previewOverlay = image;
        Q_EMIT previewOverlay(m_previewOverlay, true);
    } else
        updateOverlays();
}

void ScopeMonitorPanel::updateOverlays()
{
    bool on = false;
    for (ScopeCellView *c : m_cells) {
        if (c->kind() == ScopeKind::FalseColor || c->kind() == ScopeKind::Zebra) {
            on = true;
            break;
        }
    }
    if (!on)
        m_previewOverlay = QImage();
    Q_EMIT previewOverlay(m_previewOverlay, on);
}