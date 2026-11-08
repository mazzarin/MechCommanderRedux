# MechCommander Redux HD

**MechCommander Gold** (FASA Interactive / MicroProse, 1999) for today's computers, with sharper art, light, weather
and sound.

Redux HD is a build of [MechCommander Redux](https://github.com/RevenantBob/MechCommanderRedux), RevenantBob's rebuild
of the original game's code as 64-bit C++ on SDL3. It plays the original's missions by the original's rules, and adds
an HD pack and options that change the look (and, where marked, the play). Every addition is an option: ORIGINAL, on
the HD OPTIONS page, puts the original's look back.

What only Redux HD has is marked **Redux HD** here and in [CHANGES.md](CHANGES.md). The rest is MechCommander Redux's,
the same as [its own README](https://github.com/RevenantBob/MechCommanderRedux#readme) describes.

## Download

**[⬇ The latest version](https://github.com/mazzarin/MechCommanderRedux/releases/latest)**, with MechCommander Gold
and the HD pack:

- **Mac** (Apple silicon, macOS 14 or later): the installer, the `.pkg`. The app alone, the `-mac.zip` (about 7 MB),
  plays your own copy of MechCommander Gold (see [Installing](#installing)).
- **Windows** (Windows 10 or later): the setup, the `-setup.exe`. It installs Redux HD in your own copy of
  MechCommander Gold, or downloads the game.

The first start downloads the game's files (0.6 GB) and, if you chose it, the HD pack (5.8 GB). Neither installer is
signed yet, so macOS and Windows stop it the first time: [Installing](#installing) has the steps. Once installed,
the game updates itself: when a new version is out, UPDATE on the main menu turns amber. What each version brought:
[CHANGELOG.md](CHANGELOG.md).

Without the pack the units, ground, menus, movies and sound are the original's; the light, weather and effects show
all the same. MechCommander Redux's own [download](https://github.com/RevenantBob/MechCommanderRedux/releases/latest)
has none of Redux HD's additions.

## Community

Join MechCommander Redux's development Discord server to follow progress or help out:
**[discord.gg/TZyZ9Vbj37](https://discord.gg/TZyZ9Vbj37)**

Please report bugs in Redux HD on GitHub, in [its issue tracker](https://github.com/mazzarin/MechCommanderRedux/issues),
not on Discord.

## Table of Contents

- [MechCommander Redux HD](#mechcommander-redux-hd)
  - [Download](#download)
  - [Community](#community)
  - [What Redux HD Adds](#what-redux-hd-adds)
  - [Coming](#coming)
  - [Reports](#reports)
  - [What Each Version Brought](CHANGELOG.md)
  - [Changes, Bug Fixes and Known Issues](CHANGES.md)
  - [Legal Notice](#legal-notice)
  - [Running the Game](#running-the-game)
    - [Installing](#installing)
    - [Command-Line Options](#command-line-options)
    - [Premade Matches](#premade-matches)
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
    - [Compiling on macOS](#compiling-on-macos)
    - [Releases](#releases)
    - [Technologies](#technologies)
    - [Project Layout](#project-layout)

## What Redux HD Adds

Its options are on by default, each a row on the HD OPTIONS page (PREFERENCES, then OPTIONS under HD).

- **HD art.** The units, buildings, ground, effects, menus and movies, sharper: made larger from the original's own
  pictures by upscaling models, and checked against them.
- **Light.** Shots, fires and explosions light the battlefield around them. Dusk darkens a mission, and night darkens
  it further, the mechs' and vehicles' headlights on. Shadows darken the ground instead of solid black, the sun shows
  the ground's bumps and slopes, clouds' shadows drift over the battlefield, and forests sway in the wind.
- **Weather.** Rain, downpours with thunder and lightning, light snow, snow and blizzards. Puddles gather and feet
  splash in them, the rain catches the lights at dusk and night, and the snow settles as the mission goes on. A
  mission has the weather its map maker set, or every mission has the weather you choose.
- **Effects.** Flecks, debris, smoke, embers, splashes and wakes. The biggest blasts shake the screen. Mechs leave
  footprints where they step, and more prints and craters stay.
- **Mechs.** They turn smoothly, and a mech held up mid-stride keeps its stride. BRAKING (it changes play) has them
  brake to a halt through the stopping art the original drew, instead of stopping dead.
- **Sound.** The HD pack's music, effects and voices, sharper; rain and thunder.
- **Multiplayer.** The host's MATCH OPTIONS: the weather and time of day everyone plays in (in a match, both change
  sight, and the weather aim, footing, fires and vehicles' speed), braking, and artillery, probes, sensors and mines on
  or off. A player whose computer drops out leaves within half a minute. The host checks that everyone has its mission
  and game data, and names whoever doesn't.
- **Mission Editor.** Mission > Time of Day and Mission > Weather.
- **Menus.** UPDATE on the main menu (the Mac app updates itself), and INTERFACE SIZE on the Preferences screen.

## Coming

- The one download per platform: the app, the game and the HD pack.
- Windows: a release that updates itself, as the Mac's does.
- Updates to the HD pack, and new missions and maps, downloaded as they change.

## Reports

**Redux HD.** The game can send short reports of how it runs, to make it run well on more computers. It tells you the
first time it starts (or, where the law requires it, asks you first); REPORTS, on the HD OPTIONS page, turns them off.

- **What:** the computer (its model, processor, graphics, memory and system version), the window and settings, how
  smoothly missions and matches ran, and crashes with what led up to them.
- **When:** after each multiplayer match, and after missions: the first, then at most once a week.
- **Anonymous:** under a random ID your copy of the game makes, with no names, paths, saves, chat or addresses. The
  server keeps the country a report came from, never the IP address.

## Legal Notice

MechCommander Gold © 1999 Microsoft Corporation. All rights reserved. BattleTech material © 1998-1999 FASA
Corporation.

MechCommander Redux and Redux HD are unofficial, non-commercial fan projects. They are not affiliated with, endorsed
by, or sponsored by Microsoft, FASA Interactive, MicroProse, Catalyst Game Labs, or any other holder of the
MechCommander, BattleTech or MechWarrior trademarks and copyrights. MechCommander, BattleTech, MechWarrior and all
related names, logos and artwork are trademarks or registered trademarks of their respective owners.

This repository contains no game data: no art, audio, video, maps, missions or executables from MechCommander Gold.
Redux HD's download includes MechCommander Gold's files free of charge, with permission. The app alone plays your own
copy.

The HD pack is free and for non-commercial use only: it may not be sold (the licence of NVIDIA's model that sharpened
the sound effects requires this). The download credits the models that made it.

## Running the Game

The app alone needs a legitimate installation of **MechCommander Gold**. It only reads from your installation; it never
modifies it.

### Installing

Neither installer is signed yet (by an Apple developer account, or a Windows code signing certificate), so macOS and
Windows stop it the first time. Nothing is wrong with it: they stop any program that isn't signed.

**Redux HD on a Mac**, the [download](#download)'s `.pkg`:

1. Double-click the `.pkg`. macOS stops it: *"MechCommander-Redux-....pkg" Not Opened*. Click **Done**. Don't click
   **Move to Trash**, and don't press Return: that deletes the installer.
2. Open **System Settings**, click **Privacy & Security**, scroll down to **Security**, and click **Open Anyway** next
   to the installer. Click **Open Anyway** again, and type your Mac's password if it asks.
3. In the installer, click **Continue**, then **Agree**. Leave **Redux HD art, sound and movies** ticked for the HD
   pack, or untick it. Click **Install** and type your Mac's password. It puts the **MechCommander Redux HD** folder in
   Applications.
4. Open Applications, then **MechCommander Redux HD**, and double-click **MechCommander Redux HD**. The first time, a
   small window shows the game's files downloading, then the main menu opens.

The app alone, the `-mac.zip`, plays your own copy of MechCommander Gold: unzip it, drag `MechCommander Redux.app` to
your Applications folder and open it from there. macOS stops it the same way: click **Done**, then **Open Anyway** in
Privacy & Security as above.

**Redux HD on Windows**, the download's `-setup.exe`:

1. If your browser says the file isn't commonly downloaded, keep it (in Edge: **...**, **Keep**, then **Show more**,
   **Keep anyway**).
2. Run it. Windows shows *Windows protected your PC*: click **More info**, then **Run anyway**.
3. Choose **Download it**, or **Use my copy of MechCommander Gold** and its folder (the one containing `SYSTEM.CFG`).
   Leave **Redux HD art, sound and movies** ticked for the HD pack, or untick it. Click **Next**, then **Install**;
   the setup downloads the game's files before it finishes.

If Windows says *Smart App Control blocked* the setup instead, there's no Run anyway: Windows 11's Smart App Control
runs only signed programs. It runs with Smart App Control off (Windows Security, **App & browser control**, **Smart
App Control settings**, **Off**), but older versions of Windows 11 can't turn it back on without resetting Windows, so
that choice is yours.

Or [build it](#building), then either put `MCRedux.exe` and `SDL3.dll` in your MechCommander Gold folder, or run it
from anywhere with `--data` (see below).

**Redux HD:** the HD pack is a folder named `HD` in your game folder. One in your user folder (see
[Saves and Settings](#saves-and-settings)) is used instead, if there is one.

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
| `-matchweather <weather>`             | **Redux HD.** The match's weather: `clear`, `rain`, `downpour`, `lightsnow`, `snow` or `blizzard`. Without it (or with `mission`), the mission's own. It changes play as [A Mission's Weather](#a-missions-weather) says. |
| `-matchtime <time>`                   | **Redux HD.** The time of day every player sees: `day`, `dusk` or `night`. Without it (or with `mission`), the mission's own. Units see as far as in a mission set then: at dusk 0.8 of the day's sight, at night 0.6. |
| `-matchbraking <on\|off>`             | **Redux HD.** Whether mechs brake to a halt. Without it (or with `prefs`), the host's `MechBraking`. |
| `-matchartillery <on\|off>`           | **Redux HD.** Off: commanders get no artillery strikes. On (the original's) without it. |
| `-matchprobes <on\|off>`              | **Redux HD.** Off: commanders get no sensor probes or camera drones. On without it. |
| `-matchsensors <on\|off>`             | **Redux HD.** Off: units find their foes by sight alone: no unit's or sensor tower's sensors (a commander's sensor probe still senses). On without it. |
| `-matchmines <on\|off>`               | **Redux HD.** Off: no mines: the map's minefields are cleared and mine layers lay none. On without it. |

The players in the session must be exactly the ones named, and the host must be among them. If one is missing, an extra
player joined, the host lacks the `.mpk`, or a guest says it lacks it (or doesn't answer within 15 seconds), the host is
left on the "Waiting for Players" screen with a log message and can press Go to carry on by hand. A locked mission keeps
its own points and tech bases. The game checks only that each player has a file of the mission's name, not that it is the
same file, so a launcher should check the files match.

**Redux HD.** The match options are also the host's own on the session screen's **MATCH OPTIONS** tab (under MULTIPLAYER CHAT:
WEATHER, TIME OF DAY, BRAKING, ARTILLERY, PROBES, SENSORS and MINES), and a host started with them but no `-matchmap` opens the
session screen with them chosen. Each is played only if every player's game has it: every build says what it has when
the session screen opens (and when asked for a mission file), the host passes on what each said, and while a player's
game lacks an option, the match plays the mission's own (braking off; artillery, probes, sensors and mines on) and
every player's MATCH OPTIONS tells the players whose games lack it to upgrade, by name while the names fit the line. A game that says nothing, as the original's, lacks them all. A player with
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

**Redux HD:** its editor adds [Mission > Time of Day](#a-missions-time-of-day) and
[Mission > Weather](#a-missions-weather). Redux HD's Windows setup installs it beside the game; MechCommander Redux's
own editor has neither.

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

**Redux HD.** **Mission > Time of Day** sets a mission at **Day**, **Dusk** or **Night**. Missions are set by day
until you change it, as every mission of the original is.

- **Sight.** At dusk units see 80% as far as by day, at night 60%. This works in the original game too.
- **Look.** With **HD LIGHTING** set to **ON** (on the HD OPTIONS page), Redux HD shows a dusk mission darker, and a
  night one dark with the mechs' and vehicles' headlights on. The player's **DUSK** and **NIGHT** choices show their
  own time whatever the mission's. The original game shows every mission by day.
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

**Redux HD.** **Mission > Weather** sets a mission in **Clear** weather, **Rain**, a **Downpour**, **Light Snow**,
**Snow** or a **Blizzard**. Missions are clear until you change it, as every mission of the original is.

- **Look.** With **WEATHER** set to **ON** (on the HD OPTIONS page), Redux HD rains or snows on the
  mission, a downpour with thunder, the snow settling as the mission goes on. The player's **RAIN**, **SNOW** and other
  choices show every mission in that weather whatever its own. The original game shows every mission under a clear sky.
  On a computer that can't draw a snowfall at the screen's rate, its farthest flakes fall as specks instead, and as
  flakes again once there's time.
- **Multiplayer.** A match's host can pick another weather for everyone on the session screen's **MATCH OPTIONS** tab
  (or with `-matchweather`, see [Premade Matches](#premade-matches)) when every player's game has it; left at
  **MISSION'S**, the mission's own shows.
- **Play.** In a match whose every player's game has it, the weather the match plays (the host's, or the mission's
  own) changes play. Everywhere else (single-player missions too) it changes nothing: units see, move and fight as
  under a clear sky. The first row is the original's, the clear sky's:

  | Weather              | Flames burn | A fire catches what it touches | Hit chance at long range | Sight | Piloting checks | Vehicles' speed |
  |----------------------|-------------|--------------------------------|--------------------------|-------|-----------------|-----------------|
  | Clear (the original) | 100%        | 10% of the time                | same                     | 100%  | same            | 100%            |
  | Rain                 | 60%         | 5%                             | same                     | 100%  | 5 points harder | 95%             |
  | Downpour             | 30%         | 2%                             | 5 points less            | 80%   | 10 points harder | 90%             |
  | Light snow           | 80%         | 8%                             | same                     | 100%  | 5 points harder | 95%             |
  | Snow                 | 60%         | 5%                             | same                     | 90%   | 10 points harder | 85%             |
  | Blizzard             | 40%         | 3%                             | 5 points less            | 70%   | 15 points harder | 75%             |

  - **Flames burn** for that share of the mission's burn time, so fires die out sooner.
  - **A fire catches** a tree, building or anything else that burns, each time it touches one, that share of the time:
    in rain, half as often as under a clear sky.
  - **Hit chance at long range:** a shot at a weapon's long range loses that many points of its chance to hit; nearer
    shots are as before.
  - **Sight:** units see that share as far (after the time of day's cut). Sensors see as far as ever.
  - **Piloting checks** are that many points (out of 100) harder, so a mech is likelier to fall when a hit to its legs
    or gyro, or a jump's landing, calls for one.
  - **Vehicles** drive at that share of their speed.

For a mission's `.FIT` file, the editor writes this for any weather but a clear sky (for that, nothing):

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

**Redux HD.** The version number (`major.minor.release.build`) is `version.json`'s first three parts with the
branch's count of commits as the build (`git rev-list --count HEAD`), the same on Windows and the Mac, and the game
shows it as `0.2.1 (534)`. The build counts through git (Visual Studio's own will do). Outside a git checkout the build
still works but reports build 0, and a shallow clone counts only the commits it has. To start a new version, edit
`version.json`; the build number keeps rising, which the updaters need (they install only a higher build).

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
macOS too. Its version comes from `version.json` and git, as on Windows.

### Releases

**Redux HD:** the Mac app is released on
[this fork's Releases page](https://github.com/mazzarin/MechCommanderRedux/releases), each with a higher build than
the last, beside `appcast.xml`, the feed every copy checks for a newer version
([Sparkle](https://sparkle-project.org); the updates are signed with a key kept off the repository). This branch's
commits are checked on Windows by pushing them to `windows-HD`, which builds and tests Redux HD there
(`.github/workflows/windows.yml`), keeping the exes for a week.

MechCommander Redux's own releases come from the **Release** workflow (Actions → Release → Run workflow), which builds
`main`, runs the tests, tags the commit `v<version>` and publishes the Windows zip, its symbols and the Mac app on
[its Releases page](https://github.com/RevenantBob/MechCommanderRedux/releases).

### Technologies

- **C++23** (MSVC `/std:c++latest`)
- **SDL3** for windowing, input, audio and timing
- **SDL3's GPU renderer** (Vulkan where available, Metal on the Mac) to present the game's 8-bit frame buffer, scaled
  to the window, and (Redux HD) to draw the HD pack's art, the light and the weather over it
- **TCP/UDP sockets** in place of DirectPlay for multiplayer
- A built-in **Smacker** video decoder for the game's cutscenes
- **MFC** (linked statically) for the Mission Editor, as the original editor used
- **SQLite** for the editor's mission database, in place of Microsoft Jet/DAO
- **miniz** to unpack the editor's files from the CD's InstallShield setup
- **MSBuild / Visual Studio 2026** (toolset v145), and **CMake** with Xcode on the Mac
- **mimalloc**, the game's memory allocator
- Redux HD: **dav1d** (the HD movies, AV1), **libdeflate** (the HD pack's archives), **bc7enc_rdo** (its textures),
  **dr_flac** (its sound), **xxHash**, and **Sparkle** (the Mac app's updates)
- Redux HD's content updates: **libsodium** (their signatures and hashes), **JSON for Modern C++** (their files),
  **miniz**'s zip reader and **HDiffPatch**'s patcher, over the systems' own HTTPS (NSURLSession, WinHTTP)

### Project Layout

| Path                          | Contents                                                           |
|-------------------------------|--------------------------------------------------------------------|
| `src/lib/MCCore/`             | The reconstructed game, with folders mirroring the original source |
| `src/lib/MCCore/platform/`    | The SDL3 platform layer (display, input, audio, files, network)    |
| `src/lib/SDL-release-3.4.12/` | SDL3                                                               |
| `src/apps/MCRedux/`           | The game executable and the test suite                             |
| `src/apps/MCEditor/`          | The Mission Editor                                                 |
| `src/lib/sqlite-3.50.4/`      | SQLite, for the editor's mission database                          |
| `src/lib/miniz-3.0.2/`        | miniz, for unpacking the editor's files from the CD (and Redux HD's update zips) |
| `src/lib/mimalloc-3.3.1/`     | mimalloc, the memory allocator                                     |
| `src/lib/dav1d-1.5.4/`, `libdeflate-1.26/`, `bc7enc_rdo/`, `dr_flac/`, `xxHash-0.8.4/` | Redux HD: the HD pack's movies, archives, textures and sound |
| `src/lib/Sparkle-2.10.1/`     | Redux HD: the Mac app's updater                                    |
| `src/lib/libsodium-1.0.22/`, `nlohmann-json-3.12.0/`, `HDiffPatch-5.1.3/` | Redux HD: the content updates' signatures, files and patches |
| `src/mac/`                    | The macOS build (CMake) and its shader translator                  |
| `HD_AUDIO.md`                 | Redux HD: the HD audio pack's layout, for pack makers              |
