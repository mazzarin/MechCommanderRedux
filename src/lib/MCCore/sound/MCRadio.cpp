#include "stdafx.h"
#include "sound/MCRadio.h"
#include "gui/MCGuiSmackerWindow.h"
#include "lib/MCDice.h"
#include "lib/MCDiskFile.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "object/MCBigGameObject.h"
#include "object/MCMechWarrior.h"
#include "platform/MCFileSystem.h"
#include "platform/MCSmacker.h"
#include "sound/MCRadioMessage.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>What PlayMessage returns when a message isn't played.</summary>
    constexpr int32_t RADIO_NOT_PLAYED = -0x152fffd;

    /// <summary>The fields of a radio.csv row, split at commas as strtok split them (empty fields vanish).</summary>
    std::vector<std::string_view> SplitRow(std::string_view line)
    {
        std::vector<std::string_view> fields;

        for (auto part : std::views::split(line, ','))
        {
            if (!part.empty())
            {
                fields.emplace_back(part.begin(), part.end());
            }
        }

        return fields;
    }

    /// <summary>A field read as atoi read it: leading spaces, a sign and digits; 0 for anything else.</summary>
    int32_t FieldNumber(std::string_view field)
    {
        return std::atoi(std::string(field).c_str());
    }

    /// <summary>Loads packet <paramref name="packet"/> of <paramref name="file"/>, or nothing when there is none.
    /// </summary>
    std::optional<std::vector<uint8_t>> LoadPacket(MCPacketFile& file, int32_t packet)
    {
        if (file.SeekPacket(packet) != 0)
        {
            return std::nullopt;
        }

        std::vector<uint8_t> data(static_cast<size_t>(file.GetPacketSize()));
        file.ReadPacket(packet, data);
        return data;
    }
}

namespace
{
    /// <summary>The movie code of a message that shows no video (radio.csv's).</summary>
    constexpr char NoMovieCode = 'x';
}

MCRadioMessageInfo ParseRadioMessageRow(std::string_view line)
{
    const std::vector<std::string_view> fields = SplitRow(line);
    // The first field is the message's name.
    size_t next = 1;
    auto field = [&fields, &next]() -> std::optional<std::string_view>
    {
        if (next >= fields.size())
        {
            return std::nullopt;
        }

        return fields[next++];
    };

    MCRadioMessageInfo info;
    std::optional<std::string_view> value = field();
    info.Priority = value.has_value() ? static_cast<uint8_t>(FieldNumber(*value)) : 4;
    value = field();
    info.ShelfLife = value.has_value() ? static_cast<float>(FieldNumber(*value)) : 0.0f;
    value = field();
    info.MovieCode = value.has_value() ? value->front() : '\0';
    value = field();
    info.Styles = value.has_value() ? static_cast<uint8_t>(FieldNumber(*value)) : 1;

    for (uint8_t& chance : info.StyleChance)
    {
        value = field();
        chance = value.has_value() ? static_cast<uint8_t>(FieldNumber(*value)) : 0;
    }

    value = field();
    info.PilotIdentifiesSelf = value.has_value() && value->front() == 'y';
    value = field();
    info.MsgId = value.has_value() ? FieldNumber(*value) : 0;
    return info;
}

std::expected<MCRadioMessageTable, int32_t> LoadRadioMessageInfo()
{
    MCFile infoFile;

    if (const int32_t result = infoFile.Open(GamePath(SoundPath, "radio", ".csv")); result != 0)
    {
        return std::unexpected(result);
    }

    std::array<uint8_t, 0x200> line{};
    infoFile.ReadLine(line, 0x1ff);
    MCRadioMessageTable table;

    for (MCRadioMessageInfo& info : table)
    {
        if (infoFile.ReadLine(line, 0x1ff) == 0)
        {
            Fatal(0, "Bad Message Info File");
        }

        info = ParseRadioMessageRow(reinterpret_cast<const char*>(line.data()));
    }

    infoFile.Close();
    return table;
}

MCRadio::MCRadio(MCSoundSystem& sound) : _Sound(sound)
{
}

MCRadio::~MCRadio() = default;

std::expected<std::unique_ptr<MCRadio>, int32_t> MCRadio::Create(MCSoundSystem& sound, std::string_view fileName,
                                                                 std::string_view movieName)
{
    auto radio = std::make_unique<MCRadio>(sound);
    radio->RadioFile = std::make_unique<MCPacketFile>();

    if (const int32_t result = radio->RadioFile->Open(GamePath(CDsoundPath, fileName, ".pak")); result != 0)
    {
        return std::unexpected(result);
    }

    // Port-only (the workspace's SPEC-first-use.md): the pilot's lines held as the radio is made (at the load, about
    // 0.6 MB a pilot), so a message queued in play reads nothing from the disk.
    radio->RadioFile->HoldAllPackets();

    radio->MovieName = movieName;

    // Faithful: a noise file that fails to open stays, unopened, for the radios after.
    if (sound.NoiseFile == nullptr)
    {
        sound.NoiseFile = std::make_unique<MCPacketFile>();

        if (const int32_t result = sound.NoiseFile->Open(GamePath(CDsoundPath, "noise", ".pak")); result != 0)
        {
            return std::unexpected(result);
        }

        // Port-only: and the radio's noise, as the pilots' lines.
        sound.NoiseFile->HoldAllPackets();
    }

    if (!sound.RadioMessageInfoLoaded)
    {
        std::expected<MCRadioMessageTable, int32_t> table = LoadRadioMessageInfo();

        if (!table.has_value())
        {
            Fatal(0, "Unable to load message info");
        }

        sound.RadioMessageInfo = *table;
        sound.RadioMessageInfoLoaded = true;
    }

    radio->ReadMovies(sound.RadioMessageInfo);
    return radio;
}

std::string MCRadio::VideoPath(char code) const
{
    return GamePath(MoviePath, std::format("{}{}", MovieName, code), ".smk");
}

void MCRadio::ReadMovies(const MCRadioMessageTable& table)
{
    if (MovieName.empty())
    {
        return;
    }

    // Each code once, read or not (most messages share a few).
    std::set<char> tried;

    for (const MCRadioMessageInfo& info : table)
    {
        if (info.MovieCode == NoMovieCode || info.MovieCode == '\0' || !tried.insert(info.MovieCode).second)
        {
            continue;
        }

        // A video that can't be read is left to the message, which opens it from the disk as the original did (and
        // shows none when that fails too).
        const std::string path = VideoPath(info.MovieCode);
        std::optional<MCDiskFile> file =
            MCDiskFile::Open(MCFileSystem::Resolve(path), MCDiskFileMode::Read, MCDiskFileReads::Direct);

        if (!file.has_value())
        {
            SDL_Log("MCRadio: the pilot's video %s isn't read ahead: it can't be opened", path.c_str());
            continue;
        }

        std::vector<uint8_t> bytes(file->Length());

        if (file->Read(bytes) != bytes.size())
        {
            SDL_Log("MCRadio: the pilot's video %s isn't read ahead: it reads short", path.c_str());
            continue;
        }

        Movies.emplace(info.MovieCode, std::move(bytes));
    }
}

int32_t MCRadio::PlayMessage(MCRadioMessageType msgType)
{
    const std::shared_ptr<MCMechWarrior> owner = Owner.lock();

    if (UseSound == 0 || !Enabled || owner == nullptr)
    {
        return RADIO_NOT_PLAYED;
    }

    const MCRadioMessageInfo& info = _Sound.RadioMessageInfo[std::to_underlying(msgType)];

    if (!_Sound.RadioQueue.MayQueue(*owner, info.Priority, msgType))
    {
        return RADIO_NOT_PLAYED;
    }

    // Pick a variation by its odds, not the one this pilot just said.
    int32_t variation = 0;

    if (info.Styles > 1)
    {
        const int32_t roll = RandomNumber(100);
        const int32_t styles = info.Styles;
        int32_t total = 0;
        int32_t style = 0;

        for (; style < styles; style++)
        {
            // The original read the odds as bytes on past the three columns (radio.csv never has more styles).
            total += style < std::ssize(info.StyleChance) ? info.StyleChance[style] : 0;

            if (roll < total)
            {
                break;
            }
        }

        if (style != 0 && style == styles)
        {
            return RADIO_NOT_PLAYED;
        }

        variation = style;

        if (info.MsgId + variation == owner->LastMessage)
        {
            variation++;
        }

        if (variation >= styles)
        {
            variation = 0;
        }
    }

    auto message = std::make_unique<MCRadioMessage>();
    message->ExpirationDate = ScenarioTime + info.ShelfLife;
    message->MsgType = msgType;
    message->TurnQueued = Turn;
    message->MsgId = static_cast<uint32_t>(info.MsgId + variation);
    message->Priority = info.Priority;
    message->Pilot = owner;

    // The pilot's video, when the tactical map shows its video window.
    const std::shared_ptr<MCTacticalMap> tacMap = TacticalMap();

    if (info.MovieCode != NoMovieCode && !MovieName.empty() &&
        owner->GetVehicle()->ObjectClass == MCObjectClass::BattleMech && tacMap->IsShowing() != 0 &&
        tacMap->IsHidden() == 0 && tacMap->DisplayType == MCTacmapPage::Map && tacMap->VideoWindow != nullptr)
    {
        // On the screen, hidden until the message plays (the original added it to the screen then).
        message->MovieWindow = ScreenWindow()->CreateChild<MCGuiSmackerWindow>(
            [&message, area = tacMap->GetVideoRect()](MCGuiSmackerWindow& window)
            {
                message->MovieWindow = window.WeakAs<MCGuiSmackerWindow>();
                window.Init(area, {});
                window.ShowGuiWindow(false);
            });
        const std::string path = VideoPath(info.MovieCode);
        const auto held = Movies.find(info.MovieCode);
        message->Movie =
            held != Movies.end() ? SmackOpenMemory(path, held->second) : SmackOpen(path.c_str(), 0xfe000, -1);
    }

    // The pilot's name first (sometimes), then the message, and the static before them.
    if (info.PilotIdentifiesSelf)
    {
        int32_t idPacket = 0;
        const int32_t roll = RandomNumber(100);

        if (roll < 45)
        {
            idPacket = 10;
        }

        if (roll < 30)
        {
            idPacket = 9;
        }

        if (idPacket != 0)
        {
            // The original gave up on the message when the radio heap was full; the port's memory isn't.
            if (std::optional<std::vector<uint8_t>> id = LoadPacket(*RadioFile, idPacket); id.has_value())
            {
                message->Fragments.push_back(std::move(*id));
            }
        }
    }

    if (std::optional<std::vector<uint8_t>> speech = LoadPacket(*RadioFile, static_cast<int32_t>(message->MsgId));
        speech.has_value())
    {
        message->Fragments.push_back(std::move(*speech));

        if (std::optional<std::vector<uint8_t>> noise =
                LoadPacket(*_Sound.NoiseFile, static_cast<int32_t>(message->NoiseId));
            noise.has_value())
        {
            message->Noise = std::move(*noise);
        }
    }

    const int32_t msgId = static_cast<int32_t>(message->MsgId);

    if (_Sound.QueueRadioMessage(std::move(message)))
    {
        return msgId;
    }

    return RADIO_NOT_PLAYED;
}
