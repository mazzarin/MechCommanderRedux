#pragma once

#include "platform/MCMemoryRoom.h"

#include <functional>

/// <summary>
/// A stand-in for what the platform says the HD art frames may take (platform/MCMemoryRoom.h): whether memory is short
/// and the card's budget, as the test sets them. The game's use of the card is <see cref="Rest"/> plus what
/// <see cref="Held"/> says the HD pictures hold when asked (the renderer's, in a game), as a real card counts them.
/// </summary>
class MCFakeMemorySystem final : public MCMemoryRoom::MCSystem
{
public:
    bool IsShort = false;
    /// <summary>Whether the system can say anything of the card; its budget, and the game's use there besides HD.</summary>
    bool Reports = true;
    uint64_t Budget = 0;
    uint64_t Rest = 0;
    bool SharesMemory = false;
    std::function<uint64_t()> Held = [] { return uint64_t{0}; };
    /// <summary>How many times the card was asked, and the name it was asked by last.</summary>
    int CardReads = 0;
    std::string AskedName;

    bool Short() override { return IsShort; }

    std::optional<MCMemoryRoom::MCCard> Card(std::string_view deviceName) override
    {
        ++CardReads;
        AskedName = std::string(deviceName);

        if (!Reports)
        {
            return std::nullopt;
        }

        return MCMemoryRoom::MCCard{.Budget = Budget, .Used = Rest + Held(), .SharesMemory = SharesMemory};
    }
};

/// <summary>Sets <paramref name="system"/> as the platform's while it lives, and none again however the test ends.</summary>
class MCFakeMemoryScope
{
public:
    explicit MCFakeMemoryScope(std::shared_ptr<MCFakeMemorySystem> system)
    {
        MCMemoryRoom::SetSystem(std::move(system));
    }

    ~MCFakeMemoryScope() { MCMemoryRoom::SetSystem(nullptr); }
    MCFakeMemoryScope(const MCFakeMemoryScope&) = delete;
    MCFakeMemoryScope& operator=(const MCFakeMemoryScope&) = delete;
};
