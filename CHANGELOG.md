# Redux HD's Releases

What each version of MechCommander Redux HD brought, newest first. [CHANGES.md](CHANGES.md) lists everything changed
from the original game, and the [README](README.md#what-redux-hd-adds) what Redux HD adds.

<!--
Each version's entry is that release's notes: the game's UPDATE page shows it (in capitals, in the game's own font),
and so do the Mac's update and the GitHub release. So: plain text, "- " for a list, no links or other Markdown, and
only the characters on a keyboard. The release script takes the entry under the heading of the release's version
(version.json) and refuses a release without one.
-->

## 0.7.0

- HD art made again: mechs, vehicles, buildings and the ground are sharper and more detailed.
- HD art: the briefing maps are drawn again from each map's own data, and the minimaps, sensor contacts, move and
  attack markers, the globe and the words shown in battle are redrawn. The DATA page's armor diagrams, the loading
  screen's static and the menus' scroll bars are sharp too.
- HD voices: cleaner and louder, without the echo and hiss, and the radio and Betty are heard over the battle and the
  music.
- Smaller HD art for computers with less memory: 4.9 GB instead of 7.9, picked by itself under 12 GB of memory, so the
  game plays without waiting on the disk. The installer and HD OPTIONS' HD ART SIZE let you choose.
- This update downloads most of the HD art again: 7.4 GB, or 4.3 GB for the smaller set.
- Smoother: scrolling the map, with far fewer frames shown late, and buildings, trees and units appear as soon as they
  reach the edge of the screen instead of popping in.
- Fewer pauses: units, effects and screens are read ahead, so they show without a stall the first time.
- Fixed: selling a unit on another screen while it was deployed for the mission crashed the briefing.
- You can own up to 200 mechs and vehicles outside a match (was 100).
- A game started from a launcher plays your Redux HD, with its HD art and updates; the launcher's folder gives only
  its maps.
- Fixed: the game could crash on a match's results screen when the host left, or on Windows while it was minimised.
- Fixed: after a save that couldn't be loaded, the campaign's screens could crash the game. The load screen now says
  so, and a damaged save can no longer make the game hang.
- Fixed: without a sound device, the operation briefing no longer waits forever.
- Fixed: a second click on OPERATION BRIEFING restarts its movie instead of stopping it.
- Fixed: in multiplayer, a match whose mission won't load, or whose host leaves as it starts, takes you back to the
  games, where the game quit or waited forever.
- HD art: the menus drawn again, with sharper words and buttons, new metal and stone textures.
- HD: water at the edge of the fog of war no longer flickers; its waves move, dimmed.
- Faster: much faster at night and in the rain, with the drawing on a thread of its own. On Windows the HD movies take
  a third of the CPU, and the first start's black screen is shorter.
- The main menu shows the version you're running.
- Fixed: the game could crash as a Hollander or an Atlas fell over. The fall is now drawn from the mech's own art.
- Fixed: with HD art on, the game could crash as some mechs moved, such as a limping Thor or Hunchback.
- Fixed: a right-click on a mech or vehicle in your inventory crashed the game.
- Fixed: a game started from a launcher no longer crashes when you quit on the match's results screen.
- Fixed: in multiplayer, a joining game can no longer add extra players or send oversized messages, and a short chat
  message, or one from a player who just left, no longer crashes the game.
- Night mode is in testing, to see how the community likes it: at night units see half as far, and their headlights
  light the way but show them to the enemy. LIGHTS (N) turns the selected units' headlights off or on.
- MATCH OPTIONS: MOST SKILLED PILOT, and a ban on each kind of weapon.
- Smoother: mechs' legs between frames at any frame rate, guests' mechs walking as the host's do, a falling tree that
  no longer vanishes, sensor blips at the original's pace.
- Faster: the HD movies, art, weather and sound do more of their work away from the game.
- Windows: this update runs the setup once; after it, the game puts its own updates in place, even when a launcher
  started it.
- Multiplayer cleanup: match continues after host leaves, reallocation of mechs after a player leaves, ability to
  rematch, a match always plays the game's own data and never a mod's, other bug fixes and enhancements.

## 0.6.8

- Fixed: the game could crash on a match's results screen when the host left, or on Windows while it was minimised.
- Fixed: after a save that couldn't be loaded, the campaign's screens could crash the game. The load screen now says
  so, and a damaged save can no longer make the game hang.
- Fixed: without a sound device, the operation briefing no longer waits forever.
- Fixed: a second click on OPERATION BRIEFING restarts its movie instead of stopping it.
- Fixed: in multiplayer, a match whose mission won't load, or whose host leaves as it starts, takes you back to the
  games, where the game quit or waited forever.
- HD art: the menus drawn again, with sharper words and buttons, new metal and stone textures and clearer briefing
  maps.
- HD: water at the edge of the fog of war no longer flickers; its waves move, dimmed.
- Faster: much faster at night and in the rain, with the drawing on a thread of its own. On Windows the HD movies take
  a third of the CPU, and the first start's black screen is shorter.
- The main menu shows the version you're running.
- Fixed: the game could crash as a Hollander or an Atlas fell over. The fall is now drawn from the mech's own art.
- Fixed: with HD art on, the game could crash as some mechs moved, such as a limping Thor or Hunchback.
- Fixed: a right-click on a mech or vehicle in your inventory crashed the game.
- Fixed: a game started from a launcher no longer crashes when you quit on the match's results screen.
- Fixed: in multiplayer, a joining game can no longer add extra players or send oversized messages, and a short chat
  message, or one from a player who just left, no longer crashes the game.
- Night mode is in testing, to see how the community likes it: at night units see half as far, and their headlights
  light the way but show them to the enemy. LIGHTS (N) turns the selected units' headlights off or on.
- MATCH OPTIONS: MOST SKILLED PILOT, and a ban on each kind of weapon.
- Smoother: mechs' legs between frames at any frame rate, guests' mechs walking as the host's do, a falling tree that
  no longer vanishes, sensor blips at the original's pace.
- Faster: the HD movies, art, weather and sound do more of their work away from the game.
- Windows: from 0.6.1 or earlier, this update runs the setup once; after it, the game puts its own updates in place,
  even when a launcher started it.
- Multiplayer cleanup: match continues after host leaves, reallocation of mechs after a player leaves, ability to
  rematch, other bug fixes and enhancements.

## 0.6.3

- Fixed: the game could crash as a Hollander or an Atlas fell over. The fall is now drawn from the mech's own art.
- Fixed: with HD art on, the game could crash as some mechs moved, such as a limping Thor or Hunchback.
- Fixed: a right-click on a mech or vehicle in your inventory crashed the game.
- Fixed: a game started from a launcher no longer crashes when you quit on the match's results screen.
- Fixed: in multiplayer, a joining game can no longer add extra players or send oversized messages, and a short chat
  message, or one from a player who just left, no longer crashes the game.
- Night mode is in testing, to see how the community likes it: at night units see half as far, and their headlights
  light the way but show them to the enemy. LIGHTS (N) turns the selected units' headlights off or on.
- MATCH OPTIONS: MOST SKILLED PILOT, and a ban on each kind of weapon.
- Smoother: mechs' legs between frames at any frame rate, guests' mechs walking as the host's do, a falling tree that
  no longer vanishes, sensor blips at the original's pace.
- Faster: the HD movies, art, weather and sound do more of their work away from the game.
- Windows: this update still runs the setup once; after it, the game puts its own updates in place, even when a
  launcher started it.
- Multiplayer cleanup: match continues after host leaves, reallocation of mechs after a player leaves, ability to
  rematch, other bug fixes and enhancements.

## 0.6.2

- Fixed: the game could crash as a Hollander or an Atlas fell over. The fall is now drawn from the mech's own art.
- Fixed: with HD art on, the game could crash as some mechs moved, such as a limping Thor or Hunchback.
- Fixed: a right-click on a mech or vehicle in your inventory crashed the game.
- Fixed: a game started from a launcher no longer crashes when you quit on the match's results screen.
- Fixed: in multiplayer, a joining game can no longer add extra players or send oversized messages, and a short chat
  message, or one from a player who just left, no longer crashes the game.
- Night mode is in testing, to see how the community likes it: at night units see half as far, and their headlights
  light the way but show them to the enemy. LIGHTS (N) turns the selected units' headlights off or on.
- MATCH OPTIONS: MOST SKILLED PILOT, and a ban on each kind of weapon.
- Smoother: mechs' legs between frames at any frame rate, guests' mechs walking as the host's do, a falling tree that
  no longer vanishes, sensor blips at the original's pace.
- Faster: the HD movies, art, weather and sound do more of their work away from the game.
- Windows: this update still runs the setup once; after it, the game puts its own updates in place, even when a
  launcher started it.
- Multiplayer cleanup: match continues after host leaves, reallocation of mechs after a player leaves, ability to
  rematch, other bug fixes and enhancements.

## 0.6.1

- Fixed: with HD art on, the game could crash as some mechs moved, such as a limping Thor or Hunchback.
- Fixed: a right-click on a mech or vehicle in your inventory crashed the game.
- Night mode is in testing, to see how the community likes it: at night units see half as far, and their headlights
  light the way but show them to the enemy. LIGHTS (N) turns the selected units' headlights off or on.
- MATCH OPTIONS: MOST SKILLED PILOT, and a ban on each kind of weapon.
- Smoother: mechs' legs between frames at any frame rate, guests' mechs walking as the host's do, a falling tree that
  no longer vanishes, sensor blips at the original's pace.
- Faster: the HD movies, art, weather and sound do more of their work away from the game.
- Multiplayer cleanup: match continues after host leaves, reallocation of mechs after a player leaves, ability to
  rematch, other bug fixes and enhancements.

## 0.6.0

- Fixed: a right-click on a mech or vehicle in your inventory crashed the game.
- Night mode is in testing, to see how the community likes it: at night units see half as far, and their headlights
  light the way but show them to the enemy. LIGHTS (N) turns the selected units' headlights off or on.
- MATCH OPTIONS: MOST SKILLED PILOT, and a ban on each kind of weapon.
- Smoother: mechs' legs between frames at any frame rate, guests' mechs walking as the host's do, a falling tree that
  no longer vanishes, sensor blips at the original's pace.
- Faster: the HD movies, art, weather and sound do more of their work away from the game.
- Multiplayer cleanup: match continues after host leaves, reallocation of mechs after a player leaves, ability to
  rematch, other bug fixes and enhancements.

## 0.5.2

- Fixed: a right-click on a mech or vehicle in your inventory crashed the game.

## 0.5.1

- An overhaul of the code underneath, with bug fixes, courtesy of RevenantBob.
- The Windows setup, when you choose your own copy of MechCommander Gold, now says that Redux HD takes it over, and
  how to leave it as it is.

## 0.5.0

The first release of Redux HD, on Windows and the Mac, with MechCommander Gold included.

- HD art, sound and movies: the units, buildings, ground, effects and menus sharper, from the HD pack.
- Light: shots, fires and explosions light the battlefield; dusk and night, with headlights; the sun's relief,
  softer shadows, drifting clouds and forests swaying in the wind.
- Weather: rain, thunder and lightning, snow and blizzards, with puddles and settling snow.
- Effects: flecks, debris, smoke, splashes and screen shake; footprints and craters that stay.
- Mechs turn smoothly, and BRAKING brings them to a halt as the original's art draws it.
- MATCH OPTIONS for multiplayer: the weather and time of day everyone plays in, braking, and artillery, probes,
  sensors and mines on or off.
- The Mission Editor sets a mission's time of day and weather.
- UPDATE on the main menu updates the game and its files together, downloading only what changed; INTERFACE SIZE
  on the Preferences screen.
