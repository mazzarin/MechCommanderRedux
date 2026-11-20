#include "stdafx.h"
#include "ui/MCRmlFrame.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/Spritesheet.h>

namespace
{
    /// <summary>The pieces, in the order the decorator names them.</summary>
    enum MCPiece : size_t
    {
        TopLeft,
        Top,
        TopRight,
        Left,
        Center,
        Right,
        BottomLeft,
        Bottom,
        BottomRight,
        Pieces,
    };

    constexpr std::array<const char*, Pieces> PieceNames = {
        "top-left", "top", "top-right", "left", "center", "right", "bottom-left", "bottom", "bottom-right",
    };

    /// <summary>One copy of a piece along an axis: where it starts and its length in pixels, and the share of the piece it shows.</summary>
    struct MCCopy
    {
        float At = 0.0f;
        float Length = 0.0f;
        float Share = 1.0f;
    };

    /// <summary>
    /// The copies of a piece <paramref name="length"/> pixels long laid from <paramref name="from"/> to
    /// <paramref name="to"/>: each starts where its place falls, rounded to a whole pixel; the last one cut at the end.
    /// </summary>
    std::vector<MCCopy> Copies(float from, float to, float length)
    {
        std::vector<MCCopy> copies;
        // Below a pixel a piece would take a copy per pixel: none is drawn so small.
        constexpr float Least = 1.0f;

        if (length < Least)
        {
            return copies;
        }

        for (int32_t copy = 0;; ++copy)
        {
            const float start = from + std::round(static_cast<float>(copy) * length);
            const float whole = from + std::round(static_cast<float>(copy + 1) * length);

            if (start >= to)
            {
                break;
            }

            const float end = std::min(whole, to);

            if (whole > start && end > start)
            {
                copies.push_back(MCCopy{.At = start, .Length = end - start, .Share = (end - start) / (whole - start)});
            }
        }

        return copies;
    }

    /// <summary>The decorator: the nine sprites' rectangles in the picture (its 1998 pixels) and the picture.</summary>
    class MCRmlFrame final : public Rml::Decorator
    {
    public:
        MCRmlFrame(const std::array<Rml::Rectanglef, Pieces>& sprites, Rml::Texture texture, float displayScale)
            : _Sprites(sprites), _DisplayScale(displayScale)
        {
            AddTexture(texture);
        }

        Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea paintArea) const override
        {
            const Rml::Style::ComputedValues& computed = element->GetComputedValues();
            const Rml::Vector2f picture(GetTexture().GetDimensions());
            const Rml::RenderBox box = element->GetRenderBox(paintArea);
            const Rml::Vector2f offset = box.GetFillOffset().Round();
            const Rml::Vector2f size = box.GetFillSize().Round();
            const Rml::ColourbPremultiplied colour = computed.image_color().ToPremultiplied(computed.opacity());
            // A 1998 pixel's size in window pixels.
            const float scale = Rml::ElementUtilities::GetDensityIndependentPixelRatio(element) * _DisplayScale;
            const auto width = [&](MCPiece piece) { return std::round(_Sprites[piece].Width() * scale); };
            const auto height = [&](MCPiece piece) { return std::round(_Sprites[piece].Height() * scale); };

            Rml::Mesh mesh;

            // The piece repeated over the area from (left, top) to (right, bottom).
            const auto lay = [&](MCPiece piece, float left, float top, float right, float bottom)
            {
                const Rml::Rectanglef& sprite = _Sprites[piece];
                const std::vector<MCCopy> columns = Copies(left, right, sprite.Width() * scale);
                const std::vector<MCCopy> rows = Copies(top, bottom, sprite.Height() * scale);

                for (const MCCopy& row : rows)
                {
                    for (const MCCopy& column : columns)
                    {
                        const Rml::Vector2f shown(sprite.Width() * column.Share, sprite.Height() * row.Share);
                        Rml::MeshUtilities::GenerateQuad(
                            mesh, offset + Rml::Vector2f(column.At, row.At), Rml::Vector2f(column.Length, row.Length),
                            colour, sprite.TopLeft() / picture, (sprite.TopLeft() + shown) / picture);
                    }
                }
            };

            const float right = size.x;
            const float bottom = size.y;
            lay(TopLeft, 0.0f, 0.0f, width(TopLeft), height(TopLeft));
            lay(TopRight, right - width(TopRight), 0.0f, right, height(TopRight));
            lay(BottomLeft, 0.0f, bottom - height(BottomLeft), width(BottomLeft), bottom);
            lay(BottomRight, right - width(BottomRight), bottom - height(BottomRight), right, bottom);
            lay(Top, width(TopLeft), 0.0f, right - width(TopRight), height(Top));
            lay(Bottom, width(BottomLeft), bottom - height(Bottom), right - width(BottomRight), bottom);
            lay(Left, 0.0f, height(TopLeft), width(Left), bottom - height(BottomLeft));
            lay(Right, right - width(Right), height(TopRight), right, bottom - height(BottomRight));
            lay(Center, width(Left), height(Top), right - width(Right), bottom - height(Bottom));

            // RmlUi's handle is a number: the geometry is ours until ReleaseElementData.
            auto* geometry = new Rml::Geometry(element->GetRenderManager()->MakeGeometry(std::move(mesh)));
            return reinterpret_cast<Rml::DecoratorDataHandle>(geometry);
        }

        void ReleaseElementData(Rml::DecoratorDataHandle elementData) const override
        {
            delete reinterpret_cast<Rml::Geometry*>(elementData);
        }

        void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle elementData) const override
        {
            reinterpret_cast<Rml::Geometry*>(elementData)
                ->Render(element->GetAbsoluteOffset(Rml::BoxArea::Border), GetTexture());
        }

    private:
        std::array<Rml::Rectanglef, Pieces> _Sprites;
        float _DisplayScale = 1.0f;
    };
}

MCRmlFrameInstancer::MCRmlFrameInstancer()
{
    std::string names;

    for (size_t piece = 0; piece < Pieces; ++piece)
    {
        _Pieces[piece] = RegisterProperty(PieceNames[piece], "").AddParser("string").GetId();
        names += std::format("{}{}", piece == 0 ? "" : ", ", PieceNames[piece]);
    }

    RegisterShorthand("decorator", names, Rml::ShorthandType::RecursiveCommaSeparated);
}

Rml::SharedPtr<Rml::Decorator> MCRmlFrameInstancer::InstanceDecorator(
    const Rml::String& /*name*/, const Rml::PropertyDictionary& properties,
    const Rml::DecoratorInstancerInterface& instancerInterface)
{
    std::array<Rml::Rectanglef, Pieces> sprites{};
    const Rml::Spritesheet* sheet = nullptr;

    for (size_t piece = 0; piece < Pieces; ++piece)
    {
        const Rml::Property* property = properties.GetProperty(_Pieces[piece]);
        const Rml::String spriteName = property != nullptr ? property->Get<Rml::String>() : Rml::String();
        const Rml::Sprite* sprite = instancerInterface.GetSprite(spriteName);

        if (sprite == nullptr)
        {
            Rml::Log::Message(Rml::Log::LT_WARNING, "%s: no sprite '%s' for its %s piece", Name, spriteName.c_str(),
                              PieceNames[piece]);
            return nullptr;
        }

        if (sheet != nullptr && sprite->sprite_sheet != sheet)
        {
            Rml::Log::Message(Rml::Log::LT_WARNING, "%s: sprite '%s' isn't on the other pieces' sprite sheet", Name,
                              spriteName.c_str());
            return nullptr;
        }

        sheet = sprite->sprite_sheet;
        sprites[piece] = sprite->rectangle;
    }

    const Rml::Texture texture = sheet->texture_source.GetTexture(instancerInterface.GetRenderManager());

    if (!texture)
    {
        Rml::Log::Message(Rml::Log::LT_WARNING, "%s: sprite sheet '%s' has no picture", Name, sheet->name.c_str());
        return nullptr;
    }

    return Rml::MakeShared<MCRmlFrame>(sprites, texture, sheet->display_scale);
}
