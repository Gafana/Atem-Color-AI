#include "ScopeProcessor.h"

#include "ScopeMath.h"
#include "ScopeRender.h"

ScopeFrameOutput ScopeProcessor::render(ScopeKind kind, const ScopeFrameInput &input)
{
    ScopeFrameOutput empty;
    empty.kind = kind;
    if (kind == ScopeKind::None || input.frameA.isNull())
        return empty;

    // Analyse a reduced frame (<= 720 px wide) but render at the full plot size.
    QImage src = ScopeMath::scaledRgb32(input.frameA, qMin(720, qMax(64, input.plotWidth)));
    ScopeFrameInput scaled = input;

    switch (kind) {
    case ScopeKind::WaveformLuma:
        return ScopeRender::waveformLuma(src, scaled);
    case ScopeKind::RgbParade:
        return ScopeRender::rgbParade(src, scaled);
    case ScopeKind::YcbcrParade:
        return ScopeRender::ycbcrParade(src, scaled);
    case ScopeKind::RgbOverlayWaveform:
        return ScopeRender::rgbOverlay(src, scaled);
    case ScopeKind::Vectorscope:
        return ScopeRender::vectorscope(src, scaled);
    case ScopeKind::HistogramLuma:
        return ScopeRender::histogramLuma(src, scaled);
    case ScopeKind::HistogramRgb:
        return ScopeRender::histogramRgb(src, scaled, false);
    case ScopeKind::HistogramRgbOverlay:
        return ScopeRender::histogramRgb(src, scaled, true);
    case ScopeKind::FalseColor:
        return ScopeRender::falseColor(src, scaled);
    case ScopeKind::Zebra:
        return ScopeRender::zebra(src, scaled);
    case ScopeKind::Cie1931:
        return ScopeRender::cie1931(src, scaled);
    case ScopeKind::CompareWipe: {
        QImage b = input.frameB.isNull() ? QImage() : ScopeMath::scaledRgb32(input.frameB, input.plotWidth);
        return ScopeRender::compareWipe(src, b, 0.5f);
    }
    case ScopeKind::CompareSideBySide: {
        QImage b = input.frameB.isNull() ? QImage() : ScopeMath::scaledRgb32(input.frameB, input.plotWidth);
        return ScopeRender::compareSideBySide(src, b);
    }
    default:
        return empty;
    }
}

QVector<ScopeFrameOutput> ScopeProcessor::renderMany(const QVector<ScopeKind> &kinds,
                                                     const ScopeFrameInput &input)
{
    QVector<ScopeKind> unique;
    for (ScopeKind k : kinds) {
        if (k == ScopeKind::None)
            continue;
        if (!unique.contains(k))
            unique.push_back(k);
    }
    QVector<ScopeFrameOutput> outputs;
    outputs.reserve(unique.size());
    for (ScopeKind kind : unique)
        outputs.push_back(render(kind, input));
    return outputs;
}