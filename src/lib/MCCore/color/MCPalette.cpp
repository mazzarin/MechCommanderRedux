#include "stdafx.h"
#include "color/MCPalette.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "platform/MCRenderer.h"
#include "vfx/MCVfx.h"

std::string PalettePath;

namespace
{
    /// <summary>A whole file of the palette path, or why it couldn't be read.</summary>
    std::expected<std::vector<uint8_t>, std::string> ReadPaletteFile(std::string_view name, std::string_view extension)
    {
        const std::string fileName = GamePath(PalettePath, name, extension);
        MCFile file;

        if (const int32_t result = file.Open(fileName); result != 0)
        {
            return std::unexpected(std::format("Could not open {} ({:#x})", fileName, static_cast<uint32_t>(result)));
        }

        std::vector<uint8_t> data(file.FileSize());
        file.Read(data);
        return data;
    }

    /// <summary>A FIT error as the message of a failed load.</summary>
    std::string FitMessage(std::string_view fileName, std::string_view what, MCFitError error)
    {
        return std::format("{}: could not read {} ({:#x})", fileName, what,
                           static_cast<uint32_t>(std::to_underlying(error)));
    }
}

MCPalette::MCPalette(std::span<const uint8_t> palFile, std::vector<uint8_t> fadeTables, int32_t numBitmapHazeLevels)
    : FadePalettes(std::move(fadeTables)), NumBitmapHazeLevels(numBitmapHazeLevels)
{
    // The .pal header's colour count; the colours fill the palette from entry 0 whatever its first colour says.
    uint16_t numColors = 0;

    if (palFile.size() >= 4)
    {
        std::memcpy(&numColors, palFile.data() + 2, sizeof(numColors));
    }

    RgbData.assign(static_cast<size_t>(ColorCount) * 3, 0);
    const size_t size = std::min(
        {static_cast<size_t>(numColors) * 3, RgbData.size(), palFile.size() >= 4 ? palFile.size() - 4 : size_t{0}});
    std::copy_n(palFile.begin() + (palFile.size() >= 4 ? 4 : 0), size, RgbData.begin());

    if (!FadePalettes.empty())
    {
        MCRenderer::RegisterData(std::span(FadePalettes.data(), FadePalettes.size()), MCDataKind::Tables);
    }
}

MCPalette::~MCPalette()
{
    if (!FadePalettes.empty())
    {
        MCRenderer::UnregisterData(std::span(FadePalettes.data(), 1));
    }
}

auto MCPalette::Create(std::string_view paletteFileName) -> std::expected<std::unique_ptr<MCPalette>, std::string>
{
    const std::string fileName = GamePath(PalettePath, paletteFileName, ".fit");
    MCFitIniFile paletteFile;

    if (const int32_t result = paletteFile.Open(fileName); result != 0)
    {
        return std::unexpected(std::format("Could not open {} ({:#x})", fileName, static_cast<uint32_t>(result)));
    }

    if (paletteFile.SeekBlock("Palette") != 0)
    {
        return std::unexpected(FitMessage(fileName, "[Palette]", MCFitError::BlockNotFound));
    }

    const MCFitResult<int32_t> hazeLevels = paletteFile.Read<int32_t>("NumBitmapHazeLevels");

    if (!hazeLevels)
    {
        return std::unexpected(FitMessage(fileName, "NumBitmapHazeLevels", hazeLevels.error()));
    }

    const MCFitResult<std::string> palName = paletteFile.Read<std::string>("PaletteFileName");

    if (!palName)
    {
        return std::unexpected(FitMessage(fileName, "PaletteFileName", palName.error()));
    }

    if (paletteFile.SeekBlock("Tables") != 0)
    {
        return std::unexpected(FitMessage(fileName, "[Tables]", MCFitError::BlockNotFound));
    }

    const MCFitResult<std::string> fadeName = paletteFile.Read<std::string>("FadeTableFile");

    if (!fadeName)
    {
        return std::unexpected(FitMessage(fileName, "FadeTableFile", fadeName.error()));
    }

    const std::expected<std::vector<uint8_t>, std::string> palFile = ReadPaletteFile(*palName, ".pal");

    if (!palFile)
    {
        return std::unexpected(palFile.error());
    }

    std::expected<std::vector<uint8_t>, std::string> fadeTables = ReadPaletteFile(*fadeName, ".tbl");

    if (!fadeTables)
    {
        return std::unexpected(fadeTables.error());
    }

    return std::make_unique<MCPalette>(*palFile, std::move(*fadeTables), *hazeLevels);
}

auto GamePalette() -> MCPalette&
{
    return MCGameContext::Current().Palette();
}

auto FitColorTable(const std::function<std::array<uint8_t, 3>(int32_t)>& colourOf, MCColorTable table) -> MCTableFit
{
    // T(c) = c * k + C over the free colours (one k, a C a channel).
    constexpr int32_t count = LastFreeColor - FirstFreeColor + 1;
    constexpr double Most = 255.0;
    std::array<double, 3> meanIn{};
    std::array<double, 3> meanOut{};

    for (int32_t i = FirstFreeColor; i <= LastFreeColor; ++i)
    {
        const std::array<uint8_t, 3> in = colourOf(i);
        const std::array<uint8_t, 3> mapped = colourOf(table[static_cast<size_t>(i)]);

        for (size_t c = 0; c < 3; ++c)
        {
            meanIn[c] += in[c];
            meanOut[c] += mapped[c];
        }
    }

    for (size_t c = 0; c < 3; ++c)
    {
        meanIn[c] /= count * Most;
        meanOut[c] /= count * Most;
    }

    double covariance = 0.0;
    double variance = 0.0;

    for (int32_t i = FirstFreeColor; i <= LastFreeColor; ++i)
    {
        const std::array<uint8_t, 3> in = colourOf(i);
        const std::array<uint8_t, 3> mapped = colourOf(table[static_cast<size_t>(i)]);

        for (size_t c = 0; c < 3; ++c)
        {
            const double x = in[c] / Most;
            const double y = mapped[c] / Most;
            covariance += (x - meanIn[c]) * (y - meanOut[c]);
            variance += (x - meanIn[c]) * (x - meanIn[c]);
        }
    }

    MCTableFit fit{.Slope = std::clamp(variance > 0.0 ? covariance / variance : 1.0, 0.0, 1.0)};

    for (size_t c = 0; c < 3; ++c)
    {
        fit.Offset[c] = std::clamp(meanOut[c] - fit.Slope * meanIn[c], 0.0, 1.0);
    }

    return fit;
}

auto MCPalette::GetHazePalette(int32_t hazeLevel) const -> std::optional<MCColorTable>
{
    if (hazeLevel == 0)
    {
        return std::nullopt;
    }

    // Levels 1..n are the first set, -1..-n the second.
    const int32_t level = std::min(hazeLevel > 0 ? hazeLevel : -hazeLevel, NumBitmapHazeLevels);
    const int32_t set = hazeLevel > 0 ? 0 : NumBitmapHazeLevels;
    return std::span(FadePalettes).subspan(static_cast<size_t>(set + level - 1) * FadeTableSize).first<FadeTableSize>();
}

auto MCPalette::GetFadeTable(int32_t index) const -> std::optional<MCColorTable>
{
    // Issue #25: the index is a paint scheme from the mission or a pilot's file; one past the tables has none.
    const int64_t table = static_cast<int64_t>(index) + NumBitmapHazeLevels * 2;

    if (table < 0 || (table + 1) * static_cast<int64_t>(FadeTableSize) > static_cast<int64_t>(FadePalettes.size()))
    {
        return std::nullopt;
    }

    return std::span(FadePalettes).subspan(static_cast<size_t>(table) * FadeTableSize).first<FadeTableSize>();
}

auto MCPalette::Activate() -> void
{
    GuiSystem().ActivatePalette(RgbData, 0, ColorCount);
}

auto MCPalette::TweakPalette(int32_t start, std::span<const MCVfxRgb> colors) -> void
{
    for (const MCVfxRgb& color : colors)
    {
        std::memcpy(RgbData.data() + static_cast<size_t>(start & 0xff) * 3, &color, 3);
        start++;
    }
}
