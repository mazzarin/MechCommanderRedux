#include "stdafx.h"
#include "object/MCBuildingMarines.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "mission/MCScenario.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCObjectList.h"
#include "object/MCObjectSystem.h"
#include "object/MCTeam.h"
#include "sprite/MCElementalActor.h"

int32_t DefaultPilotId = 0x28d;
int32_t NumMarines = 0;

namespace
{
    /// <summary>The marine profile opened, or the open's error.</summary>
    std::expected<std::unique_ptr<MCFitIniFile>, int32_t> OpenMarineProfile()
    {
        auto profile = std::make_unique<MCFitIniFile>();

        if (const int32_t result = profile->Open(GamePath(ProfilePath, MarineProfileName, ".fit")); result != 0)
        {
            return std::unexpected(result);
        }

        return profile;
    }
}

std::unique_ptr<MCFitIniFile> ReadMarineProfile()
{
    return OpenMarineProfile().value_or(nullptr);
}

void LoadMarineProfile(MCMover& marine)
{
    std::unique_ptr<MCFitIniFile>& profile = Scenario()->MarineProfile;

    if (profile == nullptr)
    {
        std::expected<std::unique_ptr<MCFitIniFile>, int32_t> opened = OpenMarineProfile();

        if (!opened.has_value())
        {
            Fatal(opened.error(), " Unable to open Vehicle Marine Profile ");
        }

        profile = std::move(*opened);
    }

    if (marine.LoadProfile(*profile) != 0)
    {
        Fatal(-1, " Bad Vehicle Marine Profile File ");
    }
}

std::shared_ptr<MCMover> MakeMarine(std::string_view createFailure)
{
    std::shared_ptr<MCMover> marine = CreateObjectAs<MCMover>(DefaultPilotId);

    if (marine == nullptr)
    {
        Fatal(-1, createFailure);
    }

    marine->SetAwake(1);
    LoadMarineProfile(*marine);
    return marine;
}

void LetOutBuildingMarines(MCBigGameObject& building, int32_t marinesWanted, const std::function<void(MCMover&)>& place)
{
    if (marinesWanted == 0)
    {
        return;
    }

    int32_t marinesMade = 0;
    const auto numWarriors = static_cast<int32_t>(Scenario()->NumWarriors());

    // Each marine is piloted by an enemy warrior with no working vehicle (none, disabled or destroyed); warrior 0 is
    // never used.
    for (int32_t i = 0; i < numWarriors; i++)
    {
        if (i <= 0 || static_cast<uint32_t>(i) > Scenario()->NumWarriors())
        {
            continue;
        }

        std::shared_ptr<MCMechWarrior> warrior = Scenario()->Warrior(i);

        if (warrior == nullptr || warrior->Alignment == HomeTeam()->Alignment)
        {
            continue;
        }

        if (LockObject(warrior->Vehicle) != nullptr)
        {
            const auto vehicleStatus = static_cast<int8_t>(LockObject(warrior->Vehicle)->Status);

            if (vehicleStatus != 2 && vehicleStatus != 1)
            {
                continue;
            }
        }

        std::shared_ptr<MCMover> newMarine = MakeMarine(" Couldnt create Marine for Building ");
        const std::shared_ptr<MCMover> marine = newMarine;
        marine->SetPilot(warrior);
        warrior->SetVehicle(marine);
        warrior->Lobotomy();
        marine->SetControl(2, 3, -1);
        marine->SetTeam(ClanTeam());
        place(*marine);
        auto marineAppearance = std::static_pointer_cast<MCElementalActor>(marine->GetAppearance());

        if (marineAppearance != nullptr)
        {
            marineAppearance->SetGesture(0);
            marineAppearance->FadeTableIndex = building.GetAlignment() == -1 ? 0x1c : 0x12;
        }

        marine->IdNumber = 2500000;
        marine->SetPartId(0xfff - NumMarines++);
        marine->SetAlignment(building.GetAlignment());
        std::shared_ptr<MCObjectList> list = building.GetAlignment() == -1 ? ClanMechList() : InnerSphereMechList();

        if (list != nullptr)
        {
            list->Add(std::move(newMarine));
        }

        marine->SetPotentialContact(0);
        marine->SetExists(1);
        warrior->ClearAttackOrders();
        warrior->ClearMoveOrders();
        warrior->OrderMoveToPoint(0, 1, MCOrderOrigin::Player, MCVector3D(0.0f, 0.0f, 0.0f), -1, 1);

        if (++marinesMade == marinesWanted)
        {
            return;
        }
    }
}
