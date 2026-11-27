#include "stdafx.h"
#include "ai/MCRefit.h"
#include "object/MCMasterComponent.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCMechWarrior.h"
#include "network/MCMultiPlayer.h"
#include "sound/MCSoundSystem.h"

int GRepairWalkOut = 0;

auto MCRepairWalkOutInForce() -> bool
{
    return GRepairWalkOut != 0 && MultiPlayer() == nullptr;
}

namespace
{
    /// <summary>The arms (locations 4 and 5) aren't repaired once destroyed.</summary>
    bool SkipLocation(MCMover& mover, int32_t location)
    {
        return (location == 4 || location == 5) && mover.BodyAt(location).DamageState == 2;
    }

    /// <summary>Whether a location's internal structure or armor is below its maximum.</summary>
    bool LocationNeedsRepair(MCMover& mover, int32_t location)
    {
        return (location < mover.NumBodyLocations() &&
                mover.BodyAt(location).CurInternalStructure <
                    static_cast<float>(mover.BodyAt(location).MaxInternalStructure)) ||
               mover.Armor[location].CurArmor < static_cast<float>(mover.Armor[location].MaxArmor);
    }
}

auto DoRefit(MCMover& mover, float refitPoints, float& pointsUsed, int ammoOnly) -> int32_t
{
    float pointsLeft = refitPoints;
    int32_t finished = 0;
    std::optional<uint32_t> bettySample;

    if (refitPoints <= 0.0f)
    {
        if (mover.NetPlayerId != -1)
        {
            bettySample = 0x15;
        }

        finished = 1;
    }
    else
    {
        const float shareBase = static_cast<float>(RefitAmount * (1.0 / 3.0));
        int32_t locationsToFix = 0;
        int32_t ammoToFix = 0;

        if (ammoOnly == 0)
        {
            for (int32_t location = 0; location < mover.NumArmorLocations(); location++)
            {
                if (SkipLocation(mover, location))
                {
                    continue;
                }

                if (location < mover.NumBodyLocations() &&
                    mover.BodyAt(location).CurInternalStructure <
                        static_cast<float>(mover.BodyAt(location).MaxInternalStructure))
                {
                    locationsToFix++;
                }

                if (mover.Armor[location].CurArmor < static_cast<float>(mover.Armor[location].MaxArmor))
                {
                    locationsToFix++;
                }
            }
        }

        for (int32_t i = 0; i < mover.NumAmmoTypes(); i++)
        {
            if (mover.AmmoTypeTotal[i].CurAmount < mover.AmmoTypeTotal[i].StartAmount)
            {
                ammoToFix++;
            }
        }

        for (int32_t location = 0; location < mover.NumArmorLocations(); location++)
        {
            if (locationsToFix == 0 || pointsLeft <= 0.0f || SkipLocation(mover, location))
            {
                continue;
            }

            MCArmorLocation& armor = mover.Armor[location];
            const float maxArmor = static_cast<float>(armor.MaxArmor);

            if (armor.CurArmor < maxArmor)
            {
                double share =
                    std::min(static_cast<double>(shareBase) / locationsToFix, static_cast<double>(pointsLeft));
                const double added = static_cast<double>(RefitCostArray[0][0]) * share;
                float addedStored = static_cast<float>(added);

                if (maxArmor < added + armor.CurArmor)
                {
                    const double room = static_cast<double>(maxArmor) - armor.CurArmor;
                    addedStored = static_cast<float>(room);
                    share = room / RefitCostArray[0][0];
                }

                armor.CurArmor = static_cast<float>(static_cast<double>(addedStored) + armor.CurArmor);
                pointsLeft = static_cast<float>(pointsLeft - share);
            }

            if (location < mover.NumBodyLocations())
            {
                MCBodyLocation& body = mover.BodyAt(location);
                const float maxStructure = static_cast<float>(body.MaxInternalStructure);

                if (body.CurInternalStructure < maxStructure)
                {
                    const double shareRaw = static_cast<double>(shareBase) / locationsToFix;
                    float share = pointsLeft < shareRaw ? pointsLeft : static_cast<float>(shareRaw);
                    double added = static_cast<double>(RefitCostArray[1][0]) * share;

                    if (maxStructure < added + body.CurInternalStructure)
                    {
                        added = static_cast<double>(maxStructure) - body.CurInternalStructure;
                        share = static_cast<float>(added / RefitCostArray[1][0]);
                    }

                    const double newStructure = added + body.CurInternalStructure;
                    body.CurInternalStructure = static_cast<float>(newStructure);
                    uint8_t damageState;

                    if (newStructure == 0.0)
                    {
                        damageState = 2;
                    }
                    else
                    {
                        damageState = 0.5 < newStructure / maxStructure ? 0 : 1;
                    }

                    if (mover.ObjectClass == MCObjectClass::BattleMech && damageState != body.DamageState)
                    {
                        if (location == 6 || location == 7)
                        {
                            static_cast<MCBattleMech&>(mover).CalcLegStatus();
                        }

                        if (location == 1)
                        {
                            static_cast<MCBattleMech&>(mover).CalcTorsoStatus();
                        }
                    }

                    pointsLeft = static_cast<float>(static_cast<double>(pointsLeft) - share);
                    body.DamageState = damageState;
                }
            }
        }

        for (int32_t i = 0; i < mover.NumAmmoTypes(); i++)
        {
            if (ammoToFix <= 0 || pointsLeft <= 0.0f)
            {
                continue;
            }

            MCAmmoTally& ammo = mover.AmmoTypeTotal[i];
            const int32_t curAmount = ammo.CurAmount;
            const int32_t maxAmount = ammo.StartAmount;

            if (curAmount >= maxAmount)
            {
                continue;
            }

            const bool wasEmpty = curAmount == 0;
            double share = std::min(static_cast<double>(shareBase) / ammoToFix, static_cast<double>(pointsLeft));
            const double costPerPoint =
                static_cast<double>(MasterComponentList[ammo.MasterId].LongValue) * RefitCostArray[2][0];
            const float costPerPointStored = static_cast<float>(costPerPoint);
            float added = static_cast<float>(costPerPoint * share);
            const float current = static_cast<float>(curAmount);

            if (static_cast<double>(maxAmount) < static_cast<double>(current) + added)
            {
                added = static_cast<float>(maxAmount - curAmount);
                share = static_cast<double>(maxAmount - curAmount) / costPerPointStored;
            }

            ammo.CurAmount = static_cast<int32_t>(static_cast<double>(current) + added);
            pointsLeft = static_cast<float>(pointsLeft - share);

            if (wasEmpty)
            {
                mover.CalcLongestRangeWeapon();
                mover.CalcWeaponEffectiveness(0);
                mover.CalcOptimalRange(nullptr);
            }
        }

        // Finished once nothing is left to fix.
        bool needsMore = false;

        if (ammoOnly == 0)
        {
            for (int32_t location = 0; location < mover.NumArmorLocations() && !needsMore; location++)
            {
                needsMore = !SkipLocation(mover, location) && LocationNeedsRepair(mover, location);
            }
        }

        for (int32_t i = 0; i < mover.NumAmmoTypes() && !needsMore; i++)
        {
            needsMore = mover.AmmoTypeTotal[i].CurAmount < mover.AmmoTypeTotal[i].StartAmount;
        }

        if (!needsMore)
        {
            if (mover.NetPlayerId != -1)
            {
                mover.GetPilot()->RadioMessage(MCRadioMessageType::RefitDone, 1);
                bettySample = 0x14;
            }

            finished = 1;
        }
    }

    if (bettySample.has_value())
    {
        SoundSystem()->PlayBettySample(*bettySample);
    }

    // Points used, at least a quarter and rounded to quarters.
    const float used = refitPoints - pointsLeft;
    pointsUsed = 0.0f < used && used < 0.25f ? 0.25f : used;
    pointsUsed = static_cast<float>(static_cast<int32_t>((static_cast<double>(pointsUsed) + 0.125) * 4.0)) * 0.25f;
    return finished;
}
