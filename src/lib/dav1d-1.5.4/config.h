/*
 * dav1d's build configuration for MechCommander Redux, in place of the config.h meson writes: 8 bits a sample only,
 * for clang on macOS and MSVC on Windows (see README.md). The C code alone, but for the arm64 assembly the Mac build
 * assembles (it defines MC_DAV1D_ASM). Only the switches the vendored files read are set.
 */
#pragma once

#define CONFIG_8BPC 1
#define CONFIG_16BPC 0
#define CONFIG_LOG 1
#if defined(MC_DAV1D_ASM) && defined(__aarch64__)
#define HAVE_ASM 1
#else
#define HAVE_ASM 0
#endif
#define TRIM_DSP_FUNCTIONS 0
#define ENDIANNESS_BIG 0
#define HAVE_C11_GENERIC 1

#if defined(__aarch64__) || defined(_M_ARM64)
#define ARCH_AARCH64 1
#else
#define ARCH_AARCH64 0
#endif
#define ARCH_ARM 0

// The arm64 assembly's switches, as meson finds them for Apple clang 17: no .func; .arch up to armv8.6-a (with +crc,
// which older clang needed); every .arch_extension and its instructions. Which extensions run is the CPU's (cpu.c).
#define HAVE_AS_FUNC 0
#define HAVE_AS_ARCH_DIRECTIVE 1
#define AS_ARCH_LEVEL armv8.6-a+crc
#define HAVE_AS_ARCHEXT_DOTPROD_DIRECTIVE 1
#define HAVE_AS_ARCHEXT_I8MM_DIRECTIVE 1
#define HAVE_AS_ARCHEXT_SVE_DIRECTIVE 1
#define HAVE_AS_ARCHEXT_SVE2_DIRECTIVE 1
#define HAVE_DOTPROD 1
#define HAVE_I8MM 1
#define HAVE_SVE 1
#define HAVE_SVE2 1
#if defined(__APPLE__)
// Mach-O symbols start with an underscore.
#define PREFIX 1
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_X86 1
#define ARCH_X86_64 1
#else
#define ARCH_X86 0
#define ARCH_X86_64 0
#endif
#define ARCH_X86_32 0

#define ARCH_LOONGARCH 0
#define ARCH_LOONGARCH32 0
#define ARCH_LOONGARCH64 0
#define ARCH_PPC64LE 0
#define ARCH_RISCV 0
#define ARCH_RV32 0
#define ARCH_RV64 0

#define HAVE_GETAUXVAL 0
#define HAVE_ELF_AUX_INFO 0
#define HAVE_DLSYM 0
#define HAVE_MEMALIGN 0
#define HAVE_PTHREAD_NP_H 0
#define HAVE_PTHREAD_GETAFFINITY_NP 0
#define HAVE_PTHREAD_SETAFFINITY_NP 0
#define HAVE_PTHREAD_SET_NAME_NP 0

#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#define UNICODE 1
#define _UNICODE 1
#define _CRT_DECLARE_NONSTDC_NAMES 1
#define HAVE_UNISTD_H 0
#define HAVE_POSIX_MEMALIGN 0
#define HAVE_ALIGNED_ALLOC 0
#define HAVE_PTHREAD_SETNAME_NP 0
#else
#define HAVE_UNISTD_H 1
#define HAVE_POSIX_MEMALIGN 1
#define HAVE_ALIGNED_ALLOC 1
#define HAVE_PTHREAD_SETNAME_NP 1
#endif
