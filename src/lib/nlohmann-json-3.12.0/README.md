# JSON for Modern C++ 3.12.0

Niels Lohmann's JSON library (MIT, `LICENSE.MIT`), from https://github.com/nlohmann/json/releases/tag/v3.12.0's
`json.tar.xz` (114,576 bytes, SHA-256 `42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa`, as its
release notes give it): `single_include/nlohmann/json.hpp` (SHA-256
`aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`, as its notes give it too) and the licence,
unchanged.

Header-only: it reads updates' files (`release.json`, the key list and the content list, `update/`). Both builds add
`single_include` to MCCore's include path; only the update code includes it, never the precompiled header.
