#pragma once

/// <summary>Audio files the tests decode.</summary>
namespace MCTestAudio
{
    /// <summary>libFLAC 1.4.3: 960 stereo frames at 48000 Hz, signed PCM16 (1000 left, -2000 right).</summary>
    inline std::vector<uint8_t> Flac()
    {
        const std::string hex =
            "664c614300000022100010000000100000100bb802f0000003c09bf6bd9cc5a10655125d927e2b6ff8f68400002820000000726566"
            "6572656e6365206c6962464c414320312e342e3320323032333036323300000000fff87a180003bfab01207d011f838b4a";
        std::vector<uint8_t> bytes;

        for (size_t i = 0; i < hex.size(); i += 2)
        {
            bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        }

        return bytes;
    }
}
