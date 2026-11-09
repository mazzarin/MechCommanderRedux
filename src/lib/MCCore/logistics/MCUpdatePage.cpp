#include "stdafx.h"
#include "logistics/MCUpdatePage.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogLabel.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCLogScrollList.h"
#include "logistics/MCLogTextButton.h"
#include "logistics/MCPrefsPage.h"
#include "main/MCUpdater.h"
#include "network/MCLobbyUpdate.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    using MCUpdater::MCAnswer;
    using MCUpdater::MCStage;
    using MCUpdater::MCStatus;

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
    std::shared_ptr<MCLogPort> WordFace(std::string_view state, std::string_view word, std::span<const uint8_t> palette,
                                        std::map<int32_t, MCMenuWord>& words)
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

    /// <summary>The page's title and its buttons' words.</summary>
    constexpr std::string_view Title = "UPDATE";
    constexpr std::string_view InstallLabel = "INSTALL";
    constexpr std::string_view SkipLabel = "SKIP";
    constexpr std::string_view RestartLabel = "RESTART";

    /// <summary>The column titles: the version found, and at the right the one running.</summary>
    constexpr std::string_view FoundTitle = "NEW: {}";
    constexpr std::string_view RunningTitle = "RUNNING: {}";

    /// <summary>What the status line says of an update ready, by how it goes in (main/MCUpdater.h's MCGoesIn).</summary>
    std::string ReadyText(const MCStatus& status)
    {
        switch (status.GoesIn)
        {
            case MCUpdater::MCGoesIn::NextStart:
                return MCUpdater::CanStartAgain()
                           ? std::format("VERSION {} IS IN. RESTART TO PLAY IT.", status.Version)
                           : std::format("VERSION {} IS IN. IT PLAYS WHEN THE GAME STARTS AGAIN.", status.Version);
            case MCUpdater::MCGoesIn::PlayersStart:
                return std::format("VERSION {} INSTALLS WHEN YOU START REDUX HD YOURSELF.", status.Version);
            case MCUpdater::MCGoesIn::AsItQuits:
                break;
        }

        return std::format("VERSION {} INSTALLS AS YOU QUIT.", status.Version);
    }

    /// <summary>What the status line says the update is doing.</summary>
    std::string StatusText(const MCStatus& status)
    {
        switch (status.Stage)
        {
            case MCStage::None:
                return {};
            case MCStage::Found:
            {
                if (status.Expected == 0)
                {
                    return "A NEWER VERSION IS OUT.";
                }

                return std::format("A NEWER VERSION IS OUT: {} TO DOWNLOAD.", MCUpdateWork::SizeText(status.Expected));
            }
            case MCStage::Downloading:
            {
                if (status.Expected == 0)
                {
                    return std::format("DOWNLOADING: {}.", MCUpdateWork::SizeText(status.Received));
                }

                return std::format("DOWNLOADING: {}.", MCUpdateWork::ProgressText(status.Received, status.Expected));
            }
            case MCStage::Ready:
            {
                // In a lobby's room, what RESTART does there (network/MCLobbyUpdate.h).
                if (std::string room = MCLobbyUpdate::ReadyText(); !room.empty())
                {
                    return room;
                }

                return ReadyText(status);
            }
            case MCStage::Installing:
                return "INSTALLING. THE GAME WILL RESTART BY ITSELF.";
            case MCStage::Failed:
            {
                std::string text = std::format("THE UPDATE FAILED: {}", status.Error);
                std::ranges::transform(text, text.begin(), [](char c)
                                       { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
                return text;
            }
        }

        return {};
    }

    /// <summary>
    /// Port-only: the UPDATE page's list, in the preferences lists' style: the version found and the one running as its
    /// column titles, what the update brings a line a row (wrapped, in capitals as the fonts write), and what the update
    /// is doing on the status line. It draws every frame from the updater.
    /// </summary>
    /// <summary>RESTART shown as the update's stage and a lobby's room allow (with the page's buttons, below).</summary>
    void ShowRestart(MCStage stage);

    class MCLogUpdateNotes : public MCLogScrollList
    {
    public:
        /// <summary>A row: a line of text.</summary>
        static constexpr int32_t RowHeight = 11;

        MCLogUpdateNotes() : MCLogScrollList(RowHeight) {}

        /// <summary>Places the list at (<paramref name="xPos"/>, <paramref name="yPos"/>), <paramref name="width"/> x <paramref name="height"/>.</summary>
        void InitAt(int32_t xPos, int32_t yPos, int32_t width, int32_t height)
        {
            MCLogObject::Init(xPos, yPos, width, height);
        }

        void Draw() override
        {
            if (!_Port->ViewOpen())
            {
                return;
            }

            const MCStatus& status = MCUpdater::Status();
            DrawFrame(StatusText(status), status.Stage == MCStage::Ready || status.Stage == MCStage::Failed);
            // A room's players and match change without the updater: RESTART follows them every frame.
            ShowRestart(status.Stage);

            const int32_t textRight = ScrollBarLeft() - HeadingLeft;
            const std::string running = std::format(RunningTitle, MCUpdater::RunningVersion());
            const int32_t runningLeft = textRight - WhiteFont->Width(running);
            Write(HeadingLeft, TitleTop, std::format(FoundTitle, status.Version), TitleColor,
                  runningLeft - HeadingLeft);
            Write(runningLeft, TitleTop, running, TitleColor, textRight - runningLeft);

            const std::vector<std::string>& lines = Lines();
            const int32_t rows = std::min(VisibleRows(), RowCount() - FirstRow());

            for (int32_t shown = 0; shown < rows; shown++)
            {
                Write(HeadingLeft, RowsTop + shown * RowHeight + TextOffset,
                      lines[static_cast<size_t>(FirstRow() + shown)], NameColor, textRight - HeadingLeft);
            }
        }

        bool DrawsLive() override { return true; }

        /// <summary>The wheel, the keys and the bar scroll the lines; other keys go to the screen.</summary>
        void HandleEvent(MCGuiEvent& event) override
        {
            if (HandleScrollMouse(event))
            {
                return;
            }

            if (event.Type == MCGuiEventType::KeyDown && !ScrollByKey(event.Key))
            {
                PassToScreen(event);
            }
        }

        /// <summary>Back to the first line (the page opened).</summary>
        void Reset() { ResetScroll(); }

        /// <summary>What the update brings, wrapped to the list's width, in capitals.</summary>
        const std::vector<std::string>& Lines()
        {
            const std::string& notes = MCUpdater::Status().Notes;

            if (_Wrapped.has_value() && notes == *_Wrapped)
            {
                return _Lines;
            }

            _Wrapped = notes;
            _Lines.clear();
            const MCGuiFont& font = *WhiteFont;
            const int32_t width = ScrollBarLeft() - 2 * HeadingLeft;
            std::string upper = notes;
            std::ranges::transform(upper, upper.begin(), [](char c)
                                   { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });

            for (const auto paragraph : std::views::split(upper, '\n'))
            {
                std::string_view rest(paragraph.begin(), paragraph.end());

                if (rest.empty())
                {
                    _Lines.emplace_back();
                    continue;
                }

                while (!rest.empty())
                {
                    // A line ends at the last space that fits; a word too long for a line is cut.
                    int32_t fits = font.CharactersToWidth(rest, width, true);

                    if (fits <= 0)
                    {
                        fits = std::max(font.CharactersToWidth(rest, width, false), 1);
                    }

                    const std::string_view line = rest.substr(0, static_cast<size_t>(fits));
                    _Lines.emplace_back(line.substr(0, line.find_last_not_of(' ') + 1));
                    rest.remove_prefix(std::min(static_cast<size_t>(fits), rest.size()));
                    rest.remove_prefix(std::min(rest.find_first_not_of(' '), rest.size()));
                }
            }

            return _Lines;
        }

    protected:
        int32_t RowCount() override { return static_cast<int32_t>(Lines().size()); }

    private:
        /// <summary>The notes <see cref="_Lines"/> were wrapped from, once they have been.</summary>
        std::optional<std::string> _Wrapped;
        std::vector<std::string> _Lines;
    };

    /// <summary>
    /// Where the line under QUIT goes: the main menu's buttons are 29 rows apart, QUIT the one under ORIGINAL CINEMA,
    /// and the box has 23 rows under QUIT.
    /// </summary>
    constexpr int32_t MenuButtonStep = 29;
    constexpr int32_t RowsUnderQuit = 23;

    /// <summary>What the line under QUIT says while an update waits.</summary>
    std::string NoticeText(const MCStatus& status)
    {
        switch (status.Stage)
        {
            case MCStage::None:
                return {};
            case MCStage::Found:
                return std::format("VERSION {} IS OUT", status.Version);
            case MCStage::Downloading:
                return std::format("DOWNLOADING VERSION {}", status.Version);
            case MCStage::Ready:
                return status.GoesIn == MCUpdater::MCGoesIn::NextStart
                           ? std::format("VERSION {} IS IN", status.Version)
                           : std::format("VERSION {} IS READY TO INSTALL", status.Version);
            case MCStage::Installing:
                return std::format("INSTALLING VERSION {}", status.Version);
            case MCStage::Failed:
                return "THE UPDATE FAILED";
        }

        return {};
    }

    /// <summary>
    /// A screen's UPDATE page (the main menu's, and a lobby's session screen's): each shows the updater (the model);
    /// it goes with its screen.
    /// </summary>
    struct MCUpdatePageView
    {
        MCGuiHandle<MCLogGroupBox> Page;
        MCGuiHandle<MCLogUpdateNotes> Notes;
        MCGuiHandle<MCLogTextButton> InstallButton;
        MCGuiHandle<MCLogTextButton> SkipButton;
        MCGuiHandle<MCLogTextButton> RestartButton;
    };

    /// <summary>The pages there are (a gone screen's are dropped as they are next looked at).</summary>
    std::vector<MCUpdatePageView> Pages;
    /// <summary>The main menu: UPDATE shows its page.</summary>
    MCGuiHandle<MCGuiObject> Home;

    /// <summary>The pages whose screen is still there.</summary>
    std::vector<MCUpdatePageView>& LivePages()
    {
        std::erase_if(Pages, [](const MCUpdatePageView& view) { return view.Page == nullptr; });
        return Pages;
    }

    /// <summary><paramref name="screen"/>'s page, if it has one.</summary>
    MCOptionalRef<MCUpdatePageView> PageOn(const MCGuiObject& screen)
    {
        for (MCUpdatePageView& view : LivePages())
        {
            if (view.Page->ParentObject().get() == &screen)
            {
                return view;
            }
        }

        return std::nullopt;
    }

    /// <summary>
    /// The session screen's line: a field as the screen's own (the darkest grey, outlined as the page is), its text
    /// centred.
    /// </summary>
    constexpr uint8_t LineFieldColor = 0x10;
    constexpr uint8_t LineOutlineColor = 0x18;

    /// <summary>
    /// Port-only: the line saying an update waits (and which), in UPDATE's amber. On the main menu it sits in the box
    /// under QUIT, centred under the buttons, and the mouse goes through it; on the session screen of a game a lobby
    /// started (which never shows the main menu) it is a field of its own, and a click on it opens the UPDATE page over
    /// that screen. It draws every frame from the updater, and not while the page shows.
    /// </summary>
    class MCUpdateNotice : public MCLogObject
    {
    public:
        MCUpdateNotice(uint8_t color, bool opensPage, std::function<bool()> shown = nullptr)
            : _Color(color), _OpensPage(opensPage), _Shown(std::move(shown))
        {
        }

        void Draw() override
        {
            const std::string text = NoticeText(MCUpdater::Status());

            if (text.empty() || UpdatePageShown() || !Shown())
            {
                return;
            }

            const MCGuiFont& font = *WhiteFont;
            int32_t top = 0;

            if (_OpensPage)
            {
                const auto right = static_cast<int16_t>(Width() - 1);
                const auto bottom = static_cast<int16_t>(Height() - 1);
                FillBox(0, 0, right, bottom, LineOutlineColor);
                FillBox(1, 1, static_cast<int16_t>(right - 1), static_cast<int16_t>(bottom - 1), LineFieldColor);
                top = (Height() - font.Height()) / 2;
            }

            VfxStringDraw(Lport()->Frame(), (Width() - font.Width(text)) / 2, top, font.FontData.Bytes(), text,
                          FontColors(_Color));
        }

        bool DrawsLive() override { return true; }

        std::shared_ptr<MCGuiObject> FindObject(int32_t xPos, int32_t yPos) override
        {
            if (!_OpensPage || !MCUpdater::Waiting() || UpdatePageShown() || !Shown())
            {
                return nullptr;
            }

            return MCLogObject::FindObject(xPos, yPos);
        }

        void HandleEvent(MCGuiEvent& event) override
        {
            if (const std::shared_ptr<MCGuiObject> screen = ParentObject();
                _OpensPage && event.Type == MCGuiEventType::LeftButtonDown && screen != nullptr)
            {
                ShowUpdatePageOver(*screen);
                return;
            }

            MCLogObject::HandleEvent(event);
        }

    private:
        bool Shown() const { return !_Shown || _Shown(); }

        uint8_t _Color;
        bool _OpensPage;
        std::function<bool()> _Shown;
    };

    /// <summary>The UPDATE button.</summary>
    MCGuiHandle<MCLogButton> MenuButton;

    /// <summary>Shows <paramref name="button"/> when <paramref name="shown"/>, else hides it.</summary>
    void ShowButton(const MCGuiHandle<MCLogTextButton>& button, bool shown)
    {
        if (const std::shared_ptr<MCLogTextButton> made = button.Lock(); made != nullptr)
        {
            made->ShowGuiWindow(shown);
        }
    }

    void ShowRestart(MCStage stage)
    {
        // Only where what the game starts outlives it, and not for a setup waiting for the player's own start; in a
        // lobby's room, only when every game there can rejoin, before the match (network/MCLobbyUpdate.h).
        const bool shown = stage == MCStage::Ready && MCUpdater::CanStartAgain() &&
                           MCUpdater::Status().GoesIn != MCUpdater::MCGoesIn::PlayersStart &&
                           (!MCLobbyUpdate::InRoom() || MCLobbyUpdate::CanRestart());

        for (const MCUpdatePageView& view : LivePages())
        {
            ShowButton(view.RestartButton, shown);
        }
    }

    /// <summary>
    /// The updater changed: the UPDATE button lit (amber) while an update waits and grayed otherwise, the page's buttons
    /// for its stage: INSTALL also after a failed download, to try again.
    /// </summary>
    void Refresh()
    {
        if (const std::shared_ptr<MCLogButton> button = MenuButton.Lock(); button != nullptr)
        {
            button->Disabled = !MCUpdater::Waiting();
        }

        const MCStatus& status = MCUpdater::Status();

        for (const MCUpdatePageView& view : LivePages())
        {
            ShowButton(view.InstallButton,
                       status.Stage == MCStage::Found || (status.Stage == MCStage::Failed && status.Retry));
            ShowButton(view.SkipButton, status.Stage == MCStage::Found);
        }

        ShowRestart(status.Stage);
    }

    /// <summary>INSTALL: the download starts (after a failure, once a fresh check finds the update again).</summary>
    void Install()
    {
        MCUpdater::Answer(MCAnswer::Install);
    }

    /// <summary>RESTART: the game quits so the update installs; in a lobby's room, it comes back to the room.</summary>
    void Restart()
    {
        if (!MCLobbyUpdate::Restart())
        {
            MCUpdater::Answer(MCAnswer::Install);
        }
    }

    /// <summary>SKIP: this version is never offered again, and the page closes.</summary>
    void Skip()
    {
        MCUpdater::Answer(MCAnswer::Skip);
        HideUpdatePage();
    }

    /// <summary>Adds a page (hidden) to <paramref name="screen"/>: its list, INSTALL, SKIP, RESTART and DONE.</summary>
    MCUpdatePageView& AddPageTo(MCGuiObject& screen)
    {
        constexpr std::array<MCPageButton, 3> buttons = {
            MCPageButton{InstallLabel, Install},
            MCPageButton{SkipLabel, Skip},
            MCPageButton{RestartLabel, Restart},
        };

        std::vector<MCGuiHandle<MCLogTextButton>> made;
        MCUpdatePageView& view = LivePages().emplace_back();
        view.Page = AddPrefsPage(screen, Title, buttons, HideUpdatePage, made);
        view.InstallButton = made.at(0);
        view.SkipButton = made.at(1);
        view.RestartButton = made.at(2);

        // RESTART takes INSTALL's place: they never show together.
        view.RestartButton->MoveTo(view.InstallButton->X(), view.InstallButton->Y());
        view.Notes = view.Page->CreateChild<MCLogUpdateNotes>(
            [](MCLogUpdateNotes& notes)
            {
                notes.InitAt(PageListLeft, PageListTop, PageListWidth, PageListHeight);
                notes.ShowGuiWindow(true);
            });

        return view;
    }

    /// <summary>Shows <paramref name="view"/>'s page from its first line, the buttons as the updater stands.</summary>
    void ShowPage(const MCUpdatePageView& view)
    {
        if (const std::shared_ptr<MCLogUpdateNotes> notes = view.Notes.Lock(); notes != nullptr)
        {
            notes->Reset();
        }

        view.Page->ShowGuiWindow(true);
        view.Page->BringToFront(true);
        Refresh();
    }
}

MCMenuButtonPictures UpdateButtonPictures(std::span<const uint8_t> palette)
{
    std::map<int32_t, MCMenuWord> words;
    const std::array<uint8_t, 256> amber = AmberTable(palette);
    return {.Up = Recoloured(*WordFace(UpState, UpdateWord, palette, words), amber),
            .Down = Recoloured(*WordFace(DownState, UpdateWord, palette, words), amber),
            .Gray = WordFace(GrayState, UpdateWord, palette, words)};
}

void AddUpdatePage(MCGenericScreen& mainScreen)
{
    const std::shared_ptr<MCLogButton> cinema = mainScreen.CinemaButton.Lock();

    if (cinema == nullptr)
    {
        SDL_Log("Updater: the main menu has no ORIGINAL CINEMA button to make UPDATE");
        return;
    }

    const MCMenuButtonPictures pictures = UpdateButtonPictures(mainScreen.Palette);
    cinema->UpPicture = pictures.Up;
    cinema->OverPicture = pictures.Up;
    cinema->DownPicture = pictures.Down;
    cinema->GrayPicture = pictures.Gray;
    cinema->Callback().SetExec(ShowUpdatePage);
    MenuButton = cinema;

    // The line under QUIT, in amber at its brightest.
    mainScreen.CreateChild<MCUpdateNotice>(
        [&](MCUpdateNotice& notice)
        {
            const int32_t noticeHeight = WhiteFont->Height();
            notice.Init(cinema->X(), cinema->Y() + 2 * MenuButtonStep + (RowsUnderQuit - noticeHeight) / 2,
                        cinema->Width(), noticeHeight);
            notice.SetTransparent(true);
            notice.ShowGuiWindow(true);
        },
        Nearest(mainScreen.Palette, Amber), false);

    AddPageTo(mainScreen);
    Home = mainScreen.weak_from_this();
    MCUpdater::SetListener(Refresh);
    Refresh();
}

MCGuiHandle<MCLogObject> AddUpdateLine(MCGuiObject& screen, std::span<const uint8_t> palette, int32_t xPos,
                                       int32_t yPos, int32_t width, int32_t height, std::function<bool()> shown)
{
    return screen.CreateChild<MCUpdateNotice>(
        [&](MCUpdateNotice& line)
        {
            line.Init(xPos, yPos, width, height);
            line.SetTransparent(true);
            line.ShowGuiWindow(true);
        },
        Nearest(palette, Amber), true, std::move(shown));
}

void ShowUpdatePageOver(MCGuiObject& screen)
{
    const MCOptionalRef<MCUpdatePageView> shown = PageOn(screen);
    ShowPage(shown ? shown->get() : AddPageTo(screen));
}

std::shared_ptr<MCGuiObject> UpdatePageFind(const MCGuiObject& screen, int32_t xPos, int32_t yPos)
{
    const MCOptionalRef<MCUpdatePageView> view = PageOn(screen);

    if (!view || !view->get().Page->IsShowing())
    {
        return nullptr;
    }

    // The page takes every click on it, even between its buttons, so none reaches the screen's controls under it.
    const std::shared_ptr<MCLogGroupBox> page = view->get().Page.Lock();
    std::shared_ptr<MCGuiObject> found = page->FindObject(xPos, yPos);
    return found != nullptr || !page->PointInside(xPos, yPos) ? found : page;
}

void ShowUpdatePage()
{
    // UPDATE: the main menu's page.
    if (const std::shared_ptr<MCGuiObject> home = Home.Lock(); home != nullptr)
    {
        ShowUpdatePageOver(*home);
    }
}

void HideUpdatePage()
{
    for (const MCUpdatePageView& view : LivePages())
    {
        view.Page->ShowGuiWindow(false);
    }
}

bool UpdatePageShown()
{
    // A page on a screen that isn't showing (the session screen as the match starts) isn't.
    return std::ranges::any_of(LivePages(),
                               [](const MCUpdatePageView& view)
                               {
                                   const std::shared_ptr<MCGuiObject> screen = view.Page->ParentObject();
                                   return view.Page->IsShowing() && (screen == nullptr || screen->IsShowing());
                               });
}

std::string UpdatePageStatus()
{
    return StatusText(MCUpdater::Status());
}

std::vector<std::string> UpdatePageLines()
{
    // Every page shows the same lines (the updater's notes, wrapped to the same width).
    for (const MCUpdatePageView& view : LivePages())
    {
        if (const std::shared_ptr<MCLogUpdateNotes> notes = view.Notes.Lock(); notes != nullptr)
        {
            return notes->Lines();
        }
    }

    return {};
}
