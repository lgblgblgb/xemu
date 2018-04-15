# A very lame NSI installer stuff, Windows/NSI expert is wanted ...
# (C)2018 LGB Gabor Lenart

Unicode true
SetCompressor /SOLID /FINAL LZMA
!include "MUI2.nsh"

Name "Xemu (x64)" # Name of the installer (usually the name of the application to install).
OutFile "bin\XemuInstaller.exe" # Name of the installer's file.
#Icon ""

AutoCloseWindow false
ShowInstDetails show
CRCCheck on
SetDateSave on
SetDatablockOptimize on

VIProductVersion "${XEMUVER}"
VIFileVersion "${XEMUVER}"
VIAddVersionKey /LANG=0 "ProductName" "Xemu"
VIAddVersionKey /LANG=0 "Comments" "Xemu - emulations of various (old-ish) computers"
VIAddVersionKey /LANG=0 "LegalCopyright" "© Gabor Lenart"
VIAddVersionKey /LANG=0 "FileDescription" "Xemu - emulations of various (old-ish) computers"
VIAddVersionKey /LANG=0 "FileVersion" "${XEMUVER}"


InstallDir "$PROGRAMFILES64\xemu"
RequestExecutionLevel admin
InstallDirRegKey HKLM "Software\Xemu\InstallDirectory" ""
#ManifestSupportedOS all


Var StartMenuFolder

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

!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH



!insertmacro MUI_LANGUAGE "English"



Section "Xemu" SecMain
	SectionIn RO
	SetOutPath "$INSTDIR"
	File "/oname=readme.txt" "..\README.md"
	File "/oname=license.txt" "..\LICENSE"
	File "bin\*.exe"
	File "bin\*.dll"
	WriteRegStr HKLM "Software\Xemu\InstallDirectory" "" $INSTDIR
	WriteUninstaller "$INSTDIR\Uninstall.exe"
	!insertmacro MUI_STARTMENU_WRITE_BEGIN Application
		SetShellVarContext all
		CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Xemu - Commodore 65 emulator.lnk" "$INSTDIR\xc65.exe"
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Xemu - Commodore LCD emulator.lnk" "$INSTDIR\xclcd.exe"
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Xemu - Mega 65 emulator.lnk" "$INSTDIR\xmega65.exe"
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\README.lnk" "$INSTDIR\readme.txt"
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\UNINSTALL XEMU.lnk" "$INSTDIR\Uninstall.exe"
	!insertmacro MUI_STARTMENU_WRITE_END
SectionEnd


Section "Uninstall"
	SetShellVarContext all
	Delete "$SMPROGRAMS\$StartMenuFolder\Xemu - Commodore 65 emulator.lnk"
	Delete "$SMPROGRAMS\$StartMenuFolder\Xemu - Commodore LCD emulator.lnk"
	Delete "$SMPROGRAMS\$StartMenuFolder\Xemu - Mega 65 emulator.lnk"
	Delete "$SMPROGRAMS\$StartMenuFolder\README.lnk"
	Delete "$SMPROGRAMS\$StartMenuFolder\UNINSTALL XEMU.lnk"
	Delete "$INSTDIR\readme.txt"
	Delete "$INSTDIR\license.txt"
	Delete "$INSTDIR\*.exe"
	Delete "$INSTDIR\*.dll"
	DeleteRegKey /ifempty HKLM "Software\Xemu\InstallDirectory"
	DeleteRegKey /ifempty HKLM "Software\Xemu"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"
	RMDir "$SMPROGRAMS\$StartMenuFolder"
SectionEnd

