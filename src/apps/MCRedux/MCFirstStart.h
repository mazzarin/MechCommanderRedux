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
/// drawn with SDL's 2D renderer and its built-in text, shows the progress. Closing it stops the download, and the next
/// start carries on where it stopped. The recovery screen, for an update that stopped partway, is such a window too
/// (<see cref="Ask"/>).
/// </summary>
namespace MCFirstStart
{
    /// <summary>The work's progress, as the window shows it: bytes in, and of the whole (0: not known yet).</summary>
    using MCProgress = MCUpdateWork::MCProgress;

    /// <summary>
    /// The work shown: it reports to the progress and stops when asked (its transfer cancelled: the window waits for
    /// it to end).
    /// </summary>
    using MCWork = std::function<std::expected<void, std::string>(MCProgress& progress, std::stop_token stop)>;

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
    /// Runs <paramref name="work"/> on a thread of its own while a window shows <paramref name="heading"/> and its
    /// progress, until it ends. Closing the window (or the system asking the game to quit) asks the work to stop, and
    /// waits for it.
    /// </summary>
    /// <returns>True when the work was done, false when the player closed the window, or what failed.</returns>
    std::expected<bool, std::string> Run(std::string_view heading, MCWork work, const MCWindowOptions& options = {});

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
