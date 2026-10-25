# Changes

These changes were made to the original game.

MechCommander Redux plays like MechCommander Gold: the game logic is rebuilt function by function, and unit stats,
damage, AI and mission scripts behave as they did in 1999. What changed is the layer underneath (graphics, sound,
input, networking and files), plus fixes for bugs in the original that could crash the game or corrupt its memory.
Bugs that have an impact on how the game *plays* may remain in the game, and are listed under [Known Issues](#known-issues).

## Table of Contents

- [Changes](#changes)
  - [Table of Contents](#table-of-contents)
  - [Changes](#changes-1)
    - [Platform](#platform)
    - [Display](#display)
    - [Mouse](#mouse)
    - [Keyboard](#keyboard)
    - [Sound](#sound)
    - [Multiplayer](#multiplayer)
    - [Files and Saves](#files-and-saves)
    - [Mission Editor](#mission-editor)
  - [Bug Fixes](#bug-fixes)
    - [Crashes and Hangs](#crashes-and-hangs)
    - [Combat](#combat)
    - [Logistics and Menus](#logistics-and-menus)
    - [Mission Scripts](#mission-scripts)
    - [Multiplayer Fixes](#multiplayer-fixes)
    - [Graphics and Text](#graphics-and-text)
    - [Mission Editor Fixes](#mission-editor-fixes)
  - [Known Issues](#known-issues)
    - [Turrets](#turrets)
    - [Units and Combat](#units-and-combat)
    - [AI and Pathfinding](#ai-and-pathfinding)
    - [Logistics and Menus](#logistics-and-menus-1)
    - [Interface](#interface)
    - [Sound Issues](#sound-issues)
    - [Multiplayer Issues](#multiplayer-issues)
    - [Mission Scripts (Modding)](#mission-scripts-modding)
    - [Mission Editor Issues](#mission-editor-issues)

---

## Changes

### Platform

- Rebuilt as a native 64-bit program. DirectDraw, DirectSound, DirectPlay and the Smacker DLL are replaced by SDL3 and
  a built-in Smacker video decoder.
- Windows only for now; Linux is planned.

### Display

- Missions render at your window's resolution (640x480 minimum) and follow the window live when it is resized. The
  `Resolution` setting in `PREFS.CFG` is no longer used, and the fan resolution patches aren't needed.
- The terrain is built far enough to cover large screens. The original's terrain was sized for 640x480, so at higher
  resolutions the edges of the view were left unbuilt and the tactical map's view box jumped around.
- Zoom is smooth and continuous instead of two fixed levels, and always uses the full-size art (the original swapped to
  half-size sprites when zoomed out). The battlefield shows from 480 lines of the world (closest) to 2160 (furthest),
  whatever the window's size; the GPU scales it to the window. Health bars, selection marks, strike timers and the
  pause and question overlays stay the same size at every zoom, and still blend with the ground under them as before.
  The zoom keys step the zoom, the zoom button switches between the furthest zoom and one world pixel per screen
  pixel, and scrolling keeps the same speed on screen at any zoom. The `Use90Pixel` and `Force45Pixel` settings in
  `PREFS.CFG` are no longer used.
- The menu and logistics screens are still 640x480, and are scaled up to fit the window.
- The game still draws in 256 colours as the original did; the GPU (Vulkan where available) scales the picture to the
  window.
- Palette changes appear on the next frame drawn instead of instantly.

### Mouse

- The mouse cursor is the system cursor, so it moves smoothly even when the game's frame rate drops.
- An item dragged in the logistics inventory moves with the cursor.
- The mouse wheel works; the original didn't use it. Over the battlefield, wheel up zooms in and wheel down zooms out,
  a little per notch. Over the tactical map it zooms the map, as its zoom buttons do. Over any list or text with a
  scroll bar (mechs, pilots, vehicles, inventory, a mech's weapon list in the repair bay and briefing, saved games, the
  tactical map's info and salvage pages, the mission results, the multiplayer game list) it scrolls a row or line per
  notch, as the scroll arrows do. A list inside another list scrolls first.

### Keyboard

- The mission keys can be changed: the preferences screen has a CONTROLS button listing every command with its key and
  an alternate key. Click a key and press the new one, with Ctrl, Alt or Shift if you like; a key another command had
  moves, and the screen says which command lost it. Right-click clears a key, and DEFAULTS brings back the original
  keys. Handy on keyboards without a numeric keypad, Home or Page Up and Down. Keys that differ from the original are
  kept in `PREFS.CFG`'s `Controls` block.

### Sound

- Without a sound device, the game runs silently instead of quitting with "DirectSound was unable to initialize".

### Multiplayer

- DirectPlay is replaced by TCP/UDP networking. One player hosts and the others connect to them.
- LAN games are found automatically. Internet games connect by `host:port`; the default port is 28800.
- Modem, serial cable, lobby (MPlayer, Zone) and host migration aren't supported.
- Zoom works in multiplayer. The original locked multiplayer games to the zoomed-out view and greyed out the zoom
  button. Zoom only changes what is drawn, so players zooming independently doesn't affect the game itself.

### Files and Saves

- The game reads its data from your MechCommander Gold folder and never writes to it. See the
  [README](README.md#running-the-game) for how the folder is found.
- Saves, preferences and temporary files go to `%APPDATA%\MechCommander Redux\`, or to the folder given with `--user`.
  Saves left in the MechCommander Gold folder still show up, and deleting or overwriting one in the game removes it
  from there.
- Data files saved with Unix line endings load correctly, so edited files work.
- A file name the game gets from a save, a mission or another player can't climb out of these folders with `..` or
  switch drives.

### Mission Editor

- The Mission Editor from the MechCommander Gold CD is rebuilt as a native 64-bit program (`MCEditor.exe`) on current
  MFC, in one file. It needs no MFC 4.2 DLLs, no Microsoft Jet or DAO database engine and no Visual Basic runtime, and
  registers nothing with Windows. The original registered itself as an OLE automation server on every start, though
  that server had nothing to call; the rebuild doesn't.
- The mission script writer (the original's Visual Basic `FitAbl.dll`) is rebuilt into the editor and writes the same
  `.FIT` and `.ABL` files.
- The mission database is SQLite. The first time the editor opens it, it converts `Teditor\FitAbl.mdb` into
  `FitAbl.db` next to it and uses that from then on; the `.mdb` is left untouched.
- On its first run, the editor copies its own files (the `Teditor` folder and its six sounds) from the CD, a mounted
  CD image or an old editor folder. Nothing on the CD is run, and nothing is written to the registry.
- The editor finds the game in its own folder, or in the folder given with `--data`.
- A crash writes a stack trace and `MCEditor.dmp` beside the exe instead of showing a dialog.

---

## Bug Fixes

### Crashes and Hangs

- Clicking, moving or jumping toward a point past the edge of the map, or a missed shot landing off the map, no longer
  reads and writes memory outside the map.
- A missing target, owner, pilot or appearance no longer crashes combat, the interface, the briefing or the mission
  results.
- An artillery strike, explosion, fire or train meeting an object without a type no longer hangs the game.
- A train derailing and splitting in two no longer crashes, and neither does a map with 64 trains.
- A team tracking more than 200 contacts no longer overruns memory.
- A route longer than the pathfinder allows for no longer overruns memory.
- More than 100 timers running in the menus no longer overruns memory.
- Damaged or hand-made game files (missions, maps, tile sets, pictures, fonts, sounds, saves, sprite and archive
  files) are checked as they load. One the game can do without is skipped, and a save it can't read stays in the list
  without its details. A mission it can't play ends with a message saying which file is wrong and why. The original
  read and wrote past such files' data.
- A mission script asking for more entries than its array holds, or recursing without end, no longer overruns memory
  or the stack; the script stops with an error.

### Combat

- Rear torso hits on mechs, vehicles, elementals and turrets no longer read damage data from outside the unit.
- Units with a top speed over 999 no longer read their speed from outside the speed table.
- Neutral turrets no longer use a map column as their visibility flags.
- A turret no longer overruns its list of 8 contacts.
- Mechs whose sprites hold fewer hot spots than they have weapons no longer read past the hot spot list.
- Shots now fly to the spot they hit. In the original, bullets, laser beams and Gauss rifle (railgun) bolts flew to a
  point on the target picked by the firing weapon's mount, while the impact played where the shot hit, so hits often
  looked like misses. Damage is unchanged.

### Logistics and Menus

- Dragging the scroll thumb of a logistics text box no longer crashes.
- Stray text no longer follows the refit dialog's messages.
- Dropping a unit with the right mouse button when no drop slot is free no longer corrupts memory, and a unit that is
  too heavy for a slot no longer lands in a random one.
- Saving to the empty save slot no longer reads its freed name.
- Cancelling out of the network screens no longer fills the selected game with unrelated values.
- Loading a campaign that replaces a mech no longer leaves the repair screen pointing at the deleted mech.
- A vehicle crew name of 9 or more characters no longer overwrites the vehicle's speed, structure and armor.
- Empty inventories, and items with fewer copies than their count, no longer crash the inventory.
- The briefing no longer crashes on a mech without a pilot or engine, or on a missing movie.
- The mission results no longer crash without a best pilot, and no longer overflow when they find more pilots than they
  made room for.
- With two or more campaigns, the campaign name is no longer garbage.
- The purchase and repair tickers no longer show garbage text.
- The marks of a fourth or later drop zone no longer overwrite the drop zone data.
- Fixed several memory leaks in the logistics screens.
- Refitting a mech assigns its critical slots as intended. This changes how the game plays, because a component is
  lost with the location that holds it.
  - Large weapons are spread over the side torsos and arms. The original put nearly all of them in the left arm, so
    losing that arm took most of a mech's firepower with it (the bug fixed by the community "left arm" patch).
  - Each jump jet takes one slot, in the leg with fewer jets. The original miscounted the jets and put each one in
    every empty slot of a leg.
  - A weapon is never put in a full location. The original wrote it over the first slot of the next location.

### Mission Scripts

- A prison filled by a script in a mission without pilots is left empty instead of holding an invalid pilot.
- Pilot wounds past 6 no longer read past the wound table.
- An unterminated string, or a character above 127, no longer runs past the script compiler's buffers.
- The script debug log stops when it is full instead of overrunning memory.

### Multiplayer Fixes

- A sixth player no longer breaks the player lights in the network lobby.
- A message from an unknown player no longer crashes the game.
- Session and player names are always terminated, and unused message bytes are cleared instead of sent as garbage.
- Message timing no longer breaks every few minutes on modern PCs, whose timers count much faster than 1999's.
- Other players can no longer write files on your computer. The original's file transfer, which the game never used,
  is gone, and a mission name another player sends must name a file inside the game's folders.
- Only the host (and the game's server) can run the game: set up and start the session, change teams, resource
  points, tech bases or the mission, start or end the scenario, and send the battle's updates. The other players send
  only their own check-ins, orders, groups, artillery and units, and can't act for someone else.
- A message that names units, slots, targets, map cells or events the game doesn't have is dropped. In the original it
  could crash the game or overwrite its memory.
- Searching for a host by address lists only that host's game. Before, any machine that answered could show up in its
  place.
- A message too big for the receive buffer is dropped. In the original it stopped every later message from arriving.
- The host turns away floods of connections, drops connections that never join, and answers only so many LAN searches
  each second, so it can't be used to flood another machine.

### Graphics and Text

- Drawing shapes wider than the screen, empty images, lines that round outside the view, or damaged image data no
  longer hangs or overruns memory.
- The right edge of some sprites no longer draws one pixel onto the next row.
- Laser beams without a target position are no longer drawn from a random point.
- Long mission briefing text, help text and rank names no longer run past their buffers.
- Chat from an unknown player takes the first player's colour instead of crashing.
- A mech icon part for a location without armor no longer divides by zero when it picks its colour. It shows red, as
  in the original.

### Mission Editor Fixes

- Loading the fade tables from `misc.fst` no longer overruns memory on every start.
- Loading a mission no longer crashes on an objective that doesn't load, an objective with more than three script
  rows, a unit or structure link whose object isn't on the map, an order that names a missing structure, or a mission
  without a save file. A unit whose orders don't read keeps its default orders.
- Changing an objective's value no longer leaves it pointing at freed memory.
- Saving no longer crashes on a guard or destroy order without a structure, a kill or escort order without a unit, an
  objective whose structure or unit is gone, or a Move to Area objective whose flag was erased.
- Saving no longer quits the editor without a word when `Data\Savegame` is missing, or when a file for a distribution
  file can't be opened. It also no longer writes to a path map or tactical map file that didn't open.
- The multiplayer save no longer overruns memory with a long description.
- Reading a damaged Access database no longer reads past its pages, and a column name holding a bracket no longer
  breaks the import.
- Pressing a tool's key while dragging on the map no longer leaves the view using a deleted tool.
- Changing a control structure's side no longer crashes when one of its linked buildings was erased.
- Drawing or saving after the libraries failed to load no longer uses the freed map.
- The Select Mission dialog no longer crashes on a preview picture that isn't 256 colours.
- Mission names with accented characters no longer read outside the character tables.
- A damaged tile, shape, FIT entry or order in the data no longer crashes the editor or reads past its buffers.
- Objects near the map's edges no longer write outside the map, the tactical map picture or the mission marks
  picture, and objects near the top and left edges are no longer drawn or hidden at random.
- Long words in the editor's tables, `System.cfg` and the dispatch text, and a dispatch file shorter than 256 bytes,
  no longer overrun memory.
- The pathfinding maps no longer overrun memory on maps with more than 5000 doors or very long routes.
- Packing very small or very large packets into save and distribution files no longer overruns memory.
- A side with more than four objectives no longer overruns the objectives dialog.
- Thinning a forest no longer reads freed memory, and a dock's overlay no longer comes from leftover memory.
- Every template texture is looked up when the cell map is built. In the original, the first texture missing from
  `Template.txt` broke every later lookup, so every template texture after it blocked all its cells. The cell maps now
  match the ones shipped with the game, and building twice in one session gives the same result.
- Saved files no longer carry stray memory: a map's `.fit` ends at `FITend`, the campaign file, `.sol` and `.mpk` end
  with their text, and the unused parts of the `.gmm` path map are zeros.
- The drop zone and objective marks are drawn on the logistics map picture. On Windows NT and later the original drew
  garbage pixels instead.
- More than six objectives no longer draw garbage marks, and marks near the picture's edges are clipped.
- Fixed several memory, file and handle leaks.

---

## Known Issues

These bugs are in the original MechCommander Gold. Some of these bugs are kept on purpose due to either preservation of original gameplay or due to uncertainty of the effect the change will have. Some of these Known Issues will be fixed at a later time.

### Turrets

- **Turrets snap onto their target instead of turning toward it.** Every turret has a turn rate in its data (45 or 360
  degrees per second), but a mistake in the original code makes the turret face its target at once. Fixing the turn
  alone would look worse: turrets don't wait to face their target before firing, so a turning turret would shoot out of
  its side or back for up to 4 seconds. A proper fix would also need turrets to hold fire until they face the target,
  which makes them weaker than in the original game.
- Turrets never show their damage effect. Two turret types have one in their data, but the original drops it.
- Every turret's weapons are always switched on. Turrets have a flag that switches their weapons on and off, but we
  haven't yet found where the original sets it, so Redux turns it on for every turret. Turrets fire as they do in the
  original, but if the original could switch a turret's weapons off (from a mission script, for example), Redux can't.

### Units and Combat

- Idle elementals drift toward the top-left of the map instead of wandering at random.
- Elementals that can't jump are removed when the camera scrolls away from them. This may be intended.
- Playing as the Clans, a destroyed friendly elemental counts as an enemy kill for the voice-over.
- An elemental pivoting to reverse keeps its old throttle.
- Units can be filed under the wrong terrain block.
- A player order given during an alarm is treated as an alarm response.
- Laser beams are widened in the wrong direction and can look twisted or collapse, never show their hot core colour,
  and friendly projectile lasers show enemy colours except at the tip.
- Buildings with no team belong to the player's team.
- A light wall with no damage level in its data falls to any hit.
- Capturing a prison with a seated vehicle loses every prisoner but one.
- Train cars collide with every wall, bridge and forest in their terrain block, their sounds stutter, and lightly
  damaged cars derail more often than heavily damaged ones. A train car's health bar and `getunitstatus` grow as it
  takes damage.
- Arm sprites stay mirrored once they have faced left.
- Mech leg animations drift when the frame rate drops below the animation's.
- Removing a salvageable object twice drops another from the salvage list, and the 101st hides the 100th.
- Each closed pilot status window leaks a little memory.

### AI and Pathfinding

- Long-range pathfinding can expand doors out of order and choose a costlier route.
- Diagonal steps are blocked or allowed by the mines of an unrelated tile near the map's top-left corner.
- Cells reached a second time during a search look more expensive than they are.
- Paths go through Clan mechs standing on bridges, but not Inner Sphere ones.
- Withdraw and scatter points picked near an impassable spot can be inside the blocked cell.

### Logistics and Menus

- **Escape on a two-button dialog does the OK action, not Cancel.**
- **A damaged engine can be removed from a mech by an item repair.**
- A component picked up with the right mouse button can be lost when it can't be mounted.
- Moving two mechs, vehicles or pilots in a row between lists can corrupt both lists.
- Removing a unit from its drop slot, or loading a campaign, can remove the wrong pilot.
- A lance holding only vehicles can show 0 tons.
- Ticker text wider than the ticker never scrolls.
- Pilots of the same rank are listed in a scrambled order in the mission results.
- Clicking the mission results' scroll bar scrolls to about 15 pixels below the click.
- Backspace leaves the deleted character on screen until the next one is typed.
- An over-long centred line of briefing text shows its last character doubled.
- Dragging the internal structure or engine slider left only redraws its knob after the repair.
- The purchase screen's chat button stops blinking for new messages.
- Non-weapon equipment shows a range of "0.0 m".
- A multiplayer save always uses the default pilot costs.
- The Ctrl+Alt resource cheat on the purchase and repair screens never fires.
- Small memory leaks when logistics widgets are destroyed, the vehicle pane is redrawn, or a multiplayer session ends.

### Interface

- With all four lances selected, the fourth isn't given the order.
- The Ctrl+Alt+G cheat (gates stay closed) also destroys the player's targets.
- About every 7 minutes one frame runs as a long 0.25-second step.
- Dragging the resize handle of a title-less window resizes the screen instead.

### Sound Issues

- **Every sound effect plays centred: stereo panning has no effect.**
- Inserting a radio message ahead of 7 queued ones drops the lowest one even though there is room.

### Multiplayer Issues

- Clients see turrets' missed shots land near the centre of the map.
- Units warp on clients more often on high ground.
- Some move messages reach the clients as garbage.
- Terrain fires of 64 seconds or more go out early on the clients.
- A client's orders only sort the units into formation when group 3 is part of the order.
- Turrets aiming at buildings aim at a random unit, or at nothing, on the clients.

### Mission Scripts (Modding)

These only matter when writing or editing mission scripts (ABL).

- Every string comparison is true.
- Numbers with an exponent (`1.5e3`) are a syntax error.
- `stopmusic` can't be compiled.
- `playwavefile` runs its second argument as the next statement.
- `orderwithdraw` leaves an extra value on the script stack.
- `setcaptureable` on a turret overwrites its last fire time, and `wasevercapturable` is true for any turret that has
  fired.
- `getmaxarmor`, `getobjectdmgpts`, `setcurrentbrvalue` and the guard, scan and mode routines compile but fail with
  "Undefined ABL RoutineKey", and `getobjectmaxdmg` returns the damage taken.
- `addprisoner` puts the prisoner in every empty slot of the prison, not just the first.
- A camera block without `MinScrollSpeed` gets a `DistanceThreshold` of 90.
- `getrepairstate` returns -2147483648 for a part that isn't a unit, or a unit with every location destroyed, so it
  passes any "below n%" test.

### Mission Editor Issues

These are in the original Mission Editor.

- **Saving a mission with a prison stops with "Object required", and one with a structure that has salvage stops with
  "Type mismatch".** So does a Guard Structure objective whose third parameter is "Yes".
- Some objective actions and structures write `.ABL` files that don't compile: an action given a parameter it doesn't
  know, a captureable Power Control, and a lookout point that uses neither a radius nor ABL.
- Allied move objectives wait for the player's force or lance instead of the allied one.
- In multiplayer scripts a Perimeter Alarm raises the general alarm twice, and its name is used as typed, so spaces or
  punctuation in it break the script.
- A secondary objective's success repeats its status line in the `.ABL`.
- The saved map's highest and lowest points are swapped.
- Every multiplayer save says the map is on Garotte, Port Arthur maps too.
- Deleting a mission in the save dialog leaves it in the database, so it comes back in the list.
- A map saved without mission data opens from the recent files list as the mission of the same name, or not at all.
- Linking a structure that wasn't linked, taking rough off with the alternate button, and objects erased with an
  overlay tool's alternate use can't be undone.
- The right-click menu opens away from the cursor, by the view's distance from the screen's corner.
- Double-clicking beside a unit opens its settings only when the unit is in the column to the right.
- Ctrl+Shift+Tab turns the Map Features palette forward, not back.
- The lower tool's alternate use is never allowed.
- The goto, circle and patrol orders show the wrong description in the order dialog.
- Transition tiles never pick their first variant when they have others.
- Colours 233 to 245 ignore the gamma setting, and the brightest colours brighten to dark ones.
- The hourglass cursor stays after a failed save.
- Some error messages are empty: a mission file missing while writing a distribution file, or a path map file that
  doesn't open.
- **Help > Help Topics** doesn't open: the help file is Windows Help (`.hlp`), which Windows 10 and 11 can't show.
