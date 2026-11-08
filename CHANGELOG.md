# Redux HD's Releases

What each version of MechCommander Redux HD brought, newest first. [CHANGES.md](CHANGES.md) lists everything changed
from the original game, and the [README](README.md#what-redux-hd-adds) what Redux HD adds.

<!--
Each version's entry is that release's notes: the game's UPDATE page shows it (in capitals, in the game's own font),
and so do the Mac's update and the GitHub release. So: plain text, "- " for a list, no links or other Markdown, and
only the characters on a keyboard. The release script takes the entry under the heading of the release's version
(version.json) and refuses a release without one.
-->

## 0.6.0

Nights that change the battle.

- Fixed: a right-click on a mech or vehicle in your inventory crashed the game.
- At night headlights change sight: units see half as far, and a lit unit sees and is seen further. LIGHTS (N) turns
  the selected units' headlights off or on. The night shows without HD LIGHTING too.
- MATCH OPTIONS: MOST SKILLED PILOT, and a ban on each kind of weapon.
- Smoother: mechs' legs between frames at any frame rate, guests' mechs walking as the host's do, a falling tree that
  no longer vanishes, sensor blips at the original's pace.
- Faster: the HD movies, art, weather and sound do more of their work away from the game.
- Multiplayer cleanup: match continues after host leaves, reallocation of mechs after a player leaves, ability to
  rematch, other bug fixes and enhancements.

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
