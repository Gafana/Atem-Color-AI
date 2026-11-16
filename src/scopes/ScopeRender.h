#pragma once

#include "ScopeTypes.h"

namespace ScopeRender {

ScopeFrameOutput waveformLuma(const QImage &src, const ScopeFrameInput &input);
ScopeFrameOutput rgbParade(const QImage &src, const ScopeFrameInput &input);
ScopeFrameOutput ycbcrParade(const QImage &src, const ScopeFrameInput &input);
ScopeFrameOutput rgbOverlay(const QImage &src, const ScopeFrameInput &input);
ScopeFrameOutput vectorscope(const QImage &src, const ScopeFrameInput &input);
ScopeFrameOutput histogramLuma(const QImage &src, const ScopeFrameInput &input);
ScopeFrameOutput histogramRgb(const QImage &src, const ScopeFrameInput &input, bool overlay);
ScopeFrameOutput falseColor(const QImage &src, const ScopeFrameInput &input);
ScopeFrameOutput zebra(const QImage &src, const ScopeFrameInput &input);
ScopeFrameOutput cie1931(const QImage &src, const ScopeFrameInput &input);
ScopeFrameOutput compareWipe(const QImage &a, const QImage &b, float wipePos);
ScopeFrameOutput compareSideBySide(const QImage &a, const QImage &b);

} // namespace ScopeRender