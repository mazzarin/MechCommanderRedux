# libjuice 1.7.4

From https://codeload.github.com/paullouisageneau/libjuice/tar.gz/refs/tags/v1.7.4 (MPL 2.0, `LICENSE`; 108,422 bytes,
SHA-256 `95c088862aa1b88b73d62aa99577135010eb768fcafb252d13aa85a8515ef798`; every file checked against the tag's tree;
the commit libdatachannel 0.24.6 pins, b89c792e): `include/` and `src/`, unchanged. Its tests, fuzzer and build
scripts aren't taken.

libdatachannel's ICE: it finds the UDP path between two games, direct through their routers or through a relay (TURN
over UDP only: libdatachannel refuses a TCP or TLS relay with it). Both builds compile every C file its CMake builds
into a library of its own, as libdatachannel builds it: its own hashes (`USE_NETTLE=0`), without its STUN and TURN
server (`NO_SERVER`), `JUICE_STATIC JUICE_EXPORTS RELEASE=1`.
