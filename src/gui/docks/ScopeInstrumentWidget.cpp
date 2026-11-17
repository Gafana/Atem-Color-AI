#include "ScopeInstrumentWidget.h"

#include <QPainter>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QVBoxLayout>

ScopeInstrumentWidget::ScopeInstrumentWidget(const QString &instanceId, ScopeKind kind, QWidget *parent)
    : QWidget(parent)
    , m_instanceId(instanceId)
    , m_kind(kind)
{
    setMinimumSize(160, 100);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_plot = new ScopePlotLabel(this);
    // The pixmap must never drive the layout (QLabel grows to its pixmap -> render bigger ->
    // grows again; the scope then gets clipped by the dock).
    m_plot->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_plot->setMinimumSize(1, 1);
    m_plot->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_plot, 1);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ScopeInstrumentWidget::setKind(ScopeKind kind)
{
    m_kind = kind;
    if (kind == ScopeKind::CompareWipe)
        m_plot->setWipeLine(m_wipePos);
    else
        m_plot->clearWipeLine();
}

void ScopeInstrumentWidget::setScopeImage(const QImage &image)
{
    m_image = image;
    refreshPlotPixmap();
}

void ScopeInstrumentWidget::setSnapshotOverlay(const QImage &overlay, bool enabled)
{
    m_snapshotOverlay = overlay;
    m_snapshotOn = enabled && !overlay.isNull();
    refreshPlotPixmap();
}

QSize ScopeInstrumentWidget::plotSize() const
{
    return m_plot ? m_plot->contentsRect().size() : size();
}

float ScopeInstrumentWidget::wipePosition() const
{
    return m_wipePos;
}

void ScopeInstrumentWidget::refreshPlotPixmap()
{
    if (!m_plot || m_image.isNull()) {
        if (m_plot)
            m_plot->clear();
        return;
    }
    const QSize area = m_plot->contentsRect().size();
    if (area.width() < 8 || area.height() < 8)
        return;
    QImage composed = m_image;
    if (m_snapshotOn && m_settings.snapshotOverlay) {
        QImage snap = m_snapshotOverlay.scaled(composed.size(), Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
        QPainter p(&composed);
        p.setOpacity(m_settings.snapshotOverlayAlpha);
        const int x = (composed.width() - snap.width()) / 2;
        const int y = (composed.height() - snap.height()) / 2;
        p.drawImage(x, y, snap);
        p.setOpacity(1.0);
    }
    if (composed.size() == area)
        m_plot->setPixmap(QPixmap::fromImage(composed));
    else
        m_plot->setPixmap(
            QPixmap::fromImage(composed.scaled(area, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void ScopeInstrumentWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshPlotPixmap();
}

void ScopeInstrumentWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_kind == ScopeKind::CompareWipe && event->button() == Qt::LeftButton) {
        const QRect plot = m_plot->geometry();
        if (plot.contains(event->pos())) {
            m_dragWipe = true;
            m_wipePos = static_cast<float>(event->pos().x() - plot.left()) / static_cast<float>(plot.width());
            m_plot->setWipeLine(m_wipePos);
            Q_EMIT wipeChanged(m_wipePos);
        }
    }
    QWidget::mousePressEvent(event);
}

void ScopeInstrumentWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragWipe && m_kind == ScopeKind::CompareWipe) {
        const QRect plot = m_plot->geometry();
        if (plot.width() > 0) {
            m_wipePos = qBound(0.0f,
                               static_cast<float>(event->pos().x() - plot.left()) /
                                   static_cast<float>(plot.width()),
                               1.0f);
            m_plot->setWipeLine(m_wipePos);
            Q_EMIT wipeChanged(m_wipePos);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void ScopeInstrumentWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragWipe = false;
    QWidget::mouseReleaseEvent(event);
}

void ScopeInstrumentWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    // Quick settings (applied on the next frame); full dialog below.
    if (m_kind == ScopeKind::Vectorscope) {
        QAction *zones = menu.addAction(QStringLiteral("Зоны: тени / средние / света"));
        zones->setCheckable(true);
        zones->setChecked(m_settings.vectorscopeLumaZones);
        connect(zones, &QAction::toggled, this, [this](bool on) { m_settings.vectorscopeLumaZones = on; });
        for (int z : {1, 2, 5}) {
            QAction *a = menu.addAction(QStringLiteral("Зум x%1").arg(z));
            a->setCheckable(true);
            a->setChecked(m_settings.vectorscopeZoom == z);
            connect(a, &QAction::triggered, this, [this, z] { m_settings.vectorscopeZoom = z; });
        }
        menu.addSeparator();
    }
    if (m_kind == ScopeKind::WaveformLuma || m_kind == ScopeKind::RgbParade || m_kind == ScopeKind::RgbOverlayWaveform) {
        QAction *mono = menu.addAction(QStringLiteral("Монохромный след"));
        mono->setCheckable(true);
        mono->setChecked(m_settings.waveformMonochrome);
        connect(mono, &QAction::toggled, this, [this](bool on) { m_settings.waveformMonochrome = on; });
        menu.addSeparator();
    }
    QMenu *bright = menu.addMenu(QStringLiteral("Яркость следа"));
    for (int pct : {25, 50, 85, 120, 160}) {
        QAction *a = bright->addAction(QStringLiteral("%1%").arg(pct));
        a->setCheckable(true);
        a->setChecked(qAbs(m_settings.traceBrightness * 100.0f - pct) < 1.0f);
        connect(a, &QAction::triggered, this, [this, pct] { m_settings.traceBrightness = pct / 100.0f; });
    }
    menu.addSeparator();
    auto *settingsAct = menu.addAction(QStringLiteral("Все настройки прибора…"));
    const QAction *picked = menu.exec(event->globalPos());
    if (picked == settingsAct)
        Q_EMIT settingsRequested(m_instanceId);
}