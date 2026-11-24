#pragma once

// Port-only (SPEC-menus.md, "Into a mission and back"; stage 8, step 1): the loading screen as designed, one RmlUi screen
// (ui/loading.rml) over the main menu's painting: the mission's name and where it is, its objectives and the orders'
// first lines, its map, the load's progress along the foot; nothing to press. MCMission::ShowLoadingScreen shows it in
// the dark after BEGIN MISSION, read from the briefing before logistics goes; the load's frames move its meter
// (gui/MCUpdateDisplay's bar); MCMission::TakeDownLoadingScreen hides it in the dark before the battlefield.

#include "ui/MCRmlScreen.h"

class MCRmlModel;

/// <summary>The loading screen.</summary>
class MCLoadingScreen final : public MCRmlScreen
{
public:
    /// <summary>A line it shows: an objective with its number, a paragraph of the orders.</summary>
    struct MCLine
    {
        std::string Text;
        int Number = 0;
    };

    MCLoadingScreen();
    ~MCLoadingScreen() override;

    /// <summary>The screen set up for the mission logistics has now, its meter empty.</summary>
    void SetUp();
    /// <summary>The load's progress, a percentage (the bar's).</summary>
    void SetProgress(int percent);

    /// <summary>What shows (the tests read it).</summary>
    const std::string& Eyebrow() const { return _Eyebrow; }
    const std::string& Title() const { return _Title; }
    const std::vector<MCLine>& Objectives() const { return _Objectives; }
    const std::vector<MCLine>& Orders() const { return _Orders; }
    const std::string& MapSource() const { return _Map; }
    int Percent() const { return _Percent; }

    void OnDestroy() override;

protected:
    void MakeModels(Rml::Context& context) override;

private:
    std::unique_ptr<MCRmlModel> _Model;
    std::string _Eyebrow;
    std::string _Title;
    std::vector<MCLine> _Objectives;
    std::vector<MCLine> _Orders;
    /// <summary>The map as a decorator (none: no map).</summary>
    std::string _Map;
    std::string _What;
    int _Percent = 0;
};

namespace MCLoadingMenu
{
    /// <summary>
    /// The loading screen shown for the mission logistics has now, its meter empty, when the remade screens are wanted
    /// (MCCampaignMenu::Wanted); false when it isn't shown (the 1998 one is drawn then).
    /// </summary>
    bool Show();

    /// <summary>The load's progress (a percentage), shown on its meter while it shows.</summary>
    void Progress(int percent);

    /// <summary>The screen hidden (the battlefield comes in).</summary>
    void Hide();

    /// <summary>The screen while the menus have it, and whether it shows.</summary>
    MCOptionalRef<MCLoadingScreen> Screen();
    bool Shown();

    /// <summary>The words: over the title, what loads.</summary>
    inline constexpr std::string_view EyebrowFormat = "Operation {} · {} · Mission {}";
    inline constexpr std::string_view LoadingText = "Loading the battlefield";
    /// <summary>The orders' first paragraphs it shows (the frames' two).</summary>
    inline constexpr size_t OrdersShown = 2;
}
