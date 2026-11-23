# FreeType 2.15.0

FreeType (the FreeType License, `docs/FTL.TXT`, taken of its two: `LICENSE.TXT`), the font engine RmlUi's own draws
the remade menus' letters with (`src/lib/RmlUi-6.3`'s `FontEngineDefault`; the workspace's `SPEC-menus.md`, "How
it's built"). From `https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz` (2,716,928 bytes,
SHA-256 `93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5`, the same as Homebrew's formula's), every
file kept checked against the archive's: all the same byte for byte.

Only what a TrueType face drawn in grey needs is kept, unchanged: `include`, and in `src` the modules `base`,
`truetype`, `sfnt`, `smooth` (the grey rasteriser), `autofit` (the hinter RmlUi asks for) and `psnames`, with
`LICENSE.TXT`, `README` and `docs/FTL.TXT`. No other drivers (Type 1, CFF, PCF, BDF, ...), no gzip, LZW, bzip2,
Brotli, PNG or HarfBuzz.

Ours, in `mc`: `mc_ftmodule.h`, the modules (`FT_CONFIG_MODULES_H`), and `mc_ftoption.h` (`FT_CONFIG_OPTIONS_H`):
FreeType's own options without zlib, LZW, Mac fonts' resource forks and the environment's properties, and with
`FT_CONFIG_OPTION_NO_ASSEMBLER`, so its fixed-point sums are C on every machine and its letters the same on the Mac
and Windows (the remade screens' recorded frames are). Built as its own unity files say (`autofit.c`, `ftbase.c`,
`ftbitmap.c`, `ftdebug.c`, `ftinit.c`, `ftmm.c`, `ftsystem.c`, `psnames.c`, `sfnt.c`, `smooth.c`, `truetype.c`)
with `FT2_BUILD_LIBRARY`: in `src/lib/MCCore/MCCore.vcxproj`'s own ItemGroup on Windows (as C, its warnings off), and
as the `mc_freetype` library in `src/mac/CMakeLists.txt` (`-ffp-contract=off`, so no fused multiply-adds either).
