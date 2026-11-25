#include "stdafx.h"
#include "logistics/MCIdleCheck.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCMenuScreens.h"
#include "main/MCLogistics.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCRoomEnd.h"
#include "network/MCMultiPlayer.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr std::string_view StillThereText = "ARE YOU STILL THERE?";
    constexpr std::string_view LeftText = "YOU WERE AWAY TOO LONG AND WERE TAKEN OUT OF THE GAME.";

    /// <summary>The tests' clock, when one is set (MCIdleCheck::SetClock).</summary>
    MCIdleCheck::MCClock TestClock;
    /// <summary>The player's last input: none yet, and the first look counts from then.</summary>
    std::optional<Clock::time_point> LastInput;
    /// <summary>When the room asked "STILL THERE?", while it waits for the answer.</summary>
    std::optional<Clock::time_point> AskedAt;
    /// <summary>The player gave input since the room asked.</summary>
    bool Answered = false;

    Clock::time_point Now()
    {
        return TestClock ? TestClock() : Clock::now();
    }

    Clock::time_point LastInputAt()
    {
        if (!LastInput.has_value())
        {
            LastInput = Now();
        }

        return *LastInput;
    }

    /// <summary>Whether this game shows an internet game's room, before its match.</summary>
    bool InInternetRoom()
    {
        const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        return game != nullptr && game->SessionManager != nullptr && game->SessionManager->Internet != nullptr &&
               game->SessionManager->CurrentSession() != nullptr && logistics != nullptr &&
               MCMenuScreens::Top() == MCMenuScreens::Room() && !MCLobbyUpdate::MatchUnderWay();
    }

    /// <summary>The question is over: its message closes, if it still shows.</summary>
    void EndQuestion()
    {
        AskedAt.reset();
        Answered = false;

        if (MCMenuDialog& dialog = *GlobalLog()->Message; dialog.IsShowing() && dialog.Text == StillThereText)
        {
            dialog.Deactivate(0);
        }
    }

    /// <summary>No answer: the player leaves the room as its EXIT does, and is told why back at the games.</summary>
    void LeaveAway()
    {
        SDL_Log("Idle: no answer to STILL THERE?: leaving the room");
        EndQuestion();

        // A lobby's game goes back to its lobby, as its room's EXIT does.
        if (LaunchedFromLobby)
        {
            MCRoomEnd::LeaveForLauncher();
            return;
        }

        MCRoomEnd::Leave(std::string(LeftText));
    }
}

void MCIdleCheck::Input()
{
    LastInput = Now();
    Answered = AskedAt.has_value();
}

bool MCIdleCheck::Away()
{
    return Now() - LastInputAt() >= AwayTime;
}

bool MCIdleCheck::Asking()
{
    return AskedAt.has_value();
}

void MCIdleCheck::Frame()
{
    if (GlobalLog() == nullptr || GlobalLog()->Message == nullptr)
    {
        return;
    }

    // Out of the room (left, or its match begun) there's nothing to answer.
    if (!InInternetRoom())
    {
        if (AskedAt.has_value())
        {
            EndQuestion();
        }

        return;
    }

    if (AskedAt.has_value())
    {
        if (Answered)
        {
            SDL_Log("Idle: STILL THERE? answered");
            EndQuestion();
        }
        else if (Now() - *AskedAt >= AnswerTime)
        {
            LeaveAway();
        }

        return;
    }

    // Asked when nothing else is being said.
    if (Away() && !GlobalLog()->Message->IsShowing())
    {
        SDL_Log("Idle: no input for %lld minutes: STILL THERE?", static_cast<long long>(AwayTime.count()));
        AskedAt = Now();
        Answered = false;
        ShowMenuMessage(StillThereText);
    }
}

void MCIdleCheck::SetClock(MCClock clock)
{
    TestClock = std::move(clock);
    LastInput = Now();
    AskedAt.reset();
    Answered = false;
}
