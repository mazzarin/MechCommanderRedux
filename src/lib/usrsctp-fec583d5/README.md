# usrsctp fec583d5

From https://codeload.github.com/paullouisageneau/usrsctp/tar.gz/fec583d54493f879d2ae44a743423bf8a04371ab (BSD
3-clause, `LICENSE.md`; 773,875 bytes, SHA-256 `e5c114afe73c9a0ec419fab5f5b3f63f3ce57b09b90d06ea85e63dca8aed3e7c`;
every file checked against the commit's tree): Paul-Louis Ageneau's fork, 26 Jun 2025, the commit libdatachannel
0.24.6 pins. `usrsctplib/`'s C files and headers, unchanged; its build scripts (CMake, meson, make), programs and
fuzzer aren't taken.

SCTP in the program, which carries libdatachannel's data channels inside DTLS: every byte, in order, resent when lost,
as TCP. Both builds compile every C file into a library of its own, as libdatachannel builds it, over its own links
only (no `INET` or `INET6`): `__Userspace__ SCTP_SIMPLE_ALLOCATOR SCTP_PROCESS_LEVEL_LOCKS`, with what its CMake
finds on each system (on the Mac `__APPLE_USE_RFC_2292` and its `HAVE_` settings, `src/mac/CMakeLists.txt`).
