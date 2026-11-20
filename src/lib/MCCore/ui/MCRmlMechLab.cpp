#include "stdafx.h"
#include "ui/MCRmlMechLab.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCRepairScreen.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "object/MCMasterComponent.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
    /// <summary>The data model's name, as the document's <c>data-model</c> names it.</summary>
    constexpr const char* ModelName = "mechlab";

    /// <summary>The picture whose colour map the logistics screens' palette is (MCLogisticsCampaign activates it).</summary>
    constexpr std::string_view PaletteArt = "logart/lsrupm05";

    /// <summary>The damage diagram's size (MCMechRepairBlock::PaintDiagram), and the rows it paints above it.</summary>
    constexpr int32_t DiagramWidth = 0x4b;
    constexpr int32_t DiagramHeight = 100;
    constexpr int32_t DiagramTop = 8;

    /// <summary>The payload bar's length at its fullest (DrawTonnageBar, as MCMechRepairBlock::PaintTonnage fills it).</summary>
    constexpr double LoadLength = 53.0;

    /// <summary>A weapon's long range below which it is short-range, and medium-range (the inventory rows' colours).</summary>
    constexpr float ShortRange = 76.0f;
    constexpr float MediumRange = 151.0f;

    /// <summary>The string table's words: the row's "%.0f TONS %s", the ranks from 0x70, the payload's headings.</summary>
    constexpr uint32_t TonsText = 0x4e;
    constexpr uint32_t FirstRank = 0x70;
    constexpr int32_t Ranks = 4;
    constexpr std::array<uint32_t, 4> Headings = {0x55, 0x50, 0x6d, 0x6f};
    constexpr uint8_t Strings = 0xfe;

    /// <summary>The name plates of a mech's three variants (MCMechRepairBlock::PaintBase).</summary>
    constexpr std::array<std::string_view, 3> PlateArt = {"lscflma", "lscflmw", "lscflmj"};

    /// <summary>The repair panel's buttons, live and greyed (MCLogistics::RepairPorts).</summary>
    constexpr std::string_view RefitLive = "art:logart/lsrupm01";
    constexpr std::string_view RefitGreyed = "art:logart/lsrupm06";
    constexpr std::string_view RepairLive = "art:logart/lsrupm02";
    constexpr std::string_view RepairGreyed = "art:logart/lsrupm07";

    constexpr uint8_t Opaque = 255;
    constexpr size_t PixelBytes = 4;

    std::string PlateOf(const MCLogMech& mech)
    {
        return mech.NameVariant >= 0 && mech.NameVariant < static_cast<int32_t>(PlateArt.size())
                   ? std::format("art:logart/{}{:02}", PlateArt[static_cast<size_t>(mech.NameVariant)], mech.NameIndex)
                   : std::string();
    }

    std::string TonsOf(const MCLogMech& mech)
    {
        return MCFormatPrintf(LoadGameString(TonsText, Strings).c_str(), static_cast<double>(mech.CurTonnage),
                              mech.WeightClassName.c_str());
    }

    /// <summary>The mech's pilot (none without one).</summary>
    std::shared_ptr<MCLogWarrior> PilotOf(const MCLogMech& mech)
    {
        std::shared_ptr<MCLogWarrior> warrior;

        if (mech.PilotIndex >= 0)
        {
            GlobalLog()->AssignedWarriorList->GetWarriorInfo(mech.PilotIndex, warrior);
        }

        return warrior;
    }

    /// <summary>The points of <paramref name="locations"/>: what they have and their maximum.</summary>
    std::pair<int32_t, int32_t> Points(std::span<const MCLogMech::ArmorPoints> locations)
    {
        std::pair<int32_t, int32_t> points;

        for (const MCLogMech::ArmorPoints& location : locations)
        {
            points.first += location.CurArmor;
            points.second += location.MaxArmor;
        }

        return points;
    }

    /// <summary>The heat of the mech's weapons, each copy's (the component table's heat for a weapon).</summary>
    float HeatOf(const MCLogMech& mech)
    {
        float heat = 0.0f;

        for (const std::shared_ptr<MCLogInventoryItem>& item : mech.Inventory->Items)
        {
            const MCMasterComponent& component = MasterComponentList[item->MasterID];

            if (IsWeapon(component.Form))
            {
                heat += component.RangeOrHeat * static_cast<float>(item->Count);
            }
        }

        return heat;
    }

    /// <summary>The colour of a component's inventory row: 0 short range (and anything not a weapon), 1 medium, 2 long.</summary>
    int RangeOf(const MCMasterComponent& component)
    {
        const float range = IsWeapon(component.Form) ? component.WeaponRange[3] : 0.0f;
        return range < ShortRange ? 0 : range < MediumRange ? 1 : 2;
    }

    /// <summary>The drop zone (an element of class <c>zone</c>) <paramref name="element"/> is in, or none.</summary>
    Rml::Element* ZoneOf(Rml::Element* element)
    {
        while (element != nullptr && !element->IsClassSet("zone"))
        {
            element = element->GetParentNode();
        }

        return element;
    }
}

MCRmlMechLab::MCRmlMechLab(Key, MCRmlMenus& menus) : _Menus(menus)
{
}

MCRmlMechLab::~MCRmlMechLab()
{
    if (const MCOptionalRef<Rml::ElementDocument> document = _Menus.Document(); _Shown && document.has_value())
    {
        for (const Rml::EventId id : {Rml::EventId::Dragdrop, Rml::EventId::Dragover, Rml::EventId::Dragout})
        {
            document->get().RemoveEventListener(id, this);
        }

        _Menus.Close();
    }

    _Menus.Context().RemoveDataModel(ModelName);
    _Menus.SetPictures(std::string(DiagramScheme), nullptr);

    if (!_Diagram.empty())
    {
        Rml::ReleaseTexture(_Diagram);
    }
}

std::expected<std::unique_ptr<MCRmlMechLab>, std::string> MCRmlMechLab::Open(MCRmlMenus& menus)
{
    const std::expected<std::array<SDL_Color, 256>, std::string> colors = MCRmlArt::Colors(PaletteArt);

    if (!colors)
    {
        return std::unexpected(std::format("the mech lab's palette: {}", colors.error()));
    }

    Rml::DataModelConstructor model = menus.Context().CreateDataModel(ModelName);

    if (!model)
    {
        return std::unexpected(std::format("the mech lab's model '{}' is made already", ModelName));
    }

    auto lab = std::make_unique<MCRmlMechLab>(Key{}, menus);
    MCRmlMechLab& made = *lab;
    made._Colors = *colors;

    if (Rml::StructHandle<MCCard> card = model.RegisterStruct<MCCard>())
    {
        card.RegisterMember("slot", &MCCard::Slot);
        card.RegisterMember("picked", &MCCard::Picked);
        card.RegisterMember("plate", &MCCard::Plate);
        card.RegisterMember("tons", &MCCard::Tons);
        card.RegisterMember("pilot", &MCCard::Pilot);
    }

    if (Rml::StructHandle<MCLine> line = model.RegisterStruct<MCLine>())
    {
        line.RegisterMember("text", &MCLine::Text);
        line.RegisterMember("heading", &MCLine::Heading);
        line.RegisterMember("damaged", &MCLine::Damaged);
        line.RegisterMember("master", &MCLine::Master);
        line.RegisterMember("copy", &MCLine::Copy);
    }

    if (Rml::StructHandle<MCSpare> spare = model.RegisterStruct<MCSpare>())
    {
        spare.RegisterMember("master", &MCSpare::Master);
        spare.RegisterMember("range", &MCSpare::Range);
        spare.RegisterMember("mountable", &MCSpare::Mountable);
        spare.RegisterMember("name", &MCSpare::Name);
        spare.RegisterMember("icon", &MCSpare::Icon);
        spare.RegisterMember("weight", &MCSpare::Weight);
        spare.RegisterMember("count", &MCSpare::Count);
    }

    model.RegisterArray<std::vector<MCCard>>();
    model.RegisterArray<std::vector<MCLine>>();
    model.RegisterArray<std::vector<MCSpare>>();
    model.Bind("points", &made._Points);
    model.Bind("mechs", &made._Cards);
    model.Bind("picked", &made._HasPicked);
    model.Bind("plate", &made._Plate);
    model.Bind("tons", &made._Tons);
    model.Bind("callsign", &made._Callsign);
    model.Bind("rank", &made._Rank);
    model.Bind("diagram", &made._Diagram);
    model.Bind("refit_button", &made._RefitButton);
    model.Bind("repair_button", &made._RepairButton);
    model.Bind("armor_knob", &made._ArmorKnob);
    model.Bind("internal_knob", &made._InternalKnob);
    model.Bind("engine_knob", &made._EngineKnob);
    model.Bind("load", &made._Load);
    model.Bind("payload", &made._Payload);
    model.Bind("weight", &made._Weight);
    model.Bind("payload_weight", &made._PayloadWeight);
    model.Bind("heat", &made._Heat);
    model.Bind("repair", &made._Repair);
    model.Bind("armor", &made._Armor);
    model.Bind("internal", &made._Internal);
    model.Bind("rating", &made._Rating);
    model.Bind("refit", &made._Refit);
    model.Bind("spares", &made._Spares);
    model.BindEventCallback("pick",
                            [&made](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments)
                            {
                                if (!arguments.empty())
                                {
                                    made.Pick(arguments.front().Get<int>());
                                }
                            });
    made._Model = model.GetModelHandle();
    menus.SetPictures(std::string(DiagramScheme), [&made](std::string_view source) { return made.Diagram(source); });
    made.Pick(0);

    if (std::expected<void, std::string> shown = menus.Show(Document); !shown)
    {
        return std::unexpected(shown.error());
    }

    made._Shown = true;

    for (const Rml::EventId id : {Rml::EventId::Dragdrop, Rml::EventId::Dragover, Rml::EventId::Dragout})
    {
        menus.Document()->get().AddEventListener(id, &made);
    }

    return lab;
}

std::shared_ptr<MCLogMech> MCRmlMechLab::Picked() const
{
    std::shared_ptr<MCLogMech> mech;
    GlobalLog()->ForceMechList->GetMechInfo(_PickedSlot, mech);
    return mech;
}

void MCRmlMechLab::Pick(int32_t slot)
{
    const int32_t mechs = GlobalLog()->ForceMechList->GetMechCount();
    _PickedSlot = slot >= 0 && slot < mechs ? slot : 0;
    Refresh();
}

void MCRmlMechLab::Refresh()
{
    const MCLogistics& logistics = *GlobalLog();
    _Points = std::format("RP {}", ResourceFigureText());
    _Cards.clear();
    int32_t slot = 0;

    for (const std::shared_ptr<MCLogMech>& mech : logistics.ForceMechList->Mechs)
    {
        const std::shared_ptr<MCLogWarrior> pilot = PilotOf(*mech);
        _Cards.push_back(MCCard{.Slot = slot,
                                .Picked = slot == _PickedSlot,
                                .Plate = PlateOf(*mech),
                                .Tons = TonsOf(*mech),
                                .Pilot = pilot != nullptr ? pilot->Callsign : std::string()});
        ++slot;
    }

    const std::shared_ptr<MCLogMech> picked = Picked();
    _HasPicked = picked != nullptr;
    _Payload.clear();

    if (picked != nullptr)
    {
        MCLogMech& mech = *picked;
        _Plate = PlateOf(mech);
        _Tons = TonsOf(mech);
        const std::shared_ptr<MCLogWarrior> pilot = PilotOf(mech);
        _Callsign = pilot != nullptr ? pilot->Callsign : std::string();
        _Rank = pilot != nullptr && pilot->Rank >= 0 && pilot->Rank < Ranks
                    ? LoadGameString(FirstRank + static_cast<uint32_t>(pilot->Rank), Strings)
                    : std::string();

        // A new source each time, so RmlUi reads the picture again; the old one let go.
        const std::string previous = _Diagram;
        _Diagram = std::format("{}{}:{}", DiagramScheme, _PickedSlot, ++_DiagramVersion);

        if (!previous.empty())
        {
            Rml::ReleaseTexture(previous);
        }

        const int32_t damaged = MCLogistics::DamagedComponents(mech);
        const int32_t repairCost = logistics.RepairCost(mech);
        _RefitButton = damaged != 0 ? RefitLive : RefitGreyed;
        _RepairButton = repairCost != 0 ? RepairLive : RepairGreyed;
        _ArmorKnob = mech.RepairSliders.Armor;
        _InternalKnob = mech.RepairSliders.Internal;
        _EngineKnob = mech.RepairSliders.Engine;
        const double load =
            mech.FreeTonnage > 0.0f ? static_cast<double>(mech.WeaponTonnage) / mech.FreeTonnage * LoadLength : 0.0;
        // The bar shows no more than its length (the 1998 panel drew a heavier payload past it).
        _Load = static_cast<int>(std::clamp(load, 0.0, LoadLength));

        // The payload as the 1998 weapon list shows it: its four headings, each list's copies.
        const MCMechRepairBlock::MCPayloadLists lists = MCMechRepairBlock::PayloadLists(mech);

        for (size_t list = 0; list < lists.Entries.size(); ++list)
        {
            _Payload.push_back(MCLine{.Text = LoadGameString(Headings[list], Strings), .Heading = true});

            for (size_t entry = 0; entry < lists.Entries[list].size(); ++entry)
            {
                uint8_t itemNum = 0;
                const std::shared_ptr<MCLogInventoryItem> item =
                    MCMechRepairBlock::InvItem(mech, lists.Entries[list], static_cast<int32_t>(entry), itemNum);
                _Payload.push_back(MCLine{.Text = MasterComponentList[item->MasterID].Name,
                                          .Damaged = lists.Hits[list][entry] != 0,
                                          .Master = item->MasterID,
                                          .Copy = itemNum});
            }
        }

        const auto [armor, maxArmor] = Points(mech.Armor);
        const auto [internal, maxInternal] = Points(mech.Internals);
        _Weight = std::format("{:.1f} / {:.0f} TONS", mech.UsedTonnage, mech.CurTonnage);
        _PayloadWeight = std::format("{:.1f} / {:.1f} TONS", mech.WeaponTonnage, mech.FreeTonnage);
        _Heat = std::format("{:.1f}", HeatOf(mech));
        _Repair = std::format("{} RP", repairCost);
        _Armor = std::format("{} / {}", armor, maxArmor);
        _Internal = std::format("{} / {}", internal, maxInternal);
        _Rating = std::format("{}", mech.BattleRating);
        _Refit = damaged != 0 ? std::format("{} DAMAGED", damaged) : std::string("NONE DAMAGED");
    }

    // The spares with copies, in the inventory list's order.
    std::vector<std::shared_ptr<MCLogInventoryItem>> stock;

    for (const std::shared_ptr<MCLogInventoryItem>& item : logistics.ComponentInventory->Items)
    {
        if (item->Count > 0)
        {
            stock.push_back(item);
        }
    }

    std::ranges::sort(stock, std::less<>(), &MCLogInventoryItem::InventoryIndex);
    _Spares.clear();

    for (const std::shared_ptr<MCLogInventoryItem>& item : stock)
    {
        const MCMasterComponent& component = MasterComponentList[item->MasterID];
        const float tonnage = MCCompInventoryBlock::MountTonnage(item->MasterID);
        _Spares.push_back(MCSpare{
            .Master = item->MasterID,
            .Range = RangeOf(component),
            .Mountable = picked != nullptr && MCCompInventoryBlock::CanMount(item->MasterID, tonnage, *picked),
            .Name = component.Name,
            .Icon = std::format("art:logart/lscicc{:02}", item->RangeIndex),
            .Weight = MCCompInventoryBlock::Texts(item->MasterID).Weight,
            .Count = std::format("x{}", item->Count),
        });
    }

    _Model.DirtyAllVariables();
}

void MCRmlMechLab::Mount(int32_t masterID)
{
    const std::shared_ptr<MCLogMech> mech = Picked();
    MCInventoryList& spares = *GlobalLog()->ComponentInventory;
    const std::shared_ptr<MCLogInventoryItem> item =
        spares.GetItemInfo(spares.GetIndexFromMasterID(static_cast<uint8_t>(masterID)));

    if (mech == nullptr || item == nullptr || item->Count <= 0 ||
        !MCCompInventoryBlock::CanMount(item->MasterID, MCCompInventoryBlock::MountTonnage(item->MasterID), *mech))
    {
        return;
    }

    // One copy off the spares; the inventory's rows given out again when the last goes (as the 1998 row's drag).
    if (GlobalLog()->MountComponent(*mech, item->MasterID) && --item->Count == 0)
    {
        GlobalLog()->ReIndexInventory();
    }

    Refresh();
}

void MCRmlMechLab::Strip(int32_t masterID, int32_t itemNum)
{
    const std::shared_ptr<MCLogMech> mech = Picked();

    if (mech == nullptr || masterID < 0 || itemNum < 0)
    {
        return;
    }

    // Dropped on the spares, as the 1998 panel's drop on the inventory: the mech undeployed, the copy out, and back
    // with the spares when it isn't damaged (a damaged one is thrown away); a new row for a component they had none of.
    GlobalLog()->UndeployMech(*mech);
    const std::optional<uint8_t> hits =
        GlobalLog()->UnmountComponent(*mech, static_cast<uint8_t>(masterID), static_cast<uint8_t>(itemNum));

    if (hits.has_value() && *hits == 0 && GlobalLog()->ReturnSpare(static_cast<uint8_t>(masterID))->Count == 1)
    {
        GlobalLog()->ReIndexInventory();
    }

    Refresh();
}

void MCRmlMechLab::ProcessEvent(Rml::Event& event)
{
    Rml::Element* zone = ZoneOf(event.GetTargetElement());

    if (zone == nullptr)
    {
        return;
    }

    if (event.GetId() != Rml::EventId::Dragdrop)
    {
        zone->SetClass("over", event.GetId() == Rml::EventId::Dragover);
        return;
    }

    zone->SetClass("over", false);
    auto* dragged = static_cast<Rml::Element*>(event.GetParameter<void*>("drag_element", nullptr));

    if (dragged == nullptr)
    {
        return;
    }

    const int32_t masterID = dragged->GetAttribute<int>("master", -1);

    if (zone->GetId() == "lab" && dragged->IsClassSet("spare"))
    {
        Mount(masterID);
    }
    else if (zone->GetId() == "spares" && dragged->IsClassSet("line"))
    {
        Strip(masterID, dragged->GetAttribute<int>("copy", -1));
    }
}

std::expected<MCRmlRecorder::MCLoaded, std::string> MCRmlMechLab::Diagram(std::string_view source) const
{
    // diagram:<slot>:<version>
    const std::string_view rest = source.substr(DiagramScheme.size());
    int32_t slot = -1;
    std::from_chars(rest.data(), rest.data() + rest.size(), slot);
    std::shared_ptr<MCLogMech> mech;

    if (GlobalLog()->ForceMechList->GetMechInfo(slot, mech) != 0 || mech == nullptr)
    {
        return std::unexpected(std::format("'{}' names no mech of the force", source));
    }

    // Painted as the 1998 panel paints it, its rows above it left out.
    MCLogPort port;

    if (port.Init(DiagramWidth, DiagramTop + DiagramHeight) != 0)
    {
        return std::unexpected(std::format("no picture for {}'s diagram", mech->MechName));
    }

    MCMechRepairBlock::PaintDiagram(*mech, port, 0, 0);
    const std::span<const uint8_t> pixels = port.Pixels();
    std::vector<uint8_t> rgba(static_cast<size_t>(DiagramWidth * DiagramHeight) * PixelBytes);

    for (int32_t y = 0; y < DiagramHeight; ++y)
    {
        for (int32_t x = 0; x < DiagramWidth; ++x)
        {
            const size_t from = static_cast<size_t>((y + DiagramTop) * port.Width() + x);
            const size_t to = static_cast<size_t>(y * DiagramWidth + x) * PixelBytes;
            const SDL_Color& color = _Colors[from < pixels.size() ? pixels[from] : 0];
            rgba[to] = color.r;
            rgba[to + 1] = color.g;
            rgba[to + 2] = color.b;
            rgba[to + 3] = Opaque;
        }
    }

    return MCRmlRecorder::MCLoaded{
        .Texture = MCRmlRecorder::MakeTexture(DiagramWidth, DiagramHeight, std::move(rgba), false),
        .Width = DiagramWidth,
        .Height = DiagramHeight,
    };
}
