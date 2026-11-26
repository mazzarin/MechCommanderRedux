#include "stdafx.h"
#include "logistics/MCLogisticsTabs.h"
#include "logistics/MCBriefingText.h"
#include "logistics/MCMatchMaps.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCStore.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCGameStrings.h"
#include "main/MCInventoryList.h"
#include "main/MCLogMech.h"
#include "main/MCLogMechList.h"
#include "main/MCLogVehicle.h"
#include "main/MCLogVehicleList.h"
#include "main/MCLogWarrior.h"
#include "main/MCLogWarriorList.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMasterComponent.h"
#include "sound/MCSoundSystem.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlModel.h"
#include "logistics/MCMenuDialog.h"
#include "main/MCLogisticsShared.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/SystemInterface.h>

namespace
{
    using MCPlace = MCLogisticsTabs::MCPlace;
    using MCTab = MCCampaignScreen::MCTab;

    /// <summary>The cards' ids: their place's prefix and their index (force3, store0); the lab's MechWarrior's.</summary>
    constexpr std::string_view ForcePrefix = "force";
    constexpr std::string_view StoredPrefix = "stored";
    constexpr std::string_view StorePrefix = "store";
    constexpr std::string_view StockPrefix = "stock";
    constexpr std::string_view FitPrefix = "fit";
    constexpr std::string_view PilotId = "pilot";
    /// <summary>A FORCE card's MechWarrior's photo (its class): a drag from it carries them, not the 'Mech.</summary>
    constexpr const char* PhotoClass = "photo";
    /// <summary>The columns a drag is let go over (their elements' ids).</summary>
    constexpr std::string_view UnitsId = "units";
    constexpr std::string_view StoreId = "store";
    constexpr std::string_view InfoId = "info";
    constexpr std::string_view LabId = "lab";
    constexpr std::string_view InventoryId = "inventory";
    constexpr const char* DealCountId = "deal-count";
    constexpr const char* DealCancelId = "deal-cancel";
    constexpr const char* DealConfirmId = "deal-confirm";
    constexpr const char* ChoicesAttribute = "data-choices";

    /// <summary>How far the mouse moves with a card held before it is a drag (pixels).</summary>
    constexpr float DragPixels = 4.0f;
    /// <summary>Where the lifted card sits from the pointer (pixels).</summary>
    constexpr float LiftedOffset = 6.0f;
    constexpr double WhySeconds = 2.0;
    constexpr int LeftButton = 0;
    constexpr int RightButton = 1;

    /// <summary>1998's sounds: a pick-up, a put-down, a refusal.</summary>
    constexpr uint32_t PickedSound = 0x35;
    constexpr uint32_t PlacedSound = 0x34;
    constexpr uint32_t RefusedSound = 0x33;
    /// <summary>1998's click: the purchase dialog's arrows (a store mech's variant buttons are the kit's switch).</summary>
    constexpr uint32_t ClickSound = 0xf;
    /// <summary>1998's messages: too poor, the force full, too heavy, short of points to repair; the refit's question.</summary>
    constexpr uint32_t TooPoorMessage = 0x4d;
    constexpr uint32_t ForceFullMessage = 0x285;
    constexpr uint32_t TooHeavyMessage = 99;
    constexpr uint32_t ShortOfPointsMessage = 0x57;
    constexpr uint32_t RefitHeadString = 0x54;
    constexpr uint32_t RefitFootString = 0x62;
    /// <summary>The rank words (0x70 green to 0x73 elite).</summary>
    constexpr uint32_t RankStrings = 0x70;
    constexpr int32_t HighestRank = 3;
    constexpr int StringBuffer = 0xfe;
    /// <summary>The pilot's speech when boarding or leaving a 'Mech, and when hired or sold.</summary>
    constexpr int32_t PilotSpeech = 2;

    /// <summary>1998's purchase dialog's types: a buy is even, a sale odd; the arrows work for buys and a part's sale.</summary>
    constexpr int32_t BuyMechType = 0;
    constexpr int32_t SellMechType = 1;
    constexpr int32_t HireType = 2;
    constexpr int32_t FireType = 3;
    constexpr int32_t BuyPartType = 4;
    constexpr int32_t SellPartType = 5;
    constexpr int32_t BuyVehicleType = 6;
    constexpr int32_t SellVehicleType = 7;

    /// <summary>1998's battle rating bar: full at 18010.</summary>
    constexpr double FullRating = 18010.0;
    constexpr int WholePercent = 100;
    constexpr float HealthPips = 6.0f;
    /// <summary>The payload's ramp: its segments, the lowest a third of the highest.</summary>
    constexpr size_t PayloadSegments = 40;
    constexpr float LowestSegment = 1.0f / 3.0f;
    /// <summary>The engine slider's levels: 0 at three hits to 3 whole.</summary>
    constexpr int EngineLevels = 3;
    /// <summary>The longest name the frames' cards hold at their size ("Light Autocannon"); longer ones are set smaller.</summary>
    constexpr size_t LongNameLetters = 16;

    /// <summary>Whether <paramref name="who"/> is longer than a card's name holds at its own size.</summary>
    bool LongName(std::string_view who)
    {
        return who.size() > LongNameLetters;
    }

    /// <summary>The skills in the order 1998's bars show them (the array's: Piloting, Jumping, Sensors, Gunnery).</summary>
    constexpr std::array<size_t, MCLogWarrior::NumSkills> SkillOrder = {3, 0, 1, 2};
    constexpr std::array<std::string_view, MCLogWarrior::NumSkills> SkillWords = {"Gunnery", "Piloting", "Jumping",
                                                                                  "Sensors"};

    /// <summary>The words the remade screens add (1998's say nothing there).</summary>
    constexpr std::string_view BuyWord = "Buy";
    constexpr std::string_view SellWord = "Sell";
    constexpr std::string_view RequiredWhy = "The mission needs it";
    constexpr std::string_view SoldOutNote = "0 left";
    constexpr std::string_view LeftFormat = "{} left";
    constexpr std::string_view InStoreFormat = "{} in store";
    constexpr std::string_view OwnedNote = "owned";
    /// <summary>A 'Mech without a MechWarrior, under its name; a unit the INVENTORY stores, before its tons.</summary>
    constexpr std::string_view NoPilotWord = "No pilot";
    constexpr std::string_view StoredWord = "Stored";
    /// <summary>The hint line's keys for the list's switch and the tabs.</summary>
    constexpr std::string_view ListKeys = "Z C";
    constexpr std::string_view ListKeysDo = "Change list";
    constexpr std::string_view TabKeys = "Q E";
    constexpr std::string_view TabKeysDo = "Change tab";
    /// <summary>The force's count against the most it holds; the INVENTORY's.</summary>
    constexpr std::string_view ForceCountFormat = "{} / {}";
    constexpr std::string_view CountFormat = "{} {}";
    constexpr std::string_view MechWord = "mech";
    constexpr std::string_view MechsWord = "mechs";
    constexpr std::string_view UnitWord = "unit";
    constexpr std::string_view UnitsWord = "units";
    constexpr std::string_view VehicleWord = "vehicle";
    constexpr std::string_view VehiclesWord = "vehicles";
    constexpr std::string_view MechWarriorWord = "MechWarrior";
    constexpr std::string_view MechWarriorsWord = "MechWarriors";
    constexpr std::string_view PartWord = "part";
    constexpr std::string_view PartsWord = "parts";
    constexpr std::string_view PointsFormat = "{} RP";
    constexpr std::string_view RepairAllFormat = "Repair all · {} RP";
    constexpr std::string_view RepairAllWord = "Repair all";
    constexpr std::string_view RepairForceFormat = "Repair force · {} RP";
    constexpr std::string_view RepairForceLabel = "Repair force";
    constexpr std::string_view OfFormat = "{} of {}";
    constexpr std::string_view PayloadOfFormat = "of {:.1f} tons";
    constexpr std::string_view TonsFormat = "{:.1f} tons";
    constexpr std::string_view FreeFormat = "{:.1f} tons free";
    constexpr std::string_view WhatFormat = "{} · {}";
    constexpr std::string_view SpeedFormat = "{} m/s";
    constexpr std::string_view WholeWord = "Whole";
    constexpr std::string_view DamagedWord = "Damaged";
    constexpr std::string_view EquipmentWord = "Equipment";

    /// <summary>A list's count with its word, agreeing with it: "1 MechWarrior", "3 parts".</summary>
    std::string CountText(size_t count, std::string_view one, std::string_view many)
    {
        return std::format(CountFormat, count, count == 1 ? one : many);
    }

    double RmlNow()
    {
        Rml::SystemInterface* system = Rml::GetSystemInterface();
        return system != nullptr ? system->GetElapsedTime() : 0.0;
    }

    std::string Points(int32_t points)
    {
        return std::format(PointsFormat, MCGroupedPoints(points));
    }

    std::string GameWord(uint32_t id)
    {
        return LoadGameString(id, StringBuffer);
    }

    std::string ClassWord(float tons)
    {
        return GameWord(WeightClassString(tons));
    }

    std::string RankWord(int32_t rank)
    {
        return rank >= 0 && rank <= HighestRank ? GameWord(RankStrings + static_cast<uint32_t>(rank)) : std::string();
    }

    std::string TonsAndClass(float tons)
    {
        return std::format(WhatFormat, MCCampaignMenu::TonsText(static_cast<int32_t>(std::lround(tons))),
                           ClassWord(tons));
    }

    /// <summary>The store's or the lab's picture of a part: its 1998 icon.</summary>
    std::string PartPicture(const MCLogInventoryItem& part)
    {
        return std::format("image({}logart/lscicc{:02} contain center center)", MCRmlArt::Scheme, part.RangeIndex);
    }

    std::string PilotPicture(int32_t nameIndex)
    {
        return MCCampaignMenu::Portrait(std::format("{}logart/pilot{:02}", MCRmlArt::Scheme, nameIndex));
    }

    /// <summary>A part's line under its name: its range (a weapon's) and its tons as fitted.</summary>
    std::string PartWhat(uint8_t masterID)
    {
        const MCStore::MCComponentTexts texts = MCStore::ComponentTexts(masterID);
        const bool weapon = IsWeapon(ComponentForm(masterID));
        return std::format(WhatFormat, weapon ? texts.Range : std::string(EquipmentWord), texts.Weight);
    }

    /// <summary>What the store has left of a good: a count, none when unlimited (a match's store).</summary>
    std::string LeftNote(int32_t available)
    {
        return available < 0 ? std::string() : std::format(LeftFormat, available);
    }

    /// <summary>The variant <paramref name="purMech"/> shows; none when it shows none, or one it lacks.</summary>
    MCOptionalRef<const MCPurMechData> ShownVariant(const MCPurMech& purMech)
    {
        const int32_t variant = purMech.CurVariant;
        return variant >= 0 && static_cast<size_t>(variant) < purMech.Variants.size()
                   ? MCOptionalOf<const MCPurMechData>(purMech.Variants[static_cast<size_t>(variant)].get())
                   : std::nullopt;
    }

    /// <summary>Whether a payload lists <paramref name="item"/>: a weapon or piece of equipment it carries.</summary>
    bool InKit(const MCLogInventoryItem& item)
    {
        const MCComponentForm form = ComponentForm(item.MasterID);
        return (IsWeapon(form) || IsEquipment(form)) && item.Count > 0;
    }

    /// <summary>A payload's rows: each weapon and piece of equipment, how many.</summary>
    std::vector<MCLogisticsTabs::MCKitRow> KitOf(const MCInventoryList& inventory)
    {
        std::vector<MCLogisticsTabs::MCKitRow> rows;

        for (const std::shared_ptr<MCLogInventoryItem>& item : inventory.Items)
        {
            if (InKit(*item))
            {
                rows.push_back({.Count = item->Count, .Name = item->Name});
            }
        }

        return rows;
    }

    /// <summary>
    /// An owned 'Mech's payload as the details list it: a row a part, its damaged copies a row of their own marked
    /// DAMAGED (the user's, 26 Nov: a 'Mech for sale showed nothing of its broken weapons).
    /// </summary>
    std::vector<MCLogisticsTabs::MCKitRow> OwnedKitOf(const MCInventoryList& inventory)
    {
        std::vector<MCLogisticsTabs::MCKitRow> rows;

        for (const std::shared_ptr<MCLogInventoryItem>& item : inventory.Items)
        {
            if (!InKit(*item))
            {
                continue;
            }

            const auto damaged =
                static_cast<int>(std::ranges::count_if(item->Stats, [](const std::shared_ptr<MCLogInventoryStat>& copy)
                                                       { return copy != nullptr && copy->Hits != 0; }));
            const int broken = std::min(damaged, item->Count);

            if (item->Count > broken)
            {
                rows.push_back({.Count = item->Count - broken, .Name = item->Name});
            }

            if (broken > 0)
            {
                rows.push_back({.Count = broken, .Name = item->Name, .State = std::string(DamagedWord)});
            }
        }

        return rows;
    }

    /// <summary>The MechWarrior aboard <paramref name="mech"/>: a teammate's 'Mech's as the match sent it.</summary>
    std::shared_ptr<MCLogWarrior> AboardOf(const MCLogistics& logistics, const MCLogMech& mech)
    {
        std::shared_ptr<MCLogWarrior> warrior;

        if (mech.LocalPart == 0)
        {
            warrior = mech.NetworkPilot.lock();
        }
        else if (mech.PilotIndex >= 0)
        {
            logistics.AssignedWarriorList->GetWarriorInfo(mech.PilotIndex, warrior);
        }

        return warrior;
    }

    int RatingPercent(int32_t rating)
    {
        return std::clamp(static_cast<int>(std::lround(rating / FullRating * WholePercent)), 0, WholePercent);
    }

    /// <summary>A unit's own list of the force or the INVENTORY's MECHS it is in.</summary>
    template <typename Part> bool In(const std::vector<std::shared_ptr<Part>>& list, const MCLogPart& part)
    {
        return std::ranges::any_of(list, [&part](const std::shared_ptr<Part>& in) { return in.get() == &part; });
    }

    /// <summary>
    /// A unit or a MechWarrior the store refuses, as 1998's drop: too poor, its message; sold out, its refusal sound.
    /// </summary>
    bool Refused(MCStore::MCRefusal refusal)
    {
        if (refusal == MCStore::MCRefusal::None)
        {
            return false;
        }

        PlayLogSound(RefusedSound);

        if (refusal == MCStore::MCRefusal::TooPoor)
        {
            ShowLogMessage(TooPoorMessage);
        }

        return true;
    }

    uint64_t Mixed(uint64_t hash, uint64_t value)
    {
        constexpr uint64_t Prime = 1099511628211ull;
        return (hash ^ value) * Prime;
    }

    uint64_t Address(const void* thing)
    {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(thing));
    }
}

// MCThing

bool MCLogisticsTabs::MCThing::Same(const MCThing& other) const
{
    const auto same = [](const auto& a, const auto& b) { return !a.owner_before(b) && !b.owner_before(a); };
    return Place == other.Place && ItemNum == other.ItemNum && same(Unit, other.Unit) &&
           same(StoreMech, other.StoreMech) && same(StorePilot, other.StorePilot) &&
           same(StoreVehicle, other.StoreVehicle) && same(Part, other.Part) && same(Warrior, other.Warrior);
}

bool MCLogisticsTabs::MCThing::Gone() const
{
    return Unit.expired() && StoreMech.expired() && StorePilot.expired() && StoreVehicle.expired() && Part.expired() &&
           Warrior.expired();
}

// MCLogisticsTabs

MCLogisticsTabs::MCLogisticsTabs(MCCampaignScreen& screen) : _Screen(screen)
{
    for (size_t segment = 0; segment < PayloadSegments; ++segment)
    {
        const float height =
            LowestSegment + (1.0f - LowestSegment) * static_cast<float>(segment) / (PayloadSegments - 1);
        _PayloadRamp.push_back({.Height = std::format("{:.1f}%", height * WholePercent)});
    }
}

MCLogisticsTabs::~MCLogisticsTabs() = default;

void MCLogisticsTabs::Bind(MCRmlModel& model)
{
    _Model = model;
    model.Value("forceCount", _ForceCount);
    model.Value("storeCount", _StoreCount);
    model.Value("stockCount", _StockCount);
    model.Value("stockKind", _StockKind);
    // The cards' type is the campaign's (its "ready" list registered it first).
    model.List("force", _Force, std::pair{"kind", &MCCampaignScreen::MCCard::Kind});
    model.List("stored", _Stored, std::pair{"kind", &MCCampaignScreen::MCCard::Kind});
    model.List("store", _Store, std::pair{"picture", &MCGood::Picture}, std::pair{"who", &MCGood::Who},
               std::pair{"what", &MCGood::What}, std::pair{"price", &MCGood::Price}, std::pair{"note", &MCGood::Note},
               std::pair{"why", &MCGood::Why}, std::pair{"off", &MCGood::Off}, std::pair{"picked", &MCGood::Picked},
               std::pair{"part", &MCGood::Part}, std::pair{"long", &MCGood::Long},
               std::pair{"warrior", &MCGood::Warrior}, std::pair{"health", &MCGood::Health});
    model.List("stock", _StockGoods, std::pair{"picture", &MCGood::Picture});
    model.List("lifted", _Lifted, std::pair{"picture", &MCGood::Picture});
    model.Value("liftedLeft", _LiftedLeft);
    model.Value("liftedTop", _LiftedTop);
    model.Value("infoShown", _InfoShown);
    model.Value("infoName", _InfoName);
    model.Value("infoWhat", _InfoWhat);
    model.Value("infoPicture", _InfoPicture);
    model.Value("variants", _Variants);
    model.Value("variantOff0", _VariantOff0);
    model.Value("variantOff1", _VariantOff1);
    model.Value("variantOff2", _VariantOff2);
    model.List("stats", _Stats, std::pair{"label", &MCStat::Label}, std::pair{"value", &MCStat::Value});
    model.Value("rated", _Rated);
    model.Value("rating", _Rating);
    model.List("kit", _Kit, std::pair{"count", &MCKitRow::Count}, std::pair{"name", &MCKitRow::Name},
               std::pair{"state", &MCKitRow::State});
    model.Value("description", _Description);
    model.Value("infoCost", _InfoCost);
    model.Value("infoNote", _InfoNote);
    model.Value("dealWord", _DealWord);
    model.Value("canDeal", _CanDeal);
    model.Value("dealOpen", _DealOpen);
    model.Value("dealTitle", _DealTitle);
    model.Value("dealPicture", _DealPicture);
    model.Value("dealName", _DealName);
    model.Value("dealWhat", _DealWhat);
    model.Value("dealEach", _DealEach);
    model.Value("dealTotal", _DealTotal);
    model.Value("dealLeft", _DealLeft);
    model.Value("labKind", _LabKind);
    model.Value("labName", _LabName);
    model.Value("labWhat", _LabWhat);
    model.Value("labPicture", _LabPicture);
    model.Value("labStored", _LabStored);
    model.Value("armorNow", _Armor.Now);
    model.Value("armorCost", _Armor.Cost);
    model.Value("armorKnob", _Armor.Knob);
    model.Value("structureNow", _Structure.Now);
    model.Value("structureCost", _Structure.Cost);
    model.Value("structureKnob", _Structure.Knob);
    model.Value("engineNow", _Engine.Now);
    model.Value("engineCost", _Engine.Cost);
    model.Value("engineKnob", _Engine.Knob);
    model.Value("repairs", _Repairs);
    model.Value("hasPilot", _HasPilot);
    model.Value("seat", _Seat);
    model.Value("pilotName", _PilotName);
    model.Value("pilotRank", _PilotRank);
    model.Value("pilotPicture", _PilotPicture);
    model.Value("pilotHealth", _PilotHealth);
    model.List("skills", _Skills, std::pair{"label", &MCStat::Label});
    model.Value("payloadNow", _PayloadNow);
    model.Value("payloadOf", _PayloadOf);
    model.List("payloadRamp", _PayloadRamp, std::pair{"height", &MCCampaignScreen::MCSegment::Height});
    model.List("fits", _Fits, std::pair{"range", &MCFitRow::Range}, std::pair{"name", &MCFitRow::Name},
               std::pair{"tons", &MCFitRow::Tons}, std::pair{"state", &MCFitRow::State});
    model.Value("free", _Free);
    model.Value("repairAllWord", _RepairAllWord);
    model.Value("canRepairAll", _CanRepairAll);
    model.Value("canRefit", _CanRefit);
    model.Value("forceRepairs", _ForceRepairs);
    model.Value("repairForceWord", _RepairForceWord);
    model.Value("canRepairForce", _CanRepairForce);
}

void MCLogisticsTabs::Show(MCTab tab)
{
    Leave();

    // The 'MECH BAY opens on the force's first 'Mech, as 1998's did; PURCHASING on nothing picked.
    if (tab == MCTab::MechBay && _LabPart.expired())
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();

        if (logistics != nullptr && !logistics->ForceMechList->Mechs.empty())
        {
            const std::shared_ptr<MCLogMech>& first = logistics->ForceMechList->Mechs.front();
            _LabPart = first;
            _Knobs = MCMechBay::KnobsOf(*first);
        }
    }

    _Fingerprint = 0;
    Fill();
}

void MCLogisticsTabs::Leave()
{
    if (_Deal.has_value())
    {
        DealCancel();
    }

    _Hold.reset();
    _Landing.reset();
    _Hovered.reset();
    _Lifted.clear();
}

std::vector<MCRmlKeyHint> MCLogisticsTabs::Keys() const
{
    if (_Deal.has_value())
    {
        return {{"← →", "How many"}, {"Enter", _DealTitle}, {"Esc", "Cancel"}};
    }

    // Z and C step the list's switch (the store's kinds, the 'MECH BAY's INVENTORY), as Q and E the tabs.
    const MCRmlKeyHint list{std::string(ListKeys), std::string(ListKeysDo)};
    const MCRmlKeyHint tabs{std::string(TabKeys), std::string(TabKeysDo)};

    if (Purchasing())
    {
        return {{"Enter", "Buy or sell"}, {"Space", "Pick"}, list, tabs};
    }

    return {{"Enter", "Fit, board, or in and out of the force"}, {"Space", "Pick"}, list, tabs};
}

void MCLogisticsTabs::Changed()
{
    if (_Model.has_value())
    {
        _Model->get().Changed();
    }
}

// Reading the data

uint64_t MCLogisticsTabs::Fingerprint() const
{
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (logistics == nullptr)
    {
        return 0;
    }

    uint64_t hash =
        Mixed(static_cast<uint64_t>(ResourcePoints), static_cast<uint64_t>(_Kind * 4 + static_cast<int>(_BayStock)));
    const auto units = [&hash](const auto& list)
    {
        hash = Mixed(hash, list.size());

        for (const auto& unit : list)
        {
            hash = Mixed(hash, Address(unit.get()));

            if constexpr (requires { unit->CalcStatus(); })
            {
                hash = Mixed(hash, static_cast<uint64_t>(std::lround(unit->CalcStatus() * WholePercent)));
            }
        }
    };

    units(logistics->ForceMechList->Mechs);
    units(logistics->MechList->Mechs);
    units(logistics->ForceVehicleList->Vehicles);
    units(logistics->VehicleList->Vehicles);

    for (const std::shared_ptr<MCLogMech>& mech : logistics->ForceMechList->Mechs)
    {
        hash = Mixed(hash, static_cast<uint64_t>(mech->PilotIndex + 1));
    }

    for (const auto& list : {&logistics->WarriorList->Warriors, &logistics->AssignedWarriorList->Warriors})
    {
        for (const std::shared_ptr<MCLogWarrior>& warrior : *list)
        {
            hash = Mixed(hash, Address(warrior.get()));
        }
    }

    for (const auto& list : {&logistics->ComponentInventory->Items, &logistics->PurchaseComponents->Items})
    {
        for (const std::shared_ptr<MCLogInventoryItem>& item : *list)
        {
            hash = Mixed(hash, static_cast<uint64_t>(item->Count) * 1024 + static_cast<uint64_t>(item->ListIndex + 1));
        }
    }

    for (const std::shared_ptr<MCPurMech>& purMech : logistics->PurMechList->Mechs)
    {
        const MCOptionalRef<const MCPurMechData> data = ShownVariant(*purMech);
        hash = Mixed(hash, static_cast<uint64_t>(purMech->CurVariant + 1) * 4096 +
                               static_cast<uint64_t>(data.has_value() ? data->get().NumAvailable + 1 : 0));
    }

    for (const std::shared_ptr<MCPurPilotData>& pilot : logistics->PurPilotList->Pilots)
    {
        hash = Mixed(hash, static_cast<uint64_t>(pilot->Status));
    }

    for (const std::shared_ptr<MCPurVehicle>& purVehicle : logistics->PurVehicleList->Vehicles)
    {
        hash = Mixed(hash, static_cast<uint64_t>(purVehicle->Data->NumAvailable + 1));
    }

    // The lab's 'Mech: its repairs and its payload.
    if (const std::shared_ptr<MCLogPart> lab = _LabPart.lock(); lab != nullptr && lab->PartType == 1)
    {
        const auto& mech = static_cast<const MCLogMech&>(*lab);
        hash = Mixed(hash, static_cast<uint64_t>(MCMechBay::Points(mech.Armor, false)));
        hash = Mixed(hash, static_cast<uint64_t>(MCMechBay::Points(mech.Internals, false)));
        const std::shared_ptr<MCLogInventoryStat> engine = MCMechBay::EngineOf(mech);
        hash = Mixed(hash, engine != nullptr ? engine->Hits : 0);
        hash = Mixed(hash, static_cast<uint64_t>(std::lround(mech.UsedTonnage * 2)));

        for (const std::shared_ptr<MCLogInventoryItem>& item : mech.Inventory->Items)
        {
            hash = Mixed(hash, static_cast<uint64_t>(item->MasterID) * 256 + item->Stats.size());

            for (const std::shared_ptr<MCLogInventoryStat>& stat : item->Stats)
            {
                hash = Mixed(hash, stat->Hits);
            }
        }
    }

    return hash;
}

bool MCLogisticsTabs::OnFrame()
{
    bool changed = false;

    if (_WhyUntil > 0 && RmlNow() >= _WhyUntil)
    {
        _WhyUntil = 0;
        _WhyThing.reset();
        _Fingerprint = 0;
    }

    // A picked or shown thing gone (sold, a match's teammate took it): nothing picked.
    if (_Picked.has_value() && _Picked->Gone())
    {
        _Picked.reset();
        _Fingerprint = 0;
    }

    if (_Deal.has_value() && _Deal->Thing.Gone())
    {
        DealCancel();
        changed = true;
    }

    if (const uint64_t fingerprint = Fingerprint(); fingerprint != _Fingerprint)
    {
        _Fingerprint = fingerprint;
        Fill();
        changed = true;
    }

    return changed;
}

void MCLogisticsTabs::Fill()
{
    if (GlobalLog() == nullptr)
    {
        return;
    }

    // A 'Mech in the lab that left (sold): the lab empty.
    if (const std::shared_ptr<MCLogPart> lab = _LabPart.lock(); lab == nullptr)
    {
        _LabPart.reset();
    }

    _StockKind = static_cast<int>(Shelf());
    FillUnits();
    FillForceRepair();
    FillStore();
    FillStock();
    FillDetails();
    FillLab();
    FillDeal();
    MarkSwitches();
    Changed();
}

void MCLogisticsTabs::MarkSwitches() const
{
    const MCOptionalRef<Rml::ElementDocument> document = _Screen.Document();

    if (!document.has_value())
    {
        return;
    }

    // The switches as the lists stand, however they were chosen (a click, the tests).
    for (const auto& [id, choice] :
         {std::pair{"kinds", _Kind}, std::pair{"stock-kinds", _StockKind}, std::pair{"variants", _Variant}})
    {
        if (Rml::Element* control = document->get().GetElementById(id);
            control != nullptr && MCRmlKit::Choice(*control) != choice)
        {
            MCRmlKit::SetChoice(*control, choice);
        }
    }
}

void MCLogisticsTabs::FillUnits()
{
    const MCLogistics& logistics = *GlobalLog();
    _Force.clear();
    _Stored.clear();
    _ForceThings.clear();
    _StoredThings.clear();
    const std::shared_ptr<MCLogPart> lab = _LabPart.lock();

    const auto add = [&](const std::shared_ptr<MCLogPart>& part, MCPlace place)
    {
        MCCampaignScreen::MCCard card = _Screen.UnitCard(*part, -1);
        const MCThing thing{.Unit = part, .Place = place};
        card.Picked = Purchasing() ? _Picked.has_value() && !_Picked->Unit.owner_before(thing.Unit) &&
                                         !thing.Unit.owner_before(_Picked->Unit)
                                   : part == lab;

        // A 'Mech without a MechWarrior: its name first, NO PILOT under it (DEPLOY's card keeps the name alone).
        if (card.Unpiloted)
        {
            card.Who = std::move(card.What);
            card.What = std::string(NoPilotWord);
            card.Long = card.Who.size() > MCCampaignScreen::UnitNameLetters;
            card.Longer = card.Who.size() > MCCampaignScreen::SmallNameLetters;
        }

        if (_WhyThing.has_value() && _WhyThing->Same(thing))
        {
            card.Why = _WhyText;
        }

        // The FORCE card a held MechWarrior would board, edged cyan.
        card.Target = place == MCPlace::Force && _Landing == _Force.size();
        (place == MCPlace::Force ? _Force : _Stored).push_back(std::move(card));
        (place == MCPlace::Force ? _ForceThings : _StoredThings).push_back(thing);
    };

    for (const std::shared_ptr<MCLogMech>& mech : logistics.ForceMechList->Mechs)
    {
        add(mech, MCPlace::Force);
    }

    for (const std::shared_ptr<MCLogVehicle>& vehicle : logistics.ForceVehicleList->Vehicles)
    {
        add(vehicle, MCPlace::Force);
    }

    for (const std::shared_ptr<MCLogMech>& mech : logistics.MechList->Mechs)
    {
        add(mech, MCPlace::Stored);
    }

    for (const std::shared_ptr<MCLogVehicle>& vehicle : logistics.VehicleList->Vehicles)
    {
        add(vehicle, MCPlace::Stored);
    }

    _ForceCount = std::format(ForceCountFormat, _Force.size(), MCMechBay::ForceUnits);
}

MCLogisticsTabs::MCStock MCLogisticsTabs::Shelf() const
{
    if (!Purchasing())
    {
        return _BayStock;
    }

    switch (Kind())
    {
        case MCKind::MechWarriors:
            return MCStock::MechWarriors;
        case MCKind::Components:
            return MCStock::Components;
        case MCKind::Mechs:
        case MCKind::Vehicles:
            return MCStock::Mechs;
    }

    return MCStock::Mechs;
}

void MCLogisticsTabs::FillStore()
{
    _Store.clear();
    _StoreThings.clear();

    if (!Purchasing())
    {
        _StoreCount.clear();
        return;
    }

    const MCLogistics& logistics = *GlobalLog();
    const auto add = [this](MCGood good, MCThing thing)
    {
        thing.Place = MCPlace::Store;
        good.Picked = _Picked.has_value() && _Picked->Same(thing);
        good.Long = LongName(good.Who);
        _Store.push_back(std::move(good));
        _StoreThings.push_back(std::move(thing));
    };

    switch (Kind())
    {
        case MCKind::Mechs:
        {
            for (const std::shared_ptr<MCPurMech>& purMech : logistics.PurMechList->Mechs)
            {
                const MCOptionalRef<const MCPurMechData> shownData = ShownVariant(*purMech);

                if (!shownData.has_value())
                {
                    continue;
                }

                const MCPurMechData& data = *shownData;
                add(MCGood{.Picture = MCCampaignMenu::Pictured(MCRmlArt::WholeUnitSource(true, data.NameIndex)),
                           .Who = data.Name,
                           .What = TonsAndClass(data.CurTonnage),
                           .Price = Points(data.Cost),
                           .Note = data.NumAvailable == 0 ? std::string(SoldOutNote) : LeftNote(data.NumAvailable),
                           .Off = data.NumAvailable == 0},
                    MCThing{.StoreMech = purMech});
            }

            break;
        }
        case MCKind::MechWarriors:
        {
            for (const std::shared_ptr<MCPurPilotData>& pilot : logistics.PurPilotList->Pilots)
            {
                if (pilot->Status != MCPurPilotData::ForHire)
                {
                    continue;
                }

                add(MCGood{.Picture = PilotPicture(pilot->NameIndex),
                           .Who = pilot->Callsign,
                           .What = RankWord(pilot->Rank),
                           .Price = Points(pilot->Cost)},
                    MCThing{.StorePilot = pilot});
            }

            break;
        }
        case MCKind::Components:
        {
            for (const std::shared_ptr<MCLogInventoryItem>& item : MCStore::ListedParts())
            {
                add(MCGood{.Picture = PartPicture(*item),
                           .Who = MasterComponentList[item->MasterID].Name,
                           .What = PartWhat(item->MasterID),
                           .Price = Points(MasterComponentList[item->MasterID].ResourcePoints),
                           .Note = item->Count == 0 ? std::string(SoldOutNote) : LeftNote(item->Count),
                           .Off = item->Count == 0,
                           .Part = true},
                    MCThing{.Part = item});
            }

            break;
        }
        case MCKind::Vehicles:
        {
            for (const std::shared_ptr<MCPurVehicle>& purVehicle : logistics.PurVehicleList->Vehicles)
            {
                const MCPurVehicleData& data = *purVehicle->Data;
                add(MCGood{.Picture = MCCampaignMenu::Pictured(MCRmlArt::WholeUnitSource(false, data.NameIndex)),
                           .Who = data.Name,
                           .What = MCCampaignMenu::TonsText(static_cast<int32_t>(std::lround(data.CurTonnage))),
                           .Price = Points(data.Cost),
                           .Note = data.NumAvailable == 0 ? std::string(SoldOutNote) : LeftNote(data.NumAvailable),
                           .Off = data.NumAvailable == 0},
                    MCThing{.StoreVehicle = purVehicle});
            }

            break;
        }
    }

    constexpr std::array<std::pair<std::string_view, std::string_view>, 4> StoreWords = {
        {{MechWord, MechsWord},
         {MechWarriorWord, MechWarriorsWord},
         {PartWord, PartsWord},
         {VehicleWord, VehiclesWord}}};
    const auto& [one, many] = StoreWords[static_cast<size_t>(Kind())];
    _StoreCount = CountText(_Store.size(), one, many);
}

void MCLogisticsTabs::FillStock()
{
    _StockGoods.clear();
    _StockThings.clear();
    const MCLogistics& logistics = *GlobalLog();

    // MECHS: the units stored (FillUnits made their cards): "3 mechs", or with a vehicle among them "4 units".
    if (Stock() == MCStock::Mechs)
    {
        const size_t units = _Stored.size();
        const bool vehicles = !logistics.VehicleList->Vehicles.empty();
        _StockCount = vehicles ? CountText(units, UnitWord, UnitsWord) : CountText(units, MechWord, MechsWord);
        return;
    }

    const bool pilots = Stock() == MCStock::MechWarriors;
    const std::shared_ptr<MCLogMech> mech = WorkedMech();
    const auto add = [this](MCGood good, MCThing thing)
    {
        thing.Place = MCPlace::Stock;
        good.Picked = _Picked.has_value() && _Picked->Same(thing);
        good.Long = LongName(good.Who);

        if (_WhyThing.has_value() && _WhyThing->Same(thing))
        {
            good.Why = _WhyText;
            good.Off = true;
        }

        _StockGoods.push_back(std::move(good));
        _StockThings.push_back(std::move(thing));
    };

    if (pilots)
    {
        for (const std::shared_ptr<MCLogWarrior>& warrior : logistics.WarriorList->Warriors)
        {
            MCGood good{.Picture = MCCampaignMenu::Portrait(MCCampaignMenu::PilotSource(warrior->Picture)),
                        .Who = warrior->Callsign,
                        .What = RankWord(warrior->Rank),
                        .Warrior = true,
                        .Health = static_cast<int>(std::clamp(std::round(warrior->Health), 0.0f, HealthPips))};
            // The 'MECH BAY: a MechWarrior boards the lab's 'Mech when it is a force 'Mech of the player's own.
            good.Off = !Purchasing() && (mech == nullptr || !MCMechBay::CanBoard(*mech));
            add(std::move(good), MCThing{.Warrior = warrior});
        }
    }
    else
    {
        std::vector<std::shared_ptr<MCLogInventoryItem>> parts;

        for (const std::shared_ptr<MCLogInventoryItem>& item : logistics.ComponentInventory->Items)
        {
            if (item->ListIndex >= 0)
            {
                parts.push_back(item);
            }
        }

        std::ranges::stable_sort(parts, {}, &MCLogInventoryItem::ListIndex);

        for (const std::shared_ptr<MCLogInventoryItem>& item : parts)
        {
            MCGood good{.Picture = PartPicture(*item),
                        .Who = MasterComponentList[item->MasterID].Name,
                        .What = PartWhat(item->MasterID),
                        .Price = std::to_string(item->Count),
                        .Note = std::string(OwnedNote),
                        .Part = true};

            // The 'MECH BAY: dim with the reason it can't go into the lab's 'Mech (1998's CantMount).
            if (!Purchasing() && mech != nullptr)
            {
                const MCMechBay::MCFit why =
                    MCMechBay::WhyNotFit(item->MasterID, MCMechBay::FittedTons(item->MasterID), std::cref(*mech));

                if (why != MCMechBay::MCFit::None)
                {
                    good.Off = true;
                    good.Why = MCLogisticsTabsWords::FitWhy(why);
                }
            }

            add(std::move(good), MCThing{.Part = item});
        }
    }

    _StockCount = pilots ? CountText(_StockGoods.size(), MechWarriorWord, MechWarriorsWord)
                         : CountText(_StockGoods.size(), PartWord, PartsWord);
}

void MCLogisticsTabs::FillDetails()
{
    _InfoShown = false;
    _InfoName.clear();
    _InfoWhat.clear();
    _InfoPicture = MCCampaignScreen::NoPicture;
    _Variants = 0;
    _Stats.clear();
    _Rated = false;
    _Kit.clear();
    _Description.clear();
    _InfoCost.clear();
    _InfoNote.clear();
    _DealWord = std::string(BuyWord);
    _CanDeal = false;

    if (!Purchasing())
    {
        return;
    }

    const std::optional<MCThing>& shown = _Hovered.has_value() && !_Hovered->Gone() ? _Hovered : _Picked;

    if (!shown.has_value())
    {
        return;
    }

    const MCThing& thing = *shown;
    _InfoShown = true;

    if (const std::shared_ptr<MCPurMech> purMech = thing.StoreMech.lock(); purMech != nullptr)
    {
        const MCOptionalRef<const MCPurMechData> shownData = ShownVariant(*purMech);

        if (!shownData.has_value())
        {
            _InfoShown = false;
            return;
        }

        const MCPurMechData& data = *shownData;
        _InfoName = data.Name;
        _InfoWhat = TonsAndClass(data.CurTonnage);
        _InfoPicture = MCCampaignMenu::Pictured(MCRmlArt::WholeUnitSource(true, data.NameIndex));
        const std::array<std::reference_wrapper<bool>, MCPurMech::NumVariants> offs = {_VariantOff0, _VariantOff1,
                                                                                       _VariantOff2};

        for (size_t variant = 0; variant < purMech->Variants.size(); ++variant)
        {
            const MCPurMechData* shown = purMech->Variants[variant].get();
            offs[variant].get() = shown == nullptr || shown->NumAvailable == 0;
            _Variants += shown != nullptr ? 1 : 0;
        }

        _Variant = std::max(purMech->CurVariant, 0);
        const int32_t internals = std::ranges::fold_left(data.CurInternalStructure, 0, std::plus<>());
        _Stats = {{"Tons", MCCampaignMenu::TonsText(static_cast<int32_t>(std::lround(data.CurTonnage)))},
                  {"Class", ClassWord(data.CurTonnage)},
                  {"Speed", std::format(SpeedFormat, data.MaxRunSpeed)},
                  {"Armor", GameWord(ArmorClassString(data.ArmorTonnage))},
                  {"Structure", GameWord(InternalClassString(internals))}};
        _Rated = true;
        _Rating = RatingPercent(data.BattleRating);

        if (data.Inventory != nullptr)
        {
            _Kit = KitOf(*data.Inventory);
        }

        _Description = MCBriefingText::Plain(data.Description);
        _InfoCost = MCGroupedPoints(data.Cost);
        _InfoNote = data.NumAvailable < 0 ? std::string() : std::format(InStoreFormat, data.NumAvailable);
        _CanDeal = MCStore::WhyNotBuy(*purMech) == MCStore::MCRefusal::None;
    }
    else if (const std::shared_ptr<MCPurPilotData> pilot = thing.StorePilot.lock(); pilot != nullptr)
    {
        _InfoName = pilot->Callsign;
        _InfoWhat = RankWord(pilot->Rank);
        _InfoPicture = PilotPicture(pilot->NameIndex);
        const std::array<char, MCLogWarrior::NumSkills> skills = {pilot->Piloting, pilot->Jumping, pilot->Sensors,
                                                                  pilot->Gunnery};

        for (size_t at = 0; at < SkillOrder.size(); ++at)
        {
            _Stats.push_back({std::string(SkillWords[at]), std::to_string(static_cast<int>(skills[SkillOrder[at]]))});
        }

        _Description = MCBriefingText::Plain(pilot->Description);
        _InfoCost = MCGroupedPoints(pilot->Cost);
        _CanDeal = MCStore::WhyNotHire(*pilot) == MCStore::MCRefusal::None;
    }
    else if (const std::shared_ptr<MCPurVehicle> purVehicle = thing.StoreVehicle.lock(); purVehicle != nullptr)
    {
        const MCPurVehicleData& data = *purVehicle->Data;
        _InfoName = data.Name;
        _InfoWhat = MCCampaignMenu::TonsText(static_cast<int32_t>(std::lround(data.CurTonnage)));
        _InfoPicture = MCCampaignMenu::Pictured(MCRmlArt::WholeUnitSource(false, data.NameIndex));
        _Stats = {{"Speed", std::format(SpeedFormat, data.MaxMoveSpeed)},
                  {"Armor", GameWord(ArmorClassString(data.ArmorTonnage))}};

        if (data.Inventory != nullptr)
        {
            _Kit = KitOf(*data.Inventory);
        }

        _Description = MCBriefingText::Plain(data.Description);
        _InfoCost = MCGroupedPoints(data.Cost);
        _InfoNote = data.NumAvailable < 0 ? std::string() : std::format(InStoreFormat, data.NumAvailable);
        _CanDeal = MCStore::WhyNotBuy(*purVehicle) == MCStore::MCRefusal::None;
    }
    else if (const std::shared_ptr<MCLogInventoryItem> part = thing.Part.lock(); part != nullptr)
    {
        const MCMasterComponent& component = MasterComponentList[part->MasterID];
        const MCStore::MCComponentTexts texts = MCStore::ComponentTexts(part->MasterID);
        _InfoName = component.Name;
        _InfoWhat = PartWhat(part->MasterID);
        _InfoPicture = PartPicture(*part);
        _Stats = {
            {"Range", texts.Range}, {"Damage", texts.Damage}, {"Recycle", texts.Recycle}, {"Weight", texts.Weight}};
        _Description = MCBriefingText::Plain(part->Description);

        if (thing.Place == MCPlace::Store)
        {
            _InfoCost = MCGroupedPoints(component.ResourcePoints);
            _InfoNote = part->Count < 0 ? std::string() : std::format(InStoreFormat, part->Count);
            _CanDeal = MCStore::WhyNotBuy(*part) == MCStore::MCRefusal::None;
        }
        else
        {
            _InfoCost = MCGroupedPoints(MCStore::SalePrice(*part));
            _InfoNote = std::format("{} {}", part->Count, OwnedNote);
            _DealWord = std::string(SellWord);
            _CanDeal = part->Count > 0;
        }
    }
    else if (const std::shared_ptr<MCLogWarrior> warrior = thing.Warrior.lock(); warrior != nullptr)
    {
        _InfoName = warrior->Callsign;
        _InfoWhat = RankWord(warrior->Rank);
        _InfoPicture = MCCampaignMenu::Portrait(MCCampaignMenu::PilotSource(warrior->Picture));

        for (size_t at = 0; at < SkillOrder.size(); ++at)
        {
            _Stats.push_back(
                {std::string(SkillWords[at]), std::to_string(static_cast<int>(warrior->Skills[SkillOrder[at]]))});
        }

        _Description = MCBriefingText::Plain(warrior->Description);
        _InfoCost = MCGroupedPoints(MCStore::SalePrice(*warrior));
        _DealWord = std::string(SellWord);
        _CanDeal = true;
    }
    else if (const std::shared_ptr<MCLogPart> unit = thing.Unit.lock(); unit != nullptr)
    {
        _InfoName = unit->FileName;
        _InfoWhat = TonsAndClass(unit->CurTonnage);
        _InfoPicture = MCCampaignMenu::Pictured(MCRmlArt::UnitSource(*unit));
        _DealWord = std::string(SellWord);
        _Rated = unit->PartType == 1;
        _Rating = RatingPercent(unit->BattleRating);

        if (unit->PartType == 1)
        {
            auto& mech = static_cast<MCLogMech&>(*unit);
            _Stats = {{"Tons", MCCampaignMenu::TonsText(static_cast<int32_t>(std::lround(mech.CurTonnage)))},
                      {"Class", ClassWord(mech.CurTonnage)},
                      {"Speed", std::format(SpeedFormat, mech.MaxRunSpeed)},
                      {"Armor", GameWord(ArmorClassString(mech.ArmorTonnage))}};
            _Kit = OwnedKitOf(*mech.Inventory);
        }

        // Only a stored unit sells (1998's inventory alone did), and none the mission needs.
        if (unit->Required)
        {
            _InfoNote = std::string(RequiredWhy);
        }
        else if (thing.Place == MCPlace::Stored)
        {
            _InfoCost = MCGroupedPoints(unit->PartType == 1 ? MCStore::SalePrice(static_cast<MCLogMech&>(*unit))
                                                            : MCStore::SalePrice(static_cast<MCLogVehicle&>(*unit)));
            _CanDeal = true;
        }
    }
    else
    {
        _InfoShown = false;
    }
}

std::shared_ptr<MCLogMech> MCLogisticsTabs::WorkedMech() const
{
    const std::shared_ptr<MCLogPart> lab = _LabPart.lock();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (lab == nullptr || lab->PartType != 1 || logistics == nullptr || !In(logistics->ForceMechList->Mechs, *lab))
    {
        return nullptr;
    }

    return std::static_pointer_cast<MCLogMech>(lab);
}

void MCLogisticsTabs::FillLab()
{
    _LabKind = 0;
    _LabName.clear();
    _LabWhat.clear();
    _LabPicture = MCCampaignScreen::NoPicture;
    _LabStored = false;
    _Repairs = false;
    _HasPilot = false;
    _Seat = false;
    _Fits.clear();
    _FitThings.clear();
    _Skills.clear();

    const std::shared_ptr<MCLogPart> lab = _LabPart.lock();

    if (Purchasing() || lab == nullptr)
    {
        return;
    }

    const MCLogistics& logistics = *GlobalLog();
    _LabName = lab->FileName;
    _LabWhat = TonsAndClass(lab->CurTonnage);
    _LabPicture = MCCampaignMenu::Pictured(MCRmlArt::UnitSource(*lab));
    // A unit the INVENTORY stores says so, and goes into the force from here (INTO THE FORCE).
    _LabStored = In(logistics.MechList->Mechs, *lab) || In(logistics.VehicleList->Vehicles, *lab);

    if (_LabStored)
    {
        _LabWhat = std::format(WhatFormat, StoredWord, _LabWhat);
    }

    if (lab->PartType != 1)
    {
        // A vehicle: its outline coloured for damage, its tons and class, nothing else.
        _LabKind = 2;
        return;
    }

    auto& mech = static_cast<MCLogMech&>(*lab);
    const std::shared_ptr<MCLogMech> worked = WorkedMech();
    // A force 'Mech is worked on; a stored one is shown as it is (1998 never showed one in the lab). No repairs in a
    // match.
    _LabKind = worked != nullptr ? 1 : 3;
    _Repairs = worked != nullptr && MultiPlayer() == nullptr;

    const int32_t armor = MCMechBay::Points(mech.Armor, false);
    const int32_t armorMost = MCMechBay::Points(mech.Armor, true);
    const int32_t structure = MCMechBay::Points(mech.Internals, false);
    const int32_t structureMost = MCMechBay::Points(mech.Internals, true);
    const std::shared_ptr<MCLogInventoryStat> engine = MCMechBay::EngineOf(mech);
    const int hits = engine != nullptr ? std::min<int>(engine->Hits, EngineLevels) : 0;
    const auto rest = [&mech](MCMechBay::MCKnob knob)
    {
        const int32_t cost = MCMechBay::RestCost(mech, knob);
        return cost > 0 ? Points(cost) : std::string();
    };

    _Armor = {.Now = std::format(OfFormat, armor, armorMost),
              .Cost = rest(MCMechBay::MCKnob::Armor),
              .Knob = MCMechBay::KnobAt(mech, MCMechBay::MCKnob::Armor)};
    _Structure = {.Now = std::format(OfFormat, structure, structureMost),
                  .Cost = rest(MCMechBay::MCKnob::Internal),
                  .Knob = MCMechBay::KnobAt(mech, MCMechBay::MCKnob::Internal)};
    _Engine = {.Now = hits == 0 ? std::string(WholeWord) : std::string(DamagedWord),
               .Cost = rest(MCMechBay::MCKnob::Engine),
               .Knob = EngineLevels - hits};
    const int32_t allCost = MCMechBay::RestCost(mech, MCMechBay::MCKnob::Armor) +
                            MCMechBay::RestCost(mech, MCMechBay::MCKnob::Internal) +
                            MCMechBay::RestCost(mech, MCMechBay::MCKnob::Engine);
    _RepairAllWord = allCost > 0 ? std::format(RepairAllFormat, MCGroupedPoints(allCost)) : std::string(RepairAllWord);
    _CanRepairAll = MCMechBay::StructureDamaged(mech);

    // The MechWarrior aboard.
    if (worked != nullptr)
    {
        if (const std::shared_ptr<MCLogWarrior> warrior = AboardOf(logistics, mech); warrior != nullptr)
        {
            _HasPilot = true;
            _PilotName = warrior->Callsign;
            _PilotRank = RankWord(warrior->Rank);
            _PilotPicture = MCCampaignMenu::Pictured(MCCampaignMenu::PilotSource(warrior->Picture));
            _PilotHealth = static_cast<int>(std::clamp(std::round(warrior->Health), 0.0f, HealthPips));

            for (size_t at = 0; at < SkillOrder.size(); ++at)
            {
                _Skills.push_back(
                    {std::string(SkillWords[at]), std::to_string(static_cast<int>(warrior->Skills[SkillOrder[at]]))});
            }
        }
    }

    // No MechWarrior aboard (a stored 'Mech never has one): the pilot's place an empty seat.
    _Seat = !_HasPilot;

    // The payload: the weapons' weight against the free weight (1998's tonnage bar), each fitted copy a row.
    const float free = mech.FreeTonnage;
    _PayloadNow = std::format("{:.1f}", mech.WeaponTonnage);
    _PayloadOf = std::format(PayloadOfFormat, free);
    const float lit = free > 0 ? std::clamp(mech.WeaponTonnage / free, 0.0f, 1.0f) : 0.0f;
    const auto litSegments = static_cast<size_t>(std::lround(lit * static_cast<float>(PayloadSegments)));

    for (size_t segment = 0; segment < _PayloadRamp.size(); ++segment)
    {
        _PayloadRamp[segment].State =
            static_cast<int>(segment < litSegments ? MCCampaignScreen::MCLit::On : MCCampaignScreen::MCLit::Dark);
    }

    _Free = std::format(FreeFormat, std::max(mech.CurTonnage - mech.UsedTonnage, 0.0f));
    _CanRefit = false;

    // A match lists only the player's own 'Mechs' weapons (1998's ShowsInventory).
    if (MultiPlayer() != nullptr && worked == nullptr)
    {
        return;
    }

    // In 1998's order: the short-, medium- and long-range weapons, each by damage, lowest first, then the equipment.
    for (const MCMechBay::MCFittedCopy& fitted : MCMechBay::FittedCopies(mech))
    {
        const MCLogInventoryItem& item = *fitted.Item;
        const bool weapon = IsWeapon(ComponentForm(item.MasterID));
        _Fits.push_back({.Range = weapon ? MCStore::ComponentTexts(item.MasterID).Range : std::string(EquipmentWord),
                         .Name = item.Name,
                         .Tons = std::format(TonsFormat, MCMechBay::FittedTons(item.MasterID)),
                         .State = fitted.Copy->Hits != 0 ? std::string(DamagedWord) : std::string()});
        _FitThings.push_back(MCThing{
            .Part = fitted.Item, .ItemNum = static_cast<uint8_t>(fitted.Copy->ItemNum), .Place = MCPlace::Fitted});
    }

    _CanRefit = MCMechBay::ItemsDamaged(mech);
}

void MCLogisticsTabs::FillDeal()
{
    _DealOpen = _Deal.has_value();

    if (!_Deal.has_value())
    {
        return;
    }

    const MCDeal& deal = *_Deal;
    const int64_t total = static_cast<int64_t>(deal.Each) * deal.Count;
    _DealTitle = std::string(deal.Sale ? SellWord : BuyWord);
    _DealEach = Points(deal.Each);
    _DealTotal = Points(static_cast<int32_t>(total));
    const int64_t left = deal.Sale ? ResourcePoints + total : ResourcePoints - total;
    _DealLeft = MCGroupedPoints(static_cast<int32_t>(std::clamp<int64_t>(left, 0, INT32_MAX)));
}

// The cards under the mouse and the keys

std::optional<std::pair<MCPlace, size_t>> MCLogisticsTabs::CardOf(MCOptionalRef<const Rml::Element> element)
{
    // Up RmlUi's tree from the element: a FORCE card's MechWarrior's photo is theirs, the rest of the card its 'Mech's.
    bool photo = false;

    for (const Rml::Element* at = element.has_value() ? &element->get() : nullptr; at != nullptr;
         at = at->GetParentNode())
    {
        const std::string& id = at->GetId();
        photo = photo || at->IsClassSet(PhotoClass);

        if (id == PilotId)
        {
            return std::pair{MCPlace::Pilot, size_t{0}};
        }

        if (const std::optional<size_t> index = MCCampaignMenu::NumberAfter(id, ForcePrefix);
            index.has_value() && photo)
        {
            return std::pair{MCPlace::Aboard, *index};
        }

        for (const auto& [prefix, place] :
             {std::pair{ForcePrefix, MCPlace::Force}, std::pair{StoredPrefix, MCPlace::Stored},
              std::pair{StorePrefix, MCPlace::Store}, std::pair{StockPrefix, MCPlace::Stock},
              std::pair{FitPrefix, MCPlace::Fitted}})
        {
            if (const std::optional<size_t> index = MCCampaignMenu::NumberAfter(id, prefix); index.has_value())
            {
                return std::pair{place, *index};
            }
        }
    }

    return std::nullopt;
}

std::optional<MCLogisticsTabs::MCSpot> MCLogisticsTabs::SpotAt(float x, float y) const
{
    const MCOptionalRef<Rml::ElementDocument> document = _Screen.Document();

    if (!document.has_value())
    {
        return std::nullopt;
    }

    // The cards first: a unit card is its group; then the columns' boxes.
    Rml::Element* under = _Screen.Layer().Context().GetElementAtPoint(Rml::Vector2f(x, y));

    if (const std::optional<std::pair<MCPlace, size_t>> card = CardOf(MCOptionalOf(under)); card.has_value())
    {
        switch (card->first)
        {
            case MCPlace::Pilot:
                return MCSpot{.Place = MCPlace::Fitted};
            case MCPlace::Aboard:
                return MCSpot{.Place = MCPlace::Force, .Card = card->second};
            default:
                return MCSpot{.Place = card->first, .Card = card->second};
        }
    }

    // The INVENTORY's column is its MECHS' while they show, else its goods'.
    const MCPlace inventory = Stock() == MCStock::Mechs ? MCPlace::Stored : MCPlace::Stock;

    for (const auto& [id, place] :
         {std::pair{UnitsId, MCPlace::Force}, std::pair{StoreId, MCPlace::Store}, std::pair{InfoId, MCPlace::Store},
          std::pair{LabId, MCPlace::Fitted}, std::pair{InventoryId, inventory}})
    {
        Rml::Element* column = document->get().GetElementById(std::string(id));

        if (column == nullptr || !column->IsVisible(true))
        {
            continue;
        }

        const Rml::Vector2f corner = column->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f size = column->GetBox().GetSize(Rml::BoxArea::Border);

        if (x >= corner.x && x < corner.x + size.x && y >= corner.y && y < corner.y + size.y)
        {
            return MCSpot{.Place = place};
        }
    }

    return std::nullopt;
}

std::optional<MCLogisticsTabs::MCThing> MCLogisticsTabs::ThingAt(MCPlace place, size_t index) const
{
    const auto from = [index](const std::vector<MCThing>& things) -> std::optional<MCThing>
    { return index < things.size() ? std::optional(things[index]) : std::nullopt; };

    switch (place)
    {
        case MCPlace::Force:
            return from(_ForceThings);
        case MCPlace::Stored:
            return from(_StoredThings);
        case MCPlace::Store:
            return from(_StoreThings);
        case MCPlace::Stock:
            return from(_StockThings);
        case MCPlace::Fitted:
            return from(_FitThings);
        case MCPlace::Pilot:
            return _HasPilot ? std::optional(MCThing{.Unit = _LabPart, .Place = MCPlace::Pilot}) : std::nullopt;
        case MCPlace::Aboard:
            return index < _Force.size() && _Force[index].Piloted ? from(_ForceThings) : std::nullopt;
    }

    return std::nullopt;
}

std::shared_ptr<MCLogPart> MCLogisticsTabs::UnitAt(MCPlace place, size_t index) const
{
    const std::optional<MCThing> thing = ThingAt(place, index);
    return thing.has_value() ? thing->Unit.lock() : nullptr;
}

void MCLogisticsTabs::OnMouse(Rml::Event& event)
{
    const auto x = event.GetParameter<float>("mouse_x", 0.0f);
    const auto y = event.GetParameter<float>("mouse_y", 0.0f);
    const int button = event.GetParameter<int>("button", LeftButton);

    // The deal window takes the mouse: its own buttons are its clicks.
    if (_Deal.has_value())
    {
        return;
    }

    switch (event.GetId())
    {
        case Rml::EventId::Mousemove:
        {
            if (_Hold.has_value())
            {
                if (!_Hold->Dragging && std::hypot(x - _Hold->StartX, y - _Hold->StartY) >= DragPixels)
                {
                    _Hold->Dragging = true;
                    PlayLogSound(PickedSound);
                    using MCGoods = MCOptionalRef<const std::vector<MCGood>>;
                    const MCGoods goods = _Hold->From == MCPlace::Store   ? MCGoods(_Store)
                                          : _Hold->From == MCPlace::Stock ? MCGoods(_StockGoods)
                                                                          : std::nullopt;
                    MCGood lifted;

                    if (goods.has_value() && _Hold->Index < goods->get().size())
                    {
                        lifted = goods->get()[_Hold->Index];
                    }
                    else if (const std::vector<MCCampaignScreen::MCCard>& cards =
                                 _Hold->From == MCPlace::Force ? _Force : _Stored;
                             (_Hold->From == MCPlace::Force || _Hold->From == MCPlace::Stored) &&
                             _Hold->Index < cards.size())
                    {
                        lifted = {.Picture = cards[_Hold->Index].Outline,
                                  .Who = cards[_Hold->Index].Who.empty() ? cards[_Hold->Index].What
                                                                         : cards[_Hold->Index].Who,
                                  .What = cards[_Hold->Index].Tons};
                    }
                    else if (_Hold->From == MCPlace::Fitted && _Hold->Index < _Fits.size())
                    {
                        lifted = {.Who = _Fits[_Hold->Index].Name, .What = _Fits[_Hold->Index].Tons};
                    }
                    else if (_Hold->From == MCPlace::Pilot)
                    {
                        lifted = {.Picture = _PilotPicture, .Who = _PilotName, .What = _PilotRank};
                    }
                    else if (const std::shared_ptr<MCLogMech> mech = PilotSource(_Hold->From, _Hold->Index);
                             mech != nullptr)
                    {
                        // A FORCE card's photo: their MechWarrior, not the 'Mech.
                        if (const std::shared_ptr<MCLogWarrior> warrior = AboardOf(*GlobalLog(), *mech);
                            warrior != nullptr)
                        {
                            lifted = {.Picture = _Force[_Hold->Index].Pilot,
                                      .Who = warrior->Callsign,
                                      .What = RankWord(warrior->Rank)};
                        }
                    }

                    lifted.Picked = false;
                    lifted.Off = false;
                    lifted.Long = LongName(lifted.Who);
                    _Lifted = {lifted};
                }

                if (_Hold->Dragging)
                {
                    _LiftedLeft = std::format("{:.0f}px", x + LiftedOffset);
                    _LiftedTop = std::format("{:.0f}px", y + LiftedOffset);

                    if (const std::optional<size_t> landing = LandingAt(x, y); landing != _Landing)
                    {
                        _Landing = landing;
                        FillUnits();
                    }

                    Changed();
                }

                return;
            }

            // PURCHASING's details: what is under the pointer.
            if (!Purchasing())
            {
                return;
            }

            const std::optional<std::pair<MCPlace, size_t>> card = CardOf(MCOptionalOf(event.GetTargetElement()));
            const std::optional<MCThing> over = card.has_value() ? ThingAt(card->first, card->second) : std::nullopt;

            if (over.has_value() != _Hovered.has_value() || (over.has_value() && !over->Same(*_Hovered)))
            {
                _Hovered = over;
                FillDetails();
                Changed();
            }

            return;
        }
        case Rml::EventId::Mouseup:
        {
            if (button != LeftButton || !_Hold.has_value())
            {
                return;
            }

            const MCHold hold = *_Hold;
            _Hold.reset();
            _Landing.reset();
            _Lifted.clear();

            if (hold.Dragging)
            {
                const std::optional<MCSpot> spot = SpotAt(x, y);
                Drop(hold.From, hold.Index, spot.has_value() ? std::optional(spot->Place) : std::nullopt,
                     spot.has_value() ? spot->Card : std::nullopt);
            }
            else
            {
                Pick(hold.From, hold.Index);
            }

            Changed();
            return;
        }
        case Rml::EventId::Mousedown:
        {
            const std::optional<std::pair<MCPlace, size_t>> card = CardOf(MCOptionalOf(event.GetTargetElement()));

            if (!card.has_value())
            {
                return;
            }

            if (button == RightButton)
            {
                Send(card->first, card->second);
            }
            else if (button == LeftButton)
            {
                _Hold = MCHold{.From = card->first, .Index = card->second, .StartX = x, .StartY = y};
            }

            return;
        }
        default:
            return;
    }
}

bool MCLogisticsTabs::OnKey(int key, MCOptionalRef<const Rml::Element> focus)
{
    if (_Deal.has_value())
    {
        switch (key)
        {
            case Rml::Input::KI_LEFT:
            case Rml::Input::KI_RIGHT:
            {
                // 1998's arrows: a click, or the refusal at an end.
                const int count = _Deal->Count + (key == Rml::Input::KI_LEFT ? -1 : 1);

                if (count < 1 || count > _DealCountMost)
                {
                    PlayLogSound(RefusedSound);
                    return true;
                }

                PlayLogSound(ClickSound);
                DealCount(count);

                if (const MCOptionalRef<Rml::ElementDocument> document = _Screen.Document(); document.has_value())
                {
                    if (Rml::Element* stepper = document->get().GetElementById(DealCountId); stepper != nullptr)
                    {
                        MCRmlKit::SetChoice(*stepper, count - 1);
                    }
                }

                return true;
            }
            case Rml::Input::KI_RETURN:
            case Rml::Input::KI_NUMPADENTER:
            {
                if (focus.has_value() && focus->get().GetId() == DealCancelId)
                {
                    DealCancel();
                }
                else
                {
                    DealConfirm();
                }

                return true;
            }
            case Rml::Input::KI_TAB:
            case Rml::Input::KI_ESCAPE:
                return false;
            default:
                // Nothing behind the window takes a key (Q and E's tabs, the cards).
                return true;
        }
    }

    const std::optional<std::pair<MCPlace, size_t>> card = CardOf(focus);
    const std::string focusId = focus.has_value() ? focus->get().GetId() : std::string();

    // A repair slider with the focus: Left and Right move it a step, as 1998's knob a pixel.
    for (const auto& [id, knob] :
         {std::pair{"armor", MCMechBay::MCKnob::Armor}, std::pair{"structure", MCMechBay::MCKnob::Internal},
          std::pair{"engine", MCMechBay::MCKnob::Engine}})
    {
        if (focusId == id && (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT))
        {
            const MCRepair& repair = knob == MCMechBay::MCKnob::Armor      ? _Armor
                                     : knob == MCMechBay::MCKnob::Internal ? _Structure
                                                                           : _Engine;
            MoveSlider(knob, repair.Knob + (key == Rml::Input::KI_LEFT ? -1 : 1));
            return true;
        }
    }

    if (!card.has_value())
    {
        return false;
    }

    switch (key)
    {
        case Rml::Input::KI_RETURN:
        case Rml::Input::KI_NUMPADENTER:
        {
            Send(card->first, card->second);
            return true;
        }
        case Rml::Input::KI_SPACE:
        {
            Pick(card->first, card->second);
            return true;
        }
        default:
            return false;
    }
}

bool MCLogisticsTabs::Back()
{
    if (_Deal.has_value())
    {
        DealCancel();
        return true;
    }

    if (_Hold.has_value())
    {
        _Hold.reset();
        _Lifted.clear();

        if (std::exchange(_Landing, std::nullopt).has_value())
        {
            FillUnits();
        }

        Changed();
        return true;
    }

    return false;
}

// The moves

void MCLogisticsTabs::ChooseKind(MCKind kind)
{
    _Kind = static_cast<int>(kind);
    _Picked.reset();
    _Hovered.reset();
    _Fingerprint = 0;
    Fill();
}

void MCLogisticsTabs::ChooseStock(MCStock stock)
{
    // PURCHASING's INVENTORY follows the store's kind, and its switch chooses it (MECHS keeps VEHICLES).
    if (Purchasing())
    {
        if (stock != Shelf())
        {
            ChooseKind(stock == MCStock::MechWarriors ? MCKind::MechWarriors
                       : stock == MCStock::Components ? MCKind::Components
                                                      : MCKind::Mechs);
        }

        return;
    }

    _BayStock = stock;
    _Fingerprint = 0;
    Fill();
}

void MCLogisticsTabs::ChooseVariant(int variant)
{
    // 1998's variant buttons: the store mech shows (and sells) the variant chosen. Their click is the switch's own (the
    // kit's button press), once.
    const std::optional<MCThing>& shown = _Hovered.has_value() ? _Hovered : _Picked;
    const std::shared_ptr<MCPurMech> purMech = shown.has_value() ? shown->StoreMech.lock() : nullptr;

    if (purMech == nullptr || variant < 0 || static_cast<size_t>(variant) >= purMech->Variants.size() ||
        purMech->Variants[static_cast<size_t>(variant)] == nullptr)
    {
        return;
    }

    purMech->CurVariant = variant;
    _Fingerprint = 0;
    Fill();
}

void MCLogisticsTabs::Pick(MCPlace place, size_t index)
{
    const std::optional<MCThing> thing = ThingAt(place, index);

    if (!thing.has_value())
    {
        return;
    }

    if (Purchasing())
    {
        _Picked = thing;
        _Hovered.reset();
    }
    else if (const std::shared_ptr<MCLogPart> unit = thing->Unit.lock();
             unit != nullptr && (place == MCPlace::Force || place == MCPlace::Stored || place == MCPlace::Aboard))
    {
        // The 'MECH BAY: the unit into the lab, its knobs where it arrived.
        PlayLogSound(PlacedSound);
        _LabPart = unit;

        if (unit->PartType == 1)
        {
            _Knobs = MCMechBay::KnobsOf(static_cast<const MCLogMech&>(*unit));
        }
    }
    else
    {
        _Picked = thing;
    }

    _Fingerprint = 0;
    Fill();
}

void MCLogisticsTabs::Send(MCPlace place, size_t index)
{
    const std::optional<MCThing> thing = ThingAt(place, index);

    if (!thing.has_value())
    {
        return;
    }

    switch (place)
    {
        case MCPlace::Force:
        case MCPlace::Stored:
        {
            // PURCHASING: a stored unit's sale, as a store card's purchase; the 'MECH BAY: in or out of the force.
            if (Purchasing())
            {
                OfferDeal(*thing);
            }
            else
            {
                MoveUnit(*thing);
            }

            break;
        }
        case MCPlace::Store:
            OfferDeal(*thing);
            break;
        case MCPlace::Stock:
        {
            if (Purchasing())
            {
                OfferDeal(*thing);
            }
            else if (const std::shared_ptr<MCLogInventoryItem> part = thing->Part.lock(); part != nullptr)
            {
                FitPart(*part);
            }
            else if (const std::shared_ptr<MCLogWarrior> warrior = thing->Warrior.lock(); warrior != nullptr)
            {
                // Into the lab's 'Mech.
                if (const std::shared_ptr<MCLogMech> mech = WorkedMech(); mech != nullptr)
                {
                    BoardPilot(*warrior, *mech);
                }
                else
                {
                    PlayLogSound(RefusedSound);
                }
            }

            break;
        }
        case MCPlace::Fitted:
            TakeOutPart(index);
            break;
        case MCPlace::Pilot:
        case MCPlace::Aboard:
        {
            // The lab's MechWarrior, or a FORCE card's by their photo, out to the INVENTORY.
            if (const std::shared_ptr<MCLogMech> mech = PilotSource(place, index); mech != nullptr)
            {
                PilotLeaves(*mech);
            }

            break;
        }
    }

    _Fingerprint = 0;
    Fill();
}

void MCLogisticsTabs::Drop(MCPlace from, size_t index, std::optional<MCPlace> onto, std::optional<size_t> card)
{
    const std::optional<MCThing> thing = ThingAt(from, index);

    if (!thing.has_value())
    {
        return;
    }

    // The INVENTORY's column, whichever of its lists shows.
    const bool inventory = onto == MCPlace::Stored || onto == MCPlace::Stock;
    // The 'Mech a MechWarrior let go there boards (the user's, 26 Nov): a FORCE card's, or the lab's.
    const std::shared_ptr<MCLogMech> boarded = Purchasing()                                 ? nullptr
                                               : onto == MCPlace::Force && card.has_value() ? ForceMechAt(*card)
                                               : onto == MCPlace::Fitted                    ? WorkedMech()
                                                                                            : nullptr;
    const std::shared_ptr<MCLogMech> source = PilotSource(from, index);
    const std::shared_ptr<MCLogWarrior> spare = from == MCPlace::Stock ? thing->Warrior.lock() : nullptr;

    // Where each card goes (anywhere else, it goes back where it was).
    if ((from == MCPlace::Force && inventory) || (from == MCPlace::Stored && onto == MCPlace::Force))
    {
        MoveUnit(*thing);
    }
    else if ((from == MCPlace::Stored || from == MCPlace::Stock) && Purchasing() && onto == MCPlace::Store)
    {
        OfferDeal(*thing);
    }
    else if (from == MCPlace::Store && inventory)
    {
        OfferDeal(*thing);
    }
    else if (from == MCPlace::Stock && !Purchasing() && onto == MCPlace::Fitted)
    {
        Send(from, index);
    }
    else if ((from == MCPlace::Fitted || from == MCPlace::Pilot || from == MCPlace::Aboard) && inventory)
    {
        Send(from, index);
    }
    else if (spare != nullptr && !Purchasing() && onto == MCPlace::Force)
    {
        // An INVENTORY MechWarrior onto a FORCE card: aboard it (its MechWarrior, if any, to the INVENTORY).
        if (boarded != nullptr)
        {
            BoardPilot(*spare, *boarded);
        }
        else
        {
            PlayLogSound(RefusedSound);
        }
    }
    else if (source != nullptr && boarded != nullptr && boarded != source)
    {
        // The lab's MechWarrior, or a FORCE card's by their photo, into another 'Mech (the one aboard to the INVENTORY).
        MovePilot(*source, *boarded);
    }
    else
    {
        // Back where it came from: a card on its own place, a photo on its own card.
        const bool back = onto == from || (from == MCPlace::Aboard && onto == MCPlace::Force && card == index) ||
                          (from == MCPlace::Pilot && boarded != nullptr && boarded == source);
        PlayLogSound(back ? PlacedSound : RefusedSound);
    }

    _Fingerprint = 0;
    Fill();
}

void MCLogisticsTabs::MoveUnit(const MCThing& thing)
{
    const std::shared_ptr<MCLogPart> unit = thing.Unit.lock();

    if (unit == nullptr)
    {
        return;
    }

    if (thing.Place == MCPlace::Force)
    {
        OutOfForce(*unit);
    }
    else if (IntoForce(*unit))
    {
        // Into the lab, its knobs where it arrived, to be worked on.
        _LabPart = unit;

        if (unit->PartType == 1)
        {
            _Knobs = MCMechBay::KnobsOf(static_cast<const MCLogMech&>(*unit));
        }
    }
}

void MCLogisticsTabs::IntoTheForce()
{
    const std::shared_ptr<MCLogPart> lab = _LabPart.lock();

    if (lab == nullptr || !_LabStored)
    {
        return;
    }

    MoveUnit(MCThing{.Unit = lab, .Place = MCPlace::Stored});
    _Fingerprint = 0;
    Fill();
}

bool MCLogisticsTabs::IntoForce(MCLogPart& unit)
{
    PlayLogSound(PickedSound);
    const bool joined = unit.PartType == 1 ? MCMechBay::JoinForce(static_cast<MCLogMech&>(unit))
                                           : MCMechBay::JoinForce(static_cast<MCLogVehicle&>(unit));

    if (!joined)
    {
        PlayLogSound(RefusedSound);
        ShowLogMessage(ForceFullMessage);
    }
    else
    {
        PlayLogSound(PlacedSound);
    }

    if (unit.PartType == 1)
    {
        GlobalLog()->NumberMechs();
    }
    else
    {
        GlobalLog()->NumberVehicles();
    }

    return joined;
}

void MCLogisticsTabs::OutOfForce(MCLogPart& unit)
{
    PlayLogSound(PlacedSound);

    if (unit.PartType == 1)
    {
        auto& mech = static_cast<MCLogMech&>(unit);
        MCMechBay::OutOfDrop(mech);

        if (MCMechBay::LeaveForce(mech))
        {
            GlobalLog()->NumberPilots();
        }

        GlobalLog()->NumberMechs();
        return;
    }

    MCMechBay::LeaveForce(static_cast<MCLogVehicle&>(unit));
    GlobalLog()->NumberVehicles();
}

void MCLogisticsTabs::FitPart(MCLogInventoryItem& part)
{
    const std::shared_ptr<MCLogMech> mech = WorkedMech();
    const MCMechBay::MCFit why =
        MCMechBay::WhyNotFit(part.MasterID, MCMechBay::FittedTons(part.MasterID),
                             mech != nullptr ? MCOptionalRef<const MCLogMech>(std::cref(*mech)) : std::nullopt);

    if (mech == nullptr || why != MCMechBay::MCFit::None || part.Count <= 0)
    {
        PlayLogSound(RefusedSound);
        return;
    }

    // 1998's pick-up takes one copy off the row (the last one's row goes); its drop on the 'Mech fits it, or gives it
    // back.
    PlayLogSound(PickedSound);

    if (--part.Count == 0)
    {
        GlobalLog()->ReIndexInventory();
    }

    if (!MCMechBay::Fit(*mech, part.MasterID))
    {
        ShowLogMessage(TooHeavyMessage);
        ++part.Count;
        GlobalLog()->ReIndexInventory();
        PlayLogSound(RefusedSound);
        return;
    }

    PlayLogSound(PlacedSound);
}

void MCLogisticsTabs::TakeOutPart(size_t fitted)
{
    const std::shared_ptr<MCLogMech> mech = WorkedMech();
    const std::optional<MCThing> thing = ThingAt(MCPlace::Fitted, fitted);
    const std::shared_ptr<MCLogInventoryItem> item = thing.has_value() ? thing->Part.lock() : nullptr;

    if (mech == nullptr || item == nullptr)
    {
        PlayLogSound(RefusedSound);
        return;
    }

    PlayLogSound(PickedSound);
    const std::optional<MCMechBay::MCTaken> taken = MCMechBay::TakeOut(*mech, *item, thing->ItemNum);

    if (!taken.has_value())
    {
        return;
    }

    // Into the component inventory (a damaged part is thrown away): a spare back from none gets its row again.
    MCMechBay::OutOfDrop(*mech);
    PlayLogSound(PlacedSound);
    std::shared_ptr<MCLogInventoryItem> spare;

    if (MCMechBay::ToSpares(*taken, spare) == MCMechBay::MCSpareRow::Listed)
    {
        GlobalLog()->ReIndexInventory();
    }
}

void MCLogisticsTabs::BoardPilot(MCLogWarrior& warrior, MCLogMech& mech)
{
    if (!MCMechBay::CanBoard(mech))
    {
        PlayLogSound(RefusedSound);
        return;
    }

    // 1998's pick-up assigns the MechWarrior; its drop on the 'Mech boards it. One aboard goes back to the INVENTORY
    // first (1998 took only an empty 'Mech: its pilot was dragged off before).
    PlayLogSound(PickedSound);

    if (mech.PilotIndex >= 0 && MCMechBay::PilotOut(mech) != nullptr)
    {
        GlobalLog()->NumberPilots();
    }

    MCMechBay::Assign(warrior);
    MCMechBay::Board(warrior, mech, mech.SlotIndex);
    GlobalLog()->NumberPilots();
    SoundSystem()->PlayPilotSpeech(warrior.PilotAudio, PilotSpeech);
}

void MCLogisticsTabs::PilotLeaves(MCLogMech& mech)
{
    // 1998's pilot spot lifts only a 'Mech's own MechWarrior (MCMechRepairBlock: PilotIndex checked first).
    if (mech.PilotIndex < 0)
    {
        return;
    }

    PlayLogSound(PickedSound);

    if (const std::shared_ptr<MCLogWarrior> pilot = MCMechBay::PilotOut(mech); pilot != nullptr)
    {
        GlobalLog()->NumberPilots();
        SoundSystem()->PlayPilotSpeech(pilot->PilotAudio, PilotSpeech);
    }
}

void MCLogisticsTabs::MovePilot(MCLogMech& from, MCLogMech& to)
{
    // Port-only (the user's, 26 Nov): out of one 'Mech to the INVENTORY, as 1998's drag onto it, then aboard the other,
    // as an INVENTORY card's drag onto it; nothing moves unless both may (the other a 'Mech of the player's own).
    if (&from == &to || from.PilotIndex < 0 || !MCMechBay::CanBoard(from) || !MCMechBay::CanBoard(to))
    {
        PlayLogSound(RefusedSound);
        return;
    }

    const std::shared_ptr<MCLogWarrior> pilot = MCMechBay::PilotOut(from);

    if (pilot == nullptr)
    {
        PlayLogSound(RefusedSound);
        return;
    }

    GlobalLog()->NumberPilots();
    BoardPilot(*pilot, to);
}

std::shared_ptr<MCLogMech> MCLogisticsTabs::ForceMechAt(size_t index) const
{
    const std::shared_ptr<MCLogPart> unit = index < _ForceThings.size() ? _ForceThings[index].Unit.lock() : nullptr;
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (unit == nullptr || unit->PartType != 1 || logistics == nullptr || !In(logistics->ForceMechList->Mechs, *unit))
    {
        return nullptr;
    }

    return std::static_pointer_cast<MCLogMech>(unit);
}

std::shared_ptr<MCLogMech> MCLogisticsTabs::PilotSource(MCPlace from, size_t index) const
{
    switch (from)
    {
        case MCPlace::Pilot:
            return WorkedMech();
        case MCPlace::Aboard:
            return ForceMechAt(index);
        default:
            return nullptr;
    }
}

std::optional<size_t> MCLogisticsTabs::LandingAt(float x, float y) const
{
    if (!_Hold.has_value() || Purchasing())
    {
        return std::nullopt;
    }

    // Only a MechWarrior lands on a FORCE card: an INVENTORY card's, the lab's, or a FORCE card's photo's.
    const std::optional<MCThing> held = ThingAt(_Hold->From, _Hold->Index);
    const std::shared_ptr<MCLogMech> source = PilotSource(_Hold->From, _Hold->Index);
    const bool warrior = (held.has_value() && !held->Warrior.expired()) || source != nullptr;
    const std::optional<MCSpot> spot = warrior ? SpotAt(x, y) : std::nullopt;

    if (!spot.has_value() || spot->Place != MCPlace::Force || !spot->Card.has_value())
    {
        return std::nullopt;
    }

    const std::shared_ptr<MCLogMech> mech = ForceMechAt(*spot->Card);
    return mech != nullptr && mech != source && MCMechBay::CanBoard(*mech) ? spot->Card : std::nullopt;
}

void MCLogisticsTabs::MoveSlider(MCMechBay::MCKnob knob, int position)
{
    const std::shared_ptr<MCLogMech> mech = WorkedMech();

    if (mech == nullptr || !_Repairs)
    {
        return;
    }

    // The engine's slider counts levels (3 whole); 1998's knob is the place of that level.
    const int32_t place =
        knob == MCMechBay::MCKnob::Engine
            ? MCMechBay::EngineKnob(static_cast<uint8_t>(EngineLevels - std::clamp(position, 0, EngineLevels)))
            : std::clamp<int32_t>(position, MCMechBay::KnobLeft, MCMechBay::KnobRight);

    if (place == MCMechBay::KnobAt(*mech, knob))
    {
        return;
    }

    MCMechBay::OutOfDrop(*mech);

    if (!MCMechBay::MoveKnob(*mech, _Knobs, knob, place).has_value())
    {
        PlayLogSound(RefusedSound);
    }

    _Fingerprint = 0;
    Fill();
}

void MCLogisticsTabs::RepairAll()
{
    const std::shared_ptr<MCLogMech> mech = WorkedMech();

    if (mech == nullptr || !_Repairs || !MCMechBay::StructureDamaged(*mech))
    {
        return;
    }

    MCMechBay::OutOfDrop(*mech);
    PlayLogSound(PickedSound);

    if (MCMechBay::RepairAll(*mech))
    {
        ShowLogMessage(ShortOfPointsMessage);
        PlayLogSound(RefusedSound);
    }

    _Fingerprint = 0;
    Fill();
}

void MCLogisticsTabs::FillForceRepair()
{
    _ForceRepairs = !Purchasing() && MultiPlayer() == nullptr;
    _CanRepairForce = false;
    int32_t cost = 0;

    // What each force 'Mech's REPAIR ALL would cost, as its label says.
    if (_ForceRepairs)
    {
        for (const std::shared_ptr<MCLogMech>& mech : GlobalLog()->ForceMechList->Mechs)
        {
            if (mech->LocalPart != 0 && MCMechBay::StructureDamaged(*mech))
            {
                _CanRepairForce = true;
                cost += MCMechBay::RestCost(*mech, MCMechBay::MCKnob::Armor) +
                        MCMechBay::RestCost(*mech, MCMechBay::MCKnob::Internal) +
                        MCMechBay::RestCost(*mech, MCMechBay::MCKnob::Engine);
            }
        }
    }

    _RepairForceWord = cost > 0 ? std::format(RepairForceFormat, MCGroupedPoints(cost)) : std::string(RepairForceLabel);
}

void MCLogisticsTabs::RepairForce()
{
    if (!_ForceRepairs || !_CanRepairForce)
    {
        return;
    }

    // Each damaged force 'Mech's REPAIR ALL in the force's order (each out of the drop, as its own), until the points
    // run short: then 1998's message once, as REPAIR ALL's, and the rest left as they are.
    PlayLogSound(PickedSound);
    const std::vector<std::shared_ptr<MCLogMech>> force = GlobalLog()->ForceMechList->Mechs;

    for (const std::shared_ptr<MCLogMech>& mech : force)
    {
        if (mech->LocalPart == 0 || !MCMechBay::StructureDamaged(*mech))
        {
            continue;
        }

        MCMechBay::OutOfDrop(*mech);

        if (MCMechBay::RepairAll(*mech))
        {
            ShowLogMessage(ShortOfPointsMessage);
            PlayLogSound(RefusedSound);
            break;
        }
    }

    _Fingerprint = 0;
    Fill();
}

void MCLogisticsTabs::RefitAll()
{
    const std::shared_ptr<MCLogMech> mech = WorkedMech();

    // Only with a weapon, equipment or jammer damaged, as 1998's button (its CanRepairItems): otherwise nothing, the
    // mech left in the drop.
    if (mech == nullptr || !_Repairs || !MCMechBay::ItemsDamaged(*mech))
    {
        return;
    }

    MCMechBay::OutOfDrop(*mech);
    PlayLogSound(PickedSound);
    std::vector<std::shared_ptr<MCLogInventoryItem>> used;
    const std::string missing = MCMechBay::Refit(*mech, used);
    GlobalLog()->ReIndexInventory();

    if (!missing.empty())
    {
        // 1998's refit dialog: whether to strip the damaged parts that have no spare.
        std::string names = missing;
        std::ranges::replace(names, ',', '\n');
        const std::weak_ptr<MCLogMech> weak = mech;
        MCRmlDialogs::Show(MCRmlDialog::MCSay{
            .Text = std::format("{}\n{}\n{}", GameWord(RefitHeadString), names, GameWord(RefitFootString)),
            .Main = "OK",
            .Other = "Cancel",
            .OnMain = [weak]
            {
                if (const std::shared_ptr<MCLogMech> stripped = weak.lock(); stripped != nullptr)
                {
                    MCMechBay::StripUnrepaired(*stripped);
                    GlobalLog()->ReIndexInventory();
                }
            }});
        PlayLogSound(RefusedSound);
    }

    _Fingerprint = 0;
    Fill();
}

// The deal window

void MCLogisticsTabs::OfferDeal(const MCThing& thing)
{
    MCDeal deal{.Thing = thing};
    std::string what;
    std::string picture(MCCampaignScreen::NoPicture);

    if (const std::shared_ptr<MCPurMech> purMech = thing.StoreMech.lock(); purMech != nullptr)
    {
        const MCOptionalRef<const MCPurMechData> shownData = ShownVariant(*purMech);

        // 1998's pick-up refuses with the force at its size; its drop refuses when the points don't pay.
        if (!shownData.has_value() || Refused(MCStore::WhyNotBuy(*purMech)) || CheckMaxUnits())
        {
            return;
        }

        const MCPurMechData& data = *shownData;
        deal = {.Thing = thing, .Each = data.Cost, .Most = MaxPurchase(data.NumAvailable), .Type = BuyMechType};
        _DealName = data.Name;
        what = TonsAndClass(data.CurTonnage);
        picture = MCCampaignMenu::Pictured(MCRmlArt::WholeUnitSource(true, data.NameIndex));
    }
    else if (const std::shared_ptr<MCPurPilotData> pilot = thing.StorePilot.lock(); pilot != nullptr)
    {
        if (Refused(MCStore::WhyNotHire(*pilot)))
        {
            return;
        }

        deal = {.Thing = thing, .Each = pilot->Cost, .Most = 1, .Type = HireType};
        _DealName = pilot->Callsign;
        what = RankWord(pilot->Rank);
        picture = PilotPicture(pilot->NameIndex);
    }
    else if (const std::shared_ptr<MCPurVehicle> purVehicle = thing.StoreVehicle.lock(); purVehicle != nullptr)
    {
        const MCPurVehicleData& data = *purVehicle->Data;

        if (Refused(MCStore::WhyNotBuy(*purVehicle)) || CheckMaxUnits())
        {
            return;
        }

        deal = {.Thing = thing, .Each = data.Cost, .Most = MaxPurchase(data.NumAvailable), .Type = BuyVehicleType};
        _DealName = data.Name;
        what = MCCampaignMenu::TonsText(static_cast<int32_t>(std::lround(data.CurTonnage)));
        picture = MCCampaignMenu::Pictured(MCRmlArt::WholeUnitSource(false, data.NameIndex));
    }
    else if (const std::shared_ptr<MCLogInventoryItem> part = thing.Part.lock(); part != nullptr)
    {
        const MCMasterComponent& component = MasterComponentList[part->MasterID];
        _DealName = component.Name;
        what = PartWhat(part->MasterID);
        picture = PartPicture(*part);

        if (thing.Place == MCPlace::Store)
        {
            // 1998's part store says it can't be bought however it can't (sold out too).
            if (MCStore::WhyNotBuy(*part) != MCStore::MCRefusal::None)
            {
                PlayLogSound(RefusedSound);
                ShowLogMessage(TooPoorMessage);
                return;
            }

            // A part the store has no limit of counts to 1998's most.
            deal = {.Thing = thing,
                    .Each = component.ResourcePoints,
                    .Most = MCStore::MostParts(part->Count),
                    .Type = BuyPartType};
        }
        else
        {
            // None left to sell (the last sold, the part still picked): no sale, as 1998's empty row offered none.
            if (part->Count <= 0)
            {
                PlayLogSound(RefusedSound);
                return;
            }

            deal = {.Thing = thing,
                    .Sale = true,
                    .Each = MCStore::SalePrice(*part),
                    .Most = part->Count,
                    .Type = SellPartType};
        }
    }
    else if (const std::shared_ptr<MCLogWarrior> warrior = thing.Warrior.lock(); warrior != nullptr)
    {
        deal = {.Thing = thing, .Sale = true, .Each = MCStore::SalePrice(*warrior), .Most = 1, .Type = FireType};
        _DealName = warrior->Callsign;
        what = RankWord(warrior->Rank);
        picture = MCCampaignMenu::Portrait(MCCampaignMenu::PilotSource(warrior->Picture));
    }
    else if (const std::shared_ptr<MCLogPart> unit = thing.Unit.lock(); unit != nullptr)
    {
        // Only a stored unit sells (a force's isn't on PURCHASING), and none the mission needs: 1998 never offered
        // its sale, and its card says why.
        if (thing.Place != MCPlace::Stored || unit->Required)
        {
            PlayLogSound(RefusedSound);

            if (unit->Required)
            {
                _WhyThing = thing;
                _WhyText = std::string(RequiredWhy);
                _WhyUntil = RmlNow() + WhySeconds;
            }

            return;
        }

        const bool mech = unit->PartType == 1;
        deal = {.Thing = thing,
                .Sale = true,
                .Each = mech ? MCStore::SalePrice(static_cast<MCLogMech&>(*unit))
                             : MCStore::SalePrice(static_cast<MCLogVehicle&>(*unit)),
                .Most = 1,
                .Type = mech ? SellMechType : SellVehicleType};
        _DealName = unit->FileName;
        what = TonsAndClass(unit->CurTonnage);
        picture = MCCampaignMenu::Pictured(MCRmlArt::UnitSource(*unit));
    }
    else
    {
        return;
    }

    PlayLogSound(PlacedSound);
    _DealWhat = std::move(what);
    _DealPicture = std::move(picture);

    // 1998's dialog: from one, up while under the most and the points pay for one more (the arrows only for a buy and a
    // part's sale).
    int32_t most = 1;
    const bool arrows = (deal.Type & 1) == 0 || deal.Type == SellPartType;

    while (arrows && most < deal.Most && MCStore::PaysFor(deal.Sale ? -deal.Each : deal.Each, most))
    {
        ++most;
    }

    deal.Count = 1;
    _Deal = deal;
    _DealCountMost = most;

    if (const MCOptionalRef<Rml::ElementDocument> document = _Screen.Document(); document.has_value())
    {
        if (Rml::Element* count = document->get().GetElementById(DealCountId); count != nullptr)
        {
            std::string choices = "1";

            for (int32_t n = 2; n <= most; ++n)
            {
                choices += std::format("|{}", n);
            }

            count->SetAttribute(ChoicesAttribute, choices);
            MCRmlKit::SetChoice(*count, 0);
        }

        if (Rml::Element* confirm = document->get().GetElementById(DealConfirmId); confirm != nullptr)
        {
            confirm->Focus(true);
        }
    }

    FillDeal();
    _Screen.SetKeys();
    Changed();
}

void MCLogisticsTabs::DealCount(int count)
{
    if (!_Deal.has_value())
    {
        return;
    }

    _Deal->Count = std::clamp(count, 1, std::max(_DealCountMost, 1));
    FillDeal();
    Changed();
}

void MCLogisticsTabs::DealCancel()
{
    if (!_Deal.has_value())
    {
        return;
    }

    _Deal.reset();
    FillDeal();
    _Screen.SetKeys();
    Changed();
}

void MCLogisticsTabs::DealConfirm()
{
    if (!_Deal.has_value())
    {
        return;
    }

    const MCDeal deal = *_Deal;
    _Deal.reset();
    const MCThing& thing = deal.Thing;

    if (const std::shared_ptr<MCPurMech> purMech = thing.StoreMech.lock(); purMech != nullptr)
    {
        MCStore::BuyMechs(*purMech, deal.Count, deal.Each);
        GlobalLog()->NumberMechs();
        CheckNumUnits();
    }
    else if (const std::shared_ptr<MCPurPilotData> pilot = thing.StorePilot.lock(); pilot != nullptr)
    {
        MCStore::HirePilot(*pilot, deal.Each);
        GlobalLog()->NumberPilots();
        SoundSystem()->PlayPilotSpeech(pilot->PilotAudio, PilotSpeech);

        if (_Picked.has_value() && _Picked->Same(thing))
        {
            _Picked.reset();
        }
    }
    else if (const std::shared_ptr<MCPurVehicle> purVehicle = thing.StoreVehicle.lock(); purVehicle != nullptr)
    {
        MCStore::BuyVehicles(*purVehicle, deal.Count, deal.Each);
        GlobalLog()->NumberVehicles();
        CheckNumUnits();
    }
    else if (const std::shared_ptr<MCLogInventoryItem> part = thing.Part.lock(); part != nullptr)
    {
        if (!deal.Sale)
        {
            std::shared_ptr<MCLogInventoryItem> spare;

            // A spare new, or back from none, gets its row; a counted one keeps its own.
            if (MCStore::BuyParts(*part, deal.Count, deal.Each, spare) == MCStore::MCPartRow::Listed)
            {
                GlobalLog()->ReIndexInventory();
            }
        }
        else if (MCStore::SellParts(*part, deal.Count, deal.Each))
        {
            GlobalLog()->ReIndexInventory();
        }
    }
    else if (const std::shared_ptr<MCLogWarrior> warrior = thing.Warrior.lock(); warrior != nullptr)
    {
        // 1998's sale took the MechWarrior up (assigning it) before the dialog.
        SoundSystem()->PlayPilotSpeech(warrior->PilotAudio, PilotSpeech);
        MCMechBay::Assign(*warrior);
        MCStore::FirePilot(*warrior, deal.Each);
        GlobalLog()->NumberPilots();
    }
    else if (const std::shared_ptr<MCLogPart> unit = thing.Unit.lock(); unit != nullptr)
    {
        if (unit->PartType == 1)
        {
            MCStore::SellMech(static_cast<MCLogMech&>(*unit), deal.Each);
            GlobalLog()->ReIndexInventory();
            GlobalLog()->NumberMechs();
        }
        else
        {
            MCStore::SellVehicle(static_cast<MCLogVehicle&>(*unit), deal.Each);
            GlobalLog()->NumberVehicles();
        }
    }

    _Fingerprint = 0;
    Fill();
    _Screen.SetKeys();
}

void MCLogisticsTabs::PressDeal()
{
    const std::optional<MCThing>& shown = _Hovered.has_value() && !_Hovered->Gone() ? _Hovered : _Picked;

    if (!shown.has_value())
    {
        return;
    }

    OfferDeal(*shown);
}

// MCLogisticsTabsWords

std::string MCLogisticsTabsWords::FitWhy(MCMechBay::MCFit why)
{
    switch (why)
    {
        case MCMechBay::MCFit::None:
            return {};
        case MCMechBay::MCFit::NoMech:
            return "No 'Mech";
        case MCMechBay::MCFit::TooHeavy:
            return "Too heavy";
        case MCMechBay::MCFit::OneFitted:
            return "One fitted";
        case MCMechBay::MCFit::Banned:
            return "Banned in this match";
        case MCMechBay::MCFit::NotThisMech:
            return "Not for this 'Mech";
    }

    return {};
}
