#include "HistogramWidget.h"

#include <QPainter>

HistogramWidget::HistogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
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
    constexpr int kRows = 4;
    const auto drawHist = [&](const std::array<quint32, kHistogramBins> &hist, const QColor &color,
                              const QString &name, int row) {
        quint32 peak = 1;
        for (quint32 v : hist)
            peak = qMax(peak, v);

        const int bandH = area.height() / kRows;
        const QRect band(area.left(), area.top() + row * bandH, area.width(), bandH - 4);
        p.setPen(color);
        p.drawText(band.adjusted(2, 0, 0, 0), Qt::AlignLeft | Qt::AlignTop, name);
        const QRect plot = band.adjusted(18, 2, -2, -2);
        for (int i = 0; i < plot.width(); ++i) {
            const int bin = (i * kHistogramBins) / qMax(1, plot.width());
            const float h = static_cast<float>(hist[static_cast<size_t>(bin)]) / static_cast<float>(peak);
            const int y = plot.bottom() - static_cast<int>(h * (plot.height() - 2));
            p.drawLine(plot.left() + i, plot.bottom(), plot.left() + i, y);
        }
    };

    drawHist(m_analysis.histR, QColor(220, 80, 80), QStringLiteral("R"), 0);
    drawHist(m_analysis.histG, QColor(80, 200, 80), QStringLiteral("G"), 1);
    drawHist(m_analysis.histB, QColor(80, 120, 220), QStringLiteral("B"), 2);
    drawHist(m_analysis.histY, QColor(230, 230, 230), QStringLiteral("Y"), 3);
}