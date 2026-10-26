# dav1d 1.5.4

VideoLAN's AV1 decoder (BSD 2-clause, `COPYING`), which decodes the HD art pack's movies (`sprite/MCHdMovie`). From
`https://downloads.videolan.org/pub/videolan/dav1d/1.5.4/dav1d-1.5.4.tar.xz` (1,038,852 bytes, SHA-256
`686616b7c69eb88d44459391ab25cac13b6647a3b288835c5784e71c1514a5c5`, as VideoLAN publishes it).

Only what the C build for 8 bits a sample needs is kept, unchanged: `include/dav1d` and `include/common`, the MSVC
atomics shim (`include/compat/msvc`), the library's C files and headers from `src` (the `*_tmpl.c` files are compiled
with `BITDEPTH=8`), `src/win32/thread.c`, and the two CPU headers `src/cpu.h` names (`src/arm`, `src/x86`). The
assembly is left out: the C code decodes the largest movie (2560x1392) many times faster than it plays. `config.h`
and `vcs_version.h`, which meson generates, are written by hand for clang on macOS and MSVC on Windows.

Builds list the files in `src/lib/MCCore/MCCore.vcxproj` (their own ItemGroup); `src/mac/CMakeLists.txt` reads them
from there.
