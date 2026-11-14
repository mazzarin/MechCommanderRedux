#include "stdafx.h"
#include "platform/MCSharpPictures.h"
#include "lib/MCFnvHash.h"
#include "lib/MCWorkers.h"
#include "platform/MCShapeRows.h"
#include "platform/MCSoftwareRenderer.h"
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

    /// <summary>The key of what's redrawn from <paramref name="pixels"/>: its kind, size and bytes.</summary>
    uint64_t PixelsKey(Kind kind, std::span<const uint8_t> pixels, int32_t pitch, int32_t columns, int32_t rows)
    {
        uint64_t key = MCFnvHash::Basis;
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

    /// <summary>A job's pictures' keys and how to make them (empty: nothing to make).</summary>
    struct Plan
    {
        std::vector<uint64_t> Keys;
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

            plan.Make = [glyph = *glyph, pictures = std::move(pictures)]
            { return MCSharpPictures::RedrawGlyph(glyph.Pixels, glyph.Pitch, glyph.Height, pictures); };
        }
        else if (const auto* lines = std::get_if<LinesJob>(&job))
        {
            plan.Keys.push_back(lines->Key);
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
    /// Runs <paramref name="job"/> on a helper: claims the keys of its pictures no one has (Making), makes them and
    /// keeps them (Done) unless the renderer has taken to making them meanwhile (Known) or, line art, the line art made
    /// before <paramref name="lineArtGeneration"/> was forgotten. A job that fails is logged; its pictures are left to
    /// the renderer.
    /// </summary>
    void RunJob(const Job& job, uint64_t lineArtGeneration, const std::stop_token& stop)
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

        std::vector<std::vector<uint8_t>> made;

        try
        {
            made = plan.Make();
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

        for (const uint64_t key : claimed)
        {
            state.Making.erase(key);
            const auto picture = std::ranges::find(plan.Keys, key) - plan.Keys.begin();

            if (!kept || state.Known.contains(key) || static_cast<size_t>(picture) >= made.size() ||
                made[static_cast<size_t>(picture)].empty())
            {
                continue;
            }

            MCSharpPictures::MCMadePicture done(std::move(made[static_cast<size_t>(picture)]));
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
        const int32_t count =
            font.Font->size() >= FontCountAt + sizeof(int32_t) ? MCRead32(*font.Font, FontCountAt) : 0;
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
            lock.unlock();

            try
            {
                if (!batch.empty())
                {
                    helpers.Run(batch.size(), [&](size_t part) { RunJob(batch[part], generation, stop); });
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

    /// <summary>Queues <paramref name="job"/>, starting the making thread the first time. Call with State::Lock held.</summary>
    void Queue(State& state, Job&& job)
    {
        state.Queue.push_back(std::move(job));

        if (!state.Maker.joinable())
        {
            state.GamePriority = MCCurrentThreadPriority();
            state.Maker = std::jthread(MakePictures);
        }

        state.Changed.notify_all();
    }
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
    std::scoped_lock lock(state.Lock);
    Queue(state, FontJob{std::move(bytes)});
}

void MCSharpPictures::MakeLineArt(std::span<const uint8_t> table, int32_t frame,
                                  std::span<const MCSharpLines::MCDrawnLoop> drawn)
{
    if (frame < 0 || frame >= VfxShapeCount(table))
    {
        return;
    }

    // Whole, as the game draws it: the rows its header covers.
    const std::span<const uint8_t> header = table.subspan(MCShapeOffset(table, frame));
    const std::span<const uint8_t> shapeRows = header.subspan(MCShapeRows::RowsAt);
    const int32_t rows = MCShapeRows::HeaderRows(header);
    int32_t columns = 0;

    if (rows <= 0)
    {
        return;
    }

    MCShapeRows::Extent(shapeRows, rows, columns);

    if (columns <= 0)
    {
        return;
    }

    LinesJob job{LineArt(shapeRows, columns, rows), columns, rows, {drawn.begin(), drawn.end()}, 0, 0};
    job.Key = LineArtKey(job.Art, columns, rows, drawn);
    constexpr auto Scale = static_cast<size_t>(MCSharpText::Scale);
    job.Bytes = static_cast<size_t>(columns) * static_cast<size_t>(rows) * Scale * Scale * RedGreenPixelBytes;
    State& state = TheState();
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
    Queue(state, std::move(job));
}

std::optional<MCSharpPictures::MCMadePicture> MCSharpPictures::Take(uint64_t key)
{
    State& state = TheState();
    std::unique_lock lock(state.Lock);
    state.Changed.wait(lock, [&] { return !state.Making.contains(key); });
    state.Known.insert(key);
    state.Asked.erase(key);
    const auto found = state.Done.find(key);

    if (found == state.Done.end())
    {
        return std::nullopt;
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
