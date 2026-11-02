#pragma once

#include "lib/MCOptionalRef.h"
#include "sound/MCRadio.h"
#include "sound/MCRadioMessage.h"

#include <random>

class MCAudioStream;
class MCFlac;
class MCGameObject;
class MCMechWarrior;
class MCPacketFile;
class MCSoundRenderer;
class MCSoundResource;

/// <summary>
/// Sound renderer channels the sound system opens: 16 for effects and speech, 2 for music streams. The channels'
/// jobs below are the game's layout, not a memory limit.
/// </summary>
inline constexpr int32_t NUM_SOUND_CHANNELS = 18;
/// <summary>The effect and speech channels.</summary>
inline constexpr int32_t NUM_SAMPLE_CHANNELS = 16;
/// <summary>The channel pilot speech (and logistics speech) plays on.</summary>
inline constexpr int32_t PILOT_SPEECH_CHANNEL = 0;
/// <summary>The channel Betty (the computer voice) plays on.</summary>
inline constexpr int32_t BETTY_CHANNEL = 14;
/// <summary>The channel radio static (and the fire loop) plays on.</summary>
inline constexpr int32_t NOISE_CHANNEL = 15;
/// <summary>The first of the two music stream channels, cross-faded.</summary>
inline constexpr int32_t MUSIC_CHANNEL_A = 16;
/// <summary>The second music stream channel.</summary>
inline constexpr int32_t MUSIC_CHANNEL_B = 17;

/// <summary>A sound effect of the sound file ("SoundBite%d" blocks).</summary>
/// <remarks>Original source: <c>sound\soundsys.cpp</c>.</remarks>
struct MCSoundBite
{
    /// <summary>"priority" (read, not used).</summary>
    uint32_t Priority = 0;
    /// <summary>"cache" (read, not used).</summary>
    uint32_t Cache = 0;
    /// <summary>"soundId" (read, not used).</summary>
    uint32_t SoundId = 0;
    /// <summary>The wave, when loaded (empty while not).</summary>
    std::vector<uint8_t> BiteData;
    /// <summary>"volume" (0..1): the bite's level, 20 * (volume - 1) dB (see <see cref="SoundBiteGain"/>).</summary>
    float Volume = 0.0f;
};

/// <summary>What the sound system keeps for each effect and speech channel.</summary>
struct MCSampleChannel
{
    /// <summary>The sample playing; -1 for none.</summary>
    int32_t SampleId = -1;
    /// <summary>Set when a positional effect starts on the channel; cleared every update.</summary>
    bool InUse = false;
    /// <summary>Set to fade the channel out (update lowers it 1/64 a frame, then stops it).</summary>
    bool FadeDown = false;
    /// <summary>Where a positional effect was started (x).</summary>
    float X = 0.0f;
    /// <summary>Where a positional effect was started (y).</summary>
    float Y = 0.0f;
};

/// <summary>One of the two cross-faded music streams.</summary>
struct MCMusicStream
{
    /// <summary>Set while it plays.</summary>
    bool Playing = false;
    /// <summary>Its fade: positive fading in, negative fading out (seconds left), 0 steady.</summary>
    float Fade = 0.0f;
};

/// <summary>What the music playing is, as the music choice in Update keeps it. The names are the port's.</summary>
enum class MCMusicState : int32_t
{
    /// <summary>A contact cue (DMS 5..7).</summary>
    Contact = 0,
    /// <summary>Combat music (DMS 14..19).</summary>
    Combat = 1,
    /// <summary>Never set.</summary>
    Unused = 2,
    /// <summary>Set when a music below 5 is asked for.</summary>
    Low = 3,
    /// <summary>The enemy-destroyed cue (DMS 12).</summary>
    EnemyDestroyed = 4,
    /// <summary>The friendly-destroyed cue (DMS 13).</summary>
    FriendlyDestroyed = 5,
    /// <summary>An ABL script's music.</summary>
    Abl = 6,
    /// <summary>The ambient music (DMS 21).</summary>
    Ambient = 7,
};

/// <summary>Whether sound is on (the prefs).</summary>
extern int32_t UseSound;
/// <summary>Whether music is on.</summary>
extern int32_t UseMusic;
/// <summary>Music volume, 0..127 (the prefs).</summary>
extern int32_t MusicVolume;
/// <summary>Radio volume, 0..127.</summary>
extern int32_t RadioVolume;
/// <summary>Effects volume, 0..127.</summary>
extern int32_t SfxVolume;
/// <summary>Interface volume, 0..127 (the port's: clicks and the like apart from the effects).</summary>
extern int32_t InterfaceVolume;
/// <summary>Music state: the player's forces are fighting.</summary>
extern int32_t InCombat;
/// <summary>Music state: the home team has an enemy contact (the team updates it, MCTeam.cpp).</summary>
extern int32_t InContact;
/// <summary>Music state: a friendly unit was destroyed.</summary>
extern int32_t FriendlyDestroyed;
/// <summary>Music state: an enemy unit was destroyed.</summary>
extern int32_t EnemyDestroyed;

/// <summary>Whether <paramref name="data"/> starts with a RIFF WAVE header.</summary>
bool IsWaveImage(std::span<const uint8_t> data);
/// <summary>
/// The channel volume factor of a sound bite's <paramref name="volume"/> (0..1): the 20 * (volume - 1) dB the
/// original gave it (1 is 0 dB, 0 is -20 dB), under the port's fader taper.
/// </summary>
float SoundBiteGain(float volume);
/// <summary>
/// The pan position (0..128, 64 centre) of a sound <paramref name="dx"/>, <paramref name="dy"/> from the listener:
/// the angle from the screen's up direction (the world axes turned by 45 degrees), folded to the front.
/// </summary>
int32_t SoundPanPosition(float dx, float dy);

/// <summary>
/// The game's sound: effects placed by distance and angle from the camera, the radio message queue with its static
/// and pilot videos, Betty's announcements, pilot speech, and cross-faded streaming music (by ABL request and combat
/// state). Plays through the <see cref="MCSoundRenderer"/>'s channels. A game system of <see cref="MCGameContext"/>
/// (SoundSystem()), made by UserInit and kept until shutdown. It owns every pilot's radio.
/// </summary>
/// <remarks>Original source: <c>sound\soundsys.cpp</c>. Field names follow MechCommander 2's SoundSystem where it
/// kept them.</remarks>
class MCSoundSystem
{
public:
    /// <summary>
    /// Installs the sound renderer with 18 channels (Smacker shares its mixer); channels empty, volumes 127, no
    /// music, no sound file.
    /// </summary>
    MCSoundSystem();
    /// <summary>
    /// As the default constructor, then (with sound on) reads <paramref name="soundFileName"/>.snd: the setup (volumes,
    /// max distance), the sound bites (preloading some) and the digital music list; opens the .pak and Betty files.
    /// </summary>
    explicit MCSoundSystem(std::string_view soundFileName);
    /// <summary>With sound on: purges, then frees the bites and closes the files.</summary>
    ~MCSoundSystem();

    MCSoundSystem(const MCSoundSystem&) = delete;
    MCSoundSystem& operator=(const MCSoundSystem&) = delete;

    /// <summary>Stops every channel; drops the music streams, the message queue, the radios and the loaded bites.
    /// </summary>
    void PurgeSoundSystem();
    /// <summary>Plays radio static on its channel.</summary>
    void PlayStaticNoise();
    void StopStaticNoise();
    /// <summary>
    /// Per frame: fades out stopped channels, plays the next radio fragment or message, cross-fades and picks the
    /// music from ABL requests and the combat state. Port-only: the rain bed and the thunder, as the weather shown is
    /// heard while a scenario runs (MCWeather::HeardLook).
    /// </summary>
    void Update();
    /// <summary>
    /// Port-only (HD weather): the rain bed, the HD audio pack's rain loop (MCHdAudio::RainLoop) streamed round and
    /// round at the effects volume, easing to <paramref name="heard"/> (MCWeather::MCWeatherLook::Heard, a channel
    /// volume) over RainFade seconds of the frames' time (FrameLength), and stopped at none. Without the pack's loop
    /// (or with one that won't play) there is none till the purge.
    /// </summary>
    void UpdateRain(float heard);
    /// <summary>How loud the rain bed plays (as <see cref="UpdateRain"/> takes it), eased toward what is heard.</summary>
    float RainLevel() const { return _RainLevel; }
    /// <summary>
    /// Port-only (HD weather): thunder, the HD audio pack's (MCHdAudio::Thunder), rolling in from beyond the view about
    /// <paramref name="claps"/> times a minute (MCWeather::MCWeatherLook::Thunder) by the frames' time (FrameLength).
    /// Each clap plays the thunder once, slower or faster (deeper and farther, or nearer), from one side or the other,
    /// at the effects volume, as loud as <paramref name="heard"/> (a channel volume) for the nearest; one rolls at a
    /// time. Its own dice, thrown from their start again at the purge, so a mission hears the same thunder each time it
    /// is played. None while no claps come (one rolling plays out); without the pack's thunder, none till the purge.
    /// </summary>
    void UpdateThunder(float claps, float heard);
    /// <summary>Whether a clap of thunder is rolling.</summary>
    bool ThunderRolling() const { return _ThunderStream != nullptr; }
    /// <summary>Streams music <paramref name="musicId"/> ("DMS%d", +25 on the second planet), fading from the stream
    /// playing.</summary>
    /// <returns>0, or -0x5445fff2 when it is already playing or a fade is under way.</returns>
    int32_t PlayDigitalMusic(int32_t musicId, bool loop);
    /// <summary>Plays Betty sample <paramref name="bettyId"/> on her channel.</summary>
    /// <returns>Her channel, or -1.</returns>
    int32_t PlayBettySample(uint32_t bettyId);
    /// <summary>Whether sample <paramref name="sampleId"/> is on one of the effect channels.</summary>
    bool IsSamplePlaying(int32_t sampleId) const;
    /// <summary>Whether channel <paramref name="channel"/> (0..16) plays.</summary>
    bool IsChannelPlaying(int32_t channel) const;
    /// <summary>
    /// Plays sound bite <paramref name="sampleId"/> (unless already playing) on a free channel of its kind, panned by
    /// <paramref name="source"/>'s place relative to the camera; nothing beyond MaxSoundDistance (15 times that when
    /// <paramref name="farRange"/> is set).
    /// </summary>
    /// <param name="atCamera">Place it at the camera, not the source (channels 11..13 instead of 1..9).</param>
    /// <returns>The channel, or -1.</returns>
    int32_t PlayDigitalSample(uint32_t sampleId, uint32_t channelType, MCOptionalRef<MCGameObject> source,
                              bool atCamera, bool farRange);
    /// <summary>
    /// Plays an interface sound (a click, a drop, a confirmation): sound bite <paramref name="sampleId"/> as
    /// <see cref="PlayDigitalSample"/> plays one without a source, at <see cref="InterfaceLevel"/> instead of the
    /// effects volume.
    /// </summary>
    /// <returns>The channel, or -1.</returns>
    int32_t PlayInterfaceSample(uint32_t sampleId);
    /// <summary>Fades the channel out (update stops it).</summary>
    void StopDigitalSample(int32_t channel);
    void StopDigitalMusic();
    /// <summary>Plays a speech wave (packet <paramref name="speechId"/> of <paramref name="fileName"/>.pak) on the
    /// pilot speech channel, in logistics.</summary>
    int32_t PlayPilotSpeech(std::string_view fileName, int32_t speechId);
    /// <summary>An ABL script's music request.</summary>
    void PlayAblDigitalMusic(int32_t musicId);
    /// <summary>Stops the music (with sound on).</summary>
    void StopAblMusic();
    /// <summary>Plays an effect for an ABL script (no source).</summary>
    void PlayAblsfx(int32_t sfxId);
    /// <summary>Does nothing.</summary>
    void PlayAblVideo(int32_t videoId);
    /// <summary>Makes the first queued message the current one.</summary>
    void MoveFromQueueToPlaying();
    /// <summary>Drops the current message (and its video) and stops the speech channel.</summary>
    void RemoveCurrentMessage();
    /// <summary>
    /// Queues a message by priority. A priority 1 message cuts off the one playing and the pilot's waiting ones.
    /// </summary>
    /// <returns>Whether it was queued (the message is dropped when not).</returns>
    bool QueueRadioMessage(std::unique_ptr<MCRadioMessage> message);
    /// <summary>Takes ownership of a pilot's radio (until the next purge).</summary>
    std::shared_ptr<MCRadio> AddRadio(std::unique_ptr<MCRadio> radio);
    /// <summary>The radios there are.</summary>
    size_t RadioCount() const { return _Radios.size(); }

    /// <summary>The sound bites ("numBites" of them).</summary>
    std::vector<MCSoundBite> Sounds;
    /// <summary>"MaxSoundDistance".</summary>
    float MaxSoundDistance = 0.0f;
    /// <summary>Each effect and speech channel's state.</summary>
    std::array<MCSampleChannel, NUM_SAMPLE_CHANNELS> SampleChannels{};
    /// <summary>The music streams (channels A and B).</summary>
    std::array<MCMusicStream, 2> Streams{};
    /// <summary>"DMS%d": each music's file name.</summary>
    std::vector<std::string> DigitalMusicIds;
    /// <summary>"StreamFadeDownTime": the cross-fade's length in seconds.</summary>
    float StreamFadeDownTime = 0.0f;
    /// <summary>The music playing; -1 for none.</summary>
    int32_t CurrentMusicId = -1;
    /// <summary>The radio message playing.</summary>
    std::unique_ptr<MCRadioMessage> CurrentMessage;
    /// <summary>The messages waiting.</summary>
    MCRadioQueue RadioQueue;
    /// <summary>The current message's fragment playing.</summary>
    uint32_t CurrentFragment = 0;
    /// <summary>Set while the fragment's noise plays.</summary>
    bool PlayingNoise = false;
    /// <summary>Set once the current message has finished.</summary>
    bool WholeMsgDone = false;
    /// <summary>Effects volume (SFXVolume), 0..127.</summary>
    uint8_t DigitalMasterVolume = 127;
    /// <summary>Interface volume (InterfaceVolume), 0..127.</summary>
    uint8_t InterfaceLevel = 127;
    /// <summary>Radio volume (RadioVolume).</summary>
    uint8_t RadioLevel = 127;
    /// <summary>Music volume (MusicVolume).</summary>
    uint8_t MusicLevel = 127;
    /// <summary>The radio static (and pilot id) packets (noise.pak), shared by the radios.</summary>
    std::unique_ptr<MCPacketFile> NoiseFile;
    /// <summary>radio.csv, loaded by the first radio after a purge.</summary>
    MCRadioMessageTable RadioMessageInfo{};
    /// <summary>Set once radio.csv is loaded.</summary>
    bool RadioMessageInfoLoaded = false;
    /// <summary>The logistics screen's pilot speech (empty for none); freed once it has played.</summary>
    std::vector<uint8_t> PilotLogisticsSpeech;
    /// <summary>The pilot speech packet playing.</summary>
    int32_t CurrentPilotSpeech = 0;
    /// <summary>The last Betty sample played.</summary>
    int32_t LastBettyId = 0;

private:
    /// <summary>Loads sound bite <paramref name="biteId"/>'s wave from the sound file into
    /// <see cref="Sounds"/>.</summary>
    /// <returns>Whether its packet was there (and not empty).</returns>
    bool PreloadSoundBite(int32_t biteId);
    /// <summary>
    /// Loads Betty sample <paramref name="bettyId"/>, replacing the last one; an empty packet leaves the last one (and
    /// reads nothing into it), as the original did.
    /// </summary>
    /// <returns>The sample buffer (none before the first).</returns>
    std::span<const uint8_t> LoadBettySample(int32_t bettyId);
    /// <summary>Reads the .snd file and opens the sound and Betty packet files.</summary>
    void Load(std::string_view soundFileName);
    /// <summary>PlayDigitalSample's work, with the channel at <paramref name="level"/> (0..127) times the bite's
    /// <see cref="SoundBiteGain"/>.</summary>
    int32_t PlaySample(uint32_t sampleId, MCOptionalRef<MCGameObject> source, bool atCamera, bool farRange,
                       uint8_t level);
    /// <summary>Clears the music state and, when given, sets <paramref name="state"/>.</summary>
    void SetMusicState(std::optional<MCMusicState> state);
    /// <summary>Whether the music state holds <paramref name="state"/>.</summary>
    bool HasMusicState(MCMusicState state) const { return _MusicState[std::to_underlying(state)]; }
    /// <summary>Puts a new resource of <paramref name="image"/> on <paramref name="channel"/>, freeing the old one.
    /// </summary>
    void SetChannelImage(int32_t channel, std::span<const uint8_t> image);
    /// <summary>Frees <paramref name="channel"/>'s resource (stopping it).</summary>
    void FreeChannelResource(int32_t channel);
    /// <summary>Steps one music stream's cross-fade.</summary>
    void UpdateStreamFade(int32_t stream);
    /// <summary>Drops a music stream that has stopped by itself, and forgets the music.</summary>
    void DropEndedStream(int32_t stream);
    /// <summary>Stops and frees a music stream's channel.</summary>
    void StopStream(int32_t stream);
    /// <summary>Update's radio part: the current message's fragments, then the next message.</summary>
    void UpdateRadio();
    /// <summary>Update's music choice from the combat state.</summary>
    void UpdateMusicChoice();
    /// <summary>Stops the rain bed and closes its loop.</summary>
    void StopRain();
    /// <summary>Starts a clap of thunder as loud as <paramref name="heard"/> at the nearest.</summary>
    void StartThunder(float heard);
    /// <summary>Stops the thunder rolling and closes it.</summary>
    void StopThunder();

    /// <summary>The renderer the channels belong to.</summary>
    MCSoundRenderer& _Renderer;
    /// <summary>The resource on each channel (the renderer owns them).</summary>
    std::array<std::weak_ptr<MCSoundResource>, NUM_SOUND_CHANNELS> _ChannelResource{};
    /// <summary>Set once the constructor has run; cleared by the destructor's purge (as SoundOn).</summary>
    bool _SoundOn = false;
    /// <summary>The sound bites' waves (&lt;name&gt;.pak).</summary>
    std::unique_ptr<MCPacketFile> _SoundDataFile;
    /// <summary>Betty's samples (betty.pak).</summary>
    std::unique_ptr<MCPacketFile> _BettyDataFile;
    /// <summary>The Betty sample loaded.</summary>
    std::vector<uint8_t> _BettySoundBite;
    /// <summary>The radio static wave (empty while not loaded).</summary>
    std::vector<uint8_t> _NoiseData;
    /// <summary>Every pilot's radio.</summary>
    std::vector<std::shared_ptr<MCRadio>> _Radios;
    /// <summary>The music state Update keeps (by <see cref="MCMusicState"/>).</summary>
    std::array<bool, 8> _MusicState{};
    /// <summary>The rain loop the rain bed reads (null while it plays none).</summary>
    std::unique_ptr<MCFlac> _RainLoop;
    /// <summary>The stream the rain bed plays through (null while it plays none).</summary>
    std::shared_ptr<MCAudioStream> _RainStream;
    /// <summary>The rain loop's PCM read last.</summary>
    std::vector<uint8_t> _RainChunk;
    /// <summary>How loud the rain bed plays (<see cref="RainLevel"/>).</summary>
    float _RainLevel = 0.0f;
    /// <summary>Set once the pack had no rain loop that plays: none is looked for again till the purge.</summary>
    bool _RainMissing = false;
    /// <summary>The thunder the clap rolling reads (null while none rolls).</summary>
    std::unique_ptr<MCFlac> _Thunder;
    /// <summary>The stream the clap rolling plays through (null while none rolls).</summary>
    std::shared_ptr<MCAudioStream> _ThunderStream;
    /// <summary>The thunder's PCM read last.</summary>
    std::vector<uint8_t> _ThunderChunk;
    /// <summary>Set once the clap rolling is all queued.</summary>
    bool _ThunderRead = false;
    /// <summary>How loud the clap rolling plays, as a channel volume before the effects volume.</summary>
    float _ThunderLevel = 0.0f;
    /// <summary>Seconds till the next clap (below 0: none due, as while no claps come).</summary>
    float _ThunderWait = -1.0f;
    /// <summary>The thunder's dice (its own: never the game's).</summary>
    std::minstd_rand _ThunderDice;
    /// <summary>Set once the pack had no thunder that plays: none is looked for again till the purge.</summary>
    bool _ThunderMissing = false;
};

/// <summary>The current context's sound system (null before UserInit makes it); the caller keeps it as its pin.</summary>
std::shared_ptr<MCSoundSystem> SoundSystem();
