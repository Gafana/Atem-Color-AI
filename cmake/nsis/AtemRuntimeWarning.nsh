; Warn when Blackmagic ATEM Switchers runtime (COM) is missing — installer does not ship SDK or BMD software.

!macro AtemRuntimeInstallWarning
  StrCpy $R0 "0"
  IfFileExists "$PROGRAMFILES32\Blackmagic Design\Blackmagic ATEM Switchers\BMDSwitcherAPI.dll" found found2
  found:
    StrCpy $R0 "1"
    Goto checkdone
  found2:
  IfFileExists "$PROGRAMFILES64\Blackmagic Design\Blackmagic ATEM Switchers\BMDSwitcherAPI.dll" found3 checkreg
  found3:
    StrCpy $R0 "1"
    Goto checkdone
  checkreg:
    ReadRegStr $R1 HKCR "BMDSwitcher.BMDSwitcherDiscovery" ""
    StrCmp $R1 "" 0 found4
    ReadRegStr $R1 HKCR "BMDSwitcherAPI.BMDSwitcherDiscovery.1" ""
    StrCmp $R1 "" checkdone found4
  found4:
    StrCpy $R0 "1"
  checkdone:
  StrCmp $R0 "1" done
  MessageBox MB_OK|MB_ICONEXCLAMATION \
    "Blackmagic ATEM Switchers / ATEM Software Control не найден.$\r$\n$\r$\n\
    Для подключения к пульту ATEM нужен COM-сервер BMDSwitcherAPI, который ставится вместе с ПО Blackmagic.$\r$\n\
    Установите ATEM Software Control с сайта Blackmagic:$\r$\n\
    https://www.blackmagicdesign.com/support/family/atem-live-production-switchers$\r$\n$\r$\n\
    Программа Atem Color AI установлена, но без Blackmagic ПО подключение к ATEM работать не будет."
  done:
!macroend