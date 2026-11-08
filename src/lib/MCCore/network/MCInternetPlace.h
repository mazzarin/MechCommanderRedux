#pragma once

#include <string>

/// <summary>
/// Port (Redux HD): a hosted internet game's place on the service: its code, and the token its host comes back to it
/// with (network/MCInternetGame.h's HostAgain, after a restart into an update).
/// </summary>
struct MCInternetPlace
{
    std::string Code;
    std::string Token;
};
