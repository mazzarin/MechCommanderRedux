#pragma once

#include "gui/MCScreenFlow.h"
#include "logistics/MCMainMenuModel.h"
#include "ui/MCRmlScreen.h"

class MCInternetCount;
class MCRmlModel;

/// <summary>The main menu's panels: the menu itself and the submenus shown in its place.</summary>
enum class MCMainPanel : uint8_t
{
    Main,
    Campaigns,
    Solo,
    Multiplayer,
};

/// <summary>
/// Port-only (SPEC-menus.md, "Main menu" and "MULTIPLAYER"; stage 7): the main menu remade, a panel in the painting's
/// window (<c>ui/main_menu*.rml</c>, one template), its version line and UPDATE at its foot (and the HD art's line while
/// the art a program installed alone came for downloads, with RESTART once it's in). Four screens of the flow,
/// each a document on the template: the main menu (CONTINUE, CAMPAIGNS, LOAD GAME, SOLO MISSIONS, MULTIPLAYER,
/// PREFERENCES, ORIGINAL CINEMA, QUIT; RETURN TO GAME and SAVE GAME between missions), and its submenus CAMPAIGNS,
/// SOLO MISSIONS and MULTIPLAYER, swapped in its place from it with no fade, the painting staying. Each reads what it
/// shows as it shows: the saves (logistics/MCSaves), the single missions, the network and the online count. The 1998
/// main menu (mainScreen.fit) stays logistics' main screen, hidden under it (MCLogistics::ShowLogScreen), until stage 9.
/// What each shows and what its choices do is the model's (logistics/MCMainMenuModel.h); the screen is its view.
/// </summary>
class MCMainMenuScreen final : public MCRmlScreen
{
public:
    explicit MCMainMenuScreen(MCMainPanel panel);
    ~MCMainMenuScreen() override;

    MCMainPanel Panel() const { return _Panel; }

    /// <summary>A row of CAMPAIGNS or SOLO MISSIONS, as it shows: its name and the line under it.</summary>
    using MCRow = MCMainMenuModel::MCRow;

    /// <summary>The rows shown (CAMPAIGNS', SOLO MISSIONS'), and the picked one's index (-1 for none).</summary>
    const std::vector<MCRow>& Rows() const { return _Rows; }
    int Picked() const { return _Picked; }
    /// <summary>Picks row <paramref name="row"/>, as a click does.</summary>
    void Pick(int row);

    /// <summary>The main menu's CONTINUE line ("Original campaign · Operation 2, mission 3"); empty without a save.</summary>
    const std::string& ContinueLine() const { return _ContinueLine; }
    /// <summary>MULTIPLAYER's INTERNET line ("58 players online"), and whether it says there's no network.</summary>
    const std::string& OnlineLine() const { return _Online; }
    bool Offline() const { return !_Network; }

    /// <summary>The panel read again from the game as it is now (as it shows).</summary>
    void Refresh();

    void OnFrame() override;
    void OnDestroy() override;
    /// <summary>The online count let go (the game quitting: its thread kept before the quit joins it).</summary>
    void ReleaseCount();

protected:
    void MakeModels(Rml::Context& context) override;
    void OnLoaded(Rml::ElementDocument& document) override;
    void OnShow() override;
    void OnHide() override;
    MCOptionalRef<Rml::Element> FirstFocus(Rml::ElementDocument& document) override;

private:
    /// <summary>Each panel's part of <see cref="Refresh"/>.</summary>
    void RefreshMain();
    void RefreshCampaigns();
    void RefreshSolo();
    void RefreshMultiplayer();
    /// <summary>The version line, the HD art's line with its RESTART, and UPDATE, set only where they changed.</summary>
    void RefreshFoot();
    /// <summary>The main menu's choices pressed.</summary>
    void Continue();
    void ReturnToGame();
    void Cinema();
    /// <summary>CAMPAIGNS' buttons on the picked campaign.</summary>
    void ContinueCampaign();
    void LoadCampaign();
    void NewCampaign();
    /// <summary>SOLO MISSIONS' PLAY: the picked one's briefing.</summary>
    void PlaySolo();
    /// <summary>MULTIPLAYER: the network looked at again, the count asked for.</summary>
    void LookForNetwork();
    /// <summary>The save <paramref name="name"/> played (CONTINUE, PLAY), in a change; refused, said over the panel.</summary>
    void Play(std::string name, bool campaign, std::string change);

    MCMainPanel _Panel;
    std::unique_ptr<MCRmlModel> _Model;
    std::unique_ptr<MCInternetCount> _Count;
    /// <summary>The submenus' pieces left to prepare while the main menu shows (one a frame, after its first).</summary>
    std::deque<MCScreenChange::MCPiece> _Ahead;
    bool _AheadQueued = false;
    /// <summary>The save CONTINUE plays, the campaigns' rows' campaigns and latest saves, the single missions' files.</summary>
    std::string _Latest;
    std::vector<MCSaves::MCCampaign> _Campaigns;
    std::vector<std::string> _CampaignLatest;
    std::vector<std::string> _SoloNames;

    // The model's values.
    std::vector<MCRow> _Rows;
    int _Picked = -1;
    std::string _Version;
    std::string _Update;
    bool _CanUpdate = false;
    std::string _Art;
    bool _HasArt = false;
    bool _CanRestart = false;
    bool _Returns = false;
    std::string _ReturnLine;
    bool _Continues = false;
    std::string _ContinueLine;
    bool _Saves = false;
    bool _Solo = false;
    bool _Started = false;
    std::string _MainWord;
    bool _Network = true;
    bool _Internet = false;
    std::string _Online;
};

namespace MCMainMenuScreens
{
    /// <summary>
    /// The flow's kind for <paramref name="panel"/> ("main", "campaigns", "solo missions", "multiplayer"): kept, read
    /// once; a submenu's way back swaps the main menu in again.
    /// </summary>
    MCScreenFlow::MCScreenKind Kind(MCMainPanel panel);

    /// <summary>The panel's screen while made.</summary>
    MCOptionalRef<MCMainMenuScreen> Screen(MCMainPanel panel);

    /// <summary>
    /// The panel's preparation, a piece a frame (made first, if it isn't): a change's job that shows it next (the
    /// intro's end); none when the menus can't start.
    /// </summary>
    std::vector<MCScreenChange::MCPiece> Prepare(MCMainPanel panel);

    /// <summary>Whether the panel's first frame is whole, or it can't be made (nothing to wait for).</summary>
    bool Ready(MCMainPanel panel);

    /// <summary>
    /// Logistics shows its main screen (MCLogistics::ShowLogScreen): the remade main menu shown at once in its place,
    /// another panel shown hidden; false when the menus' RmlUi can't start or its document can't be read (the 1998
    /// screen shows).
    /// </summary>
    bool ShowMain();

    /// <summary>
    /// <paramref name="panel"/> shown at once, the panel shown before hidden (a change's dark work, a landing); false when
    /// it can't be made.
    /// </summary>
    bool ShowNow(MCMainPanel panel);

    /// <summary>A submenu swapped in for the main menu, in place, with no fade (its choice pressed).</summary>
    void SwapTo(MCMainPanel panel);

    /// <summary>A submenu's BACK and Escape: the main menu swapped in again, in place (the multiplayer game let go).</summary>
    void SwapBack(MCMainPanel panel);

    /// <summary>Every panel hidden (another screen shows).</summary>
    void HideAll();

    /// <summary>Whether <paramref name="panel"/> shows.</summary>
    bool Shown(MCMainPanel panel);

    /// <summary>Whether the main menu shows with nothing over it: the reports' question and the notices' turn.</summary>
    bool MainOnTop();

    /// <summary>QUIT and Escape on the main menu: "Quit the game?" over it; false when it doesn't show (the old question).</summary>
    bool AskQuit();

    /// <summary>The online count let go (ReleaseInternetMenus, at the game's quit).</summary>
    void Release();

    /// <summary>
    /// For the tests of 1998's main menu (mainScreen.fit, kept until stage 9): logistics' main screen shows as when the
    /// menus' RmlUi can't start, the remade main menu not shown in its place.
    /// </summary>
    void SetOldForTests(bool old);
}
