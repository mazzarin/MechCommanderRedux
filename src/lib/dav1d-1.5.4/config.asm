; dav1d's assembly configuration for MechCommander Redux, in place of the config.asm meson writes (see README.md):
; Windows x64, NASM -f win64, as meson configures it for x86_64 with MSVC. The Windows build assembles the x86-64 code
; with it where NASM is on PATH (MCCore.vcxproj's MCDav1dX86Asm). Which instruction sets run is the CPU's (x86/cpu.c).

%define private_prefix dav1d
%define ARCH_X86_64 1
%define ARCH_X86_32 0
%define PIC 1
%define STACK_ALIGNMENT 16
; MSVC builds without /arch:AVX (no __AVX__), so the SSE code keeps its own encoding.
%define FORCE_VEX_ENCODING 0
; No PREFIX: Windows x64's symbols have no leading underscore (meson sets it for Apple's systems and 32-bit Windows).
