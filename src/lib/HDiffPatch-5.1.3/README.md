# HDiffPatch 5.1.3 (patcher only)

From https://github.com/sisong/HDiffPatch, tag v5.1.3 (MIT, `LICENSE`): its patcher, `libHDiffPatch/HPatch/patch.c`
and the headers it includes, unchanged. The game builds it single-threaded (`_IS_USED_MULTITHREAD=0`, so
`hpatch_mt/` is only its header) and applies the window diffs `hdiffz -WD -c-no` makes, checking their xxh128
checksums (lib/MCFilePatch). The rest of the project (the differ, its compressors, its tools) isn't taken: the release
script runs `hdiffz` itself.
