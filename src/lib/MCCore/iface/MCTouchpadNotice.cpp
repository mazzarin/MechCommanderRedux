#include "stdafx.h"
#include "iface/MCTouchpadNotice.h"

#include "gui/MCGuiGlobals.h"
#include "logistics/MCPauseScreen.h"
#include "main/MCMissionGlobals.h"
#include "main/MCPrefs.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCInput.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"

#include <utility>

namespace
{
    /// <summary>Single player paused under the notice, to go on at its OK.</summary>
    bool PausedUnder = false;
    /// <summary>The notice shows (said, its OK not yet pressed).</summary>
    bool Showing = false;

    int64_t Ticks(MCTouchpadNotice::Clock::time_point at)
    {
        return at.time_since_epoch().count();
    }

    /// <summary>Whether the battle has started and nothing over it would take the notice's place.</summary>
    bool BattleCanSay()
    {
        const std::shared_ptr<MCScenario> scenario = Scenario();
        return scenario != nullptr && !scenario->StartingUp && scenario->StartUpTurns < Turn &&
               EventsToMissionResultsScreen == 0 && !GamePaused && !MCPauseScreen::Shows() && !MCRmlDialogs::Shown();
    }

    /// <summary>Its OK, Enter or Escape: single player goes on.</summary>
    void Answered()
    {
        Showing = false;

        if (std::exchange(PausedUnder, false))
        {
            GamePaused = false;
        }
    }
}

void MCTouchpadNotice::MCSwallowedClicks::EmptyPacket(Clock::time_point at)
{
    const int64_t now = Ticks(at);
    const auto pairWithin = std::chrono::duration_cast<Clock::duration>(PairWithin).count();

    if (_UnpairedAt != None && now - _UnpairedAt <= pairWithin)
    {
        _SwallowedAt.store(now);
        _UnpairedAt = None;
        return;
    }

    _UnpairedAt = now;
}

std::optional<MCTouchpadNotice::Clock::time_point> MCTouchpadNotice::MCSwallowedClicks::Take()
{
    const int64_t at = _SwallowedAt.exchange(None);
    return at == None ? std::nullopt : std::optional(Clock::time_point(Clock::duration(at)));
}

MCTouchpadNotice::MCSwallowedClicks& MCTouchpadNotice::Clicks()
{
    static MCSwallowedClicks clicks;
    return clicks;
}

void MCTouchpadNotice::EmptyPacket(Clock::time_point at)
{
    Clicks().EmptyPacket(at);
}

void MCTouchpadNotice::Tend()
{
    // Seen once: what the watcher finds from then is let go.
    if (TouchpadNoticeSeen)
    {
        Clicks().Take();
        return;
    }

    // Waiting for the battle to start, or for the pause or a dialog to close.
    if (!Clicks().Waiting() || !BattleCanSay())
    {
        return;
    }

    const std::optional<Clock::time_point> swallowed = Clicks().Take();

    if (!swallowed || Clock::now() - *swallowed > SayWithin)
    {
        return;
    }

    SDL_Log("Touchpad: a click was dropped with a key held; the notice shows (once)");
    TouchpadNoticeSeen = true;
    SavePrefs();
    Showing = true;

    // The key still held (Space) lets go under the notice, as under the pause: its release would never reach the battle.
    MCInput::LetGoOfKeys();

    // Single player: the mission waits under it, as under the pause; a match plays on.
    if (MultiPlayer() == nullptr)
    {
        GamePaused = true;
        PausedUnder = true;
    }

    MCRmlDialogs::Show({.Eyebrow = std::string(Eyebrow), .Text = std::string(Text), .OnMain = Answered},
                       MCRmlFlow::MCOn::Mission);
}

bool MCTouchpadNotice::Shows()
{
    return Showing;
}
