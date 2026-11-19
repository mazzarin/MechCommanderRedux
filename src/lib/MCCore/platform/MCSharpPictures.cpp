#include "stdafx.h"
#include "platform/MCSharpPictures.h"
#include "lib/MCFastFile.h"
#include "lib/MCFnvHash.h"
#include "lib/MCInflate.h"
#include "lib/MCWorkers.h"
#include "platform/MCFileSystem.h"
#include "platform/MCShapeRows.h"
#include "platform/MCSoftwareRenderer.h"
#include "sprite/MCHdArt.h"
#include "vfx/MCVfxFunctions.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace
{
    /// <summary>What a picture is redrawn from: its key's space.</summary>
    enum class Kind : uint8_t
    {
        Glyph = 1,
        Lines,
    };

    /// <summary>
    /// A VFX font's glyph count and its glyph offsets' table (vfx/MCVfxFunctions.h's fonts); a glyph's pixels follow its
    /// width.
    /// </summary>
    constexpr size_t FontCountAt = 4;
    constexpr size_t FontTableAt = 0x10;
    constexpr size_t FontOffsetBytes = 4;
    constexpr size_t GlyphPixelsAt = 4;
    /// <summary>The characters of a font made ahead: the printable ones, space to tilde (the game's text).</summary>
    constexpr int32_t FirstMadeCharacter = 0x20;
    constexpr int32_t LastMadeCharacter = 0x7e;
    /// <summary>An RGBA8 pixel's bytes, and those of one kept as its red and green.</summary>
    constexpr size_t RgbaPixelBytes = 4;
    constexpr size_t RedGreenPixelBytes = 2;
    /// <summary>The jobs the making thread hands out at once (each waits for the slowest of them).</summary>
    constexpr size_t BatchJobs = 32;
    /// <summary>The most helpers the making thread has (MCWorkers::HelpersFor: half the machine's threads, less one).</summary>
    constexpr size_t MostHelpers = 7;
    /// <summary>
    /// The extension of a picture in the pack's sharp.hda (MCHdArt::SharpArchive): named by its key, 16 hex digits, and
    /// holding its pixels' red and green bytes.
    /// </summary>
    constexpr std::string_view PackedExtension = ".rg8";
    /// <summary>
    /// The most bytes of the pack's sharp.hda read (its pictures as stored: about 3 MB, 14 Nov); past it the pack is
    /// left out, as a malformed one.
    /// </summary>
    constexpr size_t MostPackBytes = size_t{64} * 1024 * 1024;

    /// <summary>The bytes of a picture redrawn from <paramref name="columns"/> x <paramref name="rows"/> pixels, as kept.</summary>
    size_t RedGreenBytes(int32_t columns, int32_t rows)
    {
        constexpr auto Scale = static_cast<size_t>(MCSharpText::Scale);
        return static_cast<size_t>(std::max(columns, 0)) * static_cast<size_t>(std::max(rows, 0)) * Scale * Scale *
               RedGreenPixelBytes;
    }

    /// <summary>The key of what's redrawn from <paramref name="pixels"/>: the redraws' version, its kind, size and bytes.</summary>
    uint64_t PixelsKey(Kind kind, std::span<const uint8_t> pixels, int32_t pitch, int32_t columns, int32_t rows)
    {
        uint64_t key = MCFnvHash::Basis;
        MCFnvHash::Mix(key, MCSharpPictures::RedrawVersion);
        MCFnvHash::Mix(key, static_cast<uint64_t>(kind));
        MCFnvHash::Mix(key, static_cast<uint64_t>(columns));
        MCFnvHash::Mix(key, static_cast<uint64_t>(rows));

        for (int32_t row = 0; row < rows; ++row)
        {
            for (int32_t column = 0; column < columns; ++column)
            {
                const size_t offset =
                    static_cast<size_t>(row) * static_cast<size_t>(pitch) + static_cast<size_t>(column);
                MCFnvHash::Mix(key, offset < pixels.size() ? pixels[offset] : MCSharpText::NoInk);
            }
        }

        return key;
    }

    /// <summary>A font to make the glyphs of: its bytes.</summary>
    struct FontJob
    {
        std::shared_ptr<const std::vector<uint8_t>> Font;
    };

    /// <summary>A glyph to make: its font's bytes and layers, and its character.</summary>
    struct GlyphJob
    {
        std::shared_ptr<const std::vector<uint8_t>> Font;
        std::shared_ptr<const MCSharpText::MCFontLayers> Layers;
        int32_t Character = 0;
    };

    /// <summary>
    /// Line art to make: its bytes, size, parts drawn by hand and key, and the bytes it's expected to be kept in
    /// (counted against MostLineArtBytes from the time it's asked for).
    /// </summary>
    struct LinesJob
    {
        std::vector<uint8_t> Art;
        int32_t Columns = 0;
        int32_t Rows = 0;
        std::vector<MCSharpLines::MCDrawnLoop> Drawn;
        uint64_t Key = 0;
        size_t Bytes = 0;
    };

    using Job = std::variant<FontJob, GlyphJob, LinesJob>;

    /// <summary>
    /// The pack's sharp.hda read whole: its directory (MCFastFile::Find; its file closed once read) and its pictures as
    /// stored, from <see cref="StoredAt"/> in the file on: about 3 MB (14 Nov), so that the helpers and a late draw
    /// take them without reading the disk.
    /// </summary>
    struct Packed
    {
        std::unique_ptr<MCFastFile> Archive;
        std::vector<uint8_t> Stored;
        size_t StoredAt = 0;
    };

    /// <summary>The archive at <paramref name="path"/> read whole; null (logged) when it can't be.</summary>
    std::shared_ptr<const Packed> ReadPack(const std::filesystem::path& path)
    {
        const std::string name = MCFileSystem::ToUtf8(path);
        std::expected<std::unique_ptr<MCFastFile>, std::string> archive = MCFastFile::Create(name);

        if (!archive)
        {
            SDL_Log("MCSharpPictures: the pack's %s can't be opened: %s (its pictures are made)", name.c_str(),
                    archive.error().c_str());
            return nullptr;
        }

        // Where its entries' stored bytes lie, checked before they're read.
        auto packed = std::make_shared<Packed>();
        size_t first = std::numeric_limits<size_t>::max();
        size_t end = 0;

        for (const MCFileEntry& entry : (*archive)->GetEntries())
        {
            if (entry.Offset < 0 || entry.Size < 0 || entry.RealSize < 0)
            {
                SDL_Log("MCSharpPictures: the pack's %s has an entry that can't be (%.*s; its pictures are made)",
                        name.c_str(), static_cast<int>(entry.GetName().size()), entry.GetName().data());
                return nullptr;
            }

            first = std::min(first, static_cast<size_t>(entry.Offset));
            end = std::max(end, static_cast<size_t>(entry.Offset) + static_cast<size_t>(entry.Size));
        }

        if (end > first && end - first > MostPackBytes)
        {
            SDL_Log("MCSharpPictures: the pack's %s holds %zu bytes, more than %zu (its pictures are made)",
                    name.c_str(), end - first, MostPackBytes);
            return nullptr;
        }

        if (end > first)
        {
            std::optional<MCDiskFile> file = (*archive)->OpenAnother();
            packed->Stored.resize(end - first);
            packed->StoredAt = first;

            if (!file.has_value() ||
                file->ReadAt(static_cast<uint32_t>(first), packed->Stored) != packed->Stored.size())
            {
                SDL_Log("MCSharpPictures: the pack's %s can't be read (its pictures are made)", name.c_str());
                return nullptr;
            }
        }

        (*archive)->Release();
        packed->Archive = std::move(*archive);
        return packed;
    }

    /// <summary>
    /// The picture under <paramref name="key"/> in <paramref name="pack"/>, <paramref name="bytes"/> its red and green
    /// bytes; none when the pack has no such picture, or one of another size or that doesn't unpack (logged).
    /// </summary>
    std::optional<MCSharpPictures::MCMadePicture> FromPack(const Packed& pack, uint64_t key, size_t bytes)
    {
        const std::optional<int32_t> index = pack.Archive->Find(std::format("{:016x}{}", key, PackedExtension));

        if (!index.has_value())
        {
            return std::nullopt;
        }

        const MCFileEntry& entry = pack.Archive->GetEntries()[static_cast<size_t>(*index)];

        if (static_cast<size_t>(entry.RealSize) != bytes)
        {
            return std::nullopt;
        }

        const std::span<const uint8_t> stored =
            std::span(pack.Stored)
                .subspan(static_cast<size_t>(entry.Offset) - pack.StoredAt, static_cast<size_t>(entry.Size));

        if (entry.Size == entry.RealSize)
        {
            return MCSharpPictures::MCMadePicture::OfRedGreen({stored.begin(), stored.end()});
        }

        std::expected<std::vector<uint8_t>, std::string> inflated = MCInflate::Inflate(stored, bytes);

        if (!inflated)
        {
            SDL_Log("MCSharpPictures: the pack's picture %016llx doesn't unpack: %s (it's made)",
                    static_cast<unsigned long long>(key), inflated.error().c_str());
            return std::nullopt;
        }

        return MCSharpPictures::MCMadePicture::OfRedGreen(std::move(*inflated));
    }

    /// <summary>A picture made, and the bytes it counts against MostLineArtBytes (0: a glyph's).</summary>
    struct Made
    {
        MCSharpPictures::MCMadePicture Picture;
        size_t LineArtBytes = 0;
    };

    /// <summary>
    /// What the game's thread and the making thread share, under Lock. A key is in at most one of Asked (line art asked
    /// for, not begun), Making (being made), Done (made, not taken) and Known (the renderer has it, or makes it itself):
    /// a job leaves alone the keys in any of the last three.
    /// </summary>
    struct State
    {
        std::mutex Lock;
        /// <summary>Told of each job asked for, picture made and batch done.</summary>
        std::condition_variable_any Changed;
        std::deque<Job> Queue;
        std::unordered_set<uint64_t> Asked;
        std::unordered_set<uint64_t> Making;
        std::unordered_map<uint64_t, Made> Done;
        std::unordered_set<uint64_t> Known;
        /// <summary>The bytes of line art asked for, being made or made and not taken.</summary>
        size_t LineArtBytes = 0;
        /// <summary>Counts ForgetLineArt's calls: line art a job made before the last is dropped.</summary>
        uint64_t LineArtGeneration = 0;
        /// <summary>Set while the making thread works on a batch.</summary>
        bool Working = false;
        /// <summary>
        /// The pack's sharp.hda as last asked (MCHdArt::SharpArchive, as a job is queued), and the one read (Pack; by
        /// the making thread, before the batch after it changed).
        /// </summary>
        std::optional<std::filesystem::path> PackPath;
        std::optional<std::filesystem::path> PackReadFrom;
        std::shared_ptr<const Packed> Pack;
        MCSharpPictures::MCMadeTally Tally;
        /// <summary>The game's thread's priority as the making thread started, a step below which it runs.</summary>
        MCThreadPriority GamePriority;
        /// <summary>The making thread, started with the first job; last, so it stops before the rest goes.</summary>
        std::jthread Maker;
    };

    State& TheState()
    {
        static State state;
        return state;
    }

    /// <summary>A job's pictures' keys, the bytes each is kept in and how to make them (empty: nothing to make).</summary>
    struct Plan
    {
        std::vector<uint64_t> Keys;
        size_t PictureBytes = 0;
        std::function<std::vector<std::vector<uint8_t>>()> Make;
        size_t LineArtBytes = 0;
    };

    /// <summary>What <paramref name="job"/> (a glyph's or line art's) makes; worked out on a helper.</summary>
    Plan PlanOf(const Job& job)
    {
        Plan plan;

        if (const auto* glyphJob = std::get_if<GlyphJob>(&job))
        {
            const std::optional<MCSharpPictures::MCFontGlyph> glyph =
                MCSharpPictures::FontGlyph(*glyphJob->Font, glyphJob->Character);

            if (!glyph.has_value() || glyph->Pitch <= 0 || glyph->Height <= 0)
            {
                return plan;
            }

            MCSharpPictures::MCGlyphPictures pictures =
                MCSharpPictures::GlyphPictures(glyph->Pixels, glyph->Pitch, glyph->Height, *glyphJob->Layers);

            for (size_t picture = 0; picture < pictures.Count(); ++picture)
            {
                plan.Keys.push_back(pictures.KeyOf(picture));
            }

            plan.PictureBytes = RedGreenBytes(glyph->Pitch, glyph->Height);
            plan.Make = [glyph = *glyph, pictures = std::move(pictures)]
            { return MCSharpPictures::RedrawGlyph(glyph.Pixels, glyph.Pitch, glyph.Height, pictures); };
        }
        else if (const auto* lines = std::get_if<LinesJob>(&job))
        {
            plan.Keys.push_back(lines->Key);
            plan.PictureBytes = RedGreenBytes(lines->Columns, lines->Rows);
            plan.LineArtBytes = lines->Bytes;
            plan.Make = [lines]
            {
                return std::vector<std::vector<uint8_t>>{
                    MCSharpLines::Redraw(lines->Art, lines->Columns, lines->Columns, lines->Rows, lines->Drawn)};
            };
        }

        return plan;
    }

    /// <summary>
    /// Runs <paramref name="job"/> on a helper: claims the keys of its pictures no one has (Making), takes them from
    /// <paramref name="pack"/> (none: none there) or makes them, and keeps them (Done) unless the renderer has taken to
    /// making them meanwhile (Known) or, line art, the line art made before <paramref name="lineArtGeneration"/> was
    /// forgotten. A job that fails is logged; its pictures are left to the renderer.
    /// </summary>
    void RunJob(const Job& job, uint64_t lineArtGeneration, const std::shared_ptr<const Packed>& pack,
                const std::stop_token& stop)
    {
        State& state = TheState();
        const Plan plan = PlanOf(job);
        std::vector<uint64_t> claimed;
        const auto current = [&] { return plan.LineArtBytes == 0 || state.LineArtGeneration == lineArtGeneration; };

        {
            std::scoped_lock lock(state.Lock);

            for (const uint64_t key : plan.Keys)
            {
                state.Asked.erase(key);

                if (!state.Known.contains(key) && !state.Done.contains(key) && !state.Making.contains(key) &&
                    current() && !stop.stop_requested())
                {
                    claimed.push_back(key);
                    state.Making.insert(key);
                }
            }

            if (claimed.empty())
            {
                state.LineArtBytes -= std::min(state.LineArtBytes, plan.LineArtBytes);
                state.Changed.notify_all();
                return;
            }
        }

        // The pack's pictures, then (any missing) the job's redrawn for the rest: a glyph's layers are redrawn together.
        std::vector<std::optional<MCSharpPictures::MCMadePicture>> made(plan.Keys.size());
        size_t fromPack = 0;

        try
        {
            for (size_t picture = 0; pack != nullptr && picture < plan.Keys.size(); ++picture)
            {
                if (std::ranges::contains(claimed, plan.Keys[picture]))
                {
                    made[picture] = FromPack(*pack, plan.Keys[picture], plan.PictureBytes);
                    fromPack += made[picture].has_value() ? 1 : 0;
                }
            }

            if (fromPack < claimed.size())
            {
                std::vector<std::vector<uint8_t>> redrawn = plan.Make();

                for (size_t picture = 0; picture < std::min(made.size(), redrawn.size()); ++picture)
                {
                    if (!made[picture].has_value() && !redrawn[picture].empty())
                    {
                        made[picture].emplace(std::move(redrawn[picture]));
                    }
                }
            }
        }
        catch (const std::exception& error)
        {
            SDL_Log("MCSharpPictures: making a picture ahead (key %016llx) failed: %s",
                    static_cast<unsigned long long>(claimed.front()), error.what());
        }
        catch (...)
        {
            SDL_Log("MCSharpPictures: making a picture ahead (key %016llx) failed: not a std::exception",
                    static_cast<unsigned long long>(claimed.front()));
        }

        std::scoped_lock lock(state.Lock);
        const bool kept = current();
        bool any = false;
        state.Tally.FromPack += fromPack;

        for (const uint64_t key : claimed)
        {
            state.Making.erase(key);
            const auto picture = static_cast<size_t>(std::ranges::find(plan.Keys, key) - plan.Keys.begin());

            if (!kept || state.Known.contains(key) || picture >= made.size() || !made[picture].has_value())
            {
                continue;
            }

            MCSharpPictures::MCMadePicture done(std::move(*made[picture]));
            MCSharpPictures::MCMadeTally& tally = state.Tally;
            tally.KeptBytes += done.KeptBytes();
            tally.RgbaBytes += done.RgbaBytes();
            ++tally.Held;
            tally.HeldBytes += done.KeptBytes();
            ++(plan.LineArtBytes == 0 ? tally.GlyphPictures : tally.Drawings);
            any = true;
            state.Done.emplace(key, Made{std::move(done), plan.LineArtBytes});
        }

        if (any && plan.LineArtBytes == 0)
        {
            ++state.Tally.Glyphs;
        }

        // Line art not kept no longer counts against MostLineArtBytes; kept, it counts until taken (or forgotten).
        if (!any && kept)
        {
            state.LineArtBytes -= std::min(state.LineArtBytes, plan.LineArtBytes);
        }

        state.Changed.notify_all();
    }

    /// <summary>Jobs for <paramref name="font"/>'s printable glyphs, its layers worked out once for them all.</summary>
    std::vector<Job> GlyphJobs(const FontJob& font)
    {
        const auto layers = std::make_shared<const MCSharpText::MCFontLayers>(MCSharpText::LayersOf(*font.Font));
        const int32_t count = MCSharpPictures::FontGlyphCount(*font.Font);
        std::vector<Job> jobs;

        for (int32_t character = FirstMadeCharacter; character <= std::min(LastMadeCharacter, count - 1); ++character)
        {
            jobs.emplace_back(GlyphJob{font.Font, layers, character});
        }

        return jobs;
    }

    /// <summary>
    /// The making thread: takes the jobs in turn, a font's as a job a glyph, and runs them in batches on its helpers
    /// and itself (MCWorkers), until asked to stop.
    /// </summary>
    void MakePictures(std::stop_token stop)
    {
        // Ahead of the screens that show them: a step below the game's thread, as nothing waits for them yet (set before
        // this thread started).
        State& state = TheState();
        MCWorkers::LowerThisThread(state.GamePriority);
        MCWorkers helpers(MCWorkers::HelpersFor(MostHelpers), MCWorkers::Priority::Low, state.GamePriority);
        std::unique_lock lock(state.Lock);

        while (state.Changed.wait(lock, stop, [&state] { return !state.Queue.empty(); }))
        {
            state.Working = true;
            std::vector<Job> batch;

            while (!state.Queue.empty() && batch.size() < BatchJobs)
            {
                Job job = std::move(state.Queue.front());
                state.Queue.pop_front();

                // A font's glyphs go next, in order.
                if (const auto* font = std::get_if<FontJob>(&job))
                {
                    lock.unlock();
                    std::vector<Job> glyphs = GlyphJobs(*font);
                    lock.lock();
                    state.Queue.insert(state.Queue.begin(), std::make_move_iterator(glyphs.begin()),
                                       std::make_move_iterator(glyphs.end()));
                    continue;
                }

                batch.push_back(std::move(job));
            }

            const uint64_t generation = state.LineArtGeneration;

            // The pack's pictures, read again whenever its archive changed (the pack, or whether it has one).
            if (state.PackPath != state.PackReadFrom)
            {
                const std::optional<std::filesystem::path> path = state.PackPath;
                lock.unlock();
                std::shared_ptr<const Packed> pack = path.has_value() ? ReadPack(*path) : nullptr;
                lock.lock();
                state.Pack = std::move(pack);
                state.PackReadFrom = path;
                state.Tally.PackPictures = state.Pack != nullptr ? state.Pack->Archive->GetEntries().size() : 0;
            }

            const std::shared_ptr<const Packed> pack = state.Pack;
            lock.unlock();

            try
            {
                if (!batch.empty())
                {
                    helpers.Run(batch.size(), [&](size_t part) { RunJob(batch[part], generation, pack, stop); });
                }
            }
            catch (const std::exception& error)
            {
                SDL_Log("MCSharpPictures: making %zu pictures ahead failed: %s", batch.size(), error.what());
            }

            lock.lock();
            state.Working = false;
            state.Changed.notify_all();
        }
    }

    /// <summary>
    /// Queues <paramref name="job"/>, starting the making thread the first time, the pack's sharp.hda as
    /// <paramref name="pack"/> (MCHdArt::SharpArchive, asked on the game's thread). Call with State::Lock held.
    /// </summary>
    void Queue(State& state, Job&& job, std::optional<std::filesystem::path> pack)
    {
        state.PackPath = std::move(pack);
        state.Queue.push_back(std::move(job));

        if (!state.Maker.joinable())
        {
            state.GamePriority = MCCurrentThreadPriority();
            state.Maker = std::jthread(MakePictures);
        }

        state.Changed.notify_all();
    }
}

int32_t MCSharpPictures::FontGlyphCount(std::span<const uint8_t> font)
{
    return font.size() >= FontCountAt + sizeof(int32_t) ? std::max(MCRead32(font, FontCountAt), 0) : 0;
}

std::optional<MCSharpPictures::MCFontGlyph> MCSharpPictures::FontGlyph(std::span<const uint8_t> font, int32_t character)
{
    const size_t entry = FontTableAt + static_cast<size_t>(std::max(character, 0)) * FontOffsetBytes;

    if (character < 0 || entry + FontOffsetBytes > font.size())
    {
        return std::nullopt;
    }

    const auto at = static_cast<uint32_t>(MCRead32(font, entry));

    if (static_cast<size_t>(at) + GlyphPixelsAt > font.size())
    {
        return std::nullopt;
    }

    const int32_t pitch = MCRead32(font, at);
    const int32_t height = VfxFontHeight(font);
    const size_t whole = static_cast<size_t>(std::max(pitch, 0)) * static_cast<size_t>(std::max(height, 0));

    if (at + GlyphPixelsAt + whole > font.size())
    {
        return std::nullopt;
    }

    return MCFontGlyph{at, pitch, height, font.subspan(at + GlyphPixelsAt, whole)};
}

uint64_t MCSharpPictures::MCGlyphPictures::KeyOf(size_t picture) const
{
    return (Key ^ picture) * MCFnvHash::Prime;
}

MCSharpPictures::MCGlyphPictures MCSharpPictures::GlyphPictures(std::span<const uint8_t> glyph, int32_t columns,
                                                                int32_t rows, const MCSharpText::MCFontLayers& font)
{
    // A two-tone glyph by its layers as well (its font's say how its bytes stack).
    MCGlyphPictures pictures{PixelsKey(Kind::Glyph, glyph, columns, columns, rows), std::nullopt};

    if (MCSharpText::Layered(glyph, columns, columns, rows))
    {
        pictures.Layers = MCSharpText::LayersOf(glyph, columns, columns, rows, font);

        for (const uint8_t byte : pictures.Layers->Bytes)
        {
            MCFnvHash::Mix(pictures.Key, byte);
        }

        if (const std::optional<MCSharpText::MCDropShadow>& shadow = pictures.Layers->Shadow; shadow.has_value())
        {
            for (const int32_t offset : {shadow->FromX, shadow->FromY, shadow->ToX, shadow->ToY})
            {
                MCFnvHash::Mix(pictures.Key, static_cast<uint64_t>(offset));
            }
        }
    }

    return pictures;
}

std::vector<std::vector<uint8_t>> MCSharpPictures::RedrawGlyph(std::span<const uint8_t> glyph, int32_t columns,
                                                               int32_t rows, const MCGlyphPictures& pictures)
{
    return pictures.Layers.has_value()
               ? MCSharpText::RedrawLayers(glyph, columns, columns, rows, *pictures.Layers)
               : std::vector<std::vector<uint8_t>>{MCSharpText::Redraw(glyph, columns, columns, rows)};
}

std::vector<uint8_t> MCSharpPictures::LineArt(std::span<const uint8_t> shapeRows, int32_t columns, int32_t rows)
{
    const size_t pixels = static_cast<size_t>(columns) * static_cast<size_t>(rows);
    std::vector<uint8_t> decoded(pixels * MCShapeRows::DecodedBytes);
    MCShapeRows::Decode(shapeRows, rows, columns, decoded);
    std::vector<uint8_t> art(pixels, MCSharpText::NoInk);

    for (size_t pixel = 0; pixel < pixels; ++pixel)
    {
        if (decoded[pixel * MCShapeRows::DecodedBytes + 1] == MCShapeRows::Drawn)
        {
            art[pixel] = decoded[pixel * MCShapeRows::DecodedBytes];
        }
    }

    return art;
}

uint64_t MCSharpPictures::LineArtKey(std::span<const uint8_t> art, int32_t columns, int32_t rows,
                                     std::span<const MCSharpLines::MCDrawnLoop> drawn)
{
    uint64_t key = PixelsKey(Kind::Lines, art, columns, columns, rows);

    // The parts drawn by hand are part of what it's redrawn from.
    for (const MCSharpLines::MCDrawnLoop& loop : drawn)
    {
        for (const int32_t edge : {loop.Left, loop.Top, loop.Right, loop.Bottom})
        {
            MCFnvHash::Mix(key, static_cast<uint64_t>(static_cast<uint32_t>(edge)));
        }

        for (const auto& point : loop.Through)
        {
            for (const float along : point)
            {
                MCFnvHash::Mix(key, std::bit_cast<uint32_t>(along));
            }
        }
    }

    return key;
}

std::optional<MCSharpPictures::MCLineArtCut> MCSharpPictures::CutLineArt(
    std::span<const uint8_t> table, int32_t frame, std::span<const MCSharpLines::MCDrawnLoop> drawn)
{
    // A frame whose header the table holds whole.
    if (frame < 0 || frame >= VfxShapeCount(table) || table.size() < MCShapeRows::RowsAt ||
        MCShapeOffset(table, frame) > table.size() - MCShapeRows::RowsAt)
    {
        return std::nullopt;
    }

    // Whole, as the game draws it: the rows its header covers.
    const std::span<const uint8_t> header = table.subspan(MCShapeOffset(table, frame));
    const std::span<const uint8_t> shapeRows = header.subspan(MCShapeRows::RowsAt);
    const int32_t rows = MCShapeRows::HeaderRows(header);
    int32_t columns = 0;

    if (rows <= 0)
    {
        return std::nullopt;
    }

    MCShapeRows::Extent(shapeRows, rows, columns);

    if (columns <= 0)
    {
        return std::nullopt;
    }

    MCLineArtCut cut{LineArt(shapeRows, columns, rows), columns, rows, 0};
    cut.Key = LineArtKey(cut.Art, columns, rows, drawn);
    return cut;
}

MCSharpPictures::MCMadePicture::MCMadePicture(std::vector<uint8_t> rgba) : _RgbaBytes(rgba.size())
{
    // Blue and alpha are 0 throughout as the redraws leave them: kept as red and green alone, half the bytes.
    bool redGreen = rgba.size() % RgbaPixelBytes == 0;

    for (size_t at = 0; redGreen && at < rgba.size(); at += RgbaPixelBytes)
    {
        redGreen = rgba[at + 2] == 0 && rgba[at + 3] == 0;
    }

    if (!redGreen)
    {
        _Bytes = std::move(rgba);
        return;
    }

    _RedGreen = true;
    _Bytes.resize(rgba.size() / RgbaPixelBytes * RedGreenPixelBytes);

    for (size_t pixel = 0; pixel < rgba.size() / RgbaPixelBytes; ++pixel)
    {
        _Bytes[pixel * RedGreenPixelBytes] = rgba[pixel * RgbaPixelBytes];
        _Bytes[pixel * RedGreenPixelBytes + 1] = rgba[pixel * RgbaPixelBytes + 1];
    }
}

MCSharpPictures::MCMadePicture MCSharpPictures::MCMadePicture::OfRedGreen(std::vector<uint8_t> redGreen)
{
    MCMadePicture picture;
    picture._RgbaBytes = redGreen.size() / RedGreenPixelBytes * RgbaPixelBytes;
    picture._Bytes = std::move(redGreen);
    picture._RedGreen = true;
    return picture;
}

std::optional<std::span<const uint8_t>> MCSharpPictures::MCMadePicture::RedGreen() const
{
    return _RedGreen ? std::optional<std::span<const uint8_t>>(_Bytes) : std::nullopt;
}

void MCSharpPictures::MCMadePicture::CopyRgbaTo(std::span<uint8_t> out) const
{
    if (!_RedGreen)
    {
        std::ranges::copy(_Bytes, out.begin());
        return;
    }

    // A pixel's red and green as a 16-bit word, widened to RGBA8's 32 bits: blue and alpha 0 (bytes in memory order,
    // red first, on the little-endian machines the port runs on). Word by word, as a screen's line art takes 10 MB.
    static_assert(std::endian::native == std::endian::little);

    for (size_t pixel = 0; pixel < _RgbaBytes / RgbaPixelBytes; ++pixel)
    {
        uint16_t redGreen = 0;
        std::memcpy(&redGreen, &_Bytes[pixel * RedGreenPixelBytes], sizeof(redGreen));
        const uint32_t rgba = redGreen;
        std::memcpy(&out[pixel * RgbaPixelBytes], &rgba, sizeof(rgba));
    }
}

void MCSharpPictures::MakeFont(std::span<const uint8_t> font)
{
    if (font.size() < FontTableAt)
    {
        return;
    }

    State& state = TheState();
    auto bytes = std::make_shared<const std::vector<uint8_t>>(font.begin(), font.end());
    std::optional<std::filesystem::path> pack = MCHdArt::SharpArchive();
    std::scoped_lock lock(state.Lock);
    Queue(state, FontJob{std::move(bytes)}, std::move(pack));
}

void MCSharpPictures::MakeLineArt(std::span<const uint8_t> table, int32_t frame,
                                  std::span<const MCSharpLines::MCDrawnLoop> drawn)
{
    std::optional<MCLineArtCut> cut = CutLineArt(table, frame, drawn);

    if (!cut.has_value())
    {
        return;
    }

    constexpr auto Scale = static_cast<size_t>(MCSharpText::Scale);
    LinesJob job{std::move(cut->Art), cut->Columns, cut->Rows, {drawn.begin(), drawn.end()}, cut->Key, 0};
    job.Bytes = static_cast<size_t>(job.Columns) * static_cast<size_t>(job.Rows) * Scale * Scale * RedGreenPixelBytes;
    State& state = TheState();
    std::optional<std::filesystem::path> pack = MCHdArt::SharpArchive();
    std::scoped_lock lock(state.Lock);

    if (state.Known.contains(job.Key) || state.Done.contains(job.Key) || state.Making.contains(job.Key) ||
        state.Asked.contains(job.Key))
    {
        return;
    }

    if (state.LineArtBytes + job.Bytes > MostLineArtBytes)
    {
        ++state.Tally.DrawingsLeft;
        return;
    }

    state.LineArtBytes += job.Bytes;
    state.Asked.insert(job.Key);
    Queue(state, std::move(job), std::move(pack));
}

std::optional<MCSharpPictures::MCMadePicture> MCSharpPictures::Take(uint64_t key, int32_t columns, int32_t rows)
{
    State& state = TheState();
    std::unique_lock lock(state.Lock);
    state.Changed.wait(lock, [&] { return !state.Making.contains(key); });
    state.Known.insert(key);
    state.Asked.erase(key);
    const auto found = state.Done.find(key);

    // Not made ahead: from the pack's pictures, read (none: made by the caller).
    if (found == state.Done.end())
    {
        const std::shared_ptr<const Packed> pack = state.Pack;
        lock.unlock();
        std::optional<MCMadePicture> packed =
            pack != nullptr ? FromPack(*pack, key, RedGreenBytes(columns, rows)) : std::nullopt;
        lock.lock();

        if (packed.has_value())
        {
            MCMadeTally& tally = state.Tally;
            ++tally.TakenFromPack;
            ++tally.Taken;
            tally.TakenRgbaBytes += packed->RgbaBytes();
        }

        return packed;
    }

    MCMadePicture picture = std::move(found->second.Picture);
    state.LineArtBytes -= std::min(state.LineArtBytes, found->second.LineArtBytes);
    state.Done.erase(found);
    MCMadeTally& tally = state.Tally;
    ++tally.Taken;
    tally.TakenRgbaBytes += picture.RgbaBytes();
    --tally.Held;
    tally.HeldBytes -= picture.KeptBytes();
    return picture;
}

void MCSharpPictures::ForgetLineArt()
{
    State& state = TheState();
    std::scoped_lock lock(state.Lock);
    ++state.LineArtGeneration;
    std::erase_if(state.Queue,
                  [&state](const Job& job)
                  {
                      const auto* lines = std::get_if<LinesJob>(&job);

                      if (lines != nullptr)
                      {
                          state.Asked.erase(lines->Key);
                      }

                      return lines != nullptr;
                  });

    for (auto it = state.Done.begin(); it != state.Done.end();)
    {
        if (it->second.LineArtBytes == 0)
        {
            ++it;
            continue;
        }

        --state.Tally.Held;
        state.Tally.HeldBytes -= it->second.Picture.KeptBytes();
        it = state.Done.erase(it);
    }

    state.LineArtBytes = 0;
    state.Changed.notify_all();
}

void MCSharpPictures::WaitForMade()
{
    State& state = TheState();
    std::unique_lock lock(state.Lock);
    state.Changed.wait(lock, [&] { return state.Queue.empty() && !state.Working; });
}

MCSharpPictures::MCMadeTally MCSharpPictures::Tally()
{
    State& state = TheState();
    std::scoped_lock lock(state.Lock);
    return state.Tally;
}
