#pragma once

#include <QString>

struct AtemPathCheckResult
{
    bool valid = false;
    QString detail;
};

namespace AtemPathValidation {

AtemPathCheckResult validateSdkPath(const QString &path);
AtemPathCheckResult validateSoftwareControlPath(const QString &path);

/// Нормализованный каталог установки ATEM Software Control (из exe или папки).
QString normalizeSoftwareControlPath(const QString &path);

QString detectInstalledSoftwareControlPath();
QString detectLikelySdkPath();

} // namespace AtemPathValidation