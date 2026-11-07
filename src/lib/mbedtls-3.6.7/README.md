# Mbed TLS 3.6.7

From https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2 (Apache 2.0 or GPL 2.0
or later, `LICENSE`; the game takes it under Apache 2.0; 5,473,689 bytes, SHA-256
`a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`, the release's own `mbedtls-3.6.7-sha256sum.txt`):
the long-term branch (libdatachannel takes Mbed TLS 3). `include/` and `library/`'s C files and headers, unchanged;
its build scripts, programs, tests, docs and `3rdparty/` (Everest and p256-m, off in its configuration) aren't taken.

The encryption of libdatachannel's links (DTLS) and WebSockets (TLS). Both builds compile every C file its
`library/CMakeLists.txt` builds into a library of its own (not `ecp_curves_new.c`, which it doesn't). Its configuration
is its default with the game's additions, `mc/mc_mbedtls_config.h` (ours), which every unit that includes its headers
reads (`MBEDTLS_USER_CONFIG_FILE=<mc_mbedtls_config.h>`: MechCommander.props on Windows, its library's public settings
in `src/mac/CMakeLists.txt`):
- `MBEDTLS_THREADING_C` with `MBEDTLS_THREADING_ALT`: libdatachannel calls it from its own threads, so it locks its
  shared state with the game's mutexes (`mc/threading_alt.h`, ours; MCCore's `platform/MCTlsThreading`).
- `MBEDTLS_SSL_DTLS_SRTP`: libdatachannel's DTLS offers its profiles whatever it carries.
