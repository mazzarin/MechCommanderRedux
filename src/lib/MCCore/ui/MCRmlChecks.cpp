#include "stdafx.h"
#include "ui/MCRmlChecks.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/FontEngineInterface.h>

namespace
{
    /// <summary>How far a box may lie past an edge and still be within it (RmlUi's layout is floats).</summary>
    constexpr float Slack = 0.5f;
    /// <summary>
    /// How far past its box's width a wrapped line can run: RmlUi wraps to the width rounded up to a whole pixel
    /// (LineBox::AddBox), so it lies within a box that keeps a pixel of padding at its right.
    /// </summary>
    constexpr float WrapRounding = 1.0f;
    /// <summary>A background at least this opaque is the ground a text is read against (the well at 85%).</summary>
    constexpr uint8_t GroundAlpha = 217;
    /// <summary>A disabled thing's class: its words need only the large contrast.</summary>
    constexpr const char* OffClass = "off";
    /// <summary>A decorator that is a flat colour or a fade, not a picture, under text.</summary>
    constexpr const char* GroundOkClass = "ground-ok";
    /// <summary>A row its design cuts at its box's edge (a map description's banner row longer than the line).</summary>
    constexpr const char* CutClass = "cut";
    constexpr float ByteMax = 255.0f;

    struct MCArea
    {
        float Left = 0.0f;
        float Top = 0.0f;
        float Right = 0.0f;
        float Bottom = 0.0f;

        bool Empty() const { return Right - Left <= Slack || Bottom - Top <= Slack; }
        bool Within(const MCArea& outer) const
        {
            return Left >= outer.Left - Slack && Top >= outer.Top - Slack && Right <= outer.Right + Slack &&
                   Bottom <= outer.Bottom + Slack;
        }

        bool Overlaps(const MCArea& other) const
        {
            return Left < other.Right - Slack && other.Left < Right - Slack && Top < other.Bottom - Slack &&
                   other.Top < Bottom - Slack;
        }

        MCArea Meet(const MCArea& other) const
        {
            return MCArea{std::max(Left, other.Left), std::max(Top, other.Top), std::min(Right, other.Right),
                          std::min(Bottom, other.Bottom)};
        }
    };

    MCArea AreaOf(Rml::Element& element, Rml::BoxArea box)
    {
        const Rml::Vector2f at = element.GetAbsoluteOffset(box);
        const Rml::Vector2f size = element.GetBox().GetSize(box);
        return MCArea{at.x, at.y, at.x + size.x, at.y + size.y};
    }

    /// <summary>An element as a selector names it: its tag, its classes, its id.</summary>
    std::string NameOf(Rml::Element& element)
    {
        std::string classes = element.GetClassNames();
        std::ranges::replace(classes, ' ', '.');
        const std::string id = element.GetId();
        return std::format("{}{}{}{}{}", element.GetTagName(), classes.empty() ? "" : ".", classes,
                           id.empty() ? "" : "#", id);
    }

    /// <summary>Whether <paramref name="element"/> clips what it holds (it scrolls or hides what overflows).</summary>
    bool Clips(Rml::Element& element)
    {
        const Rml::Style::ComputedValues& values = element.GetComputedValues();
        return values.overflow_x() != Rml::Style::Overflow::Visible ||
               values.overflow_y() != Rml::Style::Overflow::Visible;
    }

    /// <summary>Whether <paramref name="element"/> is a row cut at its edge by design (it clips its line).</summary>
    bool CutByDesign(Rml::Element& element)
    {
        return element.IsClassSet(CutClass) && Clips(element);
    }

    /// <summary>Whether <paramref name="element"/>'s words wrap to its box (its white-space breaks lines).</summary>
    bool Wraps(Rml::Element& element)
    {
        const Rml::Style::WhiteSpace space = element.GetComputedValues().white_space();
        return space == Rml::Style::WhiteSpace::Normal || space == Rml::Style::WhiteSpace::Prewrap ||
               space == Rml::Style::WhiteSpace::Preline;
    }

    /// <summary>Whether <paramref name="element"/> is placed out of the flow (an overlay, a fade, a ring).</summary>
    bool Placed(Rml::Element& element)
    {
        const Rml::Style::Position position = element.GetComputedValues().position();
        return position == Rml::Style::Position::Absolute || position == Rml::Style::Position::Fixed;
    }

    void CheckBoxes(Rml::Element& element, const MCArea& window, const MCArea& clip, std::string& wrong)
    {
        std::vector<std::pair<Rml::Element*, MCArea>> shown;
        const MCArea parent = AreaOf(element, Rml::BoxArea::Padding);
        const bool clipping = Clips(element);

        for (int i = 0; i < element.GetNumChildren(); ++i)
        {
            Rml::Element& child = *element.GetChild(i);

            if (!child.IsVisible())
            {
                continue;
            }

            if (auto* text = dynamic_cast<Rml::ElementText*>(&child); text != nullptr)
            {
                if (CutByDesign(element))
                {
                    continue;
                }

                const MCArea content = AreaOf(element, Rml::BoxArea::Content);
                // A wrapped line may take the pixel RmlUi rounds its room up by, where the box keeps it as padding.
                const float right = Wraps(element) ? std::max(content.Right + Slack,
                                                              std::min(content.Right + WrapRounding, parent.Right))
                                                   : content.Right + Slack;
                const Rml::Vector2f origin = text->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::FontMetrics& face =
                    Rml::GetFontEngineInterface()->GetFontMetrics(element.GetFontFaceHandle());

                for (const Rml::ElementText::Line& line : text->GetLines())
                {
                    const MCArea letters{origin.x + line.position.x, origin.y + line.position.y - face.ascent,
                                         origin.x + line.position.x + static_cast<float>(line.width),
                                         origin.y + line.position.y + face.descent};

                    // Across its box: a line may run under a line box's leading, never past its sides.
                    if (letters.Left < content.Left - Slack || letters.Right > right)
                    {
                        wrong += std::format("'{}' spills from {}; ", line.text, NameOf(element));
                    }
                }

                continue;
            }

            const MCArea border = AreaOf(child, Rml::BoxArea::Border);
            // Siblings are compared inside their borders: two that share a border (a switch's halves) don't overlap.
            const MCArea inside = AreaOf(child, Rml::BoxArea::Padding);

            // Scrolled out of its clip: not shown.
            if (border.Meet(clip).Empty())
            {
                continue;
            }

            if (!border.Meet(clip).Within(window))
            {
                wrong += std::format("{} lies outside the window; ", NameOf(child));
            }

            if (!clipping && !Placed(child) && !border.Empty() && !border.Within(parent))
            {
                wrong += std::format("{} spills from {}; ", NameOf(child), NameOf(element));
            }

            if (!Placed(child))
            {
                for (const auto& [other, area] : shown)
                {
                    if (!inside.Empty() && !area.Empty() && inside.Overlaps(area))
                    {
                        wrong += std::format("{} overlaps {}; ", NameOf(child), NameOf(*other));
                    }
                }

                shown.emplace_back(&child, inside);
            }

            CheckBoxes(child, window, Clips(child) ? clip.Meet(AreaOf(child, Rml::BoxArea::Padding)) : clip, wrong);
        }
    }

    float Channel(uint8_t value)
    {
        constexpr float Knee = 0.03928f;
        constexpr float Linear = 12.92f;
        constexpr float Offset = 0.055f;
        constexpr float Gamma = 2.4f;
        const float c = static_cast<float>(value) / ByteMax;
        return c <= Knee ? c / Linear : std::pow((c + Offset) / (1.0f + Offset), Gamma);
    }

    /// <summary>WCAG's relative luminance.</summary>
    float Luminance(const Rml::Colourb& colour)
    {
        constexpr float Red = 0.2126f;
        constexpr float Green = 0.7152f;
        constexpr float Blue = 0.0722f;
        return Red * Channel(colour.red) + Green * Channel(colour.green) + Blue * Channel(colour.blue);
    }

    float ContrastOf(const Rml::Colourb& first, const Rml::Colourb& second)
    {
        constexpr float Flare = 0.05f;
        const float a = Luminance(first);
        const float b = Luminance(second);
        return (std::max(a, b) + Flare) / (std::min(a, b) + Flare);
    }

    /// <summary>A colour laid over black at its alpha.</summary>
    Rml::Colourb OverBlack(const Rml::Colourb& colour)
    {
        const auto mix = [&colour](uint8_t value)
        {
            return static_cast<uint8_t>(static_cast<int>(value) * static_cast<int>(colour.alpha) /
                                        static_cast<int>(ByteMax));
        };

        return Rml::Colourb(mix(colour.red), mix(colour.green), mix(colour.blue), static_cast<uint8_t>(ByteMax));
    }

    struct MCGround
    {
        Rml::Colourb Colour{0, 0, 0, static_cast<uint8_t>(ByteMax)};
        bool OnPicture = false;
        bool Disabled = false;
    };

    /// <summary>Whether <paramref name="element"/> or anything round it is off (a held switch's segment, its row).</summary>
    bool InsideOff(Rml::Element& element)
    {
        for (Rml::Element* at = &element; at != nullptr; at = at->GetParentNode())
        {
            if (at->IsClassSet(OffClass))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>What a text sits on: the nearest element out from it with a ground; a picture found first is a picture.</summary>
    MCGround GroundOf(Rml::Element& text)
    {
        MCGround ground;
        // Off anywhere round it, not only inside its ground (a held switch's segment is its own ground).
        ground.Disabled = text.GetParentNode() != nullptr && InsideOff(*text.GetParentNode());

        for (Rml::Element* element = text.GetParentNode(); element != nullptr; element = element->GetParentNode())
        {
            const Rml::Style::ComputedValues& values = element->GetComputedValues();

            if (values.background_color().alpha >= GroundAlpha)
            {
                ground.Colour = OverBlack(values.background_color());
                return ground;
            }

            if (values.has_decorator() && !element->IsClassSet(GroundOkClass))
            {
                ground.OnPicture = true;
                return ground;
            }
        }

        return ground;
    }

    void Walk(Rml::Element& element, float unit, const MCArea& window, MCArea clip,
              std::vector<MCRmlChecks::MCText>& found)
    {
        if (!element.IsVisible())
        {
            return;
        }

        if (Clips(element))
        {
            clip = clip.Meet(AreaOf(element, Rml::BoxArea::Padding));
        }

        if (auto* text = dynamic_cast<Rml::ElementText*>(&element); text != nullptr)
        {
            Rml::Element& parent = *text->GetParentNode();
            const MCArea box = AreaOf(parent, Rml::BoxArea::Border);
            const std::string words = text->GetText();

            if (words.find_first_not_of(" \t\r\n") != std::string::npos && !box.Meet(clip).Empty())
            {
                const Rml::Style::ComputedValues& values = parent.GetComputedValues();
                MCRmlChecks::MCText shown{.Words = words};
                // RmlUi sets the face at the font size cut to whole pixels.
                shown.Units = std::floor(values.font_size()) * MCRmlChecks::FaceBody / unit;
                const MCGround ground = GroundOf(*text);
                shown.Contrast = ContrastOf(values.color(), ground.Colour);
                shown.Need = ground.Disabled || shown.Units >= MCRmlChecks::LargeUnits ? MCRmlChecks::LargeContrast
                                                                                       : MCRmlChecks::TextContrast;
                shown.OnPicture = ground.OnPicture;
                shown.Inside = box.Meet(clip).Within(window);
                const Rml::Vector2f origin = text->GetAbsoluteOffset(Rml::BoxArea::Border);

                // A row cut at its edge by design is read as far as it shows.
                for (const Rml::ElementText::Line& line : text->GetLines())
                {
                    const float left = origin.x + line.position.x;

                    if (!CutByDesign(parent) &&
                        (left < clip.Left - Slack || left + static_cast<float>(line.width) > clip.Right + Slack))
                    {
                        shown.Whole = false;
                    }
                }

                found.push_back(std::move(shown));
            }
        }

        for (int i = 0; i < element.GetNumChildren(); ++i)
        {
            Walk(*element.GetChild(i), unit, window, clip, found);
        }
    }

    /// <summary>The context's documents shown, in order.</summary>
    std::vector<Rml::ElementDocument*> ShownDocuments(Rml::Context& context)
    {
        std::vector<Rml::ElementDocument*> shown;

        for (int i = 0; i < context.GetNumDocuments(); ++i)
        {
            if (Rml::ElementDocument* document = context.GetDocument(i); document != nullptr && document->IsVisible())
            {
                shown.push_back(document);
            }
        }

        return shown;
    }

    MCArea WindowOf(Rml::Context& context)
    {
        const Rml::Vector2i size = context.GetDimensions();
        return MCArea{0.0f, 0.0f, static_cast<float>(size.x), static_cast<float>(size.y)};
    }
}

namespace MCRmlChecks
{
    bool MCText::Passes(float unit) const
    {
        return Units * unit >= LeastUnits * unit - Slack && Contrast >= Need && !OnPicture && Inside && Whole;
    }

    std::vector<MCText> Texts(Rml::Context& context, float unit)
    {
        std::vector<MCText> found;
        const MCArea window = WindowOf(context);

        for (Rml::ElementDocument* document : ShownDocuments(context))
        {
            Walk(*document, unit, window, window, found);
        }

        return found;
    }

    std::string BoxProblems(Rml::Context& context)
    {
        std::string wrong;
        const MCArea window = WindowOf(context);

        for (Rml::ElementDocument* document : ShownDocuments(context))
        {
            CheckBoxes(*document, window, window, wrong);
        }

        return wrong;
    }

    std::string Illegible(Rml::Context& context, float unit)
    {
        std::string failing;

        for (const MCText& text : Texts(context, unit))
        {
            if (!text.Passes(unit))
            {
                failing +=
                    std::format("'{}': {:.2f} units, {:.2f}:1 (needs {:.1f}){}{}{}\n", text.Words, text.Units,
                                text.Contrast, text.Need, text.OnPicture ? ", on a picture" : "",
                                text.Inside ? "" : ", outside the window", text.Whole ? "" : ", cut at the side");
            }
        }

        return failing;
    }
}
