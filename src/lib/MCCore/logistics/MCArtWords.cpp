#include "stdafx.h"
#include "logistics/MCArtWords.h"
#include "logistics/MCLogPort.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"

#include <deque>
#include <functional>
#include <map>
#include <set>

namespace
{
    using Pixel = std::pair<int32_t, int32_t>;

    /// <summary>A rectangle of a picture's pixels, inclusive.</summary>
    struct Area
    {
        int32_t Left = 0;
        int32_t Top = 0;
        int32_t Right = 0;
        int32_t Bottom = 0;

        bool Holds(int32_t x, int32_t y) const { return x >= Left && x <= Right && y >= Top && y <= Bottom; }
    };

    /// <summary>
    /// The access screen's big buttons (175 x 28): the face inside the bevel where their words are, each row one colour
    /// (read at <see cref="ButtonPlainX"/>, left of every word).
    /// </summary>
    constexpr Area ButtonFace = {.Left = 4, .Top = 2, .Right = 169, .Bottom = 25};
    constexpr int32_t ButtonPlainX = 20;
    /// <summary>The buttons' pictures: up (lit under the mouse too), pressed, grayed; the words' pieces are the up one's.</summary>
    constexpr std::array<std::string_view, 3> ButtonStates = {"bh_", "bg_", "bn_"};
    constexpr std::string_view LanPlate = "lan";
    constexpr std::string_view SerialWord = "seria";
    constexpr std::string_view InternetWord = "inter";
    /// <summary>The pieces of each word: NETWORK (N, ET joined, W, O, R, K), SERIAL, and INTERNET (TE joined).</summary>
    constexpr size_t NetworkPieces = 6;
    constexpr size_t SerialPieces = 6;
    constexpr size_t InternetPieces = 7;
    constexpr size_t SerialA = 4;
    constexpr size_t SerialL = 5;
    constexpr size_t InternetN = 1;
    /// <summary>
    /// A big button letter's HD copy (HdLetterOwners): its strokes are the HD pixels at least half as far from their row's
    /// face (the largest colour channel's difference) as the word's farthest (a grayed button's letters lie nearer
    /// their face), its soft edges the others at least this far, two original pixels at most from the letter. A stroke is
    /// one letter's when three quarters of its pixels on the word's letters are on that letter's.
    /// </summary>
    constexpr int32_t StrokeShare = 2;
    constexpr int32_t EdgeDifference = 8;
    constexpr int32_t EdgeReach = 2;
    constexpr size_t OwnStrokeShare = 3;
    constexpr size_t OwnStrokeOf = 4;
    constexpr size_t RgbaBytes = 4;
    constexpr size_t RgbBytes = 3;
    constexpr int32_t ChannelMax = 255;

    /// <summary>
    /// The multiplayer panels (290 x 339), which share one background behind their titles; the LAN screen's is the
    /// one retitled. The others are listed in the order their backgrounds are taken from, the shortest titles first.
    /// </summary>
    constexpr std::string_view LanPanel = "lan00.tga";
    constexpr std::array<std::string_view, 8> OtherPanels = {
        "mm_wn00.tga",  "mm_wn01.tga", "mm_wn02.tga", "prefs_00.tga",
        "access00.tga", "rroom00.tga", "modem00.tga", "serial00.tga",
    };

    /// <summary>Where the panels' titles are (inside the frame), and the grey ramp their letters are drawn in.</summary>
    constexpr Area TitleArea = {.Left = 4, .Top = 8, .Right = 285, .Bottom = 34};
    constexpr uint8_t GreyFirst = 0x10;
    constexpr uint8_t GreyLast = 0x1f;
    /// <summary>A pixel this near a letter's may hold its glow: the background is taken only from farther.</summary>
    constexpr int32_t GlowReach = 2;
    /// <summary>
    /// NETWORK CONNECTION's pieces: NETWORK's N, ET joined, W, O, R, K, then CONNECTION's C, O, N, N, E, C, T, I, O, N.
    /// </summary>
    constexpr size_t TitlePieces = 16;
    constexpr size_t NetworkLast = 5;
    constexpr size_t ConnectionFirst = 6;
    constexpr std::array<size_t, 8> InternetLetters = {13, 0, 12, 10, 4, 8, 10, 12};

    /// <summary>One letter (or letters the art joined): its pixels, and its columns on each of its rows.</summary>
    struct Piece
    {
        std::vector<Pixel> Pixels;
        std::map<int32_t, std::pair<int32_t, int32_t>> Rows;

        int32_t Left() const
        {
            int32_t left = std::numeric_limits<int32_t>::max();

            for (const auto& [y, columns] : Rows)
            {
                left = std::min(left, columns.first);
            }

            return left;
        }

        int32_t Right() const
        {
            int32_t right = std::numeric_limits<int32_t>::min();

            for (const auto& [y, columns] : Rows)
            {
                right = std::max(right, columns.second);
            }

            return right;
        }
    };

    uint8_t PixelOf(const MCLogPort& port, int32_t x, int32_t y)
    {
        return port.Pixels()[static_cast<size_t>(y * port.Width() + x)];
    }

    /// <summary>
    /// The pieces of <paramref name="area"/> whose pixels <paramref name="differs"/> says differ from the background,
    /// each joined run (side by side or above each other) one, left to right.
    /// </summary>
    std::vector<Piece> PiecesOf(Area area, const std::function<bool(int32_t, int32_t)>& differs)
    {
        const int32_t width = area.Right - area.Left + 1;
        const int32_t height = area.Bottom - area.Top + 1;
        std::vector<uint8_t> seen(static_cast<size_t>(width * height));
        const auto seenAt = [&](int32_t x, int32_t y) -> uint8_t&
        { return seen[static_cast<size_t>((y - area.Top) * width + x - area.Left)]; };
        std::vector<Piece> pieces;

        for (int32_t x = area.Left; x <= area.Right; x++)
        {
            for (int32_t y = area.Top; y <= area.Bottom; y++)
            {
                if (seenAt(x, y) != 0 || !differs(x, y))
                {
                    continue;
                }

                Piece piece;
                std::vector<Pixel> next = {{x, y}};
                seenAt(x, y) = 1;

                while (!next.empty())
                {
                    const auto [px, py] = next.back();
                    next.pop_back();
                    piece.Pixels.emplace_back(px, py);
                    auto [row, added] = piece.Rows.try_emplace(py, px, px);
                    row->second = {std::min(row->second.first, px), std::max(row->second.second, px)};

                    for (const auto [nx, ny] :
                         std::array<Pixel, 4>{{{px + 1, py}, {px - 1, py}, {px, py + 1}, {px, py - 1}}})
                    {
                        if (area.Holds(nx, ny) && seenAt(nx, ny) == 0 && differs(nx, ny))
                        {
                            seenAt(nx, ny) = 1;
                            next.emplace_back(nx, ny);
                        }
                    }
                }

                pieces.push_back(std::move(piece));
            }
        }

        std::ranges::stable_sort(pieces, {}, &Piece::Left);
        return pieces;
    }

    /// <summary>
    /// How close <paramref name="after"/>, moved <paramref name="shift"/> columns, comes to <paramref name="before"/> on
    /// the rows they share: its left less the other's right at the closest (none when they share no row).
    /// </summary>
    std::optional<int32_t> Approach(const Piece& before, const Piece& after, int32_t shift)
    {
        std::optional<int32_t> closest;

        for (const auto& [y, columns] : after.Rows)
        {
            if (const auto found = before.Rows.find(y); found != before.Rows.end())
            {
                const int32_t gap = columns.first + shift - found->second.second;
                closest = closest.has_value() ? std::min(*closest, gap) : gap;
            }
        }

        return closest;
    }

    /// <summary>The art's spacing in <paramref name="word"/>: the closest approach its letters most often have.</summary>
    int32_t ArtGap(std::span<const Piece> word)
    {
        std::map<int32_t, int32_t> counts;

        for (size_t i = 1; i < word.size(); i++)
        {
            if (const std::optional<int32_t> gap = Approach(word[i - 1], word[i], 0))
            {
                counts[*gap]++;
            }
        }

        return counts.empty()
                   ? 1
                   : std::ranges::max_element(counts, {}, [](const auto& count) { return count.second; })->first;
    }

    /// <summary>
    /// The shifts that lay <paramref name="letters"/> left to right, each at <paramref name="gap"/> from the last at their
    /// closest, the first's left at <paramref name="left"/>.
    /// </summary>
    std::vector<int32_t> Laid(std::span<const std::reference_wrapper<const Piece>> letters, int32_t left, int32_t gap)
    {
        std::vector<int32_t> shifts;

        for (size_t i = 0; i < letters.size(); i++)
        {
            if (i == 0)
            {
                shifts.push_back(left - letters[0].get().Left());
                continue;
            }

            const Piece& before = letters[i - 1];
            const Piece& after = letters[i];
            // Moved to touch the one before (its right plus the gap), then on to the gap where they come closest.
            int32_t shift = before.Right() + shifts.back() + gap - after.Left();
            const std::optional<int32_t> closest = Approach(before, after, shift - shifts.back());
            shift += closest.has_value() ? gap - *closest : 0;
            shifts.push_back(shift);
        }

        return shifts;
    }

    /// <summary>
    /// An HD frame's colours as RGBA8, read and written by HD pixel: a BC7 frame's decoded, and only its changed blocks
    /// encoded again, so the rest stays as the pack has it.
    /// </summary>
    class HdColors
    {
    public:
        /// <summary>The colours of <paramref name="frame"/>; none for a frame kept other than as RGBA8 or BC7.</summary>
        static std::optional<HdColors> Of(const MCHdArt::Frame& frame)
        {
            HdColors colors;
            colors._Width = frame.Width;
            colors._Height = frame.Height;
            colors._Bc7 = frame.Format == MCHdArt::ColorFormat::Bc7;

            if (colors._Bc7)
            {
                colors._Rgba = MCBc7::Decode(frame.Colors, frame.Width, frame.Height);
            }
            else if (frame.Format == MCHdArt::ColorFormat::Rgba8)
            {
                colors._Rgba = frame.Colors;
            }

            if (colors._Rgba.size() != static_cast<size_t>(frame.Width) * static_cast<size_t>(frame.Height) * RgbaBytes)
            {
                return std::nullopt;
            }

            return colors;
        }

        int32_t Width() const { return _Width; }
        int32_t Height() const { return _Height; }

        std::span<const uint8_t, RgbaBytes> At(int32_t x, int32_t y) const
        {
            return std::span<const uint8_t, RgbaBytes>(_Rgba.data() + Index(x, y), RgbaBytes);
        }

        void Set(int32_t x, int32_t y, std::span<const uint8_t, RgbaBytes> color)
        {
            std::ranges::copy(color, _Rgba.begin() + static_cast<ptrdiff_t>(Index(x, y)));
            _Changed.emplace(x / MCBc7::BlockSide, y / MCBc7::BlockSide);
        }

        /// <summary>Puts the colours back in <paramref name="frame"/> (the frame they were read from).</summary>
        void WriteTo(MCHdArt::Frame& frame) const
        {
            if (!_Bc7)
            {
                frame.Colors = _Rgba;
                return;
            }

            constexpr size_t BlockRgbaBytes = MCBc7::BlockSide * MCBc7::BlockSide * RgbaBytes;
            const int32_t blocksWide = MCBc7::Padded(_Width) / MCBc7::BlockSide;

            for (const auto [blockX, blockY] : _Changed)
            {
                // Past the frame's right and bottom edges, transparent black, as MCBc7::Encode pads.
                std::array<uint8_t, BlockRgbaBytes> pixels{};

                for (int32_t y = 0; y < MCBc7::BlockSide; y++)
                {
                    for (int32_t x = 0; x < MCBc7::BlockSide; x++)
                    {
                        const int32_t fromX = blockX * MCBc7::BlockSide + x;
                        const int32_t fromY = blockY * MCBc7::BlockSide + y;

                        if (fromX < _Width && fromY < _Height)
                        {
                            std::ranges::copy(At(fromX, fromY),
                                              pixels.begin() +
                                                  static_cast<ptrdiff_t>((y * MCBc7::BlockSide + x) * RgbaBytes));
                        }
                    }
                }

                const size_t at = static_cast<size_t>(blockY * blocksWide + blockX) * MCBc7::BlockBytes;
                MCBc7::EncodeBlock(pixels,
                                   std::span<uint8_t, MCBc7::BlockBytes>(frame.Colors.data() + at, MCBc7::BlockBytes));
            }
        }

    private:
        size_t Index(int32_t x, int32_t y) const
        {
            return (static_cast<size_t>(y) * static_cast<size_t>(_Width) + static_cast<size_t>(x)) * RgbaBytes;
        }

        int32_t _Width = 0;
        int32_t _Height = 0;
        bool _Bc7 = false;
        std::vector<uint8_t> _Rgba;
        std::set<Pixel> _Changed;
    };

    /// <summary>The largest of two HD pixels' colour channels' differences.</summary>
    int32_t Difference(std::span<const uint8_t, RgbaBytes> a, std::span<const uint8_t, RgbaBytes> b)
    {
        int32_t most = 0;

        for (size_t channel = 0; channel < RgbBytes; channel++)
        {
            most = std::max(most, std::abs(static_cast<int32_t>(a[channel]) - static_cast<int32_t>(b[channel])));
        }

        return most;
    }

    /// <summary>The HD column a big button's face is read at, each row's: the middle of <see cref="ButtonPlainX"/>.</summary>
    int32_t FaceColumn(int32_t scale)
    {
        return ButtonPlainX * scale + scale / 2;
    }

    /// <summary>
    /// Which letter of <paramref name="word"/> (a big button's word, as pieces of its up picture) each HD pixel of
    /// <paramref name="colors"/> (a button's HD copy, <paramref name="scale"/> HD pixels an original one) belongs to,
    /// row by row (-1: none). A letter's are its strokes, each joined run of HD pixels far from their row's face most of
    /// whose pixels lie on the letter's original pixels (a run two letters share split by the letter each pixel lies
    /// nearest), and its soft edges, the other pixels off the face on its original pixels or on the face's nearest them.
    /// So a letter's HD copy comes whole, past its original pixels where the HD art's edges reach, and holds nothing of a
    /// neighbour's stroke that reaches into them.
    /// </summary>
    std::vector<int32_t> HdLetterOwners(const HdColors& colors, int32_t scale, std::span<const Piece> word)
    {
        const Area area = {.Left = ButtonFace.Left * scale,
                           .Top = ButtonFace.Top * scale,
                           .Right = (ButtonFace.Right + 1) * scale - 1,
                           .Bottom = (ButtonFace.Bottom + 1) * scale - 1};
        const int32_t faceX = FaceColumn(scale);
        const auto offFace = [&](int32_t x, int32_t y) { return Difference(colors.At(x, y), colors.At(faceX, y)); };
        int32_t farthest = 0;

        for (int32_t y = area.Top; y <= area.Bottom; y++)
        {
            for (int32_t x = area.Left; x <= area.Right; x++)
            {
                farthest = std::max(farthest, offFace(x, y));
            }
        }

        const int32_t strokeDifference = std::max(EdgeDifference, farthest / StrokeShare);
        const auto at = [&](int32_t x, int32_t y)
        { return static_cast<size_t>(y) * static_cast<size_t>(colors.Width()) + static_cast<size_t>(x); };
        // Each original pixel's letter; and each HD pixel's nearest letter (its original pixels') and how far, in steps.
        std::map<Pixel, int32_t> letterAt;
        std::vector<std::pair<int32_t, int32_t>> nearest(
            static_cast<size_t>(colors.Width()) * static_cast<size_t>(colors.Height()), {-1, 0});
        std::deque<Pixel> out;

        for (size_t letter = 0; letter < word.size(); letter++)
        {
            for (const auto [x, y] : word[letter].Pixels)
            {
                letterAt[{x, y}] = static_cast<int32_t>(letter);

                for (int32_t hdY = y * scale; hdY < (y + 1) * scale; hdY++)
                {
                    for (int32_t hdX = x * scale; hdX < (x + 1) * scale; hdX++)
                    {
                        nearest[at(hdX, hdY)] = {static_cast<int32_t>(letter), 0};
                        out.emplace_back(hdX, hdY);
                    }
                }
            }
        }

        while (!out.empty())
        {
            const auto [x, y] = out.front();
            out.pop_front();
            const auto [letter, steps] = nearest[at(x, y)];

            for (const auto [nx, ny] : std::array<Pixel, 4>{{{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}}})
            {
                if (area.Holds(nx, ny) && nearest[at(nx, ny)].first < 0)
                {
                    nearest[at(nx, ny)] = {letter, steps + 1};
                    out.emplace_back(nx, ny);
                }
            }
        }

        const auto originalOf = [&](int32_t x, int32_t y) { return Pixel{x / scale, y / scale}; };
        std::vector<int32_t> owners(static_cast<size_t>(colors.Width()) * static_cast<size_t>(colors.Height()), -1);
        const auto ownerAt = [&](int32_t x, int32_t y) -> int32_t& { return owners[at(x, y)]; };

        for (const Piece& stroke :
             PiecesOf(area, [&](int32_t x, int32_t y) { return offFace(x, y) > strokeDifference; }))
        {
            std::map<int32_t, size_t> onLetters;
            size_t onWord = 0;

            for (const auto [x, y] : stroke.Pixels)
            {
                if (const auto found = letterAt.find(originalOf(x, y)); found != letterAt.end())
                {
                    onLetters[found->second]++;
                    onWord++;
                }
            }

            if (onWord == 0)
            {
                continue;
            }

            const auto [letter, count] =
                *std::ranges::max_element(onLetters, {}, [](const auto& on) { return on.second; });
            const bool own = count * OwnStrokeOf >= onWord * OwnStrokeShare;

            for (const auto [x, y] : stroke.Pixels)
            {
                ownerAt(x, y) = own || nearest[at(x, y)].first < 0 ? letter : nearest[at(x, y)].first;
            }
        }

        // The soft edges.
        for (int32_t y = area.Top; y <= area.Bottom; y++)
        {
            for (int32_t x = area.Left; x <= area.Right; x++)
            {
                if (ownerAt(x, y) >= 0 || offFace(x, y) <= EdgeDifference)
                {
                    continue;
                }

                if (const auto [letter, steps] = nearest[at(x, y)]; letter >= 0 && steps <= EdgeReach * scale)
                {
                    ownerAt(x, y) = letter;
                }
            }
        }

        return owners;
    }

    /// <summary>
    /// A picture being made of others' pixels, each pixel's origin kept, so its HD copy is made of theirs the same way.
    /// </summary>
    class Canvas
    {
    public:
        explicit Canvas(std::shared_ptr<MCLogPort> base)
            : _Width(base->Width())
            , _Height(base->Height())
            , _Pixels(base->Pixels().begin(), base->Pixels().end())
            , _Origins(_Pixels.size())
        {
            _Sources.push_back(std::move(base));

            for (int32_t y = 0; y < _Height; y++)
            {
                for (int32_t x = 0; x < _Width; x++)
                {
                    _Origins[Index(x, y)] = {0, x, y};
                }
            }
        }

        /// <summary>The number <paramref name="picture"/> is known by here (as <see cref="Put"/> takes it).</summary>
        int32_t SourceOf(const std::shared_ptr<MCLogPort>& picture)
        {
            const auto found = std::ranges::find(_Sources, picture);

            if (found != _Sources.end())
            {
                return static_cast<int32_t>(found - _Sources.begin());
            }

            _Sources.push_back(picture);
            return static_cast<int32_t>(_Sources.size() - 1);
        }

        /// <summary>Pixel (<paramref name="x"/>, <paramref name="y"/>) becomes source <paramref name="source"/>'s at (<paramref name="fromX"/>, <paramref name="fromY"/>).</summary>
        void Put(int32_t x, int32_t y, int32_t source, int32_t fromX, int32_t fromY)
        {
            if (x < 0 || y < 0 || x >= _Width || y >= _Height)
            {
                return;
            }

            _Pixels[Index(x, y)] = PixelOf(*_Sources[static_cast<size_t>(source)], fromX, fromY);
            _Origins[Index(x, y)] = {source, fromX, fromY};
        }

        /// <summary>
        /// Wipes <paramref name="word"/> (the big button's own word, as pieces of its up picture, this picture's base) from
        /// the HD copy by its HD pixels (HdLetterOwners), each to its row's face: the original pixels are wiped with
        /// <see cref="Put"/>, but the HD art's edges reach past them.
        /// </summary>
        void WipeButtonWord(std::span<const Piece> word) { _WipedWord.assign(word.begin(), word.end()); }

        /// <summary>
        /// Lays letter <paramref name="letter"/> of <paramref name="word"/> (a big button's word, as pieces of its up
        /// picture), cut from <paramref name="from"/> (the button in this picture's state), moved <paramref name="shift"/>
        /// columns. Its pixels are put as <see cref="Put"/> puts them; its HD copy is the letter's HD pixels in the pack's
        /// copy of <paramref name="from"/> (HdLetterOwners), its soft edges whole, laid on the face this picture's HD copy
        /// has there.
        /// </summary>
        void LayButtonLetter(std::span<const Piece> word, size_t letter, const std::shared_ptr<MCLogPort>& from,
                             int32_t shift)
        {
            const int32_t source = SourceOf(from);

            for (const auto [x, y] : word[letter].Pixels)
            {
                if (x + shift >= 0 && x + shift < _Width && y >= 0 && y < _Height)
                {
                    _Pixels[Index(x + shift, y)] = PixelOf(*from, x, y);
                }
            }

            _ButtonLetters.push_back(
                {.Source = source, .Word = std::vector(word.begin(), word.end()), .Letter = letter, .Shift = shift});
        }

        /// <summary>The picture made, with its HD copy kept when the pack has its sources' (<paramref name="what"/> for the log).</summary>
        std::shared_ptr<MCLogPort> Made(std::string_view what) const
        {
            auto made = std::make_shared<MCLogPort>();
            made->Init(_Width, _Height);
            std::ranges::copy(_Pixels, made->Pixels().begin());
            KeepHd(*made, what);
            return made;
        }

    private:
        struct Origin
        {
            int32_t Source = 0;
            int32_t X = 0;
            int32_t Y = 0;
        };

        /// <summary>A big button's letter laid by its HD pixels (<see cref="LayButtonLetter"/>).</summary>
        struct ButtonLetter
        {
            int32_t Source = 0;
            std::vector<Piece> Word;
            size_t Letter = 0;
            int32_t Shift = 0;
        };

        size_t Index(int32_t x, int32_t y) const { return static_cast<size_t>(y * _Width + x); }

        /// <summary>
        /// Wipes <see cref="_WipedWord"/> from <paramref name="shown"/> (the HD copy, its other pixels made) and lays
        /// <see cref="_ButtonLetters"/> on it from <paramref name="hd"/> (the sources' HD copies, the base's first): each
        /// HD pixel of a letter as far from the face it lies on as it was from its own button's face, and its original
        /// pixels' indices. False when a frame isn't kept as RGBA8 or BC7.
        /// </summary>
        bool LayHdLetters(MCHdArt::Frame& shown, std::span<const std::shared_ptr<const MCHdArt::Frame>> hd) const
        {
            std::optional<HdColors> to = HdColors::Of(shown);

            if (!to.has_value())
            {
                return false;
            }

            const int32_t scale = shown.Scale;
            const int32_t faceX = FaceColumn(scale);

            if (!_WipedWord.empty())
            {
                const std::optional<HdColors> base = HdColors::Of(*hd[0]);

                if (!base.has_value() || hd[0]->Scale != scale)
                {
                    return false;
                }

                const std::vector<int32_t> owners = HdLetterOwners(*base, scale, _WipedWord);

                for (int32_t y = 0; y < base->Height() && y < to->Height(); y++)
                {
                    for (int32_t x = 0; x < base->Width() && x < to->Width(); x++)
                    {
                        if (owners[static_cast<size_t>(y) * static_cast<size_t>(base->Width()) +
                                   static_cast<size_t>(x)] >= 0)
                        {
                            const auto face = to->At(faceX, y);
                            to->Set(x, y, std::array<uint8_t, RgbaBytes>{face[0], face[1], face[2], face[RgbBytes]});
                        }
                    }
                }
            }

            // Each source's colours and owners, found once for its letters.
            std::map<int32_t, std::pair<HdColors, std::vector<int32_t>>> sources;

            for (const ButtonLetter& letter : _ButtonLetters)
            {
                const MCHdArt::Frame& fromFrame = *hd[static_cast<size_t>(letter.Source)];
                auto found = sources.find(letter.Source);

                if (found == sources.end())
                {
                    std::optional<HdColors> colors = HdColors::Of(fromFrame);

                    if (!colors.has_value() || fromFrame.Scale != scale)
                    {
                        return false;
                    }

                    std::vector<int32_t> owners = HdLetterOwners(*colors, scale, letter.Word);
                    found = sources.try_emplace(letter.Source, std::move(*colors), std::move(owners)).first;
                }

                const auto& [from, owners] = found->second;
                const int32_t toShift = letter.Shift * scale;

                for (int32_t y = 0; y < from.Height() && y < to->Height(); y++)
                {
                    for (int32_t x = 0; x < from.Width(); x++)
                    {
                        if (owners[static_cast<size_t>(y) * static_cast<size_t>(from.Width()) +
                                   static_cast<size_t>(x)] != static_cast<int32_t>(letter.Letter) ||
                            x + toShift < 0 || x + toShift >= to->Width())
                        {
                            continue;
                        }

                        const auto pixel = from.At(x, y);
                        const auto fromFace = from.At(faceX, y);
                        const auto toFace = to->At(faceX, y);
                        std::array<uint8_t, RgbaBytes> color{};

                        for (size_t channel = 0; channel < RgbBytes; channel++)
                        {
                            color[channel] = static_cast<uint8_t>(
                                std::clamp(static_cast<int32_t>(toFace[channel]) + pixel[channel] - fromFace[channel],
                                           0, ChannelMax));
                        }

                        color[RgbBytes] = pixel[RgbBytes];
                        to->Set(x + toShift, y, color);
                    }
                }

                const int32_t fromWidth = fromFrame.OriginalWidth();
                const int32_t toWidth = shown.OriginalWidth();

                for (const auto [x, y] : letter.Word[letter.Letter].Pixels)
                {
                    const size_t fromAt = static_cast<size_t>(y * fromWidth + x) * MCHdArt::IndexBytes;
                    const size_t toAt = static_cast<size_t>(y * toWidth + x + letter.Shift) * MCHdArt::IndexBytes;

                    if (fromAt + MCHdArt::IndexBytes <= fromFrame.Indices.size() &&
                        toAt + MCHdArt::IndexBytes <= shown.Indices.size())
                    {
                        std::copy_n(fromFrame.Indices.begin() + static_cast<ptrdiff_t>(fromAt), MCHdArt::IndexBytes,
                                    shown.Indices.begin() + static_cast<ptrdiff_t>(toAt));
                    }
                }
            }

            to->WriteTo(shown);
            return true;
        }

        /// <summary>
        /// Gives <paramref name="made"/> an HD copy made of the pack's copies of its sources, as it was of them. None
        /// while HD art is off, or when the pack lacks one: it shows as it is.
        /// </summary>
        void KeepHd(const MCLogPort& made, std::string_view what) const
        {
            std::vector<std::shared_ptr<const MCHdArt::Frame>> hd;

            for (const std::shared_ptr<MCLogPort>& source : _Sources)
            {
                std::shared_ptr<const MCHdArt::Frame> frame =
                    MCHdArt::FindPicture(source->Pixels(), source->Width(), source->Height());

                if (frame == nullptr)
                {
                    return;
                }

                hd.push_back(std::move(frame));
            }

            MCHdArt::Frame shown = *hd[0];

            for (int32_t y = 0; y < _Height; y++)
            {
                for (int32_t x = 0; x < _Width; x++)
                {
                    const Origin& origin = _Origins[Index(x, y)];

                    if (origin.Source == 0 && origin.X == x && origin.Y == y)
                    {
                        continue;
                    }

                    const MCHdArt::Frame& from = *hd[static_cast<size_t>(origin.Source)];

                    if (!MCHdArt::CopyArea(from, origin.X, origin.Y, 1, 1, shown, x, y))
                    {
                        SDL_Log(
                            "%.*s: the HD art pack's pictures at %dx (format %d) can't be lettered from %dx (format "
                            "%d); it shows as it is",
                            static_cast<int>(what.size()), what.data(), shown.Scale, static_cast<int>(shown.Format),
                            from.Scale, static_cast<int>(from.Format));
                        return;
                    }
                }
            }

            if ((!_ButtonLetters.empty() || !_WipedWord.empty()) && !LayHdLetters(shown, hd))
            {
                SDL_Log(
                    "%.*s: the HD art pack's pictures (format %d) can't be lettered by their HD pixels; it shows as it "
                    "is",
                    static_cast<int>(what.size()), what.data(), static_cast<int>(shown.Format));
                return;
            }

            MCHdArt::KeepPicture(made.Pixels(), made.Width(), made.Height(), std::move(shown));
        }

        int32_t _Width = 0;
        int32_t _Height = 0;
        std::vector<uint8_t> _Pixels;
        std::vector<Origin> _Origins;
        std::vector<std::shared_ptr<MCLogPort>> _Sources;
        std::vector<Piece> _WipedWord;
        std::vector<ButtonLetter> _ButtonLetters;
    };

    /// <summary>A big button's picture in <paramref name="state"/>, and whether it's the size the buttons are.</summary>
    std::shared_ptr<MCLogPort> ButtonPicture(std::string_view state, std::string_view word)
    {
        std::shared_ptr<MCLogPort> picture = LogArt(std::format("{}{}.tga", state, word));
        return picture->Width() > ButtonFace.Right && picture->Height() > ButtonFace.Bottom ? picture : nullptr;
    }

    /// <summary>A big button's word, as pieces of its up picture: pixels that differ from their row's face.</summary>
    std::vector<Piece> ButtonWord(const MCLogPort& up)
    {
        return PiecesOf(ButtonFace,
                        [&](int32_t x, int32_t y) { return PixelOf(up, x, y) != PixelOf(up, ButtonPlainX, y); });
    }

    /// <summary>Every pixel of <paramref name="pieces"/>, and those within <paramref name="reach"/> of one.</summary>
    std::set<Pixel> Around(std::span<const Piece> pieces, int32_t reach)
    {
        std::set<Pixel> around;

        for (const Piece& piece : pieces)
        {
            for (const auto [x, y] : piece.Pixels)
            {
                for (int32_t dy = -reach; dy <= reach; dy++)
                {
                    for (int32_t dx = -reach; dx <= reach; dx++)
                    {
                        around.emplace(x + dx, y + dy);
                    }
                }
            }
        }

        return around;
    }

    /// <summary>A letter and the picture its colours are cut from (the state's, for a button).</summary>
    struct CutLetter
    {
        std::reference_wrapper<const Piece> Letter;
        std::shared_ptr<MCLogPort> From;
    };

    /// <summary>Lays <paramref name="letters"/> on <paramref name="canvas"/>, each moved by its shift.</summary>
    void Lay(Canvas& canvas, std::span<const CutLetter> letters, std::span<const int32_t> shifts)
    {
        for (size_t i = 0; i < letters.size(); i++)
        {
            const int32_t source = canvas.SourceOf(letters[i].From);

            for (const auto [x, y] : letters[i].Letter.get().Pixels)
            {
                canvas.Put(x + shifts[i], y, source, x, y);
            }
        }
    }
}

std::optional<MCButtonPictures> MCLanButtonPictures()
{
    const std::shared_ptr<MCLogPort> plateUp = ButtonPicture(ButtonStates[0], LanPlate);
    const std::shared_ptr<MCLogPort> serialUp = ButtonPicture(ButtonStates[0], SerialWord);
    const std::shared_ptr<MCLogPort> internetUp = ButtonPicture(ButtonStates[0], InternetWord);

    if (plateUp == nullptr || serialUp == nullptr || internetUp == nullptr)
    {
        SDL_Log("LAN button: the access screen's buttons aren't the size they were; NETWORK's shows");
        return std::nullopt;
    }

    const std::vector<Piece> network = ButtonWord(*plateUp);
    const std::vector<Piece> serial = ButtonWord(*serialUp);
    const std::vector<Piece> internet = ButtonWord(*internetUp);

    if (network.size() != NetworkPieces || serial.size() != SerialPieces || internet.size() != InternetPieces)
    {
        SDL_Log("LAN button: the access screen's words have %zu, %zu and %zu letters, not NETWORK's, SERIAL's and "
                "INTERNET's; NETWORK's shows",
                network.size(), serial.size(), internet.size());
        return std::nullopt;
    }

    // L and A at SERIAL's spacing, their middle at NETWORK's.
    const std::array<std::reference_wrapper<const Piece>, 3> lan = {serial[SerialL], serial[SerialA],
                                                                    internet[InternetN]};
    std::vector<int32_t> shifts = Laid(lan, 0, ArtGap(serial));
    const int32_t right = lan.back().get().Right() + shifts.back();
    const int32_t middle = network.front().Left() + network.back().Right();
    const int32_t centring = (middle - right + 1) / 2;
    std::ranges::for_each(shifts, [&](int32_t& shift) { shift += centring; });
    const std::set<Pixel> wiped = Around(network, 1);
    std::array<std::shared_ptr<MCLogPort>, ButtonStates.size()> made;

    for (size_t i = 0; i < ButtonStates.size(); i++)
    {
        const std::string_view state = ButtonStates[i];
        const std::shared_ptr<MCLogPort> plate = ButtonPicture(state, LanPlate);
        const std::shared_ptr<MCLogPort> serialArt = ButtonPicture(state, SerialWord);
        const std::shared_ptr<MCLogPort> internetArt = ButtonPicture(state, InternetWord);

        if (plate == nullptr || serialArt == nullptr || internetArt == nullptr)
        {
            SDL_Log("LAN button: the %.*s pictures aren't the size they were; NETWORK's shows",
                    static_cast<int>(state.size()), state.data());
            return std::nullopt;
        }

        // NETWORK wiped to each row's face, the face's glow with it, then L, A, N in this state's colours.
        Canvas canvas(plate);

        for (const auto [x, y] : wiped)
        {
            if (ButtonFace.Holds(x, y))
            {
                canvas.Put(x, y, 0, ButtonPlainX, y);
            }
        }

        canvas.WipeButtonWord(network);

        canvas.LayButtonLetter(serial, SerialL, serialArt, shifts[0]);
        canvas.LayButtonLetter(serial, SerialA, serialArt, shifts[1]);
        canvas.LayButtonLetter(internet, InternetN, internetArt, shifts[2]);
        made[i] = canvas.Made(std::format("LAN button ({})", state));
    }

    return MCButtonPictures{.Up = made[0], .Down = made[1], .Gray = made[2]};
}

std::shared_ptr<MCLogPort> MCInternetPanel()
{
    const std::shared_ptr<MCLogPort> lan = LogArt(LanPanel);
    std::vector<std::shared_ptr<MCLogPort>> others;

    for (const std::string_view name : OtherPanels)
    {
        std::shared_ptr<MCLogPort> panel = LogArt(name);

        if (panel->Width() == lan->Width() && panel->Height() == lan->Height())
        {
            others.push_back(std::move(panel));
        }
    }

    if (lan->Width() <= TitleArea.Right || lan->Height() <= TitleArea.Bottom)
    {
        SDL_Log("INTERNET CONNECTION: the LAN screen's panel isn't the size it was; its title shows");
        return nullptr;
    }

    const auto isLetter = [](const MCLogPort& panel, int32_t x, int32_t y)
    {
        const uint8_t pixel = PixelOf(panel, x, y);
        return pixel >= GreyFirst && pixel <= GreyLast;
    };

    const auto lettersOf = [&](const MCLogPort& panel)
    { return PiecesOf(TitleArea, [&](int32_t x, int32_t y) { return isLetter(panel, x, y); }); };
    std::vector<std::set<Pixel>> glows;

    for (const std::shared_ptr<MCLogPort>& panel : others)
    {
        glows.push_back(Around(lettersOf(*panel), GlowReach));
    }

    // The background: each pixel near NETWORK CONNECTION's letters from the first panel with none near it.
    const std::set<Pixel> lanGlow = Around(lettersOf(*lan), GlowReach);
    Canvas canvas(lan);
    std::map<Pixel, uint8_t> background;

    for (const auto [x, y] : lanGlow)
    {
        if (!TitleArea.Holds(x, y))
        {
            continue;
        }

        const auto clear =
            std::ranges::find_if(glows, [&](const std::set<Pixel>& glow) { return !glow.contains({x, y}); });

        if (clear == glows.end())
        {
            SDL_Log("INTERNET CONNECTION: no panel has the background behind (%d, %d); the LAN screen's title shows", x,
                    y);
            return nullptr;
        }

        const auto& panel = others[static_cast<size_t>(clear - glows.begin())];
        canvas.Put(x, y, canvas.SourceOf(panel), x, y);
        background[{x, y}] = PixelOf(*panel, x, y);
    }

    // The letters: what differs from that background (their soft edges too), as NETWORK CONNECTION has them.
    const std::vector<Piece> title =
        PiecesOf(TitleArea,
                 [&](int32_t x, int32_t y)
                 {
                     const auto found = background.find({x, y});
                     return found != background.end() && found->second != PixelOf(*lan, x, y);
                 });

    if (title.size() != TitlePieces)
    {
        SDL_Log("INTERNET CONNECTION: the LAN screen's title has %zu letters, not NETWORK CONNECTION's; it shows",
                title.size());
        return nullptr;
    }

    // INTERNET at the old title's left and CONNECTION's spacing, then CONNECTION as it was, as far after it as it was
    // after NETWORK.
    const std::span<const Piece> connection = std::span(title).subspan(ConnectionFirst);
    std::vector<std::reference_wrapper<const Piece>> word;

    for (const size_t letter : InternetLetters)
    {
        word.emplace_back(title[letter]);
    }

    const std::vector<int32_t> shifts = Laid(word, title.front().Left(), ArtGap(connection));
    const int32_t space = Approach(title[NetworkLast], connection.front(), 0).value_or(1);
    const std::array<std::reference_wrapper<const Piece>, 2> joint = {word.back(), connection.front()};
    const int32_t connectionShift = Laid(joint, word.back().get().Left() + shifts.back(), space).back();
    std::vector<CutLetter> letters;
    std::vector<int32_t> letterShifts;

    for (size_t i = 0; i < word.size(); i++)
    {
        letters.push_back({word[i], lan});
        letterShifts.push_back(shifts[i]);
    }

    for (const Piece& letter : connection)
    {
        letters.push_back({letter, lan});
        letterShifts.push_back(connectionShift);
    }

    if (connection.back().Right() + connectionShift > TitleArea.Right)
    {
        SDL_Log("INTERNET CONNECTION: the title doesn't fit the panel; the LAN screen's shows");
        return nullptr;
    }

    Lay(canvas, letters, letterShifts);
    return canvas.Made("INTERNET CONNECTION");
}
