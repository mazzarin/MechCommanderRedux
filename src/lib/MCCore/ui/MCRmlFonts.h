#pragma once

#include <RmlUi/Core/FontEngineInterface.h>

/// <summary>
/// Port-only (SPEC-menus.md, "The 1998 art"): RmlUi's font engine over the game's bitmap fonts (VFX fonts, as
/// gui/MCGuiFont reads them, with gui/MCFontGlyphs' added glyphs). A style sheet's <c>font-family</c> is the font file's
/// name under the game's <c>FontPath</c> without <c>.fnt</c> (<c>white10</c>, <c>yellow</c>); its <c>font-size</c> is the
/// height its letters are drawn at, its cell's height in the 1998 pixels being the size the art was drawn for (in
/// <c>dp</c>, the menu scale times it). Each face is drawn at that size in whole window pixels: its letters' bytes in the
/// fonts' colours (a 1998 picture's colour map: the fonts are the screens' palette indices), widths and places in
/// proportion to the 1998 ones. With HD art, its letters redrawn sharp from their strokes, as the sharp screen draws
/// them (platform/MCSharpText); without, each window pixel takes the 1998 pixel under its middle. Text is the game's
/// Windows-1252: a character it has no letter for is a question mark.
/// </summary>
/// <remarks>
/// A face's letters lie in one picture (an atlas) made once per face, drawn 1:1, so both renderers draw the same
/// pixels. RmlUi asks for sizes in whole pixels, so a size the menu scale makes fractional is cut down.
/// </remarks>
class MCRmlFonts final : public Rml::FontEngineInterface
{
public:
    /// <summary>The fonts drawn in <paramref name="colors"/>, sharp when <paramref name="sharp"/>.</summary>
    MCRmlFonts(const std::array<SDL_Color, 256>& colors, bool sharp);
    ~MCRmlFonts() override;
    MCRmlFonts(const MCRmlFonts&) = delete;
    MCRmlFonts& operator=(const MCRmlFonts&) = delete;

    /// <summary>Draws the letters sharp or not from now on (RmlUi makes its text again).</summary>
    void SetSharp(bool sharp);
    bool Sharp() const { return _Sharp; }

    /// <summary>The height of the font <paramref name="family"/>'s cells in 1998 pixels (0: no such font).</summary>
    int32_t CellHeight(std::string_view family);

    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight, int size) override;
    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle,
                                              const Rml::FontEffectList& fontEffects) override;
    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override;
    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
                       const Rml::TextShapingContext& textShapingContext, Rml::Character priorCharacter) override;
    int GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle faceHandle,
                       Rml::FontEffectsHandle fontEffectsHandle, Rml::StringView string, Rml::Vector2f position,
                       Rml::ColourbPremultiplied colour, float opacity,
                       const Rml::TextShapingContext& textShapingContext, Rml::TexturedMeshList& meshList) override;
    int GetVersion(Rml::FontFaceHandle handle) override;
    void ReleaseFontResources() override;
    void Shutdown() override;

    struct MCFont;
    struct MCFace;

private:
    /// <summary>The font file <paramref name="family"/> names, read the first time; none when it can't be read.</summary>
    MCFont* FontOf(std::string_view family);
    /// <summary>The face's atlas, made (again) when it has none or was made for the other look.</summary>
    void MakeAtlas(MCFace& face);

    std::array<SDL_Color, 256> _Colors{};
    bool _Sharp = false;
    /// <summary>Raised whenever the letters' look changes, so RmlUi makes its text again.</summary>
    int _Version = 1;
    std::map<std::string, std::unique_ptr<MCFont>, std::less<>> _Fonts;
    std::map<std::pair<std::string, int>, std::unique_ptr<MCFace>> _Faces;
};
