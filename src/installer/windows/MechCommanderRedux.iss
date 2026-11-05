; Port-only (the personal build): the Windows setup (SPEC-install-and-updates.md's Install, The player's copy and
; Uninstalling). For the player alone (no admin, unless Windows protects the folder: then Windows asks once, and the
; player's account is let change it), it asks two things beside the folder: the game, downloaded into a folder of its
; own (by default %LOCALAPPDATA%\Programs\MechCommander Redux) or the player's copy, which Redux installs into and
; takes over (the folder the game was last pointed at, else the one the original's installer recorded); and HD,
; ticked. It puts the program in, leaves the answers for the game (Redux update\sets.json), and starts the game,
; whose first start downloads what the folder lacks: the setup itself downloads and copies no content. In a copy taken
; over, MCX.EXE becomes a launcher that starts Redux, so whatever starts the original gets Redux; what the program
; replaces there (the original's MCX.EXE and MCEditor.exe, a Redux copied in by hand) is kept in "Redux backup".
;
; The uninstaller has the game remove its content first (MCRedux --remove-content: the files updates put there),
; then removes the program and puts back what's in the backup, so a copy taken over is as it was. A program update is
; this setup run silently by the game (main/MCUpdater.h's Windows backend): it waits for the game that started it to
; end, closes any other copy running (Windows' Restart Manager), and starts the game again when asked.
;
; Built by .github/workflows/windows.yml with Inno Setup 6.7:
;   ISCC /DAppVersionNumber=0.2.1.548 "/DAppVersionShown=0.2.1 (548)" /DBinDir=<the exes' folder>
;        /DDocsDir=<src\docs> /DLicencesFile=<make_licences.ps1's file> [/DGameBytes=<n> /DHdBytes=<n>]
;        /O<output folder> src\installer\windows\MechCommanderRedux.iss
; GameBytes and HdBytes are the release's sets' sizes, which make_release.py's setup-sizes prints from its content
; list; a build without them (a work branch's) shows no sizes.
;
; Silently: /DIR=<folder>, /GAME=DOWNLOAD or COPY (by default COPY for a folder holding the game, else DOWNLOAD), and
; /HD=NO to leave HD out.

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
#ifndef GameBytes
  #define GameBytes "0"
#endif
#ifndef HdBytes
  #define HdBytes "0"
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
; The sets the player chose, for the game (update/MCContentUpdater.h's ChosenFile), in the install folder.
#define ChosenFolder "Redux update"
#define ChosenFile "sets.json"
; What the game sets when it runs this setup as an update (MCSetupBackend): its process, which the setup waits for, and
; whether to start the game again once installed (RESTART).
#define UpdatePidVariable "MCREDUX_UPDATE_PID"
#define UpdateRestartVariable "MCREDUX_UPDATE_RESTART"
; How long an update waits for the game that started it to end, in milliseconds.
#define GameEndWait "60000"

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
; For the player alone, {autopf} is %LOCALAPPDATA%\Programs: the download's folder. The player's copy is its own.
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
WizardSelectDir=The game
SelectDirDesc=Where should {#AppName} play MechCommander Gold from?
SelectDirLabel3={#AppName} downloads the game into a folder of its own, or installs into your copy and plays it.

[CustomMessages]
DownloadGame=&Download it
UseMyCopy=&Use my copy (its folder holds {#GameDataFile})
HdChoice=Redux &HD art, sound and movies
GameSize=%1 (%2 GB)
HdSize=%1 (%2 GB more)
Totals=About %1 GB with HD, %2 GB without.
ChooseFolder=Choose a folder for the game.
ChooseGameFolder=Choose the folder MechCommander Gold is installed in, the one with {#GameDataFile}.
NotGameFolder=%1 has no {#GameDataFile}, so it isn't a MechCommander Gold folder.
SyncedFolder=OneDrive syncs %1. Choose a folder outside it: the game's files would fill it, and OneDrive would hold them while it syncs.
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
; An update the game ran for RESTART: the new version starts by itself.
Filename: "{app}\{#GameExe}"; WorkingDir: "{app}"; Flags: nowait; Check: StartAfterUpdate

[UninstallRun]
; First, the game removes its content (the files updates put here; the setup never owns content), leaving the
; player's own and what a copy taken over had. Then the program goes, and the backup comes back.
Filename: "{app}\{#GameExe}"; Parameters: "--remove-content --data ""{app}"""; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveContent"

[Code]
const
  // Written and deleted to learn whether the player can change the folder.
  WriteTestName = 'MechCommander Redux setup.tmp';
  // The bytes in a GB, as Windows shows sizes.
  BytesPerGB = 1073741824;
  // Waiting for a process to end: OpenProcess's SYNCHRONIZE, and WaitForSingleObject's WAIT_OBJECT_0.
  WaitAccess = $00100000;
  WaitEnded = 0;
  // The answers on the command line (silent installs).
  CopyAnswer = 'COPY';
  DownloadAnswer = 'DOWNLOAD';
  NoAnswer = 'NO';

var
  // The folder whose files were copied into its backup this run, which ones, and whether the install finished: a setup
  // that stops part way drops its copies (DropCopies).
  CopiedFolder: String;
  CopiedNames: TArrayOfString;
  Installed: Boolean;
  // The folder page's questions: the game downloaded or the player's copy, and HD; and the folder each choice shows.
  GameDownload, GameCopy: TNewRadioButton;
  HdBox: TNewCheckBox;
  DownloadFolder, CopyFolder: String;
  CopyShown: Boolean;

function OpenProcess(Access: Cardinal; Inherit: Integer; ProcessId: Cardinal): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function WaitForSingleObject(Handle: THandle; Milliseconds: Cardinal): Cardinal;
  external 'WaitForSingleObject@kernel32.dll stdcall';
function CloseHandle(Handle: THandle): Integer;
  external 'CloseHandle@kernel32.dll stdcall';

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

// A size in bytes (the build's), in GB to a tenth, as shown: "0.6".
function ShownGB(const Bytes: String): String;
var
  Tenths: Int64;
begin
  Tenths := (StrToInt64Def(Bytes, 0) * 10 + BytesPerGB div 2) div BytesPerGB;
  Result := IntToStr(Tenths div 10) + '.' + IntToStr(Tenths mod 10);
end;

function SizesKnown: Boolean;
begin
  Result := (StrToInt64Def('{#GameBytes}', 0) > 0) and (StrToInt64Def('{#HdBytes}', 0) > 0);
end;

// The folder page's line under the folder: what the game takes on disk, with HD and without.
procedure ShowTotals;
begin
  WizardForm.DiskSpaceLabel.Visible := SizesKnown;

  // A line of [Code] can't start with "[": Inno takes it for a section's.
  if SizesKnown then
    WizardForm.DiskSpaceLabel.Caption := FmtMessage(CustomMessage('Totals'), [
      ShownGB(IntToStr(StrToInt64('{#GameBytes}') + StrToInt64('{#HdBytes}'))), ShownGB('{#GameBytes}')]);
end;

// The player picked the other answer to the game: the folder box shows that answer's folder, keeping the one edited.
procedure GameChosen(Sender: TObject);
begin
  if GameCopy.Checked = CopyShown then
    Exit;

  if CopyShown then
    CopyFolder := WizardForm.DirEdit.Text
  else
    DownloadFolder := WizardForm.DirEdit.Text;

  CopyShown := GameCopy.Checked;

  if CopyShown then
    WizardForm.DirEdit.Text := CopyFolder
  else
    WizardForm.DirEdit.Text := DownloadFolder;
end;

// The folder page's questions, above and below the folder: the game, and HD.
procedure AddQuestions;
var
  Page: TWizardPage;
  Top: Integer;
  DownloadCaption, HdCaption: String;
begin
  DownloadCaption := CustomMessage('DownloadGame');
  HdCaption := CustomMessage('HdChoice');

  if SizesKnown then
  begin
    DownloadCaption := FmtMessage(CustomMessage('GameSize'), [DownloadCaption, ShownGB('{#GameBytes}')]);
    HdCaption := FmtMessage(CustomMessage('HdSize'), [HdCaption, ShownGB('{#HdBytes}')]);
  end;

  Page := PageFromID(wpSelectDir);
  Top := WizardForm.SelectDirBrowseLabel.Top;
  WizardForm.SelectDirBrowseLabel.Visible := False;

  GameDownload := TNewRadioButton.Create(WizardForm);
  GameDownload.Parent := Page.Surface;
  GameDownload.Left := WizardForm.DirEdit.Left;
  GameDownload.Top := Top;
  GameDownload.Width := Page.SurfaceWidth - GameDownload.Left;
  GameDownload.Caption := DownloadCaption;

  GameCopy := TNewRadioButton.Create(WizardForm);
  GameCopy.Parent := Page.Surface;
  GameCopy.Left := GameDownload.Left;
  GameCopy.Top := GameDownload.Top + GameDownload.Height + ScaleY(4);
  GameCopy.Width := GameDownload.Width;
  GameCopy.Caption := CustomMessage('UseMyCopy');

  WizardForm.DirEdit.Top := GameCopy.Top + GameCopy.Height + ScaleY(10);
  WizardForm.DirBrowseButton.Top := WizardForm.DirEdit.Top;

  HdBox := TNewCheckBox.Create(WizardForm);
  HdBox.Parent := Page.Surface;
  HdBox.Left := GameDownload.Left;
  HdBox.Top := WizardForm.DirEdit.Top + WizardForm.DirEdit.Height + ScaleY(16);
  HdBox.Width := GameDownload.Width;
  HdBox.Caption := HdCaption;
end;

procedure InitializeWizard;
var
  Given, Answer: String;
begin
  AddQuestions;
  HdBox.Checked := Uppercase(ExpandConstant('{param:HD|}')) <> NoAnswer;
  DownloadFolder := ExpandConstant('{autopf}\{#AppName}');
  // The player's copy: the folder the game was last pointed at, else the one the original's installer recorded.
  CopyFolder := RememberedGameFolder;

  if CopyFolder = '' then
    CopyFolder := RecordedGameFolder('{#OriginalKey}');

  if CopyFolder = '' then
    CopyFolder := RecordedGameFolder('{#LobbyKey}');

  // /DIR= names the folder, for the answer /GAME= gives, or by default the one the folder fits. Otherwise the player's
  // copy when one was found, else the download. (An install keeps its folder: its folder page is skipped.)
  Given := ExpandConstant('{param:DIR|}');
  Answer := Uppercase(ExpandConstant('{param:GAME|}'));

  if Answer = '' then
  begin
    if ((Given <> '') and IsGameFolder(Given)) or ((Given = '') and (CopyFolder <> '')) then
      Answer := CopyAnswer
    else
      Answer := DownloadAnswer;
  end;

  CopyShown := Answer = CopyAnswer;

  if Given <> '' then
  begin
    if CopyShown then
      CopyFolder := Given
    else
      DownloadFolder := Given;
  end;

  GameCopy.Checked := CopyShown;
  GameDownload.Checked := not CopyShown;

  if WizardForm.PrevAppDir = '' then
  begin
    if CopyShown then
      WizardForm.DirEdit.Text := CopyFolder
    else
      WizardForm.DirEdit.Text := DownloadFolder;
  end;

  GameDownload.OnClick := @GameChosen;
  GameCopy.OnClick := @GameChosen;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectDir then
    ShowTotals;
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

// The player's account, named here: an elevated command may run as another account (an administrator's).
function PlayerAccount: String;
begin
  Result := GetEnv('USERDOMAIN') + '\' + GetUserNameString;
end;

// Lets the player's account change Folder and everything in it, making it first if need be; elevated by Windows' prompt
// unless Setup already is. Whether the player can now change it.
function Grant(const Folder: String): Boolean;
var
  Verb, Command: String;
  ErrorCode: Integer;
begin
  if IsAdmin then
    Verb := ''
  else
    Verb := 'runas';

  Command := '/c mkdir ' + QuotedFolder(Folder) + ' 2>nul & "' + ExpandConstant('{sys}\icacls.exe') + '" ' +
             QuotedFolder(Folder) + ' /grant "' + PlayerAccount + ':(OI)(CI)M"';

  if not ShellExec(Verb, ExpandConstant('{cmd}'), Command, '', SW_HIDE, ewWaitUntilTerminated, ErrorCode) then
    Log(Format('icacls for %s on %s didn''t run: %s', [PlayerAccount, Folder, SysErrorMessage(ErrorCode)]))
  else
    Log(Format('Let %s change %s', [PlayerAccount, Folder]));

  Result := DirExists(Folder) and CanChange(Folder);
end;

// Asks Windows, once, to let the player's account change Folder and everything in it; whether it now can.
function GetPermission(const Folder: String): Boolean;
begin
  Result := False;

  if SuppressibleMsgBox(FmtMessage(CustomMessage('FolderProtected'), [Folder]), mbConfirmation, MB_OKCANCEL,
                        IDOK) <> IDOK then
    Exit;

  Result := Grant(Folder);

  if not Result then
    SuppressibleMsgBox(FmtMessage(CustomMessage('FolderStillProtected'), [Folder]), mbError, MB_OK, IDOK);
end;

// The folder, or the nearest folder holding it that exists: where Setup will make it.
function ExistingFolder(const Folder: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Folder);

  while (Result <> '') and not DirExists(Result) and (ExtractFileDir(Result) <> Result) do
    Result := ExtractFileDir(Result);
end;

// Whether Folder is in the folder OneDrive syncs (the player's own, or a work or school one).
function OneDriveSyncs(const Folder: String): Boolean;
var
  Variables: TArrayOfString;
  Synced: String;
  I: Integer;
begin
  Result := False;
  SetArrayLength(Variables, 3);
  Variables[0] := 'OneDrive';
  Variables[1] := 'OneDriveConsumer';
  Variables[2] := 'OneDriveCommercial';

  for I := 0 to GetArrayLength(Variables) - 1 do
  begin
    Synced := RemoveBackslashUnlessRoot(GetEnv(Variables[I]));

    if (Synced <> '') and ((CompareText(Folder, Synced) = 0) or
                           (CompareText(Copy(Folder, 1, Length(Synced) + 1), AddBackslash(Synced)) = 0)) then
      Result := True;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Folder: String;
begin
  Result := True;

  // An install keeps its folder, whatever it holds by now.
  if (CurPageID <> wpSelectDir) or (WizardForm.PrevAppDir <> '') then
    Exit;

  Folder := RemoveBackslashUnlessRoot(WizardDirValue);

  if Folder = '' then
  begin
    if GameCopy.Checked then
      SuppressibleMsgBox(CustomMessage('ChooseGameFolder'), mbError, MB_OK, IDOK)
    else
      SuppressibleMsgBox(CustomMessage('ChooseFolder'), mbError, MB_OK, IDOK);

    Result := False;
  end
  else if GameCopy.Checked and not IsGameFolder(Folder) then
  begin
    SuppressibleMsgBox(FmtMessage(CustomMessage('NotGameFolder'), [Folder]), mbError, MB_OK, IDOK);
    Result := False;
  end
  else if OneDriveSyncs(Folder) then
  begin
    SuppressibleMsgBox(FmtMessage(CustomMessage('SyncedFolder'), [Folder]), mbError, MB_OK, IDOK);
    Result := False;
  end
  // Run as administrator, Setup can't tell whether the player can change the folder: the game, run as the player,
  // must, to update itself. Otherwise Windows asks only when the player can't.
  else if IsAdmin then
    Result := Grant(Folder)
  else if not CanChange(ExistingFolder(Folder)) then
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

// An update the game ran: waits for that game to end, so its files are free before Windows' Restart Manager looks for
// programs using them (one quitting has no window left to be asked to close). Past the wait, the Restart Manager
// closes it as it does any other copy running.
procedure WaitForGame;
var
  Id: Cardinal;
  Process: THandle;
begin
  Id := StrToIntDef(GetEnv('{#UpdatePidVariable}'), 0);

  if Id = 0 then
    Exit;

  Process := OpenProcess(WaitAccess, 0, Id);

  // Ended already, or never there.
  if Process = 0 then
    Exit;

  if WaitForSingleObject(Process, {#GameEndWait}) = WaitEnded then
    Log(Format('The game that started the update (process %d) has ended', [Id]))
  else
    Log(Format('The game that started the update (process %d) is still running', [Id]));

  CloseHandle(Process);
end;

// Whether the game ran this setup as an update and asked to be started again once it's in (RESTART).
function StartAfterUpdate: Boolean;
begin
  Result := WizardSilent and (GetEnv('{#UpdateRestartVariable}') = '1');
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
  WaitForGame;
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

// A new install leaves the player's answers for the game (update/MCContentUpdater.h's ChosenFile): the game's data
// always, HD when ticked. The game's first start downloads them; HD OPTIONS changes them later.
procedure LeaveAnswers(const Folder: String);
var
  Path, Sets: String;
begin
  Path := AddBackslash(Folder) + '{#ChosenFolder}\{#ChosenFile}';
  Sets := '"game"';

  if HdBox.Checked then
    Sets := Sets + ', "hd"';

  if ForceDirectories(ExtractFileDir(Path)) and SaveStringToFile(Path, '{"format": 1, "sets": [' + Sets + ']}', False) then
    Log(Format('The answers for the game: %s', [Sets]))
  else
    Log(Format('Couldn''t write the answers for the game, %s', [Path]));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    Installed := True;
    LiftCompatibility(ExpandConstant('{app}'));

    if WizardForm.PrevAppDir = '' then
      LeaveAnswers(ExpandConstant('{app}'));
  end;
end;

procedure DeinitializeSetup;
begin
  if not Installed then
    DropCopies;
end;

// Moves everything in Folder's backup under Relative ('' for the backup itself) back into Folder at the same place,
// making the folders on its way; the names of what couldn't go back are added to Left.
procedure PutBackAll(const Folder, Relative: String; var Left: String);
var
  FindRec: TFindRec;
  Files, Folders: TArrayOfString;
  Kept: String;
  I: Integer;
begin
  Kept := AddBackslash(BackupFolder(Folder)) + Relative;

  // The names first: moving files out while the folder is listed could skip some.
  if FindFirst(AddBackslash(Kept) + '*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
          Append(Files, Relative + FindRec.Name)
        else if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
          Append(Folders, Relative + FindRec.Name);
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;

  for I := 0 to GetArrayLength(Files) - 1 do
    if not (ForceDirectories(ExtractFileDir(AddBackslash(Folder) + Files[I])) and PutBack(Folder, Files[I])) then
      Left := Left + #13#10 + Files[I];

  for I := 0 to GetArrayLength(Folders) - 1 do
  begin
    PutBackAll(Folder, AddBackslash(Folders[I]), Left);
    RemoveDir(AddBackslash(BackupFolder(Folder)) + Folders[I]);
  end;
end;

// After the game removed its content and the program's files are gone: everything in the backup goes back, so the
// folder is as it was, and the original's MCX.EXE gets its compatibility settings back.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Folder, Left: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;

  Folder := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));
  Left := '';
  PutBackAll(Folder, '', Left);
  RemoveDir(BackupFolder(Folder));
  RestoreCompatibility(Folder);

  if Left <> '' then
    SuppressibleMsgBox(FmtMessage(CustomMessage('RestoreFailed'), [BackupFolder(Folder), Left]), mbInformation,
                       MB_OK, IDOK);
end;
