#pragma once

class MCAudio;
class MCAudioStream;

/// <summary>
/// A Smacker (<c>.SMK</c>) video decoder, written from the public description of the format.
/// It replaces RAD's SMACKW32.DLL, which MCX.EXE called for its movies (the opening, the ending, the logos, the
/// radio videos in the mission panes and the pilot portraits).
/// </summary>
/// <remarks>
/// <para>The decoder is sequential, as Smacker is: frame n needs the pixels and the palette of frame n - 1.
/// <see cref="DecodeNextFrame"/> decodes one frame; its 8-bit pixels (<see cref="Pixels"/>, the stored
/// <see cref="Width"/> x <see cref="StoredHeight"/>), its 256-colour palette (<see cref="Palette"/>, 8 bits per
/// channel) and the PCM of each audio track (<see cref="Audio"/>) stay valid until the next call.</para>
/// <para>It has no SDL dependency and is safe to use from any one thread at a time. <see cref="MCSmackerPlayer"/>
/// adds what the game's Smack* calls did on top: timing, sound through <see cref="MCAudio"/>, palette remapping and
/// the copy into a pane.</para>
/// </remarks>
class MCSmacker
{
    /// <summary>Only <see cref="Open"/> and <see cref="OpenMemory"/> make one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>A movie with nothing read yet.</summary>
    explicit MCSmacker(Key);

    /// <summary>Smacker files carry up to seven audio tracks.</summary>
    static constexpr int MaxTracks = 7;

    /// <summary>Header flag: the file stores one more frame than it plays, a copy of the first, for looping.</summary>
    static constexpr uint32_t FlagRingFrame = 0x1;
    /// <summary>Header flag: every stored row is shown on every other screen row.</summary>
    static constexpr uint32_t FlagYInterlaced = 0x2;
    /// <summary>Header flag: every stored row is shown twice.</summary>
    static constexpr uint32_t FlagYDoubled = 0x4;

    /// <summary>What one audio track holds, from the header's AudioRate word.</summary>
    struct MCTrackInfo
    {
        /// <summary>Whether the track has data (bit 30).</summary>
        bool Present = false;
        /// <summary>Huffman/DPCM compressed (bit 31); otherwise raw PCM.</summary>
        bool Compressed = false;
        /// <summary>16-bit signed samples (bit 29); otherwise 8-bit unsigned.</summary>
        bool Is16Bit = false;
        /// <summary>Two interleaved channels (bit 28).</summary>
        bool Stereo = false;
        /// <summary>Bits 26-27: a non-zero value names a compression this decoder doesn't know (Bink audio).</summary>
        uint32_t Codec = 0;
        /// <summary>Samples per second per channel (bits 0-23).</summary>
        uint32_t Rate = 0;
        /// <summary>The largest decoded chunk of the track, in bytes, as the header gives it.</summary>
        uint32_t LargestChunk = 0;

        /// <summary>1 or 2.</summary>
        int Channels() const { return Stereo ? 2 : 1; }
        /// <summary>Bytes per sample frame (all channels).</summary>
        int BlockAlign() const { return Channels() * (Is16Bit ? 2 : 1); }
    };

    /// <summary>Opens a movie from disk; the file stays open and frames are read as they are decoded.</summary>
    /// <param name="path">The file (an absolute path, or one resolved through MCFileSystem by the caller).</param>
    /// <returns>The decoder positioned before frame 0, or why the file can't be played.</returns>
    static std::expected<std::unique_ptr<MCSmacker>, std::string> Open(const std::filesystem::path& path);

    /// <summary>Opens a movie held in memory.</summary>
    static std::expected<std::unique_ptr<MCSmacker>, std::string> OpenMemory(std::vector<uint8_t> data);

    ~MCSmacker();
    MCSmacker(const MCSmacker&) = delete;
    MCSmacker& operator=(const MCSmacker&) = delete;

    /// <summary>2 for <c>SMK2</c>, 4 for <c>SMK4</c>.</summary>
    int Version() const { return _Version; }

    /// <summary>Width in pixels.</summary>
    int Width() const { return static_cast<int>(_Width); }

    /// <summary>Height as stored, i.e. the rows <see cref="Pixels"/> holds.</summary>
    int StoredHeight() const { return static_cast<int>(_Height); }

    /// <summary>
    /// Height as shown: twice the stored height for a Y-doubled or Y-interlaced movie. This is what RAD's
    /// <c>SmackOpen</c> put in <c>Smack->Height</c> (SMACKW32 @ 0x10004224 doubles it), so it is the height the game
    /// sized its panes by.
    /// </summary>
    int Height() const { return static_cast<int>((_Flags & (FlagYInterlaced | FlagYDoubled)) ? _Height * 2 : _Height); }

    /// <summary>The frames the movie plays (without the ring frame).</summary>
    uint32_t FrameCount() const { return _Frames; }

    /// <summary>The header flags (<see cref="FlagRingFrame"/>, <see cref="FlagYInterlaced"/>, <see cref="FlagYDoubled"/>).</summary>
    uint32_t Flags() const { return _Flags; }

    /// <summary>How long each frame shows, in microseconds.</summary>
    uint32_t MicrosecondsPerFrame() const { return _MicrosecondsPerFrame; }

    /// <summary>Frames per second, from <see cref="MicrosecondsPerFrame"/>.</summary>
    double FramesPerSecond() const { return 1000000.0 / _MicrosecondsPerFrame; }

    /// <summary>One audio track's format.</summary>
    const MCTrackInfo& Track(int track) const { return _Tracks[static_cast<size_t>(track)]; }

    /// <summary>
    /// Decodes the next frame: its palette record, its audio chunks and its video. After the last frame the movie
    /// starts again from frame 0 (a ring frame, when there is one, is decoded as the frame after the last).
    /// </summary>
    /// <returns>An error for a corrupt frame; the pixels are then as far as the decode got.</returns>
    std::expected<void, std::string> DecodeNextFrame();

    /// <summary>Goes back to before frame 0: black palette, zero pixels.</summary>
    void Rewind();

    /// <summary>Decodes forward (from the start if needed) until <paramref name="frame"/> is the current frame.</summary>
    std::expected<void, std::string> SeekFrame(uint32_t frame);

    /// <summary>The frame last decoded, or -1 before the first.</summary>
    int32_t CurrentFrame() const { return _Current; }

    /// <summary>Whether the frame last decoded is a key frame (the frame-size table's bit 0).</summary>
    bool IsKeyFrame() const;

    /// <summary>The pixels of the current frame: <see cref="Width"/> x <see cref="StoredHeight"/> palette indices.</summary>
    std::span<const uint8_t> Pixels() const { return _Pixels; }

    /// <summary>The palette of the current frame: 256 x (r, g, b), 8 bits per channel.</summary>
    const std::array<uint8_t, 768>& Palette() const { return _Palette; }

    /// <summary>Whether the current frame carried a palette record (RAD's <c>Smack->NewPalette</c>).</summary>
    bool PaletteChanged() const { return _PaletteChanged; }

    /// <summary>
    /// The PCM a track carried in the current frame, in the track's own format (8-bit unsigned or 16-bit signed
    /// little-endian, interleaved when stereo). Empty when the frame has none.
    /// </summary>
    std::span<const uint8_t> Audio(int track) const { return _Audio[static_cast<size_t>(track)]; }

    /// <summary>
    /// Writes the current frame into an 8-bit buffer as it is shown: rows doubled for a Y-doubled movie, every other
    /// row for a Y-interlaced one (the rows in between are left as they are, or filled with
    /// <paramref name="gapColor"/>), and each pixel through <paramref name="remap"/> when one is given.
    /// </summary>
    /// <param name="dest">The destination from the top-left pixel the frame goes to.</param>
    /// <param name="pitch">Bytes from one destination row to the next.</param>
    /// <param name="maxWidth">Columns available at <paramref name="dest"/>; the frame is clipped to them.</param>
    /// <param name="maxHeight">Rows available at <paramref name="dest"/>; the frame is clipped to them.</param>
    /// <param name="remap">256 indices, or empty.</param>
    /// <param name="gapColor">The colour of an interlaced movie's rows in between, or -1 to leave them.</param>
    void CopyTo(std::span<uint8_t> dest, int pitch, int maxWidth, int maxHeight, std::span<const uint8_t> remap = {},
                int gapColor = -1) const;

    /// <summary>The 6-bit to 8-bit palette scale Smacker uses: <c>(v &lt;&lt; 2) | (v &gt;&gt; 4)</c>.</summary>
    static uint8_t ScalePaletteComponent(uint8_t sixBit)
    {
        return static_cast<uint8_t>(((sixBit & 0x3f) << 2) | ((sixBit & 0x3f) >> 4));
    }

    /// <summary>The run lengths a block-type code's bits 2-7 select.</summary>
    static uint32_t BlockRun(uint32_t index);

    /// <summary>
    /// Builds the table SmackColorRemap uses: for each movie colour the nearest colour of <paramref name="target"/>.
    /// </summary>
    /// <param name="source">256 x (r, g, b), the movie's palette.</param>
    /// <param name="target">The palette to map onto, (r, g, b) per entry, 8 bits per channel: 1-256 entries.</param>
    /// <param name="remap">256 entries written.</param>
    static void BuildRemap(std::span<const uint8_t, 768> source, std::span<const uint8_t> target,
                           std::span<uint8_t, 256> remap);

    class MCBitReader;
    class MCHuffman;

private:
    /// <summary>Reads and checks the header, the frame tables and the trees.</summary>
    std::expected<void, std::string> ReadHeader();

    /// <summary>Fills <paramref name="dest"/> from <paramref name="offset"/> of the file.</summary>
    bool ReadAt(uint64_t offset, std::span<uint8_t> dest);

    std::expected<void, std::string> DecodePalette(std::span<const uint8_t> chunk);
    std::expected<void, std::string> DecodeAudio(int track, std::span<const uint8_t> chunk);
    std::expected<void, std::string> DecodeVideo(std::span<const uint8_t> chunk);

    std::ifstream _File;
    std::vector<uint8_t> _Memory;
    bool _InMemory = false;
    uint64_t _FileSize = 0;

    int _Version = 2;
    uint32_t _Width = 0;
    uint32_t _Height = 0;
    uint32_t _Frames = 0;
    /// <summary>Frames stored: <see cref="_Frames"/>, plus one with a ring frame.</summary>
    uint32_t _StoredFrames = 0;
    uint32_t _Flags = 0;
    uint32_t _MicrosecondsPerFrame = 0;
    std::array<MCTrackInfo, MaxTracks> _Tracks{};

    std::vector<uint32_t> _FrameSizes;
    std::vector<uint8_t> _FrameTypes;
    std::vector<uint64_t> _FrameOffsets;

    std::unique_ptr<MCHuffman> _MMap;
    std::unique_ptr<MCHuffman> _MClr;
    std::unique_ptr<MCHuffman> _Full;
    std::unique_ptr<MCHuffman> _Type;

    int32_t _Current = -1;
    /// <summary>The stored frame decoded next.</summary>
    uint32_t _Next = 0;
    std::vector<uint8_t> _Pixels;
    std::array<uint8_t, 768> _Palette{};
    bool _PaletteChanged = false;
    std::array<std::vector<uint8_t>, MaxTracks> _Audio;
    std::vector<uint8_t> _FrameData;
};

/// <summary>
/// A movie as the game plays it: the <see cref="MCSmacker"/> decoder plus RAD's playback services, one method per
/// Smack* call MCX.EXE made (<c>SmackOpen</c>, <c>SmackDoFrame</c>, <c>SmackNextFrame</c>, <c>SmackWait</c>,
/// <c>SmackToBuffer</c>/<c>SmackToBufferRect</c>, <c>SmackColorRemap</c>, <c>SmackClose</c>).
/// </summary>
/// <remarks>
/// <para>The game's loop was: <c>SmackDoFrame</c> (decode the current frame into the buffer and queue its sound),
/// then <c>SmackNextFrame</c> (advance), and <c>SmackWait</c> each tick to know whether the next frame is due. The
/// buffer is a parameter here: <see cref="DoFrame"/> decodes, and <see cref="CopyFrame"/> copies the frame into memory
/// the caller holds for the call
/// (see <c>aSmackerWindow::display</c> @ 0x0061b690 and <c>0x0061b8d0</c>). The fields the game read from the
/// <c>SmackTag</c> are methods here: <c>+0x4 Width</c>, <c>+0x8 Height</c> (shown height), <c>+0xc Frames</c>,
/// <c>+0x68 NewPalette</c>, <c>+0x6c Palette</c>, <c>+0x374 FrameNum</c>.</para>
/// <para>Sound: with an <see cref="MCAudio"/>, the first present track (RAD played track 0) is queued on an
/// <see cref="MCAudioStream"/> as each frame is done. The frame clock starts at the first <see cref="DoFrame"/>.</para>
/// </remarks>
class MCSmackerPlayer
{
    /// <summary>Only <see cref="Open"/> makes one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>A player with no movie yet.</summary>
    explicit MCSmackerPlayer(Key) {}

    /// <summary>Opens a movie (<c>SmackOpen</c>).</summary>
    /// <param name="path">The file on disk.</param>
    /// <param name="audio">Where the sound goes; null plays the movie silent.</param>
    static std::expected<std::unique_ptr<MCSmackerPlayer>, std::string> Open(const std::filesystem::path& path,
                                                                             std::shared_ptr<MCAudio> audio);

    /// <summary>Stops the sound (<c>SmackClose</c>).</summary>
    ~MCSmackerPlayer();

    /// <summary>The decoder.</summary>
    MCSmacker& Decoder() { return *_Smacker; }

    /// <summary><c>Smack->Width</c>.</summary>
    int Width() const { return _Smacker->Width(); }
    /// <summary><c>Smack->Height</c>: the height as shown (doubled for interlaced/doubled movies).</summary>
    int Height() const { return _Smacker->Height(); }
    /// <summary><c>Smack->Frames</c>.</summary>
    uint32_t Frames() const { return _Smacker->FrameCount(); }
    /// <summary><c>Smack->FrameNum</c>: the frame <see cref="DoFrame"/> decodes next (0-based).</summary>
    uint32_t FrameNum() const { return _FrameNum; }
    /// <summary><c>Smack->NewPalette</c>: whether the frame done last changed the palette.</summary>
    bool NewPalette() const { return _Smacker->PaletteChanged(); }
    /// <summary><c>Smack->Palette</c>: 256 x (r, g, b), 8 bits per channel.</summary>
    const std::array<uint8_t, 768>& Palette() const { return _Smacker->Palette(); }

    /// <summary>
    /// Copies the frame <see cref="DoFrame"/> decoded last into <paramref name="buffer"/> (what <c>SmackToBuffer</c>
    /// set up and <c>SmackDoFrame</c> did): at <paramref name="left"/>, <paramref name="top"/> of a buffer of
    /// <paramref name="pitch"/> bytes per row, writing at most <paramref name="height"/> rows from
    /// <paramref name="top"/> down, through the colour remap.
    /// </summary>
    /// <remarks>MCX.EXE passed the movie's own height as RAD's <c>destheight</c> (aSmackerWindow::display
    /// @ 0x0061b690), so it counts rows from <paramref name="top"/>, not rows of the whole buffer. Every pixel of the
    /// frame's rectangle is written: an interlaced movie's rows in between get 0, which RAD left as they were (the
    /// game wiped them to 0 first), because the buffer may be fresh upload memory
    /// (<see cref="MCRenderer::LockTexture"/>).</remarks>
    void CopyFrame(std::span<uint8_t> buffer, int left, int top, int pitch, int height) const;

    /// <summary>
    /// Remaps the movie onto a fixed palette (<c>SmackColorRemap</c>): from now on every pixel written is the index
    /// of the nearest colour in <paramref name="palette"/>, recomputed when the movie's palette changes.
    /// </summary>
    /// <param name="palette">(r, g, b) per entry, 8 bits per channel, 1-256 entries; empty turns remapping off.</param>
    void ColorRemap(std::span<const uint8_t> palette);

    /// <summary>
    /// Decodes the frame <see cref="FrameNum"/> names (<c>SmackDoFrame</c>) and queues its sound; copy it with
    /// <see cref="CopyFrame"/>. The first call starts the clock.
    /// </summary>
    std::expected<void, std::string> DoFrame();

    /// <summary>Moves to the next frame (<c>SmackNextFrame</c>); after the last it wraps to 0 as RAD did.</summary>
    void NextFrame();

    /// <summary>
    /// Whether the caller should still wait before doing the next frame (<c>SmackWait</c>): true until
    /// the next frame's time has come. With sound, the clock follows the sound actually played.
    /// </summary>
    bool Wait();

    /// <summary>The time the next frame is due, in microseconds from the first <see cref="DoFrame"/>.</summary>
    uint64_t NextFrameTime() const;

private:
    uint64_t ElapsedMicroseconds() const;

    std::unique_ptr<MCSmacker> _Smacker;
    std::shared_ptr<MCAudioStream> _Stream;
    int _AudioTrack = -1;
    uint32_t _FrameNum = 0;
    /// <summary>Frames done since the clock started.</summary>
    uint64_t _FramesDone = 0;
    uint64_t _StartTicks = 0;
    bool _Started = false;

    std::vector<uint8_t> _RemapPalette;
    std::array<uint8_t, 256> _Remap{};
    bool _RemapValid = false;
};

/// <summary>
/// The original's Smacker handle (<c>Smack*</c>, which the game's headers name <c>SmackTag</c>). The port's is a box
/// around the player; the game only passes it around, and closing it (<c>SmackClose</c>) is destroying it.
/// </summary>
struct MCSmackTag
{
    /// <summary>The open movie.</summary>
    std::unique_ptr<MCSmackerPlayer> Player;
    /// <summary>Port-only: the game path it was opened by (the HD art pack's frames are found by its name).</summary>
    std::string FileName;
};

/// <summary>Opens a movie of the game's data (<c>SmackOpen</c>); its sound goes where
/// <see cref="SmackSoundUseDirectSound"/> said.</summary>
/// <param name="fileName">The game path.</param>
/// <param name="flags">SMACK* flags; the port's player needs none.</param>
/// <param name="extraBuffers">Not used.</param>
/// <returns>The movie, or null when it can't be opened.</returns>
std::unique_ptr<MCSmackTag> SmackOpen(std::string_view fileName, uint32_t flags, int32_t extraBuffers);

/// <summary>
/// Sends movie sound to <paramref name="audio"/> (<c>SmackSoundUseDirectSound</c>) while it lives; empty plays movies
/// silent.
/// </summary>
void SmackSoundUseDirectSound(std::weak_ptr<MCAudio> audio);
