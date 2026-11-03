# xxHash 0.8.4

Yann Collet's fast hash (BSD 2-clause, `LICENSE`), whose XXH3 identifies the HD audio pack's decoded sounds by their
original PCM as they play (`sound/MCHdAudio`): a wave played again is found by it at memory speed, where the canonical
FNV-1a key that names the pack's files is worked out once a wave, to find its file. From
`https://raw.githubusercontent.com/Cyan4973/xxHash/v0.8.4/xxhash.h` (280,387 bytes, SHA-256
`3dc8d161e867a62d3417f7885b55fdaded8b7b497e2f165d05c94f4fe24f2ca4`) and its `LICENSE`, tag v0.8.4.

Kept unchanged and used header-only: the one file that hashes defines `XXH_INLINE_ALL` before including it, so there is
nothing to build or list in the projects; both builds add this folder to MCCore's include path
(`src/MechCommander.props`, `src/mac/CMakeLists.txt`).
