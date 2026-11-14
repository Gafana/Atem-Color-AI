#include "AtemRuntime.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace AtemRuntime {

QString missingRuntimeUserMessage()
{
    return QStringLiteral(
        "Для работы с реальным ATEM нужен установленный пакет Blackmagic «ATEM Switchers» "
        "(ATEM Software Control), который регистрирует COM-сервер BMDSwitcherAPI.\n"
        "Скачайте и установите ПО с сайта Blackmagic:\n"
        "https://www.blackmagicdesign.com/support/family/atem-live-production-switchers\n"
        "Без него подключение к пульту невозможно (ошибка CoCreateInstance).");
}

#ifdef _WIN32

namespace {

bool registryHasAtemUninstallEntry()
{
    static const wchar_t *roots[] = {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    };

    for (const wchar_t *uninstallRoot : roots) {
        HKEY hRoot = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, uninstallRoot, 0, KEY_READ, &hRoot) != ERROR_SUCCESS)
            continue;

        DWORD index = 0;
        wchar_t subKeyName[256];
        DWORD subKeyLen = 256;
        while (RegEnumKeyExW(hRoot, index++, subKeyName, &subKeyLen, nullptr, nullptr, nullptr,
                             nullptr)
               == ERROR_SUCCESS) {
            HKEY hSub = nullptr;
            if (RegOpenKeyExW(hRoot, subKeyName, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                wchar_t displayName[512] = {};
                DWORD nameLen = sizeof(displayName);
                DWORD type = 0;
                if (RegQueryValueExW(hSub, L"DisplayName", nullptr, &type,
                                     reinterpret_cast<LPBYTE>(displayName), &nameLen)
                        == ERROR_SUCCESS
                    && type == REG_SZ) {
                    const QString name = QString::fromWCharArray(displayName).toLower();
                    if (name.contains(QStringLiteral("blackmagic atem"))
                        || name.contains(QStringLiteral("atem switchers"))) {
                        RegCloseKey(hSub);
                        RegCloseKey(hRoot);
                        return true;
                    }
                }
                RegCloseKey(hSub);
            }
            subKeyLen = 256;
        }
        RegCloseKey(hRoot);
    }
    return false;
}

bool filesystemHasBmdSwitcherApiDll()
{
    static const wchar_t *paths[] = {
        L"C:\\Program Files (x86)\\Blackmagic Design\\Blackmagic ATEM Switchers\\BMDSwitcherAPI.dll",
        L"C:\\Program Files\\Blackmagic Design\\Blackmagic ATEM Switchers\\BMDSwitcherAPI.dll",
        L"C:\\Program Files\\Blackmagic Design\\Blackmagic ATEM Switchers\\bin\\BMDSwitcherAPI.dll",
    };
    for (const wchar_t *p : paths) {
        const DWORD attrs = GetFileAttributesW(p);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            return true;
    }
    return false;
}

bool comProgIdRegistered()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"BMDSwitcher.BMDSwitcherDiscovery", 0, KEY_READ, &hKey)
        == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"BMDSwitcherAPI.BMDSwitcherDiscovery.1", 0, KEY_READ,
                      &hKey)
        == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

} // namespace

bool isBlackmagicAtemRuntimeAvailable()
{
    if (filesystemHasBmdSwitcherApiDll())
        return true;
    if (comProgIdRegistered())
        return true;
    if (registryHasAtemUninstallEntry())
        return true;
    return false;
}

#else

bool isBlackmagicAtemRuntimeAvailable()
{
    return true;
}

#endif

} // namespace AtemRuntime