MechCommander Redux
===================

An unofficial recreation of MechCommander Gold (FASA Interactive / MicroProse, 1999)
for modern 64-bit systems.

MechCommander Redux rebuilds MechCommander Gold's game code as 64-bit C++, keeping the
game's own logic and replacing its old Windows/DirectX layer with SDL3. It is not an
emulator and not a patch to the original executable.

Redux HD comes with MechCommander Gold's files and the HD pack (sharper art, movies and
sound), or plays your own copy of the game. UPDATE on the main menu keeps the game and
its files up to date. If an update replaces a file you changed, your copy is kept in
the game folder's Redux backup folder.

- Faithful game logic, rebuilt function by function from the original game.
- Renders at your window's resolution during missions (640x480 minimum), and scales
  the menu screens to fit.
- Multiplayer over TCP/UDP in place of DirectPlay: LAN games are found automatically,
  and internet games connect by host:port (default port 28800).
- Many bugs in the original game are fixed.


Running the Game
----------------

Redux HD's setup installs it in your MechCommander Gold folder, or downloads the game
into a folder of its own. Start it from the Start menu. MCX.EXE in that folder starts
Redux too, so a launcher that starts the original starts Redux; the uninstall puts the
original MCX.EXE back.

MCRedux.exe also runs from any MechCommander Gold folder (the one containing
SYSTEM.CFG), or kept somewhere else, pointed at the game folder:

    MCRedux.exe --data "C:\Games\MechCommander Gold"

Options:

    -d, --data <folder>   Your MechCommander Gold folder (the one containing SYSTEM.CFG)
    --user <folder>       Where saves, preferences and temporary files go
    -h, --help            Shows the list of options
    -mission <n>          Starts mission segment n directly, skipping logistics
    -load <file>          Loads a saved game

Saves and preferences go to %APPDATA%\MechCommander Redux\ unless --user is given, so
they never touch the game folder.


Mission Editor
--------------

MCEditor.exe is a 64-bit recreation of the MechCommander Mission Editor, the tool that
came on the MechCommander Gold CD for making your own maps and missions. It works just
like the original, but runs on modern Windows without any of the old components the
original needed (MFC 4.2, Microsoft Jet/DAO, the Visual Basic runtime and so on).

Using the editor is optional. You don't need it to play the game.


What you need

1. MechCommander Gold installed on your PC. The editor uses the game's own art, maps
   and settings, so it has to work inside the game's folder.

2. ONE of these, so the editor can get its own files the first time you run it:
   - the MechCommander Gold CD in your CD/DVD drive, or
   - a disc image of that CD (an .iso file), or
   - a folder where the original Mission Editor was installed in the past.

   You only need this once. After the first run, the editor has everything it needs.


Step 1: Find your MechCommander Gold folder

This is the folder that contains the files SYSTEM.CFG and MCX.EXE.


Step 2: Put the editor in that folder

Copy MCEditor.exe into the MechCommander Gold folder, next to SYSTEM.CFG (and next to
MCRedux.exe, if you put the game there too). MCEditor.exe doesn't need any other files
next to it.

If you would rather keep MCEditor.exe somewhere else, you can. Then you have to tell it
where the game is each time you start it, with the --data option. For example, make a
shortcut to MCEditor.exe, open the shortcut's Properties, and change "Target" to:

    "C:\Tools\MCEditor.exe" --data "C:\Games\MechCommander Gold"

(using your own folders). The short form -d works too.


Step 3: Run the editor for the first time

Double-click MCEditor.exe. The first time, the editor notices that its own files are
missing and shows a window called "Mission Editor Setup". It asks where to copy its files
from. Choose one of these:

- If you have the CD: put it in the drive before you start the editor. The editor finds
  it by itself and fills in the drive (for example D:\). Just click Install.

- If you have an .iso file of the CD: double-click the .iso file in Windows Explorer (or
  right-click it and choose "Mount"). Windows opens it as if it were a CD, under a new
  drive letter in "This PC". Then start the editor; it finds the mounted image the same
  way it finds a real CD. If you mounted the image after starting the editor, click
  Browse and choose that drive.

- If you have a folder where the original Mission Editor was installed: click Browse and
  choose that folder. (It's the one that has a "Teditor" folder inside it. Choosing the
  Teditor folder itself works too.)

Then click Install. It only takes a moment. When it's done, the editor starts normally,
and you won't see this window again.

Nothing on the CD is run: the editor reads its files straight out of the CD's Mission
Editor setup package. Nothing is written to the Windows registry either.


What the editor adds to your game folder

- A Teditor folder, with the editor's templates, tables and mission database.
- Six small sound files in Data\Sound, the clicks and sounds the editor plays.
- When you save your work: your maps and missions, in Data\Terrain and Data\Missions,
  just like the original editor.

It never replaces a file that's already there. If you want to be safe, make a copy of
your MechCommander Gold folder before you start making missions.

The first time the editor opens its mission database, it converts Teditor\FitAbl.mdb
(the original's old Microsoft Access file) into a new file, FitAbl.db, next to it, and
uses that from then on. The original FitAbl.mdb is left untouched.


A mission's time of day

Redux HD only: Mission > Time of Day sets a mission at Day, Dusk or Night. Missions are
set by day until you change it, as every mission of the original is.

- At dusk units see 80% as far as by day, at night 60%. This works in the original game
  too.
- With HD LIGHTING set to ON (on the HD OPTIONS page), Redux HD shows a dusk mission
  darker, and a night one dark with the mechs' and vehicles' headlights on. The
  player's DUSK and NIGHT choices show their own time whatever the mission's.

In the mission's .FIT file this is [Environment]'s st TimeOfDay ("Day", "Dusk" or
"Night"), and a VisualRangeTable of 256 entries in [GameScale]; the project's README.md
has the details.


A mission's weather

Redux HD only: Mission > Weather sets a mission in Clear weather, Rain, a Downpour, Light
Snow, Snow or a Blizzard. Missions are clear until you change it, as every mission of the
original is.

- With WEATHER set to ON (on the HD OPTIONS page), Redux HD rains or snows on the mission,
  a downpour with thunder. The player's RAIN, SNOW and other choices show every mission
  in that weather whatever its own.
- In a match whose every player's game has it, the weather changes play: fires, sight,
  long-range aim, mechs' footing and vehicles' speed. Elsewhere it changes nothing.

In the mission's .FIT file this is [Environment]'s st Weather ("Clear", "Rain",
"Downpour", "LightSnow", "Snow" or "Blizzard"), in the same [Environment] block as a dusk
or night mission's TimeOfDay; the project's README.md has the details.


If something goes wrong

"... isn't a MechCommander Gold folder: it has no SYSTEM.CFG."
    MCEditor.exe isn't in your game folder. Move it there (step 2), or start it with
    --data and your game folder.

"... has neither the Mission Editor's files (a Teditor folder) nor its setup ..."
    The folder you chose isn't the CD, a mounted CD image, or an installed editor. Click
    Browse and choose the CD's drive itself (for example D:\), or the folder that
    contains the old editor's Teditor folder.

"Couldn't make ..." or "Couldn't copy ..." or "Couldn't write ..."
    Windows didn't let the editor write into your game folder. This usually happens
    when the game is installed under C:\Program Files or C:\Program Files (x86), which
    normal programs aren't allowed to change. Either move (or reinstall) MechCommander
    Gold to a folder of your own, such as C:\Games\MechCommander Gold, or right-click
    MCEditor.exe and choose "Run as administrator". The editor also saves your missions
    into the game folder, so moving the game out of Program Files is the better fix.

You clicked Exit by mistake
    Nothing was changed. Start the editor again and it asks again.

Help > Help Topics (or Help on a right-click menu) doesn't open
    The original help file uses Windows Help (.hlp), which Windows 10 and 11 can no
    longer show. This doesn't affect the rest of the editor.


Notices
-------

MechCommander Gold (c) 1999 Microsoft Corporation. All rights reserved. BattleTech
material (c) 1998-1999 FASA Corporation.

MechCommander Redux and Redux HD are unofficial, non-commercial fan projects. They are
not affiliated with, endorsed by, or sponsored by Microsoft, FASA Interactive,
MicroProse, Catalyst Game Labs, or any other holder of the MechCommander, BattleTech or
MechWarrior trademarks and copyrights. MechCommander, BattleTech, MechWarrior and all
related names, logos and artwork are trademarks or registered trademarks of their
respective owners.

Redux HD's download includes MechCommander Gold's files free of charge, with permission.

The HD pack is free and for non-commercial use only: it may not be sold (the licence of
NVIDIA's model that sharpened the sound effects requires this). The licences of the
program's open-source parts are in "MechCommander Redux licences.txt", in this folder.
