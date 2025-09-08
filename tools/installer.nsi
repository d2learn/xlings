Outfile "xlings-0.0.4-windows.exe"
InstallDir "$PROGRAMFILES\xlings"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"

  SetOutPath "$INSTDIR"
  File /r "..\*.*"

  # add installdir to reg table
  WriteRegStr HKCU "Software\xlings" "Install_Dir" "$INSTDIR"

  #CreateDirectory "$SMPROGRAMS\xlings"
  #CreateShortCut "$SMPROGRAMS\xlings\xlings.lnk" "$INSTDIR\xlings.bat"

  # add uninstall.exe
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  # run real install
  ExecWait '"$INSTDIR\tools\install.win.bat"'

SectionEnd

Section "Uninstall"

  RMDir /r "$INSTDIR"

  #Delete "$SMPROGRAMS\xlings\xlings.lnk"
  #RMDir "$SMPROGRAMS\xlings"

  DeleteRegKey HKCU "Software\xlings"

  # run real uninstall
  ExecWait 'xlings self uninstall'
SectionEnd