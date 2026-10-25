#pragma once

#include "logistics/MCLogObject.h"

class MCGuiFont;

/// <summary>
/// Port: a colour table drawing a font's inked values in <paramref name="color"/> (255 stays out), made and
/// registered with the renderers the first time a colour is asked for, and kept.
/// </summary>
MCColorTable FontColors(uint8_t color);

/// <summary>
/// Port: a line of fixed text on a logistics screen, drawn each frame as the art's labels look (the white font in the
/// labels' colour 0xe3), or in another font. The mouse goes through it.
/// </summary>
class MCLogLabel : public MCLogObject
{
public:
    /// <summary>A label showing <paramref name="text"/>.</summary>
    explicit MCLogLabel(std::string_view text);

    /// <summary>A label showing <paramref name="text"/> in <paramref name="font"/>, in the font's own colours.</summary>
    MCLogLabel(std::string_view text, std::shared_ptr<MCGuiFont> font);

    /// <summary>Places the label with its text's top left corner at (<paramref name="xPos"/>, <paramref name="yPos"/>), as big as the text.</summary>
    void InitAt(int32_t xPos, int32_t yPos);

    /// <summary>Draws the text.</summary>
    void Draw() override;

    /// <summary>The label draws itself each frame.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>The label is part of the screen's picture: the mouse goes through it.</summary>
    std::shared_ptr<MCGuiObject> FindObject(int32_t, int32_t) override { return nullptr; }

    /// <summary>The text.</summary>
    const std::string& Text() const { return _Text; }

private:
    /// <summary>The text.</summary>
    std::string _Text;
    /// <summary>The font drawn in its own colours, or null for the white font in the labels' colour.</summary>
    std::shared_ptr<MCGuiFont> _Font;
};
