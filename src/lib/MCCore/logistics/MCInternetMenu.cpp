#include "stdafx.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCGamesScreen.h"
#include "logistics/MCMainMenuScreen.h"
#include "network/MCInternetGame.h"

bool HasInternetGames()
{
    return MCInternetGame::Available();
}

void ReleaseInternetMenus()
{
    MCMainMenuScreens::Release();
    ReleaseGamesScreens();
}
