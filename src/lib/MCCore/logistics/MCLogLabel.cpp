#include "stdafx.h"
#include "logistics/MCLogLabel.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "logistics/MCLogComboBox.h"
#include "platform/MCRenderer.h"
#include "vfx/MCVfxFunctions.h"

MCColorTable FontColors(uint8_t color)
{
    // One table per colour, made on first use; the renderers keep pointing at it, so it never moves or goes.
    static std::array<std::unique_ptr<std::array<uint8_t, 256>>, 256> tables;
    std::unique_ptr<std::array<uint8_t, 256>>& table = tables[color];

    if (table == nullptr)
    {
        table = std::make_unique<std::array<uint8_t, 256>>();

        for (size_t i = 0; i < table->size(); ++i)
        {
            (*table)[i] = i == 0xff ? 0xff : color;
        }

        MCRenderer::RegisterData(std::span(table->data(), table->size()), MCDataKind::Tables);
    }

    return MCColorTable(*table);
}

MCLogLabel::MCLogLabel(std::string_view text) : _Text(text)
{
}

MCLogLabel::MCLogLabel(std::string_view text, std::shared_ptr<MCGuiFont> font) : _Text(text), _Font(std::move(font))
{
}

auto MCLogLabel::InitAt(int32_t xPos, int32_t yPos) -> void
{
    const MCGuiFont& font = _Font != nullptr ? *_Font : *WhiteFont;
    MCLogObject::Init(xPos, yPos, std::max(font.Width(_Text), 1), std::max(font.Height(), 1));
    SetTransparent(true);
}

auto MCLogLabel::Draw() -> void
{
    if (_Font != nullptr)
    {
        VfxStringDraw(Lport()->Frame(), 0, 0, _Font->FontData.Bytes(), _Text, _Font->ColorTable);
        return;
    }

    VfxStringDraw(Lport()->Frame(), 0, 0, WhiteFont->FontData.Bytes(), _Text, MCLogComboBox::LabelColors());
}
