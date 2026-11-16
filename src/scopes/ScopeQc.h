#pragma once

#include "ScopeTypes.h"

#include <QImage>

namespace ScopeQc {

ScopeQcResult analyzeFrame(const QImage &rgb32, float ireLow, float ireHigh);

} // namespace ScopeQc