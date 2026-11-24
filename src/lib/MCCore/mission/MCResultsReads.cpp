#include "stdafx.h"
#include "mission/MCResultsReads.h"
#include "gui/MCGuiScrollTextObject.h"
#include "iface/MCFriendlyMechIcon.h"
#include "iface/MCTacticalInterface.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "terrain/MCTacticalMap.h"

auto MCResultsReads::Card(const MCMechWarrior& warrior) -> std::shared_ptr<MCFriendlyMechIcon>
{
    const std::shared_ptr<MCGameObject> vehicle = LockObject(warrior.Vehicle);
    const std::shared_ptr<MCTacticalInterface> tactical = TacticalInterface();
    return vehicle != nullptr && tactical != nullptr ? tactical->GetMechIconFromID(vehicle->PartId) : nullptr;
}

auto MCResultsReads::Salvage() -> std::vector<std::shared_ptr<MCGameObject>>
{
    std::vector<std::shared_ptr<MCGameObject>> salvage;

    if (const std::shared_ptr<MCTacticalMap> map = TacticalMap(); map != nullptr)
    {
        for (std::shared_ptr<MCGameObject>& object : map->Salvage.Pins())
        {
            if (object != nullptr)
            {
                salvage.push_back(std::move(object));
            }
        }
    }

    return salvage;
}

auto MCResultsReads::SalvageText() -> std::shared_ptr<MCGuiScrollTextObject>
{
    const std::shared_ptr<MCTacticalMap> map = TacticalMap();
    return map != nullptr ? map->SalvageText.Lock() : nullptr;
}
