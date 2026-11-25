#pragma once

// Port-only (SPEC-menus.md's stage 4, CHOOSE MAP): the host's window over the remade room (ui/choose_map.rml), the
// room's CHOOSE MAP or CHANGE: the maps the room can play (logistics/MCMatchMaps), each with its file, the points each
// team starts with and its time limit; beside them the picked one's picture, points, time limit and description. CHOOSE
// (Enter, or a double click on a map) loads it into the room, as 1998's LOAD MAP did; CANCEL (Escape) goes back.

#include "logistics/MCMatchMaps.h"
#include "ui/MCRmlScreen.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class MCRmlModel;

/// <summary>CHOOSE MAP's window (<c>ui/choose_map.rml</c>).</summary>
class MCChooseMapWindow final : public MCRmlScreen
{
public:
    MCChooseMapWindow();
    ~MCChooseMapWindow() override;

    /// <summary>Opens over the room, the maps read afresh, the room's map picked (none: the first).</summary>
    void OpenOver(std::string_view roomsMap);

    /// <summary>What CHOOSE does with the map picked (its file, as the room loads it).</summary>
    std::function<void(const std::string& file)> Choosing;

    /// <summary>The maps listed, and which is picked (-1: none).</summary>
    const std::vector<MCMatchMap>& Maps() const { return _Maps; }
    int Picked() const { return _Picked; }

    /// <summary>What a player's presses do (and the tests'): a map picked, CHOOSE, CANCEL.</summary>
    void Pick(int at);
    void PressChoose();
    void PressCancel();

    /// <summary>The picked map's facts as the window says them: "300,000 RP a team. No time limit.".</summary>
    static std::string FactsOf(const MCMatchMap& map);

    void OnFrame() override;
    void OnDestroy() override;

protected:
    void MakeModels(Rml::Context& context) override;
    void OnLoaded(Rml::ElementDocument& document) override;
    bool OnKey(int key, int modifiers) override;

private:
    /// <summary>A map as the list binds it.</summary>
    struct MCRow
    {
        std::string Name;
        std::string File;
        std::string Points;
        std::string Time;

        bool operator==(const MCRow&) const = default;
    };

    struct MCLine
    {
        std::string Text;

        bool operator==(const MCLine&) const = default;
    };

    /// <summary>The list and the picked map's side put in the model.</summary>
    void ShowMaps();
    void ShowPick();

    /// <summary>
    /// The keys' focus put on the picked map once its row is laid out (the list's rows come with the model's next
    /// update), the list scrolled to it: the arrows then move the pick.
    /// </summary>
    void FocusPicked();

    std::unique_ptr<MCRmlModel> _Model;
    /// <summary>The picked map's row still waits for the keys' focus (the window just opened).</summary>
    bool _FocusPicked = false;
    std::vector<MCMatchMap> _Maps;
    std::vector<MCRow> _Rows;
    int _Picked = -1;
    std::string _PickName;
    std::string _PickPic;
    std::string _PickFacts;
    std::vector<MCLine> _PickLines;
};

/// <summary>
/// CHOOSE MAP's window opened over the remade room, the room's map (<paramref name="roomsMap"/>, its file; none: empty)
/// picked, CHOOSE doing <paramref name="choosing"/>. Port-only (stage 9): where it can't be read, the game stops
/// (MCRmlFlow::Broken).
/// </summary>
void ShowChooseMapWindow(std::string_view roomsMap, std::function<void(const std::string& file)> choosing);

/// <summary>CHOOSE MAP's window (none before it first opened).</summary>
MCGuiHandle<MCChooseMapWindow> ChooseMapWindow();

/// <summary>Whether CHOOSE MAP's window shows.</summary>
bool ChooseMapWindowShown();
