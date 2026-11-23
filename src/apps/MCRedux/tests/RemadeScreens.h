#pragma once

#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlScreen.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

/// <summary>
/// Port-only (SPEC-menus.md): what the game tests share of the remade screens: the screen on top of a layer of the
/// game's menus' RmlUi, its elements and their words, and a press of one.
/// </summary>
namespace MCRemadeTest
{
    /// <summary>The menus' layer, or the mission's (over the battlefield).</summary>
    enum class MCLayer : uint8_t
    {
        Menus,
        Mission,
    };

    /// <summary>The screen on top of <paramref name="layer"/> as a <typeparamref name="T"/>; null when another shows, or none.</summary>
    template <typename T> std::shared_ptr<T> OnTop(MCLayer layer = MCLayer::Menus)
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();

        if (!menus.has_value())
        {
            return nullptr;
        }

        const MCRmlLayer& shown = layer == MCLayer::Menus ? menus->get().Menus() : menus->get().Mission();
        const MCOptionalRef<MCRmlScreen> top = shown.Top();
        return top.has_value() ? std::dynamic_pointer_cast<T>(top->get().shared_from_this()) : nullptr;
    }

    /// <summary>The element <paramref name="id"/> of <paramref name="screen"/>'s document; null without one.</summary>
    inline Rml::Element* Find(const MCRmlScreen& screen, const std::string& id)
    {
        const MCOptionalRef<Rml::ElementDocument> document = screen.Document();
        return document.has_value() ? document->get().GetElementById(id) : nullptr;
    }

    /// <summary>The words element <paramref name="id"/> shows (its markup decoded); empty without one.</summary>
    inline std::string Words(const MCRmlScreen& screen, const std::string& id)
    {
        const Rml::Element* element = Find(screen, id);
        return element != nullptr ? Rml::StringUtilities::DecodeRml(element->GetInnerRML()) : std::string();
    }

    /// <summary>Whether element <paramref name="id"/> shows (it and every element round it).</summary>
    inline bool Visible(const MCRmlScreen& screen, const std::string& id)
    {
        Rml::Element* element = Find(screen, id);
        return element != nullptr && element->IsVisible(true);
    }

    /// <summary>Element <paramref name="id"/> pressed as a click on it does; false when it isn't shown.</summary>
    inline bool Press(const MCRmlScreen& screen, const std::string& id)
    {
        Rml::Element* element = Find(screen, id);

        if (element == nullptr || !element->IsVisible(true))
        {
            return false;
        }

        element->Click();
        return true;
    }
}
