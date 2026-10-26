#include "stdafx.h"
#include "gui/MCScrollPane.h"
#include "gui/MCGuiFont.h"
#include "lib/MCFatal.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The arrows' repeat timer (200 ms).</summary>
    constexpr int32_t ArrowTimer = 6;
}

MCScrollPane::~MCScrollPane()
{
    MCScrollPane::Destroy();
}

auto MCScrollPane::Init(int32_t width, int32_t height, int32_t xPos, int32_t yPos, std::string_view name) -> int32_t
{
    if (name.empty())
    {
        Init(width, height, xPos, yPos, std::nullopt);
        return 0;
    }

    MCLogPort background;
    background.Load(name);
    Init(width, height, xPos, yPos, std::cref(background));
    return 0;
}

auto MCScrollPane::Init(int32_t width, int32_t height, int32_t xPos, int32_t yPos,
                        MCOptionalRef<const MCLogPort> background) -> void
{
    _Port.reset();
    FramePane = nullptr;

    ContentPort = std::make_shared<MCLogPort>();
    ContentPort->Init(width - SliderWidth, height);
    VfxPaneWipe(ContentPort->Frame(), 0x10);

    if (background.has_value())
    {
        const MCLogPort& source = background->get();
        BackgroundCopy = std::make_shared<MCLogPort>();
        BackgroundCopy->Init(source.Width(), source.Height());
        source.CopyTo(BackgroundCopy->Frame(), 0, 0, true);
    }

    // The pane draws into its content.
    InitWithoutPort(xPos, yPos, width, height);
    _Port = ContentPort;

    // The track tile repeats down the column, below the first row; the arrows go at the ends.
    ColumnArts = {ColumnArt{"scroll.tga", "supbup.tga", "sdnbup.tga"}};
    ColumnHeight = height;

    if (PanePort == nullptr)
    {
        PanePort = std::make_shared<MCLogPort>();
    }

    PanePort->InitView(width, height);
    SetUpSlider();
    SetScrollPos(0.0f);
    ShowGuiWindow(false);
    SetDepth(100);
}

auto MCScrollPane::Destroy() -> void
{
    _Port.reset();
    MCLogObject::Destroy();
    ContentPort.reset();
    ColumnArts.clear();
    FreeSlider();
    BackgroundCopy.reset();
    PanePort.reset();
}

auto MCScrollPane::SetScrollPos(float position) -> void
{
    ScrollPos = position;

    if (MaxScroll < position)
    {
        ScrollPos = MaxScroll;
    }

    if (ScrollPos < 0.0f)
    {
        ScrollPos = 0.0f;
    }

    if (SliderHeight != 0)
    {
        const auto newSliderPos =
            static_cast<int32_t>((WinHeight - 2 * ArrowHeight) * static_cast<double>(0.01f) * ScrollPos + ArrowHeight);
        SetChildren();
        SliderPos = newSliderPos;
    }
}

auto MCScrollPane::SetSliderPos(int32_t position) -> void
{
    if (SliderHeight == 0)
    {
        return;
    }

    position = std::max(std::min(position, SliderMax), ArrowHeight);
    SliderPos = position;
    const auto newScrollPos = static_cast<float>(static_cast<double>(position - ArrowHeight) /
                                                 static_cast<double>(WinHeight - 2 * ArrowHeight) * 100.0);
    ScrollPos = std::min(newScrollPos, MaxScroll);
    SetChildren();
}

auto MCScrollPane::SetChildren() -> void
{
    for (int32_t i = 0; i < NumberOfChildren(); i++)
    {
        std::shared_ptr<MCGuiObject> child = Child(i);
        child->MoveTo(child->X(), LastScrollOffset - GetScrollOffset() + child->Y());
    }

    LastScrollOffset = GetScrollOffset();
}

auto MCScrollPane::Draw() -> void
{
    MCPane& view = PanePort->Frame();

    if (BackgroundCopy != nullptr)
    {
        BackgroundCopy->CopyTo(view, 0, 0, true);
    }

    if (ContentPort != nullptr)
    {
        const auto offset = static_cast<int32_t>(-(static_cast<double>(ScrollPos) * ScrollUnit));

        if (ContentPort->IsView())
        {
            const MCView& pane = PanePort->View();
            ContentPort->OpenView(pane.Target, pane.OriginX, pane.OriginY + offset, pane.Scissor, true);
            DrawContent();
            ContentPort->CloseView();
        }
        else
        {
            ContentPort->CopyTo(view, 0, offset, true);
        }
    }

    DrawSliderColumn(view, WinWidth - SliderWidth, 0, true);
}

auto MCScrollPane::DrawContent() -> void
{
    if (ContentPort->DrawContent)
    {
        ContentPort->DrawContent(*ContentPort);
    }
}

auto MCScrollPane::DrawContentTo(const MCPane& target, int32_t xPos, int32_t yPos) -> void
{
    if (ContentPort == nullptr)
    {
        return;
    }

    if (!ContentPort->IsView())
    {
        VfxPaneCopy(ContentPort->Frame(), 0, GetScrollOffset(), target, xPos, yPos, -1);
        return;
    }

    const auto offset = static_cast<int32_t>(-(static_cast<double>(ScrollPos) * ScrollUnit));
    const MCRect scissor{target.X0 + xPos, target.Y0 + yPos, target.X1, target.Y1};
    ContentPort->OpenView(target.Window.lock(), target.X0 + xPos, target.Y0 + yPos + offset, scissor, false);
    DrawContent();
    ContentPort->CloseView();
}

auto MCScrollPane::Display() -> void
{
    if (!IsShowing())
    {
        return;
    }

    // The original copied its pieces over what the parent had shown there, and showed no children.
    DrawInFramePass(*PanePort, 0, false, false);
}

auto MCScrollPane::HeldArrow() const -> Arrow
{
    // An arrow shows held only while the content scrolls (the original put its art in the column when it erased
    // the slider, and there is none to erase when the content fits).
    if (SliderHeight == 0 || !GuiSystem().Grabs(*this))
    {
        return Arrow::None;
    }

    return _ArrowPressed;
}

auto MCScrollPane::PressedArrowArt(bool down) -> std::shared_ptr<MCLogPort>
{
    return LogScreenArt(down ? "lscsb04.tga" : "lscsb03.tga");
}

auto MCScrollPane::DrawSliderColumn(const MCPane& target, int32_t xPos, int32_t yPos, bool keyed) -> void
{
    const int key = keyed ? -1 : 0;

    // The column, drawn in place: the original's picture of it started zeroed, and its art was copied in keyed (so it
    // held no key colour, and copied keyed or not alike).
    {
        MCLogBlockPort column(target, xPos, yPos, SliderWidth, ColumnHeight, false);
        VfxPaneWipe(column.Frame(), 0);

        for (const ColumnArt& art : ColumnArts)
        {
            const std::shared_ptr<MCLogPort> track = LogScreenArt(art.Track);
            const int32_t tiles = ColumnHeight / track->Height() - 1;

            for (int32_t i = 0; i < tiles; i++)
            {
                track->CopyTo(column.Frame(), 0, track->Height() * i + 1, true);
            }

            LogScreenArt(art.UpArrow)->CopyTo(column.Frame(), 0, 0, true);
            LogScreenArt(art.DownArrow)->CopyTo(column.Frame(), 0, ColumnHeight - ArrowArtHeight, true);
        }
    }

    const Arrow held = HeldArrow();

    if (held == Arrow::Up)
    {
        if (const std::shared_ptr<MCLogPort> art = PressedArrowArt(false); art != nullptr)
        {
            art->CopyTo(target, xPos, yPos, key);
        }
    }
    else if (held == Arrow::Down)
    {
        if (const std::shared_ptr<MCLogPort> art = PressedArrowArt(true); art != nullptr)
        {
            art->CopyTo(target, xPos, yPos + Height() - art->Height(), key);
        }
    }

    if (const std::shared_ptr<MCTexture> slider = SliderTexture.lock();
        SliderHeight > 0 && slider != nullptr && SliderWindow != nullptr)
    {
        // The slider image is a 13-wide picture of whole rows.
        VfxPaneCopy(MCPane::Over(SliderWindow), 0, 0, target, xPos, yPos + SliderPos, -1);
    }
}

auto MCScrollPane::MakeSliderTexture() -> void
{
    MCRenderer::DestroyTexture(SliderTexture);
    SliderTexture = MCRenderer::CreateTexture(*SliderWindow, MCTextureUse::Static);
}

auto MCScrollPane::FreeSlider() -> void
{
    MCRenderer::DestroyTexture(SliderTexture);
    SliderWindow.reset();
}

auto MCScrollPane::SetUpSlider() -> void
{
    const int32_t paneHeight = WinHeight;

    if (ContentPort->Height() <= paneHeight)
    {
        SliderHeight = 0;
        return;
    }

    // The slider's share of the track (the column less its two arrows) is the pane's share of the content.
    const auto paneHeightF = static_cast<float>(paneHeight);
    SliderHeight = std::max(
        static_cast<int32_t>(static_cast<double>(paneHeightF) / ContentPort->Height() * (paneHeight - 2 * ArrowHeight)),
        3);
    SliderPos = ArrowHeight;

    // Every row: dark edges, a light left bevel, a mid fill and a shadowed right bevel; the top row has a highlight
    // and the bottom row a shadow.
    const uint8_t edge = SliderEdgeColor();
    const std::array<uint8_t, SliderWidth> sliderRow = {edge, 0x10, 0x1c, 0x1a, 0x1a, 0x1a, 0x1a,
                                                        0x1a, 0x1a, 0x1a, 0x17, 0x10, edge};
    FreeSlider();
    SliderWindow = MCWindow::Make(SliderWidth, SliderHeight);
    const std::span<uint8_t> image = SliderWindow->Bytes();

    for (int32_t row = 0; row < SliderHeight; row++)
    {
        std::ranges::copy(sliderRow, image.begin() + row * SliderWidth);
    }

    std::fill_n(image.begin() + 3, 8, uint8_t{0x1c});
    std::fill_n(image.end() - 11, 9, uint8_t{0x17});
    MakeSliderTexture();
}

auto MCScrollPane::SetDisplayPort(std::shared_ptr<MCLogPort> port, bool resetPosition) -> void
{
    ShowContent(std::move(port), resetPosition);
}

auto MCScrollPane::ClearDisplayPort() -> void
{
    ContentPort.reset();
    _Port.reset();
}

auto MCScrollPane::ShowContent(std::shared_ptr<MCLogPort> port, bool resetPosition) -> void
{
    if (port == nullptr)
    {
        ClearDisplayPort();
        return;
    }

    ContentPort = std::move(port);
    _Port = ContentPort;
    const std::shared_ptr<MCLogPort>& shown = ContentPort;
    const auto unit = static_cast<float>(shown->Height() * 0.01);
    ScrollUnit = unit;
    MaxScroll = static_cast<float>(shown->Height() - WinHeight) / unit;
    float newScrollPos = 0.0f;

    if (!resetPosition && Height() <= shown->Height())
    {
        newScrollPos = static_cast<float>(LastScrollOffset) / static_cast<float>(shown->Height()) * 100.0f;
    }

    LastScrollOffset = 0;
    SetUpSlider();
    SetScrollPos(0.0f);
    SliderMax = WinHeight - SliderHeight - ArrowHeight;

    if (newScrollPos != 0.0f)
    {
        SetScrollPos(newScrollPos);
    }
}

auto MCScrollPane::Lport() const -> std::shared_ptr<MCLogPort>
{
    return ContentPort;
}

auto MCScrollPane::ForwardToChildren(MCGuiEvent& event) -> void
{
    if (GlobalX() - 14 + WinWidth <= event.X)
    {
        return;
    }

    for (int32_t i = 0; i < NumberOfChildren(); i++)
    {
        std::shared_ptr<MCGuiObject> child = Child(i);

        if (child != nullptr && child->GlobalX() <= event.X && event.X <= child->GlobalX() + child->Width() &&
            child->GlobalY() <= event.Y && event.Y <= child->Height() + child->GlobalY())
        {
            child->HandleEvent(event);
            return;
        }
    }
}

auto MCScrollPane::ScrollToRow(int32_t row) -> void
{
    const auto rowTop = static_cast<float>(Child(0)->Height() * row);
    SetScrollPos(rowTop / static_cast<float>(ContentPort->Height()) * 100.0f);
}

auto MCScrollPane::NextRow() -> int32_t
{
    const int32_t rowHeight = Child(0)->Height();
    const int32_t offset = GetScrollOffset();
    return offset % rowHeight == 0 ? offset / rowHeight + 1 : offset / rowHeight + 2;
}

auto MCScrollPane::HandleEvent(MCGuiEvent& event) -> void
{
    if (ContentPort == nullptr)
    {
        return;
    }

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            if (event.X <= GlobalX() - SliderWidth + WinWidth || GlobalX() + WinWidth <= event.X)
            {
                // Outside the slider column: the first child whose rows hold the mouse.
                for (int32_t i = 0; i < NumberOfChildren(); i++)
                {
                    std::shared_ptr<MCGuiObject> child = Child(i);

                    if (child->GlobalY() <= event.Y && event.Y <= child->GlobalY() + child->Height())
                    {
                        child->HandleEvent(event);
                        return;
                    }
                }

                return;
            }

            GuiSystem().Grab(weak_from_this());
            const int32_t mouseY = event.Y;

            if (mouseY < GlobalY() + ArrowHeight)
            {
                // The up arrow.
                _ArrowPressed = Arrow::Up;
                GuiSystem().AddTimer(weak_from_this(), ArrowTimer, 200, 0, 0, false);

                if (Child(0) == nullptr)
                {
                    SetSliderPos(SliderPos - BlackFont->Height());
                    return;
                }

                const int32_t rowHeight = Child(0)->Height();
                const int32_t offset = GetScrollOffset();
                ScrollToRow(offset % rowHeight == 0 ? offset / rowHeight - 1 : offset / rowHeight);
                return;
            }

            if (mouseY > GlobalY() - ArrowHeight + WinHeight)
            {
                // The down arrow.
                GuiSystem().AddTimer(weak_from_this(), ArrowTimer, 200, 0, 0, false);

                if (Child(0) == nullptr)
                {
                    SetSliderPos(BlackFont->Height() + SliderPos);
                }
                else
                {
                    ScrollToRow(NextRow());
                }

                _ArrowPressed = Arrow::Down;
                return;
            }

            // The track: grab the slider, or page toward the click.
            if (GlobalY() + SliderPos < mouseY && mouseY < GlobalY() + SliderHeight + SliderPos)
            {
                _DragY = mouseY;
                _DraggingSlider = true;
                return;
            }

            if (GlobalY() + ArrowHeight + SliderPos <= mouseY)
            {
                SetSliderPos(SliderHeight + SliderPos);
            }
            else
            {
                SetSliderPos(SliderPos - SliderHeight);
            }

            return;
        }

        case MCGuiEventType::LeftButtonUp:
        {
            GuiSystem().RemoveTimer(*this, ArrowTimer);

            if (GuiSystem().GrabbedObject() != nullptr)
            {
                GuiSystem().Release();
                _DraggingSlider = false;
                _DragY = -1;
            }

            // OB-131 (fixed): the original let go of the arrow only on a release over the column. Let go elsewhere, it
            // stayed pressed on screen and the slider could not be dragged until an arrow was released on the column.
            const Arrow released = _ArrowPressed;
            _ArrowPressed = Arrow::None;

            if (GlobalX() - 14 + WinWidth <= event.X)
            {
                if (released != Arrow::None)
                {
                    SetScrollPos(ScrollPos);
                }

                return;
            }

            ForwardToChildren(event);
            return;
        }

        case MCGuiEventType::MouseMove:
        {
            if (GuiSystem().GrabbedObject() != nullptr)
            {
                if (_ArrowPressed != Arrow::None || !_DraggingSlider)
                {
                    return;
                }

                SetSliderPos(SliderPos - _DragY + event.Y);
                _DragY = event.Y;
                return;
            }

            ForwardToChildren(event);
            return;
        }

        case MCGuiEventType::KeyUp:
        case MCGuiEventType::KeyDown:
        {
            ParentObject()->HandleEvent(event);
            return;
        }

        case MCGuiEventType::Timer:
        {
            // The arrow timer repeats the step.
            if (event.Y < GlobalY() + ArrowHeight)
            {
                if (Child(0) == nullptr)
                {
                    SetSliderPos(SliderPos - BlackFont->Height());
                    return;
                }

                const int32_t rowHeight = Child(0)->Height();
                const int32_t offset = GetScrollOffset();
                ScrollToRow(std::max(offset / rowHeight + (offset % rowHeight == 0 ? -1 : -2), 0));
                return;
            }

            if (Child(0) == nullptr)
            {
                SetSliderPos(BlackFont->Height() + SliderPos);
                return;
            }

            ScrollToRow(NextRow());
            return;
        }

        default:
        {
            ForwardToChildren(event);
            return;
        }
    }
}

auto MCScrollPane::MouseWheel(int32_t steps, int32_t, int32_t) -> bool
{
    if (ContentPort == nullptr || SliderHeight == 0)
    {
        return false;
    }

    for (; steps != 0; steps += steps < 0 ? 1 : -1)
    {
        if (Child(0) == nullptr)
        {
            SetSliderPos(SliderPos + (steps < 0 ? -BlackFont->Height() : BlackFont->Height()));
            continue;
        }

        // To the row boundary above or below the top of the view. The percent position can leave the offset a pixel
        // short of a boundary, so going down counts that pixel as the boundary.
        const int32_t rowHeight = Child(0)->Height();
        const int32_t offset = GetScrollOffset();
        int32_t row;

        if (steps < 0)
        {
            row = offset % rowHeight == 0 ? offset / rowHeight - 1 : offset / rowHeight;
        }
        else
        {
            row = (offset + 1) / rowHeight + 1;
        }

        ScrollToRow(row);
    }

    return true;
}

auto MCScrollPane::GetScrollOffset() const -> int32_t
{
    return static_cast<int32_t>(static_cast<double>(ScrollPos) * ScrollUnit);
}

auto MCScrollPane::GetScrollBottom() -> int32_t
{
    return static_cast<int32_t>(Height() + static_cast<double>(ScrollPos) * ScrollUnit);
}
