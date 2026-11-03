#include "stdafx.h"
#include "platform/MCFlac.h"
#define DR_FLAC_NO_STDIO
#include "dr_flac.h"

struct MCFlac::Impl
{
    std::ifstream File;
    /// <summary>The file's bytes, when it is read from memory.</summary>
    std::shared_ptr<const std::vector<uint8_t>> Memory;
    drflac* Decoder = nullptr;

    ~Impl() { drflac_close(Decoder); }

    static size_t Read(void* user, void* data, size_t size)
    {
        auto& file = static_cast<Impl*>(user)->File;
        file.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
        return static_cast<size_t>(file.gcount());
    }

    static drflac_bool32 Seek(void* user, int offset, drflac_seek_origin origin)
    {
        auto& file = static_cast<Impl*>(user)->File;
        file.clear();
        file.seekg(offset, origin == DRFLAC_SEEK_SET   ? std::ios::beg
                           : origin == DRFLAC_SEEK_END ? std::ios::end
                                                       : std::ios::cur);
        return file.good();
    }

    static drflac_bool32 Tell(void* user, drflac_int64* cursor)
    {
        auto& file = static_cast<Impl*>(user)->File;
        *cursor = static_cast<drflac_int64>(file.tellg());
        return *cursor >= 0;
    }
};

MCFlac::MCFlac() : _Impl(std::make_unique<Impl>())
{
}

MCFlac::~MCFlac() = default;

std::unique_ptr<MCFlac> MCFlac::Open(const std::filesystem::path& path)
{
    auto result = std::unique_ptr<MCFlac>(new MCFlac());
    result->_Impl->File.open(path, std::ios::binary);

    if (!result->_Impl->File)
    {
        return nullptr;
    }

    result->_Impl->Decoder = drflac_open(Impl::Read, Impl::Seek, Impl::Tell, result->_Impl.get(), nullptr);
    return result->Opened() ? std::move(result) : nullptr;
}

std::unique_ptr<MCFlac> MCFlac::Open(std::shared_ptr<const std::vector<uint8_t>> bytes)
{
    if (bytes == nullptr)
    {
        return nullptr;
    }

    auto result = std::unique_ptr<MCFlac>(new MCFlac());
    result->_Impl->Memory = std::move(bytes);
    const std::vector<uint8_t>& memory = *result->_Impl->Memory;
    result->_Impl->Decoder = drflac_open_memory(memory.data(), memory.size(), nullptr);
    return result->Opened() ? std::move(result) : nullptr;
}

std::shared_ptr<const std::vector<uint8_t>> MCFlac::ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(path, error);

    if (!file || error)
    {
        return nullptr;
    }

    auto bytes = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    return file.gcount() == static_cast<std::streamsize>(bytes->size()) ? std::move(bytes) : nullptr;
}

bool MCFlac::Opened()
{
    const drflac* decoder = _Impl->Decoder;

    // Require STREAMINFO's exact length. Limit both allocation and maliciously long validation work to ten minutes.
    if (decoder == nullptr || decoder->channels < 1 || decoder->channels > 2 || decoder->sampleRate < 100 ||
        decoder->sampleRate > 200000 || decoder->bitsPerSample > 24 || decoder->totalPCMFrameCount == 0 ||
        decoder->totalPCMFrameCount > static_cast<uint64_t>(decoder->sampleRate) * 600)
    {
        return false;
    }

    _Format = {decoder->sampleRate, static_cast<uint16_t>(decoder->channels), 16};
    _Frames = decoder->totalPCMFrameCount;
    return true;
}

size_t MCFlac::Read(std::span<uint8_t> bytes)
{
    const size_t frames = bytes.size() / _Format.BlockAlign();
    // The stream ring may not have int16_t alignment. A small aligned scratch buffer avoids aliasing and alignment UB.
    std::array<int16_t, 4096> pcm;
    size_t done = 0;

    while (done < frames)
    {
        const size_t wanted = std::min(frames - done, pcm.size() / _Format.Channels);
        const size_t read = static_cast<size_t>(drflac_read_pcm_frames_s16(_Impl->Decoder, wanted, pcm.data()));

        for (size_t i = 0; i < read * _Format.Channels; ++i)
        {
            const uint16_t sample = static_cast<uint16_t>(pcm[i]);
            const size_t at = done * _Format.BlockAlign() + i * 2;
            bytes[at] = static_cast<uint8_t>(sample);
            bytes[at + 1] = static_cast<uint8_t>(sample >> 8);
        }

        done += read;

        if (read < wanted)
        {
            break;
        }
    }

    return done * _Format.BlockAlign();
}

bool MCFlac::Seek(uint64_t frame)
{
    return frame < _Frames && drflac_seek_to_pcm_frame(_Impl->Decoder, frame) != 0;
}

bool MCFlac::Validate()
{
    if (!Seek(0))
    {
        return false;
    }

    std::array<uint8_t, 16384> block;
    uint64_t bytes = 0;

    while (const size_t read = Read(block))
    {
        bytes += read;

        if (bytes > _Frames * _Format.BlockAlign())
        {
            return false;
        }
    }

    return bytes == _Frames * _Format.BlockAlign() && Seek(0);
}

bool MCFlac::CheckEnd()
{
    // A frame is at most two 16-bit samples: Open takes mono or stereo, and Read gives 16-bit.
    std::array<uint8_t, 2 * sizeof(int16_t)> last;
    const auto frame = std::span(last).first(_Format.BlockAlign());
    return Seek(_Frames - 1) && Read(frame) == frame.size() && Seek(0);
}
