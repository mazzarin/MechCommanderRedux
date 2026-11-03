# Sparkle 2.10.1

The Sparkle project's macOS updater (MIT, `LICENSE`), with which the Mac app of the personal build updates itself
(`apps/MCRedux/MCSparkleUpdater.mm`, behind `main/MCUpdater.h`): it reads the fork's feed, checks each update's EdDSA
signature against the app's `SUPublicEDKey`, downloads it, and swaps the new app in when the game quits. From
`https://github.com/sparkle-project/Sparkle/releases/download/2.10.1/Sparkle-2.10.1.tar.xz` (16,346,336 bytes, SHA-256
`1a856d2a9edb8e628de88121d7be04cf0e2167f8641650b78fca00b14244cf27`): its `Sparkle.framework` unchanged (signed ad hoc,
as released) and its `LICENSE`. The release's tools (`generate_keys`, `sign_update`, `generate_appcast`) stay out of
the tree, with the release script.

`src/mac/CMakeLists.txt` links the app against it and copies it into the app's `Contents/Frameworks` before the app is
signed. Windows doesn't use it.
