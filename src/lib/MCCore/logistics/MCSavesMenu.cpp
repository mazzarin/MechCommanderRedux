#include "stdafx.h"
#include "logistics/MCSavesMenu.h"
#include "ai/MCMoveGeometry.h"
#include "gui/MCGuiCallback.h"
#include "gui/MCGuiSystem.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCLogistics.h"
#include "sound/MCSoundSystem.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlModel.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>

namespace
{
    constexpr const char* ScreenDocument = "saves.rml";
    constexpr const char* DialogDocument = "saves_dialog.rml";
    constexpr const char* ScreenModel = "saves";
    constexpr const char* DialogModel = "saves_dialog";
    /// <summary>The campaign screens' music, as the old screens play it again after a load or a save.</summary>
    constexpr int32_t LogisticsMusic = 0x16;
    /// <summary>The switch's choices: ALL, then the campaigns in MCSaves::MCCampaign's order.</summary>
    constexpr int AllChoice = 0;
    constexpr std::array<MCSaves::MCCampaign, 2> Campaigns = {MCSaves::MCCampaign::Original,
                                                              MCSaves::MCCampaign::Expansion};
    constexpr const char* ListId = "list";
    constexpr const char* RowClass = "list-row";
    constexpr const char* NameId = "name";
    constexpr const char* MapId = "map";
    constexpr const char* WhichId = "which";
    /// <summary>The thousands' separator in RP ("12,400"), and the dot between an operation's number and name.</summary>
    constexpr char ThousandsMark = ',';
    constexpr std::string_view OperationDot = " · ";

    MCGuiHandle<MCSavesScreen>& TheScreen()
    {
        static MCGuiHandle<MCSavesScreen> screen;
        return screen;
    }

    /// <summary>The screen on the game's menus, made the first time it's wanted; none when the menus can't start.</summary>
    MCOptionalRef<MCSavesScreen> MadeScreen()
    {
        if (const std::shared_ptr<MCSavesScreen> screen = TheScreen().Lock();
            screen != nullptr && !screen->WasDestroyed())
        {
            return *screen;
        }

        const auto menus = MCRmlMenus::ForGame();

        if (!menus)
        {
            SDL_Log("Menus: LOAD GAME and SAVE GAME can't show: %s", menus.error().c_str());
            return std::nullopt;
        }

        TheScreen() = menus->get().Menus().CreateChild<MCSavesScreen>([](MCSavesScreen&) {});
        return *TheScreen();
    }

    std::string PointsText(uint32_t points)
    {
        constexpr size_t Group = 3;
        std::string digits = std::to_string(points);

        for (size_t at = digits.size(); at > Group; at -= Group)
        {
            digits.insert(at - Group, 1, ThousandsMark);
        }

        return digits;
    }

    std::string_view WhenText(MCSaves::MCWhen when)
    {
        switch (when)
        {
            case MCSaves::MCWhen::Before:
                return "Before";
            case MCSaves::MCWhen::After:
                return "After";
            case MCSaves::MCWhen::Named:
                break;
        }

        return "Named";
    }

    /// <summary>The picked save's SAVED: "Before the mission", "After the mission", "Named".</summary>
    std::string_view SavedText(MCSaves::MCWhen when)
    {
        switch (when)
        {
            case MCSaves::MCWhen::Before:
                return "Before the mission";
            case MCSaves::MCWhen::After:
                return "After the mission";
            case MCSaves::MCWhen::Named:
                break;
        }

        return "Named";
    }

    /// <summary>What a save is, in DELETE's question: "Operation 2, mission 3: Destroy enemy convoy."</summary>
    std::string WhatText(const MCSaves::MCSave& save)
    {
        if (!save.Read)
        {
            return save.Name + ".";
        }

        return std::format("Operation {}, mission {}: {}.", save.Operation, save.Mission, save.MissionName);
    }

    /// <summary>When it was saved, in a sentence: "Saved before the mission, today at 21:52."</summary>
    std::string WhenSentence(const MCSaves::MCSave& save)
    {
        const std::string date = MCSaves::Game().DateSentence(save.Saved);
        std::string text = "Saved";

        if (save.Read && save.When == MCSaves::MCWhen::Before)
        {
            text += " before the mission";
        }
        else if (save.Read && save.When == MCSaves::MCWhen::After)
        {
            text += " after the mission";
        }
        else if (save.Read)
        {
            text += std::format(" as {}", save.Name);
        }

        return text + (date.empty() ? "." : std::format("{}{}.", save.Read ? ", " : " ", date));
    }

    std::string Trimmed(std::string_view text)
    {
        const size_t first = text.find_first_not_of(' ');
        return first == std::string_view::npos
                   ? std::string()
                   : std::string(text.substr(first, text.find_last_not_of(' ') - first + 1));
    }

    /// <summary>The campaign the logistics plays (CurPlanet: Port Arthur's, or Cermak's in the expansion).</summary>
    MCSaves::MCCampaign PlayedCampaign()
    {
        return CurPlanet == 0 ? MCSaves::MCCampaign::Original : MCSaves::MCCampaign::Expansion;
    }

    double RmlNow()
    {
        Rml::SystemInterface* system = Rml::GetSystemInterface();
        return system != nullptr ? system->GetElapsedTime() : 0.0;
    }
}

// MCSavesDialog

MCSavesDialog::MCSavesDialog()
{
    SetDocument(DialogDocument);
    Modal = true;
}

MCSavesDialog::~MCSavesDialog() = default;

void MCSavesDialog::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, DialogModel);
    _Model->Value("question", _Say.Question);
    _Model->Value("first", _Say.First);
    _Model->Value("second", _Say.Second);
    _Model->Value("answer", _Say.Answer);
    _Model->Value("asks", _Say.Asks);
}

void MCSavesDialog::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick("cancel",
            [this]
            {
                if (Back)
                {
                    Back();
                }
            });
    OnClick("answer", [this] { Answer(); });
}

void MCSavesDialog::Ask(MCSay say)
{
    _Say = std::move(say);
    Keys = {{"Enter", _Say.Answer}};

    if (_Say.Asks)
    {
        Keys.push_back({"Esc", "Cancel"});
    }

    // Escape is a question's CANCEL, and a message's OK.
    Back = _Say.Asks ? std::function<void()>([this] { Hide(); }) : std::function<void()>([this] { Answer(); });

    if (_Model != nullptr)
    {
        _Model->Changed();
    }

    if (!Document().has_value())
    {
        if (const auto opened = Open(); !opened)
        {
            SDL_Log("Menus: the saves' window can't open (%s): %s", _Say.Question.c_str(), opened.error().c_str());
        }
    }
    else
    {
        // Its words in before it shows, so its first frame says them.
        Layer().Context().Update();
        Show();
    }

    _ShownAt = RmlNow();

    if (_Say.ClosesAfter > 0 && _Ticker == nullptr && HasGuiSystem())
    {
        _Ticker = std::make_shared<MCGuiCallback>();
        const std::weak_ptr<MCRmlScreen> self = weak_from_this();
        _Ticker->SetExec(
            [self]
            {
                if (const std::shared_ptr<MCRmlScreen> dialog = self.lock(); dialog != nullptr)
                {
                    static_cast<MCSavesDialog&>(*dialog).Tick();
                }
            });
        GuiSystem().AddCallback(_Ticker);
    }
}

void MCSavesDialog::Tick()
{
    if (Shown() && _Say.ClosesAfter > 0 && RmlNow() - _ShownAt >= _Say.ClosesAfter)
    {
        Answer();
    }
}

void MCSavesDialog::Answer()
{
    // Hidden first, inside its own click (the kit pins the window while its press runs), then its action.
    const std::function<void()> answered = std::move(_Say.Answered);
    _Say.Answered = nullptr;
    _Say.ClosesAfter = 0;
    Hide();

    if (answered)
    {
        answered();
    }
}

void MCSavesDialog::OnDestroy()
{
    if (_Ticker != nullptr && HasGuiSystem())
    {
        GuiSystem().RemoveCallback(*_Ticker);
    }

    _Ticker.reset();
    _Model.reset();
    MCRmlScreen::OnDestroy();
}

// MCSavesScreen

MCSavesScreen::MCSavesScreen()
{
    SetDocument(ScreenDocument);
}

MCSavesScreen::~MCSavesScreen() = default;

void MCSavesScreen::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, ScreenModel);
    MCRmlModel& model = *_Model;
    model.Value("eyebrow", _Eyebrow);
    model.Value("title", _Title);
    model.Value("count", _Count);
    model.Value("empty", _Empty);
    model.Value("does", _Does);
    model.Value("all", _All);
    model.Value("saving", _Saving);
    model.Value("canDelete", _CanDelete);
    model.Value("canMain", _CanMain);
    model.Value("shown", _PickShown);
    model.Value("picked", _Picked);
    model.Value("name", _Name);
    model.Value("pickEyebrow", _PickEyebrow);
    model.Value("pickHeading", _PickHeading);
    model.Value("pickCampaign", _PickCampaign);
    model.Value("pickOperation", _PickOperation);
    model.Value("pickMission", _PickMission);
    model.Value("pickSaved", _PickSaved);
    model.Value("pickPoints", _PickPoints);
    model.Value("pickDate", _PickDate);
    model.List("rows", _Rows, std::pair{"mission", &MCRow::Mission}, std::pair{"campaign", &MCRow::Campaign},
               std::pair{"when", &MCRow::When}, std::pair{"points", &MCRow::Points}, std::pair{"date", &MCRow::Date});
    model.Event("pick",
                [this](const Rml::VariantList& arguments)
                {
                    if (!arguments.empty())
                    {
                        Pick(arguments.front().Get<int>());
                    }
                });
}

void MCSavesScreen::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick("back",
            [this]
            {
                if (Back)
                {
                    Back();
                }
            });
    OnClick("delete", [this] { PressDelete(); });
    OnClick("main", [this] { PressMain(); });
    OnChange(WhichId,
             [this](int choice)
             {
                 const std::optional<MCSaves::MCCampaign> campaign =
                     choice > AllChoice && static_cast<size_t>(choice) <= Campaigns.size()
                         ? std::optional(Campaigns[static_cast<size_t>(choice - 1)])
                         : std::nullopt;

                 if (campaign != _Campaign)
                 {
                     _Campaign = campaign;
                     Fill(PickedSave());
                 }
             });
}

MCOptionalRef<Rml::Element> MCSavesScreen::FirstFocus(Rml::ElementDocument& document)
{
    // SAVE GAME: the name, to type; LOAD GAME: the picked save, to load.
    if (_Saving)
    {
        if (Rml::Element* name = document.GetElementById(NameId); name != nullptr)
        {
            return *name;
        }
    }

    Rml::ElementList rows;

    if (Rml::Element* list = document.GetElementById(ListId); list != nullptr)
    {
        list->GetElementsByClassName(rows, RowClass);
    }

    // The list's rows as RmlUi made them from the model (its template, hidden, left out).
    std::erase_if(rows, [](Rml::Element* row) { return !row->IsVisible(); });

    if (_Picked >= 0 && static_cast<size_t>(_Picked) < rows.size())
    {
        return *rows[static_cast<size_t>(_Picked)];
    }

    return MCRmlScreen::FirstFocus(document);
}

void MCSavesScreen::SetUp(MCMode mode, std::optional<MCSaves::MCCampaign> campaign)
{
    EnsureRegistryVersion();
    _Mode = mode;
    _Saving = mode == MCMode::Save;
    _Campaign = campaign;
    _Eyebrow = _Saving ? "Between missions" : "Main menu";
    _Title = _Saving ? "Save game" : "Load game";
    _Does = _Saving ? "Save" : "Load";
    _Name = _Saving ? MCSaves::FreeName().value_or(std::string()) : std::string();
    _Picked = -1;
    MCSaves::Game().Refresh();
    Fill(std::nullopt);
}

std::optional<std::string> MCSavesScreen::PickedSave() const
{
    if (_Picked < 0 || static_cast<size_t>(_Picked) >= _Names.size())
    {
        return std::nullopt;
    }

    return _Names[static_cast<size_t>(_Picked)];
}

void MCSavesScreen::Fill(std::optional<std::string> keep)
{
    std::vector<std::reference_wrapper<const MCSaves::MCSave>> saves = MCSaves::Game().Of(_Campaign);

    // SAVE GAME: the player's own first (what a name writes over), then the game's, each newest first.
    if (_Saving)
    {
        std::ranges::stable_partition(saves, [](const MCSaves::MCSave& save)
                                      { return !save.Read || save.When == MCSaves::MCWhen::Named; });
    }

    _Names.clear();
    _Rows.clear();

    for (const MCSaves::MCSave& save : saves)
    {
        _Names.push_back(save.Name);
        _Rows.push_back(MCRow{
            .Mission = save.Read && save.When != MCSaves::MCWhen::Named ? save.MissionName : save.Name,
            .Campaign = save.Read ? std::string(MCSaves::CampaignShortName(save.Campaign)) : std::string(),
            .When = save.Read ? std::string(WhenText(save.When)) : std::string(),
            .Points = save.Read ? PointsText(save.ResourcePoints) : std::string(),
            .Date = MCSaves::Game().DateText(save.Saved),
        });
    }

    const auto kept = keep.has_value() ? std::ranges::find(_Names, *keep) : _Names.end();

    if (kept != _Names.end())
    {
        _Picked = static_cast<int>(kept - _Names.begin());
    }
    else
    {
        // LOAD GAME picks the newest; SAVE GAME none (SAVE makes a new save).
        _Picked = !_Saving && !_Names.empty() ? 0 : -1;
    }

    _Revision = MCSaves::Game().Revision();
    ShowPick();
    SetControls();
    Changed();
}

void MCSavesScreen::ShowPick()
{
    std::optional<MCSaves::MCSave> shown;
    const std::optional<std::string> picked = PickedSave();
    _PickEyebrow.clear();

    if (picked.has_value())
    {
        if (const MCOptionalRef<const MCSaves::MCSave> save = MCSaves::Game().Find(*picked); save.has_value())
        {
            shown = save->get();
        }
    }
    else if (_Saving && GlobalLog() != nullptr)
    {
        // Nothing picked on SAVE GAME: the save SAVE makes.
        shown = MCSaves::Unsaved(PlayedCampaign(), GlobalLog()->CurrentMission,
                                 static_cast<uint32_t>(std::max(ResourcePoints, 0)));
        _PickEyebrow = "This save";
    }

    _PickShown = shown.has_value();
    const bool read = shown.has_value() && shown->Read;
    _PickHeading = !shown.has_value() ? std::string() : read ? shown->MissionName : shown->Name;
    _PickCampaign = read ? std::string(MCSaves::CampaignName(shown->Campaign)) : std::string();
    _PickOperation = read ? std::format("{}{}{}", shown->Operation, OperationDot, shown->OperationName) : std::string();
    _PickMission = read ? std::to_string(shown->Mission) : std::string();
    _PickSaved = read ? std::string(SavedText(shown->When)) : std::string();
    _PickPoints = read ? PointsText(shown->ResourcePoints) : std::string();
    _PickDate = !shown.has_value()     ? std::string()
                : _PickEyebrow.empty() ? MCSaves::Game().DateText(shown->Saved)
                                       : std::string("Now");
    _MapSource = read && !shown->Scenario.empty() ? std::string(MCRmlArt::MapScheme) + shown->Scenario : std::string();

    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value())
    {
        if (Rml::Element* map = document->get().GetElementById(MapId); map != nullptr)
        {
            if (_MapSource.empty())
            {
                map->RemoveProperty("decorator");
            }
            else
            {
                map->SetProperty("decorator", std::format("image({} contain left top)", _MapSource));
            }
        }
    }

    // The next map read ahead, so a pick's map draws at once on HD (as the briefing's way back reads its own).
    if (read && !shown->Scenario.empty())
    {
        MCLogistics::ReadBriefingMapAhead(shown->Scenario);
    }
}

void MCSavesScreen::SetControls()
{
    const size_t count = _Rows.size();
    _All = !_Campaign.has_value();
    _Count = count == 0 ? std::string("No saves") : count == 1 ? std::string("1 save") : std::format("{} saves", count);
    _Empty = _Campaign.has_value() ? "No saves in this campaign yet." : "No saves yet.";
    _CanDelete = _Picked >= 0;
    _CanMain = _Saving || _Picked >= 0;

    if (_Saving)
    {
        Keys = {{"Enter", "Save"}, {"Esc", "Back"}};
    }
    else if (count > 0)
    {
        Keys = {{"Enter", "Load"}, {"Del", "Delete"}, {"Esc", "Back"}};
    }
    else
    {
        Keys = {{"Esc", "Back"}};
    }

    WriteKeys();

    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value())
    {
        if (Rml::Element* which = document->get().GetElementById(WhichId); which != nullptr)
        {
            int choice = AllChoice;

            if (_Campaign.has_value())
            {
                choice = static_cast<int>(std::ranges::find(Campaigns, *_Campaign) - Campaigns.begin()) + 1;
            }

            if (MCRmlKit::Choice(*which) != choice)
            {
                MCRmlKit::SetChoice(*which, choice);
            }
        }
    }
}

void MCSavesScreen::Changed()
{
    if (_Model != nullptr)
    {
        _Model->Changed();
    }
}

void MCSavesScreen::Pick(int row)
{
    const int picked = row >= 0 && static_cast<size_t>(row) < _Rows.size() ? row : -1;

    if (picked == _Picked)
    {
        return;
    }

    _Picked = picked;

    // SAVE GAME: the picked save's name in the box, to write over it.
    if (_Saving && picked >= 0)
    {
        _Name = _Names[static_cast<size_t>(picked)];
    }

    ShowPick();
    SetControls();
    Changed();
}

void MCSavesScreen::PressMain()
{
    if (!_Saving)
    {
        if (const std::optional<std::string> name = PickedSave(); name.has_value())
        {
            Load(*name);
        }

        return;
    }

    std::string name = Trimmed(_Name);

    if (name.empty())
    {
        name = MCSaves::FreeName().value_or(std::string());
    }

    if (MCSaves::Game().Find(name).has_value())
    {
        const MCSaves::MCSave& save = MCSaves::Game().Find(name)->get();
        Say(MCSavesDialog::MCSay{
            .Question = std::string(MCSavesMenu::OverwriteQuestion),
            .First = WhatText(save),
            .Second = WhenSentence(save),
            .Answer = "Overwrite",
            .Asks = true,
            .Answered = [this, name] { Save(name, MCSaves::MCOld::DeletedFirst); },
        });
        return;
    }

    Save(name);
}

void MCSavesScreen::PressDelete()
{
    const std::optional<std::string> name = PickedSave();
    const MCOptionalRef<const MCSaves::MCSave> save =
        name.has_value() ? MCSaves::Game().Find(*name) : MCOptionalRef<const MCSaves::MCSave>();

    if (!save.has_value())
    {
        return;
    }

    Say(MCSavesDialog::MCSay{
        .Question = std::string(MCSavesMenu::DeleteQuestion),
        .First = WhatText(save->get()),
        .Second = WhenSentence(save->get()),
        .Answer = "Delete",
        .Asks = true,
        .Answered =
            [this, name = *name]
        {
            // The row after it picked in its place (the one before, for the last).
            const auto at = std::ranges::find(_Names, name);
            std::optional<std::string> next;

            if (at != _Names.end() && at + 1 != _Names.end())
            {
                next = *(at + 1);
            }
            else if (at != _Names.end() && at != _Names.begin())
            {
                next = *(at - 1);
            }

            MCSaves::Game().Delete(name);
            Fill(next);
        },
    });
}

bool MCSavesScreen::OnKey(int key, int /*modifiers*/)
{
    if (key == Rml::Input::KI_DELETE)
    {
        PressDelete();
        return true;
    }

    return false;
}

void MCSavesScreen::Say(MCSavesDialog::MCSay say)
{
    if (!_Dialog)
    {
        _Dialog = Layer().CreateChild<MCSavesDialog>([](MCSavesDialog&) {});
    }

    _Dialog->Ask(std::move(say));
}

void MCSavesScreen::Load(const std::string& name)
{
    const std::weak_ptr<MCRmlScreen> self = weak_from_this();
    MCScreenFlow::Change(
        "load game",
        [self, name]
        {
            const std::shared_ptr<MCRmlScreen> screen = self.lock();

            if (screen == nullptr)
            {
                return;
            }

            auto& saves = static_cast<MCSavesScreen&>(*screen);
            saves.Hide();
            LoadingSolo = false;

            if (LoadSavedGame(name, true))
            {
                return;
            }

            // Refused: LOAD GAME again, saying so.
            SDL_Log("Menus: the save %s couldn't be loaded", name.c_str());
            saves.Show();
            saves.Say(MCSavesDialog::MCSay{.Question = std::string(MCSavesMenu::RefusedText), .Answer = "OK"});
        });
}

void MCSavesScreen::Save(const std::string& name, MCSaves::MCOld old)
{
    SoundSystem()->StopDigitalMusic();
    const MCSaves::MCWritten written = MCSaves::Game().Write(name, old);
    SoundSystem()->PlayDigitalMusic(LogisticsMusic, true);

    if (written == MCSaves::MCWritten::Saved)
    {
        Fill(name);
        Say(MCSavesDialog::MCSay{.Question = std::string(MCSavesMenu::SavedText),
                                 .Answer = "OK",
                                 .Answered = BackToCampaign,
                                 .ClosesAfter = MCSavesMenu::SavedSeconds});
        return;
    }

    Fill(PickedSave());
    Say(MCSavesDialog::MCSay{.Question =
                                 std::string(written == MCSaves::MCWritten::DiskFull ? MCSavesMenu::DiskFullText
                                                                                     : MCSavesMenu::SaveFailedText),
                             .Answer = "OK"});
}

void MCSavesScreen::BackToCampaign()
{
    MCScreenFlow::Change("saved",
                         []
                         {
                             if (const MCOptionalRef<MCSavesScreen> screen = MCSavesMenu::Screen(); screen.has_value())
                             {
                                 screen->get().Hide();
                             }

                             // The campaign screen it was left from (the old save screen's way back).
                             SaveWorkedCallback(0);
                         });
}

void MCSavesScreen::OnDestroy()
{
    if (const std::shared_ptr<MCSavesDialog> dialog = _Dialog.Lock(); dialog != nullptr)
    {
        dialog->Close();
    }

    _Model.reset();
    MCRmlScreen::OnDestroy();
}

// MCSavesMenu

MCScreenFlow::MCScreenKind MCSavesMenu::Kind(MCSavesScreen::MCMode mode)
{
    using MCMode = MCSavesScreen::MCMode;
    return {
        .Name = mode == MCMode::Load ? "load" : "save",
        .Prepare =
            [mode]
        {
            const MCOptionalRef<MCSavesScreen> made = MadeScreen();

            if (!made.has_value())
            {
                return std::vector<MCScreenChange::MCPiece>();
            }

            // Read (first time), then filled for this opening and its data shown, then laid out and recorded.
            std::vector<MCScreenChange::MCPiece> pieces = made->get().Prepare();
            const std::weak_ptr<MCRmlScreen> self = made->get().weak_from_this();
            const auto fill = [self, mode]
            {
                if (const std::shared_ptr<MCRmlScreen> screen = self.lock();
                    screen != nullptr && screen->Error().empty())
                {
                    auto& saves = static_cast<MCSavesScreen&>(*screen);
                    saves.Back = [] { MCScreenFlow::Back(); };
                    saves.SetUp(mode, mode == MCMode::Save ? std::optional(PlayedCampaign()) : std::nullopt);
                    screen->Layer().Context().Update();
                }
            };

            pieces.insert(pieces.begin() + 1, MCScreenChange::MCPiece{.Part = "saves.fill", .Run = fill});
            return pieces;
        },
        .Show =
            []
        {
            if (const MCOptionalRef<MCSavesScreen> screen = Screen(); screen.has_value())
            {
                screen->get().Show();
            }
        },
        .Hide =
            []
        {
            if (const MCOptionalRef<MCSavesScreen> screen = Screen(); screen.has_value())
            {
                screen->get().Hide();
            }
        },
        .Ready =
            []
        {
            const MCOptionalRef<MCSavesScreen> screen = Screen();
            return !screen.has_value() || screen->get().Ready();
        },
        .Shows =
            [mode]
        {
            const MCOptionalRef<MCSavesScreen> screen = Screen();
            return screen.has_value() && screen->get().Shown() && screen->get().Mode() == mode;
        },
        .GivesWay = true,
    };
}

MCOptionalRef<MCSavesScreen> MCSavesMenu::Screen()
{
    const std::shared_ptr<MCSavesScreen> screen = TheScreen().Lock();
    return screen != nullptr && !screen->WasDestroyed() ? MCOptionalRef<MCSavesScreen>(*screen) : std::nullopt;
}
