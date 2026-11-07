# libdatachannel 0.24.6

From https://codeload.github.com/paullouisageneau/libdatachannel/tar.gz/refs/tags/v0.24.6 (MPL 2.0, `LICENSE`;
52,656,037 bytes, SHA-256 `22a66aebebd8dae561d8e5181da96f2fcc203e067c5b2186c07b39af913464c0`; every file checked
against the tag's tree on GitHub): `include/` and `src/`, unchanged. Its examples (43 MB of them a video), tests, pages
and build scripts aren't taken; its submodules are their own folders here (`libjuice-1.7.4`, `usrsctp-fec583d5`,
`plog-94899e0b`, the commits its tag pins), and its libsrtp (audio and video) and JSON for Modern C++ aren't taken.

The internet games' links (`platform/MCIceLink`: WebRTC data channels) and, later, the WebSockets to the service. Both
builds compile every C++ file its CMake builds (MCCore.vcxproj lists them, `tools/vendor/ice_itemgroups.py` in the
workspace writes the list) into a library of its own, in C++17, with its CMake's settings under Mbed TLS:
`USE_MBEDTLS=1 USE_GNUTLS=0 USE_NICE=0 RTC_SYSTEM_JUICE=0 RTC_SYSTEM_SRTP=0 RTC_EXPORTS`, and for everything that
includes its headers `RTC_STATIC RTC_ENABLE_MEDIA=0 RTC_ENABLE_WEBSOCKET=1` (MechCommander.props on Windows, its
library's public settings in `src/mac/CMakeLists.txt`).
