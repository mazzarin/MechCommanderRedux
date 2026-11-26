#include "stdafx.h"
#include "logistics/MCLoadingMenu.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCRoomScreen.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlModel.h"

#include <RmlUi/Core/Context.h>

namespace
{
    constexpr const char* ScreenDocument = "loading.rml";
    constexpr const char* ScreenModel = "loading";
    constexpr int WholePercent = 100;

    MCGuiHandle<MCLoadingScreen>& TheScreen()
    {
        static MCGuiHandle<MCLoadingScreen> screen;
        return screen;
    }
}

MCLoadingScreen::MCLoadingScreen()
{
    SetDocument(ScreenDocument);
    // Nothing to press: Escape does nothing while the mission loads.
    Back = [] {};
    // No map until it's set up (the bound decorator is read as the document loads).
    _Map = MCCampaignScreen::NoPicture;
}

MCLoadingScreen::~MCLoadingScreen() = default;

void MCLoadingScreen::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, ScreenModel);
    MCRmlModel& model = *_Model;
    model.Value("eyebrow", _Eyebrow);
    model.Value("title", _Title);
    model.Value("timeLimit", _TimeLimit);
    model.List("objectives", _Objectives, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number},
               std::pair{"limit", &MCLine::Limit});
    model.List("orders", _Orders, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number},
               std::pair{"limit", &MCLine::Limit});
    model.Value("map", _Map);
    model.Value("what", _What);
    model.Value("percent", _Percent);
    model.Value("match", _Match);
    model.List("players", _Players, std::pair{"name", &MCPlayerRow::Name}, std::pair{"colour", &MCPlayerRow::Colour},
               std::pair{"in", &MCPlayerRow::In}, std::pair{"word", &MCPlayerRow::Word});
    model.Value("count", _Count);
}

void MCLoadingScreen::OnDestroy()
{
    _Model.reset();
    MCRmlScreen::OnDestroy();
}

void MCLoadingScreen::SetUp()
{
    const MCCampaignMenu::MCMissionRead mission = MCCampaignMenu::ReadMission();
    _Title = mission.Title;
    _Eyebrow = mission.Mission > 0 ? std::format(MCLoadingMenu::EyebrowFormat, mission.Operation, mission.OperationName,
                                                 mission.Mission)
                                   : mission.OperationName;
    _Objectives.clear();

    for (size_t at = 0; at < mission.Briefing.Objectives.size(); ++at)
    {
        _Objectives.push_back(MCLine{.Text = mission.Briefing.Objectives[at].Text,
                                     .Number = static_cast<int>(at + 1),
                                     .Limit = mission.Briefing.Objectives[at].TimeLimit});
    }

    _TimeLimit = mission.Briefing.TimeLimit;

    _Orders.clear();

    for (size_t at = 0; at < std::min(mission.Briefing.Orders.size(), MCLoadingMenu::OrdersShown); ++at)
    {
        _Orders.push_back(MCLine{.Text = mission.Briefing.Orders[at]});
    }

    _Map = mission.MapSource.empty() ? std::string(MCCampaignScreen::NoPicture)
                                     : std::format("image({} contain center top)", mission.MapSource);
    _What = MCLoadingMenu::LoadingText;
    _Percent = 0;
    _Loaded = false;
    _Who.clear();
    _Players.clear();
    _Count.clear();
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    _Match = game != nullptr && logistics != nullptr;

    // A match: its room over the title, its players read while the room's model is there (logistics goes in the load).
    if (_Match)
    {
        _Host = game->IsServer;
        _Eyebrow = std::format(MCLoadingMenu::MatchEyebrowFormat, RoomPlace(), logistics->Room.Name());

        for (const MCCampaignMenu::MCMatchPlayer& player : MCCampaignMenu::MatchPlayers())
        {
            _Who.push_back(MCWho{.Id = player.Id, .Me = player.Me});
            _Players.push_back(MCPlayerRow{.Name = player.Name, .Colour = player.Colour});
        }

        FillPlayers();
    }

    if (_Model != nullptr)
    {
        _Model->Changed();
    }
}

void MCLoadingScreen::SetLoaded()
{
    _Loaded = true;

    if (FillPlayers() && _Model != nullptr)
    {
        _Model->Changed();
    }
}

void MCLoadingScreen::OnFrame()
{
    if (_Match && FillPlayers() && _Model != nullptr)
    {
        _Model->Changed();
    }
}

bool MCLoadingScreen::FillPlayers()
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();

    // The match gone (left, or lost): the screen stays as it was until it fades out.
    if (!_Match || game == nullptr)
    {
        return false;
    }

    std::vector<MCPlayerRow> players = _Players;
    std::vector<std::string> waitingFor;
    size_t in = 0;

    for (size_t at = 0; at < players.size(); ++at)
    {
        const MCWho& who = _Who[at];
        MCPlayerRow& row = players[at];
        // Once in, in (the host's count is cleared as the match starts); the start says every game is.
        row.In =
            row.In || game->InMission ||
            (_Host ? std::ranges::contains(game->PlayerCheckedIn, static_cast<int32_t>(who.Id)) : who.Me && _Loaded);
        row.Word = std::string(row.In  ? MCLoadingMenu::InWord
                               : _Host ? MCLoadingMenu::LoadingWord
                                       : std::string_view());
        in += row.In ? 1 : 0;

        if (!row.In)
        {
            waitingFor.push_back(row.Name);
        }
    }

    std::string count = _Host ? std::format(MCLoadingMenu::CountFormat, in, players.size()) : std::string();
    std::string what = _What;

    // The foot once this game is in, until the match starts (it keeps its words as the screen fades out).
    if (_Loaded && !game->InMission)
    {
        what = _Host && !waitingFor.empty() ? std::format(MCLoadingMenu::WaitingForFormat, MCNamesJoined(waitingFor))
               : _Host                      ? _What
                                            : std::string(MCLoadingMenu::WaitingForOthersText);
    }

    if (players == _Players && count == _Count && what == _What)
    {
        return false;
    }

    _Players = std::move(players);
    _Count = std::move(count);
    _What = std::move(what);
    return true;
}

void MCLoadingScreen::SetProgress(int percent)
{
    percent = std::clamp(percent, 0, WholePercent);

    if (percent == _Percent)
    {
        return;
    }

    _Percent = percent;

    if (_Model != nullptr)
    {
        _Model->Changed("percent");
    }
}

// MCLoadingMenu

void MCLoadingMenu::Show()
{
    MCLoadingScreen& screen = MCRmlFlow::Needed(TheScreen(), "the loading screen");
    screen.SetUp();
    screen.Layer().Context().Update();
    screen.Show();
}

void MCLoadingMenu::Loaded()
{
    if (const MCOptionalRef<MCLoadingScreen> screen = Screen(); screen.has_value() && screen->get().Shown())
    {
        screen->get().SetLoaded();
    }
}

bool MCLoadingMenu::HoldsForMatch()
{
    return Shown() && MultiPlayer() != nullptr;
}

void MCLoadingMenu::Progress(int percent)
{
    if (const MCOptionalRef<MCLoadingScreen> screen = Screen(); screen.has_value() && screen->get().Shown())
    {
        screen->get().SetProgress(percent);
    }
}

void MCLoadingMenu::Hide()
{
    if (const MCOptionalRef<MCLoadingScreen> screen = Screen(); screen.has_value() && screen->get().Shown())
    {
        screen->get().Hide();
    }
}

MCOptionalRef<MCLoadingScreen> MCLoadingMenu::Screen()
{
    return MCRmlFlow::Alive(TheScreen());
}

bool MCLoadingMenu::Shown()
{
    const MCOptionalRef<MCLoadingScreen> screen = Screen();
    return screen.has_value() && screen->get().Shown();
}
