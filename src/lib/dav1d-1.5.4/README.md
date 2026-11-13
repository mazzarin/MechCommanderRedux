# dav1d 1.5.4

VideoLAN's AV1 decoder (BSD 2-clause, `COPYING`), which decodes the HD art pack's movies (`sprite/MCHdMovie`). From
`https://downloads.videolan.org/pub/videolan/dav1d/1.5.4/dav1d-1.5.4.tar.xz` (1,038,852 bytes, SHA-256
`686616b7c69eb88d44459391ab25cac13b6647a3b288835c5784e71c1514a5c5`, as VideoLAN publishes it).

Only what the build for 8 bits a sample needs is kept, unchanged: `include/dav1d` and `include/common`, the MSVC
atomics shim (`include/compat/msvc`), the library's C files and headers from `src` (the `*_tmpl.c` files are compiled
with `BITDEPTH=8`), `src/win32/thread.c`, the two CPU headers `src/cpu.h` names (`src/arm`, `src/x86`), the arm64
assembly meson builds for 8 bits: `src/arm/64` (its 8-bit files and the ones they include), with `src/arm/asm.S`,
`asm-offsets.h`, `cpu.c` and the C headers that hook it in, and the x86 assembly meson builds for 8 bits: the shared
files of `src/x86` (`cpuid`, `msac`, `pal`, `refmvs`, `cdef_avx2`, `cdef_sse` and the three `itx`) and its 8-bit ones
(`cdef_avx512` and the AVX-512, AVX2 and SSE `filmgrain`, `ipred`, `loopfilter`, `looprestoration` and `mc`), with
`filmgrain_common.asm` that they include, `src/ext/x86/x86inc.asm` (x264's, under its own ISC licence, which
`src/lib/licences.json` gives for Windows), `src/x86/cpu.c` and the C headers that hook it in. `config.h`,
`config.asm` and `vcs_version.h`, which meson generates, are written by hand: `config.h` for clang on macOS and MSVC on
Windows, `config.asm` for NASM on Windows x64 (meson's switches for it: `private_prefix` dav1d, `ARCH_X86_64` 1,
`ARCH_X86_32` 0, `PIC` 1, `STACK_ALIGNMENT` 16, `FORCE_VEX_ENCODING` 0, no `PREFIX`).

The Mac build assembles the arm64 code (`MC_DAV1D_ASM` turns it on in `config.h`): with the game's 4 threads and the
pack's movies made for fast decoding, it decodes the largest movie (2560x1392) at 1.7 ms a frame on average on an M4
Pro, against 4.2 with the C code alone. The Windows build assembles the x86-64 code with NASM 2.16.03, the version
dav1d's 1.5 series was tested with, where NASM is on PATH (the Windows workflow fetches it, held to its SHA-256):
`MCCore.vcxproj`'s `MCDav1dX86Asm` runs NASM on each file as meson does (`-f win64`, `src` and this folder searched for
includes) and builds the C files with `MC_DAV1D_X86_ASM`, which turns it on in `config.h`. Without NASM it builds the C
code alone. Which instruction sets run is the CPU's, at run time (`src/x86/cpu.c`). One frame in eight of the movies
takes several times as long as the average, which `MCHdMovie` takes up by decoding frames ahead.

Builds list the C files in `src/lib/MCCore/MCCore.vcxproj` (their own ItemGroup) and the x86 assembly beside them;
`src/mac/CMakeLists.txt` reads the C files from there, but for `src/x86/cpu.c`, and lists the arm64 files itself.
