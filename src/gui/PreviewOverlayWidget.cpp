#include "PreviewOverlayWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

PreviewOverlayWidget::PreviewOverlayWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setMouseTracking(true);
    setMinimumSize(200, 120);
}

void PreviewOverlayWidget::setPreviewImage(const QImage &image)
{
    m_image = image;
    update();
}

void PreviewOverlayWidget::setScopeOverlay(const QImage &overlay, bool enabled)
{
    m_scopeOverlay = overlay;
    m_scopeOverlayOn = enabled && !overlay.isNull();
    update();
}

void PreviewOverlayWidget::setTool(Tool tool)
{
    m_tool = tool;
    update();
}

void PreviewOverlayWidget::setGrayRoi(const QRectF &roi)
{
    m_grayRoi = roi;
    update();
}

QRectF PreviewOverlayWidget::grayRoi() const
{
    return m_grayRoi;
}

void PreviewOverlayWidget::setCheckerCorners(const QVector<QPointF> &corners)
{
    m_checker = corners;
    update();
}

QVector<QPointF> PreviewOverlayWidget::checkerCorners() const
{
    return m_checker;
}

QPointF PreviewOverlayWidget::mapFromWidget(const QPoint &pos) const
{
    if (width() <= 0 || height() <= 0)
        return {};
    return QPointF(static_cast<double>(pos.x()) / static_cast<double>(width()),
                 static_cast<double>(pos.y()) / static_cast<double>(height()));
}

void PreviewOverlayWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));

    if (!m_image.isNull()) {
        const QImage scaled =
            m_image.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int x = (width() - scaled.width()) / 2;
        const int y = (height() - scaled.height()) / 2;
        painter.drawImage(x, y, scaled);
        if (m_scopeOverlayOn && !m_scopeOverlay.isNull()) {
            const QImage scope =
                m_scopeOverlay.scaled(scaled.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
            painter.drawImage(x, y, scope);
        }
    } else {
        painter.setPen(QColor(160, 160, 160));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Нет кадра"));
    }

    painter.setPen(QPen(QColor(0, 220, 120), 2));
    painter.setBrush(QColor(0, 220, 120, 40));
    if (m_tool == Tool::GrayRect && !m_grayRoi.isEmpty()) {
        const QRectF r(m_grayRoi.x() * width(), m_grayRoi.y() * height(), m_grayRoi.width() * width(),
                       m_grayRoi.height() * height());
        painter.drawRect(r);
    }
    if (m_dragging && m_tool == Tool::GrayRect) {
        const QRectF r = QRectF(m_dragStart, m_dragCurrent).normalized();
        painter.setPen(QPen(QColor(255, 200, 0), 2, Qt::DashLine));
        painter.drawRect(QRectF(r.x() * width(), r.y() * height(), r.width() * width(), r.height() * height()));
    }
    if (m_tool == Tool::ColorCheckerQuad && m_checker.size() == 4) {
        QPolygonF poly;
        for (const QPointF &p : m_checker)
            poly << QPointF(p.x() * width(), p.y() * height());
        painter.drawPolygon(poly);
        painter.setBrush(Qt::NoBrush);
        for (const QPointF &p : poly)
            painter.drawEllipse(p, 5, 5);
    }
}

void PreviewOverlayWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF norm = mapFromWidget(event->pos());
    m_dragging = true;
    m_dragStart = norm;
    m_dragCurrent = norm;
    Q_UNUSED(norm);
    update();
}

void PreviewOverlayWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    m_dragCurrent = mapFromWidget(event->pos());
    update();
}

void PreviewOverlayWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!m_dragging)
        return;
    m_dragging = false;
    if (m_tool == Tool::GrayRect) {
        m_grayRoi = QRectF(m_dragStart, m_dragCurrent).normalized();
        if (m_grayRoi.width() < 0.02 || m_grayRoi.height() < 0.02)
            m_grayRoi = {};
        else
            Q_EMIT grayRoiEdited(m_grayRoi);
    } else if (m_tool == Tool::ColorCheckerQuad) {
        const QRectF r = QRectF(m_dragStart, m_dragCurrent).normalized();
        if (r.width() >= 0.05 && r.height() >= 0.05) {
            m_checker = {QPointF(r.left(), r.top()), QPointF(r.right(), r.top()),
                         QPointF(r.right(), r.bottom()), QPointF(r.left(), r.bottom())};
            Q_EMIT checkerCornersEdited(m_checker);
        }
    }
    update();
}