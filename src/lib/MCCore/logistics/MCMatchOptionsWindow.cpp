#include "stdafx.h"
#include "logistics/MCMatchOptionsWindow.h"
#include "logistics/MCMatchOptionRows.h"
#include "main/MCWeaponBans.h"
#include "network/MCMultiPlayer.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"

#include <RmlUi/Core/ElementDocument.h>
#include <charconv>

namespace
{
    constexpr const char* WindowDocument = "match_options.rml";
    constexpr const char* RowsId = "rows";
    constexpr const char* TabsId = "tabs";
    constexpr const char* DoneId = "done";
    constexpr const char* NoteId = "note";
    /// <summary>A control's id: this, then its row's index (MatchOptionRows').</summary>
    constexpr std::string_view ControlPrefix = "opt-";
    constexpr std::string_view DoneWord = "Done";
    constexpr std::string_view CloseWord = "Close";
    constexpr std::string_view HostSetsThese = "The host sets these.";
    constexpr std::string_view UpgradeFormat = "Tell {} to upgrade.";
    /// <summary>The value a switch shows dim when a guest reads it, as the frames have it.</summary>
    constexpr std::string_view OffWord = "Off";
    /// <summary>The most choices a switch shows; more go in a stepper.</summary>
    constexpr size_t MostSwitchChoices = 4;

    /// <summary>
    /// The weapon families' names and what each covers, in WeaponFamilies' order, as the frames write them (their
    /// capitals: LRMs, PPCs); a line only where the name leaves out what it covers.
    /// </summary>
    struct MCFamilyWords
    {
        std::string_view Name;
        std::string_view Line;
    };

    constexpr std::array FamilyWords = {
        MCFamilyWords{"LRMs", ""},
        MCFamilyWords{"SRMs", "Streak racks too."},
        MCFamilyWords{"Thunderbolts", ""},
        MCFamilyWords{"PPCs", "ER PPCs too."},
        MCFamilyWords{"Lasers", "Not pulse lasers."},
        MCFamilyWords{"Pulse lasers", ""},
        MCFamilyWords{"Flamers", "Heavy flamers too."},
        MCFamilyWords{"Autocannons", "Ultras and LB-Xs too."},
        MCFamilyWords{"Gauss rifles", "Light Gauss and rail gun too."},
        MCFamilyWords{"Machine guns", ""},
        MCFamilyWords{"Long Toms", "Sniper guns too."},
    };

    static_assert(FamilyWords.size() == WeaponFamilies.size(), "words for each weapon family");

    MCGuiHandle<MCMatchOptionsWindow>& Made()
    {
        static MCGuiHandle<MCMatchOptionsWindow> made;
        return made;
    }

    /// <summary>Where row <paramref name="row"/> is: its tab, and for a weapon family which one (else none).</summary>
    struct MCPlace
    {
        MCMatchOptionsWindow::MCTab Tab = MCMatchOptionsWindow::MCTab::Play;
        std::optional<size_t> Family;
    };

    /// <summary>Row <paramref name="row"/>'s place; none for a heading (or no row).</summary>
    std::optional<MCPlace> PlaceOf(int32_t row)
    {
        const std::span<const MCOptionRow> rows = MatchOptionRows();

        if (row < 0 || static_cast<size_t>(row) >= rows.size() || rows[static_cast<size_t>(row)].IsHeading())
        {
            return std::nullopt;
        }

        // PLAY's rows come under the first heading, the weapon families under the second; the most skilled pilot is
        // FORCES' (the room's frames: the forces a team fields).
        int32_t headings = 0;
        int32_t familyStart = 0;

        for (int32_t at = 0; at <= row; at++)
        {
            if (rows[static_cast<size_t>(at)].IsHeading())
            {
                headings++;
                familyStart = at + 1;
            }
        }

        if (headings > 1)
        {
            return MCPlace{.Tab = MCMatchOptionsWindow::MCTab::Weapons,
                           .Family = static_cast<size_t>(row - familyStart)};
        }

        const bool pilot = rows[static_cast<size_t>(row)].Label == MCCapabilityOf(MCMatchOption::MostSkilledPilot).Name;
        return MCPlace{.Tab = pilot ? MCMatchOptionsWindow::MCTab::Forces : MCMatchOptionsWindow::MCTab::Play};
    }

    /// <summary>Names as a sentence lists them: "Ann", "Ann and Cal", "Ann, Cal and Dan".</summary>
    std::string Listed(std::span<const std::string> names)
    {
        std::string joined;

        for (size_t at = 0; at < names.size(); at++)
        {
            joined += std::format("{}{}", at == 0 ? "" : at + 1 == names.size() ? " and " : ", ", names[at]);
        }

        return joined;
    }

    std::string IdOf(int32_t row)
    {
        return std::format("{}{}", ControlPrefix, row);
    }

    Rml::Element* ElementIn(MCOptionalRef<Rml::ElementDocument> document, const char* id)
    {
        return document.has_value() ? document->get().GetElementById(id) : nullptr;
    }
}

MCMatchOptionsWindow::MCMatchOptionsWindow()
{
    SetDocument(WindowDocument);
    Modal = true;
    Back = [this] { Hide(); };
}

void MCMatchOptionsWindow::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick(DoneId, [this] { PressDone(); });
    OnChange(TabsId, [this](int tab) { ShowTab(static_cast<MCTab>(std::clamp(tab, 0, 2))); });
    OnChangeIn(RowsId,
               [this](const std::string& control, int choice)
               {
                   if (!control.starts_with(ControlPrefix))
                   {
                       return;
                   }

                   int32_t row = -1;
                   const std::string_view number = std::string_view(control).substr(ControlPrefix.size());

                   if (std::from_chars(number.data(), number.data() + number.size(), row).ec == std::errc())
                   {
                       Changed(row, choice);
                   }
               });
}

void MCMatchOptionsWindow::OpenOver()
{
    _Tab = MCTab::Forces;
    _Built.clear();

    if (!Document().has_value())
    {
        if (const auto opened = Open(); !opened)
        {
            SDL_Log("Menus: MATCH OPTIONS' window can't open: %s", opened.error().c_str());
        }

        return;
    }

    Show();
}

void MCMatchOptionsWindow::OnShow()
{
    const bool host = Hosting();
    SDL_Log("MATCH OPTIONS: shown, %s", host ? "the host's" : "a guest's");
    Keys = {{"ESC", std::string(host ? DoneWord : CloseWord)}};

    if (Rml::Element* tabs = ElementIn(Document(), TabsId); tabs != nullptr)
    {
        MCRmlKit::SetChoice(*tabs, static_cast<int>(_Tab));
    }

    _Built.clear();
    Build();
}

void MCMatchOptionsWindow::OnFrame()
{
    Refresh();
}

bool MCMatchOptionsWindow::Hosting() const
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    return game != nullptr && game->IsHost != 0;
}

std::vector<int32_t> MCMatchOptionsWindow::RowsOf(MCTab tab)
{
    std::vector<int32_t> rows;

    for (int32_t row = 0; row < static_cast<int32_t>(MatchOptionRows().size()); row++)
    {
        if (const std::optional<MCPlace> place = PlaceOf(row); place.has_value() && place->Tab == tab)
        {
            rows.push_back(row);
        }
    }

    return rows;
}

auto MCMatchOptionsWindow::RowNow(int32_t row) const -> MCRowNow
{
    const std::span<const MCOptionRow> rows = MatchOptionRows();
    const MCOptionRow& option = rows[static_cast<size_t>(row)];
    const int value = option.Value.has_value() ? option.Value->get() : 0;
    MCRowNow now{.Row = row, .HeldBy = MatchOptionHeldBy(row)};

    // The choices the room plays (the 1998 list's: those it doesn't are skipped), and the value's word.
    for (const MCOptionChoice& choice : option.Choices)
    {
        if (choice.Value == value)
        {
            now.Value = MCRmlDialogs::AsSentence(choice.Name);
        }

        if (choice.Offered == nullptr || choice.Offered())
        {
            now.Offered.push_back(choice.Value);
            now.Words.push_back(MCRmlDialogs::AsSentence(choice.Name));
        }
    }

    const auto found = std::ranges::find(now.Offered, value);
    now.Choice = found != now.Offered.end() ? static_cast<int32_t>(found - now.Offered.begin()) : -1;
    now.Stepper = now.Offered.size() > MostSwitchChoices;
    return now;
}

auto MCMatchOptionsWindow::Seen(int32_t row) const -> MCSeenRow
{
    const std::optional<MCPlace> place = PlaceOf(row);

    if (!place.has_value())
    {
        return {};
    }

    ReadMatchOptions();
    const MCRowNow now = RowNow(row);
    const MCOptionRow& option = MatchOptionRows()[static_cast<size_t>(row)];
    MCSeenRow seen{.Name = place->Family.has_value() ? std::string(FamilyWords[*place->Family].Name)
                                                     : MCRmlDialogs::AsSentence(option.Label),
                   .Value = now.Value,
                   .Set = Hosting() && now.HeldBy.empty() && now.Choice >= 0,
                   .Held = !now.HeldBy.empty()};

    if (seen.Held)
    {
        seen.Line = std::format(UpgradeFormat, Listed(now.HeldBy));
    }
    else if (place->Family.has_value())
    {
        seen.Line = std::string(FamilyWords[*place->Family].Line);
    }

    return seen;
}

void MCMatchOptionsWindow::ShowTab(MCTab tab)
{
    if (tab == _Tab && !_Built.empty())
    {
        return;
    }

    _Tab = tab;

    if (Rml::Element* tabs = ElementIn(Document(), TabsId);
        tabs != nullptr && MCRmlKit::Choice(*tabs) != static_cast<int>(tab))
    {
        MCRmlKit::SetChoice(*tabs, static_cast<int>(tab));
    }

    _Built.clear();
    Build();
}

void MCMatchOptionsWindow::Build()
{
    Rml::Element* rowsElement = ElementIn(Document(), RowsId);

    if (rowsElement == nullptr)
    {
        return;
    }

    ReadMatchOptions();
    const bool host = Hosting();
    std::vector<MCRowNow> built;
    std::string markup;

    for (const int32_t row : RowsOf(_Tab))
    {
        MCRowNow now = RowNow(row);
        const MCSeenRow seen = Seen(row);
        markup += std::format(R"(<div class="set-row"><div class="set-words"><div class="set-name">{}</div>)",
                              MCRmlDialog::Escape(seen.Name));

        if (!seen.Line.empty())
        {
            markup += std::format(R"(<div class="set-line">{}</div>)", MCRmlDialog::Escape(seen.Line));
        }

        markup += "</div>";

        if (seen.Set && now.Stepper)
        {
            std::string words;

            for (const std::string& word : now.Words)
            {
                words += (words.empty() ? "" : "|") + MCRmlDialog::Escape(word);
            }

            markup += std::format(R"(<div class="stepper" id="{}" data-choices="{}" data-choice="{}">)"
                                  R"(<div class="arrow">&#8249;</div><div class="step-value"></div>)"
                                  R"(<div class="arrow">&#8250;</div></div>)",
                                  IdOf(row), words, now.Choice);
        }
        else if (seen.Set)
        {
            markup += std::format(R"(<div class="switch" id="{}">)", IdOf(row));

            for (const std::string& word : now.Words)
            {
                markup += std::format("<div>{}</div>", MCRmlDialog::Escape(word));
            }

            markup += "</div>";
        }
        else
        {
            const bool dim = seen.Held || seen.Value == OffWord;
            markup += std::format(R"(<div class="set-value{}" id="{}">{}</div>)", dim ? " grey" : "", IdOf(row),
                                  MCRmlDialog::Escape(seen.Value));
        }

        markup += "</div>";
        built.push_back(std::move(now));
    }

    rowsElement->SetInnerRML(markup);

    // The controls' choices as the match plays them (no sound, no change event: the window set them).
    for (const MCRowNow& now : built)
    {
        if (Rml::Element* control = ElementIn(Document(), IdOf(now.Row).c_str());
            control != nullptr && now.Choice >= 0 && (control->IsClassSet("switch") || control->IsClassSet("stepper")))
        {
            MCRmlKit::SetChoice(*control, now.Choice);
        }
    }

    _Built = std::move(built);
    _BuiltHosting = host;

    // The foot: DONE for the host; a guest's CLOSE, saying who sets them.
    if (Rml::Element* done = ElementIn(Document(), DoneId); done != nullptr)
    {
        done->SetInnerRML(std::string(host ? DoneWord : CloseWord));
    }

    if (Rml::Element* note = ElementIn(Document(), NoteId); note != nullptr)
    {
        note->SetInnerRML(host ? std::string() : std::string(HostSetsThese));
    }
}

void MCMatchOptionsWindow::Refresh()
{
    ReadMatchOptions();
    std::vector<MCRowNow> now;

    for (const int32_t row : RowsOf(_Tab))
    {
        now.push_back(RowNow(row));
    }

    // Built again when the layout changed (the choices the room plays, who holds what, the host's or a guest's), else
    // only the values that changed set.
    const auto layout = [](const MCRowNow& row) { return std::tie(row.Offered, row.HeldBy, row.Stepper); };
    const bool same = Hosting() == _BuiltHosting && now.size() == _Built.size() &&
                      std::ranges::equal(now, _Built, {}, layout, layout);

    if (!same)
    {
        Build();
        return;
    }

    for (size_t at = 0; at < now.size(); at++)
    {
        if (now[at] == _Built[at])
        {
            continue;
        }

        if (Rml::Element* control = ElementIn(Document(), IdOf(now[at].Row).c_str()); control != nullptr)
        {
            if (control->IsClassSet("switch") || control->IsClassSet("stepper"))
            {
                if (now[at].Choice >= 0)
                {
                    MCRmlKit::SetChoice(*control, now[at].Choice);
                }
            }
            else
            {
                control->SetInnerRML(MCRmlDialog::Escape(now[at].Value));
                control->SetClass("grey", now[at].Value == OffWord || !now[at].HeldBy.empty());
            }
        }

        _Built[at] = now[at];
    }
}

void MCMatchOptionsWindow::Changed(int32_t row, int32_t choice)
{
    if (!Hosting() || !PlaceOf(row).has_value() || !MatchOptionHeldBy(row).empty())
    {
        return;
    }

    ReadMatchOptions();
    const MCRowNow now = RowNow(row);

    if (choice < 0 || static_cast<size_t>(choice) >= now.Offered.size() || choice == now.Choice)
    {
        return;
    }

    // The 1998 list's change: the value set, then its change (sent to the guests as the list sends it).
    const MCOptionRow& option = MatchOptionRows()[static_cast<size_t>(row)];
    const int32_t value = now.Offered[static_cast<size_t>(choice)];
    SDL_Log("MATCH OPTIONS: the host sets %.*s to %d", static_cast<int>(option.Label.size()), option.Label.data(),
            value);

    if (option.Value.has_value())
    {
        option.Value->get() = value;
    }

    if (option.Changed != nullptr)
    {
        option.Changed(value);
    }
}

void MCMatchOptionsWindow::Step(int32_t row, int32_t steps)
{
    ReadMatchOptions();
    const MCRowNow now = RowNow(row);
    const auto count = static_cast<int32_t>(now.Offered.size());

    if (count == 0)
    {
        return;
    }

    // A value none of the room's choices steps from the first, as the 1998 list's click.
    const int32_t from = std::max(now.Choice, 0);
    Changed(row, ((from + steps) % count + count) % count);
}

void MCMatchOptionsWindow::PressDone()
{
    if (Back)
    {
        Back();
    }
}

bool ShowMatchOptionsWindow()
{
    if (!Made())
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();

        if (!menus.has_value())
        {
            return false;
        }

        Made() = menus->get().Menus().CreateChild<MCMatchOptionsWindow>([](MCMatchOptionsWindow&) {});
    }

    Made()->OpenOver();

    if (!Made()->Error().empty())
    {
        SDL_Log("MATCH OPTIONS: the window can't show: %s", Made()->Error().c_str());
        return false;
    }

    return true;
}

MCGuiHandle<MCMatchOptionsWindow> MatchOptionsWindow()
{
    return Made();
}

bool MatchOptionsWindowShown()
{
    return Made() && Made()->Shown();
}
