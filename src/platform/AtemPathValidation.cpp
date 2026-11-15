#include "AtemPathValidation.h"

#include <QDir>
#include <QFileInfo>

namespace AtemPathValidation {

namespace {

bool fileExists(const QString &path)
{
    const QFileInfo fi(path);
    return fi.exists() && fi.isFile();
}

QString findInDir(const QDir &dir, const QString &fileName)
{
    if (fileExists(dir.filePath(fileName)))
        return dir.filePath(fileName);
    const QDir win(dir.filePath(QStringLiteral("Win")));
    if (win.exists() && fileExists(win.filePath(fileName)))
        return win.filePath(fileName);
    const QDir win64(dir.filePath(QStringLiteral("Win64")));
    if (win64.exists() && fileExists(win64.filePath(fileName)))
        return win64.filePath(fileName);
    return {};
}

bool dirHasSwitcherApiDll(const QDir &dir)
{
    static const QStringList relPaths = {
        QStringLiteral("BMDSwitcherAPI.dll"),
        QStringLiteral("bin/BMDSwitcherAPI.dll"),
        QStringLiteral("Blackmagic ATEM Switchers/BMDSwitcherAPI.dll"),
    };
    for (const QString &rel : relPaths) {
        if (fileExists(dir.filePath(rel)))
            return true;
    }
    return false;
}

bool dirHasAtemControlExe(const QDir &dir)
{
    static const QStringList names = {
        QStringLiteral("ATEM Software Control.exe"),
        QStringLiteral("ATEM Software Control/ATEM Software Control.exe"),
    };
    for (const QString &rel : names) {
        if (fileExists(dir.filePath(rel)))
            return true;
    }
    return false;
}

} // namespace

AtemPathCheckResult validateSdkPath(const QString &path)
{
    AtemPathCheckResult r;
    if (path.trimmed().isEmpty()) {
        r.detail = QStringLiteral("Путь не задан");
        return r;
    }
    const QFileInfo fi(path.trimmed());
    if (!fi.exists()) {
        r.detail = QStringLiteral("Каталог не существует");
        return r;
    }
    if (!fi.isDir()) {
        r.detail = QStringLiteral("Ожидается каталог SDK");
        return r;
    }
    const QDir dir(fi.absoluteFilePath());
    const QString idl = findInDir(dir, QStringLiteral("BMDSwitcherAPI.idl"));
    const QString header = findInDir(dir, QStringLiteral("BMDSwitcherAPI.h"));
    if (idl.isEmpty() && header.isEmpty()) {
        r.detail = QStringLiteral("Не найдены BMDSwitcherAPI.h или BMDSwitcherAPI.idl");
        return r;
    }
    r.valid = true;
    r.detail = !header.isEmpty() ? QStringLiteral("Найден BMDSwitcherAPI.h")
                                 : QStringLiteral("Найден BMDSwitcherAPI.idl");
    return r;
}

AtemPathCheckResult validateSoftwareControlPath(const QString &path)
{
    AtemPathCheckResult r;
    if (path.trimmed().isEmpty()) {
        r.detail = QStringLiteral("Путь не задан");
        return r;
    }
    QFileInfo fi(path.trimmed());
    if (!fi.exists()) {
        r.detail = QStringLiteral("Путь не существует");
        return r;
    }
    if (fi.isFile()) {
        if (fi.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0) {
            r.detail = QStringLiteral("Ожидается .exe ATEM Software Control");
            return r;
        }
        r.valid = true;
        r.detail = QStringLiteral("Исполняемый файл найден");
        return r;
    }
    const QDir dir(fi.absoluteFilePath());
    if (dirHasSwitcherApiDll(dir)) {
        r.valid = true;
        r.detail = QStringLiteral("Найден BMDSwitcherAPI.dll");
        return r;
    }
    if (dirHasAtemControlExe(dir)) {
        r.valid = true;
        r.detail = QStringLiteral("Найден ATEM Software Control.exe");
        return r;
    }
    r.detail = QStringLiteral("Нет BMDSwitcherAPI.dll и ATEM Software Control.exe");
    return r;
}

QString normalizeSoftwareControlPath(const QString &path)
{
    if (path.trimmed().isEmpty())
        return {};
    QFileInfo fi(path.trimmed());
    if (!fi.exists())
        return path.trimmed();
    if (fi.isFile())
        return fi.absolutePath();
    return fi.absoluteFilePath();
}

QString detectInstalledSoftwareControlPath()
{
#ifdef _WIN32
    static const QStringList candidates = {
        QStringLiteral("C:/Program Files (x86)/Blackmagic Design/Blackmagic ATEM Switchers"),
        QStringLiteral("C:/Program Files/Blackmagic Design/Blackmagic ATEM Switchers"),
    };
    for (const QString &c : candidates) {
        const QDir dir(c);
        if (!dir.exists())
            continue;
        if (validateSoftwareControlPath(c).valid)
            return c;
    }
#endif
    return {};
}

QString detectLikelySdkPath()
{
    // SDK обычно не устанавливается автоматически; подсказка из переменной окружения сборки.
    const QByteArray fromEnv = qgetenv("ATEM_SDK_DIR");
    if (!fromEnv.isEmpty()) {
        const QString p = QString::fromLocal8Bit(fromEnv);
        if (validateSdkPath(p).valid)
            return p;
    }
    return {};
}

} // namespace AtemPathValidation