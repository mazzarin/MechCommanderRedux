; Port-only (the personal build): the Windows setup (SPEC-install-and-updates.md's Install, The player's copy and
; Uninstalling). For the player alone (no admin, unless Windows protects the folder: then Windows asks once, and the
; player's account is let change it; so the folder must be the game's own, never a drive, Windows', the programs' or
; another account's, or one holding them), it asks two things beside the folder: the game, downloaded into a folder of
; its own (by default C:\Games\MechCommander Redux HD, else %LOCALAPPDATA%\Programs\MechCommander Redux HD) or the
; player's copy, which Redux HD installs into and takes over (the folder the game was last pointed at, else the one the
; original's installer recorded); and HD, ticked, unless the computer can't run Vulkan, which HD's art and movies need
; (a line under it says they won't show: VulkanLack). It puts the program in, leaves the answers for the game (Redux
; update\sets.json), and, shown, has the game download what the folder lacks before its Finish page (MCRedux
; --get-content, the game's own download in its own window; closed or cut off, the first start carries on): the setup
; itself downloads and copies no content. Windows' Apps list then gets the folder's size. MCX.EXE is the game itself
; under the original's name (a hard link to MCRedux.exe: one file, so Windows and its antivirus see no other program),
; so whatever starts the original gets Redux HD; what the program replaces there (the original's MCX.EXE and
; MCEditor.exe, a Redux copied in by hand) is kept in "Redux backup". MechCommander Online's copy holds the original at
; other resolutions too (MCX1920x1080.EXE): each goes into the backup, and comes back as the game only where a shortcut,
; or the lobby's DirectPlay entry, starts it.
;
; The uninstaller has the game remove its content first (MCRedux --remove-content: the files updates put there),
; then removes the program and puts back what's in the backup, so a copy taken over is as it was. When the game can't
; (a copy still running, an update it can't finish, a file it can't remove), the uninstall says why and stops, so the
; program, the content's one remover, stays to try again. A program update is
; this setup run silently by the game (main/MCUpdater.h's Windows backend): it waits for the game that started it to
; end, closes any other copy running (Windows' Restart Manager), and starts the game again when asked. It updates the
; copy that ran it, never another folder, and adds no shortcut: in the setup's own install, as an install does; in a
; copy it didn't install (the player's own: copied by hand, a mod's folder), the program's files alone, with no
; uninstall entry, shortcut, registry entry or backup (FilesOnly). The game names its folder (/DIR=); an earlier game,
; which doesn't, ran it from its own Redux update\program folder (UpdateFolder).
;
; Built by .github/workflows/windows.yml with Inno Setup 6.7:
;   ISCC /DAppVersionNumber=0.2.1.548 "/DAppVersionShown=0.2.1 (548)" /DBinDir=<the exes' folder>
;        /DDocsDir=<src\docs> /DLicencesFile=<make_licences.ps1's file> /DSetupLevel=<setup_level.py --check>
;        [/DGameBytes=<n> /DHdBytes=<n>] /O<output folder> src\installer\windows\MechCommanderRedux.iss
; SetupLevel is the setup's level (setup-level.txt), which every install records in the game folder: a later release
; whose setup is at the same level puts its program in by the game itself (update/MCProgramSwap.h), without this setup.
; GameBytes and HdBytes are the release's sets' sizes, which make_release.py's setup-sizes prints from its content
; list. A build without them (a work branch's) has no release to download from, so its folder page asks only for the
; player's copy, without HD; silent installs still take their answers.
;
; Silently: /DIR=<folder>, /GAME=DOWNLOAD or COPY (by default COPY for a folder holding the game, else DOWNLOAD), and
; /HD=NO to leave HD out, /HD=YES to have it where the computer can't run Vulkan (which leaves it out by default).

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
#ifndef SetupLevel
  #error Pass /DSetupLevel=<the level setup_level.py --check prints>
#endif
#ifndef GameBytes
  #define GameBytes "0"
#endif
#ifndef HdBytes
  #define HdBytes "0"
#endif

#define AppName "MechCommander Redux HD"
#define ProjectUrl "https://github.com/mazzarin/MechCommanderRedux"
; Where a downloaded game goes by default, on the system drive: games' own place, visible and short, made without an
; administrator's rights on a standard Windows (an account may make folders at the drive's root). Where the account
; can't, the player's own programs folder ({autopf}, %LOCALAPPDATA%\Programs).
#define GamesFolderName "Games"
; The files the setup puts in the game folder.
#define GameExe "MCRedux.exe"
#define SdlDll "SDL3.dll"
#define EditorExe "MCEditor.exe"
; The original's program, which becomes the game under its name (MakeGameName), not one of [Files].
#define OriginalExe "MCX.EXE"
; The original under another name: MechCommander Online's copy has its MCX.EXE patched for wider screens as
; MCX<width>x<height>.EXE (MCX1920x1080.EXE). Each becomes the game too, its original kept in the backup.
#define ResolutionPrefix "MCX"
#define ResolutionSizeSign "x"
#define ResolutionExtension ".EXE"
; Not README.txt: the original's Readme.txt is in the same folder, and Windows doesn't tell the two names apart.
#define ReadmeName AppName + " README.txt"
#define LicencesName AppName + " licences.txt"
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
; The program DirectPlay's lobby entry starts, in that folder.
#define ProgramValue "File"
; The player's compatibility settings for a program, by its path (the Compatibility tab's, for the player alone).
#define LayersKey "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"
; Where the setup keeps the settings it took off the original's MCX.EXE, by its path, until the uninstall.
#define ProductKey "Software\MechCommander Redux"
#define KeptLayersKey ProductKey + "\Original compatibility settings"
; The sets the player chose, for the game (update/MCContentUpdater.h's ChosenFile), in the install folder.
#define ChosenFolder "Redux update"
#define ChosenFile "sets.json"
; The setup's record of its level, and the game's own program updates' folder and journal (update/MCProgramSwap.h).
#define SetupFile "setup.json"
#define SwapFolder "swap"
#define SwapJournal "journal.json"
; What the game sets when it runs this setup as an update (MCSetupBackend): its process, which the setup waits for, and
; whether to start the game again once installed (RESTART).
#define UpdatePidVariable "MCREDUX_UPDATE_PID"
#define UpdateRestartVariable "MCREDUX_UPDATE_RESTART"
; How long an update waits for the game that started it to end, in milliseconds.
#define GameEndWait "60000"
; The game's download, run as the install's last step (MCRedux --get-content): it exits 0 once the content is in.
#define GetContentOption "--get-content"
; Never change the AppId: Windows' Apps list, updates and the uninstaller find the install by it.
#define AppIdGuid "603FB38F-BA9C-4011-9D9E-1B9A52D73050"
; Windows' entry for the install in its Apps list, where its size is (EstimatedSize, in KB). Never ExpandConstant'ed:
; its braces are the GUID's, not a constant's.
#define UninstallKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\{" + AppIdGuid + "}_is1"
#define SizeValue "EstimatedSize"
; The setup's own mutex, so only one runs at a time.
#define AppSetupMutex "MechCommanderReduxHDSetup-" + AppIdGuid

[Setup]
AppId={{{#AppIdGuid}}
; One setup at a time: a second (a double click twice over) says Setup is already running.
SetupMutex={#AppSetupMutex}
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
; The download's folder (DownloadFolderDefault: {sd}\Games\..., else {autopf}, for the player alone
; %LOCALAPPDATA%\Programs). The player's copy is its own.
DefaultDirName={sd}\{#GamesFolderName}\{#AppName}
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
; An update of a copy the setup didn't install puts the program's files in alone (FilesOnly): Windows' Apps list keeps
; the setup's own install, if any.
Uninstallable=not FilesOnly
CreateUninstallRegKey=not FilesOnly
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
SelectDirLabel3=Setup downloads the game into a folder of its own, or installs {#AppName} into your copy and plays it.

[CustomMessages]
DownloadGame=&Download it
UseMyCopy=&Use my copy of MechCommander Gold
TakeoverChoice=Redux HD takes over this copy. To keep it unchanged, choose Download it instead.%nUninstalling Redux HD restores your copy.
TakeoverCopy=Redux HD takes over this copy.%nUninstalling Redux HD restores your copy.
HdChoice=Redux &HD art, sound and movies
NoVulkan=Redux HD's art and movies won't show on this computer: its graphics can't run Vulkan, so the game plays in its original look.
GameSize=%1 (%2 GB)
HdSize=%1 (%2 GB more)
Totals=About %1 GB with HD, %2 GB without.
ChooseFolder=Choose a folder for the game.
ChooseGameFolder=Choose the folder MechCommander Gold is installed in, the one with {#GameDataFile}.
NotGameFolder=%1 has no {#GameDataFile}, so it isn't a MechCommander Gold folder.
SyncedFolder=OneDrive syncs %1. Choose a folder outside it: the game's files would fill it, and OneDrive would hold them while it syncs.
DriveFolder=%1 is a whole drive. Choose a folder on it for the game.
SharedFolder=%1 belongs to Windows, the programs or another account, or holds their folders. Choose a folder of the game's own.
FolderNotEmpty=%1 holds other files already. Choose an empty folder for the game, or a new one.
FolderSigns=Setup can't use %1: its name has a percent sign or a quote. Choose another folder.
FolderProtected=Windows protects %1. Setup will ask Windows to let you change it.
FolderStillProtected=Setup couldn't get permission to change %1.
BackupFailed=Setup couldn't copy %1 to %2.
RestoreFailed=These files couldn't go back into your MechCommander Gold folder, so they're still in %1:%2
RemovalFailed=The game couldn't remove its downloaded files from %1, so the uninstall stopped:%n%n%2%n%nClose the game and its editor, then uninstall again.
GameMissing=%2 is missing from %1, so the game's downloaded files can't be removed: they'll stay there.%n%nUninstall anyway?
PlayGame=Play {#AppName}
EditorShortcut={#AppName} Mission Editor
ReadyDownload=Then downloaded, before Setup finishes:
ReadyGame=The game (%1 GB)
ReadyCopy=What your copy lacks of the game
ReadyHd=Redux HD art, sound and movies (%1 GB)
GettingGame=Downloading the game...
DownloadLeft=Setup has installed {#AppName}, but the game's download isn't finished yet. It carries on when you start {#AppName}.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#BinDir}\{#GameExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\{#SdlDll}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\{#EditorExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DocsDir}\README.txt"; DestDir: "{app}"; DestName: "{#ReadmeName}"; Flags: ignoreversion
Source: "{#LicencesFile}"; DestDir: "{app}"; DestName: "{#LicencesName}"; Flags: ignoreversion

[Icons]
; An update makes again only the shortcuts there already (KeepsShortcut): it never adds one.
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#GameExe}"; WorkingDir: "{app}"; Check: KeepsShortcut('{autoprograms}\{#AppName}')
Name: "{autoprograms}\{cm:EditorShortcut}"; Filename: "{app}\{#EditorExe}"; WorkingDir: "{app}"; Check: KeepsShortcut('{autoprograms}\{cm:EditorShortcut}')
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#GameExe}"; WorkingDir: "{app}"; Tasks: desktopicon; Check: KeepsShortcut('{autodesktop}\{#AppName}')

[Run]
Filename: "{app}\{#GameExe}"; WorkingDir: "{app}"; Description: "{cm:PlayGame}"; Flags: postinstall nowait skipifsilent
; An update the game ran for RESTART: the new version starts by itself.
Filename: "{app}\{#GameExe}"; WorkingDir: "{app}"; Flags: nowait; Check: StartAfterUpdate

[UninstallDelete]
; The folder, once nothing's left in it: Setup may have made it itself, to let the player change it.
Type: dirifempty; Name: "{app}"

[Code]
const
  // Written and deleted to learn whether the player can change the folder: a name of its own each time (the time to the
  // millisecond and a random number), one that isn't there, so nothing put in the folder beforehand can make the test
  // fail and the setup ask for an administrator's rights it doesn't need.
  WriteTestName = 'MechCommander Redux setup %s %d.tmp';
  WriteTestTime = 'yyyymmddhhnnsszzz';
  WriteTestRange = 1000000000;
  WriteTestTries = 8;
  // The bytes in a GB, decimal, as the game's own screens show sizes (MCUpdateWork::SizeText).
  BytesPerGB = 1000000000;
  // Waiting for a process to end: OpenProcess's SYNCHRONIZE, and WaitForSingleObject's WAIT_OBJECT_0.
  WaitAccess = $00100000;
  WaitEnded = 0;
  // The answers on the command line (silent installs).
  CopyAnswer = 'COPY';
  DownloadAnswer = 'DOWNLOAD';
  NoAnswer = 'NO';
  // GetUserNameExW's form for an account as Windows' tools take it: "MACHINE\name", "DOMAIN\name".
  NameSamCompatible = 2;
  AccountNameLength = 256;
  // The number of an update's second copy of a file in the backup, "name (2).ext" (update/MCContentSwap.cpp's KeptAt).
  FirstNumberedCopy = 2;
  // The game's exit code once it has removed its content (MCRedux --remove-content), and the file it says why it
  // couldn't in (--report), in the uninstaller's temporary folder.
  RemovedCode = 0;
  RemovalReportName = 'content removal.txt';
  // The attribute of a file or folder that is a link to elsewhere (a junction, a symbolic link):
  // FILE_ATTRIBUTE_REPARSE_POINT.
  ReparsePointAttribute = $00000400;
  // A file Windows won't delete as it is (FILE_ATTRIBUTE_READONLY): a copy of the CD's files can carry it.
  ReadOnlyAttribute = $00000001;
  // GetFileAttributesW's answer when it can't look (INVALID_FILE_ATTRIBUTES), and the errors that say nothing is there:
  // ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND.
  InvalidFileAttributes = -1;
  ErrorFileNotFound = 2;
  ErrorPathNotFound = 3;
  // The game's exit code once its content is in place (MCRedux --get-content).
  ContentInCode = 0;
  // Where Windows' Vulkan loader finds the drivers it loads (Khronos' Vulkan-Loader, docs/LoaderDriverInterface.md):
  // the key of each display adapter and software component, under its class, naming one (VulkanDriverName; the
  // 32-bit programs' VulkanDriverNameWow isn't the game's), and Khronos' own list, whose values of 0 are drivers it
  // loads. The loader is vulkan-1.dll in the 64-bit System32, as the game is 64-bit.
  VulkanLoaderFile = 'System32\vulkan-1.dll';
  DisplayClassKey = 'SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}';
  SoftwareComponentClassKey = 'SYSTEM\CurrentControlSet\Control\Class\{5c4c3332-344d-483c-8739-259e934c9cc8}';
  VulkanDriverValue = 'VulkanDriverName';
  KhronosDriversKey = 'SOFTWARE\Khronos\Vulkan\Drivers';
  KhronosDriverLoaded = 0;
  // A file's size from its two halves (TFindRec's SizeHigh, SizeLow), and the KB Windows' Apps list counts in.
  HalfSize = 65536;
  BytesPerKB = 1024;
  // The shortcuts in a folder of them.
  ShortcutExtension = '.lnk';
  // Where the game puts the setup an update downloads, in its own folder (update/MCSetupBackend.h's ProgramFolder).
  UpdateSetupFolder = '\redux update\program';

var
  // The folder whose files were copied into its backup this run, which ones, and whether the install finished: a setup
  // that stops part way drops its copies (DropCopies).
  CopiedFolder: String;
  CopiedNames: TArrayOfString;
  Installed: Boolean;
  // The folder page's questions: the game downloaded or the player's copy, and HD; and the folder each choice shows.
  GameDownload, GameCopy: TNewRadioButton;
  HdBox: TNewCheckBox;
  // Under HD, when the computer can't run Vulkan: that HD's art and movies won't show.
  VulkanNote: TNewStaticText;
  // Under them, while the player's copy is chosen: that Redux HD takes it over, and how to leave it as it is.
  TakeoverNote: TNewStaticText;
  // What the computer lacks to run Vulkan (VulkanLack); empty when nothing is missing.
  VulkanLacks: String;
  DownloadFolder, CopyFolder: String;
  CopyShown: Boolean;
  // Whether the install ran the game's download, and whether that ended with the content in place.
  DownloadRan, DownloadDone: Boolean;
  // The folder of the game that ran this setup as an update (UpdateFolder; '' when it isn't one), and whether the setup
  // didn't install it, so the program's files go in alone (FilesOnly): worked out once, whenever first asked
  // (KnowUpdate), as Inno may ask for its directives before the wizard is made.
  UpdateGameFolder: String;
  UpdatesOwnCopy, UpdateKnown: Boolean;
  // In such a copy: its other names that are the game (MCX.EXE, a resolution's copy: the same bytes as its MCRedux.exe
  // before the update), made again as the new one, and whether they've been looked for.
  GameNamesInCopy: TArrayOfString;
  GameNamesFound: Boolean;

function OpenProcess(Access: Cardinal; Inherit: Integer; ProcessId: Cardinal): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function WaitForSingleObject(Handle: THandle; Milliseconds: Cardinal): Cardinal;
  external 'WaitForSingleObject@kernel32.dll stdcall';
function CloseHandle(Handle: THandle): Integer;
  external 'CloseHandle@kernel32.dll stdcall';
function GetUserNameExW(NameFormat: Integer; NameBuffer: String; var Size: Cardinal): Integer;
  external 'GetUserNameExW@secur32.dll stdcall';
// A second name for a file (a hard link): MCX.EXE as the game.
function CreateHardLinkW(FileName, ExistingFileName: String; SecurityAttributes: Cardinal): Integer;
  external 'CreateHardLinkW@kernel32.dll stdcall';
// A file's or folder's own attributes (a link's, not what it leads to).
function GetFileAttributesW(FileName: String): Longint;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function SetFileAttributesW(FileName: String; Attributes: Longint): Integer;
  external 'SetFileAttributesW@kernel32.dll stdcall';

// The names of the files the program puts in the game folder: [Files]' and MCX.EXE, the game under its name.
function ProgramFileNames: TArrayOfString;
begin
  SetArrayLength(Result, 6);
  Result[0] := '{#GameExe}';
  Result[1] := '{#SdlDll}';
  Result[2] := '{#EditorExe}';
  Result[3] := '{#OriginalExe}';
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

// Whether the folder page asks to download the game and HD: only in a setup the release script built (it has the
// release's sizes), as nothing can be downloaded without a release. Otherwise the page asks only for the player's
// copy. A silent install takes its answers from the command line either way.
function AsksDownloads: Boolean;
begin
  Result := SizesKnown or WizardSilent;
end;

// Whether one of a device class's keys (DisplayClassKey, SoftwareComponentClassKey; Devices, its subkeys) names a
// Vulkan driver.
function ClassNamesVulkanDriver(const ClassKey: String; const Devices: TArrayOfString): Boolean;
var
  Index: Integer;
begin
  Result := False;

  for Index := 0 to GetArrayLength(Devices) - 1 do
    if RegValueExists(HKLM64, ClassKey + '\' + Devices[Index], VulkanDriverValue) then
    begin
      Result := True;
      Exit;
    end;
end;

// Whether Khronos' list of drivers has one the loader loads.
function KhronosListsVulkanDriver: Boolean;
var
  Names: TArrayOfString;
  Index: Integer;
  Value: Cardinal;
begin
  Result := False;

  if RegGetValueNames(HKLM64, KhronosDriversKey, Names) then
    for Index := 0 to GetArrayLength(Names) - 1 do
      if RegQueryDWordValue(HKLM64, KhronosDriversKey, Names[Index], Value) and (Value = KhronosDriverLoaded) then
      begin
        Result := True;
        Exit;
      end;
end;

// What the computer lacks to run Vulkan, which Redux HD's art and movies need (without it the game draws on the CPU,
// in the original's look), as the log says it; empty when nothing is missing. It reads what Windows' Vulkan loader
// reads: the loader itself, and a driver named for it. Only a clear lack counts: what can't be read counts as there,
// and a driver named may still not start (the game falls back then, as it always has).
function VulkanLack: String;
var
  Redirected, Loader: Boolean;
  Adapters, Components: TArrayOfString;
begin
  Result := '';
  // The 64-bit System32, not the 32-bit one Windows shows this 32-bit setup in its place.
  Redirected := EnableFsRedirection(False);

  try
    Loader := FileExists(AddBackslash(ExpandConstant('{win}')) + VulkanLoaderFile);
  finally
    EnableFsRedirection(Redirected);
  end;

  if not Loader then
    Result := 'no vulkan-1.dll in System32'
  // Every computer has the display adapters' class: when it can't be read, nothing is known.
  else if not RegGetSubkeyNames(HKLM64, DisplayClassKey, Adapters) then
    Log('Vulkan: the display adapters'' keys couldn''t be read')
  else if not ClassNamesVulkanDriver(DisplayClassKey, Adapters) then
  begin
    if not RegGetSubkeyNames(HKLM64, SoftwareComponentClassKey, Components) then
      SetArrayLength(Components, 0);

    if not ClassNamesVulkanDriver(SoftwareComponentClassKey, Components) and not KhronosListsVulkanDriver then
      Result := 'no Vulkan driver';
  end;
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
  TakeoverNote.Visible := CopyShown;

  if CopyShown then
    WizardForm.DirEdit.Text := CopyFolder
  else
    WizardForm.DirEdit.Text := DownloadFolder;
end;

// The folder page's questions, above and below the folder: the game, and HD.
procedure AddQuestions;
var
  Page: TWizardPage;
  Top, NotesTop: Integer;
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
  WizardForm.SelectDirBrowseLabel.Visible := not AsksDownloads;

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

  HdBox := TNewCheckBox.Create(WizardForm);
  HdBox.Parent := Page.Surface;
  HdBox.Left := GameDownload.Left;
  HdBox.Width := GameDownload.Width;
  HdBox.Caption := HdCaption;

  // Without the questions the page stays Inno's own: the folder under its browse line.
  GameDownload.Visible := AsksDownloads;
  GameCopy.Visible := AsksDownloads;
  HdBox.Visible := AsksDownloads;

  if AsksDownloads then
  begin
    WizardForm.DirEdit.Top := GameCopy.Top + GameCopy.Height + ScaleY(10);
    WizardForm.DirBrowseButton.Top := WizardForm.DirEdit.Top;
  end;

  HdBox.Top := WizardForm.DirEdit.Top + WizardForm.DirEdit.Height + ScaleY(16);
  NotesTop := HdBox.Top + HdBox.Height;

  VulkanNote := TNewStaticText.Create(WizardForm);
  VulkanNote.Parent := Page.Surface;
  VulkanNote.Left := GameDownload.Left;
  VulkanNote.AutoSize := False;
  VulkanNote.WordWrap := True;
  VulkanNote.Width := GameDownload.Width;
  VulkanNote.Caption := CustomMessage('NoVulkan');
  VulkanNote.Top := NotesTop + ScaleY(4);
  VulkanNote.AutoSize := True;
  VulkanNote.Visible := AsksDownloads and (VulkanLacks <> '');

  if VulkanNote.Visible then
    NotesTop := VulkanNote.Top + VulkanNote.Height;

  TakeoverNote := TNewStaticText.Create(WizardForm);
  TakeoverNote.Parent := Page.Surface;
  TakeoverNote.Left := GameDownload.Left;
  TakeoverNote.AutoSize := False;
  TakeoverNote.WordWrap := True;
  TakeoverNote.Width := GameDownload.Width;

  // Without the download to choose instead, the note says only what the install and the uninstall do.
  if AsksDownloads then
  begin
    TakeoverNote.Caption := FmtMessage(CustomMessage('TakeoverChoice'), []);
    TakeoverNote.Top := NotesTop + ScaleY(16);
  end
  else
  begin
    TakeoverNote.Caption := FmtMessage(CustomMessage('TakeoverCopy'), []);
    TakeoverNote.Top := WizardForm.DirEdit.Top + WizardForm.DirEdit.Height + ScaleY(16);
  end;

  TakeoverNote.AutoSize := True;
end;

function DownloadFolderDefault: String; forward;
function ComparablePath(const Path: String): String; forward;

// The folder of the game that ran this setup as an update ('' when it isn't one: no game's process to wait for, or not
// silent): the folder it names (/DIR=), else, for an earlier game, which names none, the one above the Redux
// update\program folder it downloaded this setup into.
function UpdateFolder: String;
var
  Folder: String;
begin
  Result := '';

  if not WizardSilent or (GetEnv('{#UpdatePidVariable}') = '') then
    Exit;

  Result := RemoveBackslashUnlessRoot(ExpandConstant('{param:DIR|}'));

  if Result <> '' then
    Exit;

  Folder := RemoveBackslashUnlessRoot(ExtractFileDir(ExpandConstant('{srcexe}')));

  if (Length(Folder) > Length(UpdateSetupFolder)) and
     (AnsiLowercase(Copy(Folder, Length(Folder) - Length(UpdateSetupFolder) + 1, Length(UpdateSetupFolder))) =
      UpdateSetupFolder) then
    Result := Copy(Folder, 1, Length(Folder) - Length(UpdateSetupFolder));

  if (Result <> '') and not DirExists(Result) then
    Result := '';
end;

// The folder the setup's uninstall entry names (its install), as the game reads it (MCSetupRunWin.cpp's MCSetupOwns):
// the player's own entry, else every user's; '' with none.
function InstalledFolder: String;
begin
  if not RegQueryStringValue(HKCU, '{#UninstallKey}', 'InstallLocation', Result) and
     not RegQueryStringValue(HKLM64, '{#UninstallKey}', 'InstallLocation', Result) then
    Result := '';
end;

// Whether Folder and Other are one folder, however each is spelled (a junction, a subst drive, a short name), as the
// game tells them (std::filesystem::equivalent): the same path, or a file made in one seen in the other.
function SameFolder(const Folder, Other: String): Boolean;
var
  Probe: String;
  Attempt: Integer;
begin
  Result := False;

  if (Folder = '') or (Other = '') then
    Exit;

  if ComparablePath(Folder) = ComparablePath(Other) then
  begin
    Result := True;
    Exit;
  end;

  if not DirExists(Folder) or not DirExists(Other) then
    Exit;

  for Attempt := 1 to WriteTestTries do
  begin
    Probe := Format(WriteTestName, [GetDateTimeString(WriteTestTime, '-', '-'), Random(WriteTestRange)]);

    if not FileOrDirExists(AddBackslash(Folder) + Probe) and not FileOrDirExists(AddBackslash(Other) + Probe) then
    begin
      if SaveStringToFile(AddBackslash(Folder) + Probe, '', False) then
      begin
        Result := FileExists(AddBackslash(Other) + Probe);

        if not DeleteFile(AddBackslash(Folder) + Probe) then
          Log(Format('Couldn''t delete the folders'' test %s', [AddBackslash(Folder) + Probe]));
      end
      else
        Log(Format('Couldn''t write in %s to tell whether it is %s: taken as another folder', [Folder, Other]));

      Exit;
    end;
  end;

  Log(Format('No free name for the folders'' test in %s', [Folder]));
end;

// Works out, the first time it's asked, the folder of the game that ran this setup as an update and whether the setup
// installed it.
procedure KnowUpdate;
var
  Install: String;
begin
  if UpdateKnown then
    Exit;

  UpdateKnown := True;
  UpdateGameFolder := UpdateFolder;

  if UpdateGameFolder = '' then
    Exit;

  Install := InstalledFolder;
  UpdatesOwnCopy := not SameFolder(UpdateGameFolder, Install);

  if UpdatesOwnCopy then
    Log(Format('An update of %s, a copy the setup didn''t install (its install: %s): the program''s files alone',
               [UpdateGameFolder, Install]))
  else
    Log(Format('An update of the setup''s install, %s', [UpdateGameFolder]));
end;

// Whether the setup puts the program's files alone into the copy that ran it as an update: one it didn't install (no
// install of its own, or its install is another folder).
function FilesOnly: Boolean;
begin
  KnowUpdate;
  Result := UpdatesOwnCopy;
end;

// Whether a shortcut at Name (no extension) is made: always by an install, by an update only where it is already.
function KeepsShortcut(const Name: String): Boolean;
begin
  KnowUpdate;
  Result := (UpdateGameFolder = '') or
            (not UpdatesOwnCopy and FileExists(ExpandConstant(Name) + ShortcutExtension));

  if not Result then
    Log(Format('An update adds no shortcut: %s isn''t made', [ExpandConstant(Name)]));
end;

procedure InitializeWizard;
var
  Given, Answer, Hd: String;
begin
  VulkanLacks := VulkanLack;

  if VulkanLacks = '' then
    Log('Vulkan: found')
  else
    Log(Format('Vulkan: none, %s', [VulkanLacks]));

  AddQuestions;
  // HD by default where its art and movies show; /HD=NO leaves it out, any other answer has it anyway.
  Hd := Uppercase(ExpandConstant('{param:HD|}'));
  HdBox.Checked := (Hd <> NoAnswer) and ((Hd <> '') or (VulkanLacks = ''));
  DownloadFolder := DownloadFolderDefault;
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

  if not AsksDownloads then
  begin
    Answer := CopyAnswer;
    HdBox.Checked := False;
  end
  else if Answer = '' then
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
  TakeoverNote.Visible := CopyShown;

  if WizardForm.PrevAppDir = '' then
  begin
    if CopyShown then
      WizardForm.DirEdit.Text := CopyFolder
    else
      WizardForm.DirEdit.Text := DownloadFolder;
  end;

  GameDownload.OnClick := @GameChosen;
  GameCopy.OnClick := @GameChosen;

  // An update of a copy the setup didn't install goes into that copy, whatever install the setup made before.
  if FilesOnly then
    WizardForm.DirEdit.Text := UpdateGameFolder;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectDir then
    ShowTotals
  // The game's download stopped short (closed, or it couldn't finish): the player learns the first start carries on.
  else if (CurPageID = wpFinished) and DownloadRan and not DownloadDone then
    WizardForm.FinishedLabel.Caption := CustomMessage('DownloadLeft');
end;

function CanChange(const Folder: String): Boolean;
var
  TestFile: String;
  Attempt: Integer;
begin
  Result := False;

  for Attempt := 1 to WriteTestTries do
  begin
    TestFile := AddBackslash(Folder) +
                Format(WriteTestName, [GetDateTimeString(WriteTestTime, '-', '-'), Random(WriteTestRange)]);

    if not FileOrDirExists(TestFile) then
    begin
      Result := SaveStringToFile(TestFile, '', False);

      if Result and not DeleteFile(TestFile) then
        Log(Format('Couldn''t delete the write test %s', [TestFile]));

      Exit;
    end;
  end;

  Log(Format('No free name for the write test in %s', [Folder]));
end;

// Folder quoted for a command line: a backslash just before the closing quote would escape it.
function QuotedFolder(const Folder: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Folder);

  if Copy(Result, Length(Result), 1) = '\' then
    Result := Result + '.';

  Result := '"' + Result + '"';
end;

// The player's account, named here: an elevated command may run as another account (an administrator's). As Windows
// names it ("MACHINE\name"): USERDOMAIN can name another (WORKGROUP, over SSH), and then icacls grants nothing.
function PlayerAccount: String;
var
  Size: Cardinal;
begin
  Size := AccountNameLength;
  Result := StringOfChar(' ', Size);

  if GetUserNameExW(NameSamCompatible, Result, Size) <> 0 then
    SetLength(Result, Size)
  else
    // icacls looks a bare name up on this machine first.
    Result := GetUserNameString;
end;

// Whether Name (a folder's, an account's) has what Windows' command line reads as something else even between quotes:
// cmd expands %NAME%, and a quote ends the quoting.
function HasCommandSigns(const Name: String): Boolean;
begin
  Result := (Pos('%', Name) > 0) or (Pos('"', Name) > 0);
end;

// Lets the player's account change Folder and everything in it, making it first if need be; elevated by Windows' prompt
// unless Setup already is. Folder alone: the folders holding it stay as they are. Whether the player can now change it.
function Grant(const Folder: String): Boolean;
var
  Account, Verb, Command: String;
  ErrorCode: Integer;
begin
  Result := False;
  Account := PlayerAccount;

  if HasCommandSigns(Folder) or HasCommandSigns(Account) then
  begin
    Log(Format('Not letting %s change %s: cmd would read the names as something else', [Account, Folder]));
    Exit;
  end;

  if IsAdmin then
    Verb := ''
  else
    Verb := 'runas';

  // Without the AutoRun commands or delayed expansion (!NAME!) the registry may ask cmd for.
  Command := '/d /v:off /c mkdir ' + QuotedFolder(Folder) + ' 2>nul & "' + ExpandConstant('{sys}\icacls.exe') + '" ' +
             QuotedFolder(Folder) + ' /grant "' + Account + ':(OI)(CI)M"';

  if not ShellExec(Verb, ExpandConstant('{cmd}'), Command, '', SW_HIDE, ewWaitUntilTerminated, ErrorCode) then
    Log(Format('icacls for %s on %s didn''t run: %s', [Account, Folder, SysErrorMessage(ErrorCode)]))
  else
    Log(Format('Let %s change %s', [Account, Folder]));

  Result := DirExists(Folder) and CanChange(Folder);
end;

// Grant, saying so when the player still can't change Folder; whether the player now can.
function GrantOrSay(const Folder: String): Boolean;
begin
  Result := Grant(Folder);

  if not Result then
    SuppressibleMsgBox(FmtMessage(CustomMessage('FolderStillProtected'), [Folder]), mbError, MB_OK, IDOK);
end;

// Asks Windows, once, to let the player's account change Folder and everything in it; whether it now can.
function GetPermission(const Folder: String): Boolean;
begin
  Result := False;

  if SuppressibleMsgBox(FmtMessage(CustomMessage('FolderProtected'), [Folder]), mbConfirmation, MB_OKCANCEL,
                        IDOK) <> IDOK then
    Exit;

  Result := GrantOrSay(Folder);
end;

// The folder, or the nearest folder holding it that exists: where Setup will make it.
function ExistingFolder(const Folder: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Folder);

  while (Result <> '') and not DirExists(Result) and (ExtractFileDir(Result) <> Result) do
    Result := ExtractFileDir(Result);
end;

// Whether the player can make the game's folder in Folder (ExistingFolder's): at a drive's root an account may make
// folders but not files (Windows' own rights there), so a folder is made and removed there; elsewhere a file
// (CanChange), as the game's own updates write files.
function CanMakeIn(const Folder: String): Boolean;
var
  TestFolder: String;
  Attempt: Integer;
begin
  if CompareText(RemoveBackslashUnlessRoot(Folder), AddBackslash(ExtractFileDrive(Folder))) <> 0 then
  begin
    Result := CanChange(Folder);
    Exit;
  end;

  Result := False;

  for Attempt := 1 to WriteTestTries do
  begin
    TestFolder := AddBackslash(Folder) +
                  Format(WriteTestName, [GetDateTimeString(WriteTestTime, '-', '-'), Random(WriteTestRange)]);

    if not DirExists(TestFolder) and not FileExists(TestFolder) then
    begin
      Result := CreateDir(TestFolder);

      if Result and not RemoveDir(TestFolder) then
        Log(Format('Couldn''t remove the write test %s', [TestFolder]));

      Exit;
    end;
  end;

  Log(Format('No free name for the write test in %s', [Folder]));
end;

// The download's folder by default: the system drive's Games folder, where the player can make the game's folder
// there; else the player's own programs folder.
function DownloadFolderDefault: String;
begin
  Result := ExpandConstant('{sd}\{#GamesFolderName}\{#AppName}');

  if not CanMakeIn(ExistingFolder(Result)) then
  begin
    Log(Format('Can''t make %s: the download''s folder is the programs folder', [Result]));
    Result := ExpandConstant('{autopf}\{#AppName}');
  end;
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

// Path in full, lower case and ending in a backslash, for telling whether one folder is in another; '' for none.
function ComparablePath(const Path: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Trim(Path));

  if Result <> '' then
    Result := AddBackslash(AnsiLowercase(ExpandFileName(Result)));
end;

// Whether Folder is Holder or inside it (both ComparablePath's; never inside none).
function IsInside(const Folder, Holder: String): Boolean;
begin
  Result := (Holder <> '') and (Copy(Folder, 1, Length(Holder)) = Holder);
end;

// The folder holding the accounts' own (C:\Users), by the player's own.
function UsersFolder: String;
begin
  Result := ExtractFileDir(RemoveBackslashUnlessRoot(GetEnv('USERPROFILE')));
end;

// Whether Folder (ComparablePath's) is in the player's own account folder.
function InOwnProfile(const Folder: String): Boolean;
begin
  Result := IsInside(Folder, ComparablePath(GetEnv('USERPROFILE')));
end;

// Whether Folder (ComparablePath's) is in another account's folder: in Users, but not the player's own, nor the one
// every account shares (Public).
function InOthersProfile(const Folder: String): Boolean;
begin
  Result := IsInside(Folder, ComparablePath(UsersFolder)) and not InOwnProfile(Folder) and
            not IsInside(Folder, ComparablePath(GetEnv('PUBLIC')));
end;

// Windows' folders, the programs' and the accounts', which the game's folder can never be or hold.
function SharedFolders: TArrayOfString;
begin
  SetArrayLength(Result, 6);
  Result[0] := ExpandConstant('{win}');
  Result[1] := ExpandConstant('{sys}');
  Result[2] := ExpandConstant('{commonpf}');
  Result[3] := ExpandConstant('{commonpf32}');
  Result[4] := ExpandConstant('{commonappdata}');
  Result[5] := UsersFolder;
end;

// Whether Folder holds nothing; not when it can't be listed.
function IsEmptyFolder(const Folder: String): Boolean;
var
  FindRec: TFindRec;
begin
  Result := False;

  if not FindFirst(AddBackslash(Folder) + '*', FindRec) then
    Exit;

  try
    Result := True;

    repeat
      if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
        Result := False;
    until not Result or not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

// Why Folder can't be the game's (the message to show), or '' when it can. Setup may let the player's account change
// the game's folder and everything in it, so it must be the game's own: never a whole drive, nor in Windows' folder or
// another account's, nor one of the programs' or accounts' folders or one holding them (a folder of the game's in
// Program Files, an original install, is its own). Downloaded into (Download), a folder there already holds nothing or
// the game. And Windows' command line must read its name as it is (Grant).
function FolderRefusal(const Folder: String; Download: Boolean): String;
var
  Path: String;
  Shared: TArrayOfString;
  Shares: Boolean;
  I: Integer;
begin
  Result := '';
  Path := ComparablePath(Folder);

  if HasCommandSigns(Folder) then
  begin
    Result := FmtMessage(CustomMessage('FolderSigns'), [Folder]);
    Exit;
  end;

  if Path = AddBackslash(AnsiLowercase(ExtractFileDrive(Path))) then
  begin
    Result := FmtMessage(CustomMessage('DriveFolder'), [Folder]);
    Exit;
  end;

  Shares := IsInside(Path, ComparablePath(ExpandConstant('{win}'))) or InOthersProfile(Path);
  Shared := SharedFolders;

  for I := 0 to GetArrayLength(Shared) - 1 do
    if IsInside(ComparablePath(Shared[I]), Path) then
      Shares := True;

  if Shares then
    Result := FmtMessage(CustomMessage('SharedFolder'), [Folder])
  else if Download and DirExists(Folder) and not IsGameFolder(Folder) and not IsEmptyFolder(Folder) then
    Result := FmtMessage(CustomMessage('FolderNotEmpty'), [Folder]);
end;

// Whether a folder on the way to Folder, from the drive down, is a link to elsewhere (a junction, a symbolic link: the
// attribute IsLink reads) or can't be looked at. Those not there yet Setup makes (Grant).
function LinkOnTheWay(const Folder: String): Boolean;
var
  Path, Rest, Name: String;
  Slash: Integer;
  Attributes, LookError: Longint;
begin
  Result := False;
  Rest := AddBackslash(ExpandFileName(Folder));
  Path := ExtractFileDrive(Rest);
  // Each folder's name below the drive, each ending in a backslash.
  Rest := Copy(Rest, Length(Path) + 2, Length(Rest));

  while (Rest <> '') and not Result do
  begin
    Slash := Pos('\', Rest);
    Name := Copy(Rest, 1, Slash - 1);
    Rest := Copy(Rest, Slash + 1, Length(Rest));

    if Name <> '' then
    begin
      Path := Path + '\' + Name;
      Attributes := GetFileAttributesW(Path);

      if Attributes = InvalidFileAttributes then
      begin
        LookError := DLLGetLastError;

        // Not there: nor is anything past it.
        if (LookError = ErrorFileNotFound) or (LookError = ErrorPathNotFound) then
          Exit;

        Log(Format('Couldn''t look at %s, on the way to %s: %s', [Path, Folder, SysErrorMessage(LookError)]));
        Result := True;
      end
      else if (Attributes and ReparsePointAttribute) <> 0 then
      begin
        Log(Format('%s, on the way to %s, is a link to elsewhere', [Path, Folder]));
        Result := True;
      end;
    end;
  end;
end;

// Whether Setup may ask Windows to let the player change Folder, with an administrator's rights (Grant), saying why not
// as FolderRefusal's folders not the game's own: FolderRefusal reads the path's text, and a link on the way to it
// (LinkOnTheWay) would lead the grant elsewhere, into Windows' folder say.
// Known: a folder on the way swapped for a link after this look (while Windows' prompt asks) isn't seen. Setup asks only
// when the player can't change the nearest folder of the way that is there (ExistingFolder: its write test can't be
// made to fail, CanChange), so the folders Setup makes in it aren't the player's to swap; a folder above that one could
// be swapped only by a program already running as the player, which Windows' prompt (not a security boundary between a
// user's programs and that user's own rights) doesn't keep out anyway.
function MayGrant(const Folder: String): Boolean;
begin
  Result := not LinkOnTheWay(Folder);

  if not Result then
    SuppressibleMsgBox(FmtMessage(CustomMessage('SharedFolder'), [Folder]), mbError, MB_OK, IDOK);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Folder, Refusal: String;
begin
  Result := True;

  // An install keeps its folder, whatever it holds by now, and an update the folder of the game that ran it.
  if (CurPageID <> wpSelectDir) or (WizardForm.PrevAppDir <> '') or FilesOnly then
    Exit;

  Folder := RemoveBackslashUnlessRoot(WizardDirValue);

  if Folder = '' then
  begin
    if GameCopy.Checked then
      SuppressibleMsgBox(CustomMessage('ChooseGameFolder'), mbError, MB_OK, IDOK)
    else
      SuppressibleMsgBox(CustomMessage('ChooseFolder'), mbError, MB_OK, IDOK);

    Result := False;
    Exit;
  end;

  Refusal := FolderRefusal(Folder, not GameCopy.Checked);

  if GameCopy.Checked and not IsGameFolder(Folder) then
  begin
    SuppressibleMsgBox(FmtMessage(CustomMessage('NotGameFolder'), [Folder]), mbError, MB_OK, IDOK);
    Result := False;
  end
  else if Refusal <> '' then
  begin
    SuppressibleMsgBox(Refusal, mbError, MB_OK, IDOK);
    Result := False;
  end
  else if OneDriveSyncs(Folder) then
  begin
    SuppressibleMsgBox(FmtMessage(CustomMessage('SyncedFolder'), [Folder]), mbError, MB_OK, IDOK);
    Result := False;
  end
  // Run as administrator, Setup can't tell whether the player can change the folder, but for one in the player's own
  // account folder: the game, run as the player, must, to update itself. Otherwise Windows asks only when the player
  // can't.
  else if IsAdmin then
  begin
    if not InOwnProfile(ComparablePath(Folder)) then
    begin
      Result := MayGrant(Folder);

      if Result then
        Result := GrantOrSay(Folder);
    end;
  end
  else if not CanMakeIn(ExistingFolder(Folder)) then
  begin
    Result := MayGrant(Folder);

    if Result then
      Result := GetPermission(Folder);
  end;
end;

// Whether Path is a link to elsewhere (a junction, a symbolic link): what's in the backup is put back only when it is
// the backup's own, never through a link planted there, which would move another folder's files into the game's.
function IsLink(const Path: String): Boolean;
var
  FindRec: TFindRec;
begin
  Result := False;

  if FindFirst(RemoveBackslashUnlessRoot(Path), FindRec) then
  begin
    try
      Result := (FindRec.Attributes and ReparsePointAttribute) <> 0;
    finally
      FindClose(FindRec);
    end;
  end;
end;

// Moves Name from Folder's backup back into Folder, unless a file of that name is there now, or it is a link.
function PutBack(const Folder, Name: String): Boolean;
var
  Source, Target: String;
begin
  Source := AddBackslash(BackupFolder(Folder)) + Name;
  Target := AddBackslash(Folder) + Name;

  if IsLink(Source) then
  begin
    Log(Format('Left %s in the backup: a link to elsewhere', [Source]));
    Result := False;
    Exit;
  end;

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

function ResolutionCopies(const Folder: String): TArrayOfString; forward;

// In a copy the setup didn't install, its other names that are the game: MCX.EXE and the resolutions' copies with the
// same bytes as its MCRedux.exe before the update (a link to it, or a copy), noted once, so they're the new game after.
// One that isn't (the original's, a mod's program) is left as it is.
procedure FindGameNamesInCopy(const Folder: String);
var
  Names: TArrayOfString;
  Game, GameHash, Path: String;
  I: Integer;
begin
  if GameNamesFound then
    Exit;

  GameNamesFound := True;
  SetArrayLength(GameNamesInCopy, 0);
  Game := AddBackslash(Folder) + '{#GameExe}';

  if not FileExists(Game) then
    Exit;

  try
    GameHash := GetSHA256OfFile(Game);
  except
    Log(Format('Couldn''t read %s: the game''s other names are left as they are (%s)', [Game, GetExceptionMessage]));
    Exit;
  end;

  Names := ResolutionCopies(Folder);
  Append(Names, '{#OriginalExe}');

  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    Path := AddBackslash(Folder) + Names[I];

    if FileExists(Path) then
    begin
      try
        if GetSHA256OfFile(Path) = GameHash then
        begin
          Append(GameNamesInCopy, Path);
          Log(Format('%s is the game: it becomes the new one', [Path]));
        end;
      except
        Log(Format('Couldn''t read %s: left as it is (%s)', [Path, GetExceptionMessage]));
      end;
    end;
  end;
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

  // A copy the setup didn't install: the program's files replace the earlier ones, which were never the folder's own.
  if FilesOnly then
  begin
    FindGameNamesInCopy(Folder);
    Exit;
  end;

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
function OriginalPath(const Folder: String): String;
begin
  Result := AddBackslash(RemoveBackslashUnlessRoot(Folder)) + '{#OriginalExe}';
end;

// Takes the player's compatibility settings for the original at Path (MCX.EXE, or a resolution's copy: Windows 98,
// 256 colours, 640x480, run as administrator) off its path, now the game's or nothing's, and keeps them for the
// uninstall: Windows would apply them to the game. Settings put on the game's MCX.EXE after an earlier install go too;
// the kept ones stay the original's.
procedure LiftCompatibility(const Path: String);
var
  Layers, Kept: String;
begin
  if not RegQueryStringValue(HKCU, '{#LayersKey}', Path, Layers) then
    Exit;

  if not RegQueryStringValue(HKCU, '{#KeptLayersKey}', Path, Kept) then
  begin
    // Not kept, they stay on MCX.EXE: the game runs with them there rather than the original losing them.
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

// Gives the original at Path back the compatibility settings LiftCompatibility kept.
procedure RestoreCompatibility(const Path: String);
var
  Kept: String;
begin
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

// Whether Name is the original under a resolution's name, as MechCommander Online's copy has it: MCX<width>x<height>.EXE.
function IsResolutionCopy(const Name: String): Boolean;
var
  Middle: String;
  Sign, I: Integer;
begin
  Result := False;

  if (Length(Name) <= Length('{#ResolutionPrefix}') + Length('{#ResolutionExtension}')) or
     (CompareText(Copy(Name, 1, Length('{#ResolutionPrefix}')), '{#ResolutionPrefix}') <> 0) or
     (CompareText(Copy(Name, Length(Name) - Length('{#ResolutionExtension}') + 1, Length('{#ResolutionExtension}')),
                  '{#ResolutionExtension}') <> 0) then
    Exit;

  Middle := Copy(Name, Length('{#ResolutionPrefix}') + 1,
                 Length(Name) - Length('{#ResolutionPrefix}') - Length('{#ResolutionExtension}'));
  Sign := Pos(Lowercase('{#ResolutionSizeSign}'), Lowercase(Middle));

  // Digits on both sides of the sign, and nothing else.
  if (Sign < 2) or (Sign = Length(Middle)) then
    Exit;

  for I := 1 to Length(Middle) do
    if (I <> Sign) and ((Middle[I] < '0') or (Middle[I] > '9')) then
      Exit;

  Result := True;
end;

// The resolutions' copies of the original in Folder (IsResolutionCopy), by name.
function ResolutionCopies(const Folder: String): TArrayOfString;
var
  FindRec: TFindRec;
begin
  SetArrayLength(Result, 0);

  if FindFirst(AddBackslash(Folder) + '{#ResolutionPrefix}*{#ResolutionExtension}', FindRec) then
  begin
    try
      repeat
        if ((FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0) and IsResolutionCopy(FindRec.Name) then
          Append(Result, FindRec.Name);
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

// Adds the targets of the shortcuts in Folder and the folders in it (ComparablePath's) to Targets.
procedure AddShortcutTargets(const Folder: String; Shell: Variant; var Targets: TArrayOfString);
var
  FindRec: TFindRec;
  Folders: TArrayOfString;
  Shortcut: String;
  Link: Variant;
  I: Integer;
begin
  if FindFirst(AddBackslash(Folder) + '*', FindRec) then
  begin
    try
      repeat
        // A link to elsewhere isn't followed: it could lead anywhere, around in a circle even.
        if (FindRec.Attributes and ReparsePointAttribute) = 0 then
        begin
          if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
          begin
            if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
              Append(Folders, AddBackslash(Folder) + FindRec.Name);
          end
          else if CompareText(ExtractFileExt(FindRec.Name), ShortcutExtension) = 0 then
          begin
            Shortcut := AddBackslash(Folder) + FindRec.Name;

            try
              Link := Shell.CreateShortcut(Shortcut);
              Append(Targets, ComparablePath(Link.TargetPath));
            except
              Log(Format('Couldn''t read the shortcut %s: %s', [Shortcut, GetExceptionMessage]));
            end;
          end;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;

  for I := 0 to GetArrayLength(Folders) - 1 do
    AddShortcutTargets(Folders[I], Shell, Targets);
end;

// What starts a program here, by its path (ComparablePath's): the player's and every account's shortcuts on the Desktop,
// in the Start menu and pinned to the taskbar, and the program DirectPlay's lobby entry names.
function StartedPaths: TArrayOfString;
var
  Shell: Variant;
  Folders: TArrayOfString;
  Path, ProgramName: String;
  I: Integer;
begin
  SetArrayLength(Result, 0);
  SetArrayLength(Folders, 5);
  Folders[0] := ExpandConstant('{userdesktop}');
  Folders[1] := ExpandConstant('{commondesktop}');
  Folders[2] := ExpandConstant('{userprograms}');
  Folders[3] := ExpandConstant('{commonprograms}');
  Folders[4] := ExpandConstant('{userappdata}\Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar');

  try
    Shell := CreateOleObject('WScript.Shell');

    for I := 0 to GetArrayLength(Folders) - 1 do
      AddShortcutTargets(Folders[I], Shell, Result);
  except
    Log(Format('Couldn''t read the shortcuts: %s', [GetExceptionMessage]));
  end;

  if RegQueryStringValue(HKLM32, '{#LobbyKey}', '{#FolderValue}', Path) and
     RegQueryStringValue(HKLM32, '{#LobbyKey}', '{#ProgramValue}', ProgramName) then
    Append(Result, ComparablePath(AddBackslash(Path) + ProgramName));
end;

function IsStarted(const Started: TArrayOfString; const Path: String): Boolean;
var
  I: Integer;
begin
  Result := False;

  for I := 0 to GetArrayLength(Started) - 1 do
    if Started[I] = ComparablePath(Path) then
    begin
      Result := True;
      Exit;
    end;
end;

// Deletes the file at Path, read-only or not (the original's, copied off the CD, may be).
function DeleteAnyFile(const Path: String): Boolean;
var
  Attributes: Longint;
begin
  Attributes := GetFileAttributesW(Path);

  if (Attributes <> InvalidFileAttributes) and ((Attributes and ReadOnlyAttribute) <> 0) then
    SetFileAttributesW(Path, Attributes and not ReadOnlyAttribute);

  Result := DeleteFile(Path);
end;

// Path (MCX.EXE, or a resolution's copy) becomes the game itself under that name: a hard link to MCRedux.exe, the same
// file, so whatever starts the original starts Redux HD and Windows and its antivirus see no program but the game; a
// copy where the drive holds no links (FAT32, exFAT). An update's MCRedux.exe is a new file, which the old link doesn't
// follow, so every install makes them again. What was at Path is the backup's already, or an earlier install's.
procedure MakeGameName(const Folder, Path: String);
var
  Game, NoLink: String;
begin
  Game := AddBackslash(Folder) + '{#GameExe}';

  if FileExists(Path) and not DeleteAnyFile(Path) then
  begin
    Log(Format('Couldn''t delete %s to make it the game: left as it is', [Path]));
    Exit;
  end;

  if CreateHardLinkW(Path, Game, 0) <> 0 then
  begin
    Log(Format('%s is the game: a link to %s', [Path, Game]));
    Exit;
  end;

  NoLink := SysErrorMessage(DLLGetLastError);

  if FileCopy(Game, Path, False) then
    Log(Format('%s is the game: a copy of %s (no link: %s)', [Path, Game, NoLink]))
  else
    Log(Format('Couldn''t make %s the game: no link (%s), and no copy', [Path, NoLink]));
end;

// Before the uninstall puts the backup back: MCX.EXE, the game under the original's name, goes (the backup's comes
// back).
procedure DropGameName(const Path: String);
begin
  if FileExists(Path) and not DeleteFile(Path) then
    Log(Format('Couldn''t delete %s, the game under its name', [Path]));
end;

// The game running under the original's names (MCX.EXE, a resolution's copy) is closed too before the program goes in,
// by those names: [Files] gives Windows' Restart Manager MCRedux.exe's alone.
procedure RegisterExtraCloseApplicationsResources;
var
  Folder: String;
  Names: TArrayOfString;
  I: Integer;
begin
  Folder := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));

  if FilesOnly then
  begin
    FindGameNamesInCopy(Folder);

    for I := 0 to GetArrayLength(GameNamesInCopy) - 1 do
      RegisterExtraCloseApplicationsResource(False, GameNamesInCopy[I]);

    Exit;
  end;

  RegisterExtraCloseApplicationsResource(False, OriginalPath(Folder));
  Names := ResolutionCopies(Folder);

  for I := 0 to GetArrayLength(Names) - 1 do
    RegisterExtraCloseApplicationsResource(False, AddBackslash(Folder) + Names[I]);
end;

// MechCommander Online's copy holds the original at other resolutions too (IsResolutionCopy), which Redux supersedes:
// each goes into the backup, and comes back as the game only where a shortcut or the lobby's entry starts it
// (StartedPaths), so those still start the game, now Redux. The uninstall puts them all back (DropResolutionCopies).
// Run again (an update), the backup holds the originals already, and the game's names are made again for its new file.
procedure TakeOverResolutionCopies(const Folder: String);
var
  Names, Started: TArrayOfString;
  Path, Kept: String;
  I: Integer;
begin
  Names := ResolutionCopies(Folder);

  if GetArrayLength(Names) = 0 then
    Exit;

  Started := StartedPaths;

  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    Path := AddBackslash(Folder) + Names[I];
    Kept := AddBackslash(BackupFolder(Folder)) + Names[I];

    if FileExists(Kept) then
    begin
      if not DeleteFile(Path) then
      begin
        Log(Format('Couldn''t delete %s, the game under an earlier install''s name: left as it is', [Path]));
        Continue;
      end;
    end
    else if not (ForceDirectories(BackupFolder(Folder)) and RenameFile(Path, Kept)) then
    begin
      Log(Format('Couldn''t move %s to %s: left as it is', [Path, Kept]));
      Continue;
    end
    else
      Log(Format('Moved %s to %s', [Path, Kept]));

    LiftCompatibility(Path);

    if IsStarted(Started, Path) then
    begin
      Log(Format('A shortcut starts %s: it becomes the game', [Path]));
      MakeGameName(Folder, Path);
    end;
  end;
end;

// Before the uninstall puts the backup back: the game's names in the resolutions' places go (an original the backup
// doesn't hold is left, never the setup's).
procedure DropResolutionCopies(const Folder: String);
var
  Names: TArrayOfString;
  Path: String;
  I: Integer;
begin
  Names := ResolutionCopies(Folder);

  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    Path := AddBackslash(Folder) + Names[I];

    if FileExists(AddBackslash(BackupFolder(Folder)) + Names[I]) and not DeleteFile(Path) then
      Log(Format('Couldn''t delete %s, the game under its name', [Path]));
  end;
end;

// Gives every resolution's original put back its compatibility settings.
procedure RestoreResolutionCompatibility(const Folder: String);
var
  Names: TArrayOfString;
  I: Integer;
begin
  Names := ResolutionCopies(Folder);

  for I := 0 to GetArrayLength(Names) - 1 do
    RestoreCompatibility(AddBackslash(Folder) + Names[I]);
end;

// The bytes of the files in Folder and the folders in it, links to elsewhere not followed.
function FolderBytes(const Folder: String): Int64;
var
  FindRec: TFindRec;
begin
  Result := 0;

  if FindFirst(AddBackslash(Folder) + '*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and ReparsePointAttribute) = 0 then
        begin
          if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
            Result := Result + Int64(FindRec.SizeHigh) * HalfSize * HalfSize + FindRec.SizeLow
          else if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
            Result := Result + FolderBytes(AddBackslash(Folder) + FindRec.Name);
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

// Windows' Apps list shows the install's size from its entry, written with the program alone: once the game's files
// are in, it's the folder's.
procedure NoteSize(const Folder: String);
var
  KB: Int64;
  Size: Cardinal;
begin
  KB := FolderBytes(Folder) div BytesPerKB;
  Size := KB;

  if RegWriteDWordValue(HKA, '{#UninstallKey}', '{#SizeValue}', Size) then
    Log(Format('The Apps list''s size: %d KB', [KB]))
  else
    Log(Format('Couldn''t write the Apps list''s size (%d KB)', [KB]));
end;

// The install's last step: the game downloads what the folder lacks (MCRedux --get-content), in its own window over the
// setup's, before the Finish page; the player can close it, and the first start carries on.
procedure GetContent(const Folder: String);
var
  ResultCode: Integer;
begin
  DownloadRan := True;
  WizardForm.StatusLabel.Caption := CustomMessage('GettingGame');
  WizardForm.FilenameLabel.Caption := '';

  if not Exec(AddBackslash(Folder) + '{#GameExe}', '{#GetContentOption} --data ' + QuotedFolder(Folder), Folder,
              SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) then
    Log(Format('Couldn''t start the game''s download: %s', [SysErrorMessage(ResultCode)]))
  else
  begin
    DownloadDone := ResultCode = ContentInCode;
    Log(Format('The game''s download ended with code %d', [ResultCode]));
  end;
end;

// The ready page says what comes after the program: the download.
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo,
                         MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo;

  if AsksDownloads and SizesKnown and (WizardForm.PrevAppDir = '') then
  begin
    Result := Result + NewLine + NewLine + CustomMessage('ReadyDownload') + NewLine + Space;

    if GameCopy.Checked then
      Result := Result + CustomMessage('ReadyCopy')
    else
      Result := Result + FmtMessage(CustomMessage('ReadyGame'), [ShownGB('{#GameBytes}')]);

    if HdBox.Checked then
      Result := Result + NewLine + Space + FmtMessage(CustomMessage('ReadyHd'), [ShownGB('{#HdBytes}')]);
  end;

  if MemoTasksInfo <> '' then
    Result := Result + NewLine + NewLine + MemoTasksInfo;
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

// Every install records its level (update/MCProgramSwap.h's SetupFile): a later release whose setup is at this level
// puts its program in by the game itself, without the setup. A program update the game left unfinished is moot once
// the setup's files are in: its journal goes first (the game's next start would otherwise put that update's old files
// back over these), then the rest of its folder.
procedure NoteLevel(const Folder: String);
var
  Update, Swap, Journal, Path: String;
begin
  Update := AddBackslash(Folder) + '{#ChosenFolder}';
  Swap := AddBackslash(Update) + '{#SwapFolder}';
  Journal := AddBackslash(Swap) + '{#SwapJournal}';

  if FileExists(Journal) and not DeleteFile(Journal) then
    Log(Format('Couldn''t delete the unfinished program update''s journal, %s', [Journal]))
  else if DirExists(Swap) and not DelTree(Swap, True, True, True) then
    Log(Format('Couldn''t delete all of the earlier program updates in %s: the game deletes the rest', [Swap]));

  Path := AddBackslash(Update) + '{#SetupFile}';

  if ForceDirectories(Update) and SaveStringToFile(Path, '{"level": {#SetupLevel}}', False) then
    Log(Format('The setup''s level: {#SetupLevel}, in %s', [Path]))
  else
    Log(Format('Couldn''t write the setup''s level, %s: the next program update runs the setup', [Path]));
end;

// Once the program is in: the original's MCX.EXE, now the game under its name, and its resolutions' copies taken over;
// the setup's level noted and a new install's answers left; then, shown, the game's download; and the Apps list's
// size, the folder's with all that.
procedure CurStepChanged(CurStep: TSetupStep);
var
  Folder: String;
  I: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    Installed := True;
    Folder := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));

    // A copy the setup didn't install: its names that were the game become the new one, and its level is noted. Nothing
    // else of the setup's is made there (no takeover, answers, download or Apps list entry).
    if FilesOnly then
    begin
      for I := 0 to GetArrayLength(GameNamesInCopy) - 1 do
        MakeGameName(Folder, GameNamesInCopy[I]);

      NoteLevel(Folder);
      Exit;
    end;

    LiftCompatibility(OriginalPath(Folder));
    MakeGameName(Folder, OriginalPath(Folder));
    TakeOverResolutionCopies(Folder);
    NoteLevel(Folder);

    if WizardForm.PrevAppDir = '' then
      LeaveAnswers(Folder);

    // Silently, the setup is an update the game ran, or a script's: the game gets its own content.
    if not WizardSilent then
      GetContent(Folder);

    NoteSize(Folder);
  end;
end;

procedure DeinitializeSetup;
begin
  if not Installed then
    DropCopies;
end;

// Whether Name, one of Files in the backup, is a later copy an update kept of a file the backup already holds:
// "name (2).ext" beside "name.ext" (update/MCContentSwap.cpp's KeptAt). It was never in the folder by that name, so it
// stays in the backup with the player's other copies.
function IsNumberedCopy(const Name: String; const Files: TArrayOfString): Boolean;
var
  Extension, Stem, Base: String;
  Open, I: Integer;
begin
  Result := False;
  Extension := ExtractFileExt(Name);
  Stem := Copy(Name, 1, Length(Name) - Length(Extension));

  if (Stem = '') or (Stem[Length(Stem)] <> ')') then
    Exit;

  // Back from the ")" over the digits to the "(", which a space and at least one letter of the name come before.
  Open := Length(Stem) - 1;

  while (Open > 0) and (Stem[Open] >= '0') and (Stem[Open] <= '9') do
    Open := Open - 1;

  if (Open < 3) or (Open = Length(Stem) - 1) or (Copy(Stem, Open - 1, 2) <> ' (') then
    Exit;

  if StrToIntDef(Copy(Stem, Open + 1, Length(Stem) - Open - 1), 0) < FirstNumberedCopy then
    Exit;

  Base := Copy(Stem, 1, Open - 2) + Extension;

  for I := 0 to GetArrayLength(Files) - 1 do
    if CompareText(Files[I], Base) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

// Moves everything in Folder's backup under Relative ('' for the backup itself) back into Folder at the same place,
// making the folders on its way, but an update's later copies (IsNumberedCopy), which stay, and links to elsewhere
// (IsLink), which are neither gone into nor moved; the names of what couldn't go back are added to Left.
procedure PutBackAll(const Folder, Relative: String; var Left: String);
var
  FindRec: TFindRec;
  Files, Folders: TArrayOfString;
  Kept: String;
  I: Integer;
begin
  Kept := AddBackslash(BackupFolder(Folder)) + Relative;

  if IsLink(Kept) then
  begin
    Log(Format('Left %s: a link to elsewhere', [Kept]));
    Exit;
  end;

  // The names first: moving files out while the folder is listed could skip some.
  if FindFirst(AddBackslash(Kept) + '*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and ReparsePointAttribute) <> 0 then
          Log(Format('Left %s in the backup: a link to elsewhere', [Relative + FindRec.Name]))
        else if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
          Append(Files, Relative + FindRec.Name)
        else if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
          Append(Folders, Relative + FindRec.Name);
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;

  for I := 0 to GetArrayLength(Files) - 1 do
    if IsNumberedCopy(Files[I], Files) then
      Log(Format('Left %s in the backup: an update''s later copy', [Files[I]]))
    else if not (ForceDirectories(ExtractFileDir(AddBackslash(Folder) + Files[I])) and PutBack(Folder, Files[I])) then
      Left := Left + #13#10 + Files[I];

  for I := 0 to GetArrayLength(Folders) - 1 do
  begin
    PutBackAll(Folder, AddBackslash(Folders[I]), Left);
    RemoveDir(AddBackslash(BackupFolder(Folder)) + Folders[I]);
  end;
end;

// The game removes its content from Folder (the files updates put there; the setup never owns content), leaving the
// player's own and what a copy taken over had: whether it did. If not, the player is told why (silently, the log is).
function RemoveContent(const Folder: String): Boolean;
var
  Report, Reason: String;
  Lines: TArrayOfString;
  ResultCode, I: Integer;
begin
  Result := False;

  // Without the game nothing can remove its content: the player chooses to uninstall anyway, leaving it (silently,
  // the uninstall goes on), rather than an uninstall that can never finish.
  if not FileExists(AddBackslash(Folder) + '{#GameExe}') then
  begin
    Log(Format('{#GameExe} is missing from %s: its content stays', [Folder]));
    Result := SuppressibleMsgBox(FmtMessage(CustomMessage('GameMissing'), [Folder, '{#GameExe}']), mbConfirmation,
                                 MB_YESNO, IDYES) = IDYES;
    Exit;
  end;

  Report := ExpandConstant('{tmp}\') + RemovalReportName;
  DeleteFile(Report);

  // Quoted as Grant quotes it: a drive's backslash before the closing quote would escape it.
  if not Exec(AddBackslash(Folder) + '{#GameExe}', '--remove-content --data ' + QuotedFolder(Folder) + ' --report "' +
              Report + '"', Folder, SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Reason := SysErrorMessage(ResultCode)
  else if ResultCode = RemovedCode then
  begin
    Log('The game removed its content');
    Result := True;
    Exit;
  end
  else
  begin
    Reason := '';

    if LoadStringsFromFile(Report, Lines) then
      for I := 0 to GetArrayLength(Lines) - 1 do
        Reason := Trim(Reason + ' ' + Lines[I]);

    if Reason = '' then
      Reason := Format('{#GameExe} ended with code %d', [ResultCode]);
  end;

  Log(Format('The game couldn''t remove its content: %s', [Reason]));
  SuppressibleMsgBox(FmtMessage(CustomMessage('RemovalFailed'), [Folder, Reason]), mbError, MB_OK, IDOK);
end;

// First, before anything goes, the game removes its content; if it can't, the uninstall stops there, the program kept
// to try again (it's the content's one remover). After the program's files are gone, everything in the backup goes
// back, so the folder is as it was, and the original's MCX.EXE gets its compatibility settings back. An update's later
// copies of the player's files stay in the backup, which then stays too.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Folder, Left: String;
begin
  Folder := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));

  // Here, not in InitializeUninstall: that comes before the player is asked whether to uninstall at all.
  if CurUninstallStep = usUninstall then
  begin
    if not RemoveContent(Folder) then
      Abort;

    DropResolutionCopies(Folder);
    DropGameName(OriginalPath(Folder));
    Exit;
  end;

  if CurUninstallStep <> usPostUninstall then
    Exit;

  Left := '';
  PutBackAll(Folder, '', Left);
  RemoveDir(BackupFolder(Folder));
  RestoreCompatibility(OriginalPath(Folder));
  RestoreResolutionCompatibility(Folder);

  if Left <> '' then
    SuppressibleMsgBox(FmtMessage(CustomMessage('RestoreFailed'), [BackupFolder(Folder), Left]), mbInformation,
                       MB_OK, IDOK);
end;
