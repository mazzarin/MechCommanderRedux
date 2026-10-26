/*
 * dav1d's build configuration for MechCommander Redux, in place of the config.h meson writes: the C code alone (no
 * assembly), 8 bits a sample only, for clang on macOS and MSVC on Windows (see README.md). Only the switches the
 * vendored files read are set.
 */
#pragma once

#define CONFIG_8BPC 1
#define CONFIG_16BPC 0
#define CONFIG_LOG 1
#define HAVE_ASM 0
#define TRIM_DSP_FUNCTIONS 0
#define ENDIANNESS_BIG 0
#define HAVE_C11_GENERIC 1

#if defined(__aarch64__) || defined(_M_ARM64)
#define ARCH_AARCH64 1
#else
#define ARCH_AARCH64 0
#endif
#define ARCH_ARM 0

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
