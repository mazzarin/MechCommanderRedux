#pragma once

class MCMechWarrior;
class MCPacketFile;
class MCSoundSystem;

/// <summary>
/// The pilots' radio messages, in the order of <c>data\sound\radio.csv</c> (whose first column gives the names). The
/// values are data: scripts and network chunks send them as numbers.
/// </summary>
enum class MCRadioMessageType : int32_t
{
    MoveTo = 0,
    RunTo = 1,
    JumpTo = 2,
    AllStop = 3,
    Attack = 4,
    RangeAttack = 5,
    AttackFromHere = 6,
    AttackRam = 7,
    Dfa = 8,
    AttackBody = 9,
    Capture = 10,
    CannotCapture = 11,
    CapturedBuilding = 12,
    CapturedVehicle = 13,
    Refit = 14,
    RefitDone = 15,
    Power = 16,
    ObjectDestroyed = 17,
    VehicleDestroyed = 18,
    MechDestroyed = 19,
    SensorContact = 20,
    MinelayerSighted = 21,
    HittingMines = 22,
    UnderAttack = 23,
    UnderArtillery = 24,
    UnderAirstrike = 25,
    MoveBlocked = 26,
    IllegalOrder = 27,
    Ejecting = 28,
    Death = 29,
    Crippled = 30,
    Disabled = 31,
    ArmorHoled = 32,
    PilotHurt = 33,
    Weapons50 = 34,
    WeaponsOut = 35,
    AmmoOut = 36,
    Taunt = 37,
    Deploy = 38,
    Load = 39,
};

/// <summary>How many radio message types there are (the rows of radio.csv the game reads).</summary>
inline constexpr int32_t RadioMessageTypeCount = 40;

/// <summary>A row of <c>radio.csv</c>: how a message type is played.</summary>
/// <remarks>Original source: <c>sound\radio.cpp</c>.</remarks>
struct MCRadioMessageInfo
{
    /// <summary>"priority" (4 when blank): lower plays first.</summary>
    uint8_t Priority = 0;
    /// <summary>"shelflife": seconds the queued message stays worth playing.</summary>
    float ShelfLife = 0.0f;
    /// <summary>"movie": the video letter after the pilot's movie name; 'x' for none.</summary>
    char MovieCode = '\0';
    /// <summary>"styles" (1 when blank): how many variations the message has.</summary>
    uint8_t Styles = 0;
    /// <summary>"style1%".."style3%": the odds of each variation.</summary>
    std::array<uint8_t, 3> StyleChance{};
    /// <summary>"map to": the first variation's packet in the pilot's sound file.</summary>
    int32_t MsgId = 0;
    /// <summary>"pilot id?" ('y'): the pilot may say who he is first (packet 9 or 10).</summary>
    bool PilotIdentifiesSelf = false;
};

/// <summary>Every message type's row of radio.csv.</summary>
using MCRadioMessageTable = std::array<MCRadioMessageInfo, RadioMessageTypeCount>;

/// <summary>Reads <c>radio.csv</c> (SoundPath). Fatal on a short file.</summary>
/// <returns>The table, or the open error.</returns>
std::expected<MCRadioMessageTable, int32_t> LoadRadioMessageInfo();
/// <summary>
/// Parses a row of radio.csv (name, priority, shelf life, movie, styles, three style odds, pilot id, packet). Fields
/// are split at commas as strtok split them (empty fields vanish); a missing field takes its default.
/// </summary>
MCRadioMessageInfo ParseRadioMessageRow(std::string_view line);

/// <summary>
/// A pilot's radio: his sound packet file and video name. Plays a message by queueing its sound (and video) with the
/// <see cref="MCSoundSystem"/>, which owns every radio and the message info and noise they share.
/// </summary>
/// <remarks>Original source: <c>sound\radio.cpp</c>.</remarks>
class MCRadio
{
public:
    /// <summary>
    /// Opens the pilot's sound file (<paramref name="fileName"/>.pak in CDsoundPath) and the shared noise file, keeps
    /// the movie name, and loads radio.csv once. The caller hands it to <see cref="MCSoundSystem::AddRadio"/>.
    /// </summary>
    /// <returns>The radio, or the file error.</returns>
    static std::expected<std::unique_ptr<MCRadio>, int32_t> Create(MCSoundSystem& sound, std::string_view fileName,
                                                                   std::string_view movieName);
    /// <summary>A radio with no files yet, playing through <paramref name="sound"/> (<see cref="Create"/> opens
    /// them).</summary>
    explicit MCRadio(MCSoundSystem& sound);
    /// <summary>Closes the sound file.</summary>
    ~MCRadio();

    MCRadio(const MCRadio&) = delete;
    MCRadio& operator=(const MCRadio&) = delete;

    /// <summary>
    /// Builds and queues a message: picks a variation by its odds (not the one just played), loads its fragments
    /// (and the pilot's id first, sometimes) and noise, opens the pilot's video when the tactical map shows one.
    /// </summary>
    /// <returns>The packet queued, or -0x152fffd when it isn't played.</returns>
    int32_t PlayMessage(MCRadioMessageType msgType);

    /// <summary>The pilot's sound packets.</summary>
    std::unique_ptr<MCPacketFile> RadioFile;
    /// <summary>The pilot.</summary>
    std::weak_ptr<MCMechWarrior> Owner;
    /// <summary>The pilot's movie name; empty for none.</summary>
    std::string MovieName;
    /// <summary>
    /// Port-only (the first-use rule): the pilot's videos by their message's movie code, read as the radio is made (at
    /// the load, about 70 KB each), so a message's video opens from memory in play.
    /// </summary>
    std::map<char, std::vector<uint8_t>> Movies;
    /// <summary>Whether the radio plays.</summary>
    bool Enabled = true;

private:
    /// <summary>The game path of the pilot's video for movie code <paramref name="code"/>.</summary>
    std::string VideoPath(char code) const;

    /// <summary>Reads every video of the pilot the table's messages can show into <see cref="Movies"/>.</summary>
    void ReadMovies(const MCRadioMessageTable& table);

    /// <summary>The sound system the radio plays through.</summary>
    MCSoundSystem& _Sound;
};
