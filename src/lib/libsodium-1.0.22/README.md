# libsodium 1.0.22

From https://download.libsodium.org/libsodium/releases/libsodium-1.0.22.tar.gz (ISC, `LICENSE`; 2,008,529 bytes,
SHA-256 `adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349`, its minisign signature checked against
the project's key `RWQf6LRCGA9i53mlYecO4IzT51TGPpvWucNSCh1CBM0QTaLn73Y7GFO3`): `src/libsodium`'s C files and headers,
unchanged, and `src/libsodium/include/sodium/version.h`, the release's `builds/msvc/version.h` (configure makes it
from `version.h.in`; libsodium's own Visual Studio build copies it there). Its assembly (`.S`, for x86-64 only), build
scripts and tests aren't taken.

The game checks updates' signatures (Ed25519) and files (SHA-256) with it (`update/MCSignedFile`, `lib/MCSha256`).
Both builds compile every C file into a library of its own, with libsodium's own settings: on Windows those of its
`builds/msvc` projects (`inline=__inline;NATIVE_LITTLE_ENDIAN;_CRT_SECURE_NO_WARNINGS;SODIUM_STATIC`, MCCore.vcxproj
lists the files), on the Mac its portable C (`CONFIGURED=1 NATIVE_LITTLE_ENDIAN=1`, `src/mac/CMakeLists.txt`). Code
that includes its headers defines `SODIUM_STATIC` first.
