#include "stdafx.h"
#include "logistics/MCLogTextButton.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "logistics/MCLogLabel.h"
#include "vfx/MCVfxFunctions.h"

namespace
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

    /// <summary>The sounds of the preferences screen's buttons (its fit's OverSFX and PressSFX).</summary>
    constexpr uint32_t ButtonOverSound = 16;
    constexpr uint32_t ButtonPressSound = 15;

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
            VfxStringDraw(face->Frame(), (face->Width() - font.Width(label)) / 2, (face->Height() - font.Height()) / 2,
                          font.FontData.Bytes(), label, FontColors(textColor));
        }

        return face;
    }
}

MCButtonPictures MCTextButtonPictures(std::string_view label)
{
    return {.Up = LabelledFace(UpState, label, UpTextColor),
            .Down = LabelledFace(DownState, label, DownTextColor),
            .Gray = LabelledFace(GrayState, label, GrayTextColor)};
}

auto MCLogTextButton::InitWithLabel(int32_t xPos, int32_t yPos, std::string_view label, std::function<void()> clicked)
    -> void
{
    if (std::ranges::find(MCTextButtonLabels, label) == MCTextButtonLabels.end())
    {
        SDL_Log("Text button %.*s: not in MCTextButtonLabels, so the HD art pack has no copy of it",
                static_cast<int>(label.size()), label.data());
    }

    _Label = label;
    MCButtonPictures pictures = MCTextButtonPictures(label);
    Init(xPos, yPos, pictures.Up->Width(), pictures.Up->Height(), label);
    UpPicture = pictures.Up;
    OverPicture = std::move(pictures.Up);
    DownPicture = std::move(pictures.Down);
    GrayPicture = std::move(pictures.Gray);
    BackgroundColor = 0xff;
    // The art's corners are the key colour: keyed out, as the screens' own buttons are.
    SetTransparent(true);
    OverSound = ButtonOverSound;
    PressSound = ButtonPressSound;
    Callback().SetExec(std::move(clicked));
}
