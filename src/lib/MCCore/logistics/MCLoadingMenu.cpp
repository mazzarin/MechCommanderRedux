#include "stdafx.h"
#include "logistics/MCLoadingMenu.h"
#include "logistics/MCCampaignMenu.h"
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
    model.List("objectives", _Objectives, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number});
    model.List("orders", _Orders, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number});
    model.Value("map", _Map);
    model.Value("what", _What);
    model.Value("percent", _Percent);
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
        _Objectives.push_back(MCLine{.Text = mission.Briefing.Objectives[at], .Number = static_cast<int>(at + 1)});
    }

    _Orders.clear();

    for (size_t at = 0; at < std::min(mission.Briefing.Orders.size(), MCLoadingMenu::OrdersShown); ++at)
    {
        _Orders.push_back(MCLine{.Text = mission.Briefing.Orders[at]});
    }

    _Map = mission.MapSource.empty() ? std::string(MCCampaignScreen::NoPicture)
                                     : std::format("image({} contain center top)", mission.MapSource);
    _What = MCLoadingMenu::LoadingText;
    _Percent = 0;

    if (_Model != nullptr)
    {
        _Model->Changed();
    }
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

bool MCLoadingMenu::Show()
{
    const MCOptionalRef<MCLoadingScreen> made =
        MCCampaignMenu::Wanted() ? MCRmlFlow::MadeNow(TheScreen(), "the loading screen") : std::nullopt;

    if (!made.has_value())
    {
        return false;
    }

    MCLoadingScreen& screen = made->get();
    screen.SetUp();
    screen.Layer().Context().Update();
    screen.Show();
    return true;
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
