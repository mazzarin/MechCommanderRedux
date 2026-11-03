# libdeflate 1.26

Eric Biggers' DEFLATE library (MIT, `COPYING`), whose decompressor unpacks the HD art pack's archives' frames
(`lib/MCInflate`). From `https://github.com/ebiggers/libdeflate/releases/download/v1.26/libdeflate-1.26.tar.gz`
(187,565 bytes, SHA-256 `125856d4656e0feab660f94842f835923410c9281fedbcee64598c918da42b5a`).

Only the decompressor is kept, unchanged: `libdeflate.h`, `common_defs.h`, and from `lib` `deflate_decompress.c`,
`utils.c` (its allocator) and the headers they include, with `x86/cpu_features.c` and the x86 headers, which pick its
BMI2 decoder on a processor that has one. Built as a static library, it needs no settings (`LIBDEFLATE_DLL` is for its
DLL).

It unpacks the pack's frames 1.7 to 2.4 times as fast as miniz's inflate, which it replaced (2 Nov 2026: a mech's frame
in 25 us, not 43, on an M4 Pro; a large effect's in 131 us, not 311), the bulk of reading a frame the first time it is
drawn. miniz stays for the editor's cabinets.

Builds list its three C files in `src/lib/MCCore/MCCore.vcxproj` (their own ItemGroup) and `src/mac/CMakeLists.txt`.
