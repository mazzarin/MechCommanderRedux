; Port-only (the personal build): the Windows setup. It installs the program into the player's MechCommander Gold
; folder, for the player alone (no admin, unless Windows protects that folder: then Windows asks once, and the player's
; account is let change it). The folder is the one the game was last pointed at, else the one the original's installer
; recorded. MCX.EXE becomes a launcher that starts Redux, so whatever starts the original gets Redux. What the program
; replaces there (the original's MCX.EXE and MCEditor.exe, a Redux copied in by hand) is kept in "Redux backup" and
; put back by the uninstaller, which leaves the folder as it was. The game's download and the HD question come with
; the content updater.
;
; Built by .github/workflows/windows.yml with Inno Setup 6.7:
;   ISCC /DAppVersionNumber=0.2.1.548 "/DAppVersionShown=0.2.1 (548)" /DBinDir=<the exes' folder>
;        /DDocsDir=<src\docs> /DLicencesFile=<make_licences.ps1's file> /O<output folder>
;        src\installer\windows\MechCommanderRedux.iss

#ifndef AppVersionNumber
  #error Pass /DAppVersionNumber=<major.minor.patch.build> (MC_VERSION in MCVersion.h)
#endif
#ifndef AppVersionShown
  #error Pass /DAppVersionShown=<the version as the game shows it> (MC_VERSION_SHOWN in MCVersion.h)
#endif
#ifndef BinDir
  #error Pass /DBinDir=<the folder holding MCRedux.exe, SDL3.dll and MCEditor.exe>
#endif
#ifndef DocsDir
  #error Pass /DDocsDir=<src\docs>
#endif
#ifndef LicencesFile
  #error Pass /DLicencesFile=<the licences file make_licences.ps1 writes>
#endif

#define AppName "MechCommander Redux"
#define ProjectUrl "https://github.com/mazzarin/MechCommanderRedux"
; The files the setup puts in the game folder.
#define GameExe "MCRedux.exe"
#define SdlDll "SDL3.dll"
#define EditorExe "MCEditor.exe"
; The launcher (src/apps/MCXLauncher), in the original's place.
#define LauncherExe "MCX.EXE"
; Not README.txt: the original's Readme.txt is in the same folder, and Windows doesn't tell the two names apart.
#define ReadmeName "MechCommander Redux README.txt"
#define LicencesName "MechCommander Redux licences.txt"
; The files the first setups put in the game folder, before each install recorded its own (RegisterPreviousData).
#define FirstProgramFiles "|MCRedux.exe|SDL3.dll|MCEditor.exe|MechCommander Redux README.txt|"
#define ProgramFilesData "ProgramFiles"
; Where the files the program replaced are kept, in the game folder.
#define BackupFolderName "Redux backup"
; What makes a folder the game's (as the game and the editor judge a folder the player names).
#define GameDataFile "SYSTEM.CFG"
; The folder the game was last pointed at: the user folder's gamefolder.txt (MCGameFolder::Remember), UTF-8.
#define RememberedFolderFile "MechCommander Redux\gamefolder.txt"
; Where the original's installer (32-bit, so under WOW6432Node) recorded the game folder, as Path: its own key, which
; the CD's Mission Editor setup reads, and DirectPlay's entry for lobbies, which MechCommander Online writes too.
#define OriginalKey "Software\FASA Interactive\MechCommander Expansion"
#define LobbyKey "Software\Microsoft\DirectPlay\Applications\MechCommander Expansion 1.0"
#define FolderValue "Path"
; The player's compatibility settings for a program, by its path (the Compatibility tab's, for the player alone).
#define LayersKey "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"
; Where the setup keeps the settings it took off the original's MCX.EXE, by its path, until the uninstall.
#define ProductKey "Software\MechCommander Redux"
#define KeptLayersKey ProductKey + "\Original compatibility settings"

[Setup]
; Never change the AppId: Windows' Apps list, updates and the uninstaller find the install by it.
AppId={{603FB38F-BA9C-4011-9D9E-1B9A52D73050}
AppName={#AppName}
AppVersion={#AppVersionShown}
AppVerName={#AppName} {#AppVersionShown}
AppPublisherURL={#ProjectUrl}
AppSupportURL={#ProjectUrl}/issues
VersionInfoVersion={#AppVersionNumber}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
; For the player alone: the uninstall entry in HKCU, the shortcuts in the player's Start menu.
PrivilegesRequired=lowest
; The game folder is the player's to give. DefaultDirName only satisfies Inno: InitializeWizard replaces it.
DefaultDirName={autopf}\{#AppName}
AppendDefaultDirName=no
DirExistsWarning=no
EnableDirDoesntExistWarning=no
; Installed already: the setup updates that install in its folder, without asking for one.
DisableDirPage=auto
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; A running game is closed (Windows' Restart Manager) before its files are replaced.
CloseApplications=yes
RestartApplications=no
SetupIconFile={#SourcePath}..\..\res\MCRedux.ico
UninstallDisplayIcon={app}\{#GameExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
SetupLogging=yes
OutputBaseFilename=MechCommander-Redux-Setup

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WizardSelectDir=Your MechCommander Gold
SelectDirDesc=Where is MechCommander Gold installed?
SelectDirLabel3={#AppName} plays your copy of MechCommander Gold and installs into its folder, the one with {#GameDataFile}.
ReadyMemoDir=MechCommander Gold folder:

[CustomMessages]
ChooseGameFolder=Choose the folder MechCommander Gold is installed in, the one with {#GameDataFile}.
NotGameFolder=%1 has no {#GameDataFile}, so it isn't a MechCommander Gold folder.
FolderProtected=Windows protects %1. Setup will ask Windows to let you change it.
FolderStillProtected=Setup couldn't get permission to change %1.
BackupFailed=Setup couldn't copy %1 to %2.
RestoreFailed=These files couldn't go back into your MechCommander Gold folder, so they're still in %1:%2
PlayGame=Play {#AppName}
EditorShortcut={#AppName} Mission Editor

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#BinDir}\{#GameExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\{#SdlDll}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\{#EditorExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\MCX.exe"; DestDir: "{app}"; DestName: "{#LauncherExe}"; Flags: ignoreversion
Source: "{#DocsDir}\README.txt"; DestDir: "{app}"; DestName: "{#ReadmeName}"; Flags: ignoreversion
Source: "{#LicencesFile}"; DestDir: "{app}"; DestName: "{#LicencesName}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#GameExe}"; WorkingDir: "{app}"
Name: "{autoprograms}\{cm:EditorShortcut}"; Filename: "{app}\{#EditorExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#GameExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#GameExe}"; WorkingDir: "{app}"; Description: "{cm:PlayGame}"; Flags: postinstall nowait skipifsilent

[Code]
const
  // Written and deleted to learn whether the player can change the folder.
  WriteTestName = 'MechCommander Redux setup.tmp';

var
  // The folder whose files were copied into its backup this run, which ones, and whether the install finished: a setup
  // that stops part way drops its copies (DropCopies).
  CopiedFolder: String;
  CopiedNames: TArrayOfString;
  Installed: Boolean;

// The names of the files [Files] puts in the game folder.
function ProgramFileNames: TArrayOfString;
begin
  SetArrayLength(Result, 6);
  Result[0] := '{#GameExe}';
  Result[1] := '{#SdlDll}';
  Result[2] := '{#EditorExe}';
  Result[3] := '{#LauncherExe}';
  Result[4] := '{#ReadmeName}';
  Result[5] := '{#LicencesName}';
end;

// Names as one string, each between bars: '|a|b|'.
function JoinedNames(const Names: TArrayOfString): String;
var
  I: Integer;
begin
  Result := '|';

  for I := 0 to GetArrayLength(Names) - 1 do
    Result := Result + Names[I] + '|';
end;

function IsListed(const Joined, Name: String): Boolean;
begin
  Result := Pos('|' + Lowercase(Name) + '|', Lowercase(Joined)) > 0;
end;

function IsGameFolder(const Folder: String): Boolean;
begin
  Result := (Folder <> '') and FileExists(AddBackslash(Folder) + '{#GameDataFile}');
end;

function BackupFolder(const Folder: String): String;
begin
  Result := AddBackslash(Folder) + '{#BackupFolderName}';
end;

procedure Append(var Names: TArrayOfString; const Name: String);
var
  Count: Integer;
begin
  Count := GetArrayLength(Names);
  SetArrayLength(Names, Count + 1);
  Names[Count] := Name;
end;

// Folder, without spaces around it or a trailing backslash, when it's a game folder; else empty.
function GameFolderOrNone(const Folder: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Trim(Folder));

  if not IsGameFolder(Result) then
    Result := '';
end;

// The game folder the game was last pointed at, when it's still one; else empty.
function RememberedGameFolder: String;
var
  Lines: TArrayOfString;
begin
  Result := '';

  // Nested: the line is read only once the file is known to have one.
  if LoadStringsFromFile(ExpandConstant('{userappdata}\{#RememberedFolderFile}'), Lines) then
    if GetArrayLength(Lines) > 0 then
      Result := GameFolderOrNone(Lines[0]);
end;

// The game folder the original's installer recorded under Key, when it's still one; else empty.
function RecordedGameFolder(const Key: String): String;
var
  Folder: String;
begin
  Result := '';

  if RegQueryStringValue(HKLM32, Key, '{#FolderValue}', Folder) then
    Result := GameFolderOrNone(Folder);
end;

procedure InitializeWizard;
var
  Folder: String;
begin
  // An install keeps its folder (its folder page is skipped), and /DIR= names one. Otherwise the folder the game was
  // last pointed at, else the one the original's installer recorded, else none: the setup has no folder of its own.
  if (WizardForm.PrevAppDir <> '') or (ExpandConstant('{param:DIR|}') <> '') then
    Exit;

  Folder := RememberedGameFolder;

  if Folder = '' then
    Folder := RecordedGameFolder('{#OriginalKey}');

  if Folder = '' then
    Folder := RecordedGameFolder('{#LobbyKey}');

  WizardForm.DirEdit.Text := Folder;
end;

function CanChange(const Folder: String): Boolean;
var
  TestFile: String;
begin
  TestFile := AddBackslash(Folder) + WriteTestName;
  Result := SaveStringToFile(TestFile, '', False);

  if Result and not DeleteFile(TestFile) then
    Log(Format('Couldn''t delete the write test %s', [TestFile]));
end;

// Folder quoted for a command line: a backslash just before the closing quote would escape it.
function QuotedFolder(const Folder: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Folder);

  if Copy(Result, Length(Result), 1) = '\' then
    Result := Result + '.';

  Result := '"' + Result + '"';
end;

// Asks Windows, once, to let the player's account change Folder and everything in it; whether it now can.
function GetPermission(const Folder: String): Boolean;
var
  Account: String;
  ErrorCode: Integer;
begin
  Result := False;

  if SuppressibleMsgBox(FmtMessage(CustomMessage('FolderProtected'), [Folder]), mbConfirmation, MB_OKCANCEL,
                        IDOK) <> IDOK then
    Exit;

  // The player's account, named here: the elevated icacls may run as another account (an administrator's).
  Account := GetEnv('USERDOMAIN') + '\' + GetUserNameString;

  if not ShellExec('runas', ExpandConstant('{sys}\icacls.exe'),
                   QuotedFolder(Folder) + ' /grant "' + Account + ':(OI)(CI)M"', '', SW_HIDE,
                   ewWaitUntilTerminated, ErrorCode) then
    Log(Format('icacls for %s on %s didn''t run: %s', [Account, Folder, SysErrorMessage(ErrorCode)]));

  Result := CanChange(Folder);

  if not Result then
    SuppressibleMsgBox(FmtMessage(CustomMessage('FolderStillProtected'), [Folder]), mbError, MB_OK, IDOK);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Folder: String;
begin
  Result := True;

  if CurPageID <> wpSelectDir then
    Exit;

  Folder := WizardDirValue;

  if Folder = '' then
  begin
    SuppressibleMsgBox(CustomMessage('ChooseGameFolder'), mbError, MB_OK, IDOK);
    Result := False;
  end
  else if not IsGameFolder(Folder) then
  begin
    SuppressibleMsgBox(FmtMessage(CustomMessage('NotGameFolder'), [Folder]), mbError, MB_OK, IDOK);
    Result := False;
  end
  else if not CanChange(Folder) then
    Result := GetPermission(Folder);
end;

// Moves Name from Folder's backup back into Folder, unless a file of that name is there now.
function PutBack(const Folder, Name: String): Boolean;
var
  Source, Target: String;
begin
  Source := AddBackslash(BackupFolder(Folder)) + Name;
  Target := AddBackslash(Folder) + Name;
  Result := not FileExists(Target) and RenameFile(Source, Target);

  if Result then
    Log(Format('Put %s back as %s', [Source, Target]))
  else
    Log(Format('Couldn''t put %s back as %s', [Source, Target]));
end;

// Drops the copies this run made in the backup, once the files they were made from are as they were: a copy whose
// file is back in place (Setup's rollback restores what it replaced) is deleted, one whose file is missing is put
// back, and one whose file differs stays in the backup.
procedure DropCopies;
var
  Original, Kept: String;
  Same: Boolean;
  I: Integer;
begin
  for I := 0 to GetArrayLength(CopiedNames) - 1 do
  begin
    Original := AddBackslash(CopiedFolder) + CopiedNames[I];
    Kept := AddBackslash(BackupFolder(CopiedFolder)) + CopiedNames[I];

    if not FileExists(Original) then
      PutBack(CopiedFolder, CopiedNames[I])
    else
    begin
      try
        Same := GetSHA256OfFile(Original) = GetSHA256OfFile(Kept);
      except
        Log(Format('Couldn''t compare %s with %s: %s', [Original, Kept, GetExceptionMessage]));
        Same := False;
      end;

      if not Same then
        Log(Format('Kept %s: %s differs from it', [Kept, Original]))
      else if not DeleteFile(Kept) then
        Log(Format('Couldn''t delete the copy %s', [Kept]));
    end;
  end;

  if CopiedFolder <> '' then
    RemoveDir(BackupFolder(CopiedFolder));

  SetArrayLength(CopiedNames, 0);
  CopiedFolder := '';
end;

// The next setup learns which files this install put in the game folder.
procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, '{#ProgramFilesData}', JoinedNames(ProgramFileNames));
end;

// Before the files go in: the ones they replace are copied into the backup, unless an earlier install put them there.
// Copied, not moved: the originals stay where Windows' Restart Manager finds a running game by them, and Setup's
// rollback puts them back if the install fails.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Names: TArrayOfString;
  Folder, Source, Target, PutThereBefore: String;
  I: Integer;
begin
  Result := '';
  // A second call (the player went back) starts afresh.
  DropCopies;
  Folder := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));

  if CompareText(RemoveBackslashUnlessRoot(WizardForm.PrevAppDir), Folder) = 0 then
    PutThereBefore := GetPreviousData('{#ProgramFilesData}', '{#FirstProgramFiles}')
  else
    PutThereBefore := '';

  CopiedFolder := Folder;
  Names := ProgramFileNames;

  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    Source := AddBackslash(Folder) + Names[I];
    Target := AddBackslash(BackupFolder(Folder)) + Names[I];

    // A backup already there is the folder's from before an earlier install, so it stays the one put back.
    if FileExists(Source) and not FileExists(Target) and not IsListed(PutThereBefore, Names[I]) then
    begin
      if not ForceDirectories(BackupFolder(Folder)) or not FileCopy(Source, Target, True) then
      begin
        Result := FmtMessage(CustomMessage('BackupFailed'), [Source, Target]);
        // A copy cut short isn't the file: it mustn't be taken for an earlier install's backup.
        DeleteFile(Target);
        DropCopies;
        Exit;
      end;

      Append(CopiedNames, Names[I]);
      Log(Format('Copied %s to %s', [Source, Target]));
    end;
  end;
end;

// MCX.EXE's path in Folder: the name its compatibility settings have under the Layers key.
function LauncherPath(const Folder: String): String;
begin
  Result := AddBackslash(RemoveBackslashUnlessRoot(Folder)) + '{#LauncherExe}';
end;

// Takes the player's compatibility settings for the original's MCX.EXE (Windows 98, 256 colours, 640x480, run as
// administrator) off its path, now the launcher's, and keeps them for the uninstall: Windows would apply them to the
// launcher. Settings put on the launcher after an earlier install go too; the kept ones stay the original's.
procedure LiftCompatibility(const Folder: String);
var
  Path, Layers, Kept: String;
begin
  Path := LauncherPath(Folder);

  if not RegQueryStringValue(HKCU, '{#LayersKey}', Path, Layers) then
    Exit;

  if not RegQueryStringValue(HKCU, '{#KeptLayersKey}', Path, Kept) then
  begin
    // Not kept, they stay on MCX.EXE: the launcher runs with them rather than the original losing them.
    if not RegWriteStringValue(HKCU, '{#KeptLayersKey}', Path, Layers) then
    begin
      Log(Format('Couldn''t keep %s''s compatibility settings (%s); left on it', [Path, Layers]));
      Exit;
    end;
  end;

  if RegDeleteValue(HKCU, '{#LayersKey}', Path) then
    Log(Format('Took %s''s compatibility settings (%s) off it', [Path, Layers]))
  else
    Log(Format('Couldn''t take %s''s compatibility settings (%s) off it', [Path, Layers]));
end;

// Gives the original's MCX.EXE back the compatibility settings LiftCompatibility kept.
procedure RestoreCompatibility(const Folder: String);
var
  Path, Kept: String;
begin
  Path := LauncherPath(Folder);

  if not RegQueryStringValue(HKCU, '{#KeptLayersKey}', Path, Kept) then
    Exit;

  if not RegWriteStringValue(HKCU, '{#LayersKey}', Path, Kept) then
  begin
    Log(Format('Couldn''t give %s its compatibility settings (%s) back; still kept', [Path, Kept]));
    Exit;
  end;

  Log(Format('Gave %s its compatibility settings (%s) back', [Path, Kept]));
  RegDeleteValue(HKCU, '{#KeptLayersKey}', Path);
  RegDeleteKeyIfEmpty(HKCU, '{#KeptLayersKey}');
  RegDeleteKeyIfEmpty(HKCU, '{#ProductKey}');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    Installed := True;
    LiftCompatibility(ExpandConstant('{app}'));
  end;
end;

procedure DeinitializeSetup;
begin
  if not Installed then
    DropCopies;
end;

// After the program's files are gone: everything in the backup goes back, so the folder is as it was, and the
// original's MCX.EXE gets its compatibility settings back.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Folder: String;
  FindRec: TFindRec;
  Names: TArrayOfString;
  Left: String;
  I: Integer;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;

  Folder := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));

  // The names first: moving files out while the folder is listed could skip some.
  if FindFirst(AddBackslash(BackupFolder(Folder)) + '*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
          Append(Names, FindRec.Name);
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;

  Left := '';

  for I := 0 to GetArrayLength(Names) - 1 do
    if not PutBack(Folder, Names[I]) then
      Left := Left + #13#10 + Names[I];

  RemoveDir(BackupFolder(Folder));
  RestoreCompatibility(Folder);

  if Left <> '' then
    SuppressibleMsgBox(FmtMessage(CustomMessage('RestoreFailed'), [BackupFolder(Folder), Left]), mbInformation,
                       MB_OK, IDOK);
end;
