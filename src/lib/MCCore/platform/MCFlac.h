#pragma once

#include "platform/MCAudio.h"

/// <summary>A bounded, seekable FLAC reader. dr_flac is the only compressed-audio decoder.</summary>
class MCFlac
{
public:
    static std::unique_ptr<MCFlac> Open(const std::filesystem::path& path);
    /// <summary>
    /// A reader of the FLAC file <paramref name="bytes"/> already read into memory (kept while it reads), so a sound
    /// played again and again is read from disk once: no file is opened.
    /// </summary>
    static std::unique_ptr<MCFlac> Open(std::shared_ptr<const std::vector<uint8_t>> bytes);
    /// <summary>The whole file at <paramref name="path"/>, for <see cref="Open"/> from memory; null when it can't be read.
    /// </summary>
    static std::shared_ptr<const std::vector<uint8_t>> ReadFile(const std::filesystem::path& path);
    ~MCFlac();
    MCFlac(const MCFlac&) = delete;
    MCFlac& operator=(const MCFlac&) = delete;

    const MCSoundFormat& Format() const { return _Format; }
    uint64_t Frames() const { return _Frames; }
    /// <summary>Reads whole PCM frames, as signed 16-bit little-endian bytes. Short at EOF or corruption.</summary>
    size_t Read(std::span<uint8_t> bytes);
    bool Seek(uint64_t frame);
    /// <summary>Checks every decoded frame, without keeping the whole recording in memory (the pack check's).</summary>
    bool Validate();
    /// <summary>
    /// Whether the last frame decodes, then back to the start: a cheap check that the file isn't cut short, for the
    /// game to make before it streams one (decoding a whole track would hold the frame up).
    /// </summary>
    bool CheckEnd();

private:
    MCFlac();
    /// <summary>Whether the decoder just opened (none: it didn't open) is one this code plays; takes its format.</summary>
    bool Opened();
    struct Impl;
    std::unique_ptr<Impl> _Impl;
    MCSoundFormat _Format;
    uint64_t _Frames = 0;
};
