#pragma once

// Port-only (SPEC-menus.md, stage 5): LOAD GAME and SAVE GAME remade, one RmlUi screen (ui/saves.rml) viewing the saves
// list (logistics/MCSaves) in either mode, and its window (ui/saves_dialog.rml) for DELETE's question, an overwrite's
// and the messages. The flow's "load" and "save" kinds (logistics/MCMenuScreens): opened from the main menu's LOAD GAME
// and SAVE GAME, LOAD going to where the save stands through the old briefing (MCLoadSaveMenu's LoadSavedGame), SAVE
// back to the campaign screen it was left from, BACK to the main menu.

#include "gui/MCScreenFlow.h"
#include "logistics/MCSaves.h"
#include "ui/MCRmlScreen.h"

class MCGuiCallback;
class MCRmlModel;

/// <summary>The saves dialog: a question with CANCEL and its answer, or a message with OK.</summary>
class MCSavesDialog final : public MCRmlScreen
{
public:
    MCSavesDialog();
    ~MCSavesDialog() override;

    /// <summary>What the window says and does.</summary>
    struct MCSay
    {
        std::string Question;
        std::string First;
        std::string Second;
        /// <summary>The answer's button ("Delete", "OK"); with <see cref="Asks"/>, CANCEL before it.</summary>
        std::string Answer;
        bool Asks = false;
        /// <summary>Run as the answer is pressed, the window hidden first.</summary>
        std::function<void()> Answered;
        /// <summary>A message's seconds before it closes itself, as if answered; 0 for none.</summary>
        double ClosesAfter = 0;
    };

    /// <summary>Shows the window saying <paramref name="say"/> over its screen.</summary>
    void Ask(MCSay say);

    /// <summary>What it says now (for the tests).</summary>
    const MCSay& Saying() const { return _Say; }

    /// <summary>The answer pressed: hidden, then its action.</summary>
    void Answer();

    void OnDestroy() override;

protected:
    void MakeModels(Rml::Context& context) override;
    void OnLoaded(Rml::ElementDocument& document) override;

private:
    /// <summary>Each frame while a message that closes itself shows: closed once its time is up.</summary>
    void Tick();

    std::unique_ptr<MCRmlModel> _Model;
    MCSay _Say;
    double _ShownAt = 0;
    std::shared_ptr<MCGuiCallback> _Ticker;
};

/// <summary>LOAD GAME and SAVE GAME.</summary>
class MCSavesScreen final : public MCRmlScreen
{
public:
    enum class MCMode : uint8_t
    {
        Load,
        Save,
    };

    /// <summary>A row of the list, as it shows.</summary>
    struct MCRow
    {
        std::string Mission;
        std::string Campaign;
        std::string When;
        std::string Points;
        std::string Date;
    };

    MCSavesScreen();
    ~MCSavesScreen() override;

    /// <summary>
    /// The screen set for <paramref name="mode"/>, the saves read again and the list on <paramref name="campaign"/>
    /// (every campaign's without one): LOAD GAME with its newest save picked, SAVE GAME with none and the first free
    /// name typed.
    /// </summary>
    void SetUp(MCMode mode, std::optional<MCSaves::MCCampaign> campaign);

    MCMode Mode() const { return _Mode; }

    /// <summary>The list's rows, the picked one's index (-1 for none) and the save's name it stands for.</summary>
    const std::vector<MCRow>& Rows() const { return _Rows; }
    int Picked() const { return _Picked; }
    std::optional<std::string> PickedSave() const;

    /// <summary>The campaign shown (none: ALL).</summary>
    std::optional<MCSaves::MCCampaign> Campaign() const { return _Campaign; }

    /// <summary>The name SAVE GAME's box holds.</summary>
    const std::string& Name() const { return _Name; }

    /// <summary>The picked save's map (its picture's source; empty with none).</summary>
    const std::string& MapSource() const { return _MapSource; }

    /// <summary>The screen's window (made on first use).</summary>
    MCGuiHandle<MCSavesDialog> Dialog() const { return _Dialog; }

    /// <summary>Picks row <paramref name="row"/> (-1 for none), as a click does.</summary>
    void Pick(int row);

    /// <summary>The buttons: LOAD or SAVE (the main one), DELETE.</summary>
    void PressMain();
    void PressDelete();

    bool OnKey(int key, int modifiers) override;
    void OnDestroy() override;

protected:
    void MakeModels(Rml::Context& context) override;
    void OnLoaded(Rml::ElementDocument& document) override;
    MCOptionalRef<Rml::Element> FirstFocus(Rml::ElementDocument& document) override;

private:
    /// <summary>The list filled again from the saves list, the save named <paramref name="keep"/> picked if listed.</summary>
    void Fill(std::optional<std::string> keep);
    /// <summary>The picked save's facts (or SAVE GAME's "This save") and its map shown beside the list.</summary>
    void ShowPick();
    /// <summary>The switch, its words and the buttons set as the list and mode say.</summary>
    void SetControls();
    void Changed();
    /// <summary>The window, made on first use, saying <paramref name="say"/>.</summary>
    void Say(MCSavesDialog::MCSay say);
    void Load(const std::string& name);
    void Save(const std::string& name);
    /// <summary>SAVE GAME's way out once saved: the campaign screen it was left from.</summary>
    static void BackToCampaign();

    std::unique_ptr<MCRmlModel> _Model;
    MCMode _Mode = MCMode::Load;
    std::optional<MCSaves::MCCampaign> _Campaign;
    /// <summary>The saves the rows show, by name, in the rows' order.</summary>
    std::vector<std::string> _Names;
    std::vector<MCRow> _Rows;
    int _Picked = -1;
    uint64_t _Revision = 0;
    std::string _Name;
    std::string _MapSource;
    MCGuiHandle<MCSavesDialog> _Dialog;

    // The model's values.
    std::string _Eyebrow;
    std::string _Title;
    std::string _Count;
    std::string _Empty;
    std::string _Does;
    bool _All = true;
    bool _Saving = false;
    bool _CanDelete = false;
    bool _CanMain = false;
    bool _PickShown = false;
    std::string _PickEyebrow;
    std::string _PickHeading;
    std::string _PickCampaign;
    std::string _PickOperation;
    std::string _PickMission;
    std::string _PickSaved;
    std::string _PickPoints;
    std::string _PickDate;
};

namespace MCSavesMenu
{
    /// <summary>The flow's kind for LOAD GAME (<paramref name="mode"/> Load, named "load") or SAVE GAME ("save").</summary>
    MCScreenFlow::MCScreenKind Kind(MCSavesScreen::MCMode mode);

    /// <summary>The screen, while the game's menus have it (made by the first kind's Prepare).</summary>
    MCOptionalRef<MCSavesScreen> Screen();

    /// <summary>The words: "This save couldn't be loaded.", the save's and the disk's refusals, the saved message.</summary>
    inline constexpr std::string_view RefusedText = "This save couldn't be loaded.";
    inline constexpr std::string_view DiskFullText = "Couldn't save: the disk is full.";
    inline constexpr std::string_view SaveFailedText = "Couldn't save the game.";
    inline constexpr std::string_view SavedText = "Game saved.";
    inline constexpr std::string_view DeleteQuestion = "Delete this save?";
    inline constexpr std::string_view OverwriteQuestion = "Overwrite this saved game?";
    /// <summary>"Game saved." closes itself after this long, as 1998's did.</summary>
    inline constexpr double SavedSeconds = 3.0;
}
