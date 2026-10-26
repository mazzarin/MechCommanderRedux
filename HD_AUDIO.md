# Optional HD audio

The audio pack lives in `HD/audio` beside the HD art. The game uses the user folder's `HD`, or the game folder's
`HD` if the user folder has none, exactly as HD art does. It plays when the player turns it on: the preferences
screen's HD AUDIO (PREFS `HDAudio`: 0 OFF, 1 ON, 2 ORCHESTRAL), apart from HD ART. Off, no pack, an unreadable
configuration, or an unsupported format leaves original playback intact.

`HD/audio/audio.cfg` marks the pack:

```
FITini
[HDAudioPack]
l Format=1
FITend
```

ON plays the pack's stereo music; ORCHESTRAL, offered only when the pack has `audio/music/orchestral`, its
orchestral music where it has some (the stereo elsewhere). The current pack has none. A
missing, corrupt, or wrong-duration entry uses original audio. A change takes effect from the next sound and music
track. Art's `pack.cfg` is unchanged.

Layout:

```
audio/audio.cfg
audio/music/stereo/MUSIC02.flac
audio/music/orchestral/MUSIC02.flac
audio/samples/0123456789abcdef.flac
```

Music is 48 kHz stereo PCM16 in FLAC. Samples may be mono/stereo at any supported rate. A sample's name is the
16-digit lowercase FNV-1a-64 of the original PCM format (rate uint32, channels uint16, bits uint16, all little
endian), followed by the original PCM data chunk. RIFF metadata and trailing allocation bytes do not affect it.
This covers loose WAVs and every voice/effect read from a PAK without depending on packet numbering. Original
game data remains required.

`dr_flac` (public domain or MIT-0, vendored in `src/lib/dr_flac`) is the sole compressed-audio decoder. Music is
checked cheaply when a track starts (its length, and that its last frame decodes) and then decoded into the existing
four-second ring as it plays, retaining original duration and scheduling. Samples are decoded once and kept (64 MiB of
them, the least recently used going first; a wave the pack has nothing for is remembered too), so a sound played
again costs a hash of its PCM and a lookup (32 MiB maximum each). Replacements must have exactly
`round(originalFrames * replacementRate / originalRate)` frames. Limits reject oversized or invalid metadata.
Volume, pan, fades, loops and the mixer frequency controls use the existing paths. Movies remain unchanged.

Verification, with the game's `mc_tests` harness:

```
mc_tests "HD audio" "hd audio on"
mc_tests --game "/path/to/MechCommander Gold" --hd-audio-pack "/path/to/HD" --only "tool: --hd-audio-pack verifies every generated music, effect and voice against the game's reader"
```

The first command exercises synthetic decoding, streaming, seek, EOF padding, short loops, canonical lookup,
music selection, controls and fallback. The second checks all supplied original assets against a generated pack
using the engine's own PAK reader. Generation tools, measurements, sample-library/model notices and listening
comparisons are maintained separately in the workspace's `tools/audio` and `audio/listen` folders.
Pass `--hd-audio-samples-only 1` to that tool while the sample pack is ready and music is still being generated.
