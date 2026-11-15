#include "AtemSetup.h"

#include "AtemAppSettings.h"
#include "AtemPathValidation.h"
#include "AtemRuntime.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcSetup, "atem.setup")

namespace AtemSetup {

bool isLiveAtemControlAvailable()
{
#ifndef HAVE_ATEM_SDK
    return true;
#else
    const QString controlPath = AtemAppSettings::softwareControlPath();
    if (controlPath.trimmed().isEmpty())
        return false;
    return AtemPathValidation::validateSoftwareControlPath(controlPath).valid;
#endif
}

QString configurationBannerText()
{
#ifndef HAVE_ATEM_SDK
    return {};
#endif
    QStringList parts;
    const QString control = AtemAppSettings::softwareControlPath();
    const auto controlCheck = AtemPathValidation::validateSoftwareControlPath(control);

    if (control.trimmed().isEmpty() || !controlCheck.valid) {
        parts << QStringLiteral(
            "Путь к ATEM Software Control не задан или неверен — подключение к пульту и "
            "«Применить поправки» недоступны. Анализ PNG и GStreamer работают. "
            "Откройте «Настройки → Пути ATEM…» для указания папки установки Blackmagic ATEM "
            "Switchers.");
    }

#ifdef _WIN32
    if (isLiveAtemControlAvailable() && !AtemRuntime::isBlackmagicAtemRuntimeAvailable()) {
        parts << QStringLiteral(
            "Blackmagic COM-сервер BMDSwitcherAPI не зарегистрирован в системе — "
            "установите или переустановите ATEM Software Control.");
    }
#endif

    return parts.join(QStringLiteral("\n\n"));
}

void logConfigurationWarnings()
{
    const QString banner = configurationBannerText();
    if (!banner.isEmpty())
        qCWarning(lcSetup).noquote() << banner;
    else if (!isLiveAtemControlAvailable())
        qCWarning(lcSetup) << "ATEM Software Control path is not configured.";
}

} // namespace AtemSetup