#include "stdafx.h"
#include "vfx/MCVfxClip.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"

// The game's translucency tables (mcx\vfx\AlphaPalette.cpp): for every "alpha colour" defined in AlphaPal.ini, the
// palette index that colour blended over each background index gives.

std::array<uint8_t, AlphaColorCount * 256> AlphaTable{};
std::array<uint8_t, AlphaColorCount> SpecialColor{};
std::array<MCAlphaColor, AlphaColorCount> MCAlphaColors{};

namespace
{
    /// <summary>The longest AlphaPal.ini line read (the reader's limit: a longer line is cut, OB-135).</summary>
    constexpr int32_t AlphaPalLineLength = 0x100;

    /// <summary>__ftol then the clamp to 0..255 InitAlphaLookup applies.</summary>
    int ClampComponent(double value)
    {
        const int component = static_cast<int>(value);
        return component < 0 ? 0 : (component > 0xff ? 0xff : component);
    }

    /// <summary>Reads AlphaPal.ini: the entry of every alpha colour it defines, which it marks in SpecialColor.</summary>
    std::array<MCAlphaColor, AlphaColorCount> ReadAlphaPal()
    {
        std::array<MCAlphaColor, AlphaColorCount> entries{};
        MCFile file;

        if (const int32_t result = file.Open("AlphaPal.ini"); result != NO_ERR)
        {
            Fatal(result, "Could not open alphapal.ini");
        }

        int32_t lineNumber = 0;

        while (!file.Eof())
        {
            const std::string line = file.ReadLine(AlphaPalLineLength);
            ++lineNumber;
            const char first = line.empty() ? '\0' : line[0];

            if (first == '#' || first == ';' || first == '\n' || first == '\0')
            {
                continue;
            }

            int index = 0;

            if (std::sscanf(line.c_str(), "%d", &index) == EOF)
            {
                break;
            }

            if (index < 0 || index >= AlphaColorCount)
            {
                Fatal(0, std::format("Invalid index field in AlphaPal.ini, line {}", lineNumber));
            }

            SpecialColor[static_cast<size_t>(index)] = 1;
            MCAlphaColor& entry = entries[static_cast<size_t>(index)];
            int ignored = 0;

            if (std::sscanf(line.c_str(), "%d %f %f %f %f %f", &ignored, &entry.R, &entry.G, &entry.B, &entry.Alpha,
                            &entry.BackgroundWeight) == EOF)
            {
                Fatal(0, std::format("Invalid data in AlphaPal.ini, line {}", lineNumber));
            }

            // The dodge form divides by 255 - component.
            if (entry.Alpha == 0.0f && entry.BackgroundWeight == 0.0f &&
                (entry.R == 255.0f || entry.G == 255.0f || entry.B == 255.0f))
            {
                Fatal(0, std::format("Source color cannot be 255 in Alphapal.ini!, line {}", lineNumber));
            }
        }

        return entries;
    }
}

void InitAlphaLookup(std::span<const MCVfxRgb, 256> palette)
{
    SpecialColor.fill(0);
    MCAlphaColors = ReadAlphaPal();

    // The x87 ran at 53-bit precision (the MSVC CRT default), so double arithmetic gives the same results.
    auto out = AlphaTable.begin();

    for (int32_t color = 0; color < AlphaColorCount; ++color)
    {
        const MCAlphaColor& entry = MCAlphaColors[static_cast<size_t>(color)];

        for (int32_t background = 0; background < 256; ++background, ++out)
        {
            if (color == 0xff || color == 0)
            {
                *out = static_cast<uint8_t>(background);
            }
            else if (SpecialColor[static_cast<size_t>(color)] == 0)
            {
                *out = static_cast<uint8_t>(color);
            }
            else if (background < 10 || background > 0xf5)
            {
                *out = 0xff;
            }
            else
            {
                const MCVfxRgb& bg = palette[static_cast<size_t>(background)];
                const double r8 = static_cast<double>(bg.R << 2);
                const double g8 = static_cast<double>(bg.G << 2);
                const double b8 = static_cast<double>(bg.B << 2);
                int r;
                int g;
                int b;

                if (entry.Alpha == 0.0f && entry.BackgroundWeight == 0.0f)
                {
                    r = ClampComponent(r8 * 255.0 / (255.0 - static_cast<double>(entry.R)));
                    g = ClampComponent(g8 * 255.0 / (255.0 - static_cast<double>(entry.G)));
                    b = ClampComponent(b8 * 255.0 / (255.0 - static_cast<double>(entry.B)));
                }
                else
                {
                    const double a = entry.Alpha;
                    const double w = entry.BackgroundWeight;
                    r = ClampComponent(r8 * w + static_cast<double>(entry.R) * a);
                    g = ClampComponent(g8 * w + static_cast<double>(entry.G) * a);
                    b = ClampComponent(b8 * w + static_cast<double>(entry.B) * a);
                }

                *out = FindClosest(palette, r >> 2, g >> 2, b >> 2);
            }
        }
    }

    // Its rows are tables too (the logistics darken table is one).
    MCRenderer::RegisterData(AlphaTable, MCDataKind::Tables);
    MCRenderer::AlphaTableChanged();
}

MCAlphaBlend AlphaColorBlend(const MCAlphaColor& entry)
{
    constexpr float Full = 255.0f;

    // A colour dodge (background * 255 / (255 - RGB)) brightens what is under it.
    if (entry.Alpha == 0.0f && entry.BackgroundWeight == 0.0f)
    {
        return {entry.R / Full, entry.G / Full, entry.B / Full, 0.0f};
    }

    return {std::clamp(entry.R * entry.Alpha / Full, 0.0f, 1.0f), std::clamp(entry.G * entry.Alpha / Full, 0.0f, 1.0f),
            std::clamp(entry.B * entry.Alpha / Full, 0.0f, 1.0f),
            std::clamp(1.0f - entry.BackgroundWeight, 0.0f, 1.0f)};
}

uint8_t FindClosest(std::span<const MCVfxRgb, 256> palette, int r, int g, int b)
{
    uint8_t best = 10;
    int bestDistance = 0xfd02ff;

    // Only the free colours: 0..9 and 246..255 are the system's.
    for (int index = 10; index < 0xf6; ++index)
    {
        const MCVfxRgb& entry = palette[static_cast<size_t>(index)];
        const int dg = g - entry.G;
        const int db = b - entry.B;
        const int dr = r - entry.R;
        const int distance = dg * dg * 51 + db * db * 10 + dr * dr * 39;

        if (distance < bestDistance)
        {
            best = static_cast<uint8_t>(index);
            bestDistance = distance;

            if (distance == 0)
            {
                return best;
            }
        }
    }

    return best;
}
