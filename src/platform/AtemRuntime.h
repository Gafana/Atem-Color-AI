#pragma once

#include <QString>

namespace AtemRuntime {

/// True when Blackmagic ATEM Switchers / Software Control COM runtime appears installed.
bool isBlackmagicAtemRuntimeAvailable();

QString missingRuntimeUserMessage();

} // namespace AtemRuntime