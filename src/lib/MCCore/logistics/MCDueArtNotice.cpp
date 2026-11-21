#include "stdafx.h"
#include "logistics/MCDueArtNotice.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCUpdatePage.h"
#include "main/MCBenchmark.h"
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCMultiPlayer.h"

namespace
{
    using MCUpdater::MCStage;

    /// <summary>What the notice says while the new HD art downloads, then once it's in, waiting for a restart.</summary>
    constexpr std::string_view DownloadingText =
        "The new HD art is still downloading. Until you restart with it, the game shows the old art.";
    constexpr std::string_view ReadyText =
        "The new HD art is ready. Until you restart with it, the game shows the old art.";

    /// <summary>The briefing's logistics state, and the session screen's.</summary>
    constexpr int32_t BriefingState = 3;
    constexpr int32_t SessionState = 8;

    /// <summary>The player has seen it this run: it closed (OK, Enter, Escape, or its screen left).</summary>
    bool Seen = false;

    /// <summary>What the notice says now: nothing unless the new HD art downloads or waits for its restart.</summary>
    std::string_view NoticeText()
    {
        if (!MCUpdater::DueArt())
        {
            return {};
        }

        const MCStage stage = MCUpdater::Status().Stage;

        if (stage == MCStage::Downloading)
        {
            return DownloadingText;
        }

        return stage == MCStage::Ready ? ReadyText : std::string_view();
    }

    /// <summary>
    /// Whether <paramref name="logistics"/> shows where the notice goes: a mission's briefing in single player, or a
    /// match's room (the session screen) before the match.
    /// </summary>
    bool OnNoticeScreen(MCLogistics& logistics)
    {
        if (MultiPlayer() == nullptr)
        {
            return logistics.LogisticsState == BriefingState && logistics.CurrentScreen() == logistics.BriefingScreen;
        }

        return logistics.LogisticsState == SessionState && logistics.CurrentScreen() == logistics.SessionScreen &&
               !MCLobbyUpdate::MatchUnderWay();
    }

    /// <summary>Whether <paramref name="dialog"/> shows the notice.</summary>
    bool ShowsNotice(MCReusableDialog& dialog)
    {
        return dialog.IsShowing() && (dialog.Text == DownloadingText || dialog.Text == ReadyText);
    }

    /// <summary>However it closed, the player has seen it.</summary>
    void Closed(int32_t /*result*/)
    {
        Seen = true;
    }
}

void ShowDueArtNotice()
{
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (logistics == nullptr || logistics->MessageDialog == nullptr || logistics->QuestionDialog == nullptr)
    {
        return;
    }

    MCReusableDialog& dialog = *logistics->MessageDialog;

    // Its screen left (the room on to its match, say), it closes: it holds nothing up.
    if (ShowsNotice(dialog))
    {
        if (!OnNoticeScreen(*logistics))
        {
            SDL_Log("Updater: the old HD art's notice closes: its screen was left");
            dialog.Deactivate(0);
        }

        return;
    }

    const std::string_view text = NoticeText();

    // A benchmark's run (main/MCBenchmark.h) goes through the briefing by itself: nothing stops it.
    if (Seen || text.empty() || MCBenchmark::Running() || !OnNoticeScreen(*logistics) || dialog.IsShowing() ||
        logistics->QuestionDialog->IsShowing() || UpdatePageShown())
    {
        return;
    }

    SDL_Log("Updater: the notice says the game shows the old HD art until the new art is in (%s)",
            MCUpdater::Status().Stage == MCStage::Ready ? "ready" : "downloading");
    ShowMenuMessage(text, Closed);
}
