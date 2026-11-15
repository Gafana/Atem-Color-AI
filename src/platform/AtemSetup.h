#pragma once

#include <QString>

namespace AtemSetup {

/// Реальное управление ATEM (Camera Control): нужен валидный путь к ATEM Software Control.
bool isLiveAtemControlAvailable();

/// Текст для баннера в GUI; пустая строка — предупреждение не нужно.
QString configurationBannerText();

void logConfigurationWarnings();

} // namespace AtemSetup