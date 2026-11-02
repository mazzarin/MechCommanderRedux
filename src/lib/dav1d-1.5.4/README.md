# dav1d 1.5.4

VideoLAN's AV1 decoder (BSD 2-clause, `COPYING`), which decodes the HD art pack's movies (`sprite/MCHdMovie`). From
`https://downloads.videolan.org/pub/videolan/dav1d/1.5.4/dav1d-1.5.4.tar.xz` (1,038,852 bytes, SHA-256
`686616b7c69eb88d44459391ab25cac13b6647a3b288835c5784e71c1514a5c5`, as VideoLAN publishes it).

Only what the build for 8 bits a sample needs is kept, unchanged: `include/dav1d` and `include/common`, the MSVC
atomics shim (`include/compat/msvc`), the library's C files and headers from `src` (the `*_tmpl.c` files are compiled
with `BITDEPTH=8`), `src/win32/thread.c`, the two CPU headers `src/cpu.h` names (`src/arm`, `src/x86`), and the arm64
assembly meson builds for 8 bits: `src/arm/64` (its 8-bit files and the ones they include), with `src/arm/asm.S`,
`asm-offsets.h`, `cpu.c` and the C headers that hook it in. `config.h` and `vcs_version.h`, which meson generates, are
written by hand for clang on macOS and MSVC on Windows.

The Mac build assembles the arm64 code (`MC_DAV1D_ASM` turns it on in `config.h`): it decodes the largest movie
(2560x1392) about three times as fast as the C code alone, 6.5 ms a frame on average on an M4 Pro against 20. Windows
builds the C code alone; its x86 assembly would need NASM in the build. One frame in eight of the movies takes several
times as long as the average, which `MCHdMovie` takes up by decoding frames ahead.

Builds list the C files in `src/lib/MCCore/MCCore.vcxproj` (their own ItemGroup); `src/mac/CMakeLists.txt` reads them
from there and lists the arm64 files itself.
