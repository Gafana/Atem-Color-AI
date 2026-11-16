#include "ScopeCellView.h"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

ScopePlotLabel::ScopePlotLabel(QWidget *parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(80);
    setStyleSheet(QStringLiteral("background-color: #121214;"));
    setAutoFillBackground(true);
}

void ScopePlotLabel::setWipeLine(float pos01)
{
    m_wipePos = qBound(0.0f, pos01, 1.0f);
    update();
}

void ScopePlotLabel::clearWipeLine()
{
    m_wipePos = -1.0f;
    update();
}

void ScopePlotLabel::paintEvent(QPaintEvent *event)
{
    QLabel::paintEvent(event);
    if (m_wipePos < 0.0f)
        return;
    QPainter p(this);
    const QRect r = contentsRect();
    if (r.width() <= 0)
        return;
    const int x = r.left() + static_cast<int>(m_wipePos * r.width());
    p.setPen(QPen(Qt::yellow, 2));
    p.drawLine(x, r.top(), x, r.bottom());
}

ScopeCellView::ScopeCellView(int index, QWidget *parent)
    : QWidget(parent)
    , m_index(index)
{
    setMinimumSize(160, 120);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->setSpacing(4);

    m_title = new QLabel(this);
    m_title->setStyleSheet(QStringLiteral("color: #aaa; font-size: 11px;"));
    lay->addWidget(m_title);

    m_plot = new ScopePlotLabel(this);
    lay->addWidget(m_plot, 1);

    m_kindCombo = new QComboBox(this);
    m_kindCombo->addItem(QStringLiteral("—"), scopeKindId(ScopeKind::None));
    for (ScopeKind k : allScopeKinds())
        m_kindCombo->addItem(scopeKindLabel(k), scopeKindId(k));
    connect(m_kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (m_loadingKind)
            return;
        const ScopeKind k = scopeKindFromId(m_kindCombo->itemData(idx).toString());
        if (k != m_kind) {
            m_kind = k;
            updateWipeOverlay();
            Q_EMIT kindChanged(m_index, k);
        }
    });
    lay->addWidget(m_kindCombo);
}

void ScopeCellView::setTitle(const QString &title)
{
    if (m_title)
        m_title->setText(title);
}

void ScopeCellView::setScopeImage(const QImage &image)
{
    m_image = image;
    refreshPlotPixmap();
}

void ScopeCellView::setKind(ScopeKind kind)
{
    m_kind = kind;
    updateWipeOverlay();
    if (!m_kindCombo)
        return;
    m_loadingKind = true;
    for (int i = 0; i < m_kindCombo->count(); ++i) {
        if (scopeKindFromId(m_kindCombo->itemData(i).toString()) == kind) {
            m_kindCombo->setCurrentIndex(i);
            break;
        }
    }
    m_loadingKind = false;
}

void ScopeCellView::setActive(bool active)
{
    m_active = active;
    update();
}

void ScopeCellView::setWipePosition(float p)
{
    m_wipePos = qBound(0.0f, p, 1.0f);
    updateWipeOverlay();
}

void ScopeCellView::updateWipeOverlay()
{
    if (!m_plot)
        return;
    if (m_kind == ScopeKind::CompareWipe)
        m_plot->setWipeLine(m_wipePos);
    else
        m_plot->clearWipeLine();
}

QRect ScopeCellView::plotRect() const
{
    return m_plot ? m_plot->geometry() : QRect();
}

void ScopeCellView::refreshPlotPixmap()
{
    if (!m_plot)
        return;
    if (m_image.isNull()) {
        m_plot->clear();
        return;
    }
    const QSize area = m_plot->contentsRect().size();
    if (area.width() < 8 || area.height() < 8) {
        m_plot->setPixmap(QPixmap());
        return;
    }
    const QPixmap pm = QPixmap::fromImage(
        m_image.scaled(area, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_plot->setPixmap(pm);
}

void ScopeCellView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshPlotPixmap();
}

void ScopeCellView::paintEvent(QPaintEvent *)
{
    if (!m_active)
        return;
    QPainter p(this);
    p.setPen(QPen(QColor(90, 140, 220), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(1, 1, -2, -2));
}

void ScopeCellView::mousePressEvent(QMouseEvent *event)
{
    if (m_kind == ScopeKind::CompareWipe && event->button() == Qt::LeftButton) {
        const QRect plot = plotRect();
        if (plot.contains(event->pos())) {
            m_dragWipe = true;
            if (plot.width() > 0)
                m_wipePos =
                    static_cast<float>(event->pos().x() - plot.left()) / static_cast<float>(plot.width());
            updateWipeOverlay();
        }
    }
    QWidget::mousePressEvent(event);
}

void ScopeCellView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragWipe && m_kind == ScopeKind::CompareWipe) {
        const QRect plot = plotRect();
        if (plot.width() > 0)
            m_wipePos = qBound(0.0f,
                               static_cast<float>(event->pos().x() - plot.left()) /
                                   static_cast<float>(plot.width()),
                               1.0f);
        updateWipeOverlay();
    }
    QWidget::mouseMoveEvent(event);
}

void ScopeCellView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragWipe = false;
    QWidget::mouseReleaseEvent(event);
}

void ScopeCellView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    auto *settingsAct = menu.addAction(QStringLiteral("Настройки прибора…"));
    auto *detachAct = menu.addAction(QStringLiteral("Открепить в окно"));
    const QAction *picked = menu.exec(event->globalPos());
    if (picked == settingsAct)
        Q_EMIT settingsRequested(m_index);
    else if (picked == detachAct)
        Q_EMIT detachRequested(m_index);
}