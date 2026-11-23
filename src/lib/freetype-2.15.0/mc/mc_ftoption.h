/* The game's FreeType options (README-MC.md): the released ones, read through FT_CONFIG_OPTIONS_H, less what reads
   the machine or another library, so a face draws the same letters on every machine the game runs on. */
#ifndef MC_FTOPTION_H_
#define MC_FTOPTION_H_

#include <freetype/config/ftoption.h>

/* No compressed or Mac resource-fork fonts (zlib, LZW; Carbon), and no FREETYPE_PROPERTIES from the environment. */
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_LZW
#undef FT_CONFIG_OPTION_MAC_FONTS
#undef FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK
#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES

/* FreeType's C sums everywhere, not its assembly for some processors (x86-64's and 32-bit ARM's FT_MulFix). */
#define FT_CONFIG_OPTION_NO_ASSEMBLER

#endif
