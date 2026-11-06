#include "stdafx.h"
#include "main/MCLogistics.h"
#include "gui/MCGuiGlobals.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCUnitLimits.h"
#include "network/MCMultiPlayer.h"
#include "sound/MCSoundSystem.h"

namespace
{
    /// <summary>The scan code that ends a cheat code.</summary>
    constexpr int16_t CheatEnd = 0xff;

    /// <summary>
    /// The logistics cheat codes as DirectInput scan codes, each ended by 0xff (the name is the port's):
    /// MITCHLOVESYOU, HEREITCOMES, POUNDOFFLESH, KEEPTHEHAMMERDOWN, ROCKANDROLLPEOPLE, INFO, COCKADOODLEDOO.
    /// </summary>
    constexpr std::array<std::array<int16_t, 18>, 7> LogCheatCodes = {{
        {50, 23, 20, 46, 35, 38, 24, 47, 18, 31, 21, 24, 22, 255, 0, 0, 0, 0},
        {35, 18, 19, 18, 23, 20, 46, 24, 50, 18, 31, 255, 0, 0, 0, 0, 0, 0},
        {25, 24, 22, 49, 32, 24, 33, 33, 38, 18, 31, 35, 255, 0, 0, 0, 0, 0},
        {37, 18, 18, 25, 20, 35, 18, 35, 30, 50, 50, 18, 19, 32, 24, 17, 49, 255},
        {19, 24, 46, 37, 30, 49, 32, 19, 24, 38, 38, 25, 18, 24, 25, 38, 18, 255},
        {23, 49, 33, 24, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {46, 24, 46, 37, 30, 32, 24, 24, 32, 38, 18, 32, 24, 24, 255, 0, 0, 0},
    }};

    /// <summary>The cheat codes by position in <see cref="LogCheatCodes"/>.</summary>
    enum class MCLogCheat : int32_t
    {
        RepairForce = 0,
        MoreComponents = 1,
        MillionPoints = 2,
        HammerDown = 3,
        NoTonnageLimit = 4,
        Info = 5,
        HeapInTitle = 6,
    };

    /// <summary>Betty's "cheat on" line (a test may run without a sound system).</summary>
    void PlayCheatSound()
    {
        if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
        {
            sound->PlayBettySample(4);
        }
    }
}

auto MCLogistics::ProcessCheatCode(int16_t key) -> void
{
    int32_t position = LogCurCheatChar;

    if (InDemo != 0 || MultiPlayer() != nullptr || !CheatsOn)
    {
        return;
    }

    // Every code still matching so far stays active; a code whose last key this is fires.
    if (LogCurCheatChar == 0)
    {
        LogCheatActive.fill(true);
    }

    std::optional<MCLogCheat> matched;

    for (size_t code = 0; code < LogCheatCodes.size(); code++)
    {
        if (!LogCheatActive[code])
        {
            continue;
        }

        const std::array<int16_t, 18>& keys = LogCheatCodes[code];

        if (key != keys[static_cast<size_t>(position)])
        {
            LogCheatActive[code] = false;
            continue;
        }

        if (keys[static_cast<size_t>(position) + 1] == CheatEnd)
        {
            position = 0;
            LogCurCheatChar = 0;
            LogCheatActive[code] = false;
            matched = static_cast<MCLogCheat>(code);
            break;
        }
    }

    // Original behaviour: after a match the codes still active go on from their first key (position 0 + 1).
    LogCurCheatChar = std::ranges::contains(LogCheatActive, true) ? position + 1 : 0;

    if (!matched.has_value())
    {
        return;
    }

    switch (*matched)
    {
        case MCLogCheat::RepairForce:
        {
            // MITCHLOVESYOU: repairs the force completely.
            PlayCheatSound();

            for (const std::shared_ptr<MCLogMech>& mech : ForceMechList->Mechs)
            {
                MCMechRepairBlock::RepairArmor(*mech, -1);
                MCMechRepairBlock::RepairInternal(*mech, -1);

                for (const std::shared_ptr<MCLogInventoryItem>& item : mech->Inventory->Items)
                {
                    for (const std::shared_ptr<MCLogInventoryStat>& stat : item->Stats)
                    {
                        stat->Hits = 0;
                    }
                }

                // The three knobs at the mech's state (the block's Set*Slider(-1)), then the row works out what it
                // shows.
                MCMechRepairBlock::PlaceSliders(*mech);

                if (const std::shared_ptr<MCMechRepairBlock> row = RepairScreen->RowOf(*mech); row != nullptr)
                {
                    row->DrawBackground(mech->SlotIndex, std::nullopt);
                }
            }

            break;
        }
        case MCLogCheat::MoreComponents:
        {
            // HEREITCOMES: one more of every component.
            PlayCheatSound();

            for (const std::shared_ptr<MCLogInventoryItem>& item : ComponentInventory->Items)
            {
                item->Count++;
            }

            const std::shared_ptr<MCLogInvScreen> screen =
                CurrentScreen() == RepairScreen ? std::static_pointer_cast<MCLogInvScreen>(RepairScreen.Lock())
                                                : PurchaseScreen.Lock();
            MCLogInvScreen::CreateCompInvBlock();
            screen->SetUpCompInv(true, true);
            break;
        }
        case MCLogCheat::MillionPoints:
        {
            // POUNDOFFLESH: a million resource points.
            PlayCheatSound();
            ResourcePoints += 1000000;
            break;
        }
        case MCLogCheat::NoTonnageLimit:
        {
            // ROCKANDROLLPEOPLE: no drop tonnage limit.
            PlayCheatSound();
            HammerDown = true;
            BriefingScreen->CalcTonnages();
            break;
        }
        default:
        {
            // KEEPTHEHAMMERDOWN does nothing in logistics; INFO only reset the number of a mission warp nothing could
            // switch on; COCKADOODLEDOO put the logistics heap's free memory in the window title (the heap is gone).
            break;
        }
    }
}
