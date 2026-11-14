#include "VectorscopeWidget.h"

#include <QPainter>

VectorscopeWidget::VectorscopeWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(160, 160);
}

void VectorscopeWidget::setAnalysis(const FrameColorAnalysis &analysis)
{
    m_analysis = analysis;
    update();
}

void VectorscopeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int side = qMin(width(), height()) - 16;
    const QRect plot((width() - side) / 2, (height() - side) / 2, side, side);

    p.fillRect(rect(), QColor(16, 16, 16));
    p.setPen(QColor(80, 80, 80));
    p.drawEllipse(plot);

    quint32 peak = 1;
    for (quint32 v : m_analysis.vectorscopeUv)
        peak = qMax(peak, v);

    for (int y = 0; y < kVectorscopeSize; ++y) {
        for (int x = 0; x < kVectorscopeSize; ++x) {
            const quint32 c = m_analysis.vectorscopeUv[static_cast<size_t>(y * kVectorscopeSize + x)];
            if (c == 0)
                continue;
            const float t = static_cast<float>(c) / static_cast<float>(peak);
            const QColor col = QColor::fromHsv(static_cast<int>(x / static_cast<double>(kVectorscopeSize) * 300),
                                               static_cast<int>(180 + t * 75), static_cast<int>(80 + t * 175));
            const int px = plot.left() + (x * plot.width()) / kVectorscopeSize;
            const int py = plot.top() + (y * plot.height()) / kVectorscopeSize;
            p.fillRect(px, py, qMax(1, plot.width() / kVectorscopeSize),
                       qMax(1, plot.height() / kVectorscopeSize), col);
        }
    }
}