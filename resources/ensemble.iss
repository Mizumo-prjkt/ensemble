; ==============================================================================
; Ensemble - Inno Setup 6 installer script
;
; This script is normally driven by build.py, which passes every value below
; through ISCC "/D" defines. It can also be compiled by hand, e.g.:
;
;   ISCC.exe /DSourceDir="C:\path\to\dist_win" /DAppVersion=1.1.1 resources\ensemble.iss
; ==============================================================================

#ifndef SourceDir
  #error SourceDir must be defined (folder containing ensemble.exe + Qt runtime)
#endif

#ifndef AppVersion
  #define AppVersion "1.1.1"
#endif

; Numeric-only version for the PE version resource (must be N.N.N[.N])
#ifndef NumericVersion
  #define NumericVersion "1.1.1"
#endif

#ifndef OutputDir
  #define OutputDir "."
#endif

#ifndef OutputBaseName
  #define OutputBaseName "Ensemble-" + AppVersion + "-windows-x64-setup"
#endif

#define AppName "Ensemble"
#define AppPublisher "Ensemble"
#define AppExe "ensemble.exe"
#define AppURL "https://github.com/Mizumo-prjkt/ensemble"

[Setup]
; Keep AppId stable across releases so upgrades replace the previous install.
AppId={{EDAEF477-0A5E-4397-B663-DB60E113F81C}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
VersionInfoVersion={#NumericVersion}
VersionInfoProductTextVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Lets the user pick per-user (no admin) or all-users install.
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\{#AppExe}
ChangesAssociations=yes
#ifdef LicenseFile
LicenseFile={#LicenseFile}
#endif
#ifdef SetupIcon
SetupIconFile={#SetupIcon}
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assoc"; Description: "Associate .ao3proj project files with {#AppName}"; GroupDescription: "File associations:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.ao3proj\OpenWithProgids"; ValueType: string; ValueName: "Ensemble.ao3proj"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Ensemble.ao3proj"; ValueType: string; ValueName: ""; ValueData: "Ensemble AO3 Project Archive"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Ensemble.ao3proj\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Ensemble.ao3proj\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc

Root: HKA; Subkey: "Software\Classes\.ao3projasve\OpenWithProgids"; ValueType: string; ValueName: "Ensemble.ao3projasve"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Ensemble.ao3projasve"; ValueType: string; ValueName: ""; ValueData: "Ensemble AO3 Autosave Recovery"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Ensemble.ao3projasve\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Ensemble.ao3projasve\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
