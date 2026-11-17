#include "ScopeTypes.h"

QString scopeKindId(ScopeKind kind)
{
    switch (kind) {
    case ScopeKind::WaveformLuma:
        return QStringLiteral("waveform_luma");
    case ScopeKind::RgbParade:
        return QStringLiteral("rgb_parade");
    case ScopeKind::YcbcrParade:
        return QStringLiteral("ycbcr_parade");
    case ScopeKind::RgbOverlayWaveform:
        return QStringLiteral("rgb_overlay");
    case ScopeKind::Vectorscope:
        return QStringLiteral("vectorscope");
    case ScopeKind::HistogramLuma:
        return QStringLiteral("hist_luma");
    case ScopeKind::HistogramRgb:
        return QStringLiteral("hist_rgb");
    case ScopeKind::HistogramRgbOverlay:
        return QStringLiteral("hist_rgb_overlay");
    case ScopeKind::FalseColor:
        return QStringLiteral("false_color");
    case ScopeKind::Zebra:
        return QStringLiteral("zebra");
    case ScopeKind::Cie1931:
        return QStringLiteral("cie1931");
    case ScopeKind::CompareWipe:
        return QStringLiteral("compare_wipe");
    case ScopeKind::CompareSideBySide:
        return QStringLiteral("compare_sbs");
    case ScopeKind::None:
        return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

ScopeKind scopeKindFromId(const QString &id)
{
    if (id == QLatin1String("waveform_luma"))
        return ScopeKind::WaveformLuma;
    if (id == QLatin1String("rgb_parade"))
        return ScopeKind::RgbParade;
    if (id == QLatin1String("ycbcr_parade"))
        return ScopeKind::YcbcrParade;
    if (id == QLatin1String("rgb_overlay"))
        return ScopeKind::RgbOverlayWaveform;
    if (id == QLatin1String("vectorscope"))
        return ScopeKind::Vectorscope;
    if (id == QLatin1String("hist_luma"))
        return ScopeKind::HistogramLuma;
    if (id == QLatin1String("hist_rgb"))
        return ScopeKind::HistogramRgb;
    if (id == QLatin1String("hist_rgb_overlay"))
        return ScopeKind::HistogramRgbOverlay;
    if (id == QLatin1String("false_color"))
        return ScopeKind::FalseColor;
    if (id == QLatin1String("zebra"))
        return ScopeKind::Zebra;
    if (id == QLatin1String("cie1931"))
        return ScopeKind::Cie1931;
    if (id == QLatin1String("compare_wipe"))
        return ScopeKind::CompareWipe;
    if (id == QLatin1String("compare_sbs"))
        return ScopeKind::CompareSideBySide;
    return ScopeKind::None;
}

QString scopeKindLabel(ScopeKind kind)
{
    switch (kind) {
    case ScopeKind::WaveformLuma:
        return QStringLiteral("Waveform Luma");
    case ScopeKind::RgbParade:
        return QStringLiteral("RGB Parade");
    case ScopeKind::YcbcrParade:
        return QStringLiteral("YCbCr Parade");
    case ScopeKind::RgbOverlayWaveform:
        return QStringLiteral("RGB Overlay");
    case ScopeKind::Vectorscope:
        return QStringLiteral("Вектороскоп");
    case ScopeKind::HistogramLuma:
        return QStringLiteral("Гистограмма Luma");
    case ScopeKind::HistogramRgb:
        return QStringLiteral("Гистограмма RGB");
    case ScopeKind::HistogramRgbOverlay:
        return QStringLiteral("Гистограмма RGB (наложение)");
    case ScopeKind::FalseColor:
        return QStringLiteral("False Color");
    case ScopeKind::Zebra:
        return QStringLiteral("Зебра");
    case ScopeKind::Cie1931:
        return QStringLiteral("CIE 1931");
    case ScopeKind::CompareWipe:
        return QStringLiteral("Сравнение (wipe)");
    case ScopeKind::CompareSideBySide:
        return QStringLiteral("Сравнение (рядом)");
    case ScopeKind::None:
        return QStringLiteral("—");
    }
    return QStringLiteral("—");
}

QVector<ScopeKind> allScopeKinds()
{
    return {ScopeKind::WaveformLuma,     ScopeKind::RgbParade,     ScopeKind::YcbcrParade,
            ScopeKind::RgbOverlayWaveform, ScopeKind::Vectorscope, ScopeKind::HistogramLuma,
            ScopeKind::HistogramRgb,   ScopeKind::HistogramRgbOverlay, ScopeKind::FalseColor,
            ScopeKind::Zebra,          ScopeKind::Cie1931,       ScopeKind::CompareWipe,
            ScopeKind::CompareSideBySide};
}

QString scopeLayoutId(ScopeLayoutId layout)
{
    switch (layout) {
    case ScopeLayoutId::Single:
        return QStringLiteral("1");
    case ScopeLayoutId::DualHorizontal:
        return QStringLiteral("2");
    case ScopeLayoutId::Grid2x2:
        return QStringLiteral("2x2");
    case ScopeLayoutId::OnePlusThree:
        return QStringLiteral("1+3");
    }
    return QStringLiteral("2");
}

ScopeLayoutId scopeLayoutFromId(const QString &id)
{
    if (id == QLatin1String("1"))
        return ScopeLayoutId::Single;
    if (id == QLatin1String("2x2"))
        return ScopeLayoutId::Grid2x2;
    if (id == QLatin1String("1+3"))
        return ScopeLayoutId::OnePlusThree;
    return ScopeLayoutId::DualHorizontal;
}

QString scopeLayoutLabel(ScopeLayoutId layout)
{
    switch (layout) {
    case ScopeLayoutId::Single:
        return QStringLiteral("1 прибор");
    case ScopeLayoutId::DualHorizontal:
        return QStringLiteral("2 рядом");
    case ScopeLayoutId::Grid2x2:
        return QStringLiteral("2×2");
    case ScopeLayoutId::OnePlusThree:
        return QStringLiteral("1 + 3");
    }
    return QStringLiteral("2 рядом");
}

int scopeLayoutCellCount(ScopeLayoutId layout)
{
    switch (layout) {
    case ScopeLayoutId::Single:
        return 1;
    case ScopeLayoutId::DualHorizontal:
        return 2;
    case ScopeLayoutId::Grid2x2:
        return 4;
    case ScopeLayoutId::OnePlusThree:
        return 4;
    }
    return 2;
}