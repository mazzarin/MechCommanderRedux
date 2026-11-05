#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>

/// <summary>
/// Port-only: the first start of a copy an installer prepared (update/MCContentUpdater.h's AwaitsInstall). The game's
/// content is downloaded before anything opens, so its art and fonts may not be there yet: a small window of its own,
/// drawn with SDL's 2D renderer and its built-in text, shows the progress. Closing it stops the download, and the next
/// start carries on where it stopped.
/// </summary>
namespace MCFirstStart
{
    /// <summary>The work's progress, as the window shows it: bytes in, and of the whole (0: not known yet).</summary>
    class MCProgress
    {
    public:
        void Set(uint64_t received, uint64_t expected);
        std::pair<uint64_t, uint64_t> Get() const;

    private:
        mutable std::mutex _Mutex;
        uint64_t _Received = 0;
        uint64_t _Expected = 0;
    };

    /// <summary>
    /// The work shown: it reports to the progress and stops when asked. It owns everything it uses: a window closed
    /// while it won't stop leaves it to end with the process.
    /// </summary>
    using MCWork = std::function<std::expected<void, std::string>(MCProgress& progress, std::stop_token stop)>;

    /// <summary>How the window is made (the tests': hidden, its last frame saved).</summary>
    struct MCWindowOptions
    {
        bool Hidden = false;
        /// <summary>Where the window's last frame is saved, as a BMP (empty: nowhere).</summary>
        std::filesystem::path Shot;
    };

    /// <summary>
    /// Runs <paramref name="work"/> on a thread of its own while a window shows <paramref name="heading"/> and its
    /// progress, until it ends. Closing the window (or the system asking the game to quit) asks the work to stop.
    /// </summary>
    /// <returns>True when the work was done, false when the player closed the window, or what failed.</returns>
    std::expected<bool, std::string> Run(std::string_view heading, MCWork work, const MCWindowOptions& options = {});
}
