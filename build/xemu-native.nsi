# A very lame NSI installer stuff, Windows/NSI expert is wanted ...
# (C)2018 LGB Gabor Lenart
# Seems to be useful example: http://nsis.sourceforge.net/A_simple_installer_with_start_menu_shortcut_and_uninstaller
# Please consider to help me to improvide this ... I have really *NO* idea about windows and its installers,
# I just try to learn from NSI examples and try to put stuffs together into this file ...
# Some icon work is really needed as well!!

#Unicode true
SetCompressor /SOLID /FINAL LZMA
!include "MUI2.nsh"

Name "Xemu (${ARCH})"
OutFile "bin\${EXENAME}"
Icon "bin\xemu.ico"

AutoCloseWindow false
ShowInstDetails show
CRCCheck on
SetDateSave on
SetDatablockOptimize on

VIProductVersion "${XEMUVER}"
#VIFileVersion "${XEMUVER}"
VIAddVersionKey /LANG=0 "ProductName" "Xemu"
VIAddVersionKey /LANG=0 "Comments" "Xemu - emulations of various (old-ish) computers"
VIAddVersionKey /LANG=0 "LegalCopyright" "(C) Gabor Lenart"
VIAddVersionKey /LANG=0 "FileDescription" "Xemu - emulations of various (old-ish) computers"
VIAddVersionKey /LANG=0 "FileVersion" "${XEMUVER}"

!ifndef WIN64
InstallDir "$PROGRAMFILES\xemu"
!else
InstallDir "$PROGRAMFILES64\xemu"
!endif
RequestExecutionLevel admin
InstallDirRegKey HKLM "Software\Xemu\InstallDirectory" ""
#ManifestSupportedOS all


#Var StartMenuFolder

!define MUI_HEADERIMAGE
#!define MUI_HEADERIMAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Header\nsis.bmp"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY

!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\Xemu"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"

#!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH



!insertmacro MUI_LANGUAGE "English"



Section "Xemu" SecMain
	SectionIn RO
	SetShellVarContext all
	SetOutPath "$INSTDIR"
	File "..\rom\rom-fetch-list.txt"
	File "/oname=readme.txt" "..\README.md"
	File "/oname=license.txt" "..\LICENSE"
	File "bin\x*.exe"
	File "bin\xemu.ico"
	File "bin\${SDL2DLL}"
	WriteRegStr HKLM "Software\Xemu\InstallDirectory" "" $INSTDIR
	WriteUninstaller "$INSTDIR\Uninstall.exe"
#	!insertmacro MUI_STARTMENU_WRITE_BEGIN Application
		SetShellVarContext all
		CreateDirectory "$SMPROGRAMS\Xemu"
		CreateShortCut "$SMPROGRAMS\Xemu\Xemu - Commodore 65 emulator.lnk" "$INSTDIR\xc65.exe" "" "$INSTDIR\xemu.ico"
		CreateShortCut "$SMPROGRAMS\Xemu\Xemu - Commodore LCD emulator.lnk" "$INSTDIR\xclcd.exe" "" "$INSTDIR\xemu.ico"
		CreateShortCut "$SMPROGRAMS\Xemu\Xemu - Mega 65 emulator.lnk" "$INSTDIR\xmega65.exe" "" "$INSTDIR\xemu.ico"
#		CreateShortCut "$SMPROGRAMS\Xemu\README.lnk" "$INSTDIR\readme.txt"
#		CreateShortCut "$SMPROGRAMS\Xemu\UNINSTALL XEMU.lnk" "$INSTDIR\Uninstall.exe"
#	!insertmacro MUI_STARTMENU_WRITE_END

	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "DisplayName" "Xemu - emulators - for windows"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "InstallLocation" "$\"$INSTDIR$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "DisplayIcon" "$\"$INSTDIR\xemu.ico$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "Publisher" "$\"Gabor Lenart (LGB)$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "HelpLink" "$\"https://github.com/lgblgblgb/xemu/wiki$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "URLUpdateInfo" "$\"https://github.com/lgblgblgb/xemu/commits/master$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "URLInfoAbout" "$\"https://github.com/lgblgblgb/xemu/blob/master/README.md$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "DisplayVersion" "$\"${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}$\""
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "VersionMajor" ${VERSIONMAJOR}
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "VersionMinor" ${VERSIONMINOR}
	# There is no option for modifying or repairing the install
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "NoRepair" 1
	# Set the INSTALLSIZE constant (!defined at the top of this script) so Add/Remove Programs can accurately report the size
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators" "EstimatedSize" ${INSTALLSIZE}
SectionEnd


Section "Uninstall"
	SetShellVarContext all
	Delete "$SMPROGRAMS\Xemu\Xemu - Commodore 65 emulator.lnk"
	Delete "$SMPROGRAMS\Xemu\Xemu - Commodore LCD emulator.lnk"
	Delete "$SMPROGRAMS\Xemu\Xemu - Mega 65 emulator.lnk"
	Delete "$SMPROGRAMS\Xemu\README.lnk"
	Delete "$SMPROGRAMS\Xemu\UNINSTALL XEMU.lnk"
	Delete "$INSTDIR\rom-fetch-list.txt"
	Delete "$INSTDIR\readme.txt"
	Delete "$INSTDIR\license.txt"
	Delete "$INSTDIR\x*.exe"
	Delete "$INSTDIR\xemu.ico"
	Delete "$INSTDIR\${SDL2DLL}"
	DeleteRegKey /ifempty HKLM "Software\Xemu\InstallDirectory"
	DeleteRegKey /ifempty HKLM "Software\Xemu"
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xemu emulators"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"
	RMDir "$SMPROGRAMS\Xemu"
SectionEnd

