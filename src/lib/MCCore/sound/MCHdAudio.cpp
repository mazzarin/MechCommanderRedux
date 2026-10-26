#include "stdafx.h"
#include "sound/MCHdAudio.h"
#include "lib/MCFitIniFile.h"
#include "platform/MCFlac.h"
#include "sprite/MCHdArt.h"

namespace
{
    std::optional<std::string> MusicSet()
    {
        const std::filesystem::path path = MCHdArt::PackFolder() / "audio" / "audio.cfg";
        std::error_code error;

        if (!std::filesystem::is_regular_file(path, error))
        {
            return std::nullopt;
        }

        MCFitIniFile file;

        if (file.Open(path.string()) != 0 || file.SeekBlock("HDAudioPack") != 0 || file.Read<int32_t>("Format") != 1)
        {
            return std::nullopt;
        }

        const auto selected = file.Read<std::string>("MusicSet");
        return selected.has_value() ? *selected : "stereo";
    }

    std::optional<std::filesystem::path> Existing(const std::filesystem::path& path)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) ? std::optional(path) : std::nullopt;
    }
}

std::optional<std::filesystem::path> MCHdAudio::Music(std::string_view original)
{
    const auto set = MusicSet();

    if (!set.has_value() || (*set != "stereo" && *set != "orchestral"))
    {
        return std::nullopt;
    }

    const std::string_view file = original.substr(original.find_last_of("/\\") + 1);
    std::string name(file.substr(0, file.find_last_of('.')));

    // Only the game's MUSICnn names are eligible; never interpret caller paths inside the pack.
    if (name.size() != 7 ||
        !std::equal(name.begin(), name.begin() + 5, "MUSIC",
                    [](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == b; }) ||
        name[5] < '0' || name[5] > '9' || name[6] < '0' || name[6] > '9')
    {
        return std::nullopt;
    }

    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const auto root = MCHdArt::PackFolder() / "audio" / "music";
    auto path = Existing(root / *set / (name + ".flac"));

    if (!path.has_value() && *set == "orchestral")
    {
        path = Existing(root / "stereo" / (name + ".flac"));
    }

    return path;
}

std::string MCHdAudio::SampleKey(std::span<const uint8_t> wave)
{
    const auto parsed = MCAudio::ParseWave(wave);

    if (!parsed)
    {
        return {};
    }

    uint64_t hash = 14695981039346656037ull;
    const auto byte = [&hash](uint8_t value) { hash = (hash ^ value) * 1099511628211ull; };

    for (unsigned i = 0; i < 4; ++i)
    {
        byte(static_cast<uint8_t>(parsed->Format.Rate >> (i * 8)));
    }

    for (uint16_t field : {parsed->Format.Channels, parsed->Format.Bits})
    {
        byte(static_cast<uint8_t>(field));
        byte(static_cast<uint8_t>(field >> 8));
    }

    for (uint8_t sample : parsed->Samples)
    {
        byte(sample);
    }

    return std::format("{:016x}", hash);
}

std::optional<std::filesystem::path> MCHdAudio::Sample(std::span<const uint8_t> wave)
{
    if (!MusicSet().has_value())
    {
        return std::nullopt;
    }

    const std::string key = SampleKey(wave);
    return key.empty() ? std::nullopt : Existing(MCHdArt::PackFolder() / "audio" / "samples" / (key + ".flac"));
}

bool MCHdAudio::MatchesLength(const MCFlac& flac, uint64_t sourceFrames, uint32_t sourceRate)
{
    if (sourceRate == 0)
    {
        return false;
    }

    const uint64_t wanted = (sourceFrames * flac.Format().Rate + sourceRate / 2) / sourceRate;
    return flac.Frames() == wanted;
}
