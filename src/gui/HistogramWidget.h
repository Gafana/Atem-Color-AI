#pragma once

#include "color/ColorTypes.h"

#include <QWidget>

class HistogramWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget *parent = nullptr);

    void setAnalysis(const FrameColorAnalysis &analysis);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    FrameColorAnalysis m_analysis;
};