#include "stdafx.h"
#include "logistics/MCUpdateModel.h"
#include "main/MCUpdater.h"
#include "network/MCIntroNote.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCRejoinNote.h"
#include "platform/MCFileSystem.h"

namespace
{
    using MCUpdater::MCAnswer;
    using MCUpdater::MCStage;
    using MCUpdater::MCStatus;

    /// <summary>What the version line writes: Redux HD and the release running (<c>{}</c>).</summary>
    constexpr std::string_view VersionLineFormat = "Redux HD {}";

    /// <summary>Where the running version as shown ("0.6.3 (899)") gives its build: the release is what comes before.</summary>
    constexpr std::string_view BuildOpening = " (";

    /// <summary>The remade main menu's UPDATE: the release it brings (<c>{}</c>), or none when that's the one running.</summary>
    constexpr std::string_view UpdateToFormat = "Update to {}";
    constexpr std::string_view BareUpdate = "Update";

    /// <summary>
    /// Whether RESTART is offered at the update's <paramref name="stage"/> (<see cref="UpdateRestartOffered"/>; the
    /// line under QUIT asks it too).
    /// </summary>
    bool RestartOffered(MCStage stage)
    {
        // Only where what the game starts outlives it, and not for a setup waiting for the player's own start; in a
        // lobby's room, only when every game there can rejoin, before the match (network/MCLobbyUpdate.h).
        return stage == MCStage::Ready && MCUpdater::CanStartAgain() &&
               MCUpdater::Status().GoesIn != MCUpdater::MCGoesIn::PlayersStart &&
               (!MCLobbyUpdate::InRoom() || MCLobbyUpdate::CanRestart());
    }

    /// <summary>What the status line says of an update ready, by how it goes in (main/MCUpdater.h's MCGoesIn).</summary>
    std::string ReadyText(const MCStatus& status)
    {
        switch (status.GoesIn)
        {
            case MCUpdater::MCGoesIn::NextStart:
                return MCUpdater::CanStartAgain()
                           ? std::format("VERSION {} IS IN. RESTART TO PLAY IT.", status.Version)
                           : std::format("VERSION {} IS IN. IT PLAYS WHEN THE GAME STARTS AGAIN.", status.Version);
            case MCUpdater::MCGoesIn::PlayersStart:
                return std::format("VERSION {} INSTALLS WHEN YOU START REDUX HD YOURSELF.", status.Version);
            case MCUpdater::MCGoesIn::AsItQuits:
                break;
        }

        return std::format("VERSION {} INSTALLS AS YOU QUIT.", status.Version);
    }

    /// <summary>What's in of a download: of the whole once that's known ("2.1 OF 7.4 GB"), else what's in.</summary>
    std::string InHandText(const MCStatus& status)
    {
        return status.Expected == 0 ? MCUpdateWork::SizeText(status.Received)
                                    : MCUpdateWork::ProgressText(status.Received, status.Expected);
    }

    /// <summary>What the status line says the update is doing.</summary>
    std::string StatusText(const MCStatus& status)
    {
        switch (status.Stage)
        {
            case MCStage::None:
                return {};
            case MCStage::Found:
            {
                if (status.Expected == 0)
                {
                    return "A NEWER VERSION IS OUT.";
                }

                return std::format("A NEWER VERSION IS OUT: {} TO DOWNLOAD.", MCUpdateWork::SizeText(status.Expected));
            }
            case MCStage::Downloading:
                return std::format("DOWNLOADING: {}.", InHandText(status));
            case MCStage::Ready:
            {
                // In a lobby's room, what RESTART does there (network/MCLobbyUpdate.h).
                if (std::string room = MCLobbyUpdate::ReadyText(); !room.empty())
                {
                    return room;
                }

                return ReadyText(status);
            }
            case MCStage::Installing:
                return "INSTALLING. THE GAME WILL RESTART BY ITSELF.";
            case MCStage::Failed:
            {
                std::string text = std::format("THE UPDATE FAILED: {}", status.Error);
                std::ranges::transform(text, text.begin(), [](char c)
                                       { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
                return text;
            }
        }

        return {};
    }

    /// <summary>
    /// Port-only (Redux HD): what the line says of the new HD art a program installed alone came for
    /// (MCUpdater::DueArt), the game showing the old art until it's in: while it downloads (what's in of the whole), then
    /// once it's in, asking for a restart when the page offers RESTART.
    /// </summary>
    constexpr std::string_view DueArtDownloadingLine = "NEW HD ART DOWNLOADING: {}";
    constexpr std::string_view DueArtRestartLine = "NEW HD ART READY: PLEASE RESTART";
    constexpr std::string_view DueArtReadyLine = "NEW HD ART READY";

    /// <summary>The remade menus' words for that art (DueArtLineText): what's in of the whole (<c>{}</c>), then that it's in.</summary>
    constexpr std::string_view DueArtCountLine = "HD art: {}";
    constexpr std::string_view DueArtInLine = "New HD art is in";

    /// <summary>What the line under QUIT says while an update waits.</summary>
    std::string NoticeText(const MCStatus& status)
    {
        switch (status.Stage)
        {
            case MCStage::None:
                return {};
            case MCStage::Found:
                return std::format("VERSION {} IS OUT", status.Version);
            case MCStage::Downloading:
                return MCUpdater::DueArt() ? std::format(DueArtDownloadingLine, InHandText(status))
                                           : std::format("DOWNLOADING VERSION {}", status.Version);
            case MCStage::Ready:
            {
                if (MCUpdater::DueArt())
                {
                    return std::string(RestartOffered(status.Stage) ? DueArtRestartLine : DueArtReadyLine);
                }

                return status.GoesIn == MCUpdater::MCGoesIn::NextStart
                           ? std::format("VERSION {} IS IN", status.Version)
                           : std::format("VERSION {} IS READY TO INSTALL", status.Version);
            }
            case MCStage::Installing:
                return std::format("INSTALLING VERSION {}", status.Version);
            case MCStage::Failed:
                return "THE UPDATE FAILED";
        }

        return {};
    }
}

std::string VersionLineText(std::string_view running)
{
    const std::string_view release = ReleaseOf(running);
    return release.empty() ? std::string() : std::format(VersionLineFormat, release);
}

std::string_view ReleaseOf(std::string_view shown)
{
    return shown.substr(0, shown.find(BuildOpening));
}

std::string UpdateButtonText(std::string_view running, std::string_view update)
{
    const std::string_view release = ReleaseOf(update);
    return release == ReleaseOf(running) ? std::string(BareUpdate) : std::format(UpdateToFormat, release);
}

bool UpdateRestartOffered()
{
    return RestartOffered(MCUpdater::Status().Stage);
}

void RestartForUpdate()
{
    if (!MCLobbyUpdate::Restart())
    {
        // Port-only (SPEC-menus.md's "The intro movies"): the game starts itself again, so its next start skips the
        // intro (network/MCIntroNote.h); a lobby's room's restart rejoins the room, with no intro.
        if (const auto written = MCIntroNote::Write(MCFileSystem::UserRoot(), MCRejoinNote::Now()); !written)
        {
            SDL_Log("UPDATE: RESTART's intro note: %s", written.error().c_str());
        }

        MCUpdater::Answer(MCAnswer::Install);
    }
}

std::string UpdatePageStatus()
{
    return StatusText(MCUpdater::Status());
}

std::string UpdateLineText()
{
    return NoticeText(MCUpdater::Status());
}

std::string DueArtLineText()
{
    if (!MCUpdater::DueArt())
    {
        return {};
    }

    const MCStatus& status = MCUpdater::Status();

    switch (status.Stage)
    {
        case MCStage::Downloading:
            return std::format(DueArtCountLine, MCUpdateWork::CountText(status.Received, status.Expected));
        case MCStage::Ready:
        case MCStage::Installing:
            return std::string(DueArtInLine);
        case MCStage::None:
        case MCStage::Found:
        case MCStage::Failed:
            break;
    }

    return {};
}
