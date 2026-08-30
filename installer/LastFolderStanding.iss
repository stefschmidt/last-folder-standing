; Last Folder Standing -- per-user installer.
;
; No admin rights anywhere: files go to %LOCALAPPDATA%\Programs, all registration
; is HKCU. Explorer is never killed; the entry appears in new file dialogs right
; away and in Explorer itself after the next restart.

#define AppName        "Last Folder Standing"
#define AppShortName   "LastFolderStanding"
#define AppPublisher   "Last Folder Standing"

#ifndef AppVersion
  #define AppVersion   "0.1.0"
#endif
#ifndef BinDir
  #define BinDir       "..\build\bin\Release"
#endif

[Setup]
AppId={{9C0D5E7A-4F2B-4E5C-9E51-2B7D8A6F1C34}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion}

; NEVER touch the user's running programs. Inno's default is to ask the Restart
; Manager to shut down whatever holds a file we want to replace -- and Explorer
; holds our extension DLL, so the default kills the user's desktop mid-install.
; PrepareToInstall renames the old DLL out of the way instead.
CloseApplications=no
RestartApplications=no

; Per-user install: no elevation prompt, no machine-wide state.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={localappdata}\Programs\{#AppShortName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto

; The shell extension is x64; a 32-bit install would register an unusable DLL.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

OutputDir=Output
OutputBaseFilename=LastFolderStanding-{#AppVersion}-setup
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\LFS.Settings.exe
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#BinDir}\LFS.Monitor.exe";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\LFS.Settings.exe";       DestDir: "{app}"; Flags: ignoreversion
; No restartreplace here: it writes to PendingFileRenameOperations under HKLM,
; which needs admin and silently does nothing in a per-user install. The old DLL
; is renamed away in PrepareToInstall instead, which works while it is loaded.
Source: "{#BinDir}\LFS.ShellExtension.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\assets\app.ico";                DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md";                     DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE";                       DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
; One entry only: the settings window is the sole thing worth launching by hand.
Name: "{userprograms}\{#AppName}"; Filename: "{app}\LFS.Settings.exe"; \
    IconFilename: "{app}\app.ico"

[Registry]
; Autostart. Hardcoded on for v1, deliberately not a setting.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "{#AppShortName}"; \
    ValueData: """{app}\LFS.Monitor.exe"""; Flags: uninsdeletevalue

[Run]
; DllRegisterServer writes HKCU only, so plain regsvr32 without elevation works.
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\LFS.ShellExtension.dll"""; \
    StatusMsg: "Registering navigation pane entry..."; Flags: runhidden waituntilterminated
Filename: "{app}\LFS.Monitor.exe"; Description: "Start monitoring now"; \
    Flags: nowait postinstall skipifsilent
Filename: "{app}\LFS.Settings.exe"; Description: "Open settings"; \
    Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
; DLLs renamed aside during an update; Explorer has released them by now.
Type: files; Name: "{app}\LFS.ShellExtension.dll.old*"
; Without this the (now empty) program folder survives the uninstall.
Type: dirifempty; Name: "{app}"

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\LFS.ShellExtension.dll"""; \
    RunOnceId: "UnregisterShellExtension"; Flags: runhidden waituntilterminated

[Code]
const
  MonitorWindowClass = 'LFS_MonitorTrayWindow';
  WM_CLOSE_MSG = $0010;

// FindWindowByClassName and PostMessage are built into Inno Setup's Pascal
// script. Declaring them as externals again crashes the installer, because
// FindWindowW actually takes two arguments.

// Asks a running monitor to quit through its tray window, so the files are not
// locked while we replace them. Falls back to just carrying on: the installer
// marks the DLL restartreplace anyway.
procedure StopMonitor();
var
  Window: HWND;
  Waited: Integer;
begin
  Window := FindWindowByClassName(MonitorWindowClass);
  if Window = 0 then
    Exit;

  PostMessage(Window, WM_CLOSE_MSG, 0, 0);

  Waited := 0;
  while (Waited < 3000) and (FindWindowByClassName(MonitorWindowClass) <> 0) do
  begin
    Sleep(100);
    Waited := Waited + 100;
  end;
end;

// Leftovers from an earlier update whose DLL was still loaded. By now Explorer
// has long let go of them, so they usually delete without a fuss.
procedure DeleteStaleFiles(Dir: string);
var
  Found: TFindRec;
begin
  if FindFirst(Dir + '\LFS.ShellExtension.dll.old*', Found) then
  try
    repeat
      DeleteFile(Dir + '\' + Found.Name);
    until not FindNext(Found);
  finally
    FindClose(Found);
  end;
end;

// A DLL that Explorer has mapped cannot be overwritten -- but it can be renamed.
// That is what makes an update possible without shutting down the user's
// desktop, which is what the Restart Manager would otherwise do.
function MoveOldDllAside(): Boolean;
var
  Dll, Stale: string;
  Index, ResultCode: Integer;
begin
  Result := True;
  Dll := ExpandConstant('{app}\LFS.ShellExtension.dll');
  if not FileExists(Dll) then
    Exit;

  // Unregister first: with the CLSID gone, nothing new binds to it.
  Exec(ExpandConstant('{sys}\regsvr32.exe'), '/u /s "' + Dll + '"', '', SW_HIDE,
       ewWaitUntilTerminated, ResultCode);

  // Nobody holds it: plain delete and we are done.
  if DeleteFile(Dll) then
    Exit;

  Index := 0;
  repeat
    Stale := Dll + '.old' + IntToStr(Index);
    Index := Index + 1;
  until (not FileExists(Stale)) or (Index > 100);

  Result := RenameFile(Dll, Stale);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopMonitor();
  DeleteStaleFiles(ExpandConstant('{app}'));

  if MoveOldDllAside() then
    Result := ''
  else
    Result := 'The previous version of LFS.ShellExtension.dll is in use and could not ' +
              'be moved aside. Sign out and back in, then run Setup again.';
end;

function InitializeUninstall(): Boolean;
begin
  StopMonitor();
  Result := True;
end;

// Explorer keeps the extension DLL mapped until it decides to unload it, so the
// program folder often survives the uninstall. The usual fix (restartreplace ->
// PendingFileRenameOperations) needs HKLM and therefore admin, which a per-user
// install does not have. HKCU RunOnce does the same job at the next logon.
procedure ScheduleFolderCleanup(Dir: string);
begin
  // By this point the [Files] entries are gone. If the DLL is still there, it
  // is because Explorer has it mapped -- that is the only case worth scheduling
  // a cleanup for. Doing it unconditionally would flash a console window at the
  // next logon of every uninstall, for nothing.
  if not FileExists(Dir + '\LFS.ShellExtension.dll') then
    Exit;

  RegWriteStringValue(HKEY_CURRENT_USER,
    'Software\Microsoft\Windows\CurrentVersion\RunOnce',
    'LastFolderStandingCleanup',
    'cmd.exe /c rd /s /q "' + Dir + '"');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: string;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;

  ScheduleFolderCleanup(ExpandConstant('{app}'));

  // A silent uninstall must not pop a dialog, so it keeps the data. Removing
  // someone's settings without asking is the wrong default anyway.
  if UninstallSilent then
    Exit;

  DataDir := ExpandConstant('{localappdata}\LastFolderStanding');
  if not DirExists(DataDir) then
    Exit;

  if MsgBox('Remove the folder list and your settings as well?' + #13#10 + #13#10 +
            DataDir, mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    DelTree(DataDir, True, True, True);
end;
