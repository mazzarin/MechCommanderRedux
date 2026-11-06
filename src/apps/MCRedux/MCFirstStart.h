#pragma once

#include "update/MCUpdateWork.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// Port-only: the first start of a copy an installer prepared (update/MCContentUpdater.h's AwaitsInstall). The game's
/// content is downloaded before anything opens, so its art and fonts may not be there yet: a small window of its own,
/// drawn with SDL's 2D renderer and its built-in text, shows the progress: how much is in, the speed and the time left.
/// Closing it stops the download, and the next start carries on where it stopped. A download that fails says in the
/// same window what's wrong, in plain words, over TRY AGAIN and QUIT (<see cref="RunAsking"/>). The recovery screen,
/// for an update that stopped partway, is such a window too (<see cref="Ask"/>).
/// </summary>
namespace MCFirstStart
{
    /// <summary>The work's progress, as the window shows it (update/MCUpdateWork.h).</summary>
    using MCProgress = MCUpdateWork::MCProgress;
    using MCFailure = MCUpdateWork::MCFailure;

    /// <summary>
    /// The work shown: it reports to the progress and stops when asked (its transfer cancelled: the window waits for
    /// it to end).
    /// </summary>
    using MCWork = std::function<std::expected<void, MCFailure>(MCProgress& progress, std::stop_token stop)>;

    /// <summary>The window's title bar: the game's name.</summary>
    constexpr std::string_view WindowTitle = "MechCommander Redux HD";

    /// <summary>The heading of a download that failed (<see cref="RunAsking"/>), and its buttons.</summary>
    constexpr std::string_view ProblemHeading = "THE DOWNLOAD STOPPED.";
    constexpr std::string_view TryAgainButton = "TRY AGAIN";
    constexpr std::string_view QuitButton = "QUIT";

    /// <summary>How the window is made (the tests': hidden, its last frame saved).</summary>
    struct MCWindowOptions
    {
        bool Hidden = false;
        /// <summary>Where the window's last frame is saved, as a BMP (empty: nowhere).</summary>
        std::filesystem::path Shot;
        /// <summary>The button <see cref="Ask"/> takes as pressed once its window is drawn (the tests').</summary>
        std::optional<size_t> Answer;
    };

    /// <summary>
    /// The line under the heading: what's going on before the bytes are known ("LOOKING AT WHAT'S NEEDED."), while
    /// what an earlier start left is checked ("CHECKING WHAT'S ALREADY DOWNLOADED."), or how much is in ("1.2 GB OF
    /// 6.5 GB"); "STOPPING." once the window is closed.
    /// </summary>
    std::string AmountLine(const MCUpdateWork::MCProgressReport& report, bool stopping);

    /// <summary>
    /// What the window says when the work failed (<see cref="RunAsking"/>): what's wrong, in plain words, by its kind
    /// (the detail goes to the log), and the step to take.
    /// </summary>
    std::string ProblemText(const MCFailure& failure);

    /// <summary>
    /// Runs <paramref name="work"/> on a thread of its own while a window shows <paramref name="heading"/> and its
    /// progress, until it ends. Closing the window (or the system asking the game to quit) asks the work to stop, and
    /// waits for it.
    /// </summary>
    /// <returns>True when the work was done, false when the player closed the window, or what failed.</returns>
    std::expected<bool, MCFailure> Run(std::string_view heading, MCWork work, const MCWindowOptions& options = {});

    /// <summary>
    /// Runs <paramref name="work"/> as <see cref="Run"/> does; when it fails, the same window says what's wrong
    /// (<see cref="ProblemText"/>, the failure logged) over TRY AGAIN and QUIT, and TRY AGAIN runs the work again in it,
    /// carrying on from what the work left.
    /// </summary>
    /// <returns>
    /// Whether the work was done: false when the player closed the window or chose QUIT; or why the window can't open.
    /// </returns>
    std::expected<bool, std::string> RunAsking(std::string_view heading, const MCWork& work,
                                               const MCWindowOptions& options = {});

    /// <summary>
    /// <paramref name="text"/> as <see cref="Ask"/> shows it: lines of at most <paramref name="width"/> letters, broken
    /// between words (a word too long for a line, within it), each '\n' starting a line. Past 12 lines the last
    /// paragraph (the step to take) still shows whole and the text before it is cut short, ending "...".
    /// </summary>
    std::vector<std::string> Wrapped(std::string_view text, size_t width);

    /// <summary>
    /// Shows <paramref name="heading"/> and <paramref name="text"/>, wrapped, over a row of <paramref name="buttons"/>,
    /// and waits for the player to press one: by the mouse, or the arrows and Enter (Escape presses the last).
    /// </summary>
    /// <returns>The button pressed; none when the window is closed, or can't open (logged).</returns>
    std::optional<size_t> Ask(std::string_view heading, std::string_view text,
                              std::span<const std::string_view> buttons, const MCWindowOptions& options = {});
}
