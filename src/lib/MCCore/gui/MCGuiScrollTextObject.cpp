#include "stdafx.h"
#include "gui/MCGuiScrollTextObject.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCRenderer.h"
#include "sprite/MCHdArt.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The gap the section highlight leaves at the right.</summary>
    constexpr int32_t HighlightRightGap = 3;

    /// <summary>The thumb's track: the object's height less 16 pixels above and below.</summary>
    constexpr int32_t TrackInset = 0x10;

    /// <summary>The font row (the <c>Fonts</c> table's first index) a line of colour <paramref name="color"/> is drawn in.</summary>
    int32_t FontRowForColor(uint8_t color)
    {
        switch (color)
        {
            case 0x10:
                return 0;
            case 0xef:
                return 1;
            case 0xf2:
                return 2;
            case 0x0b:
                return 3;
            case 0x0c:
                return 4;
            case 0x19:
                return 5;
            case 0x1f:
                return 6;
            default:
                return 7;
        }
    }
}

auto PaintScrollTab(MCGuiObject& obj) -> void
{
    PaintScrollTabBevel(obj.Port().Frame(), obj.Width(), obj.Height());
}

namespace
{
    /// <summary>The scroll thumb's colours: its face, its lit edges (top, left) and its shaded ones (right, bottom).</summary>
    constexpr uint8_t TabFace = 0x1a;
    constexpr uint8_t TabLit = 0x1f;
    constexpr uint8_t TabShaded = 0x16;
    /// <summary>Port-only: the HD thumb's pixels per original pixel, and how many sizes' frames are kept.</summary>
    constexpr int32_t TabHdScale = 4;
    constexpr size_t TabFramesKept = 8;
    /// <summary>
    /// Port-only: how much lighter the HD thumb's face is at its top than its colour, and darker at its bottom (a share of
    /// the way to white and to black): a face lit from above, as the HD menus' grey buttons are.
    /// </summary>
    constexpr float TabFaceRamp = 0.12f;

    /// <summary>Port-only: an HD thumb's frame, by its size and the three colours it was made in.</summary>
    struct TabFrame
    {
        int32_t Width = 0;
        int32_t Height = 0;
        std::array<SDL_Color, 3> Colours{};
        std::shared_ptr<const MCHdArt::Frame> Frame;
    };

    bool SameColour(const SDL_Color& a, const SDL_Color& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }

    /// <summary>
    /// Port-only: the HD frame of a thumb <paramref name="width"/> x <paramref name="height"/> in
    /// <paramref name="colours"/> (face, lit, shaded), each original pixel <see cref="TabHdScale"/> square: the edges where
    /// the bevel's lines lie, the face a smooth ramp from lighter at the top to darker at the bottom.
    /// </summary>
    std::shared_ptr<const MCHdArt::Frame> MakeTabFrame(int32_t width, int32_t height,
                                                       const std::array<SDL_Color, 3>& colours)
    {
        constexpr size_t RgbaBytes = 4;
        constexpr uint8_t Opaque = 255;
        constexpr float Full = 255.0f;
        auto made = std::make_shared<MCHdArt::Frame>();
        made->Id = MCHdArt::NewFrameId();
        made->Width = width * TabHdScale;
        made->Height = height * TabHdScale;
        made->Scale = TabHdScale;
        made->Colors.resize(static_cast<size_t>(made->Width) * static_cast<size_t>(made->Height) * RgbaBytes);
        made->Indices.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 2);
        const SDL_Color& face = colours[0];

        for (int32_t y = 0; y < made->Height; ++y)
        {
            const int32_t row = y / TabHdScale;
            // The face's light: +TabFaceRamp toward white at the top, toward black at the bottom.
            const float down = (static_cast<float>(y) + 0.5f) / static_cast<float>(made->Height);
            const float light = TabFaceRamp * (1.0f - 2.0f * down);
            const auto ramp = [light](uint8_t value)
            {
                const float v = static_cast<float>(value);
                const float lit = light >= 0.0f ? v + (Full - v) * light : v * (1.0f + light);
                return static_cast<uint8_t>(std::clamp(std::lround(lit), 0L, 255L));
            };

            for (int32_t x = 0; x < made->Width; ++x)
            {
                const int32_t column = x / TabHdScale;
                // As the lines are drawn: the top edge but its last pixel, the left edge but its last, then the right
                // edge and the bottom edge whole.
                const bool shaded = column == width - 1 || row == height - 1;
                const bool lit = !shaded && (row == 0 || column == 0);
                const uint8_t index = shaded ? TabShaded : lit ? TabLit : TabFace;
                const SDL_Color& colour = shaded ? colours[2] : lit ? colours[1] : face;
                const size_t at =
                    (static_cast<size_t>(y) * static_cast<size_t>(made->Width) + static_cast<size_t>(x)) * RgbaBytes;
                const bool onFace = !shaded && !lit;
                made->Colors[at] = onFace ? ramp(colour.r) : colour.r;
                made->Colors[at + 1] = onFace ? ramp(colour.g) : colour.g;
                made->Colors[at + 2] = onFace ? ramp(colour.b) : colour.b;
                made->Colors[at + 3] = Opaque;

                if (x % TabHdScale == 0 && y % TabHdScale == 0)
                {
                    const size_t pixel =
                        static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(column);
                    made->Indices[pixel * 2] = index;
                    made->Indices[pixel * 2 + 1] = Opaque;
                }
            }
        }

        return made;
    }

    /// <summary>
    /// Port-only: shows the HD frame of the thumb painted into <paramref name="pane"/> over its pixels, on a sharp
    /// screen with HD art on (else none: its pixels alone).
    /// </summary>
    void ShowTabHd(MCPane& pane, int32_t width, int32_t height)
    {
        const std::shared_ptr<MCWindow> window = pane.Window.lock();
        const std::shared_ptr<MCTexture> texture = window != nullptr ? window->Texture.lock() : nullptr;

        if (texture == nullptr)
        {
            return;
        }

        const std::shared_ptr<MCDisplay> display = MCInput::Display();

        if (display == nullptr || !display->SharpScreen() || !MCHdArt::Active() || !MCRenderer::ShowsHdPictures() ||
            width <= 0 || height <= 0)
        {
            MCRenderer::ShowHdPicture(*texture, nullptr, 0, 0);
            return;
        }

        std::array<SDL_Color, 256> shown{};
        display->GetShownColors(shown);
        const std::array<SDL_Color, 3> colours = {shown[TabFace], shown[TabLit], shown[TabShaded]};
        static std::deque<TabFrame> kept;
        auto found = std::ranges::find_if(kept,
                                          [&](const TabFrame& frame)
                                          {
                                              return frame.Width == width && frame.Height == height &&
                                                     std::ranges::equal(frame.Colours, colours, SameColour);
                                          });

        if (found == kept.end())
        {
            if (kept.size() == TabFramesKept)
            {
                kept.pop_back();
            }

            kept.push_front({width, height, colours, MakeTabFrame(width, height, colours)});
            found = kept.begin();
        }

        MCRenderer::ShowHdPicture(*texture, found->Frame, pane.X0, pane.Y0);
    }
}

auto PaintScrollTabBevel(MCPane& pane, int32_t width, int32_t height) -> void
{
    VfxPaneWipe(pane, TabFace);
    VfxLineDraw(pane, 0, 0, width - 2, 0, TabLit);
    VfxLineDraw(pane, 0, 0, 0, height - 2, TabLit);
    VfxLineDraw(pane, width - 1, 0, width - 1, height - 1, TabShaded);
    VfxLineDraw(pane, 0, height - 1, width - 1, height - 1, TabShaded);
    ShowTabHd(pane, width, height);
}

auto ScrollTabEventHandler(MCGuiObject& obj, MCGuiEvent& event) -> void
{
    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            GuiSystem().Grab(obj.weak_from_this());
            obj.StartDrag(0, event.Y - obj.GlobalY());
            break;
        }
        case MCGuiEventType::LeftButtonUp:
        {
            GuiSystem().Release();
            obj.StopDrag();
            break;
        }
        case MCGuiEventType::MouseMove:
        {
            // Any grab will do (the original doesn't check that it is this thumb).
            if (GuiSystem().GrabbedObject() == nullptr)
            {
                break;
            }

            const auto text = std::static_pointer_cast<MCGuiScrollTextObject>(obj.ParentObject());
            const int32_t lowest = text->Height() - TrackInset - text->ScrollTab->Height();
            obj.MoveTo(obj.X(), (event.Y - text->Y()) - obj.DragStartY());

            if (obj.Y() < TrackInset)
            {
                obj.MoveTo(obj.X(), TrackInset);
            }

            if (obj.Y() > lowest)
            {
                obj.MoveTo(obj.X(), lowest);
            }

            text->CalcFirstPixel(obj.Y() - TrackInset);
            break;
        }
        default:
            break;
    }
}

auto MCGuiScrollTextObject::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view text)
    -> int32_t
{
    // The base's init, with a scroll port for the port.
    const bool live = DrawsLive();
    InitFailed = false;
    Transparent = false;
    Place(xPos, yPos, width, height);
    DisplayPort = std::make_unique<MCGuiScrollPort>();

    if (const int32_t result = live ? DisplayPort->InitView(width, height) : DisplayPort->Init(width, height);
        result != 0)
    {
        InitFailed = true;
        return result;
    }

    auto tab = TryCreateChild<MCGuiObject>(
        [&](MCGuiObject& made)
        {
            made.SetDrawsLive();

            if (const int32_t result = made.Init(0, 0, 9, height - 2 * TrackInset, {}); result != 0)
            {
                return result;
            }

            made.MoveTo(Width() + 2, TrackInset);
            made.SetDepth(100);
            return 0;
        });

    if (!tab)
    {
        InitFailed = true;
        return tab.error();
    }

    ScrollTab = *tab;
    ScrollTab->ShowGuiWindow(true);
    ScrollTab->SetEventRoutine(ScrollTabEventHandler);
    ScrollTab->SetPaintRoutine(PaintScrollTab);
    ScrollTab->SetDepth(1);

    TextBuffer.clear();
    NumLines = 0;
    FirstPixel = 0;
    FontIndex = 0;
    SectionStarts.fill(-1);
    SectionColors.fill(0xff);

    if (!text.empty())
    {
        Print(text, 0x1f);
    }

    return 0;
}

auto MCGuiScrollTextObject::OnDestroy() -> void
{
    TextBuffer = {};
    MCGuiObject::OnDestroy();
}

auto MCGuiScrollTextObject::LineHeight() const -> int32_t
{
    return Fonts[0][FontIndex]->Height() + 2;
}

auto MCGuiScrollTextObject::Draw() -> void
{
    const int32_t lineHeight = LineHeight();
    VfxPaneWipe(Port().Frame(), 0x10);

    // Each section highlights one line.
    for (size_t i = 0; i < NumSections; i++)
    {
        if (const int32_t start = SectionStarts[i]; start != -1)
        {
            FillBox(1, static_cast<int16_t>(lineHeight * start + 1), static_cast<int16_t>(Width() - HighlightRightGap),
                    static_cast<int16_t>((start + 1) * lineHeight - 1), SectionColors[i]);
        }
    }

    // Lines are (colour byte, text, '\n'). A tab splits a line into two pieces, the second drawn at TabStop. (The
    // tab is looked for in the rest of the text, as the original did, so a line without one takes a later line's.)
    // The text ends at its first null, as the original's C string did.
    const size_t end = std::string_view(TextBuffer.c_str()).size();
    size_t line = 0;
    bool more = true;
    int32_t lineY = 2;

    while (more && line < end)
    {
        const auto color = static_cast<uint8_t>(TextBuffer[line]);
        line++;
        int32_t pieces = 0;
        int32_t lineX = 2;

        if (const size_t tab = TextBuffer.find('\t', line); tab < end)
        {
            if (TabStop < 0)
            {
                TextBuffer[tab] = ' ';
            }
            else
            {
                TextBuffer[tab] = '\n';
                pieces = 1;
            }
        }

        std::shared_ptr<MCGuiFont> font = Fonts[FontRowForColor(color)][FontIndex];

        do
        {
            font->WriteStringToNewline(Port().Frame(), lineX, lineY,
                                       std::string_view(TextBuffer).substr(line, end - line));
            const size_t newline = TextBuffer.find('\n', line);
            more = newline < end;

            if (pieces > 0)
            {
                // Put the tab back.
                if (more)
                {
                    TextBuffer[newline] = '\t';
                }

                lineX = TabStop;
            }

            pieces--;

            if (more)
            {
                line = newline + 1;
            }
        } while (pieces >= 0 && more);

        lineY += lineHeight;
    }

    for (const std::shared_ptr<MCGuiObject>& child : Children())
    {
        if (DrawsChild(*child))
        {
            child->Draw();
        }
    }
}

auto MCGuiScrollTextObject::Display() -> void
{
    if (!ShowWindow || (IsHidden() && HideOffset == 0))
    {
        return;
    }

    // Port: the port is the whole text (taller than the object), drawn each frame scrolled by FirstPixel.
    if (DrawsLive())
    {
        DrawInFramePass(Port(), FirstPixel);
        return;
    }

    if (DisplayPort != nullptr)
    {
        VfxPaneCopy(Port().Frame(), 0, 0, *FramePane.get(), 0, -FirstPixel, -1);
    }

    DisplayChildren();
}

auto MCGuiScrollTextObject::Resize(int32_t newWidth, int32_t newHeight) -> void
{
    if (newWidth <= 0 || newHeight <= 0 || (newWidth == WinWidth && newHeight == WinHeight))
    {
        return;
    }

    WinWidth = newWidth;
    WinHeight = newHeight;
    FramePane->X1 = FramePane->X0 - 1 + newWidth;
    FramePane->Y1 = FramePane->Y0 - 1 + newHeight;
    Port().Resize(newWidth, std::max(NumLines * LineHeight(), newHeight));
    ScrollTab->MoveTo(newWidth + 2, ScrollTab->Y());
    PositionScrollTab();
}

auto MCGuiScrollTextObject::CopyTextFrom(const MCGuiScrollTextObject& other) -> void
{
    FontIndex = other.FontIndex;
    TabStop = other.TabStop;
    TextBuffer = other.TextBuffer;
    NumLines = other.NumLines;
    SectionColors = other.SectionColors;
    SectionStarts = other.SectionStarts;
    FirstPixel = other.FirstPixel;
    Port().Resize(Port().Width(), std::max(LineHeight() * NumLines, other.DisplayPort->Height()));
    PositionScrollTab();
}

auto MCGuiScrollTextObject::ResetPortSize() -> void
{
    // The port only grows.
    Port().Resize(Port().Width(), std::max(LineHeight() * NumLines, Port().Height()));
}

auto MCGuiScrollTextObject::PrintBlank(uint8_t color) -> void
{
    TextBuffer.push_back(static_cast<char>(color));
    TextBuffer.push_back('\n');
    NumLines++;
}

auto MCGuiScrollTextObject::Print(std::string_view line, uint8_t color) -> void
{
    // The line ends at its first NUL, as the original's C string.
    line = line.substr(0, line.find('\0'));
    TextBuffer.push_back(static_cast<char>(color));
    TextBuffer += line;
    TextBuffer.push_back('\n');
    NumLines++;

    if (const int32_t needed = NumLines * LineHeight(); needed > Port().Height())
    {
        if (Port().Resize(Port().Width(), needed) != 0)
        {
            InitFailed = true;
        }
    }

    PositionScrollTab();
}

auto MCGuiScrollTextObject::PrintWrapped(std::string_view line, uint8_t color, int32_t wrapWidth) -> void
{
    if (wrapWidth == -1)
    {
        wrapWidth = Width();
    }

    line = line.substr(0, line.find('\0'));

    while (true)
    {
        std::shared_ptr<MCGuiFont> font = Fonts[0][FontIndex];
        size_t split = std::string_view::npos;

        if (font->Width(line) > wrapWidth - 6)
        {
            // Cut at the last space, then at earlier spaces until the piece fits. When even the first word doesn't
            // fit, the whole rest is printed as one line.
            split = line.rfind(' ');

            while (split != std::string_view::npos && font->Width(line.substr(0, split)) > wrapWidth - 6)
            {
                split = line.substr(0, split).rfind(' ');
            }
        }

        if (split == std::string_view::npos)
        {
            Print(line, color);
            return;
        }

        Print(line.substr(0, split), color);
        line.remove_prefix(split + 1);
    }
}

auto MCGuiScrollTextObject::Clear() -> void
{
    FirstPixel = 0;
    NumLines = 0;
    TextBuffer.clear();
    SectionStarts.fill(-1);
}

auto MCGuiScrollTextObject::CalcFirstPixel(int32_t thumbY) -> void
{
    const int32_t track = Height() - 2 * TrackInset - ScrollTab->Height();
    const int32_t range = Port().Height() - Height();
    FirstPixel = track > 0 && range > 0 ? (range * thumbY) / track : 0;
}

auto MCGuiScrollTextObject::PositionScrollTab() -> void
{
    if (GuiSystem().GrabbedObject().get() == ScrollTab.Lock().get())
    {
        return;
    }

    const int32_t track = Height() - 2 * TrackInset;
    const int32_t range = Port().Height() - Height();

    if (range == 0)
    {
        ScrollTab->ShowGuiWindow(false);
        return;
    }

    // The thumb's length is the visible fraction of the track (x87: float quotient, then times the track).
    const auto shown = static_cast<float>(Height());
    const int32_t tabLength = std::max(static_cast<int32_t>(static_cast<double>(shown) / Port().Height() * track), 3);
    ScrollTab->ShowGuiWindow(true);
    ScrollTab->Resize(ScrollTab->Width(), tabLength);
    ScrollTab->MoveTo(ScrollTab->X(), ((track - tabLength) * FirstPixel) / range + TrackInset);
}

auto MCGuiScrollTextObject::ReceiveClick(int32_t direction, int32_t yPos) -> void
{
    if (Height() == Port().Height())
    {
        return;
    }

    const int32_t lastPixel = Port().Height() - Height();

    if (direction == -1)
    {
        FirstPixel = std::max(FirstPixel - LineHeight(), 0);
    }
    else if (direction == 0)
    {
        if (yPos < ScrollTab->Y())
        {
            FirstPixel = std::max(FirstPixel - Height(), 0);
        }
        else if (yPos > ScrollTab->Bottom())
        {
            FirstPixel = std::min(FirstPixel + Height(), lastPixel);
        }
    }
    else if (direction == 1)
    {
        FirstPixel = std::min(FirstPixel + LineHeight(), lastPixel);
    }

    PositionScrollTab();
}

auto MCGuiScrollTextObject::MouseWheel(int32_t steps, int32_t, int32_t) -> bool
{
    if (Height() == Port().Height())
    {
        return false;
    }

    for (; steps != 0; steps += steps < 0 ? 1 : -1)
    {
        ReceiveClick(steps < 0 ? -1 : 1, 0);
    }

    return true;
}
