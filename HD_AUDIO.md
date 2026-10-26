# Optional HD audio

The audio pack lives in `HD/audio` beside the HD art. The game uses the user folder's `HD`, or the game folder's
`HD` if the user folder has none, exactly as HD art does. Audio works independently of the HDArt preference.
No pack, an unreadable configuration, or an unsupported format leaves original playback intact.

`HD/audio/audio.cfg`:

```
FITini
[HDAudioPack]
l Format=1
st MusicSet="stereo"
FITend
```

`MusicSet` is `stereo` (default), `orchestral` (alternate automatic transcription renders), or `original` (original
music with enhanced effects/voices). A missing orchestral track uses its stereo entry; a missing, corrupt, or
wrong-duration entry uses original audio. Changing the file affects resources loaded afterwards; restart to
change a currently playing track. Unknown MusicSet values select original music. Removing/renaming `audio.cfg`
disables all substitutions. Art's `pack.cfg` is unchanged.

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
validated in bounded scratch memory then decoded into the existing four-second ring, retaining original duration
and scheduling. Static samples are decoded before substitution (32 MiB maximum). Replacements must have exactly
`round(originalFrames * replacementRate / originalRate)` frames. Limits reject oversized or invalid metadata.
Volume, pan, fades, loops and the mixer frequency controls use the existing paths. Movies remain unchanged.

Verification, with the game's `mc_tests` harness:

```
mc_tests "HD audio:"
mc_tests --game "/path/to/MechCommander Gold" --hd-audio-pack "/path/to/HD" --only "tool: --hd-audio-pack verifies every generated music, effect and voice against the game's reader"
```

The first command exercises synthetic decoding, streaming, seek, EOF padding, short loops, canonical lookup,
music selection, controls and fallback. The second checks all supplied original assets against a generated pack
using the engine's own PAK reader. Generation tools, measurements, sample-library/model notices and listening
comparisons are maintained separately in the workspace's `tools/audio` and `audio/listen` folders.
