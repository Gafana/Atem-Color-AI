#include "HistogramWidget.h"

#include <QPainter>

HistogramWidget::HistogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(140);
}

void HistogramWidget::setAnalysis(const FrameColorAnalysis &analysis)
{
    m_analysis = analysis;
    update();
}

void HistogramWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 24));

    if (m_analysis.sampleCount == 0) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Нет данных"));
        return;
    }

    const QRect area = rect().adjusted(8, 8, -8, -8);
    const auto drawHist = [&](const std::array<quint32, kHistogramBins> &hist, const QColor &color,
                              int row) {
        quint32 peak = 1;
        for (quint32 v : hist)
            peak = qMax(peak, v);

        const int bandH = area.height() / 3;
        const QRect band(area.left(), area.top() + row * bandH, area.width(), bandH - 4);
        p.setPen(color);
        for (int i = 0; i < band.width(); ++i) {
            const int bin = (i * kHistogramBins) / qMax(1, band.width());
            const float h = static_cast<float>(hist[static_cast<size_t>(bin)]) / static_cast<float>(peak);
            const int y = band.bottom() - static_cast<int>(h * (band.height() - 2));
            p.drawLine(band.left() + i, band.bottom(), band.left() + i, y);
        }
    };

    drawHist(m_analysis.histR, QColor(220, 80, 80), 0);
    drawHist(m_analysis.histG, QColor(80, 200, 80), 1);
    drawHist(m_analysis.histB, QColor(80, 120, 220), 2);
}