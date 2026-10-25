#include "stdafx.h"
#include "logistics/MCLogTextButton.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "logistics/MCLogLabel.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The ACCEPT button's pictures: up (also lit under the mouse), pressed and grayed.</summary>
    constexpr std::string_view UpArt = "bh_accept.tga";
    constexpr std::string_view DownArt = "bg_accept.tga";
    constexpr std::string_view GrayArt = "bn_accept.tga";

    /// <summary>
    /// The face inside the bevel, where the art's word is (inclusive). Each of its rows is one colour, read at
    /// <see cref="FaceSampleX"/>, left of the word.
    /// </summary>
    constexpr int32_t FaceLeft = 3;
    constexpr int32_t FaceRight = 59;
    constexpr int32_t FaceTop = 2;
    constexpr int32_t FaceBottom = 11;
    constexpr int32_t FaceSampleX = 5;

    /// <summary>The word's colours, as the art's: dark on the face, light pressed, faint grayed.</summary>
    constexpr uint8_t UpTextColor = 0x10;
    constexpr uint8_t DownTextColor = 0x1f;
    constexpr uint8_t GrayTextColor = 0x16;

    /// <summary>The sounds of the preferences screen's buttons (its fit's OverSFX and PressSFX).</summary>
    constexpr uint32_t ButtonOverSound = 16;
    constexpr uint32_t ButtonPressSound = 15;

    /// <summary>The art <paramref name="art"/> with its word wiped and <paramref name="label"/> written, centred, in <paramref name="textColor"/>.</summary>
    std::shared_ptr<MCLogPort> LabelledFace(std::string_view art, std::string_view label, uint8_t textColor)
    {
        const std::shared_ptr<MCLogPort> source = LogArt(art);
        auto face = std::make_shared<MCLogPort>();
        face->Init(source->Width(), source->Height());
        VfxPaneCopy(source->Frame(), 0, 0, face->Frame(), 0, 0, VfxNoColor);
        const std::span<const uint8_t> pixels = source->Pixels();
        const int32_t bottom = std::min(FaceBottom, source->Height() - 1);

        for (int32_t y = FaceTop; y <= bottom; y++)
        {
            MCPane row = face->Frame();
            row.X0 = FaceLeft;
            row.X1 = std::min(FaceRight, source->Width() - 1);
            row.Y0 = y;
            row.Y1 = y;
            VfxPaneWipe(row, pixels[static_cast<size_t>(y * source->Width() + FaceSampleX)]);
        }

        const MCGuiFont& font = *WhiteFont;
        VfxStringDraw(face->Frame(), (face->Width() - font.Width(label)) / 2, (face->Height() - font.Height()) / 2,
                      font.FontData.Bytes(), label, FontColors(textColor));
        return face;
    }
}

auto MCLogTextButton::InitWithLabel(int32_t xPos, int32_t yPos, std::string_view label, std::function<void()> clicked)
    -> void
{
    _Label = label;
    const std::shared_ptr<MCLogPort> up = LabelledFace(UpArt, label, UpTextColor);
    Init(xPos, yPos, up->Width(), up->Height(), label);
    UpPicture = up;
    OverPicture = up;
    DownPicture = LabelledFace(DownArt, label, DownTextColor);
    GrayPicture = LabelledFace(GrayArt, label, GrayTextColor);
    BackgroundColor = 0xff;
    // The art's corners are the key colour: keyed out, as the screens' own buttons are.
    SetTransparent(true);
    OverSound = ButtonOverSound;
    PressSound = ButtonPressSound;
    Callback().SetExec(std::move(clicked));
}
