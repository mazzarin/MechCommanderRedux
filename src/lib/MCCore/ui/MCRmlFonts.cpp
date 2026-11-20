#include "stdafx.h"
#include "ui/MCRmlFonts.h"
#include "gui/MCFontGlyphs.h"
#include "lib/MCFile.h"
#include "main/MCGamePaths.h"
#include "platform/MCSharpPictures.h"
#include "platform/MCSharpText.h"
#include "vfx/MCVfxFunctions.h"

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/FontMetrics.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StringUtilities.h>

namespace
{
    constexpr size_t PixelBytes = 4;
    constexpr uint32_t Full = 255;

    /// <summary>The characters a face's atlas holds: the printable ASCII, then Windows-1252's 0xA0 to 0xFF.</summary>
    constexpr uint8_t FirstAscii = 0x20;
    constexpr uint8_t LastAscii = 0x7e;
    constexpr uint8_t FirstHigh = 0xa0;
    constexpr uint8_t LastHigh = 0xff;

    /// <summary>What a character the fonts have no letter for is drawn as.</summary>
    constexpr uint8_t Unknown = '?';

    /// <summary>The atlas's width, and the space kept between letters (so a letter's edge never takes its neighbour's).</summary>
    constexpr int32_t AtlasWidth = 1024;
    constexpr int32_t AtlasGap = 1;

    /// <summary>The font file a family names: its name under FontPath, with .fnt.</summary>
    constexpr std::string_view FontExtension = ".fnt";

    /// <summary>Unicode's few characters Windows-1252 keeps at 0x80-0x9F that text may hold, and theirs.</summary>
    constexpr std::array<std::pair<char32_t, uint8_t>, 6> Punctuation = {{
        {U'‘', '\''},
        {U'’', '\''},
        {U'“', '"'},
        {U'”', '"'},
        {U'–', '-'},
        {U'—', '-'},
    }};

    /// <summary>The game's character for <paramref name="character"/>.</summary>
    uint8_t GameCharacter(char32_t character)
    {
        constexpr char32_t AsciiEnd = 0x80;

        if (character < AsciiEnd || (character >= FirstHigh && character <= LastHigh))
        {
            return static_cast<uint8_t>(character);
        }

        const auto found = std::ranges::find(Punctuation, character, &std::pair<char32_t, uint8_t>::first);
        return found != Punctuation.end() ? found->second : Unknown;
    }

    /// <summary><paramref name="value"/> times <paramref name="size"/> over <paramref name="cell"/>, to the nearest.</summary>
    int32_t Scaled(int64_t value, int32_t size, int32_t cell)
    {
        return static_cast<int32_t>((2 * value * size + cell) / (2 * static_cast<int64_t>(cell)));
    }

    /// <summary>Whether a character is in a face's atlas.</summary>
    bool InAtlas(uint8_t character)
    {
        return (character >= FirstAscii && character <= LastAscii) || character >= FirstHigh;
    }
}

/// <summary>A font file, and its letters redrawn sharp (eight times the size, premultiplied) as they're asked for.</summary>
struct MCRmlFonts::MCFont
{
    std::vector<uint8_t> Data;
    int32_t Height = 0;
    MCSharpText::MCFontLayers Layers;
    std::map<uint8_t, std::vector<uint8_t>> Sharp;

    int32_t Width(uint8_t character) const { return std::max(VfxCharacterWidth(Data, character), 0); }
};

/// <summary>A font at a size: its metrics and its atlas.</summary>
struct MCRmlFonts::MCFace
{
    struct MCGlyph
    {
        int32_t X = 0;
        int32_t Y = 0;
        int32_t Width = 0;
    };

    MCFont* Font = nullptr;
    int32_t Size = 0;
    Rml::FontMetrics Metrics{};
    std::array<std::optional<MCGlyph>, 256> Glyphs;
    int32_t AtlasHeight = 0;
    bool Made = false;
    bool MadeSharp = false;
    std::unique_ptr<Rml::CallbackTextureSource> Texture;
};

namespace
{
    /// <summary>
    /// The glyph <paramref name="character"/> of <paramref name="font"/> redrawn sharp at eight times its size, its layers
    /// laid back to front: premultiplied RGBA8 in <paramref name="colors"/>.
    /// </summary>
    std::vector<uint8_t> RedrawSharp(MCRmlFonts::MCFont& font, uint8_t character,
                                     const std::array<SDL_Color, 256>& colors)
    {
        const std::optional<MCSharpPictures::MCFontGlyph> glyph = MCSharpPictures::FontGlyph(font.Data, character);

        if (!glyph.has_value() || glyph->Pitch <= 0 || glyph->Height <= 0)
        {
            return {};
        }

        const MCSharpPictures::MCGlyphPictures pictures =
            MCSharpPictures::GlyphPictures(glyph->Pixels, glyph->Pitch, glyph->Height, font.Layers);
        const std::vector<std::vector<uint8_t>> layers =
            MCSharpPictures::RedrawGlyph(glyph->Pixels, glyph->Pitch, glyph->Height, pictures);
        const size_t pixels = static_cast<size_t>(glyph->Pitch * MCSharpText::Scale) *
                              static_cast<size_t>(glyph->Height * MCSharpText::Scale);
        std::vector<uint8_t> out(pixels * PixelBytes);

        for (const std::vector<uint8_t>& layer : layers)
        {
            if (layer.size() < pixels * PixelBytes)
            {
                continue;
            }

            for (size_t i = 0; i < pixels; ++i)
            {
                const uint32_t coverage = layer[i * PixelBytes + 1];

                if (coverage == 0)
                {
                    continue;
                }

                const SDL_Color& color = colors[layer[i * PixelBytes]];
                const std::array<uint32_t, 4> source = {color.r * coverage / Full, color.g * coverage / Full,
                                                        color.b * coverage / Full, coverage};

                for (size_t channel = 0; channel < PixelBytes; ++channel)
                {
                    uint8_t& under = out[i * PixelBytes + channel];
                    under = static_cast<uint8_t>(source[channel] + under * (Full - coverage) / Full);
                }
            }
        }

        return out;
    }

    /// <summary>
    /// The <paramref name="width"/> x <paramref name="height"/> picture <paramref name="sharp"/> (premultiplied RGBA8)
    /// brought down to <paramref name="toWidth"/> x <paramref name="toHeight"/>, each pixel the mean of the part it
    /// covers, in whole numbers.
    /// </summary>
    void BoxDown(std::span<const uint8_t> sharp, int32_t width, int32_t height, std::span<uint8_t> out,
                 int32_t outPitch, int32_t toWidth, int32_t toHeight)
    {
        // Measured in toWidth (toHeight) parts of a picture pixel: a picture pixel is toWidth long, an out pixel width.
        const uint64_t area = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);

        for (int32_t y = 0; y < toHeight; ++y)
        {
            const int64_t top = static_cast<int64_t>(y) * height;
            const int64_t bottom = top + height;

            for (int32_t x = 0; x < toWidth; ++x)
            {
                const int64_t left = static_cast<int64_t>(x) * width;
                const int64_t right = left + width;
                std::array<uint64_t, 4> sum{};

                for (int64_t sy = top / toHeight; sy < height && sy * toHeight < bottom; ++sy)
                {
                    const int64_t rows = std::min(bottom, (sy + 1) * toHeight) - std::max(top, sy * toHeight);

                    for (int64_t sx = left / toWidth; sx < width && sx * toWidth < right; ++sx)
                    {
                        const int64_t columns = std::min(right, (sx + 1) * toWidth) - std::max(left, sx * toWidth);
                        const auto weight = static_cast<uint64_t>(rows * columns);
                        const size_t at =
                            (static_cast<size_t>(sy) * static_cast<size_t>(width) + static_cast<size_t>(sx)) *
                            PixelBytes;

                        for (size_t channel = 0; channel < PixelBytes; ++channel)
                        {
                            sum[channel] += sharp[at + channel] * weight;
                        }
                    }
                }

                const size_t at =
                    (static_cast<size_t>(y) * static_cast<size_t>(outPitch) + static_cast<size_t>(x)) * PixelBytes;

                for (size_t channel = 0; channel < PixelBytes; ++channel)
                {
                    out[at + channel] = static_cast<uint8_t>((sum[channel] + area / 2) / area);
                }
            }
        }
    }

    /// <summary>
    /// The glyph's bytes brought to <paramref name="toWidth"/> x <paramref name="toHeight"/>: each pixel the 1998
    /// pixel under its middle, in its colour (none where the glyph has no ink).
    /// </summary>
    void Nearest(const MCSharpPictures::MCFontGlyph& glyph, const std::array<SDL_Color, 256>& colors,
                 std::span<uint8_t> out, int32_t outPitch, int32_t toWidth, int32_t toHeight)
    {
        for (int32_t y = 0; y < toHeight; ++y)
        {
            const int32_t sy = ((2 * y + 1) * glyph.Height) / (2 * toHeight);

            for (int32_t x = 0; x < toWidth; ++x)
            {
                const int32_t sx = ((2 * x + 1) * glyph.Pitch) / (2 * toWidth);
                const uint8_t ink = glyph.Pixels[static_cast<size_t>(sy * glyph.Pitch + sx)];

                if (ink == MCSharpText::NoInk)
                {
                    continue;
                }

                const SDL_Color& color = colors[ink];
                const size_t at =
                    (static_cast<size_t>(y) * static_cast<size_t>(outPitch) + static_cast<size_t>(x)) * PixelBytes;
                out[at] = color.r;
                out[at + 1] = color.g;
                out[at + 2] = color.b;
                out[at + 3] = static_cast<uint8_t>(Full);
            }
        }
    }
}

MCRmlFonts::MCRmlFonts(const std::array<SDL_Color, 256>& colors, bool sharp) : _Colors(colors), _Sharp(sharp)
{
}

MCRmlFonts::~MCRmlFonts() = default;

void MCRmlFonts::SetSharp(bool sharp)
{
    if (sharp != _Sharp)
    {
        _Sharp = sharp;
        ++_Version;
    }
}

auto MCRmlFonts::FontOf(std::string_view family) -> MCFont*
{
    std::string name(family);
    std::erase_if(name, [](char c) { return c == '"' || c == '\'' || c == ' '; });
    std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });

    if (const auto found = _Fonts.find(name); found != _Fonts.end())
    {
        return found->second.get();
    }

    std::unique_ptr<MCFont> font;

    if (!name.empty() && name.find_first_of("/\\:.") == std::string::npos)
    {
        MCFile file;

        if (file.Open(std::format("{}{}{}", std::string_view(FontPath), name, FontExtension)) == 0)
        {
            std::vector<uint8_t> data(file.FileSize());
            file.Read(data);
            file.Close();

            if (VfxFontFits(data) && VfxFontHeight(data) > 0)
            {
                // As gui/MCGuiFont loads a font: with the letters it leaves blank made.
                MCFontGlyphs::AddMissing(data);
                font = std::make_unique<MCFont>();
                font->Height = VfxFontHeight(data);
                font->Layers = MCSharpText::LayersOf(data);
                font->Data = std::move(data);
            }
        }
    }

    if (font == nullptr)
    {
        Rml::Log::Message(Rml::Log::LT_WARNING, "No game font '%s%s' for the font family '%s'", name.c_str(),
                          FontExtension.data(), std::string(family).c_str());
    }

    return _Fonts.emplace(std::move(name), std::move(font)).first->second.get();
}

int32_t MCRmlFonts::CellHeight(std::string_view family)
{
    const MCFont* font = FontOf(family);
    return font != nullptr ? font->Height : 0;
}

Rml::FontFaceHandle MCRmlFonts::GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle /*style*/,
                                                  Rml::Style::FontWeight /*weight*/, int size)
{
    MCFont* font = FontOf(family);

    if (font == nullptr || size <= 0)
    {
        return 0;
    }

    auto& face = _Faces[{family, size}];

    if (face == nullptr)
    {
        face = std::make_unique<MCFace>();
        face->Font = font;
        face->Size = size;
        // The letters stand on the baseline, their cell above it (the fonts have no descenders below it).
        constexpr float Half = 0.5f;
        face->Metrics = Rml::FontMetrics{
            .size = size,
            .ascent = static_cast<float>(size),
            .descent = 0.0f,
            .line_spacing = static_cast<float>(size),
            .x_height = static_cast<float>(size) * Half,
            .underline_position = 1.0f,
            .underline_thickness = 1.0f,
            .has_ellipsis = false,
        };
    }

    return reinterpret_cast<Rml::FontFaceHandle>(face.get());
}

Rml::FontEffectsHandle MCRmlFonts::PrepareFontEffects(Rml::FontFaceHandle /*handle*/,
                                                      const Rml::FontEffectList& /*fontEffects*/)
{
    // The menus' style sheets give the letters no effects: the fonts' own shadows are in their bytes.
    return 0;
}

const Rml::FontMetrics& MCRmlFonts::GetFontMetrics(Rml::FontFaceHandle handle)
{
    return reinterpret_cast<const MCFace*>(handle)->Metrics;
}

int MCRmlFonts::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
                               const Rml::TextShapingContext& /*textShapingContext*/, Rml::Character /*priorCharacter*/)
{
    const auto& face = *reinterpret_cast<const MCFace*>(handle);
    int64_t width = 0;

    for (Rml::StringIteratorU8 it(string); it; ++it)
    {
        width += face.Font->Width(GameCharacter(static_cast<char32_t>(*it)));
    }

    return Scaled(width, face.Size, face.Font->Height);
}

void MCRmlFonts::MakeAtlas(MCFace& face)
{
    if (face.Made && face.MadeSharp == _Sharp)
    {
        return;
    }

    MCFont& font = *face.Font;
    face.Glyphs = {};
    int32_t x = 0;
    int32_t y = 0;

    // Places first: a row of letters each as wide as its 1998 width scaled, as tall as the face.
    for (int32_t character = FirstAscii; character <= LastHigh; ++character)
    {
        const auto c = static_cast<uint8_t>(character);

        if (!InAtlas(c) || !MCFontGlyphs::Draws(font.Data, c))
        {
            continue;
        }

        const int32_t width = Scaled(font.Width(c), face.Size, font.Height);

        if (width <= 0)
        {
            continue;
        }

        if (x + width > AtlasWidth)
        {
            x = 0;
            y += face.Size + AtlasGap;
        }

        face.Glyphs[c] = MCFace::MCGlyph{x, y, width};
        x += width + AtlasGap;
    }

    face.AtlasHeight = y + face.Size;
    auto pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(AtlasWidth) *
                                                         static_cast<size_t>(face.AtlasHeight) * PixelBytes);
    const std::span<uint8_t> atlas(*pixels);

    for (size_t c = 0; c < face.Glyphs.size(); ++c)
    {
        const std::optional<MCFace::MCGlyph>& place = face.Glyphs[c];
        const auto character = static_cast<uint8_t>(c);
        const std::optional<MCSharpPictures::MCFontGlyph> glyph =
            place.has_value() ? MCSharpPictures::FontGlyph(font.Data, character) : std::nullopt;

        if (!glyph.has_value() || glyph->Pitch <= 0)
        {
            continue;
        }

        const std::span<uint8_t> at =
            atlas.subspan((static_cast<size_t>(place->Y) * AtlasWidth + static_cast<size_t>(place->X)) * PixelBytes);

        if (_Sharp)
        {
            auto [kept, made] = font.Sharp.try_emplace(character);

            if (made)
            {
                kept->second = RedrawSharp(font, character, _Colors);
            }

            if (!kept->second.empty())
            {
                BoxDown(kept->second, glyph->Pitch * MCSharpText::Scale, glyph->Height * MCSharpText::Scale, at,
                        AtlasWidth, place->Width, face.Size);
            }
        }
        else
        {
            Nearest(*glyph, _Colors, at, AtlasWidth, place->Width, face.Size);
        }
    }

    const Rml::Vector2i size(AtlasWidth, face.AtlasHeight);
    face.Texture = std::make_unique<Rml::CallbackTextureSource>(
        [pixels, size](const Rml::CallbackTextureInterface& texture)
        { return texture.GenerateTexture(Rml::Span<const Rml::byte>(pixels->data(), pixels->size()), size); });
    face.Made = true;
    face.MadeSharp = _Sharp;
}

int MCRmlFonts::GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle faceHandle,
                               Rml::FontEffectsHandle /*fontEffectsHandle*/, Rml::StringView string,
                               Rml::Vector2f position, Rml::ColourbPremultiplied colour, float /*opacity*/,
                               const Rml::TextShapingContext& /*textShapingContext*/, Rml::TexturedMeshList& meshList)
{
    auto& face = *reinterpret_cast<MCFace*>(faceHandle);
    MakeAtlas(face);
    meshList.resize(1);
    meshList[0].texture = face.Texture->GetTexture(renderManager);
    Rml::Mesh& mesh = meshList[0].mesh;
    const MCFont& font = *face.Font;
    // Whole pixels: the baseline's start, and the cell above it.
    const auto left = static_cast<int32_t>(std::floor(position.x + 0.5f));
    const int32_t top = static_cast<int32_t>(std::floor(position.y + 0.5f)) - face.Size;
    const float atlasWidth = static_cast<float>(AtlasWidth);
    const auto atlasHeight = static_cast<float>(face.AtlasHeight);
    int64_t along = 0;

    for (Rml::StringIteratorU8 it(string); it; ++it)
    {
        const uint8_t character = GameCharacter(static_cast<char32_t>(*it));

        if (const std::optional<MCFace::MCGlyph>& glyph = face.Glyphs[character]; glyph.has_value())
        {
            const Rml::Vector2f origin(static_cast<float>(left + Scaled(along, face.Size, font.Height)),
                                       static_cast<float>(top));
            const Rml::Vector2f dimensions(static_cast<float>(glyph->Width), static_cast<float>(face.Size));
            const Rml::Vector2f topLeft(static_cast<float>(glyph->X) / atlasWidth,
                                        static_cast<float>(glyph->Y) / atlasHeight);
            const Rml::Vector2f bottomRight(static_cast<float>(glyph->X + glyph->Width) / atlasWidth,
                                            static_cast<float>(glyph->Y + face.Size) / atlasHeight);
            Rml::MeshUtilities::GenerateQuad(mesh, origin, dimensions, colour, topLeft, bottomRight);
        }

        along += font.Width(character);
    }

    return Scaled(along, face.Size, font.Height);
}

int MCRmlFonts::GetVersion(Rml::FontFaceHandle /*handle*/)
{
    return _Version;
}

void MCRmlFonts::ReleaseFontResources()
{
    _Faces.clear();
}

void MCRmlFonts::Shutdown()
{
    _Faces.clear();
    _Fonts.clear();
}
