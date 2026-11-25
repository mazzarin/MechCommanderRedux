#include "stdafx.h"
#include "logistics/MCArtWords.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiPort.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "logistics/MCLogPort.h"
#include "main/MCGamePaths.h"
#include "mission/MCRematchLetters.h"
#include "platform/MCRenderer.h"
#include "vfx/MCVfxFunctions.h"

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

    /// <summary>
    /// A colour table drawing a font's inked values in <paramref name="color"/> (255 stays out), made and registered
    /// with the renderers the first time a colour is asked for, and kept (1998's logistics labels').
    /// </summary>
    MCColorTable FontColors(uint8_t color)
    {
        // One table per colour, made on first use; the renderers keep pointing at it, so it never moves or goes.
        static std::array<std::unique_ptr<std::array<uint8_t, 256>>, 256> tables;
        std::unique_ptr<std::array<uint8_t, 256>>& table = tables[color];

        if (table == nullptr)
        {
            table = std::make_unique<std::array<uint8_t, 256>>();

            for (size_t i = 0; i < table->size(); ++i)
            {
                (*table)[i] = i == 0xff ? 0xff : color;
            }

            MCRenderer::RegisterData(std::span(table->data(), table->size()), MCDataKind::Tables);
        }

        return MCColorTable(*table);
    }

    // The text buttons' words (1998's MCLogTextButton), made of ACCEPT's pictures.
    namespace TextButtons
    {
        /// <summary>
        /// The prefixes of the art's button pictures (<c>&lt;state&gt;&lt;button&gt;.tga</c>): up (also lit under the
        /// mouse), pressed and grayed. The text buttons take ACCEPT's pictures.
        /// </summary>
        constexpr std::string_view UpState = "bh_";
        constexpr std::string_view DownState = "bg_";
        constexpr std::string_view GrayState = "bn_";
        constexpr std::string_view FaceButton = "accept";

        /// <summary>
        /// The face inside the bevel, where the art's word is (inclusive). Each of its rows is one colour, read at
        /// <see cref="FaceSampleX"/>, left of the word.
        /// </summary>
        constexpr int32_t FaceLeft = 3;
        constexpr int32_t FaceRight = 59;
        constexpr int32_t FaceTop = 2;
        constexpr int32_t FaceBottom = 11;
        constexpr int32_t FaceSampleX = 5;

        /// <summary>
        /// A word in the white font (one with a letter the art has none of): its colours, as the art's words are dark on
        /// the face, light pressed and faint grayed.
        /// </summary>
        constexpr uint8_t UpTextColor = 0x10;
        constexpr uint8_t DownTextColor = 0x1f;
        constexpr uint8_t GrayTextColor = 0x16;

        /// <summary>
        /// A letter as the art's buttons write it: a button whose word has it apart from its neighbours (its up, pressed
        /// and grayed pictures write it in the same columns), and those columns.
        /// </summary>
        struct MCArtLetter
        {
            char Letter = 0;
            std::string_view Button;
            int32_t Left = 0;
            int32_t Width = 0;
        };

        /// <summary>The letters the buttons of ACCEPT's size write, in ACCEPT, DIAL NUMBER, GO!, IPX and the rest.</summary>
        constexpr std::array ArtLetters = {
            MCArtLetter{'A', "accept", 14, 5}, MCArtLetter{'B', "dial", 56, 5},   MCArtLetter{'C', "accept", 20, 5},
            MCArtLetter{'D', "dial", 12, 5},   MCArtLetter{'E', "accept", 32, 4}, MCArtLetter{'G', "go", 24, 5},
            MCArtLetter{'I', "dial", 18, 2},   MCArtLetter{'J', "join", 18, 4},   MCArtLetter{'K', "okay", 32, 5},
            MCArtLetter{'L', "dial", 27, 4},   MCArtLetter{'M', "dial", 48, 7},   MCArtLetter{'N', "dial", 36, 5},
            MCArtLetter{'O', "no", 31, 6},     MCArtLetter{'P', "accept", 37, 5}, MCArtLetter{'R', "dial", 67, 5},
            MCArtLetter{'S', "yes", 34, 5},    MCArtLetter{'T', "accept", 43, 5}, MCArtLetter{'U', "dial", 42, 5},
            MCArtLetter{'V', "leave", 34, 5},  MCArtLetter{'X', "ipx", 33, 5},    MCArtLetter{'Y', "yes", 23, 5},
            MCArtLetter{'!', "go", 37, 2},
        };

        /// <summary>F, which no button writes: E with its bottom bar's rows cut back to the stem's columns.</summary>
        constexpr char DerivedLetter = 'F';
        constexpr char DerivedFrom = 'E';
        constexpr int32_t BottomBarRows = 2;
        constexpr int32_t StemColumns = 2;

        /// <summary>The rows the art's words take, and the columns between two letters and of a space.</summary>
        constexpr int32_t LetterTop = 3;
        constexpr int32_t LetterBottom = 10;
        constexpr int32_t LetterRows = LetterBottom - LetterTop + 1;
        constexpr int32_t LetterGap = 1;
        constexpr int32_t SpaceWidth = 3;

        /// <summary>The logistics palette's grey ramp, black to white, that the faces and their words are drawn in.</summary>
        constexpr int32_t GreyFirst = 0x10;
        constexpr int32_t GreyLast = 0x1f;

        /// <summary>
        /// A letter's shading: each pixel's steps along the grey ramp from its row's face colour (darker below 0), row by row
        /// from <see cref="LetterTop"/>.
        /// </summary>
        struct MCLetterShade
        {
            int32_t Width = 0;
            std::vector<int32_t> Steps;

            int32_t& At(int32_t x, int32_t y) { return Steps[static_cast<size_t>(y * Width + x)]; }
        };

        /// <summary>The art file of <paramref name="button"/>'s picture in <paramref name="state"/>.</summary>
        std::string ButtonArt(std::string_view state, std::string_view button)
        {
            return std::format("{}{}.tga", state, button);
        }

        /// <summary>Pixel (<paramref name="x"/>, <paramref name="y"/>) of <paramref name="port"/>.</summary>
        uint8_t& PixelAt(const MCLogPort& port, int32_t x, int32_t y)
        {
            return port.Pixels()[static_cast<size_t>(y * port.Width() + x)];
        }

        /// <summary>
        /// <paramref name="letter"/>'s shading in the art's pictures of <paramref name="state"/>, or none when no button
        /// writes it.
        /// </summary>
        std::optional<MCLetterShade> LetterShade(std::string_view state, char letter)
        {
            if (letter == DerivedLetter)
            {
                std::optional<MCLetterShade> shade = LetterShade(state, DerivedFrom);

                for (int32_t y = LetterRows - BottomBarRows; shade.has_value() && y < LetterRows; y++)
                {
                    for (int32_t x = StemColumns; x < shade->Width; x++)
                    {
                        shade->At(x, y) = 0;
                    }
                }

                return shade;
            }

            const auto found = std::ranges::find(ArtLetters, letter, &MCArtLetter::Letter);

            if (found == ArtLetters.end())
            {
                return std::nullopt;
            }

            const std::shared_ptr<MCLogPort> art = LogArt(ButtonArt(state, found->Button));

            if (art->Width() < found->Left + found->Width || art->Height() <= LetterBottom)
            {
                return std::nullopt;
            }

            MCLetterShade shade{.Width = found->Width, .Steps = std::vector<int32_t>(found->Width * LetterRows)};

            for (int32_t y = 0; y < LetterRows; y++)
            {
                const int32_t face = PixelAt(*art, FaceSampleX, LetterTop + y);

                for (int32_t x = 0; x < found->Width; x++)
                {
                    shade.At(x, y) = PixelAt(*art, found->Left + x, LetterTop + y) - face;
                }
            }

            return shade;
        }

        /// <summary>
        /// Writes <paramref name="label"/> centred on the wiped <paramref name="face"/> in the art's own lettering: each
        /// letter's shading in the art's pictures of <paramref name="state"/>, laid on the face's rows.
        /// </summary>
        /// <returns>Whether it did: not for a letter no button writes, nor a word wider than the face.</returns>
        bool WriteInArtLetters(MCLogPort& face, std::string_view state, std::string_view label)
        {
            // A space has no shading.
            std::vector<std::optional<MCLetterShade>> letters;
            int32_t width = LetterGap * (static_cast<int32_t>(label.size()) - 1);

            for (const char letter : label)
            {
                if (letter == ' ')
                {
                    letters.emplace_back();
                    width += SpaceWidth;
                    continue;
                }

                std::optional<MCLetterShade> shade = LetterShade(state, letter);

                if (!shade.has_value())
                {
                    return false;
                }

                width += shade->Width;
                letters.push_back(std::move(shade));
            }

            if (label.empty() || width > FaceRight - FaceLeft + 1 || face.Height() <= LetterBottom)
            {
                return false;
            }

            int32_t left = (face.Width() - width + 1) / 2;

            for (std::optional<MCLetterShade>& letter : letters)
            {
                if (!letter.has_value())
                {
                    left += SpaceWidth + LetterGap;
                    continue;
                }

                for (int32_t y = 0; y < LetterRows; y++)
                {
                    for (int32_t x = 0; x < letter->Width; x++)
                    {
                        uint8_t& pixel = PixelAt(face, left + x, LetterTop + y);
                        pixel = static_cast<uint8_t>(std::clamp(pixel + letter->At(x, y), GreyFirst, GreyLast));
                    }
                }

                left += letter->Width + LetterGap;
            }

            return true;
        }

        /// <summary>
        /// ACCEPT's picture in <paramref name="state"/> with its word wiped and <paramref name="label"/> written, centred:
        /// in the art's lettering, else in the white font in <paramref name="textColor"/>.
        /// </summary>
        std::shared_ptr<MCLogPort> LabelledFace(std::string_view state, std::string_view label, uint8_t textColor)
        {
            const std::shared_ptr<MCLogPort> source = LogArt(ButtonArt(state, FaceButton));
            auto face = std::make_shared<MCLogPort>();
            face->Init(source->Width(), source->Height());
            VfxPaneCopy(source->Frame(), 0, 0, face->Frame(), 0, 0, VfxNoColor);
            const int32_t bottom = std::min(FaceBottom, source->Height() - 1);

            for (int32_t y = FaceTop; y <= bottom; y++)
            {
                MCPane row = face->Frame();
                row.X0 = FaceLeft;
                row.X1 = std::min(FaceRight, source->Width() - 1);
                row.Y0 = y;
                row.Y1 = y;
                VfxPaneWipe(row, PixelAt(*source, FaceSampleX, y));
            }

            if (!WriteInArtLetters(*face, state, label))
            {
                const MCGuiFont& font = *WhiteFont;
                VfxStringDraw(face->Frame(), (face->Width() - font.Width(label)) / 2,
                              (face->Height() - font.Height()) / 2, font.FontData.Bytes(), label,
                              FontColors(textColor));
            }

            return face;
        }
    }

    // UPDATE (1998's main menu button, MCUpdatePage), made of ORIGINAL CINEMA's pictures.
    namespace UpdateButton
    {
        /// <summary>
        /// The main menu buttons' pictures, <c>mm_xb&lt;state&gt;&lt;nn&gt;.tga</c> for button nn: the states' letters, h up
        /// (also lit), g pressed and n grayed.
        /// </summary>
        constexpr std::string_view UpState = "h";
        constexpr std::string_view DownState = "g";
        constexpr std::string_view GrayState = "n";

        /// <summary>ORIGINAL CINEMA's number: UPDATE's pictures are made from its own.</summary>
        constexpr int32_t CinemaNumber = 8;

        /// <summary>The word the button writes.</summary>
        constexpr std::string_view UpdateWord = "UPDATE";

        /// <summary>
        /// Where the buttons write their words (inclusive): the rows, and the columns inside the bevel; the face's colour is
        /// read left of every word.
        /// </summary>
        constexpr int32_t WordTop = 3;
        constexpr int32_t WordBottom = 22;
        constexpr int32_t WordRows = WordBottom - WordTop + 1;
        constexpr int32_t WordLeft = 3;
        constexpr int32_t WordRightInset = 3;
        constexpr int32_t FaceSampleX = 5;
        constexpr int32_t FaceSampleY = 12;

        /// <summary>The lettering's slant: a column to the right for every 4 rows up from row 19, the letters' foot.</summary>
        constexpr int32_t SlantFoot = 19;
        constexpr int32_t SlantRows = 4;

        /// <summary>The columns kept clear between two letters on each row, as the art's words keep them.</summary>
        constexpr int32_t LetterGap = 1;

        /// <summary>
        /// A letter the buttons write: the button whose word has it standing apart from its neighbours once the slant is
        /// taken out, which of the word's letters it is (from 0, spaces not counted), and how many letters the word has.
        /// </summary>
        struct MCMenuLetter
        {
            char Letter = 0;
            int32_t Button = 0;
            int32_t Index = 0;
            int32_t WordLetters = 0;
        };

        /// <summary>UPDATE's letters, from QUIT, PREFERENCES and LOAD MISSION.</summary>
        constexpr std::array MenuLetters = {
            MCMenuLetter{'U', 9, 1, 4},  MCMenuLetter{'P', 6, 0, 11}, MCMenuLetter{'D', 4, 3, 11},
            MCMenuLetter{'A', 4, 2, 11}, MCMenuLetter{'T', 9, 3, 4},  MCMenuLetter{'E', 6, 2, 11},
        };

        /// <summary>Not a letter's pixel.</summary>
        constexpr int32_t NoLetter = -1;

        /// <summary>The art file of main menu button <paramref name="number"/>'s picture in <paramref name="state"/>.</summary>
        std::string MenuButtonArt(std::string_view state, int32_t number)
        {
            return std::format("mm_xb{}{:02}.tga", state, number);
        }

        /// <summary>Pixel (<paramref name="x"/>, <paramref name="y"/>) of <paramref name="port"/>.</summary>
        uint8_t& PixelAt(const MCLogPort& port, int32_t x, int32_t y)
        {
            return port.Pixels()[static_cast<size_t>(y * port.Width() + x)];
        }

        /// <summary>How bright a colour is: its red, green and blue weighed as the eye weighs them.</summary>
        constexpr int32_t Brightness(int32_t red, int32_t green, int32_t blue)
        {
            constexpr int32_t RedWeight = 30;
            constexpr int32_t GreenWeight = 59;
            constexpr int32_t BlueWeight = 11;
            return RedWeight * red + GreenWeight * green + BlueWeight * blue;
        }

        /// <summary>How bright palette entry <paramref name="index"/> is (the palette is 6-bit RGB).</summary>
        int32_t Brightness(std::span<const uint8_t> palette, uint8_t index)
        {
            const size_t at = size_t{index} * 3;

            if (at + 2 >= palette.size())
            {
                return 0;
            }

            return Brightness(palette[at], palette[at + 1], palette[at + 2]);
        }

        /// <summary>Column <paramref name="x"/> of row <paramref name="y"/> with the slant taken out: a letter's columns line up on every row.</summary>
        int32_t Unslanted(int32_t x, int32_t y)
        {
            // Rounded down: the rows under the foot shift the other way.
            const int32_t up = SlantFoot - y;
            const int32_t shift = up >= 0 ? up / SlantRows : -((SlantRows - 1 - up) / SlantRows);
            return x - shift;
        }

        /// <summary>A pixel of a letter: where it is and its colour.</summary>
        struct MCLetterPixel
        {
            int32_t X = 0;
            int32_t Y = 0;
            uint8_t Color = 0;
        };

        /// <summary>
        /// A main menu button's word, its letters told apart in its up picture (the brightest). A letter's core is the
        /// pixels brighter than the face by over half the word's brightest; with the slant taken out, each letter's core
        /// takes columns of its own. The edges between (the art's smoothing, which joins neighbouring letters) go to the
        /// letter whose core is nearest on their row, and to none when two are as near.
        /// </summary>
        class MCMenuWord
        {
        public:
            MCMenuWord(int32_t number, std::span<const uint8_t> palette) : _Up(LogArt(MenuButtonArt(UpState, number)))
            {
                const MCLogPort& up = *_Up;
                _Width = up.Width();
                const int32_t right = _Width - 1 - WordRightInset;

                if (up.Height() <= WordBottom || right <= WordLeft)
                {
                    return;
                }

                _Face = PixelAt(up, FaceSampleX, FaceSampleY);
                const int32_t face = Brightness(palette, _Face);
                int32_t brightest = face;

                for (int32_t y = WordTop; y <= WordBottom; y++)
                {
                    for (int32_t x = WordLeft; x <= right; x++)
                    {
                        brightest = std::max(brightest, Brightness(palette, PixelAt(up, x, y)));
                    }
                }

                if (brightest == face)
                {
                    return;
                }

                // The cores, and the runs of columns they take with the slant out: a letter each.
                std::vector<bool> core(static_cast<size_t>(_Width * WordRows), false);
                std::set<int32_t> columns;

                for (int32_t y = WordTop; y <= WordBottom; y++)
                {
                    for (int32_t x = WordLeft; x <= right; x++)
                    {
                        if ((Brightness(palette, PixelAt(up, x, y)) - face) * 2 > brightest - face)
                        {
                            core[static_cast<size_t>((y - WordTop) * _Width + x)] = true;
                            columns.insert(Unslanted(x, y));
                        }
                    }
                }

                for (const int32_t column : columns)
                {
                    if (_Runs.empty() || column != _Runs.back().second + 1)
                    {
                        _Runs.emplace_back(column, column);
                    }
                    else
                    {
                        _Runs.back().second = column;
                    }
                }

                _Owner.assign(static_cast<size_t>(_Width * WordRows), NoLetter);

                for (int32_t y = WordTop; y <= WordBottom; y++)
                {
                    for (int32_t x = WordLeft; x <= right;)
                    {
                        if (PixelAt(up, x, y) == _Face)
                        {
                            x++;
                            continue;
                        }

                        int32_t end = x;

                        while (end <= right && PixelAt(up, end, y) != _Face)
                        {
                            end++;
                        }

                        std::vector<std::pair<int32_t, int32_t>> cores;

                        for (int32_t at = x; at < end; at++)
                        {
                            if (core[static_cast<size_t>((y - WordTop) * _Width + at)])
                            {
                                cores.emplace_back(at, RunOf(Unslanted(at, y)));
                            }
                        }

                        for (int32_t at = x; at < end; at++)
                        {
                            _Owner[static_cast<size_t>((y - WordTop) * _Width + at)] =
                                cores.empty() ? NearestRun(Unslanted(at, y)) : NearestCore(cores, at);
                        }

                        x = end;
                    }
                }
            }

            /// <summary>How many letters the word was found to have.</summary>
            int32_t Letters() const { return static_cast<int32_t>(_Runs.size()); }

            /// <summary>Letter <paramref name="index"/>'s pixels, coloured as in <paramref name="picture"/> (one of the button's pictures).</summary>
            std::vector<MCLetterPixel> Letter(int32_t index, const MCLogPort& picture) const
            {
                std::vector<MCLetterPixel> pixels;

                if (_Owner.empty() || picture.Width() != _Width || picture.Height() <= WordBottom)
                {
                    return pixels;
                }

                for (int32_t y = WordTop; y <= WordBottom; y++)
                {
                    for (int32_t x = 0; x < _Width; x++)
                    {
                        if (_Owner[static_cast<size_t>((y - WordTop) * _Width + x)] == index)
                        {
                            pixels.push_back({.X = x, .Y = y, .Color = PixelAt(picture, x, y)});
                        }
                    }
                }

                return pixels;
            }

        private:
            /// <summary>The letter whose run holds unslanted column <paramref name="column"/>.</summary>
            int32_t RunOf(int32_t column) const
            {
                for (size_t run = 0; run < _Runs.size(); run++)
                {
                    if (column >= _Runs[run].first && column <= _Runs[run].second)
                    {
                        return static_cast<int32_t>(run);
                    }
                }

                return NoLetter;
            }

            /// <summary>The letter whose run is nearest unslanted column <paramref name="column"/>.</summary>
            int32_t NearestRun(int32_t column) const
            {
                int32_t nearest = NoLetter;
                int32_t distance = INT_MAX;

                for (size_t run = 0; run < _Runs.size(); run++)
                {
                    const int32_t away = std::max({_Runs[run].first - column, column - _Runs[run].second, 0});

                    if (away < distance)
                    {
                        distance = away;
                        nearest = static_cast<int32_t>(run);
                    }
                }

                return nearest;
            }

            /// <summary>The letter of the core pixel nearest column <paramref name="x"/> among <paramref name="cores"/> (column, letter); none when two letters are as near.</summary>
            static int32_t NearestCore(std::span<const std::pair<int32_t, int32_t>> cores, int32_t x)
            {
                int32_t nearest = NoLetter;
                int32_t distance = INT_MAX;

                for (const auto& [column, letter] : cores)
                {
                    const int32_t away = std::abs(column - x);

                    if (away < distance)
                    {
                        distance = away;
                        nearest = letter;
                    }
                    else if (away == distance && letter != nearest)
                    {
                        nearest = NoLetter;
                    }
                }

                return nearest;
            }

            std::shared_ptr<MCLogPort> _Up;
            int32_t _Width = 0;
            uint8_t _Face = 0;
            std::vector<std::pair<int32_t, int32_t>> _Runs;
            /// <summary>Each pixel of the word's rows: the letter it is part of, or <see cref="NoLetter"/>.</summary>
            std::vector<int32_t> _Owner;
        };

        /// <summary>
        /// <paramref name="word"/>'s letters cut from the buttons' pictures in <paramref name="state"/> (their words read
        /// once into <paramref name="words"/>), or none when one can't be.
        /// </summary>
        std::optional<std::vector<std::vector<MCLetterPixel>>> CutLetters(std::string_view state, std::string_view word,
                                                                          std::span<const uint8_t> palette,
                                                                          std::map<int32_t, MCMenuWord>& words)
        {
            std::vector<std::vector<MCLetterPixel>> letters;

            for (const char letter : word)
            {
                const auto found = std::ranges::find(MenuLetters, letter, &MCMenuLetter::Letter);

                if (found == MenuLetters.end())
                {
                    return std::nullopt;
                }

                const MCMenuWord& source = words.try_emplace(found->Button, found->Button, palette).first->second;

                if (source.Letters() != found->WordLetters)
                {
                    SDL_Log("Updater: %s has %d letters, not %d: UPDATE is written in the white font",
                            MenuButtonArt(UpState, found->Button).c_str(), source.Letters(), found->WordLetters);
                    return std::nullopt;
                }

                std::vector<MCLetterPixel> pixels =
                    source.Letter(found->Index, *LogArt(MenuButtonArt(state, found->Button)));

                if (pixels.empty())
                {
                    return std::nullopt;
                }

                letters.push_back(std::move(pixels));
            }

            return letters;
        }

        /// <summary>
        /// Lays <paramref name="letters"/> side by side from column 0: each as far left as keeps <see cref="LetterGap"/>
        /// columns clear of the one before on every row both have, as the art spaces its slanted letters.
        /// </summary>
        void LayOut(std::vector<std::vector<MCLetterPixel>>& letters)
        {
            constexpr int32_t None = INT_MIN;
            std::array<int32_t, WordRows> rightmost{};
            rightmost.fill(None);
            int32_t widest = None;

            for (std::vector<MCLetterPixel>& letter : letters)
            {
                std::array<int32_t, WordRows> leftmost{};
                leftmost.fill(INT_MAX);
                int32_t left = INT_MAX;

                for (const MCLetterPixel& pixel : letter)
                {
                    leftmost[static_cast<size_t>(pixel.Y - WordTop)] =
                        std::min(leftmost[static_cast<size_t>(pixel.Y - WordTop)], pixel.X);
                    left = std::min(left, pixel.X);
                }

                int32_t shift = widest == None ? -left : None;

                for (size_t row = 0; widest != None && row < leftmost.size(); row++)
                {
                    if (rightmost[row] != None && leftmost[row] != INT_MAX)
                    {
                        shift = std::max(shift, rightmost[row] + 1 + LetterGap - leftmost[row]);
                    }
                }

                // No row in common: the letter goes after the widest point so far.
                if (shift == None)
                {
                    shift = widest + 1 + LetterGap - left;
                }

                for (MCLetterPixel& pixel : letter)
                {
                    pixel.X += shift;
                    rightmost[static_cast<size_t>(pixel.Y - WordTop)] =
                        std::max(rightmost[static_cast<size_t>(pixel.Y - WordTop)], pixel.X);
                    widest = std::max(widest, pixel.X);
                }
            }
        }

        /// <summary>
        /// ORIGINAL CINEMA's picture in <paramref name="state"/> with its word wiped and <paramref name="word"/> written,
        /// centred: in the menu's lettering, else in the white font in the colour of the word's brightest pixel.
        /// </summary>
        std::shared_ptr<MCLogPort> WordFace(std::string_view state, std::string_view word,
                                            std::span<const uint8_t> palette, std::map<int32_t, MCMenuWord>& words)
        {
            const std::shared_ptr<MCLogPort> source = LogArt(MenuButtonArt(state, CinemaNumber));
            const int32_t width = source->Width();
            auto face = std::make_shared<MCLogPort>();
            face->Init(width, source->Height());
            VfxPaneCopy(source->Frame(), 0, 0, face->Frame(), 0, 0, VfxNoColor);

            if (source->Height() <= WordBottom || width <= WordLeft + WordRightInset)
            {
                return face;
            }

            const uint8_t faceColor = PixelAt(*source, FaceSampleX, FaceSampleY);
            uint8_t brightest = faceColor;

            for (int32_t y = WordTop; y <= WordBottom; y++)
            {
                for (int32_t x = WordLeft; x < width - WordRightInset; x++)
                {
                    uint8_t& pixel = PixelAt(*face, x, y);

                    if (Brightness(palette, pixel) > Brightness(palette, brightest))
                    {
                        brightest = pixel;
                    }

                    pixel = faceColor;
                }
            }

            std::optional<std::vector<std::vector<MCLetterPixel>>> letters = CutLetters(state, word, palette, words);

            if (!letters.has_value())
            {
                const MCGuiFont& font = *WhiteFont;
                VfxStringDraw(face->Frame(), (width - font.Width(word)) / 2, (face->Height() - font.Height()) / 2,
                              font.FontData.Bytes(), word, FontColors(brightest));
                return face;
            }

            LayOut(*letters);
            int32_t left = INT_MAX;
            int32_t right = INT_MIN;

            for (const std::vector<MCLetterPixel>& letter : *letters)
            {
                for (const MCLetterPixel& pixel : letter)
                {
                    left = std::min(left, pixel.X);
                    right = std::max(right, pixel.X);
                }
            }

            const int32_t shift = (width - (right - left + 1)) / 2 - left;

            for (const std::vector<MCLetterPixel>& letter : *letters)
            {
                for (const MCLetterPixel& pixel : letter)
                {
                    const int32_t x = pixel.X + shift;

                    if (x >= WordLeft && x < width - WordRightInset)
                    {
                        PixelAt(*face, x, pixel.Y) = pixel.Color;
                    }
                }
            }

            return face;
        }

        /// <summary>
        /// The colour UPDATE turns while an update waits (6-bit RGB, the brightest): its blues take the palette's entry
        /// nearest this at their own brightness, the letters staying white. The line under QUIT is written in it too.
        /// </summary>
        constexpr std::array<int32_t, 3> Amber = {63, 40, 8};
        constexpr int32_t MaxChannel = 63;

        /// <summary>How much brighter than the blue the amber is (twice, as a rational): the blue face's darkness in amber is brown.</summary>
        constexpr int32_t AmberLiftNumerator = 2;
        constexpr int32_t AmberLiftDenominator = 1;

        /// <summary>The entry of <paramref name="palette"/> nearest the colour <paramref name="target"/> (6-bit RGB).</summary>
        uint8_t Nearest(std::span<const uint8_t> palette, const std::array<int32_t, 3>& target)
        {
            const size_t entries = std::min<size_t>(256, palette.size() / 3);
            uint8_t found = 0;
            int32_t nearest = INT_MAX;

            for (size_t entry = 0; entry < entries; entry++)
            {
                int32_t distance = 0;

                for (size_t channel = 0; channel < target.size(); channel++)
                {
                    const int32_t difference = palette[entry * 3 + channel] - target[channel];
                    distance += difference * difference;
                }

                if (distance < nearest)
                {
                    nearest = distance;
                    found = static_cast<uint8_t>(entry);
                }
            }

            return found;
        }

        /// <summary>
        /// <paramref name="palette"/>'s entries mapped to amber: each blue one (blue its strongest) to the entry nearest
        /// <see cref="Amber"/> at its brightness, the rest kept.
        /// </summary>
        std::array<uint8_t, 256> AmberTable(std::span<const uint8_t> palette)
        {
            std::array<uint8_t, 256> table{};
            constexpr int32_t amber = Brightness(Amber[0], Amber[1], Amber[2]);
            const size_t entries = std::min(table.size(), palette.size() / 3);

            for (size_t entry = 0; entry < table.size(); entry++)
            {
                table[entry] = static_cast<uint8_t>(entry);

                if (entry >= entries)
                {
                    continue;
                }

                const int32_t red = palette[entry * 3];
                const int32_t green = palette[entry * 3 + 1];
                const int32_t blue = palette[entry * 3 + 2];

                if (blue <= red || blue <= green)
                {
                    continue;
                }

                const int32_t brightness = Brightness(palette, static_cast<uint8_t>(entry));
                std::array<int32_t, 3> target{};

                for (size_t channel = 0; channel < target.size(); channel++)
                {
                    target[channel] = std::min(
                        Amber[channel] * brightness * AmberLiftNumerator / (amber * AmberLiftDenominator), MaxChannel);
                }

                table[entry] = Nearest(palette, target);
            }

            return table;
        }

        /// <summary>A copy of <paramref name="picture"/> with its colours mapped through <paramref name="table"/>.</summary>
        std::shared_ptr<MCLogPort> Recoloured(const MCLogPort& picture, const std::array<uint8_t, 256>& table)
        {
            auto copy = std::make_shared<MCLogPort>();
            copy->Init(picture.Width(), picture.Height());

            for (size_t at = 0; at < picture.Pixels().size(); at++)
            {
                copy->Pixels()[at] = table[picture.Pixels()[at]];
            }

            return copy;
        }
    }

    // MATCH OPTIONS' tabs (1998's session screen, MCMatchOptionsTab), made of the briefing's tabs.
    namespace MatchTabs
    {
        /// <summary>
        /// The briefing's tab pictures (13 x 231, two tabs a picture, the top one's word read down it): lsbdw00 OPERATION
        /// BRIEFING in front of MISSION BRIEFING, lsbdw01 behind it; lsbdw02 MULTIPLAYER CHAT in front of MISSION BRIEFING,
        /// lsbdw03 behind it.
        /// </summary>
        constexpr std::string_view OperationInFront = "lsbdw00.tga";
        constexpr std::string_view OperationBehind = "lsbdw01.tga";
        constexpr std::string_view ChatInFront = "lsbdw02.tga";
        constexpr std::string_view ChatBehind = "lsbdw03.tga";
        constexpr int32_t TabsWidth = 13;
        constexpr int32_t TabsHeight = 231;

        /// <summary>The first of the pictures' rows that the tab behind shows of (the rows above are the front tab's).</summary>
        constexpr int32_t BehindTop = 122;

        /// <summary>
        /// A tab's face, the columns its word is written on (inclusive): a tab in front reaches the panel at its left, one
        /// behind is drawn in a column. The rows of the bottom tab's word (MISSION BRIEFING), and rows of its face with no
        /// word above and below it.
        /// </summary>
        constexpr std::pair<int32_t, int32_t> FrontFace = {0, 10};
        constexpr std::pair<int32_t, int32_t> BehindFace = {2, 11};
        constexpr int32_t OldWordTop = 133;
        constexpr int32_t OldWordBottom = 216;
        constexpr std::array<std::pair<int32_t, int32_t>, 2> BlankFaceRows = {{{123, 132}, {217, 225}}};

        /// <summary>A tab's word in the pictures: on the top tab, MULTIPLAYER CHAT or OPERATION BRIEFING; on the bottom one, MISSION BRIEFING.</summary>
        enum class TabWord : uint8_t
        {
            Chat,
            Operation,
            Mission,
        };

        /// <summary>
        /// A run of <see cref="MatchOptionsWord"/>'s letters as a tab's word has them, one after another (each letter's
        /// rows touch its neighbours'): the word and its rows; none, rows of blank face (a space, or the gap a letter
        /// needs that its word had beside it).
        /// </summary>
        struct TabLetters
        {
            std::string_view Letters;
            std::optional<TabWord> Word;
            int32_t Top = 0;
            int32_t Rows = 0;
        };

        /// <summary>MATCH OPTIONS: M, AT and CH from MULTIPLAYER CHAT, OP and TION from OPERATION, S from MISSION.</summary>
        constexpr std::array MatchOptionsLetters = {
            TabLetters{"M", TabWord::Chat, 18, 8},        TabLetters{"AT", TabWord::Chat, 94, 11},
            TabLetters{"CH", TabWord::Chat, 82, 12},      TabLetters{" ", std::nullopt, 0, 3},
            TabLetters{"OP", TabWord::Operation, 13, 12}, TabLetters{"TION", TabWord::Operation, 43, 20},
            TabLetters{"", std::nullopt, 0, 1},           TabLetters{"S", TabWord::Mission, 145, 5},
        };

        /// <summary>The picture with <paramref name="word"/> on the tab in front (<paramref name="front"/>) or behind.</summary>
        std::string_view PictureOf(TabWord word, bool front)
        {
            switch (word)
            {
                case TabWord::Chat:
                    return front ? ChatInFront : ChatBehind;
                case TabWord::Operation:
                    return front ? OperationInFront : OperationBehind;
                case TabWord::Mission:
                default:
                    // MISSION BRIEFING is the bottom tab: in front where the top one is behind.
                    return front ? OperationBehind : OperationInFront;
            }
        }

        /// <summary>Whether <paramref name="art"/> is a tabs picture's size.</summary>
        bool IsTabsPicture(const MCLogPort& art)
        {
            return art.Width() == TabsWidth && art.Height() == TabsHeight;
        }

        /// <summary>
        /// Copies row <paramref name="from"/>'s face (<paramref name="face"/>'s columns) of <paramref name="source"/> to row
        /// <paramref name="to"/> of <paramref name="target"/>.
        /// </summary>
        void CopyFaceRow(const MCLogPort& source, int32_t from, MCLogPort& target, int32_t to,
                         std::pair<int32_t, int32_t> face)
        {
            const std::span<const uint8_t> sourceRow = source.Pixels().subspan(static_cast<size_t>(from * TabsWidth));
            const std::span<uint8_t> targetRow = target.Pixels().subspan(static_cast<size_t>(to * TabsWidth));
            std::copy(sourceRow.begin() + face.first, sourceRow.begin() + face.second + 1,
                      targetRow.begin() + face.first);
        }

        /// <summary>A row of a tab's face put into the lettered tabs: row <c>From</c> of a picture as row <c>To</c>.</summary>
        struct FaceRow
        {
            std::string_view Picture;
            int32_t From = 0;
            int32_t To = 0;
        };

        /// <summary>
        /// The picture the lettered tabs are made on, with MATCH OPTIONS in front (<paramref name="front"/>: lsbdw03,
        /// MULTIPLAYER CHAT behind) or behind (lsbdw02).
        /// </summary>
        std::string_view TabsPictureFor(bool front)
        {
            return front ? ChatBehind : ChatInFront;
        }

        /// <summary>
        /// The face rows that letter <see cref="MatchOptionsWord"/> on <see cref="TabsPictureFor"/>'s picture: MISSION
        /// BRIEFING wiped with rows of the face left blank above and below it, then the letters, the word centred where
        /// the old one was.
        /// </summary>
        std::vector<FaceRow> LetteredRows(bool front)
        {
            const std::string_view tabs = TabsPictureFor(front);
            std::vector<int32_t> blankRows;

            for (const auto& [first, last] : BlankFaceRows)
            {
                for (int32_t row = first; row <= last; row++)
                {
                    blankRows.push_back(row);
                }
            }

            std::vector<FaceRow> rows;

            for (int32_t row = OldWordTop; row <= OldWordBottom; row++)
            {
                rows.push_back({tabs, blankRows[static_cast<size_t>(row - OldWordTop) % blankRows.size()], row});
            }

            int32_t letterRows = 0;

            for (const TabLetters& letters : MatchOptionsLetters)
            {
                letterRows += letters.Rows;
            }

            int32_t row = (OldWordTop + OldWordBottom + 1 - letterRows) / 2;

            for (const TabLetters& letters : MatchOptionsLetters)
            {
                if (letters.Word.has_value())
                {
                    for (int32_t offset = 0; offset < letters.Rows; offset++)
                    {
                        rows.push_back({PictureOf(*letters.Word, front), letters.Top + offset, row + offset});
                    }
                }

                row += letters.Rows;
            }

            return rows;
        }

        /// <summary>The pictures the lettered tabs are made of, by name.</summary>
        using TabsArt = std::map<std::string_view, std::shared_ptr<MCLogPort>>;

        /// <summary>
        /// <paramref name="tabs"/>' picture and those <paramref name="rows"/> are from; none (logged) when one isn't a tabs
        /// picture.
        /// </summary>
        std::optional<TabsArt> ArtOf(std::string_view tabs, std::span<const FaceRow> rows)
        {
            TabsArt art{{tabs, nullptr}};

            for (const FaceRow& row : rows)
            {
                art.emplace(row.Picture, nullptr);
            }

            for (auto& [name, picture] : art)
            {
                picture = LogScreenArt(name);

                if (!IsTabsPicture(*picture))
                {
                    SDL_Log("MATCH OPTIONS: %.*s is %dx%d, not %dx%d as the tabs' pictures are; no tab drawn",
                            static_cast<int>(name.size()), name.data(), picture->Width(), picture->Height(), TabsWidth,
                            TabsHeight);
                    return std::nullopt;
                }
            }

            return art;
        }

        /// <summary>
        /// The two tabs with the bottom one's word <see cref="MatchOptionsWord"/>, in front (<paramref name="front"/>: as
        /// lsbdw03, MULTIPLAYER CHAT behind) or behind (as lsbdw02); null when the art isn't the tabs' as expected. Their HD
        /// copy is the HD art pack's, found by their pixels as any picture's is (the pack draws the words in its font, from
        /// the tabs mc_tests' --hd-extract-menus writes).
        /// </summary>
        std::shared_ptr<MCLogPort> LetterTabs(bool front)
        {
            const std::string_view base = TabsPictureFor(front);
            const std::vector<FaceRow> rows = LetteredRows(front);
            const std::optional<TabsArt> art = ArtOf(base, rows);

            if (!art.has_value())
            {
                return nullptr;
            }

            const std::pair<int32_t, int32_t> face = front ? FrontFace : BehindFace;
            MCLogPort lettered;
            lettered.Init(TabsWidth, TabsHeight);
            std::ranges::copy(art->at(base)->Pixels(), lettered.Pixels().begin());

            for (const FaceRow& row : rows)
            {
                CopyFaceRow(*art->at(row.Picture), row.From, lettered, row.To, face);
            }

            // Behind MULTIPLAYER CHAT only the bottom tab is drawn: the background has the one in front.
            const int32_t top = front ? 0 : BehindTop;
            auto tabs = std::make_shared<MCLogPort>();
            tabs->Init(TabsWidth, TabsHeight - top);
            std::ranges::copy(lettered.Pixels().subspan(static_cast<size_t>(top * TabsWidth)), tabs->Pixels().begin());
            return tabs;
        }
    }

    // REMATCH (1998's results, MCMissionResultsScreen), made of NEXT MISSION's faces.
    namespace Rematch
    {
        /// <summary>Port-only (Redux HD): the move-on button's faces, which REMATCH is made from.</summary>
        constexpr std::string_view MoveOnUpFace = "mr_ms01.tga";
        constexpr std::string_view MoveOnDownFace = "mr_ms02.tga";
        /// <summary>
        /// The face's lettering lies within these columns and rows (inside its bevel); the face column left of it gives each
        /// row's colour (the face's shading and its two lines).
        /// </summary>
        constexpr int32_t FaceColumn = 3;
        constexpr int32_t LetteringRight = 0x67;
        constexpr int32_t LetteringTop = 2;
        constexpr int32_t LetteringBottom = 0xf;
        /// <summary>Port-only (Redux HD): the palette's grey ramp, black to white, the faces' and their words' greys.</summary>
        constexpr uint8_t GreyRampFirst = 0x10;
        constexpr uint8_t GreyRampLast = 0x1f;

        /// <summary>Port-only (Redux HD): how a face's word is inked: darker than the face (up) or lighter (held).</summary>
        enum class MCInk
        {
            Dark,
            Light
        };

        /// <summary>Port-only (Redux HD): a colour's red, green and blue.</summary>
        constexpr std::array<int64_t, 3> Channels(const SDL_Color& color)
        {
            return {color.r, color.g, color.b};
        }

        /// <summary>Port-only (Redux HD): how bright a grey is (its channels' sum).</summary>
        constexpr int32_t Brightness(const SDL_Color& color)
        {
            return color.r + color.g + color.b;
        }

        /// <summary>
        /// Port-only (Redux HD): of <paramref name="face"/>, <paramref name="ink"/> and the grey ramp, the entry of
        /// <paramref name="colors"/> nearest <paramref name="face"/>'s colour blended with <paramref name="ink"/>'s by
        /// <paramref name="coverage"/> (of MCRematchLetters::Full); the first of equals. In whole numbers, so every machine
        /// picks the same.
        /// </summary>
        uint8_t NearestBlend(const MCTgaColors& colors, uint8_t face, uint8_t ink, int32_t coverage)
        {
            const std::array<int64_t, 3> from = Channels(colors[face]);
            const std::array<int64_t, 3> to = Channels(colors[ink]);
            std::array<int64_t, 3> target{};

            for (size_t channel = 0; channel < target.size(); channel++)
            {
                target[channel] = from[channel] * (MCRematchLetters::Full - coverage) + to[channel] * coverage;
            }

            uint8_t nearest = face;
            int64_t distance = std::numeric_limits<int64_t>::max();
            const auto weigh = [&](uint8_t candidate)
            {
                const std::array<int64_t, 3> channels = Channels(colors[candidate]);
                int64_t away = 0;

                for (size_t channel = 0; channel < target.size(); channel++)
                {
                    const int64_t difference = channels[channel] * MCRematchLetters::Full - target[channel];
                    away += difference * difference;
                }

                if (away < distance)
                {
                    distance = away;
                    nearest = candidate;
                }
            };

            weigh(face);
            weigh(ink);

            for (int32_t grey = GreyRampFirst; grey <= GreyRampLast; grey++)
            {
                weigh(static_cast<uint8_t>(grey));
            }

            return nearest;
        }

        /// <summary>
        /// Port-only (Redux HD): REMATCH's face made from the move-on button's <paramref name="file"/>, its word inked as
        /// <paramref name="ink"/> says (see RematchFaces); null when the face can't be read.
        /// </summary>
        std::shared_ptr<MCGuiPort> RematchFace(std::string_view file, MCInk ink)
        {
            MCTgaColors colors{};
            auto face = std::make_shared<MCGuiPort>();

            if (face->Init(file, colors) != 0 || face->Width() <= LetteringRight || face->Height() <= LetteringBottom ||
                face->Width() < MCRematchLetters::Left + MCRematchLetters::Width ||
                face->Height() < MCRematchLetters::Top + MCRematchLetters::Rows)
            {
                SDL_Log("Results: REMATCH has no face: %.*s didn't load as NEXT MISSION's (%dx%d)",
                        static_cast<int>(file.size()), file.data(), face->Width(), face->Height());
                return nullptr;
            }

            const int32_t width = face->Width();
            std::vector<uint8_t> pixels(face->Pixels().begin(), face->Pixels().end());
            const auto at = [&](int32_t x, int32_t y) -> uint8_t&
            { return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]; };

            // The art's ink (its word's darkest pixel on the up face, its brightest on the held one), and the word wiped:
            // each row the face column's colour.
            uint8_t inkColor = at(FaceColumn, LetteringTop);

            for (int32_t y = LetteringTop; y <= LetteringBottom; y++)
            {
                const uint8_t faceColor = at(FaceColumn, y);

                for (int32_t x = FaceColumn + 1; x <= LetteringRight; x++)
                {
                    const int32_t brightness = Brightness(colors[at(x, y)]);
                    const int32_t inkBrightness = Brightness(colors[inkColor]);

                    if (ink == MCInk::Dark ? brightness < inkBrightness : brightness > inkBrightness)
                    {
                        inkColor = at(x, y);
                    }

                    at(x, y) = faceColor;
                }
            }

            // REMATCH laid on.
            for (int32_t row = 0; row < MCRematchLetters::Rows; row++)
            {
                for (int32_t column = 0; column < MCRematchLetters::Width; column++)
                {
                    const int32_t coverage =
                        MCRematchLetters::Coverage[static_cast<size_t>(row)][static_cast<size_t>(column)];

                    if (coverage != 0)
                    {
                        uint8_t& pixel = at(MCRematchLetters::Left + column, MCRematchLetters::Top + row);
                        pixel = NearestBlend(colors, pixel, inkColor, coverage);
                    }
                }
            }

            const std::shared_ptr<MCTexture> texture = face->Bitmap().Texture.lock();

            if (texture == nullptr)
            {
                SDL_Log("Results: REMATCH has no face: %.*s's picture has no texture", static_cast<int>(file.size()),
                        file.data());
                return nullptr;
            }

            std::ranges::copy(pixels, MCRenderer::LockTexture(*texture).begin());
            MCRenderer::UnlockTexture(*texture);
            return face;
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

MCButtonPictures MCTextButtonPictures(std::string_view label)
{
    using namespace TextButtons;
    return {.Up = LabelledFace(UpState, label, UpTextColor),
            .Down = LabelledFace(DownState, label, DownTextColor),
            .Gray = LabelledFace(GrayState, label, GrayTextColor)};
}

MCButtonPictures UpdateButtonPictures(std::span<const uint8_t> palette)
{
    using namespace UpdateButton;
    std::map<int32_t, MCMenuWord> words;
    const std::array<uint8_t, 256> amber = AmberTable(palette);
    return {.Up = Recoloured(*WordFace(UpState, UpdateWord, palette, words), amber),
            .Down = Recoloured(*WordFace(DownState, UpdateWord, palette, words), amber),
            .Gray = WordFace(GrayState, UpdateWord, palette, words)};
}

auto MakeMatchTabs(MCSessionTab front) -> std::shared_ptr<MCLogPort>
{
    using namespace MatchTabs;
    return LetterTabs(front == MCSessionTab::Options);
}

MCRematchFaces RematchFaces()
{
    using namespace Rematch;
    return {.Up = RematchFace(MoveOnUpFace, MCInk::Dark), .Down = RematchFace(MoveOnDownFace, MCInk::Light)};
}

auto PaletteFromArt(std::string_view fileName) -> std::vector<uint8_t>
{
    MCFile file;
    std::string path = std::format("{}{}", ArtPath, fileName);

    if (file.Open(path) != 0)
    {
        path = fileName;

        if (file.Open(path) != 0)
        {
            GeneralMsg(std::format("Error reading '{}'", path));
        }
    }

    const uint32_t size = file.FileSize();

    if (size == 0)
    {
        GeneralMsg(std::format("Error reading '{}'", path));
    }

    std::vector<uint8_t> data(size);
    file.Read(data);
    file.Close();

    // The TGA's palette (BGR after the 18-byte header), as 6-bit RGB.
    std::vector<uint8_t> palette(0x300);
    const std::span<const uint8_t> source = std::span<const uint8_t>(data).subspan(0x12);

    for (size_t i = 0; i < 0x100; i++)
    {
        palette[i * 3] = source[i * 3 + 2] >> 2;
        palette[i * 3 + 1] = source[i * 3 + 1] >> 2;
        palette[i * 3 + 2] = source[i * 3] >> 2;
    }

    return palette;
}
