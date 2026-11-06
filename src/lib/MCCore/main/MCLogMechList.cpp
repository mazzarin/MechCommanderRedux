#include "stdafx.h"
#include "main/MCLogMechList.h"
#include "gui/MCScrollPane.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCMechInventoryBlock.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "logistics/MCRepairScreen.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "main/MCLogisticsShared.h"
#include "object/MCBattleMech.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectTypeManager.h"

MCLogMechList::~MCLogMechList()
{
    Clear();
}

auto MCLogMechList::Position(int32_t index) const -> size_t
{
    return index < 0 ? 0 : std::min(static_cast<size_t>(index), Mechs.size());
}

auto MCLogMechList::Delete(size_t position) -> void
{
    const std::shared_ptr<MCLogMech> mech = Mechs[position];

    // Its repair row takes no more clicks (the original took the block out of the pane).
    if (GlobalLog() != nullptr && GlobalLog()->RepairScreen != nullptr)
    {
        GlobalLog()->RepairScreen->HideUnitRow(*mech);
    }

    Mechs.erase(Mechs.begin() + static_cast<ptrdiff_t>(position));
}

auto MCLogMechList::Clear() -> void
{
    while (!Mechs.empty())
    {
        Delete(0);
    }
}

auto MCLogMechList::GetMechIndex(std::shared_ptr<const MCLogMech> mech) const -> int32_t
{
    const auto found =
        std::ranges::find_if(Mechs, [&](const std::shared_ptr<MCLogMech>& entry) { return entry == mech; });
    return found != Mechs.end() ? static_cast<int32_t>(found - Mechs.begin()) : -1;
}

auto MCLogMechList::AddMech(std::string_view fileName, bool required, bool sorted, bool widgets)
    -> std::shared_ptr<MCLogMech>
{
    MCFitIniFile file;
    const int32_t result = file.Open(GamePath(ProfilePath, fileName, ".fit"));
    Assert(result == 0, 0, "(addMech) Could not open file");
    std::shared_ptr<MCLogMech> mech = AddMech(file, required, sorted, widgets);
    mech->ProfileName = std::string(fileName.substr(0, 9));
    file.Close();
    return mech;
}

auto MCLogMechList::ReplaceMech(MCPacketFile& file, int32_t packet) -> int32_t
{
    int32_t pilot = 0;
    {
        MCFitIniFile profile;
        int32_t result = file.SeekPacket(packet);
        Assert(result == 0, 0, " Unable to find Mech file ");
        result = profile.Open(file, static_cast<uint32_t>(file.GetPacketSize()));
        Assert(result == 0, 0, " Unable to open mech file ");
        result = profile.SeekBlock("General");
        Assert(result == 0, static_cast<uint32_t>(result), " Bad Saved Mech file ");
        pilot = ReadRequired<int32_t>(profile, "Pilot", " No Pilot in Saved Mech file ");
    }

    // The mech flown by that pilot is replaced by the saved one.
    const auto found =
        std::ranges::find_if(Mechs, [&](const std::shared_ptr<MCLogMech>& mech) { return mech->PilotIndex == pilot; });

    if (found == Mechs.end())
    {
        return 5;
    }

    // Port fix (OB-090): the original freed the mech but not its widgets, which went on pointing at it (the repair
    // block from the repair screen's list), nor its other names. The port deletes it whole.
    Delete(static_cast<size_t>(found - Mechs.begin()));
    AddMech(file, packet);
    return 0;
}

auto MCLogMechList::AddMech(MCPacketFile& file, int32_t packet) -> std::shared_ptr<MCLogMech>
{
    MCFitIniFile profile;
    int32_t result = file.SeekPacket(packet);
    Assert(result == 0, static_cast<uint32_t>(result), "Campaign file cannot find packet for mech. #1");
    result = profile.Open(file, static_cast<uint32_t>(file.GetPacketSize()));
    Assert(result == 0, static_cast<uint32_t>(result), "Campaign file cannot find packet for mech. #2");
    std::shared_ptr<MCLogMech> mech = AddMech(profile, false, true, true);
    profile.Close();
    return mech;
}

namespace
{
    /// <summary>The profile block of each mech body location, in location order.</summary>
    constexpr std::array<std::string_view, MCLogMech::NumBodyLocations> MechLocationBlocks = {
        "Head", "CenterTorso", "LeftTorso", "RightTorso", "LeftArm", "RightArm", "LeftLeg", "RightLeg"};

    /// <summary>The profile keys of the eleven armor locations (the eight, then the three rear torso ones).</summary>
    constexpr std::array<std::string_view, MCLogMech::NumArmorLocations> MechArmorKeys = {
        "Head",    "CenterTorso", "LeftTorso",       "RightTorso",    "LeftArm",       "RightArm",
        "LeftLeg", "RightLeg",    "RearCenterTorso", "RearLeftTorso", "RearRightTorso"};

    /// <summary>The profile reader's numbered check: "(AddMech) could not find key in profile <paramref name="number"/>".</summary>
    void CheckKey(bool read, int32_t number)
    {
        Assert(read, 0, std::format("(AddMech) could not find key in profile {}", number));
    }

    /// <summary><see cref="CheckKey"/> of a numeric entry read into <paramref name="value"/>.</summary>
    template <MCFitValue T> void ReadKey(MCFitIniFile& file, std::string_view name, T& value, int32_t number)
    {
        CheckKey(ReadEntry(file, name, value), number);
    }

    /// <summary>
    /// Reads a chassis's maximum internal structure from its packet in the object packet file; any failure is fatal
    /// (0xbeef0006). A chassis with no packet keeps none.
    /// </summary>
    void ReadChassisInternals(MCLogMech& mech)
    {
        MCPacketFile objects;
        const int32_t result = objects.Open(GamePath(ObjectPath, ObjectPakName));
        Assert(result == 0, 0, "(AddMech) could not open file 8");

        if (objects.SeekPacket(static_cast<int32_t>(mech.Chassis)) != 0)
        {
            return;
        }

        MCFitIniFile chassis;

        if (chassis.Open(objects, static_cast<uint32_t>(objects.GetPacketSize())) != 0 ||
            chassis.SeekBlock("InternalStructure") != 0)
        {
            Fatal(static_cast<int32_t>(0xbeef0006));
        }

        for (size_t location = 0; location < MCLogMech::NumBodyLocations; ++location)
        {
            const MCFitResult<uint8_t> points = chassis.Read<uint8_t>(MechLocationBlocks[location]);

            if (!points.has_value())
            {
                Fatal(static_cast<int32_t>(0xbeef0006));
            }

            mech.Internals[location].MaxArmor = *points;
        }
    }

    /// <summary>The name of the internal structure's class (from the chassis's total internal structure).</summary>
    uint32_t StructureNameId(const MCLogMech& mech)
    {
        int32_t structure = 0;

        for (const MCLogMech::ArmorPoints& points : mech.Internals)
        {
            structure += points.MaxArmor;
        }

        if (structure < 0x24)
        {
            return 100;
        }

        if (structure < 0x38)
        {
            return 0x4f;
        }

        if (structure < 0x51)
        {
            return 0x65;
        }

        return structure < 0x79 ? 0x51 : 0x66;
    }

    /// <summary>The name of the chassis class (from the armor tonnage).</summary>
    uint32_t ChassisNameId(float armorTonnage)
    {
        if (armorTonnage <= 2.0f)
        {
            return 100;
        }

        if (armorTonnage <= 7.0f)
        {
            return 0x4f;
        }

        if (armorTonnage <= 12.0f)
        {
            return 0x65;
        }

        return armorTonnage > 17.0f ? 0x66 : 0x51;
    }

    /// <summary>The name of the jump jets' class (from the count of the last jump jet item).</summary>
    uint32_t JumpJetNameId(const MCInventoryList& inventory)
    {
        int32_t jumpJets = 0;

        for (const std::shared_ptr<MCLogInventoryItem>& entry : inventory.Items)
        {
            if (MasterComponentList[entry->MasterID].Form == MCComponentForm::JumpJet)
            {
                jumpJets = entry->Count;
            }
        }

        if (jumpJets == 0)
        {
            return 0x6c;
        }

        jumpJets = jumpJets * 2 / 3;

        if (jumpJets < 2)
        {
            return 0x56;
        }

        return jumpJets < 4 ? 0x55 : 0x50;
    }
}

auto MCLogMechList::AddMech(MCFitIniFile& file, bool required, bool sorted, bool widgets) -> std::shared_ptr<MCLogMech>
{
    auto mech = std::make_shared<MCLogMech>();
    mech->LocalPart = true;

    // OB-089 (fixed): a profile without a [General] block was read as a saved mech list (raw record images) into a
    // mech that was never added; nothing in MCX.EXE writes such a file.
    if (file.SeekBlock("General") != 0)
    {
        Fatal(BLOCK_NOT_FOUND, " Mech profile has no General Block ", file.GetFilename());
    }

    mech->Inventory = std::make_unique<MCInventoryList>();
    mech->PilotIndex = -1;
    int32_t result = file.SeekBlock("Header");
    Assert(result == 0, 0, "(AddMech) could not find Header in profile. 0");
    // The texts are read into one buffer, as the original did: a missing one leaves the text read before it.
    std::string text;
    const bool typeRead = ReadText(file, "FileType", 0x14, text);
    Assert(typeRead, 0, "(AddMech) could not find key in profile 1");

    // Original behaviour: a FileType other than MechProfile only fails when it could not be read at all.
    if (text != "MechProfile")
    {
        Assert(typeRead, 0, "(AddMech) could not find key in profile 2");
    }

    CheckKey(file.SeekBlock("General") == 0, 3);
    ReadText(file, "Name", 0x7f, text);
    mech->MechName = text;
    ReadKey(file, "CurTonnage", mech->CurTonnage, 4);
    ReadKey(file, "Status", mech->Status, 5);

    if (mech->Status == 1 || mech->Status == 2)
    {
        mech->Status = 0;
    }

    mech->ResourcePoints = file.Read<int32_t>("ResourcePoints").value_or(100);
    mech->NameIndex = ReadRequired<int32_t>(file, "NameIndex", "(AddMech) could not find NameIndex");
    mech->NameVariant = ReadRequired<int32_t>(file, "NameVariant", "(AddMech) could not fine NameVariant");
    mech->BaseResourcePoints = mech->ResourcePoints;
    mech->DescIndex = -1;
    ReadEntry(file, "DescIndex", mech->DescIndex);
    mech->LoadDescription(mech->DescIndex);
    mech->FileName = LoadLogString(static_cast<uint32_t>(mech->DescIndex + 300));
    CheckKey(ReadText(file, "icon", 0x7f, text), 6);
    mech->IconName = text;
    ReadKey(file, "Chassis", mech->Chassis, 7);
    mech->ChassisBR = file.Read<int32_t>("ChassisBR").value_or(100);
    ReadChassisInternals(*mech);
    mech->Assigned = file.Read<bool>("Assigned").value_or(false);
    mech->Deployed = file.Read<bool>("Deployed").value_or(false);
    mech->Required = file.Read<bool>("Required").value_or(false);
    mech->NotMineYet = file.Read<bool>("NotMineYet").value_or(false);
    mech->PilotIndex = file.Read<int32_t>("Pilot").value_or(-1);
    CheckKey(ReadText(file, "MechType", 0x28, text), 9);

    // The sort key: the name's place in the mech order, three variants apart.
    // Original behaviour: variant 1 sorts after variant 2 (1 gets +2, 2 gets +1).
    // Port fix: a name index past the order table read past it; the port gives it the first place.
    const int32_t order = mech->NameIndex >= 0 && static_cast<size_t>(mech->NameIndex) < MechSort.size()
                              ? MechSort[static_cast<size_t>(mech->NameIndex)]
                              : 0;
    mech->SortKey = order * 3 + (mech->NameVariant == 1 ? 2 : (mech->NameVariant == 2 ? 1 : 0));

    CheckKey(file.SeekBlock("Engine") == 0, 14);
    ReadKey(file, "Tonnage", mech->EngineTonnage, 15);
    ReadKey(file, "Rating", mech->EngineRating, 16);
    ReadKey(file, "MaxRunSpeed", mech->MaxRunSpeed, 17);
    CheckKey(file.SeekBlock("Armor") == 0, 18);
    ReadKey(file, "Type", mech->ArmorType, 19);
    ReadKey(file, "Tonnage", mech->ArmorTonnage, 20);
    CheckKey(file.SeekBlock("MaxArmorPoints") == 0, 21);
    mech->SellValue = file.Read<int32_t>("SellValue").value_or(0x32);

    for (size_t location = 0; location < MCLogMech::NumArmorLocations; ++location)
    {
        ReadKey(file, MechArmorKeys[location], mech->Armor[location].MaxArmor, 22 + static_cast<int32_t>(location));
    }

    CheckKey(file.SeekBlock("CurArmorPoints") == 0, 33);

    for (size_t location = 0; location < MCLogMech::NumArmorLocations; ++location)
    {
        uint8_t points = 0;
        ReadKey(file, MechArmorKeys[location], points, 34 + static_cast<int32_t>(location));
        mech->Armor[location].CurArmor = points;
    }

    CheckKey(file.SeekBlock("InventoryInfo") == 0, 45);
    ReadKey(file, "NumOther", mech->NumOther, 46);
    ReadKey(file, "NumWeapons", mech->NumWeapons, 47);
    ReadKey(file, "NumAmmo", mech->NumAmmo, 48);

    // Original behaviour: 0xc0 bytes of the slots are cleared, the first five locations and a third of the sixth.
    std::span<MCLogMech::ItemSlot> slots(mech->ItemSlots.front().data(),
                                         MCLogMech::NumBodyLocations * MCLogMech::MaxCriticalSlots);
    std::ranges::fill(slots.first(0xc0 / sizeof(MCLogMech::ItemSlot)),
                      MCLogMech::ItemSlot{MCLogMech::EmptySlot, MCLogMech::EmptySlot, MCLogMech::EmptySlot});

    mech->FreeTonnage = 0.0f;
    mech->WeaponTonnage = 0.0f;
    mech->UsedTonnage =
        static_cast<float>(static_cast<double>(mech->CurTonnage) * 0.1f + mech->ArmorTonnage + mech->EngineTonnage);
    MCInventoryList& inventory = *mech->Inventory;
    int32_t item = 0;
    const int32_t numOther = mech->NumOther;

    // Seeks Item:n and reads its MasterID with the two numbered checks.
    auto readItem = [&](int32_t blockCheck, int32_t idCheck)
    {
        CheckKey(file.SeekBlock(std::format("Item:{}", item)) == 0, blockCheck);
        uint8_t masterID = 0;
        ReadKey(file, "MasterID", masterID, idCheck);
        return masterID;
    };

    // Adds the copy and its tonnage and price.
    auto add = [&](uint8_t masterID, uint8_t facing, int16_t amount, bool weapon)
    {
        inventory.AddItem(masterID, inventory.CreateStat(static_cast<uint8_t>(item), 0, facing, amount, 0xff), false);
        const MCMasterComponent& master = MasterComponentList[masterID];
        mech->UsedTonnage += master.Tonnage;

        if (weapon || master.Form == MCComponentForm::Sensor || master.Form == MCComponentForm::Ecm ||
            master.Form == MCComponentForm::Probe || master.Form == MCComponentForm::Jammer)
        {
            mech->WeaponTonnage += master.Tonnage;
        }

        mech->ResourcePoints += master.ResourcePoints;
    };

    for (; item < numOther; ++item)
    {
        add(readItem(49, 50), 0, 1, false);
    }

    for (const int32_t weaponEnd = numOther + mech->NumWeapons; item < weaponEnd; ++item)
    {
        const uint8_t masterID = readItem(51, 52);
        uint8_t facesForward = 0;
        ReadKey(file, "FacesForward", facesForward, 53);
        add(masterID, facesForward, 1, true);
    }

    for (const int32_t ammoEnd = numOther + mech->NumAmmo + mech->NumWeapons; item < ammoEnd; ++item)
    {
        const uint8_t masterID = readItem(54, 55);

        // The amount is read (as a long, else a byte) but not used: ammunition copies get -1.
        if (!file.Read<int32_t>("Amount").has_value())
        {
            CheckKey(file.Read<uint8_t>("Amount").has_value(), 56);
        }

        add(masterID, 0, -1, true);
    }

    mech->FreeTonnage = (mech->CurTonnage - mech->UsedTonnage) + mech->WeaponTonnage;

    for (size_t location = 0; location < MCLogMech::NumBodyLocations; ++location)
    {
        CheckKey(file.SeekBlock(MechLocationBlocks[location]) == 0, 57);
        uint8_t hasCase = 0;
        ReadKey(file, "CASE", hasCase, 58);
        mech->HasCase[location] = hasCase != 0 ? 1 : 0;
        ReadKey(file, "CurInternalStructure", mech->Internals[location].CurArmor, 59);
        ReadKey(file, "HotSpotNumber", mech->HotSpotNumber[location], 60);

        // The critical slots only carry the damage of the copies they hold.
        for (int32_t space = 0; space < NumLocationCriticalSpaces[location]; ++space)
        {
            std::array<uint8_t, 2> slot{};
            CheckKey(file.ReadArray(std::format("Component:{}", space), std::span<uint8_t>(slot)).has_value(), 61);

            if (slot[0] >= inventory.NextStatID || slot[1] == 0)
            {
                continue;
            }

            // Port fix: a copy number with no copy left (the original read a null item's master id).
            const std::shared_ptr<MCLogInventoryItem> owner = inventory.GetItemStatIndex(slot[0]);

            if (owner == nullptr)
            {
                continue;
            }

            const MCComponentForm form = MasterComponentList[owner->MasterID].Form;

            if (form == MCComponentForm::Weapon || form == MCComponentForm::WeaponEnergy ||
                form == MCComponentForm::WeaponMissile || form == MCComponentForm::WeaponBallistic ||
                form == MCComponentForm::Sensor || form == MCComponentForm::Engine || form == MCComponentForm::Ecm ||
                form == MCComponentForm::Probe)
            {
                inventory.HitItem(slot[0], slot[1]);
            }
        }
    }

    mech->Deployed = false;

    if (!mech->Required)
    {
        mech->Required = required;
    }

    // In by sort key when sorted, else at the front.
    auto position = Mechs.begin();

    if (sorted)
    {
        position = std::ranges::find_if(Mechs, [&](const std::shared_ptr<MCLogMech>& other)
                                        { return mech->SortKey <= other->SortKey; });
    }

    const std::shared_ptr<MCLogMech> added = *Mechs.insert(position, std::move(mech));
    added->TakeArrivalState();

    if (widgets)
    {
        // The repair sliders' knobs (the repair block's init placed them; the repair screen makes the rows now).
        MCMechRepairBlock::PlaceSliders(*added);
        added->ReadWithWidgets = true;
        // The inventory row (the inventory block's init set it; the list makes the rows now).
        added->ListIndex = added->NameIndex;
        // (The original also made the mech's briefing box here; the briefing screen makes the box it shows.)
    }

    added->CalcBR();
    added->CalcPilotModifier();

    // The names shown: weight class (from the tonnage), chassis class (from the armor tonnage), the internal
    // structure's class and the jump jets' class.
    static constexpr std::array<uint32_t, 4> weightNames = {0x4f, 0x50, 0x51, 0x52};
    added->WeightClassName = LoadLogString(weightNames[static_cast<size_t>(LogWeightClass(added->CurTonnage))]);
    added->ChassisClassName = LoadLogString(ChassisNameId(added->ArmorTonnage));
    added->ExtraName1 = LoadLogString(StructureNameId(*added));
    added->ExtraName2 = LoadLogString(JumpJetNameId(inventory));
    return added;
}

auto MCLogMechList::AddMech(std::shared_ptr<MCLogMech> mech, bool sorted) -> void
{
    // In by tonnage when sorted, else at the front.
    auto position = Mechs.begin();

    if (sorted)
    {
        position = std::ranges::find_if(Mechs, [&](const std::shared_ptr<MCLogMech>& other)
                                        { return mech->CurTonnage <= other->CurTonnage; });
    }

    Mechs.insert(position, std::move(mech));
}

auto MCLogMechList::ExtractMech(int32_t index) -> std::shared_ptr<MCLogMech>
{
    const size_t position = Position(index);

    if (index >= GetMechCount() || position >= Mechs.size())
    {
        return nullptr;
    }

    std::shared_ptr<MCLogMech> mech = std::move(Mechs[position]);
    Mechs.erase(Mechs.begin() + static_cast<ptrdiff_t>(position));
    return mech;
}

auto MCLogMechList::RemoveMech(int32_t index) -> int32_t
{
    const size_t position = Position(index);

    if (index >= GetMechCount() || position >= Mechs.size())
    {
        return -1;
    }

    Delete(position);
    return 0;
}

auto MCLogMechList::RemoveMech(const MCLogMech& mech) -> int32_t
{
    const auto found =
        std::ranges::find_if(Mechs, [&](const std::shared_ptr<MCLogMech>& entry) { return entry.get() == &mech; });
    const int32_t index = found != Mechs.end() ? static_cast<int32_t>(found - Mechs.begin()) : -1;

    if (index < 0)
    {
        return -1;
    }

    Delete(static_cast<size_t>(index));
    return 0;
}

auto MCLogMechList::GetMechPilotIndex(int32_t index) const -> int32_t
{
    std::shared_ptr<MCLogMech> mech;
    return GetMechInfo(index, mech) == 0 ? mech->PilotIndex : -1;
}

auto MCLogMechList::GetMechInfo(int32_t index, std::shared_ptr<MCLogMech>& mech) const -> int32_t
{
    const size_t position = Position(index);
    mech = index < GetMechCount() && position < Mechs.size() ? Mechs[position] : nullptr;
    return mech != nullptr ? 0 : -1;
}

auto MCLogMechList::SaveMechText(std::string_view fileName, int32_t index) const -> int32_t
{
    std::shared_ptr<MCLogMech> mech;

    if (GetMechInfo(index, mech) != 0)
    {
        return -1;
    }

    return MCMissionLogisticsBridge::LogisticsMechProfileWriter(fileName, *mech, false);
}
