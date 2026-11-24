#include "stdafx.h"
#include "logistics/MCMainMenuScreen.h"
#include "ai/MCMoveGeometry.h"
#include "lib/MCFile.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCQuestionWindow.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCSavesMenu.h"
#include "logistics/MCUpdatePage.h"
#include "logistics/MCUpdateScreen.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "network/MCInternetLobby.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCFileSystem.h"
#include "platform/MCSocket.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlModel.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
    using MCCampaign = MCSaves::MCCampaign;

    /// <summary>A panel's flow kind, its document and its model's name.</summary>
    struct MCPanelInfo
    {
        MCMainPanel Panel;
        std::string_view Kind;
        const char* Document;
        const char* Model;
    };

    constexpr std::array<MCPanelInfo, 4> Panels = {{
        {MCMainPanel::Main, "main", "main_menu.rml", "main-menu"},
        {MCMainPanel::Campaigns, "campaigns", "main_menu_campaigns.rml", "campaigns"},
        {MCMainPanel::Solo, "solo missions", "main_menu_solo.rml", "solo-missions"},
        {MCMainPanel::Multiplayer, "multiplayer", "main_menu_multiplayer.rml", "multiplayer"},
    }};

    /// <summary>The submenus, prepared ahead while the main menu shows.</summary>
    constexpr std::array<MCMainPanel, 3> Submenus = {MCMainPanel::Campaigns, MCMainPanel::Solo,
                                                     MCMainPanel::Multiplayer};

    const MCPanelInfo& InfoOf(MCMainPanel panel)
    {
        return Panels[static_cast<size_t>(panel)];
    }

    /// <summary>The documents' places (ui/main_menu*.rml and their template, ui/main_menu_frame.rml).</summary>
    constexpr const char* ReturnId = "return";
    constexpr const char* ContinueId = "continue";
    constexpr const char* CampaignsId = "campaigns";
    constexpr const char* LoadId = "load";
    constexpr const char* SaveId = "save";
    constexpr const char* SoloId = "solo";
    constexpr const char* MultiplayerId = "multiplayer";
    constexpr const char* PreferencesId = "preferences";
    constexpr const char* CinemaId = "cinema";
    constexpr const char* QuitId = "quit";
    constexpr const char* UpdateId = "update";
    constexpr const char* BackId = "back";
    constexpr const char* MainId = "main";
    constexpr const char* NewId = "new";
    constexpr const char* InternetId = "internet";
    constexpr const char* LanId = "lan";
    constexpr const char* RetryId = "retry";
    constexpr const char* RowsId = "rows";
    constexpr const char* ChoiceClass = "choice";
    constexpr const char* RowClass = "list-row";

    /// <summary>The words.</summary>
    constexpr std::string_view StandsFormat = "Operation {}, mission {}";
    constexpr std::string_view LineFormat = "{} · {}";
    constexpr std::string_view UpdateFormat = "Update to {}";
    constexpr std::string_view SoloFormat = "Buy {} tons with {} RP";
    constexpr std::string_view OnlineFormat = "{} players online";
    constexpr std::string_view OneOnline = "1 player online";
    constexpr std::string_view SoloReturn = "Solo mission";
    constexpr std::string_view ContinueWord = "Continue";
    constexpr std::string_view NewCampaignWord = "New campaign";
    constexpr std::string_view QuitWord = "Quit";
    constexpr std::string_view MainMenuPlace = "Main menu";
    /// <summary>The campaigns' own names, under CAMPAIGNS' rows, by MCCampaign.</summary>
    constexpr std::array<std::string_view, 2> CampaignTitles = {"MechCommander", "Desperate Measures"};
    constexpr std::array<MCCampaign, 2> Campaigns = {MCCampaign::Original, MCCampaign::Expansion};

    /// <summary>The logistics state 1998's MULTIPLAYER ACCESS had.</summary>
    constexpr int32_t MultiplayerState = 10;

    /// <summary>The tests' network (<see cref="MCMainMenuScreens::SetNetworkForTests"/>).</summary>
    std::optional<bool> NetworkForTests;

    /// <summary>The tests of 1998's main menu (<see cref="MCMainMenuScreens::SetOldForTests"/>).</summary>
    bool OldForTests = false;

    /// <summary>The campaign the logistics plays (CurPlanet: Port Arthur's, or Cermak's in the expansion).</summary>
    MCCampaign PlayedCampaign()
    {
        return CurPlanet == 0 ? MCCampaign::Original : MCCampaign::Expansion;
    }

    std::string Stands(int32_t operation, int32_t mission)
    {
        return std::format(StandsFormat, operation, mission);
    }

    /// <summary>Where a save opens, as CONTINUE names it: "Original campaign · Operation 2, mission 3".</summary>
    std::string OpensAt(const MCSaves::MCSave& save)
    {
        return std::format(LineFormat, MCSaves::CampaignName(save.Campaign),
                           Stands(save.OpensOperation, save.OpensMission));
    }

    /// <summary>The panel's screen while made, as it was made.</summary>
    std::shared_ptr<MCMainMenuScreen> MadeScreen(MCMainPanel panel)
    {
        const std::shared_ptr<MCRmlScreen> made = MCRmlFlow::Made(InfoOf(panel).Kind).Lock();
        return made != nullptr && !made->WasDestroyed() ? std::static_pointer_cast<MCMainMenuScreen>(made) : nullptr;
    }

    /// <summary>The panel's flow id (the kinds registered first).</summary>
    MCScreenFlow::MCScreenId IdOf(MCMainPanel panel)
    {
        switch (panel)
        {
            case MCMainPanel::Main:
                return MCMenuScreens::Main();
            case MCMainPanel::Campaigns:
                return MCMenuScreens::Campaigns();
            case MCMainPanel::Solo:
                return MCMenuScreens::Solo();
            case MCMainPanel::Multiplayer:
                return MCMenuScreens::Multiplayer();
        }

        return MCScreenFlow::NoScreen;
    }

    /// <summary>The kind's preparation's pieces (the panel made first, if it isn't); none when it can't be made.</summary>
    std::vector<MCScreenChange::MCPiece> PiecesOf(MCMainPanel panel)
    {
        const MCOptionalRef<const MCScreenFlow::MCScreenKind> kind = MCScreenFlow::Kind(IdOf(panel));

        if (!kind.has_value() || kind->get().Prepare == nullptr)
        {
            return {};
        }

        // Copied: making the menus may register the kinds again.
        const std::function<std::vector<MCScreenChange::MCPiece>()> prepare = kind->get().Prepare;
        return prepare();
    }

    /// <summary>The panel made and read now if it isn't; none when the menus can't start or it can't be read.</summary>
    std::shared_ptr<MCMainMenuScreen> ReadyScreen(MCMainPanel panel)
    {
        if (std::shared_ptr<MCMainMenuScreen> made = MadeScreen(panel); made != nullptr && made->Ready())
        {
            return made;
        }

        MCScreenChange::RunPieces(PiecesOf(panel));
        std::shared_ptr<MCMainMenuScreen> made = MadeScreen(panel);

        if (made == nullptr || !made->Error().empty())
        {
            SDL_Log("Menus: the main menu's %.*s can't show: %s", static_cast<int>(InfoOf(panel).Kind.size()),
                    InfoOf(panel).Kind.data(), made != nullptr ? made->Error().c_str() : "the menus can't start");
            return nullptr;
        }

        return made;
    }

    /// <summary>The old CANCEL's way back to the main menu (its remade panel shows over it: MCLogistics::ShowLogScreen).</summary>
    void SetUpMainMenu()
    {
        WhackTimer = true;
        GlobalLog()->SetUpMainScreen(true);
        LoadingSolo = false;
    }

    /// <summary>The multiplayer game made and started, as 1998's MULTIPLAYER did (its connection screen's).</summary>
    void EnsureMultiplayer()
    {
        if (MultiPlayer() == nullptr)
        {
            MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
            MultiPlayer()->Start();
        }
    }
}

// ---- MCMainMenuScreen ----

MCMainMenuScreen::MCMainMenuScreen(MCMainPanel panel) : _Panel(panel)
{
    SetDocument(InfoOf(panel).Document);
}

MCMainMenuScreen::~MCMainMenuScreen() = default;

void MCMainMenuScreen::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, InfoOf(_Panel).Model);
    MCRmlModel& model = *_Model;
    model.Value("version", _Version);
    model.Value("update", _Update);
    model.Value("canUpdate", _CanUpdate);

    switch (_Panel)
    {
        case MCMainPanel::Main:
            model.Value("returns", _Returns);
            model.Value("returnLine", _ReturnLine);
            model.Value("continues", _Continues);
            model.Value("continueLine", _ContinueLine);
            model.Value("saves", _Saves);
            model.Value("solo", _Solo);
            break;
        case MCMainPanel::Campaigns:
        case MCMainPanel::Solo:
            model.List("rows", _Rows, std::pair{"title", &MCRow::Title}, std::pair{"line", &MCRow::Line});
            model.Value("picked", _Picked);
            model.Value("started", _Started);
            model.Value("mainWord", _MainWord);
            model.Event("pick",
                        [this](const Rml::VariantList& arguments)
                        {
                            if (!arguments.empty())
                            {
                                Pick(arguments.front().Get<int>());
                            }
                        });
            break;
        case MCMainPanel::Multiplayer:
            model.Value("network", _Network);
            model.Value("internet", _Internet);
            model.Value("online", _Online);
            break;
    }
}

void MCMainMenuScreen::OnLoaded(Rml::ElementDocument& /*document*/)
{
    const auto back = [this]
    {
        if (Back)
        {
            Back();
        }
    };

    OnClick(UpdateId, [] { MCUpdateScreen::OpenFromMenus(); });
    OnClick(BackId, back);

    switch (_Panel)
    {
        case MCMainPanel::Main:
            OnClick(ReturnId, [this] { ReturnToGame(); });
            OnClick(ContinueId, [this] { Continue(); });
            OnClick(CampaignsId, [] { MCMainMenuScreens::SwapTo(MCMainPanel::Campaigns); });
            OnClick(LoadId, [] { MCSavesMenu::OpenLoad(std::nullopt); });
            OnClick(SaveId,
                    []
                    {
                        if (GlobalLog() != nullptr && GlobalLog()->CurrentMission != -1)
                        {
                            MCScreenFlow::Open(MCMenuScreens::Save());
                        }
                    });
            OnClick(SoloId, [] { MCMainMenuScreens::SwapTo(MCMainPanel::Solo); });
            OnClick(MultiplayerId, [] { MCMainMenuScreens::SwapTo(MCMainPanel::Multiplayer); });
            OnClick(PreferencesId, [] { MCScreenFlow::Open(MCMenuScreens::Preferences()); });
            OnClick(CinemaId, [this] { Cinema(); });
            OnClick(QuitId, [] { MCMainMenuScreens::AskQuit(); });
            break;
        case MCMainPanel::Campaigns:
            OnClick(MainId,
                    [this]
                    {
                        if (_Started)
                        {
                            ContinueCampaign();
                        }
                        else
                        {
                            NewCampaign();
                        }
                    });
            OnClick(LoadId, [this] { LoadCampaign(); });
            OnClick(NewId, [this] { NewCampaign(); });
            break;
        case MCMainPanel::Solo:
            OnClick(MainId, [this] { PlaySolo(); });
            break;
        case MCMainPanel::Multiplayer:
            OnClick(InternetId, [] { MCScreenFlow::Open(MCMenuScreens::Internet()); });
            OnClick(LanId, [] { MCScreenFlow::Open(MCMenuScreens::Lan()); });
            OnClick(RetryId, [this] { LookForNetwork(); });
            break;
    }
}

void MCMainMenuScreen::OnShow()
{
    if (_Panel == MCMainPanel::Multiplayer)
    {
        // 1998's MULTIPLAYER: no single mission's game, the multiplayer game made as it opens (the main menu lets it
        // go).
        EnsureRegistryVersion();
        Solo = false;
        EnsureMultiplayer();

        if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr)
        {
            logistics->LogisticsState = MultiplayerState;
        }
    }

    Refresh();
    MCRmlDialogs::SetPlace(std::string(MainMenuPlace));
    // The views made from the model now, so the focus goes to a choice that shows (MCRmlScreen::Show).
    Layer().Context().Update();
}

void MCMainMenuScreen::OnHide()
{
    // Asked again as it shows.
    _Count.reset();
}

MCOptionalRef<Rml::Element> MCMainMenuScreen::FirstFocus(Rml::ElementDocument& document)
{
    // A list's picked row (CAMPAIGNS, SOLO MISSIONS), else the first choice that shows (the main menu's, MULTIPLAYER's),
    // else MULTIPLAYER's RETRY.
    if (_Panel == MCMainPanel::Campaigns || _Panel == MCMainPanel::Solo)
    {
        Rml::ElementList rows;

        if (Rml::Element* list = document.GetElementById(RowsId); list != nullptr)
        {
            list->GetElementsByClassName(rows, RowClass);
        }

        std::erase_if(rows, [](Rml::Element* row) { return !row->IsVisible(); });

        if (_Picked >= 0 && static_cast<size_t>(_Picked) < rows.size())
        {
            return *rows[static_cast<size_t>(_Picked)];
        }

        return std::nullopt;
    }

    Rml::ElementList choices;
    document.GetElementsByClassName(choices, ChoiceClass);

    for (Rml::Element* choice : choices)
    {
        if (choice->IsVisible())
        {
            return *choice;
        }
    }

    if (Rml::Element* retry = document.GetElementById(RetryId); retry != nullptr && retry->IsVisible())
    {
        return *retry;
    }

    return std::nullopt;
}

void MCMainMenuScreen::Pick(int row)
{
    _Picked = row >= 0 && static_cast<size_t>(row) < _Rows.size() ? row : -1;

    if (_Panel == MCMainPanel::Campaigns)
    {
        // A campaign with a save continues it (its LOAD and NEW CAMPAIGN above); one not started starts.
        _Started = _Picked >= 0 && !_CampaignLatest[static_cast<size_t>(_Picked)].empty();
        _MainWord = std::string(_Started ? ContinueWord : NewCampaignWord);
    }

    if (_Model != nullptr)
    {
        _Model->Changed();
    }
}

void MCMainMenuScreen::Refresh()
{
    switch (_Panel)
    {
        case MCMainPanel::Main:
            RefreshMain();
            break;
        case MCMainPanel::Campaigns:
            RefreshCampaigns();
            break;
        case MCMainPanel::Solo:
            RefreshSolo();
            break;
        case MCMainPanel::Multiplayer:
            RefreshMultiplayer();
            break;
    }

    RefreshFoot();

    if (_Model != nullptr)
    {
        _Model->Changed();
    }
}

void MCMainMenuScreen::RefreshMain()
{
    MCSaves& saves = MCSaves::Game();
    saves.Refresh();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    // Between missions (a campaign loaded): RETURN TO GAME leads, and SAVE GAME follows LOAD GAME (not after a solo
    // mission's, as 1998's SAVE MISSION).
    _Returns = logistics != nullptr && logistics->CurrentMission != -1;
    _Saves = _Returns && !Solo;
    _ReturnLine.clear();

    if (_Returns)
    {
        const MCSaves::MCSave playing = MCSaves::Unsaved(PlayedCampaign(), logistics->CurrentMission, 0);
        _ReturnLine = Solo ? std::string(SoloReturn)
                      : playing.Read
                          ? std::format(LineFormat, MCSaves::CampaignName(playing.Campaign),
                                        Stands(playing.Operation, playing.Mission))
                          : std::string(MCSaves::CampaignName(playing.Campaign));
    }

    const MCOptionalRef<const MCSaves::MCSave> latest = saves.Latest();
    _Continues = !_Returns && latest.has_value();
    _Latest = latest.has_value() ? latest->get().Name : std::string();
    _ContinueLine = latest.has_value() ? OpensAt(latest->get()) : std::string();
    _Solo = !MCFileSystem::FindFileSources(GamePath(SavePath, "*", ".sol")).empty();
}

void MCMainMenuScreen::RefreshCampaigns()
{
    MCSaves& saves = MCSaves::Game();
    saves.Refresh();
    _Rows.clear();
    _CampaignLatest.clear();
    std::optional<size_t> newest;
    SDL_Time newestSaved = 0;

    for (size_t index = 0; index < Campaigns.size(); index++)
    {
        const MCCampaign campaign = Campaigns[index];
        const MCOptionalRef<const MCSaves::MCSave> latest = saves.Latest(campaign);
        const std::string stands = latest.has_value()
                                       ? Stands(latest->get().OpensOperation, latest->get().OpensMission)
                                       : std::string(MCMainMenuScreens::NotStarted);
        _Rows.push_back({.Title = std::string(MCSaves::CampaignName(campaign)),
                         .Line = std::format(LineFormat, CampaignTitles[index], stands)});
        _CampaignLatest.push_back(latest.has_value() ? latest->get().Name : std::string());

        if (latest.has_value() && (!newest.has_value() || latest->get().Saved > newestSaved))
        {
            newest = index;
            newestSaved = latest->get().Saved;
        }
    }

    // Picked as it opens: the campaign of the latest save, else the original (a pick kept while it stays open, as
    // across a campaign's LOAD and back).
    Pick(_Picked >= 0 ? _Picked : static_cast<int>(newest.value_or(0)));
}

void MCMainMenuScreen::RefreshSolo()
{
    _Rows.clear();
    _SoloNames.clear();

    for (const MCSaves::MCSolo& solo : MCSaves::SoloMissions())
    {
        // One that can't be read is listed by its file's name alone.
        _Rows.push_back({.Title = solo.Read ? solo.MissionName : solo.Name,
                         .Line = solo.Read ? std::format(SoloFormat, solo.Tons, MCSaves::PointsText(solo.ResourcePoints))
                                           : std::string()});
        _SoloNames.push_back(solo.Name);
    }

    Pick(_Picked >= 0 && static_cast<size_t>(_Picked) < _Rows.size() ? _Picked : (_Rows.empty() ? -1 : 0));
}

void MCMainMenuScreen::RefreshMultiplayer()
{
    LookForNetwork();
}

void MCMainMenuScreen::LookForNetwork()
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const bool sockets = game != nullptr && game->SessionManager != nullptr && game->SessionManager->IsTcpAvailable();
    _Network = NetworkForTests.value_or(sockets && MCSocketTransport::HasNetwork());
    _Internet = HasInternetGames();
    _Online = std::string(MCMainMenuScreens::InternetLine);
    _Count.reset();

    if (!_Network)
    {
        SDL_Log("Menus: MULTIPLAYER finds no network (%s)", sockets ? "no interface is up" : "sockets can't start");
    }

    // The count asked once as it opens, shown when it comes; the line stays when the service can't be reached.
    if (_Network && _Internet)
    {
        _Count = MCInternetCount::Ask();
    }

    if (_Model != nullptr)
    {
        _Model->Changed();
    }
}

void MCMainMenuScreen::RefreshFoot()
{
    std::string version = VersionLineText(MCUpdater::RunningVersion());
    const bool canUpdate = MCUpdater::Available() && MCUpdater::Waiting();
    std::string update =
        canUpdate ? std::format(UpdateFormat, ReleaseOf(MCUpdater::Status().Version)) : std::string();

    if (version != _Version || canUpdate != _CanUpdate || update != _Update)
    {
        _Version = std::move(version);
        _CanUpdate = canUpdate;
        _Update = std::move(update);

        if (_Model != nullptr)
        {
            _Model->Changed();
        }
    }
}

void MCMainMenuScreen::OnFrame()
{
    RefreshFoot();

    if (_Count != nullptr && _Count->Done())
    {
        if (const std::optional<int32_t> online = _Count->Online(); online.has_value())
        {
            _Online = *online == 1 ? std::string(OneOnline) : std::format(OnlineFormat, *online);

            if (_Model != nullptr)
            {
                _Model->Changed("online");
            }
        }

        _Count.reset();
    }

    if (_Panel != MCMainPanel::Main)
    {
        return;
    }

    // The submenus read ahead, a piece a frame from the main menu's second frame, so each swaps in whole at its press.
    if (!_AheadQueued)
    {
        _AheadQueued = true;

        for (const MCMainPanel panel : Submenus)
        {
            for (MCScreenChange::MCPiece& piece : PiecesOf(panel))
            {
                _Ahead.push_back(std::move(piece));
            }
        }

        return;
    }

    if (!_Ahead.empty())
    {
        const MCScreenChange::MCPiece piece = std::move(_Ahead.front());
        _Ahead.pop_front();
        MCScreenChange::RunPieces(std::span(&piece, 1));
    }
}

void MCMainMenuScreen::OnDestroy()
{
    _Count.reset();
    _Ahead.clear();
    _Model.reset();
    MCRmlScreen::OnDestroy();
}

void MCMainMenuScreen::ReleaseCount()
{
    _Count.reset();
}

void MCMainMenuScreen::Play(std::string name, bool campaign, std::string change)
{
    const std::weak_ptr<MCRmlScreen> self = weak_from_this();
    MCScreenFlow::Change(std::move(change),
                         [self, name = std::move(name), campaign]
                         {
                             const std::shared_ptr<MCRmlScreen> screen = self.lock();

                             if (screen == nullptr)
                             {
                                 return;
                             }

                             screen->Hide();
                             LoadingSolo = false;

                             if (LoadSavedGame(name, campaign))
                             {
                                 return;
                             }

                             // Refused: the panel again, saying so.
                             SDL_Log("Menus: the save %s couldn't be loaded", name.c_str());
                             screen->Show();
                             MCRmlDialogs::Show(
                                 MCRmlDialog::MCSay{.Text = std::string(MCSavesMenu::RefusedText)});
                         });
}

void MCMainMenuScreen::Continue()
{
    if (!_Latest.empty())
    {
        Play(_Latest, true, "continue");
    }
}

void MCMainMenuScreen::ReturnToGame()
{
    MCScreenFlow::Change("return to game",
                         []
                         {
                             MCMainMenuScreens::HideAll();
                             ::ReturnToGame();
                         });
}

void MCMainMenuScreen::Cinema()
{
    MCScreenFlow::Change("cinema",
                         []
                         {
                             MCMainMenuScreens::HideAll();
                             ReplayCinema();
                         });
}

void MCMainMenuScreen::ContinueCampaign()
{
    if (_Picked >= 0 && static_cast<size_t>(_Picked) < _CampaignLatest.size() &&
        !_CampaignLatest[static_cast<size_t>(_Picked)].empty())
    {
        Play(_CampaignLatest[static_cast<size_t>(_Picked)], true, "continue campaign");
    }
}

void MCMainMenuScreen::LoadCampaign()
{
    if (_Picked >= 0 && static_cast<size_t>(_Picked) < Campaigns.size())
    {
        MCSavesMenu::OpenLoad(Campaigns[static_cast<size_t>(_Picked)]);
    }
}

void MCMainMenuScreen::NewCampaign()
{
    // 1998's NEW CAMPAIGN and the expansion's (logistics/MCMainMenu.cpp): Betty's line, then the first briefing.
    if (_Picked >= 0 && static_cast<size_t>(_Picked) < Campaigns.size())
    {
        if (Campaigns[static_cast<size_t>(_Picked)] == MCCampaign::Expansion)
        {
            NewMcxCampaign();
        }
        else
        {
            ::NewCampaign();
        }
    }
}

void MCMainMenuScreen::PlaySolo()
{
    if (_Picked >= 0 && static_cast<size_t>(_Picked) < _SoloNames.size())
    {
        Play(_SoloNames[static_cast<size_t>(_Picked)], false, "solo mission");
    }
}

// ---- MCMainMenuScreens ----

MCScreenFlow::MCScreenKind MCMainMenuScreens::Kind(MCMainPanel panel)
{
    MCScreenFlow::MCScreenKind kind = MCRmlFlow::Kind(
        std::string(InfoOf(panel).Kind),
        [panel](MCRmlLayer& layer)
        {
            return MCGuiHandle<MCRmlScreen>(layer.CreateChild<MCMainMenuScreen>(
                [panel](MCMainMenuScreen& made)
                {
                    // The main menu's Escape is the stack's end (QUIT's question); a submenu's the main menu again.
                    made.Back = panel == MCMainPanel::Main ? std::function<void()>([] { MCScreenFlow::Back(); })
                                                           : std::function<void()>([panel] { SwapBack(panel); });
                },
                panel));
        },
        MCRmlFlow::MCKeep::Kept);

    if (panel != MCMainPanel::Main)
    {
        kind.Leave = [panel] { SwapBack(panel); };
    }

    // SOLO MISSIONS gives way to the briefing its PLAY opens (Escape there goes to the main menu, as 1998's LOAD
    // MISSION's); CAMPAIGNS stays under a campaign's LOAD GAME, which goes back to it.
    kind.GivesWay = panel == MCMainPanel::Solo;
    return kind;
}

MCOptionalRef<MCMainMenuScreen> MCMainMenuScreens::Screen(MCMainPanel panel)
{
    const std::shared_ptr<MCMainMenuScreen> made = MadeScreen(panel);
    return made != nullptr ? MCOptionalRef<MCMainMenuScreen>(*made) : std::nullopt;
}

std::vector<MCScreenChange::MCPiece> MCMainMenuScreens::Prepare(MCMainPanel panel)
{
    return PiecesOf(panel);
}

bool MCMainMenuScreens::Ready(MCMainPanel panel)
{
    const std::shared_ptr<MCMainMenuScreen> made = MadeScreen(panel);
    return made == nullptr || made->Ready() || !made->Error().empty();
}

bool MCMainMenuScreens::ShowNow(MCMainPanel panel)
{
    const std::shared_ptr<MCMainMenuScreen> screen = ReadyScreen(panel);

    if (screen == nullptr)
    {
        return false;
    }

    // Shown already on top: as it is.
    if (const MCOptionalRef<MCRmlScreen> top = screen->Layer().Top();
        screen->Shown() && top.has_value() && &top->get() == screen.get())
    {
        return true;
    }

    // As an old screen coming up does (MCRmlFlow::OldScreenShown), the remade screens over the main menu go.
    screen->Layer().HideAll();
    screen->Show();
    MCScreenFlow::Noted();
    return true;
}

bool MCMainMenuScreens::ShowMain()
{
    return !OldForTests && ShowNow(MCMainPanel::Main);
}

void MCMainMenuScreens::SwapTo(MCMainPanel panel)
{
    // A change of screen running: the press goes no further.
    if (MCScreenChange::Running())
    {
        return;
    }

    const std::shared_ptr<MCMainMenuScreen> screen = ReadyScreen(panel);

    if (screen == nullptr)
    {
        return;
    }

    // Opened from the main menu: on the list's first pick (the latest save's campaign, the first mission).
    screen->Pick(-1);
    screen->Show();

    if (const std::shared_ptr<MCMainMenuScreen> main = MadeScreen(MCMainPanel::Main); main != nullptr)
    {
        main->Hide();
    }

    MCScreenFlow::NoteShown(IdOf(panel));
}

void MCMainMenuScreens::SwapBack(MCMainPanel /*panel*/)
{
    if (MCScreenChange::Running())
    {
        return;
    }

    // The main menu set up again, as 1998's CANCEL did (the multiplayer game let go): its panel in this one's place.
    SetUpMainMenu();
}

void MCMainMenuScreens::HideAll()
{
    for (const MCPanelInfo& info : Panels)
    {
        if (const std::shared_ptr<MCMainMenuScreen> screen = MadeScreen(info.Panel);
            screen != nullptr && screen->Shown())
        {
            screen->Hide();
        }
    }
}

bool MCMainMenuScreens::Shown(MCMainPanel panel)
{
    const std::shared_ptr<MCMainMenuScreen> screen = MadeScreen(panel);
    return screen != nullptr && screen->Shown();
}

bool MCMainMenuScreens::MainOnTop()
{
    const std::shared_ptr<MCMainMenuScreen> screen = MadeScreen(MCMainPanel::Main);

    if (screen == nullptr || !screen->Shown())
    {
        return false;
    }

    const MCOptionalRef<MCRmlScreen> top = screen->Layer().Top();
    return top.has_value() && &top->get() == screen.get();
}

bool MCMainMenuScreens::AskQuit()
{
    const std::shared_ptr<MCMainMenuScreen> screen = MadeScreen(MCMainPanel::Main);

    if (screen == nullptr || !screen->Shown())
    {
        return false;
    }

    return static_cast<bool>(MCQuestionWindow::Ask(*screen, std::string(MCMenuScreens::QuitQuestion),
                                                   std::string(QuitWord), GameOverMan, {}));
}

void MCMainMenuScreens::Release()
{
    if (const std::shared_ptr<MCMainMenuScreen> screen = MadeScreen(MCMainPanel::Multiplayer); screen != nullptr)
    {
        screen->ReleaseCount();
    }
}

void MCMainMenuScreens::SetNetworkForTests(std::optional<bool> network)
{
    NetworkForTests = network;
}

void MCMainMenuScreens::SetOldForTests(bool old)
{
    OldForTests = old;
}
