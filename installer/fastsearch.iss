; FastSearch — Windows installer (Inno Setup 6)
; Build with:  iscc installer\fastsearch.iss
; Or via the helper: powershell installer\build_installer.ps1

#define MyAppName        "FastSearch"
#define MyAppVersion     "0.1.0"
#define MyAppPublisher   "FastSearch"
#define MyAppURL         "https://github.com/Prgmcat/fast-search"
#define MyAppExeName     "fastsearch-gui.exe"
#define MyServerExe      "fastsearch-server.exe"
#define MyCliExe         "fastsearch.exe"
#define MyServiceName    "FastSearchService"
#define SrcRoot          SourcePath + ".."
#define BuildDir         SrcRoot + "\build\Release"

[Setup]
; Stable AppId — never change across versions, upgrades key on this.
AppId={{B2E7F4A1-3C8D-4E5F-A6B1-0D9C8B7E6F54}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
VersionInfoVersion={#MyAppVersion}.0
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile={#SrcRoot}\installer\LICENSE.txt
InfoBeforeFile={#SrcRoot}\installer\readme-before-install.txt
OutputDir={#SrcRoot}\installer\dist
OutputBaseFilename=FastSearch-Setup-{#MyAppVersion}
SetupIconFile={#SrcRoot}\res\fastsearch.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/ultra
SolidCompression=yes
WizardStyle=modern
; x64 only (WebView2 + service code is built for x64)
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Needs admin: installs into Program Files, may register a service.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
; Tell Windows we might change the system PATH.
ChangesEnvironment=yes
DisableProgramGroupPage=auto
CloseApplications=yes
RestartApplications=no
MinVersion=10.0

[Languages]
; ChineseSimplified.isl is not bundled with Inno Setup 6 out of the box
; (only the 29 default languages are); we carry a copy from the upstream
; jrsoftware/issrc repo at installer\ChineseSimplified.isl.
Name: "chinesesimp"; MessagesFile: "ChineseSimplified.isl"
Name: "english";    MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimp.TaskDesktopIcon=创建桌面快捷方式
english.TaskDesktopIcon=Create a &desktop shortcut
chinesesimp.TaskInstallService=作为 Windows 服务安装 (开机自启，推荐)
english.TaskInstallService=Install as a Windows service (auto-start at boot, recommended)
chinesesimp.TaskAddToPath=将命令行工具加入系统 PATH
english.TaskAddToPath=Add command-line tools to system PATH
chinesesimp.GroupStartServerConsole=以控制台启动服务器 (调试)
english.GroupStartServerConsole=Start server in console (debug)
chinesesimp.RunLaunchApp=启动 {#MyAppName}
english.RunLaunchApp=Launch {#MyAppName}
chinesesimp.StatusInstallService=正在注册 {#MyAppName} 服务...
english.StatusInstallService=Registering the {#MyAppName} service...
chinesesimp.StatusStartService=正在启动 {#MyAppName} 服务...
english.StatusStartService=Starting the {#MyAppName} service...

[Tasks]
Name: "desktopicon";     Description: "{cm:TaskDesktopIcon}";    GroupDescription: "{cm:AdditionalIcons}"
Name: "installservice";  Description: "{cm:TaskInstallService}"
Name: "addtopath";       Description: "{cm:TaskAddToPath}";      Flags: unchecked

[Files]
Source: "{#BuildDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\{#MyServerExe}";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\{#MyCliExe}";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcRoot}\res\fastsearch.ico";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcRoot}\README.md";           DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\fastsearch.ico"
Name: "{group}\{cm:GroupStartServerConsole}"; Filename: "{app}\{#MyServerExe}"; WorkingDir: "{app}"; IconFilename: "{app}\fastsearch.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\fastsearch.ico"; Tasks: desktopicon

[Registry]
; Add to system PATH when the user opts in. Idempotency & removal are
; handled in [Code] so the registry key stays clean if the user reinstalls.
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; \
    Check: NeedsAddPath(ExpandConstant('{app}')); Tasks: addtopath

[Run]
; Register + start the Windows service when requested. The server exe
; implements both "--install" and "--service"; see src/server_main.cpp.
Filename: "{app}\{#MyServerExe}"; Parameters: "--install"; \
    StatusMsg: "{cm:StatusInstallService}"; Flags: runhidden waituntilterminated; \
    Tasks: installservice
Filename: "{sys}\sc.exe"; Parameters: "start {#MyServiceName}"; \
    StatusMsg: "{cm:StatusStartService}"; Flags: runhidden waituntilterminated; \
    Tasks: installservice
; Optional: launch the GUI at the end of the wizard.
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:RunLaunchApp}"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
; Make sure the GUI isn't holding the exe open.
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#MyAppExeName} /T"; \
    Flags: runhidden; RunOnceId: "KillFastSearchGui"
; Stop + unregister the service (safe if it was never installed; the
; server just prints "Service not found" and returns non-zero — that's
; fine, [UninstallRun] failures don't abort the uninstaller).
Filename: "{sys}\sc.exe"; Parameters: "stop {#MyServiceName}"; \
    Flags: runhidden; RunOnceId: "StopFastSearchService"
Filename: "{app}\{#MyServerExe}"; Parameters: "--uninstall"; \
    Flags: runhidden waituntilterminated skipifdoesntexist; \
    RunOnceId: "UninstallFastSearchService"

[UninstallDelete]
; Leave the user's index.db / logs alone by default — deleting them here
; would be data-loss on reinstalls. If the user wants a full wipe they
; can remove %ProgramData%\FastSearch manually.
Type: filesandordirs; Name: "{app}\fastsearch-gui.exe.WebView2"

[Code]
{ ── system PATH handling ───────────────────────────────────────────── }

function NeedsAddPath(Param: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE,
      'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
      'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  { Wrap both sides with ';' so we match whole-entry, not substring. }
  Result := Pos(';' + Uppercase(Param) + ';',
                ';' + Uppercase(OrigPath) + ';') = 0;
end;

procedure RemoveFromPath(PathToRemove: string);
var
  OrigPath, NewPath, Needle, Hay: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE,
      'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
      'Path', OrigPath) then
    exit;

  NewPath := OrigPath;
  Hay := ';' + Uppercase(NewPath) + ';';
  Needle := ';' + Uppercase(PathToRemove) + ';';
  P := Pos(Needle, Hay);
  if P = 0 then exit;

  { Preserve original casing by deleting from NewPath with the same offset. }
  if P = 1 then
    Delete(NewPath, 1, Length(PathToRemove) + 1) { leading: "X;..." -> "..." }
  else
    Delete(NewPath, P - 1, Length(PathToRemove) + 1); { middle / trailing: ";X..." }

  RegWriteExpandStringValue(HKEY_LOCAL_MACHINE,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', NewPath);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
end;
