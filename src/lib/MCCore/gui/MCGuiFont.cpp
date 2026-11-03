#include "stdafx.h"
#include "gui/MCGuiFont.h"
#include "gui/MCFontGlyphs.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "main/MCGamePaths.h"
#include "platform/MCRenderer.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The text as the original's C string saw it: up to its first NUL.</summary>
    std::string_view CString(std::string_view text)
    {
        return text.substr(0, text.find('\0'));
    }
}

MCGuiFont::~MCGuiFont()
{
    Unload();
    MCRenderer::UnregisterData(std::span(ColorTable.data(), ColorTable.size()));
}

auto MCGuiFont::Create(std::string_view fileName) -> std::expected<std::unique_ptr<MCGuiFont>, std::string>
{
    auto font = std::make_unique<MCGuiFont>();

    if (const int32_t result = font->Load(fileName); result != 0)
    {
        return std::unexpected(std::format("font {} is {}", fileName, result == -3 ? "damaged" : "empty"));
    }

    return font;
}

auto MCGuiFont::Load(std::string_view fileName) -> int32_t
{
    Unload();
    MCFile file;
    const std::string path = std::format("{}{}", std::string_view(FontPath), fileName);

    if (file.Open(path) != 0)
    {
        GeneralMsg(std::format("Unable to find '{}'", path));
    }

    const uint32_t size = file.FileSize();

    if (size == 0)
    {
        return -2;
    }

    std::vector<uint8_t> data(size);
    file.Read(data);
    file.Close();

    // A font whose glyphs don't fit its data is refused (issue #25).
    if (!VfxFontFits(data))
    {
        return -3;
    }

    // Port-only: glyphs for the characters the font leaves blank (´, §, ...).
    MCFontGlyphs::AddMissing(data);
    FontData = MCRegisteredBlock(data.size(), MCDataKind::Shapes);
    std::ranges::copy(data, FontData.Bytes().begin());

    for (size_t i = 0; i < ColorTable.size(); i++)
    {
        ColorTable[i] = static_cast<uint8_t>(i);
    }

    MCRenderer::RegisterData(std::span(ColorTable.data(), ColorTable.size()), MCDataKind::Tables);
    return 0;
}

auto MCGuiFont::Unload() -> void
{
    FontData = {};
}

auto MCGuiFont::Height() const -> int32_t
{
    if (FontData.Empty())
    {
        return 0;
    }

    return VfxFontHeight(FontData.Bytes());
}

auto MCGuiFont::Width(std::string_view text) const -> int32_t
{
    if (FontData.Empty())
    {
        return 0;
    }

    int32_t total = 0;

    for (const char c : CString(text))
    {
        total += VfxCharacterWidth(FontData.Bytes(), static_cast<uint8_t>(c));
    }

    return total;
}

auto MCGuiFont::Width(uint8_t c) const -> int32_t
{
    if (c == 0 || FontData.Empty())
    {
        return 0;
    }

    return VfxCharacterWidth(FontData.Bytes(), c);
}

auto MCGuiFont::Draws(uint8_t c) const -> bool
{
    return !FontData.Empty() && MCFontGlyphs::Draws(FontData.Bytes(), c);
}

auto MCGuiFont::WriteChar(const MCPane& pane, int32_t xPos, int32_t yPos, char c) -> int32_t
{
    if (FontData.Empty())
    {
        return -3;
    }

    if (c != 0)
    {
        VfxCharacterDraw(pane, xPos, yPos, FontData.Bytes(), static_cast<uint8_t>(c), MCColorTable(ColorTable));
    }

    return 0;
}

auto MCGuiFont::WriteString(const MCPane& pane, int32_t xPos, int32_t yPos, std::string_view text, int32_t maxWidth)
    -> int32_t
{
    if (FontData.Empty())
    {
        return -3;
    }

    text = CString(text);

    if (text.empty())
    {
        return 0;
    }

    // Too wide: the last character comes off until it fits.
    if (maxWidth != -1)
    {
        while (!text.empty() && maxWidth < Width(text))
        {
            text.remove_suffix(1);
        }
    }

    const std::string shown(text);
    VfxStringDraw(pane, xPos, yPos, FontData.Bytes(), shown.c_str(), MCColorTable(ColorTable));
    return 0;
}

auto MCGuiFont::WriteStringToNewline(const MCPane& pane, int32_t xPos, int32_t yPos, std::string_view text) -> int32_t
{
    if (FontData.Empty())
    {
        return -3;
    }

    text = CString(text);
    const std::string line(text.substr(0, text.find('\n')));

    if (!line.empty())
    {
        VfxStringDraw(pane, xPos, yPos, FontData.Bytes(), line.c_str(), MCColorTable(ColorTable));
    }

    return 0;
}

auto MCGuiFont::CharactersToWidth(std::string_view text, int32_t maxWidth, bool wordWrap) const -> int32_t
{
    text = CString(text);

    if (!wordWrap)
    {
        // Characters come off the end (as WriteString cuts) until the rest fits.
        size_t end = text.size();

        while (end > 0 && Width(text.substr(0, end)) > maxWidth)
        {
            end--;
        }

        return static_cast<int32_t>(end);
    }

    if (Width(text) <= maxWidth)
    {
        return static_cast<int32_t>(text.size());
    }

    // Cut at the last space, then the one before it, until the rest fits.
    size_t space = text.rfind(' ');

    if (space == std::string_view::npos)
    {
        return -1;
    }

    while (maxWidth < Width(text.substr(0, space)))
    {
        space = space == 0 ? std::string_view::npos : text.rfind(' ', space - 1);

        if (space == std::string_view::npos)
        {
            return -1;
        }
    }

    return static_cast<int32_t>(space);
}
