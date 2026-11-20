#pragma once

#include "ui/MCRmlRecorder.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/EventListener.h>

class MCLogMech;
class MCRmlMenus;

/// <summary>
/// Port-only (SPEC-menus.md, "The screens": the 'MECH BAY): the remade mech lab, a first take, an RmlUi screen
/// (src/res/ui/mechlab.rml) over the logistics data: the force's mechs as cards; the picked mech's lab (the 1998
/// 'MECH BAY row whole, its damage diagram as the 1998 panel paints it, its payload, its repair knobs), its figures
/// (weight, payload, armor, internal structure, heat, battle rating, what REPAIR ALL costs and REFIT ALL replaces); and
/// the spare components, dragged onto the lab to mount one and from its payload back to the spares.
/// </summary>
/// <remarks>
/// A view (PR #59's rule): what it shows is read from the logistics data (GlobalLog) whenever it changes
/// (<see cref="Refresh"/>), into top-level values and lists (RmlUi sees a change only there), and every change is the
/// logistics data's own, as the 1998 screen makes it: MCLogistics::MountComponent, UnmountComponent and ReturnSpare, the
/// spares' counts and ReIndexInventory, MCCompInventoryBlock::CanMount for what the picked mech can take.
/// </remarks>
class MCRmlMechLab final : public Rml::EventListener
{
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>The screen's document, in the menus' folder.</summary>
    static constexpr std::string_view Document = "mechlab.rml";
    /// <summary>The scheme of the damage diagram's picture: <c>diagram:&lt;force slot&gt;:&lt;version&gt;</c>.</summary>
    static constexpr std::string_view DiagramScheme = "diagram:";

    /// <summary>A mech's card.</summary>
    struct MCCard
    {
        int Slot = 0;
        bool Picked = false;
        /// <summary>Its name plate (<c>art:</c>).</summary>
        Rml::String Plate;
        Rml::String Tons;
        Rml::String Pilot;
    };

    /// <summary>A line of the payload list: a heading, or a copy of a weapon or piece of equipment.</summary>
    struct MCLine
    {
        Rml::String Text;
        bool Heading = false;
        bool Damaged = false;
        /// <summary>The copy's component and item number (-1 on a heading).</summary>
        int Master = -1;
        int Copy = -1;
    };

    /// <summary>A stack of spare components.</summary>
    struct MCSpare
    {
        int Master = 0;
        /// <summary>Its range: 0 short (or none), 1 medium, 2 long, as its inventory row's colour.</summary>
        int Range = 0;
        bool Mountable = false;
        Rml::String Name;
        Rml::String Icon;
        Rml::String Weight;
        Rml::String Count;
    };

    explicit MCRmlMechLab(Key, MCRmlMenus& menus);
    ~MCRmlMechLab() override;
    MCRmlMechLab(const MCRmlMechLab&) = delete;
    MCRmlMechLab& operator=(const MCRmlMechLab&) = delete;

    /// <summary>
    /// Shows the lab on <paramref name="menus"/> (its model made, its document shown, its pictures' scheme taken), the
    /// force's first mech picked; why not when it can't be.
    /// </summary>
    static std::expected<std::unique_ptr<MCRmlMechLab>, std::string> Open(MCRmlMenus& menus);

    /// <summary>Reads what the screen shows from the logistics data again.</summary>
    void Refresh();

    /// <summary>Picks the force's mech in row <paramref name="slot"/> (none there: the first).</summary>
    void Pick(int32_t slot);

    /// <summary>The picked mech (none when the force has none).</summary>
    std::shared_ptr<MCLogMech> Picked() const;

    /// <summary>The drops (onto the lab: a spare mounted; onto the spares: a payload copy taken out) and the drop zones' highlight.</summary>
    void ProcessEvent(Rml::Event& event) override;

private:
    /// <summary>A spare of <paramref name="masterID"/> dropped on the lab: mounted on the picked mech when it can be.</summary>
    void Mount(int32_t masterID);
    /// <summary>The payload's copy <paramref name="itemNum"/> of <paramref name="masterID"/> dropped on the spares.</summary>
    void Strip(int32_t masterID, int32_t itemNum);
    /// <summary>The picked mech's damage diagram as a picture (<see cref="DiagramScheme"/>).</summary>
    std::expected<MCRmlRecorder::MCLoaded, std::string> Diagram(std::string_view source) const;

    MCRmlMenus& _Menus;
    Rml::DataModelHandle _Model;
    /// <summary>The document is the menus' (shown by <see cref="Open"/>), closed with the lab.</summary>
    bool _Shown = false;
    int32_t _PickedSlot = 0;
    /// <summary>Raised whenever the picked mech's diagram may have changed, so its picture is made again.</summary>
    int32_t _DiagramVersion = 0;
    /// <summary>The colours the diagram's palette indices stand for (the logistics screens' palette).</summary>
    std::array<SDL_Color, 256> _Colors{};

    // The values the document binds (top-level: RmlUi sees a change only there).
    Rml::String _Points;
    std::vector<MCCard> _Cards;
    bool _HasPicked = false;
    Rml::String _Plate;
    Rml::String _Tons;
    Rml::String _Callsign;
    Rml::String _Rank;
    Rml::String _Diagram;
    Rml::String _RefitButton;
    Rml::String _RepairButton;
    int _ArmorKnob = 0;
    int _InternalKnob = 0;
    int _EngineKnob = 0;
    int _Load = 0;
    std::vector<MCLine> _Payload;
    Rml::String _Weight;
    Rml::String _PayloadWeight;
    Rml::String _Heat;
    Rml::String _Repair;
    Rml::String _Armor;
    Rml::String _Internal;
    Rml::String _Rating;
    Rml::String _Refit;
    std::vector<MCSpare> _Spares;
};
