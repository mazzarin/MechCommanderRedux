#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

// Port-only (Redux HD): a copy of the game started from this program and watched until it quits, as MechCommander
// Online's launcher starts and watches its own (the MCO probe's matches, MCMcoProbe.h): the same program with the
// arguments given, its output to a log file of its own.

/// <summary>A copy of the game this program started.</summary>
class MCGameCopy
{
public:
    /// <summary>
    /// Starts the game with <paramref name="arguments"/> (after the program's name), its standard output and errors
    /// written to <paramref name="log"/>.
    /// </summary>
    static std::expected<std::unique_ptr<MCGameCopy>, std::string> Start(std::span<const std::string> arguments,
                                                                         const std::filesystem::path& log);

    ~MCGameCopy();
    MCGameCopy(const MCGameCopy&) = delete;
    MCGameCopy& operator=(const MCGameCopy&) = delete;

    /// <summary>Whether it's still running.</summary>
    bool Running();

    /// <summary>Waits until it has quit.</summary>
    void Wait();

private:
    MCGameCopy() = default;

    /// <summary>The process (its id on the Mac, its handle on Windows); 0 once it has quit and been waited for.</summary>
    intptr_t _Process = 0;
};
