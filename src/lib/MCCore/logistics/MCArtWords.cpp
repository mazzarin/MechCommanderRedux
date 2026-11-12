#include "stdafx.h"
#include "logistics/MCArtWords.h"
#include "logistics/MCLogPort.h"

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
    /// A picture being made of others' pixels. Its HD copy is the HD art pack's, found by its pixels as any picture's is
    /// (the pack draws its words in its font, from the pictures mc_tests' --hd-extract-menus writes).
    /// </summary>
    class Canvas
    {
    public:
        explicit Canvas(const MCLogPort& base)
            : _Width(base.Width()), _Height(base.Height()), _Pixels(base.Pixels().begin(), base.Pixels().end())
        {
        }

        /// <summary>Pixel (<paramref name="x"/>, <paramref name="y"/>) becomes <paramref name="from"/>'s at (<paramref name="fromX"/>, <paramref name="fromY"/>).</summary>
        void Put(int32_t x, int32_t y, const MCLogPort& from, int32_t fromX, int32_t fromY)
        {
            if (x < 0 || y < 0 || x >= _Width || y >= _Height)
            {
                return;
            }

            _Pixels[static_cast<size_t>(y * _Width + x)] = PixelOf(from, fromX, fromY);
        }

        /// <summary>The picture made.</summary>
        std::shared_ptr<MCLogPort> Made() const
        {
            auto made = std::make_shared<MCLogPort>();
            made->Init(_Width, _Height);
            std::ranges::copy(_Pixels, made->Pixels().begin());
            return made;
        }

    private:
        int32_t _Width = 0;
        int32_t _Height = 0;
        std::vector<uint8_t> _Pixels;
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
        std::reference_wrapper<const MCLogPort> From;
    };

    /// <summary>Lays <paramref name="letters"/> on <paramref name="canvas"/>, each moved by its shift.</summary>
    void Lay(Canvas& canvas, std::span<const CutLetter> letters, std::span<const int32_t> shifts)
    {
        for (size_t i = 0; i < letters.size(); i++)
        {
            for (const auto [x, y] : letters[i].Letter.get().Pixels)
            {
                canvas.Put(x + shifts[i], y, letters[i].From, x, y);
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
        Canvas canvas(*plate);

        for (const auto [x, y] : wiped)
        {
            if (ButtonFace.Holds(x, y))
            {
                canvas.Put(x, y, *plate, ButtonPlainX, y);
            }
        }

        const std::array<CutLetter, 3> letters = {CutLetter{serial[SerialL], *serialArt},
                                                  CutLetter{serial[SerialA], *serialArt},
                                                  CutLetter{internet[InternetN], *internetArt}};
        Lay(canvas, letters, shifts);
        made[i] = canvas.Made();
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
    Canvas canvas(*lan);
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
        canvas.Put(x, y, *panel, x, y);
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
        letters.push_back({word[i], *lan});
        letterShifts.push_back(shifts[i]);
    }

    for (const Piece& letter : connection)
    {
        letters.push_back({letter, *lan});
        letterShifts.push_back(connectionShift);
    }

    if (connection.back().Right() + connectionShift > TitleArea.Right)
    {
        SDL_Log("INTERNET CONNECTION: the title doesn't fit the panel; the LAN screen's shows");
        return nullptr;
    }

    Lay(canvas, letters, letterShifts);
    return canvas.Made();
}
