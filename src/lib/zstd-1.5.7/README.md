# Zstandard 1.5.7

Meta's Zstandard (BSD, `LICENSE`; the project also offers GPLv2, which isn't taken). Its decompressor unpacks the
version 2 HD art pack's streams (the workspace's `SPEC-pack-v2.md`: zstd at level 19, one stream a packet). From
`https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz` (2,434,947 bytes, SHA-256
`eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`, as its `.sha256` says). Every file checked against
the v1.5.7 tag's tree (commit f8745da6): all of `lib` the same byte for byte.

Only the decompressor is kept, unchanged: `lib/zstd.h`, `lib/zstd_errors.h`, `lib/common` without `pool` and
`threading` (the compressor's threads), and `lib/decompress` without `huf_decompress_amd64.S`. Built with
`ZSTD_DISABLE_ASM`, so that assembly isn't needed. `ZSTD_LEGACY_SUPPORT` is left at its default, 0: no format before
zstd 0.8. Its xxHash is its own, under the `ZSTD_` prefix, apart from `src/lib/xxHash-0.8.4`.

Packed over a packet at level 19, the pack stores 90.5% of today's DEFLATE bytes and unpacks 1.2 to 2.2 times as fast
as libdeflate on an M4 Pro core (`tools/scratch-archive/4ec4f832/pack-v2-codec/README.md` in the workspace).

Builds list its ten C files in `src/lib/MCCore/MCCore.vcxproj` (their own ItemGroup); `src/mac/CMakeLists.txt` reads
the list from there.
