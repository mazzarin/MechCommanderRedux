#pragma once

/// <summary>
/// Port-only: a log of slow frames, for finding hitches in play (<c>-framelog &lt;file&gt;</c>). Each frame's time is
/// split into named parts (<see cref="Scope"/>) and carries notes of what happened in it (<see cref="Note"/>); a frame
/// much slower than the frames around it is written out with them. Costs a flag test per part while off.
/// </summary>
/// <remarks>
/// Threads: <see cref="Open"/> and <see cref="NextFrame"/> on the game's thread; parts and notes on any (the render
/// thread's parts are named <c>render.*</c>), counted in the frame running as they end.
/// </remarks>
namespace MCFrameLog
{
    /// <summary>Starts logging into <paramref name="path"/> (replaced).</summary>
    bool Open(const std::filesystem::path& path);

    /// <summary>Whether a log is open.</summary>
    bool Enabled();

    /// <summary>Ends the frame running since the last call (written if slow) and starts the next.</summary>
    void NextFrame();

    /// <summary>Adds <paramref name="text"/> to this frame's notes.</summary>
    void Note(std::string text);

    /// <summary>Adds the time from its construction to its end to the frame's part <c>Name</c>.</summary>
    class Scope
    {
    public:
        /// <summary>Starts timing part <paramref name="name"/> (copied only while a log is open).</summary>
        explicit Scope(std::string_view name);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        /// <summary>The part's name (empty while no log is open).</summary>
        std::string _Name;
        uint64_t _Start;
    };
}
