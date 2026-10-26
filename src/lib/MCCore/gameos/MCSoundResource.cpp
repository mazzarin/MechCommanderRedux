#include "stdafx.h"
#include "gameos/MCSoundResource.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFlac.h"
#include "sound/MCHdAudio.h"

namespace
{
    /// <summary>"RIFF".</summary>
    constexpr uint32_t RIFF_ID = 0x46464952;
    /// <summary>"WAVE".</summary>
    constexpr uint32_t WAVE_ID = 0x45564157;
    /// <summary>"fmt ".</summary>
    constexpr uint32_t FORMAT_ID = 0x20746d66;
    /// <summary>"data".</summary>
    constexpr uint32_t DATA_ID = 0x61746164;

    /// <summary>The little-endian 32-bit value at <paramref name="offset"/> of <paramref name="image"/>.</summary>
    /// <summary>The 32-bit value at <paramref name="offset"/> of <paramref name="image"/> (0 past its end).</summary>
    uint32_t ReadU32(std::span<const uint8_t> image, size_t offset)
    {
        uint32_t value = 0;

        if (offset <= image.size() && image.size() - offset >= sizeof(value))
        {
            std::memcpy(&value, image.data() + offset, sizeof(value));
        }

        return value;
    }

    /// <summary>The wave format of a decoded HD replacement (PCM).</summary>
    tWAVEFORMATEX WaveFormatOf(const MCSoundFormat& format)
    {
        return {1,
                format.Channels,
                format.Rate,
                format.BytesPerSecond(),
                static_cast<uint16_t>(format.BlockAlign()),
                format.Bits,
                0};
    }
}

MCSoundResource::MCSoundResource(std::span<const uint8_t> image)
    : Type(MCSoundResourceType::Memory), _Image(image.begin(), image.end())
{
    ReadWaveInfo();
    TryHdSample();
}

MCSoundResource::MCSoundResource(MCSoundResourceType type, std::string_view fileName) : Type(type), _FileName(fileName)
{
    if (type == MCSoundResourceType::File)
    {
        LoadFile();
    }
    else if (type == MCSoundResourceType::Stream)
    {
        OpenStream();
    }
}

MCSoundResource::~MCSoundResource()
{
    if (_Stream != nullptr)
    {
        _Stream->Close();
    }
}

void MCSoundResource::LoadFile()
{
    std::ifstream file(MCFileSystem::Resolve(_FileName), std::ios::binary);

    // The original tested CreateFile's result against null, not INVALID_HANDLE_VALUE, so the Fatal never fired.
    if (!file)
    {
        Fatal(-1, "Cannot load sound resource!");
    }

    file.seekg(0, std::ios::end);
    _Image.resize(static_cast<size_t>(file.tellg()));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(_Image.data()), static_cast<std::streamsize>(_Image.size()));
    ReadWaveInfo();
    TryHdSample();
}

void MCSoundResource::TryHdSample()
{
    const std::shared_ptr<const MCHdAudio::Decoded> decoded = MCHdAudio::Sample(_Image);

    if (decoded == nullptr)
    {
        return;
    }

    Format = WaveFormatOf(decoded->Format);
    _Image = decoded->Pcm;
    _WaveOffset = 0;
    WaveSize = static_cast<uint32_t>(_Image.size());
}

void MCSoundResource::TryHdStream(uint64_t sourceFrames)
{
    const auto path = MCHdAudio::Music(_FileName);

    if (!path.has_value())
    {
        return;
    }

    auto flac = MCFlac::Open(*path);

    // A file cut short falls back before playback starts; the check decodes only the last frame, so a track change
    // doesn't hold the frame up. A bad frame further in plays as silence.
    if (!flac || !MCHdAudio::MatchesLength(*flac, sourceFrames, Format.nSamplesPerSec) || !flac->CheckEnd())
    {
        return;
    }

    Format = WaveFormatOf(flac->Format());
    // DurationMs stays the original's: the game uses it for music scheduling and fades.
    _Flac = std::move(flac);
    _Stream->Close();
    _Stream.reset();
}

void MCSoundResource::ReadWaveInfo()
{
    const std::span<const uint8_t> image = _Image;
    const uint32_t riffSize = ReadU32(image, 4);

    if (ReadU32(image, 0) != RIFF_ID || ReadU32(image, 8) != WAVE_ID)
    {
        Fatal(-1, "Unknown Sound File Format!");
    }

    // Issue #25: the chunks are walked only within the file (a RIFF size under 4 wrapped the original's end around),
    // and a data chunk is cut to the bytes the file holds.
    size_t chunk = 0xc;
    const size_t end = std::min(image.size(), 0xc + static_cast<size_t>(std::max<uint32_t>(riffSize, 4)) - 4);

    while (chunk + 8 <= end)
    {
        const uint32_t chunkId = ReadU32(image, chunk);
        const uint32_t chunkSize = ReadU32(image, chunk + 4);
        const size_t chunkData = chunk + 8;

        if (chunkId == FORMAT_ID)
        {
            if (chunkSize < 0xe)
            {
                Fatal(-1, "Cannot find FMT section!");
            }

            // The fields the game reads (a PCM chunk has no cbSize: it stays 0).
            Format = {};
            const size_t copied = std::min<size_t>({chunkSize, sizeof(Format), image.size() - chunkData});
            std::memcpy(&Format, image.subspan(chunkData, copied).data(), copied);
        }
        else if (chunkId == DATA_ID)
        {
            _WaveOffset = chunkData;
            WaveSize = static_cast<uint32_t>(std::min<size_t>(chunkSize, image.size() - chunkData));
        }

        chunk = chunkData + ((chunkSize + 1) & ~1u);
    }
}

uint32_t MCSoundResource::Read(std::span<uint8_t> buffer, bool loop, int32_t depth)
{
    if (_Flac != nullptr)
    {
        ReadFlac(buffer, loop);
        return static_cast<uint32_t>(buffer.size());
    }

    const auto bytes = static_cast<uint32_t>(buffer.size());

    if (depth > 3)
    {
        Fatal(static_cast<int32_t>(bytes), " Recursed more than 3 times into SoundResource::Read");
    }

    uint32_t bytesRead = 0;

    if (_Stream != nullptr)
    {
        bytesRead = static_cast<uint32_t>(_Stream->Read(buffer));
    }

    if (bytesRead == bytes)
    {
        return bytes;
    }

    if (loop)
    {
        Rewind();
        return Read(buffer.subspan(bytesRead), true, depth + 1) + bytesRead;
    }

    std::ranges::fill(buffer.subspan(bytesRead), Format.wBitsPerSample == 8 ? uint8_t{0x80} : uint8_t{0});
    return bytes;
}

void MCSoundResource::ReadFlac(std::span<uint8_t> buffer, bool loop)
{
    // The ring is sized and filled in whole frames; a partial frame at the end (none is expected) stays silent.
    const size_t whole = buffer.size() - buffer.size() % Format.nBlockAlign;
    size_t done = 0;
    bool rewound = false;

    while (done < whole)
    {
        const size_t read = _Flac->Read(buffer.subspan(done, whole - done));
        done += read;

        if (done == whole)
        {
            break;
        }

        // The end (or a bad frame): a looping stream starts again, unless it just did and read nothing.
        if (!loop || (read == 0 && rewound) || !_Flac->Seek(0))
        {
            break;
        }

        rewound = true;
    }

    std::ranges::fill(buffer.subspan(done), uint8_t{0});
}

void MCSoundResource::Rewind()
{
    if (_Flac != nullptr)
    {
        _Flac->Seek(0);
        return;
    }

    if (_Stream != nullptr)
    {
        _Stream->Seek(_DataStart, SEEK_SET);
    }
}

void MCSoundResource::OpenStream()
{
    _Stream = std::make_unique<MCFile>();
    MCFile& file = *_Stream;
    const int32_t result = file.Open(_FileName);

    if (result != 0)
    {
        Fatal(result, std::format("Could not open Music File {}", _FileName).c_str());
    }

    if (static_cast<uint32_t>(file.ReadLong()) != RIFF_ID)
    {
        Fatal(-1, std::format("Music File {} Not a RIFF file", _FileName).c_str());
    }

    file.ReadLong();

    if (static_cast<uint32_t>(file.ReadLong()) != WAVE_ID)
    {
        Fatal(-1, std::format("Music File {} Not a WAVE file", _FileName).c_str());
    }

    uint32_t chunkId = static_cast<uint32_t>(file.ReadLong());

    while (chunkId != FORMAT_ID && !file.Eof())
    {
        const int32_t chunkSize = file.ReadLong();
        file.Seek((chunkSize + 1) & ~1, SEEK_CUR);
        chunkId = static_cast<uint32_t>(file.ReadLong());
    }

    if (file.Eof())
    {
        Fatal(-1, std::format("Music File {} Has No FMT Chunk!", _FileName).c_str());
    }

    int32_t formatEnd = file.ReadLong();
    formatEnd += static_cast<int32_t>(file.GetLogicalPosition());
    uint32_t value = static_cast<uint32_t>(file.ReadLong());
    Format.wFormatTag = static_cast<uint16_t>(value);

    if (static_cast<int16_t>(value) != 1)
    {
        Fatal(-1, std::format("Music File {} Not Microsoft Format (PCM)", _FileName).c_str());
    }

    Format.nChannels = static_cast<uint16_t>(value >> 16);
    Format.nSamplesPerSec = static_cast<uint32_t>(file.ReadLong());
    Format.nAvgBytesPerSec = static_cast<uint32_t>(file.ReadLong());
    value = static_cast<uint32_t>(file.ReadLong());
    Format.nBlockAlign = static_cast<uint16_t>(value);
    Format.wBitsPerSample = static_cast<uint16_t>(value >> 16);
    Format.cbSize = 0;
    file.Seek(formatEnd, SEEK_SET);
    chunkId = static_cast<uint32_t>(file.ReadLong());

    while (chunkId != DATA_ID && !file.Eof())
    {
        const int32_t chunkSize = file.ReadLong();
        file.Seek((chunkSize + 1) & ~1, SEEK_CUR);
        chunkId = static_cast<uint32_t>(file.ReadLong());
    }

    if (file.Eof())
    {
        Fatal(-1, std::format("Music File {} Has No DATA Chunk!", _FileName).c_str());
    }

    const uint32_t size = static_cast<uint32_t>(file.ReadLong());
    const int32_t frameBits = static_cast<int32_t>(Format.wBitsPerSample * Format.nChannels);
    const int32_t frameBytes = (frameBits + ((frameBits >> 31) & 7)) >> 3;
    const uint32_t dataSize = size & ~static_cast<uint32_t>(frameBytes - 1);
    _DataStart = static_cast<int32_t>(file.GetLogicalPosition());
    Rewind();
    // A file with no byte rate has no length to tell (issue #25: the original divided by it).
    DurationMs = Format.nAvgBytesPerSec == 0
                     ? 0
                     : static_cast<uint32_t>(static_cast<int64_t>(dataSize) * 1000 / Format.nAvgBytesPerSec);
    TryHdStream(frameBytes <= 0 ? 0 : dataSize / static_cast<uint32_t>(frameBytes));
}
