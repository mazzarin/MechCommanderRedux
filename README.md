# MechCommander Redux

An unofficial recreation of **MechCommander Gold** (FASA Interactive / MicroProse, 1999) for modern 64-bit systems.

MechCommander Redux rebuilds MechCommander Gold's game code as 64-bit C++. The original game was a 32-bit Win32 program
built on DirectDraw, DirectSound, DirectPlay and Smacker video. Redux keeps the game's own classes, functions and logic,
rewritten as modern C++, and replaces that Windows/DirectX layer with SDL3.

It is not an emulator and not a patch to the original executable. It is a from-scratch reconstruction that reads the
data files from your own copy of MechCommander Gold.

## Download

**[⬇ Download the latest version for Windows (64-bit)](https://github.com/RevenantBob/MechCommanderRedux/releases/latest/download/MechCommanderRedux-win-x64.zip)**

**[⬇ Download the latest version for Mac (Apple silicon)](https://github.com/RevenantBob/MechCommanderRedux/releases/latest/download/MechCommanderRedux-mac-arm64.zip)**:
`MechCommander Redux.app`, which finds your copy of the game by itself (see `INSTALL-Mac.txt` in the zip).

This is a zip file with the game (`MCRedux.exe`), the Mission Editor (`MCEditor.exe`) and instructions. You also need
your own copy of MechCommander Gold: see [Running the Game](#running-the-game) and [Mission Editor](#mission-editor)
for how to set it up. To see what changed in each version, or to download an older one, visit the
[releases page](https://github.com/RevenantBob/MechCommanderRedux/releases).

## Table of Contents

- [MechCommander Redux](#mechcommander-redux)
  - [Download](#download)
  - [Table of Contents](#table-of-contents)
    - [Highlights](#highlights)
    - [Planned Features](#planned-features)
  - [Changes, Bug Fixes and Known Issues](CHANGES.md)
  - [Legal Notice](#legal-notice)
  - [Running the Game](#running-the-game)
    - [Installing](#installing)
    - [Command-Line Options](#command-line-options)
    - [Saves and Settings](#saves-and-settings)
  - [Mission Editor](#mission-editor)
    - [What You Need](#what-you-need)
    - [Setting Up the Editor](#setting-up-the-editor)
    - [What the Editor Changes in Your Game Folder](#what-the-editor-changes-in-your-game-folder)
    - [A Mission's Time of Day](#a-missions-time-of-day)
    - [A Mission's Weather](#a-missions-weather)
    - [Editor Command-Line Options](#editor-command-line-options)
    - [Troubleshooting the Editor](#troubleshooting-the-editor)
  - [Building](#building)
    - [Requirements](#requirements)
    - [Compiling](#compiling)
    - [Technologies](#technologies)
    - [Project Layout](#project-layout)
  
### Highlights

- Faithful game logic, rebuilt function by function from the original game.
- Renders at your window's resolution during missions (640x480 minimum) and scales the menu screens to fit.
- Multiplayer over TCP/UDP, in place of DirectPlay: LAN games are found automatically, and internet games connect by
  `host:port` (default port 28800).
- Windows first, with Linux planned.
- Many bugs in the *original* game were fixed in this recreation. See [CHANGES.md](CHANGES.md) for the changes, bug
  fixes and known issues.

### Planned Features

These are presented in no particular order.

- Complete C++23 Integration (Finish modernization object and memory management, code structure, etc.)
- Complete Hardware Rendering
  - The original game used software rendering to an offscreen buffer and copied this buffer to the DirectDraw surface. This behavior is still how rendering functions.
- Port editor to modern C++.
- Matchmaking/lobby service implementation.
- Update on menus to allow full screen rendering on different resolutions.
  - The original game menus were 640x480. Higher resolutions only took over during gameplay.



## Legal Notice

MechCommander Redux is an unofficial, non-commercial fan project. It is not affiliated with, endorsed by, or
sponsored by Microsoft, FASA Interactive, MicroProse, Catalyst Game Labs, or any other holder of the MechCommander,
BattleTech or MechWarrior trademarks and copyrights.

I do not own the rights to MechCommander Gold or any of its content. MechCommander, BattleTech, MechWarrior and all
related names, logos and artwork are trademarks or registered trademarks of their respective owners.

This repository contains no game data: no art, audio, video, maps, missions or executables from MechCommander Gold.
You must supply your own legally obtained copy of the game to play.

**Please do not contact me asking for a copy of the game.** I cannot and will not provide one.

## Running the Game

You need a legitimate installation of **MechCommander Gold**. Redux contains none of the game's data, and only reads
from your installation; it never modifies it.

### Installing

1. Download the latest MechCommander Redux zip file ([direct link](#download)).
2. Extract it. Either:
   - extract it into your MechCommander Gold folder (the one containing `SYSTEM.CFG`), then run `MCRedux.exe`; or
   - extract it anywhere you like, and point it at your MechCommander Gold folder with `--data` (see below).

On a Mac, drag `MechCommander Redux.app` to your Applications folder and open it (the first time, allow it under
System Settings → Privacy & Security; see `INSTALL-Mac.txt`).

Redux looks for the game's data in the first of these folders that contains `SYSTEM.CFG`:

1. The folder given with `-d` / `--data` (if it has no game, Redux shows an error listing every folder it tried)
2. The current folder
3. The folder `MCRedux.exe` is in
4. The folder chosen or found last time
5. On a Mac, a copy of the game (`SYSTEM.CFG` beside `MCX.EXE`) beside the app, in `/Applications` or
   `~/Applications` (including inside a Wineskin, Porting Kit or Kegworks app) or in a CrossOver bottle

If it finds several copies, or none, Redux asks which folder to play and remembers the answer. Hold Option (⌥) as the
Mac app opens to choose again.

### Command-Line Options

```
MCRedux.exe [-d|--data <install folder>] [--user <save folder>] [game options]
```

| Option                     | Description                                                              |
|----------------------------|--------------------------------------------------------------------------|
| `-d`, `--data <folder>`    | Your MechCommander Gold installation (the folder containing `SYSTEM.CFG`) |
| `--user <folder>`          | Where saves, preferences and temporary files go                          |
| `-h`, `--help`             | Shows the list of options                                                |

The original game's own options are passed through unchanged:

| Option            | Description                                                         |
|-------------------|---------------------------------------------------------------------|
| `-mission <n>`    | Starts mission segment `n` directly, skipping the logistics screens |
| `-load <file>`    | Loads a saved game                                                  |

For example:

```
MCRedux.exe --data "C:\Games\MechCommander Gold"
```

### Premade Matches

A launcher (or a script) can set up a multiplayer match before anyone starts the game, so nobody has to pick the
mission, drag names onto teams or type resource points. Start the host with `-host` plus the match, and each other
player with `-join` and the `-player` name the host's `-slot` entries use:

```
MCRedux.exe -host 28800 -player Alice -session "Match42" -maxplayers 4 ^
  -matchmap desert_duel -team1rp 20000 -team2rp 15000 -team1tech is -team2tech clan ^
  -slot Alice 1 0 -slot Bob 1 1 -slot Carol 2 0 -slot Dave 2 1

MCRedux.exe -join 192.168.1.10:28800 -player Bob -session "Match42"
```

When every player named by a `-slot` has joined, the host checks that they are exactly the roster and that every machine
has the mission file, then takes everyone **straight from "Waiting for Players" to the mech bay**. The ready room, the map
selector and the wait for the mission's settings are never loaded: the host fills in an `MPGameConfiguration` (mission,
points, tech bases, roster) and sends it to the guests in one message. Guests need no extra options.

| Option (host only)                    | Description                                                                          |
|---------------------------------------|--------------------------------------------------------------------------------------|
| `-matchmap <name>`                    | The multiplayer mission (`.mpk`), without folder or extension. Every player needs the same file in the same place. |
| `-team1rp <n>`, `-team2rp <n>`        | Resource points for **each player** of the team (the session screen's figure is a team's pool, shared out among its players). Without them the mission file's figures are kept. |
| `-team1tech <is\|clan>`, `-team2tech <is\|clan>` | The team's tech base. Without them the mission's are kept. |
| `-slot <player> <team> <slot>`        | Puts the player named `-player` on team 1 or 2, in slot 0 to 2 (the slot decides the player's drop zones). One per player, up to six. |
| `-matchweather <weather>`             | The weather every player sees: `clear`, `rain`, `downpour`, `lightsnow`, `snow` or `blizzard`. Without it (or with `mission`), the mission's own. |
| `-matchtime <time>`                   | The time of day every player sees: `day`, `dusk` or `night`. Without it (or with `mission`), the mission's own. Units see as far as in a mission set then: at dusk 0.8 of the day's sight, at night 0.6. |
| `-matchbraking <on\|off>`             | Whether mechs brake to a halt. Without it (or with `prefs`), the host's `MechBraking`. |
| `-matchartillery <on\|off>`           | Off: commanders get no artillery strikes. On (the original's) without it. |
| `-matchprobes <on\|off>`              | Off: commanders get no sensor probes or camera drones. On without it. |
| `-matchsensors <on\|off>`             | Off: units find their foes by sight alone: no unit's or sensor tower's sensors (a commander's sensor probe still senses). On without it. |
| `-matchmines <on\|off>`               | Off: no mines: the map's minefields are cleared and mine layers lay none. On without it. |
| `-matchpilot <rank>`                  | The most skilled pilot for hire: `elite` (the original's, without it), `veteran`, `regular` or `green`. |
| `-matchlrms`, `-matchsrms`, `-matchthunderbolts`, `-matchppcs`, `-matchlasers`, `-matchpulselasers`, `-matchflamers`, `-matchautocannons`, `-matchgauss`, `-matchmachineguns`, `-matchlongtoms` `<on\|off>` | Off: no weapon of that family, Inner Sphere's or Clan's (SRMs with Streaks, autocannons with Ultras and LB-Xs, Gauss rifles with the Rail Gun, Long Toms with the Sniper): none sold, a mech comes without them (their tonnage free for others, its price that much less), a vehicle carrying one isn't sold, and every unit dropped loses any it still has. On without it. |

The players in the session must be exactly the ones named, and the host must be among them. If one is missing, an extra
player joined, the host lacks the `.mpk`, or a guest says it lacks it (or doesn't answer within 15 seconds), the host is
left on the "Waiting for Players" screen with a log message and can press Go to carry on by hand. A locked mission keeps
its own points and tech bases. The game checks only that each player has a file of the mission's name, not that it is the
same file, so a launcher should check the files match.

The match options are also the host's own on the session screen's **MATCH OPTIONS** tab (under MULTIPLAYER CHAT:
WEATHER, TIME OF DAY, BRAKING, ARTILLERY, PROBES, SENSORS, MINES and MOST SKILLED PILOT, then WEAPONS, a row for each
family), and a host started with them but no `-matchmap` opens the
session screen with them chosen. Each is played only if every player's game has it: every build says what it has when
the session screen opens (and when asked for a mission file), the host passes on what each said, and while a player's
game lacks an option, the match plays the mission's own (braking off; artillery, probes, sensors, mines, elite pilots
and every weapon on) and every player's MATCH OPTIONS tells the players whose games lack it to upgrade, every one named
(under WEAPONS once for all its rows). A game that says nothing, as the original's, lacks them all. A player with
WEATHER or HD LIGHTING off (on the HD OPTIONS page) still sees the original's clear day.

### Saves and Settings

Without `--user`, saves and preferences go to your user profile, in
`%APPDATA%\MechCommander Redux\` on Windows and `~/Library/Application Support/MechCommander Redux/` on a Mac, so they
never touch the game folder.

## Mission Editor

`MCEditor.exe` is a 64-bit recreation of the **MechCommander Mission Editor**, the tool that came on the MechCommander
Gold CD for making your own maps and missions. It works like the original, but runs on modern Windows without any of
the old components the original needed: no MFC 4.2 DLLs, no Microsoft Jet or DAO database engine, no Visual Basic
runtime, and nothing to register with Windows. Everything it needs is built into the one `.exe` file.

The editor is optional. You don't need it to play the game.

### What You Need

1. **MechCommander Gold installed on your PC.** The editor uses the game's own art, terrain and settings, so it works
   inside the game's folder, just as the original editor did.
2. **One of these**, so the editor can get its own files (its templates, tables and mission database) the first time
   you run it:
   - the **MechCommander Gold CD** in your CD/DVD drive,
   - a **disc image of the CD** (an `.iso` file), or
   - a folder where the **original Mission Editor** was installed in the past.

You only need the CD (or the image, or the old folder) once. After the first run, the editor has everything it needs.

### Setting Up the Editor

**Step 1: Find your MechCommander Gold folder.** This is the folder that contains the files `SYSTEM.CFG` and
`MCX.EXE`.

**Step 2: Put the editor in that folder.** Copy `MCEditor.exe` from the release zip into the MechCommander Gold folder,
next to `SYSTEM.CFG`. If you installed the game the same way, `MCEditor.exe` will be next to `MCRedux.exe`.
`MCEditor.exe` doesn't need any other files next to it.

> If you'd rather keep `MCEditor.exe` somewhere else, you can, but then you have to tell it where the game is each time
> you start it, with `--data` (see [Editor Command-Line Options](#editor-command-line-options)).

**Step 3: Get the CD (or the image) ready.**

- **With the CD:** put it in the drive before you start the editor.
- **With an `.iso` file:** double-click the `.iso` file in Windows Explorer, or right-click it and choose **Mount**.
  Windows opens it as if it were a real CD, under a new drive letter in **This PC**.
- **With an old editor folder:** there's nothing to prepare. You'll choose the folder in the next step.

**Step 4: Run the editor for the first time.** Double-click `MCEditor.exe`. The first time, the editor notices that its
own files are missing and opens a window called **Mission Editor Setup**, asking where to copy them from:

- If the CD or a mounted `.iso` is there, the editor finds it by itself and fills in its drive (for example `D:\`).
  Just click **Install**.
- Otherwise, click **Browse** and choose either the CD's drive, or the folder where the original editor was installed
  (the one with a `Teditor` folder inside it; choosing the `Teditor` folder itself works too). Then click **Install**.

It only takes a moment. When it's done, the editor starts normally, and you won't see the setup window again.

Nothing on the CD is run: the editor reads its files straight out of the Mission Editor setup package on the CD
(`MCEDSETP\DISK1`), and checks each one as it unpacks it. Nothing is written to the Windows registry for this either.

### What the Editor Changes in Your Game Folder

- It adds a **`Teditor`** folder with the editor's own files (23 files: templates, tables, the starting inventories,
  the unit "brain" templates and the mission database).
- It adds **six small sound files** to `Data\Sound`: the clicks and sounds the editor plays.
- When you save your work, it writes your **maps and missions** into `Data\Terrain` and `Data\Missions`, just like the
  original editor.

It never replaces a file that's already there. If you want to be safe, make a copy of your MechCommander Gold folder
before you start making missions.

The first time the editor opens its mission database, it converts `Teditor\FitAbl.mdb` (the original's Microsoft
Access file) into a new file, `FitAbl.db`, next to it, and uses that from then on. The original `FitAbl.mdb` is left
untouched.

### A Mission's Time of Day

New in Redux: **Mission > Time of Day** sets a mission at **Day**, **Dusk** or **Night**. Missions are set by day
until you change it, as every mission of the original is.

- **Sight.** At dusk units see 80% as far as by day, at night 60%. This works in the original game too.
- **Look.** With **HD LIGHTING** set to **ON** (in Preferences), Redux shows a dusk mission darker, and a night one
  dark with the mechs' and vehicles' headlights on. The player's **DUSK** and **NIGHT** choices show their own time
  whatever the mission's. The original game shows every mission by day.
- **Multiplayer.** A match's host can show another time of day to everyone on the session screen's **MATCH OPTIONS**
  tab (or with `-matchtime`) when every player's game has it, and units then see as far as in a mission set at that
  time (by day as far as by day, even in a night mission).

For a mission's `.FIT` file, the editor writes this for dusk or night (by day, neither):

```
[GameScale]
...
l[256] VisualRangeTable =
	2,2,2,2,2,2,2,2,5,5,6,6,8,8,10,10,
	...

[Environment]
st TimeOfDay = "Night"
```

- `VisualRangeTable` replaces, for this mission, the game system's (`data\missions\gamesys.fit`): how many tiles a
  unit sees around it, by the height level of the ground under it. Write all 256 entries; the original game reads a
  shorter table on past its end.
- `[Environment]`'s `TimeOfDay` is `Day`, `Dusk` or `Night` (in any case). Without it, or with any other name, the
  mission is by day. The original game skips the block.

### A Mission's Weather

New in Redux: **Mission > Weather** sets a mission in **Clear** weather, **Rain**, a **Downpour**, **Light Snow**,
**Snow** or a **Blizzard**. Missions are clear until you change it, as every mission of the original is.

- **Look.** With **WEATHER** set to **ON** (in Preferences, on the HD OPTIONS page), Redux rains or snows on the
  mission, a downpour with thunder, the snow settling as the mission goes on. The player's **RAIN**, **SNOW** and other
  choices show every mission in that weather whatever its own. The original game shows every mission under a clear sky.
  On a computer that can't draw a snowfall at the screen's rate, its farthest flakes fall as specks instead, and as
  flakes again once there's time.
- **Multiplayer.** A match's host can pick another weather for everyone on the session screen's **MATCH OPTIONS** tab
  (or with `-matchweather`, see [Premade Matches](#premade-matches)) when every player's game has it; left at
  **MISSION'S**, the mission's own shows.
- **Play.** In a match whose every player's game has it, the weather the match plays (the host's, or the mission's
  own) changes play; elsewhere it changes nothing, units seeing, moving and fighting as under a clear sky:

  | Weather    | Fires burn | Fires spread | Long-range aim | Sight | A mech's piloting check | Vehicles' speed |
  |------------|------------|--------------|----------------|-------|-------------------------|-----------------|
  | Rain       | 0.6        | 5% a touch   |                |       | +5                      | 0.95            |
  | Downpour   | 0.3        | 2%           | -5             | 0.8   | +10                     | 0.9             |
  | Light snow | 0.8        | 8%           |                |       | +5                      | 0.95            |
  | Snow       | 0.6        | 5%           |                | 0.9   | +10                     | 0.85            |
  | Blizzard   | 0.4        | 3%           | -5             | 0.7   | +15                     | 0.75            |

  A flame burns for that share of the mission's time, and a fire spreads to what it touches at that chance (clear:
  10%). Shots at a weapon's long range get that many points off their chance to hit. Units see that share as far (after
  the time of day's cut). The piloting check's roll gets that much more out of 100, so a mech is likelier to fall when a
  hit to its legs or gyro, or a jump's landing, asks for one. Sensors see as they do.

For a mission's `.FIT` file, the editor writes this for rain or a downpour (for a clear sky, nothing):

```
[Environment]
st Weather = "Rain"
```

- `Weather` is `Clear`, `Rain`, `Downpour`, `LightSnow`, `Snow` or `Blizzard` (in any case). Without it, or with any
  other name, the mission is clear. The original game skips the block.
- A mission set at dusk or night too has its `TimeOfDay` in the same block. Write a single `[Environment]` block: Redux
  reads only the first.

### Editor Command-Line Options

```
MCEditor.exe [-d|--data <game folder>]
```

| Option                  | Description                                                                                  |
|-------------------------|----------------------------------------------------------------------------------------------|
| `-d`, `--data <folder>` | Your MechCommander Gold folder (the one containing `SYSTEM.CFG`), if `MCEditor.exe` isn't in it |

The easiest way to use it is a shortcut: right-click `MCEditor.exe`, choose **Create shortcut**, open the shortcut's
**Properties**, and add the option to the end of **Target**, for example:

```
"C:\Tools\MCEditor.exe" --data "C:\Games\MechCommander Gold"
```

### Troubleshooting the Editor

| What you see | What it means and what to do |
|---|---|
| *"... isn't a MechCommander Gold folder: it has no SYSTEM.CFG."* | `MCEditor.exe` isn't in your game folder. Move it there (step 2), or start it with `--data` and your game folder. |
| *"... has neither the Mission Editor's files (a Teditor folder) nor its setup ..."* | The folder you chose isn't the CD, a mounted CD image, or an installed editor. Click **Browse** and choose the CD's drive itself (for example `D:\`), or the folder that contains the old editor's `Teditor` folder. |
| *"Couldn't make ..."*, *"Couldn't copy ..."* or *"Couldn't write ..."* | Windows didn't let the editor write into your game folder. This usually happens when the game is installed under `C:\Program Files` or `C:\Program Files (x86)`, which normal programs aren't allowed to change. The best fix is to move (or reinstall) MechCommander Gold to a folder of your own, such as `C:\Games\MechCommander Gold`, because the editor also saves your missions there. Running `MCEditor.exe` as administrator (right-click, **Run as administrator**) also works. |
| You clicked **Exit** in the setup window by mistake | Nothing was changed. Start the editor again and it asks again. |
| **Help > Help Topics** (or **Help** on a right-click menu) doesn't open | The original help file uses Windows Help (`.hlp`), which Windows 10 and 11 can no longer show. This doesn't affect the rest of the editor. |

## Building

### Requirements

- **Visual Studio 2026** (Community, Professional or Enterprise) with the **Desktop development with C++** workload,
  which includes the v145 toolset and the Windows SDK.

That's all. SDL3 is included in the repository and built as part of the solution, so nothing else needs to be
installed.

The version number (`major.minor.release.build`) comes from [Nerdbank.GitVersioning](https://github.com/dotnet/Nerdbank.GitVersioning):
`version.json` sets the first three parts, and the build number counts the commits since that version was set. The build
runs it through the **.NET SDK** (8 or later), which Visual Studio usually installs. Without the .NET SDK, or outside a git
checkout, the build still works but reports version `0.0.0.0`. To start a new version, edit `version.json`; the build number
starts again from 0.

### Compiling

Open `src/MCRedux.slnx` in Visual Studio 2026, select the `x64` platform and either the `Debug` or `Release`
configuration, then build the solution.

From a Developer Command Prompt:

```
msbuild src\MCRedux.slnx -p:Configuration=Release -p:Platform=x64 -m
```

The output goes to `src/build/bin/x64/<Configuration>/`:

| File           | Purpose                      |
|----------------|------------------------------|
| `MCRedux.exe`  | The game                     |
| `MCEditor.exe` | The Mission Editor           |
| `mc_tests.exe` | The test suite               |
| `SDL3.dll`     | SDL3 runtime                 |

### Compiling on macOS

The game and the test suite also build for macOS on Apple Silicon, with CMake and Xcode 16.3 or later; the game runs on
macOS 14 or later (the Mission Editor is Windows only). The GPU renderer runs on Metal: the build compiles the HLSL shaders with
glslang (dxc has no macOS build) and translates them to Metal's shading language with SPIRV-Cross.

```
xcode-select --install
brew install cmake glslang spirv-cross
cmake -S src/mac -B src/mac/build
cmake --build src/mac/build --parallel "$(sysctl -n hw.ncpu)"
```

The output goes to `src/mac/build/`: `MechCommander Redux.app` (the game: open it, or run
`"MechCommander Redux.app/Contents/MacOS/MechCommander Redux" --data <your MechCommander Gold folder>`) and `mc_tests`
(the test suite). The source lists come from the Visual Studio projects, so a file added to them builds on
macOS too. Without the .NET SDK the version is `0.0.0.0`, as on Windows.

### Releases

Releases come from the **Release** workflow on GitHub (Actions → Release → Run workflow). It builds `main`, runs the
tests, tags the commit `v<version>` (the same version the build puts in the exe) and publishes a GitHub Release with
the game zip `MechCommanderRedux-win-x64.zip` (`MCRedux.exe`, `SDL3.dll`, `MCEditor.exe`, and `src/docs/README.txt`
and `INSTALL.txt`), a symbols zip, `MechCommanderRedux-win-x64-symbols.zip`, and the Mac app,
`MechCommanderRedux-mac-arm64.zip` (`MechCommander Redux.app` and `src/docs/INSTALL-Mac.txt`, built on a macOS runner;
signed ad hoc, as there's no Apple developer account). The zips have the same names in every
release, so the [Download](#download) link always gets the newest one. It refuses to run twice for the same commit, since the tag would already exist.

### Technologies

- **C++23** (MSVC `/std:c++latest`)
- **SDL3** for windowing, input, audio and timing
- **SDL3's GPU renderer** (Vulkan where available) to present the game's 8-bit frame buffer, scaled to the window
- **TCP/UDP sockets** in place of DirectPlay for multiplayer
- A built-in **Smacker** video decoder for the game's cutscenes
- **MFC** (linked statically) for the Mission Editor, as the original editor used
- **SQLite** for the editor's mission database, in place of Microsoft Jet/DAO
- **miniz** to unpack the editor's files from the CD's InstallShield setup
- **MSBuild / Visual Studio 2026** (toolset v145)

### Project Layout

| Path                          | Contents                                                           |
|-------------------------------|--------------------------------------------------------------------|
| `src/lib/MCCore/`             | The reconstructed game, with folders mirroring the original source |
| `src/lib/MCCore/platform/`    | The SDL3 platform layer (display, input, audio, files, network)    |
| `src/lib/SDL-release-3.4.12/` | SDL3                                                               |
| `src/apps/MCRedux/`           | The game executable and the test suite                             |
| `src/apps/MCEditor/`          | The Mission Editor                                                 |
| `src/lib/sqlite-3.50.4/`      | SQLite, for the editor's mission database                          |
| `src/lib/miniz-3.0.2/`        | miniz, for unpacking the editor's files from the CD                |
| `src/mac/`                    | The macOS build (CMake) and its shader translator                  |
