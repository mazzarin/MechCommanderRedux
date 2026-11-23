#include "stdafx.h"
#include "logistics/MCPrefsScreen.h"
#include "platform/MCInput.h"
#include "ui/MCRmlKit.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/StringUtilities.h>

namespace
{
    using MCSettingsModel::MCSection;
    using MCSettingsModel::MCSetting;
    using Rml::StringUtilities::EncodeRml;

    constexpr const char* PrefsDocument = "prefs.rml";

    /// <summary>The document's places (ui/prefs.rml).</summary>
    constexpr const char* SectionsId = "sections";
    constexpr const char* RowsId = "rows";
    constexpr const char* SheetId = "sheet";
    constexpr const char* PlaceId = "place";
    constexpr const char* AcceptId = "accept";
    constexpr const char* CancelId = "cancel";
    constexpr const char* DefaultsId = "defaults";
    constexpr const char* OriginalId = "original";
    constexpr const char* LaptopId = "laptop";

    /// <summary>The look's and the sheet's classes (ui/look.rcss, ui/prefs.rcss).</summary>
    constexpr const char* ShownClass = "shown";
    constexpr const char* OffClass = "off";
    constexpr const char* HiddenClass = "hidden";
    constexpr const char* WaitingClass = "waiting";
    constexpr const char* EmptyClass = "empty";
    constexpr const char* BindClass = "bind";
    constexpr const char* MissionClass = "mission";
    constexpr const char* NightClass = "night";

    /// <summary>CONTROLS' boxes: <c>key-</c> and the command's number; a section's rows: <c>section-</c> and its name.</summary>
    constexpr std::string_view KeyBoxPrefix = "key-";
    constexpr std::string_view SectionPrefix = "section-";

    /// <summary>The eyebrow over the name: where Preferences opened.</summary>
    constexpr std::string_view MenusPlaceName = "Main menu";
    constexpr std::string_view MissionPlaceName = "Paused";

    /// <summary>A box waiting for a key.</summary>
    constexpr std::string_view PressAKey = "Press a key";

    /// <summary>The scan code of Escape (set 1), which stops a box waiting rather than binding.</summary>
    constexpr int16_t EscapeScanCode = 0x01;
    /// <summary>The scan code bit Windows' extended keys carry (MCInput::MCScanCode::Extended).</summary>
    constexpr int16_t ExtendedScanCode = 0x100;
    /// <summary>RmlUi's right mouse button.</summary>
    constexpr int RightButton = 1;

    /// <summary>REPORTS' words under its switch (the style frames').</summary>
    constexpr std::string_view ReportsAbout =
        "A report holds your graphics card, memory and screen, how fast each mission ran, your settings, and what "
        "happened in a crash, under a random ID made on this computer.";

    /// <summary>A 1998 label in capitals in sentence case, as the look writes a name (FORCE RUN: Force run).</summary>
    std::string SentenceCase(std::string_view label)
    {
        std::string words(label);
        bool first = true;

        for (char& c : words)
        {
            if (std::isalpha(static_cast<unsigned char>(c)) != 0)
            {
                c = first ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                          : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                first = false;
            }
        }

        return words;
    }

    std::string SectionId(MCSection section)
    {
        std::string name(MCSettingsModel::SectionName(section));
        std::ranges::transform(name, name.begin(),
                               [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        return std::string(SectionPrefix) + name;
    }

    std::string KeyBoxId(MCKeyCommand command)
    {
        return std::format("{}{}", KeyBoxPrefix, static_cast<int>(command));
    }

    /// <summary>The command of CONTROLS' box <paramref name="id"/>; Unused for another element.</summary>
    MCKeyCommand KeyBoxCommand(std::string_view id)
    {
        if (!id.starts_with(KeyBoxPrefix))
        {
            return MCKeyCommand::Unused;
        }

        int number = 0;
        const std::string_view digits = id.substr(KeyBoxPrefix.size());

        if (const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
            error != std::errc{} || end != digits.data() + digits.size() || number < 0 ||
            std::cmp_greater_equal(number, NumKeyCommands))
        {
            return MCKeyCommand::Unused;
        }

        return static_cast<MCKeyCommand>(number);
    }

    /// <summary>CONTROLS' rows: the commands in 1998's groups, a key and its alternate each in a box.</summary>
    std::string ControlsRml()
    {
        std::string rml = "<div class=\"keys-head\"><div class=\"grow\">Command</div><div class=\"c-key\">Key</div>"
                          "<div class=\"c-key\">Alternate</div></div>";
        bool first = true;

        for (const MCKeyBindings::MCControlRow& row : MCKeyBindings::ControlRows())
        {
            if (row.IsHeading())
            {
                rml += std::format("<div class=\"key-group{}\">{}</div>", first ? " first" : "",
                                   EncodeRml(SentenceCase(row.Label)));
                first = false;
                continue;
            }

            const std::string alternate =
                row.Alternate != MCKeyCommand::Unused
                    ? std::format("<div class=\"bind\" id=\"{}\"></div>", KeyBoxId(row.Alternate))
                    : std::string();
            rml += std::format("<div class=\"key-row\"><div class=\"command grow\">{}</div><div class=\"c-key\"><div "
                               "class=\"bind\" id=\"{}\"></div></div><div class=\"c-key\">{}</div></div>",
                               EncodeRml(SentenceCase(row.Label)), KeyBoxId(row.Command), alternate);
        }

        return rml;
    }

    /// <summary>Sets <paramref name="element"/>'s words when they differ (RmlUi lays out again for a change).</summary>
    void SetWords(Rml::Element& element, const std::string& words)
    {
        if (element.GetInnerRML() != words)
        {
            element.SetInnerRML(words);
        }
    }
}

MCPrefsScreen::MCPrefsScreen(MCPlace place)
    : MCSettingsScreen(place == MCPlace::Mission), _Place(place), _Keys(MCKeyBindings::Player())
{
    MCSettingsModel::Opened(InMission());
    _Saved = MCSettingsModel::Save();
    SetDocument(PrefsDocument);
    Keys = {{"Q E", "Change section"}, {"Esc", "Cancel"}};
}

void MCPrefsScreen::OnLoaded(Rml::ElementDocument& document)
{
    if (InMission())
    {
        document.SetClass(NightClass, false);
        document.SetClass(MissionClass, true);
    }

    if (Rml::Element* place = document.GetElementById(PlaceId); place != nullptr)
    {
        place->SetInnerRML(std::string(InMission() ? MissionPlaceName : MenusPlaceName));
    }

    MakeRows(document);
    OnChange(SectionsId,
             [this](int choice)
             {
                 if (choice >= 0 && std::cmp_less(choice, MCSettingsModel::Sections.size()))
                 {
                     ShowSection(MCSettingsModel::Sections[static_cast<size_t>(choice)]);
                 }
             });
    OnClick(AcceptId, [this] { Accept(); });
    OnClick(CancelId,
            [this]
            {
                if (Back)
                {
                    Back();
                }
            });
    OnClick(DefaultsId, [this] { UseDefaults(); });
    OnClick(OriginalId,
            [this]
            {
                MCSettingsModel::UseOriginal(InMission());
                Refresh();
            });
    OnClick(LaptopId,
            [this]
            {
                _Keys = MCKeyBindings::Laptop();
                StopWaiting();
                RefreshKeys();
            });
    const std::string controls = SectionId(MCSection::Controls);
    Listen(controls, Rml::EventId::Click, [this](Rml::Event& event) { OnKeyBox(event); });
    Listen(controls, Rml::EventId::Mousedown, [this](Rml::Event& event) { OnKeyBox(event); });
    // A click anywhere else stops a box waiting.
    Listen(SheetId, Rml::EventId::Click,
           [this](Rml::Event& event)
           {
               const Rml::Element* target = event.GetTargetElement();

               if (_Binding.has_value() && (target == nullptr || !target->IsClassSet(BindClass)))
               {
                   StopWaiting();
                   RefreshKeys();
               }
           });
    Refresh();
    RefreshKeys();
    ShowSection(MCSection::Game);
}

void MCPrefsScreen::MakeRows(Rml::ElementDocument& document)
{
    std::string tabs;
    std::string rows;

    for (const MCSection section : MCSettingsModel::Sections)
    {
        tabs += std::format("<div class=\"tab side-tab\">{}</div>", MCSettingsModel::SectionName(section));
        rows += std::format("<div class=\"section\" id=\"{}\">", SectionId(section));

        if (section == MCSection::Controls)
        {
            rows += ControlsRml();
        }

        std::string_view group;
        bool first = true;

        for (const MCSetting& setting : MCSettingsModel::All())
        {
            if (setting.Section != section)
            {
                continue;
            }

            if (first || setting.Group != group)
            {
                rows += std::format("<div class=\"group eyebrow{}\">{}</div>", first ? " first" : "",
                                    EncodeRml(std::string(setting.Group)));
                group = setting.Group;
                first = false;
            }

            rows += RowRml(setting, false);
        }

        if (section == MCSection::Reports)
        {
            rows += std::format("<div class=\"about\"><p>{}</p></div>", EncodeRml(std::string(ReportsAbout)));
        }

        rows += "</div>";
    }

    if (Rml::Element* sections = document.GetElementById(SectionsId); sections != nullptr)
    {
        sections->SetInnerRML(tabs);
    }

    if (Rml::Element* list = document.GetElementById(RowsId); list != nullptr)
    {
        list->SetInnerRML(rows);
    }

    for (const MCSetting& setting : MCSettingsModel::All())
    {
        ListenToRow(setting);
    }
}

void MCPrefsScreen::RefreshKeys()
{
    const MCOptionalRef<Rml::ElementDocument> found = Document();

    if (!found.has_value())
    {
        return;
    }

    for (const MCKeyBindings::MCControlRow& row : MCKeyBindings::ControlRows())
    {
        for (const MCKeyCommand command : {row.Command, row.Alternate})
        {
            if (command == MCKeyCommand::Unused)
            {
                continue;
            }

            Rml::Element* box = found->get().GetElementById(KeyBoxId(command));

            if (box == nullptr)
            {
                continue;
            }

            const bool waiting = _Binding.has_value() && _Binding->Command == command;
            const uint32_t key = _Keys[static_cast<size_t>(command)];
            box->SetClass(WaitingClass, waiting);
            box->SetClass(EmptyClass, !waiting && key == MCKeyBindings::NoKey);
            SetWords(*box, EncodeRml(waiting ? std::string(PressAKey) : MCKeyBindings::KeyLabel(key)));
        }
    }
}

void MCPrefsScreen::ShowSection(MCSection section)
{
    _Section = section;
    StopWaiting();
    RefreshKeys();
    const MCOptionalRef<Rml::ElementDocument> found = Document();

    if (!found.has_value())
    {
        return;
    }

    Rml::ElementDocument& document = found->get();
    const auto index =
        static_cast<int>(std::ranges::find(MCSettingsModel::Sections, section) - MCSettingsModel::Sections.begin());

    if (Rml::Element* tabs = document.GetElementById(SectionsId); tabs != nullptr && MCRmlKit::Choice(*tabs) != index)
    {
        MCRmlKit::SetChoice(*tabs, index);
    }

    for (const MCSection other : MCSettingsModel::Sections)
    {
        if (Rml::Element* rows = document.GetElementById(SectionId(other)); rows != nullptr)
        {
            rows->SetClass(ShownClass, other == section);
        }
    }

    if (Rml::Element* list = document.GetElementById(RowsId); list != nullptr)
    {
        list->SetScrollTop(0);
    }

    ShowFoot();
}

void MCPrefsScreen::ShowFoot()
{
    const MCOptionalRef<Rml::ElementDocument> found = Document();

    if (!found.has_value())
    {
        return;
    }

    Rml::ElementDocument& document = found->get();

    if (Rml::Element* original = document.GetElementById(OriginalId); original != nullptr)
    {
        original->SetClass(HiddenClass, _Section != MCSection::Hd);
    }

    if (Rml::Element* laptop = document.GetElementById(LaptopId); laptop != nullptr)
    {
        laptop->SetClass(HiddenClass, _Section != MCSection::Controls);
    }

    // REPORTS is the player's own answer: no default to put back.
    if (Rml::Element* defaults = document.GetElementById(DefaultsId); defaults != nullptr)
    {
        defaults->SetClass(OffClass, _Section == MCSection::Reports);
    }
}

void MCPrefsScreen::Accept()
{
    if (_Done)
    {
        return;
    }

    BeforeRestart();

    if (Accepted)
    {
        Accepted();
    }
}

void MCPrefsScreen::BeforeRestart()
{
    // ACCEPT, and the HD pack's RESTART (which quits the game): what the screen changed is kept.
    if (_Done)
    {
        return;
    }

    _Done = true;
    StopWaiting();
    MCKeyBindings::SetPlayer(_Keys);
    MCSettingsModel::Keep();
}

void MCPrefsScreen::Leave()
{
    if (_Done)
    {
        return;
    }

    _Done = true;
    StopWaiting();
    MCSettingsModel::Restore(_Saved);
}

void MCPrefsScreen::UseDefaults()
{
    switch (_Section)
    {
        case MCSection::Controls:
        {
            _Keys = MCKeyBindings::Defaults();
            StopWaiting();
            RefreshKeys();
            return;
        }
        case MCSection::Reports:
            return;
        default:
        {
            MCSettingsModel::UseDefaults(_Section, InMission());
            Refresh();
            return;
        }
    }
}

void MCPrefsScreen::OnKeyBox(Rml::Event& event)
{
    Rml::Element* target = event.GetTargetElement();

    while (target != nullptr && !target->IsClassSet(BindClass))
    {
        target = target->GetParentNode();
    }

    if (target == nullptr)
    {
        return;
    }

    const MCKeyCommand command = KeyBoxCommand(target->GetId());

    if (command == MCKeyCommand::Unused)
    {
        return;
    }

    if (event.GetId() == Rml::EventId::Mousedown)
    {
        // The right button clears the key; the left's press is the click's.
        if (event.GetParameter<int>("button", 0) == RightButton)
        {
            _Keys[static_cast<size_t>(command)] = MCKeyBindings::NoKey;
            StopWaiting();
            RefreshKeys();
        }

        return;
    }

    _Binding = MCWaiting{.Command = command};
    _PendingModifier = 0;
    RefreshKeys();
}

bool MCPrefsScreen::OnRawKey(const SDL_KeyboardEvent& key)
{
    if (!_Binding.has_value())
    {
        return false;
    }

    const MCInput::MCScanCode scan = MCInput::Win32ScanCode(key.scancode);
    const auto code = static_cast<int16_t>(scan.Code | (scan.Extended ? ExtendedScanCode : 0));
    const bool shift = (key.mod & SDL_KMOD_SHIFT) != 0;
    const bool ctrl = (key.mod & SDL_KMOD_CTRL) != 0;
    const bool alt = (key.mod & SDL_KMOD_ALT) != 0;

    if (!key.down)
    {
        // A modifier let go with no other key since it went down binds alone.
        if (_PendingModifier != 0 && _PendingModifier == code)
        {
            Bind(MCKeyBindings::FromKeyEvent(code, shift, ctrl, alt));
        }

        return true;
    }

    if (key.repeat)
    {
        return true;
    }

    if (code == EscapeScanCode)
    {
        StopWaiting();
        RefreshKeys();
    }
    else if (MCKeyBindings::IsModifierKey(code))
    {
        _PendingModifier = code;
    }
    else if (code != 0)
    {
        // A key without a code would be every such key in a mission, and PREFS couldn't name it: it waits on.
        Bind(MCKeyBindings::FromKeyEvent(code, shift, ctrl, alt));
    }

    return true;
}

void MCPrefsScreen::Bind(uint32_t key)
{
    if (!_Binding.has_value())
    {
        return;
    }

    MCKeyBindings::Bind(_Keys, _Binding->Command, key);
    StopWaiting();
    RefreshKeys();
}

void MCPrefsScreen::StopWaiting()
{
    _Binding.reset();
    _PendingModifier = 0;
}

void MCPrefsScreen::OnFrame()
{
    // What changes by itself (the HD pack's download) shown as it goes.
    RefreshLive();
}

void MCPrefsScreen::OnDestroy()
{
    // Taken down without ACCEPT or CANCEL (the game's end, a mission's): nothing it changed is kept.
    Leave();
    MCSettingsScreen::OnDestroy();
}
