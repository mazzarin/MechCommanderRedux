#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Port-only (HD art): the room the HD art frames may take, in memory (MCHdArt's frames kept) and on the GPU (the
// renderer's HD pictures), by what the machine says it can spare. Each side keeps up to MostHdBytes; below that there's
// no share of the memory or of the card, only what the system reports. On the card: the room the system gives the game
// there, less what the game holds there, plus what the HD pictures hold, asked again every CardReadInterval. In memory:
// the frames grow while the system isn't short of memory, and while it is they're let go of, TrimmedBytesPerFrame a
// frame, until it stops saying so. A card that shares the machine's memory (Apple silicon, integrated graphics) follows
// both: the memory short trims the pictures too, and the card's room falling below what they hold trims the frames.
// Both are what may be kept, never memory set aside. The platform's answers come from an MCSystem the app sets
// (MCMemorySystemMac.mm, MCMemorySystemWin.cpp); with none, nothing is short and each side keeps up to MostHdBytes.

namespace MCMemoryRoom
{
    /// <summary>The most bytes of HD frames each side keeps, in memory and on the GPU.</summary>
    inline constexpr uint64_t MostHdBytes = uint64_t{4} << 30;

    /// <summary>
    /// The bytes let go of in a frame past the room (or while memory is short): a frame stops once this many have gone,
    /// the rest going in the frames after, so no frame drops a pile at once.
    /// </summary>
    inline constexpr size_t TrimmedBytesPerFrame = size_t{64} << 20;

    /// <summary>How often the card is asked again, as other programs take memory there or give it back.</summary>
    inline constexpr std::chrono::seconds CardReadInterval{2};

    /// <summary>What the system says of the card the game draws on.</summary>
    struct MCCard
    {
        /// <summary>The bytes the system gives the game on the card, and those the game holds there now.</summary>
        uint64_t Budget = 0;
        uint64_t Used = 0;
        /// <summary>Whether the card's memory is the machine's (Apple silicon, integrated graphics).</summary>
        bool SharesMemory = false;
    };

    /// <summary>The platform's answers (the tests' stand-ins too). Asked on the game's thread.</summary>
    class MCSystem
    {
    public:
        virtual ~MCSystem() = default;

        /// <summary>Whether the system says it's short of memory now (asked every frame).</summary>
        virtual bool Short() = 0;

        /// <summary>
        /// The card SDL's GPU device named <paramref name="deviceName"/> (SDL_PROP_GPU_DEVICE_NAME_STRING) runs on; none
        /// when the system can't say.
        /// </summary>
        virtual std::optional<MCCard> Card(std::string_view deviceName) = 0;
    };

    /// <summary>
    /// Sets the system asked (none: nothing is short and each side keeps up to MostHdBytes); every reader asks the card
    /// again at its next frame.
    /// </summary>
    void SetSystem(std::shared_ptr<MCSystem> system);

    /// <summary>Has every reader ask the card again at its next frame (a new system set; the tests).</summary>
    void AskAgain();

    /// <summary>
    /// The room the HD pictures may take on a card of which the game holds <paramref name="card"/>'s Used, the pictures
    /// <paramref name="held"/> of it: what the card gives the game less what the rest of the game holds, at most
    /// MostHdBytes (0 when the rest takes it all).
    /// </summary>
    uint64_t CardRoom(const MCCard& card, uint64_t held);

    /// <summary>What a frame's trims follow (<see cref="MCReader::Frame"/>).</summary>
    struct MCFrameRoom
    {
        /// <summary>Whether the frames kept in memory are let go of this frame (MCHdArt::NextFrame).</summary>
        bool MemoryShort = false;
        /// <summary>The bytes the HD pictures on the GPU may take this frame (0 while trimmed for memory).</summary>
        uint64_t PictureBytes = MostHdBytes;
    };

    /// <summary>
    /// The renderer's reader of the system: asks whether memory is short every frame, and the card SDL's device runs
    /// on every CardReadInterval (as the pictures hold then), and says what the frame's trims follow.
    /// </summary>
    class MCReader
    {
    public:
        using MCClock = std::function<std::chrono::steady_clock::time_point()>;

        /// <summary>
        /// A reader of the card SDL's device named <paramref name="deviceName"/> runs on, timed by <paramref name="now"/>.
        /// </summary>
        explicit MCReader(std::string deviceName, MCClock now = std::chrono::steady_clock::now);

        /// <summary>The frame's room, the HD pictures holding <paramref name="held"/> bytes on the card.</summary>
        MCFrameRoom Frame(uint64_t held);

        /// <summary>The card as last read (none: never, or the system couldn't say).</summary>
        const std::optional<MCCard>& Card() const { return _Card; }

    private:
        std::string _DeviceName;
        MCClock _Now;
        /// <summary>When the card is asked next, and the generation of AskAgain it was last asked under.</summary>
        std::optional<std::chrono::steady_clock::time_point> _NextRead;
        uint64_t _Asked = 0;
        std::optional<MCCard> _Card;
        /// <summary>
        /// The pictures' room as of the card's last reading, and whether the card said the game holds more there than
        /// it gives it (its budget fell, or the game's use rose past it).
        /// </summary>
        uint64_t _Room = MostHdBytes;
        bool _CardShort = false;
    };
}
