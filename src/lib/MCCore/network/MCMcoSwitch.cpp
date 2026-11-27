#include "stdafx.h"
#include "network/MCMcoSwitch.h"

#include "lib/MCFitIniFile.h"
#include "network/MCMcoSeats.h"
#include "platform/MCFileSystem.h"

namespace
{
    /// <summary>The rooms part as last heard: a FIT file in the user folder.</summary>
    constexpr std::string_view SwitchFile = "mco.cfg";
    constexpr std::string_view SwitchBlock = "Mco";
    constexpr std::string_view RoomsEntry = "Rooms";
    /// <summary>What a game that has never heard the switch takes it as.</summary>
    constexpr bool RoomsNeverHeard = true;

    /// <summary>The rooms part, once read from the user folder (or heard).</summary>
    std::optional<bool> Rooms;

    bool ReadRooms()
    {
        MCFitIniFile file;

        if (file.Open(MCFileSystem::SettingsToRead(SwitchFile)) != 0 || file.SeekBlock(SwitchBlock) != 0)
        {
            return RoomsNeverHeard;
        }

        return file.Read<bool>(RoomsEntry).value_or(RoomsNeverHeard);
    }

    void KeepRooms(bool rooms)
    {
        MCFitIniFile file;

        if (const int32_t result = file.Create(MCFileSystem::SettingsToWrite(SwitchFile)); result != 0)
        {
            SDL_Log("MCO switch: cannot create %s (%#x); MCO's rooms %s this run only", SwitchFile.data(),
                    static_cast<uint32_t>(result), rooms ? "show" : "stay hidden");
            return;
        }

        file.WriteBlock(SwitchBlock);
        file.WriteIdBoolean(RoomsEntry, rooms);
    }
}

void MCMcoSwitch::Heard(const MCMcoSwitchSaid& said)
{
    if (said.Hosts != MCMcoSeats::HostsOn())
    {
        SDL_Log("MCO switch: INTERNET's games %s on MechCommander Online too",
                said.Hosts ? "are hosted" : "aren't hosted");
        MCMcoSeats::SetHostsOn(said.Hosts);
    }

    if (said.Rooms != RoomsOn())
    {
        SDL_Log("MCO switch: MechCommander Online's rooms %s in INTERNET", said.Rooms ? "show" : "are hidden");
        Rooms = said.Rooms;
        KeepRooms(said.Rooms);
    }
}

bool MCMcoSwitch::RoomsOn()
{
    if (!Rooms.has_value())
    {
        Rooms = ReadRooms();
    }

    return *Rooms;
}

void MCMcoSwitch::ReadAgainForTests()
{
    Rooms.reset();
}
