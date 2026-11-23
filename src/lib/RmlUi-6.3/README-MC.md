# RmlUi 6.3

RmlUi (MIT, `LICENSE.txt`; its readme is `readme.md`), the library the remade menus are laid out, styled and run
with (the workspace's `SPEC-menus.md`, "How it's built"). From `https://github.com/mikke89/RmlUi`'s tag 6.3 as an
archive (`RmlUi-6.3.tar.gz`, 3,977,268 bytes, SHA-256
`d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede`), every one of its 1,183 files checked against the
tag's tree on GitHub: all the same byte for byte.

Only its core and its font engine are kept, unchanged: `Include/RmlUi/Core.h`, `Include/RmlUi/Core` and
`Include/RmlUi/Config`, and `Source/Core` with `FontEngineDefault` (its FreeType font engine, the menus' face drawn
with `src/lib/freetype-2.15.0`). No Lua, SVG, Lottie, debugger, back ends, samples or tests.

Built as its own CMake builds a static library with `RMLUI_FONT_ENGINE` set to `freetype`: `RMLUI_STATIC_LIB` for
every unit that includes its headers (`src/MechCommander.props`, `src/mac/CMakeLists.txt`), `RMLUI_VERSION="6.3"`
and `RMLUI_FONT_ENGINE_FREETYPE` for `Core.cpp`, FreeType's headers and configuration for `FreeTypeInterface.cpp`,
in C++17. Its third-party containers (robin_hood, itlib) stay on, its default.

Builds list its 191 C++ files in `src/lib/MCCore/MCCore.vcxproj` (their own ItemGroup); `src/mac/CMakeLists.txt`
reads the list from there.
