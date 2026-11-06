#include "stdafx.h"
#include "logistics/MCLogDialogBox.h"
#include "gui/MCUpdateDisplay.h"
#include "vfx/MCVfxFunctions.h"

auto MCLogDialogBox::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height) -> void
{
    TwoButton = true;
    Spinner = true;
    Callback = nullptr;
    PicturePort.reset();
    // The original loaded the box's frame (lspcb00) as its port; the box draws the frame each frame instead.
    MCLogObject::Init(xPos, yPos, width, height);
    SetTransparent(true);
    ShowGuiWindow(false);
}

auto MCLogDialogBox::OnDestroy() -> void
{
    PicturePort.reset();
    Callback = nullptr;
    MCLogObject::OnDestroy();
}

auto MCLogDialogBox::DrawBackground() -> void
{
    if (NeedBackground)
    {
        // The original copied the screen under the box into a picture here and darkened it, then filled it with
        // 0x10, which is all the box shows of it.
        GuiSystem().SetCursorVisible(false);
        UpdateDisplay(false, false, 0, false, 0);
        GuiSystem().SetCursorVisible(true);
        NeedBackground = false;
    }

    Pressed = PressedPart::None;
}

auto MCLogDialogBox::Draw() -> void
{
    DrawBox();
    DrawPressed();
}

auto MCLogDialogBox::DrawPressed() -> void
{
    // Each part's pressed art and where it goes.
    struct PressedArt
    {
        std::string_view Name;
        int32_t X = 0;
        int32_t Y = 0;
    };

    static constexpr std::array<PressedArt, 4> arts = {{{"lspcb03.tga", 0x3f, 0x82},
                                                        {"lspcb04.tga", 0x76, 0x82},
                                                        {"lspcb07.tga", 0x92, 0x53},
                                                        {"lspcb09.tga", 0x92, 0x5b}}};

    if (Pressed == PressedPart::None)
    {
        return;
    }

    const PressedArt& art = arts[static_cast<size_t>(Pressed) - 1];
    LogScreenArt(art.Name)->CopyTo(_Port->Frame(), art.X, art.Y, true);
}

auto MCLogDialogBox::DrawBox() -> void
{
    MCPane& port = _Port->Frame();
    // The box was its frame's picture: the fill covers the frame, not the whole pane.
    const std::shared_ptr<MCLogPort> frameArt = LogScreenArt("lspcb00.tga");
    MCPane fill = port;
    fill.X1 = fill.X0 + frameArt->Width() - 1;
    fill.Y1 = fill.Y0 + frameArt->Height() - 1;
    VfxPaneWipe(fill, 0x10);
    frameArt->CopyTo(port, 0, 0, true);

    // Without a spinner its place is left as the frame is (the original copied a transparent block there).
    if (Spinner)
    {
        VfxPaneCopy(LogScreenArt("lspcb05.tga")->Frame(), 0, 0, port, 0x92, 0x53, -1);
        VfxPaneCopy(LogScreenArt("lspcb06.tga")->Frame(), 0, 0, port, 0x92, 0x5b, -1);
    }

    LogScreenArt("lspcb01.tga")->CopyTo(port, 0x3f, 0x82, true);

    if (TwoButton)
    {
        LogScreenArt("lspcb02.tga")->CopyTo(port, 0x76, 0x82, true);
    }

    if (PicturePort != nullptr)
    {
        PicturePort->CopyTo(port, 10, 0x1b, true);
    }
}

auto MCLogDialogBox::Activate() -> void
{
    NeedBackground = true;
    GuiSystem().Grab(weak_from_this());
    BringToFront(false);
    DrawBackground();
    ShowGuiWindow(true);
}

auto MCLogDialogBox::Deactivate(int32_t dialogResult) -> void
{
    GuiSystem().Release();
    ShowGuiWindow(false);

    if (Callback)
    {
        Callback(dialogResult);
    }
}
