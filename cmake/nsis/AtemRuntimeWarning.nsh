; Warn when Blackmagic ATEM Switchers runtime (COM) is missing.

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
    "Blackmagic ATEM Switchers / ATEM Software Control was not found.$\r$\n$\r$\n\
    Live ATEM control needs the BMDSwitcherAPI COM server installed by Blackmagic.$\r$\n\
    Install ATEM Software Control from:$\r$\n\
    https://www.blackmagicdesign.com/support/family/atem-live-production-switchers$\r$\n$\r$\n\
    Atem Color AI was installed, but ATEM connection will not work without Blackmagic software."
  done:
!macroend