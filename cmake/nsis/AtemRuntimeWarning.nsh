!macro AtemRuntimeInstallWarning
  IfFileExists "$PROGRAMFILES32\Blackmagic Design\Blackmagic ATEM Switchers\BMDSwitcherAPI.dll" done 0
  IfFileExists "$PROGRAMFILES\Blackmagic Design\Blackmagic ATEM Switchers\BMDSwitcherAPI.dll" done 0
  MessageBox MB_OK|MB_ICONEXCLAMATION "Blackmagic ATEM Switchers was not detected. Install ATEM Software Control from: https://www.blackmagicdesign.com/support/family/atem-live-production-switchers"
done:
!macroend