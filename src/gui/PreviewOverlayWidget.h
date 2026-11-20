#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QRectF>
#include <QVector>
#include <QWidget>

/** Draws ROI overlays on top of live preview (normalized 0..1 coords). */
class PreviewOverlayWidget final : public QWidget
{
    Q_OBJECT

public:
    enum class Tool
    {
        None, ///< whole-frame modes: nothing drawn, mouse does not edit
        GrayRect,
        ColorCheckerQuad,
    };

    explicit PreviewOverlayWidget(QWidget *parent = nullptr);

    void setPreviewImage(const QImage &image);
    void setScopeOverlay(const QImage &overlay, bool enabled);
    void setTool(Tool tool);
    void setGrayRoi(const QRectF &roi);
    QRectF grayRoi() const;
    void setCheckerCorners(const QVector<QPointF> &corners);
    QVector<QPointF> checkerCorners() const;

    /** «ИИ-подсказки»: problem areas drawn over the picture (normalized coords), dashed + caption. */
    struct Mark
    {
        QRectF rect;
        QColor color;
        QString text;
    };
    void setAdviceMarks(const QVector<Mark> &marks);

Q_SIGNALS:
    void grayRoiEdited(const QRectF &roi);
    void checkerCornersEdited(const QVector<QPointF> &corners);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPointF mapFromWidget(const QPoint &pos) const;
    /** Where the picture is drawn (letterboxed, aspect kept); zone coordinates are relative to it. */
    QRectF imageRect() const;
    QRectF toWidget(const QRectF &norm) const;

    QImage m_image;
    QImage m_scopeOverlay;
    bool m_scopeOverlayOn = false;
    Tool m_tool = Tool::None;
    QRectF m_grayRoi;
    QVector<QPointF> m_checker;
    bool m_dragging = false;
    QPointF m_dragStart;
    QPointF m_dragCurrent;
    int m_cornerDrag = -1;
    QVector<Mark> m_marks;
};