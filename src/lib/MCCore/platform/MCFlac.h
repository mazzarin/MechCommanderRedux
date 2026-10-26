#pragma once

#include "platform/MCAudio.h"

/// <summary>A bounded, seekable FLAC reader. dr_flac is the only compressed-audio decoder.</summary>
class MCFlac
{
public:
    static std::unique_ptr<MCFlac> Open(const std::filesystem::path& path);
    ~MCFlac();
    MCFlac(const MCFlac&) = delete;
    MCFlac& operator=(const MCFlac&) = delete;

    const MCSoundFormat& Format() const { return _Format; }
    uint64_t Frames() const { return _Frames; }
    /// <summary>Reads whole PCM frames, as signed 16-bit little-endian bytes. Short at EOF or corruption.</summary>
    size_t Read(std::span<uint8_t> bytes);
    bool Seek(uint64_t frame);
    /// <summary>Checks every decoded frame, without keeping the whole recording in memory.</summary>
    bool Validate();

private:
    MCFlac();
    struct Impl;
    std::unique_ptr<Impl> _Impl;
    MCSoundFormat _Format;
    uint64_t _Frames = 0;
};
