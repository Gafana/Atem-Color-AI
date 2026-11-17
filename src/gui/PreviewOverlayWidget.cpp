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
    m_dragging = false;
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

QRectF PreviewOverlayWidget::imageRect() const
{
    if (m_image.isNull() || m_image.width() <= 0 || m_image.height() <= 0)
        return QRectF(rect());
    const QSize s = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    return QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height());
}

QRectF PreviewOverlayWidget::toWidget(const QRectF &n) const
{
    const QRectF ir = imageRect();
    return QRectF(ir.x() + n.x() * ir.width(), ir.y() + n.y() * ir.height(), n.width() * ir.width(),
                  n.height() * ir.height());
}

QPointF PreviewOverlayWidget::mapFromWidget(const QPoint &pos) const
{
    // Normalised to the picture (not the widget with its black bars), clamped to it: the zone is applied
    // to the captured frame with the same coordinates.
    const QRectF ir = imageRect();
    if (ir.width() <= 0 || ir.height() <= 0)
        return {};
    return QPointF(qBound(0.0, (pos.x() - ir.x()) / ir.width(), 1.0), qBound(0.0, (pos.y() - ir.y()) / ir.height(), 1.0));
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

    // Outline only (no fill: the picture under the zone must stay readable).
    painter.setPen(QPen(QColor(0, 220, 120), 2));
    painter.setBrush(Qt::NoBrush);
    if (m_tool == Tool::GrayRect && !m_grayRoi.isEmpty() && !m_dragging)
        painter.drawRect(toWidget(m_grayRoi));
    if (m_dragging && m_tool == Tool::GrayRect) {
        const QRectF r = QRectF(m_dragStart, m_dragCurrent).normalized();
        painter.setPen(QPen(QColor(255, 200, 0), 2, Qt::DashLine));
        painter.drawRect(toWidget(r));
    }
    if (m_tool == Tool::ColorCheckerQuad && m_checker.size() == 4) {
        const QRectF ir = imageRect();
        QPolygonF poly;
        for (const QPointF &p : m_checker)
            poly << QPointF(ir.x() + p.x() * ir.width(), ir.y() + p.y() * ir.height());
        painter.drawPolygon(poly);
        painter.setBrush(Qt::NoBrush);
        for (const QPointF &p : poly)
            painter.drawEllipse(p, 5, 5);
    }
}

void PreviewOverlayWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_tool == Tool::None)
        return;
    if (event->button() == Qt::RightButton) {
        // right click: remove the zone (measure the whole frame)
        if (m_tool == Tool::GrayRect && !m_grayRoi.isEmpty()) {
            m_grayRoi = {};
            Q_EMIT grayRoiEdited(m_grayRoi);
        } else if (m_tool == Tool::ColorCheckerQuad && !m_checker.isEmpty()) {
            m_checker.clear();
            Q_EMIT checkerCornersEdited(m_checker);
        }
        update();
        return;
    }
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
        // a plain click (no drag) keeps the existing zone instead of silently dropping it
        const QRectF r = QRectF(m_dragStart, m_dragCurrent).normalized();
        if (r.width() >= 0.02 && r.height() >= 0.02) {
            m_grayRoi = r;
            Q_EMIT grayRoiEdited(m_grayRoi);
        }
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