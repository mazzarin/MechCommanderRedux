#include "stdafx.h"
#include "logistics/MCSettingsScreen.h"
#include "logistics/MCQuestionWindow.h"
#include "main/MCUpdater.h"
#include "ui/MCRmlKit.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/StringUtilities.h>

namespace
{
    using MCSettingsModel::MCControl;
    using MCSettingsModel::MCSetting;
    using Rml::StringUtilities::EncodeRml;

    /// <summary>The look's classes (ui/look.rcss, ui/prefs.rcss).</summary>
    constexpr const char* HeldClass = "held";
    constexpr const char* OffClass = "off";
    constexpr const char* HiddenClass = "hidden";

    /// <summary>A row's parts' ids: the row, its line, a slider's number.</summary>
    constexpr std::string_view RowPrefix = "row-";
    constexpr std::string_view LinePrefix = "line-";
    constexpr std::string_view LevelPrefix = "level-";

    /// <summary>The look's stepper: its choices as <c>data-choices</c> keeps them, and its arrows.</summary>
    constexpr const char* ChoicesAttribute = "data-choices";
    constexpr char ChoicesSeparator = '|';
    constexpr std::string_view BackArrow = "\xe2\x80\xb9";
    constexpr std::string_view OnArrow = "\xe2\x80\xba";

    /// <summary>A range input's level, and what holds it.</summary>
    constexpr const char* ValueAttribute = "value";
    constexpr const char* DisabledAttribute = "disabled";

    std::string PartId(std::string_view prefix, std::string_view id)
    {
        return std::string(prefix) + std::string(id);
    }

    std::vector<std::string> ChoicesOf(const MCSetting& setting)
    {
        return setting.Choices ? setting.Choices() : std::vector<std::string>{};
    }

    std::string Joined(const std::vector<std::string>& choices)
    {
        std::string joined;

        for (const std::string& choice : choices)
        {
            if (!joined.empty())
            {
                joined += ChoicesSeparator;
            }

            joined += choice;
        }

        return joined;
    }

    int Now(const MCSetting& setting)
    {
        return setting.Get ? setting.Get() : 0;
    }

    std::string LevelWords(const MCSetting& setting, int level)
    {
        return std::to_string(MCSettingsModel::LevelPercent(setting, level));
    }

    /// <summary>A setting's control, as the look marks it up (ui/look.rcss's notes); the kit writes a stepper's words.</summary>
    std::string ControlRml(const MCSetting& setting)
    {
        const int now = Now(setting);

        switch (setting.Control)
        {
            case MCControl::Switch:
            {
                const std::vector<std::string> choices = ChoicesOf(setting);
                std::string rml = std::format("<div class=\"switch\" id=\"{}\">", setting.Id);

                for (size_t i = 0; i < choices.size(); ++i)
                {
                    rml += std::format("<div{}>{}</div>", std::cmp_equal(i, now) ? " class=\"on\"" : "",
                                       EncodeRml(choices[i]));
                }

                return rml + "</div>";
            }
            case MCControl::Stepper:
                return std::format("<div class=\"stepper\" id=\"{}\" {}=\"{}\" data-choice=\"{}\"><div "
                                   "class=\"arrow\">{}</div><div class=\"step-value\"></div><div "
                                   "class=\"arrow\">{}</div></div>",
                                   setting.Id, ChoicesAttribute, EncodeRml(Joined(ChoicesOf(setting))), now, BackArrow,
                                   OnArrow);
            case MCControl::Slider:
                return std::format("<input type=\"range\" class=\"slider\" id=\"{}\" min=\"0\" max=\"{}\" step=\"{}\" "
                                   "value=\"{}\"/><div class=\"level\" id=\"{}\">{}</div>",
                                   setting.Id, setting.Most, setting.Step, now, PartId(LevelPrefix, setting.Id),
                                   LevelWords(setting, now));
            case MCControl::Button:
                return std::format("<div class=\"button small\" id=\"{}\"></div>", setting.Id);
        }

        return {};
    }

    /// <summary>Sets <paramref name="element"/>'s words when they differ (RmlUi lays out again for a change).</summary>
    void SetWords(Rml::Element& element, const std::string& words)
    {
        if (element.GetInnerRML() != words)
        {
            element.SetInnerRML(words);
        }
    }

    void SetFlag(Rml::Element& element, const char* attribute, bool set)
    {
        if (set == element.HasAttribute(attribute))
        {
            return;
        }

        if (set)
        {
            element.SetAttribute(attribute, "");
        }
        else
        {
            element.RemoveAttribute(attribute);
        }
    }

    /// <summary>Whether the mouse's left button is down (a slider dragged: its sound waits for the release).</summary>
    bool Dragging()
    {
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
    }
}

std::string MCSettingsScreen::RowRml(const MCSetting& setting, bool quick)
{
    std::string name = EncodeRml(std::string(setting.Name));

    if (setting.ChangesPlay && !quick)
    {
        name += "<span class=\"tag\">Changes play</span>";
    }

    const std::string line =
        quick ? std::string() : std::format("<div class=\"set-line\" id=\"{}\"></div>", PartId(LinePrefix, setting.Id));
    return std::format("<div class=\"set-row{}\" id=\"{}\"><div class=\"set-words\"><div class=\"set-name\">{}</div>{}"
                       "</div><div class=\"set-control\">{}</div></div>",
                       quick ? " quick" : "", PartId(RowPrefix, setting.Id), name, line, ControlRml(setting));
}

void MCSettingsScreen::ListenToRow(const MCSetting& setting)
{
    const std::string id(setting.Id);
    _Rows.emplace_back(setting);

    if (setting.Control == MCControl::Button)
    {
        OnClick(id, [this, &setting] { OnRowPress(setting); });
        return;
    }

    OnChange(id, [this, &setting](int value) { OnRowChange(setting, value); });

    // The kit writes the steppers' words as a document loads; rows written after that get theirs here.
    if (const MCOptionalRef<Rml::ElementDocument> document = Document();
        setting.Control == MCControl::Stepper && document.has_value())
    {
        if (Rml::Element* stepper = document->get().GetElementById(id); stepper != nullptr)
        {
            MCRmlKit::SetChoice(*stepper, MCRmlKit::Choice(*stepper));
        }
    }

    if (setting.Control == MCControl::Slider && setting.Preview)
    {
        Listen(id, Rml::EventId::Mouseup, [this, &setting](Rml::Event&) { Preview(setting); });
    }
}

void MCSettingsScreen::OnRowChange(const MCSetting& setting, int value)
{
    // A row put right by Refresh tells nothing (the kit sets controls without a change event); a held one refuses.
    if (!setting.Set || value == Now(setting) || !MCSettingsModel::HeldNow(setting, InMission()).empty())
    {
        Refresh();
        return;
    }

    if (setting.AskFirst)
    {
        if (std::optional<MCSettingsModel::MCQuestion> question = setting.AskFirst(value); question.has_value())
        {
            // Shown as it was until the answer.
            Refresh();
            Ask(std::move(*question));
            return;
        }
    }

    setting.Set(value);
    Changed();

    if (setting.Control == MCControl::Slider && !Dragging())
    {
        Preview(setting);
    }

    // A change can hold or free others (PARTICLES and FLECKS).
    Refresh();
}

void MCSettingsScreen::OnRowPress(const MCSetting& setting)
{
    if (!MCSettingsModel::HeldNow(setting, InMission()).empty())
    {
        return;
    }

    if (setting.AskFirst)
    {
        if (std::optional<MCSettingsModel::MCQuestion> question = setting.AskFirst(-1); question.has_value())
        {
            Ask(std::move(*question));
            return;
        }
    }

    // The HD pack's RESTART quits the game: what the screen changed is kept first, but only for a restart that goes
    // ahead (a refused one leaves the screen as it was).
    if (setting.Word && setting.Word() == MCSettingsModel::RestartWord && MCUpdater::CanRestartForHd())
    {
        BeforeRestart();
    }

    if (setting.Press)
    {
        setting.Press();
    }

    Changed();
    Refresh();
}

void MCSettingsScreen::Ask(MCSettingsModel::MCQuestion question)
{
    const std::weak_ptr<MCSettingsScreen> self = std::static_pointer_cast<MCSettingsScreen>(shared_from_this());
    std::function<void()> yes = [self, act = std::move(question.Yes)]
    {
        if (act)
        {
            act();
        }

        if (const std::shared_ptr<MCSettingsScreen> screen = self.lock(); screen != nullptr)
        {
            screen->Changed();
        }
    };

    std::function<void()> closed = [self]
    {
        if (const std::shared_ptr<MCSettingsScreen> screen = self.lock(); screen != nullptr && !screen->WasDestroyed())
        {
            screen->Refresh();
        }
    };

    static_cast<void>(MCQuestionWindow::Ask(*this, std::move(question.Heading), std::move(question.Act), std::move(yes),
                                            std::move(closed)));
}

void MCSettingsScreen::Preview(const MCSetting& setting) const
{
    if (setting.Preview)
    {
        setting.Preview(InMission());
    }
}

void MCSettingsScreen::Refresh()
{
    const MCOptionalRef<Rml::ElementDocument> document = Document();

    if (!document.has_value())
    {
        return;
    }

    for (const MCSetting& setting : _Rows)
    {
        RefreshRow(document->get(), setting);
    }
}

void MCSettingsScreen::RefreshLive()
{
    const MCOptionalRef<Rml::ElementDocument> document = Document();

    if (!document.has_value())
    {
        return;
    }

    for (const MCSetting& setting : _Rows)
    {
        if (setting.Live)
        {
            RefreshRow(document->get(), setting);
        }
    }
}

void MCSettingsScreen::RefreshRow(Rml::ElementDocument& document, const MCSetting& setting) const
{
    Rml::Element* control = document.GetElementById(std::string(setting.Id));

    if (control == nullptr)
    {
        return;
    }

    const std::string held = MCSettingsModel::HeldNow(setting, InMission());
    const bool isHeld = !held.empty();

    if (Rml::Element* row = document.GetElementById(PartId(RowPrefix, setting.Id)); row != nullptr)
    {
        row->SetClass(HeldClass, isHeld);
        // Its words are a held thing's, read as an off button's are (ui/MCRmlChecks: the large contrast).
        row->SetClass(OffClass, isHeld);
    }

    if (Rml::Element* line = document.GetElementById(PartId(LinePrefix, setting.Id)); line != nullptr)
    {
        SetWords(*line, EncodeRml(isHeld ? held : setting.Line ? setting.Line() : std::string()));
    }

    const int now = Now(setting);

    switch (setting.Control)
    {
        case MCControl::Stepper:
        {
            // Its words can change (AUTO's size follows UI RESOLUTION): the kit writes them again with the choice.
            if (const std::string joined = Joined(ChoicesOf(setting));
                control->GetAttribute<Rml::String>(ChoicesAttribute, "") != joined)
            {
                control->SetAttribute(ChoicesAttribute, joined);
                MCRmlKit::SetChoice(*control, now);
            }

            [[fallthrough]];
        }
        case MCControl::Switch:
        {
            control->SetClass(OffClass, isHeld);

            if (MCRmlKit::Choice(*control) != now)
            {
                MCRmlKit::SetChoice(*control, now);
            }

            break;
        }
        case MCControl::Slider:
        {
            if (control->GetAttribute<int>(ValueAttribute, -1) != now)
            {
                control->SetAttribute(ValueAttribute, std::to_string(now));
            }

            SetFlag(*control, DisabledAttribute, isHeld);

            if (Rml::Element* level = document.GetElementById(PartId(LevelPrefix, setting.Id)); level != nullptr)
            {
                SetWords(*level, LevelWords(setting, now));
            }

            break;
        }
        case MCControl::Button:
        {
            const std::string word = setting.Word ? setting.Word() : std::string();
            control->SetClass(OffClass, isHeld);
            control->SetClass(HiddenClass, word.empty());
            SetWords(*control, EncodeRml(word));
            break;
        }
    }
}
