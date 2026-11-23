#include "stdafx.h"
#include "ui/MCRmlKit.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/StringUtilities.h>

namespace
{
    // The look's classes for the controls (ui/look.rcss says each one's markup).
    constexpr const char* ButtonClass = "button";
    constexpr const char* MainClass = "main";
    constexpr const char* OffClass = "off";
    constexpr const char* TabsClass = "tabs";
    constexpr const char* TabClass = "tab";
    constexpr const char* NowClass = "now";
    constexpr const char* SwitchClass = "switch";
    /// <summary>A switch Q and E step as tabs (a screen's groups: LOAD GAME's campaigns).</summary>
    constexpr const char* TabsSwitchAttribute = "data-tabs";
    constexpr const char* OnClass = "on";
    constexpr const char* StepperClass = "stepper";
    constexpr const char* ArrowClass = "arrow";
    constexpr const char* StepValueClass = "step-value";
    constexpr const char* RowClass = "list-row";
    constexpr const char* PickedClass = "picked";
    constexpr const char* RingClass = "focus-ring";
    constexpr const char* FadeClass = "fade";
    /// <summary>A list's room (look.rcss: its rows scroll in its scroller), the list's head above it, and the class that leaves
    /// the head the rail's room while the rail shows.</summary>
    constexpr const char* ScrollBoxClass = "scroll-box";
    constexpr const char* ListHeadClass = "list-head";
    constexpr const char* RailRoomClass = "rail-room";
    constexpr const char* HintsClass = "mouse-hints";
    /// <summary>The hint a hint line shows now.</summary>
    constexpr const char* ShownHintAttribute = "data-shown-hint";
    constexpr const char* FocusVisible = "focus-visible";
    /// <summary>A stepper's words, apart by this, and the one shown (its index).</summary>
    constexpr const char* ChoicesAttribute = "data-choices";
    constexpr const char* ChoiceAttribute = "data-choice";
    constexpr char ChoicesSeparator = '|';
    /// <summary>What a click on an element does with the mouse's left button, for the hint line.</summary>
    constexpr const char* HintAttribute = "data-hint";
    /// <summary>The change event's parameter (RmlUi's own for its form controls).</summary>
    constexpr const char* ValueParameter = "value";
    /// <summary>How far a scrolled list may be from its foot and still be at it (RmlUi's layout is floats).</summary>
    constexpr float ScrollSlack = 0.5f;

    /// <summary>The nearest of <paramref name="element"/> and its ancestors with class <paramref name="name"/>.</summary>
    Rml::Element* Up(Rml::Element* element, const char* name)
    {
        for (; element != nullptr; element = element->GetParentNode())
        {
            if (element->IsClassSet(name))
            {
                return element;
            }
        }

        return nullptr;
    }

    /// <summary>The index of <paramref name="child"/> among its parent's children; -1 with none.</summary>
    int IndexOf(Rml::Element& child)
    {
        Rml::Element* parent = child.GetParentNode();

        for (int i = 0; parent != nullptr && i < parent->GetNumChildren(); ++i)
        {
            if (parent->GetChild(i) == &child)
            {
                return i;
            }
        }

        return -1;
    }

    /// <summary>A stepper's words.</summary>
    std::vector<std::string> ChoicesOf(Rml::Element& stepper)
    {
        Rml::StringList words;
        Rml::StringUtilities::ExpandString(words, stepper.GetAttribute<Rml::String>(ChoicesAttribute, ""),
                                           ChoicesSeparator);
        return {words.begin(), words.end()};
    }

    /// <summary>How many choices <paramref name="control"/> has.</summary>
    int ChoiceCount(Rml::Element& control)
    {
        return control.IsClassSet(StepperClass) ? static_cast<int>(ChoicesOf(control).size())
                                                : control.GetNumChildren();
    }

    /// <summary>The class a tab row's or a switch's chosen child has.</summary>
    const char* ChosenClass(Rml::Element& control)
    {
        return control.IsClassSet(TabsClass) ? NowClass : OnClass;
    }

    /// <summary>The ring <paramref name="document"/> has (the kit's last child of it).</summary>
    Rml::Element* RingOf(Rml::ElementDocument& document)
    {
        return document.QuerySelector(std::string(".") + RingClass);
    }

    /// <summary>Sets a length property in window pixels, only when it changes (a change lays the document out again).</summary>
    // Each set only when it changes: a property set dirties the element's layout, and its draws are made again.
    void SetPixels(Rml::Element& element, Rml::PropertyId id, float value)
    {
        const Rml::Property* now = element.GetLocalProperty(id);

        if (now == nullptr || now->unit != Rml::Unit::PX || now->Get<float>() != value)
        {
            element.SetProperty(id, Rml::Property(value, Rml::Unit::PX));
        }
    }

    void SetShown(Rml::Element& element, bool shown)
    {
        const Rml::Style::Display display = shown ? Rml::Style::Display::Block : Rml::Style::Display::None;
        const Rml::Property* now = element.GetLocalProperty(Rml::PropertyId::Display);

        if (now == nullptr || now->unit != Rml::Unit::KEYWORD ||
            static_cast<Rml::Style::Display>(now->Get<int>()) != display)
        {
            element.SetProperty(Rml::PropertyId::Display, Rml::Property(display));
        }
    }
}

class MCRmlKit::MCListener final : public Rml::EventListener
{
public:
    explicit MCListener(MCRmlKit& kit) : _Kit(kit) {}

    void ProcessEvent(Rml::Event& event) override
    {
        if (event == Rml::EventId::Click)
        {
            _Kit.OnClick(event);
        }
        else if (event == Rml::EventId::Keydown)
        {
            _Kit.OnKeyDown(event);
        }
    }

private:
    MCRmlKit& _Kit;
};

MCRmlKit::MCRmlKit(MCSound sound) : _Sound(std::move(sound)), _Listener(std::make_unique<MCListener>(*this))
{
}

MCRmlKit::~MCRmlKit() = default;

void MCRmlKit::Play(uint32_t sample)
{
    _Played.push_back(sample);

    if (_Sound)
    {
        _Sound(sample);
    }
}

void MCRmlKit::Refuse()
{
    Play(RefuseSound);
}

void MCRmlKit::Attach(Rml::ElementDocument& document)
{
    if (std::ranges::find(_Documents, &document) != _Documents.end())
    {
        return;
    }

    _Documents.push_back(&document);
    // Clicks first, on their way down (a disabled button's goes no further); keys on their way up, before the
    // document's own moves (Tab, the arrows' nav, Enter's click), which a control's keys stop.
    document.AddEventListener(Rml::EventId::Click, _Listener.get(), true);
    document.AddEventListener(Rml::EventId::Keydown, _Listener.get(), false);

    // The steppers' words and arrows, as their choices stand.
    Rml::ElementList steppers;
    document.GetElementsByClassName(steppers, StepperClass);

    for (Rml::Element* stepper : steppers)
    {
        Choose(*stepper, Choice(*stepper), false);
    }

    // One ring over whatever has the keys' focus, hidden until a key moves it there.
    if (RingOf(document) == nullptr)
    {
        Rml::ElementPtr ring = document.CreateElement("div");
        ring->SetClass(RingClass, true);
        ring->SetProperty("display", "none");
        document.AppendChild(std::move(ring));
    }
}

void MCRmlKit::Detach(Rml::ElementDocument& document)
{
    const auto found = std::ranges::find(_Documents, &document);

    if (found == _Documents.end())
    {
        return;
    }

    document.RemoveEventListener(Rml::EventId::Click, _Listener.get(), true);
    document.RemoveEventListener(Rml::EventId::Keydown, _Listener.get(), false);
    _Documents.erase(found);
}

int MCRmlKit::Choice(Rml::Element& control)
{
    if (control.IsClassSet(StepperClass))
    {
        const int count = ChoiceCount(control);
        return count > 0 ? std::clamp(control.GetAttribute<int>(ChoiceAttribute, 0), 0, count - 1) : -1;
    }

    if (!control.IsClassSet(TabsClass) && !control.IsClassSet(SwitchClass))
    {
        return -1;
    }

    const char* chosen = ChosenClass(control);

    for (int i = 0; i < control.GetNumChildren(); ++i)
    {
        if (control.GetChild(i)->IsClassSet(chosen))
        {
            return i;
        }
    }

    return -1;
}

void MCRmlKit::SetChoice(Rml::Element& control, int choice)
{
    // No sound and no change event: the screen set it.
    if (control.IsClassSet(StepperClass) || control.IsClassSet(TabsClass) || control.IsClassSet(SwitchClass))
    {
        const int count = ChoiceCount(control);

        if (count > 0)
        {
            choice = std::clamp(choice, 0, count - 1);

            if (control.IsClassSet(StepperClass))
            {
                control.SetAttribute(ChoiceAttribute, choice);
                const std::vector<std::string> words = ChoicesOf(control);
                Rml::ElementList parts;
                control.GetElementsByClassName(parts, StepValueClass);

                for (Rml::Element* part : parts)
                {
                    part->SetInnerRML(words[static_cast<size_t>(choice)]);
                }

                Rml::ElementList arrows;
                control.GetElementsByClassName(arrows, ArrowClass);

                for (size_t i = 0; i < arrows.size(); ++i)
                {
                    // The first arrow steps back, the last on: each dim at its end.
                    const bool back = i == 0;
                    arrows[i]->SetClass(OffClass, back ? choice == 0 : choice == count - 1);
                }

                return;
            }

            const char* chosen = ChosenClass(control);

            for (int i = 0; i < count; ++i)
            {
                control.GetChild(i)->SetClass(chosen, i == choice);
            }
        }
    }
}

void MCRmlKit::Choose(Rml::Element& control, int choice, bool tell)
{
    SetChoice(control, choice);

    if (tell)
    {
        Rml::Dictionary parameters;
        parameters[ValueParameter] = Choice(control);
        control.DispatchEvent(Rml::EventId::Change, parameters);
    }
}

bool MCRmlKit::Step(Rml::Element& control, int step)
{
    const int count = ChoiceCount(control);
    const int now = Choice(control);
    const int next = std::clamp(now + step, 0, std::max(count - 1, 0));

    if (count == 0 || next == now)
    {
        // At the end already: nothing moves (a stepper's arrow is dim there).
        return false;
    }

    Play(step > 0 ? OnSound : OffSound);
    Choose(control, next, true);
    return true;
}

bool MCRmlKit::StepTab(Rml::ElementDocument& document, int step)
{
    Rml::ElementList rows;
    document.GetElementsByClassName(rows, TabsClass);

    // The screen's first shown tab row is the one Q and E change.
    for (Rml::Element* tabs : rows)
    {
        if (!tabs->IsVisible(true) || tabs->GetNumChildren() == 0)
        {
            continue;
        }

        const int count = tabs->GetNumChildren();
        const int next = (std::max(Choice(*tabs), 0) + step + count) % count;
        Play(TabSound);
        Choose(*tabs, next, true);
        return true;
    }

    // Port-only (stage 5): a screen without tabs whose groups are a switch's (LOAD GAME's ALL and campaigns, marked
    // data-tabs) has Q and E step it, as tabs, round from its last to its first.
    Rml::ElementList switches;
    document.QuerySelectorAll(switches, std::format(".{}[{}]", SwitchClass, TabsSwitchAttribute));

    for (Rml::Element* control : switches)
    {
        if (!control->IsVisible(true) || control->GetNumChildren() == 0)
        {
            continue;
        }

        const int count = control->GetNumChildren();
        Play(TabSound);
        Choose(*control, (std::max(Choice(*control), 0) + step + count) % count, true);
        return true;
    }

    return false;
}

void MCRmlKit::OnClick(Rml::Event& event)
{
    Rml::Element* target = event.GetTargetElement();

    // Something off: its own sound, and the click goes no further (its screen never sees it).
    if (Rml::Element* off = Up(target, OffClass); off != nullptr && Up(off, ButtonClass) == off)
    {
        Play(OffClickSound);
        event.StopPropagation();
        return;
    }

    if (Rml::Element* tab = Up(target, TabClass);
        tab != nullptr && tab->GetParentNode() != nullptr && tab->GetParentNode()->IsClassSet(TabsClass))
    {
        Rml::Element& tabs = *tab->GetParentNode();

        if (IndexOf(*tab) != Choice(tabs))
        {
            Play(TabSound);
            Choose(tabs, IndexOf(*tab), true);
        }

        return;
    }

    if (Rml::Element* arrow = Up(target, ArrowClass); arrow != nullptr)
    {
        if (Rml::Element* stepper = Up(arrow, StepperClass); stepper != nullptr)
        {
            Rml::ElementList arrows;
            stepper->GetElementsByClassName(arrows, ArrowClass);
            const int step = !arrows.empty() && arrows.front() == arrow ? -1 : 1;

            if (!Step(*stepper, step))
            {
                Play(OffClickSound);
            }
        }

        return;
    }

    if (Rml::Element* control = Up(target, SwitchClass); control != nullptr)
    {
        for (Rml::Element* choice = target; choice != nullptr && choice != control; choice = choice->GetParentNode())
        {
            if (choice->GetParentNode() == control)
            {
                Step(*control, IndexOf(*choice) - Choice(*control));
                break;
            }
        }

        return;
    }

    if (Rml::Element* row = Up(target, RowClass); row != nullptr && !row->IsClassSet(OffClass))
    {
        // A list's rows: the one clicked picked, the rest not.
        if (Rml::Element* list = row->GetParentNode(); list != nullptr)
        {
            for (int i = 0; i < list->GetNumChildren(); ++i)
            {
                Rml::Element* other = list->GetChild(i);
                other->SetClass(PickedClass, other == row);
            }
        }

        return;
    }

    if (Up(target, ButtonClass) != nullptr)
    {
        Play(PressSound);
    }
}

void MCRmlKit::OnKeyDown(Rml::Event& event)
{
    const auto key = static_cast<Rml::Input::KeyIdentifier>(event.GetParameter<int>("key_identifier", 0));
    Rml::Element* target = event.GetTargetElement();

    if (target == nullptr)
    {
        return;
    }

    // A switch or stepper with the focus takes Left and Right.
    if ((key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) &&
        (target->IsClassSet(SwitchClass) || target->IsClassSet(StepperClass)))
    {
        if (!Step(*target, key == Rml::Input::KI_LEFT ? -1 : 1))
        {
            Play(OffClickSound);
        }

        event.StopPropagation();
        return;
    }

    if (key != Rml::Input::KI_RETURN && key != Rml::Input::KI_NUMPADENTER)
    {
        return;
    }

    // Enter on a list's row: picked, and the screen's main action pressed (JOIN GAME, LOAD).
    if (target->IsClassSet(RowClass))
    {
        target->Click();

        if (Rml::ElementDocument* document = target->GetOwnerDocument(); document != nullptr)
        {
            if (Rml::Element* main = document->QuerySelector(std::string(".") + ButtonClass + "." + MainClass);
                main != nullptr && !main->IsClassSet(OffClass))
            {
                main->Click();
            }
        }

        event.StopPropagation();
        return;
    }
}

void MCRmlKit::MouseMoved(Rml::Context& context)
{
    if (Rml::Element* focus = context.GetFocusElement(); focus != nullptr && focus->IsPseudoClassSet(FocusVisible))
    {
        focus->SetPseudoClass(FocusVisible, false);
    }
}

void MCRmlKit::Update(Rml::Context& context)
{
    Rml::Element* focus = context.GetFocusElement();
    Rml::Element* hover = context.GetHoverElement();

    for (Rml::ElementDocument* document : _Documents)
    {
        // The context's own documents: the other layer's are kept up by its own update, with its own hover and focus.
        if (document->GetContext() != &context)
        {
            continue;
        }

        // The ring over the edge of what has the keys' focus, in the document's own pixels.
        if (Rml::Element* ring = RingOf(*document); ring != nullptr)
        {
            const bool ringed = focus != nullptr && focus != document && focus->GetOwnerDocument() == document &&
                                focus->IsPseudoClassSet(FocusVisible) && focus->IsVisible(true);

            if (ringed)
            {
                const Rml::Vector2f at =
                    focus->GetAbsoluteOffset(Rml::BoxArea::Border) - document->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::Vector2f size = focus->GetBox().GetSize(Rml::BoxArea::Border);
                SetPixels(*ring, Rml::PropertyId::Left, at.x);
                SetPixels(*ring, Rml::PropertyId::Top, at.y);
                SetPixels(*ring, Rml::PropertyId::Width, size.x);
                SetPixels(*ring, Rml::PropertyId::Height, size.y);
            }

            SetShown(*ring, ringed);
        }

        // A scrolled list's fade, only while more of it lies below (the fade follows the list it fades).
        Rml::ElementList fades;
        document->GetElementsByClassName(fades, FadeClass);

        for (Rml::Element* fade : fades)
        {
            Rml::Element* list = fade->GetPreviousSibling();
            const bool more = list != nullptr &&
                              list->GetScrollTop() + list->GetClientHeight() < list->GetScrollHeight() - ScrollSlack;
            SetShown(*fade, more);
        }

        // A list's head as wide as its rows: the rail's room left at its right while the rail shows.
        Rml::ElementList boxes;
        document->GetElementsByClassName(boxes, ScrollBoxClass);

        for (Rml::Element* box : boxes)
        {
            Rml::Element* head = box->GetPreviousSibling();
            Rml::Element* list = box->GetNumChildren() > 0 ? box->GetChild(0) : nullptr;

            if (head != nullptr && list != nullptr && head->IsClassSet(ListHeadClass))
            {
                head->SetClass(RailRoomClass, list->GetScrollHeight() > list->GetClientHeight() + ScrollSlack);
            }
        }

        // The hint line's left: what the mouse's left button does with the thing under the pointer.
        Rml::ElementList hints;
        document->GetElementsByClassName(hints, HintsClass);
        std::string hint;

        for (Rml::Element* under = hover; under != nullptr && under->GetOwnerDocument() == document && hint.empty();
             under = under->GetParentNode())
        {
            hint = under->GetAttribute<Rml::String>(HintAttribute, "");
        }

        const std::string rml =
            hint.empty()
                ? std::string()
                : std::format("<span class=\"mouse left\"><span class=\"buttons\"></span><span class=\"split\">"
                              "</span><span class=\"press\"></span></span>{}",
                              hint);

        for (Rml::Element* line : hints)
        {
            // The hint it shows kept on it: its RML read back isn't what was set.
            if (line->GetAttribute<Rml::String>(ShownHintAttribute, "") != hint)
            {
                line->SetAttribute(ShownHintAttribute, hint);
                line->SetInnerRML(rml);
            }
        }
    }
}
