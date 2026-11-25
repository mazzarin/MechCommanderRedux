#pragma once

#include "lib/MCOptionalRef.h"

namespace Rml
{
    class Context;
    class Element;
    class ElementDocument;
    class Event;
}

/// <summary>
/// Port-only (SPEC-menus.md, "The look" and "Without a mouse"): the controls' behaviour, for every remade screen, over
/// the look's one style sheet (<c>ui/look.rcss</c>, which says each control's markup). Attached to a document, it
/// listens to its controls: buttons, list rows, tabs, switches, steppers, sliders (RmlUi's range input), text boxes,
/// windows, scrolled lists and texts, the hint line, each with 1998's interface sound. It draws the keys' focus as one ring over
/// the edge of what has it, so nothing moves, shown after a key and gone when the mouse moves.
/// </summary>
/// <remarks>
/// A control tells the screen what changed by RmlUi's <c>change</c> event on it, its parameter <c>value</c> the new
/// choice (a tab's, a switch's, a stepper's index) or level (a slider's, RmlUi's own).
/// </remarks>
class MCRmlKit
{
public:
    /// <summary>
    /// 1998's interface sounds (MCSoundSystem::PlayInterfaceSample), as the look's Sound names them. The button press
    /// is also a switch's or a stepper's change, either way, as 1998's toggles, spinners and option lists played it
    /// (MCSessionScreen's tech base buttons and spinners, MCLogOptionList's change).
    /// </summary>
    static constexpr uint32_t PressSound = 0x0f;
    static constexpr uint32_t TabSound = 0x36;
    /// <summary>A click on something off (a disabled button).</summary>
    static constexpr uint32_t OffClickSound = 0x33;
    /// <summary>A refusal (<see cref="Refuse"/>).</summary>
    static constexpr uint32_t RefuseSound = 0x46;

    /// <summary>What plays a sound (the game's: MCSoundSystem; a test's records them).</summary>
    using MCSound = std::function<void(uint32_t sample)>;

    explicit MCRmlKit(MCSound sound);
    ~MCRmlKit();
    MCRmlKit(const MCRmlKit&) = delete;
    MCRmlKit& operator=(const MCRmlKit&) = delete;

    /// <summary>Listens to <paramref name="document"/>'s controls, and sets its steppers' words and its ring.</summary>
    void Attach(Rml::ElementDocument& document);
    /// <summary>Lets go of <paramref name="document"/> (before it closes).</summary>
    void Detach(Rml::ElementDocument& document);

    /// <summary>Q and E: <paramref name="document"/>'s tabs moved by <paramref name="step"/>; whether it has any.</summary>
    bool StepTab(Rml::ElementDocument& document, int step);

    /// <summary>
    /// The frame: each attached document's focus ring, its lists' fades, its texts' reach by the keys and its hint line
    /// kept up.
    /// </summary>
    void Update(Rml::Context& context);

    /// <summary>The mouse moved: the focus's ring goes until the next key (RmlUi's <c>:focus-visible</c>).</summary>
    void MouseMoved(Rml::Context& context);

    /// <summary>The refusal's sound (a drop that doesn't fit, a part that can't go there).</summary>
    void Refuse();

    /// <summary>A switch's, stepper's or tab row's choice (its index; -1 for none or another element).</summary>
    static int Choice(Rml::Element& control);
    /// <summary>Sets <paramref name="control"/>'s choice, as the player would, without its sound.</summary>
    static void SetChoice(Rml::Element& control, int choice);

    /// <summary>Port-only, for the tests: from now on the sounds played are kept (<see cref="Played"/>); off, none.</summary>
    void KeepPlayed(bool keep) { _KeepPlayed = keep; }
    /// <summary>Port-only, for the tests: the sounds played while <see cref="KeepPlayed"/> was on.</summary>
    const std::vector<uint32_t>& Played() const { return _Played; }

    /// <summary>The kit's listener on a document (an Rml::EventListener).</summary>
    class MCListener;

private:
    friend class MCListener;

    void Play(uint32_t sample);
    /// <summary>A click or a press in an attached document, on its way down; a keydown, on its way up.</summary>
    void OnClick(Rml::Event& event);
    void OnKeyDown(Rml::Event& event);
    void OnMouseDown(Rml::Event& event);
    /// <summary>Moves a switch's or stepper's choice by <paramref name="step"/>, with its sound; whether it moved.</summary>
    bool Step(Rml::Element& control, int step);
    /// <summary>Makes <paramref name="choice"/> the choice of <paramref name="control"/>, its look and its change event.</summary>
    void Choose(Rml::Element& control, int choice, bool tell);

    MCSound _Sound;
    /// <summary>Port-only, for the tests (<see cref="KeepPlayed"/>): off in the game, so the list doesn't grow.</summary>
    bool _KeepPlayed = false;
    std::vector<uint32_t> _Played;
    std::unique_ptr<MCListener> _Listener;
    /// <summary>The documents attached (RmlUi's; each detached before it closes).</summary>
    std::vector<Rml::ElementDocument*> _Documents;
};
