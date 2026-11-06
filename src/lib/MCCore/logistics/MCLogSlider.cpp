#include "stdafx.h"
#include "logistics/MCLogSlider.h"
#include "gui/MCGuiEvent.h"
#include "main/MCGamePaths.h"
#include "vfx/MCVfxFunctions.h"

MCLogSlider::MCLogSlider() : ThumbPort(std::make_shared<MCLogPort>())
{
    ThumbPort->Load(std::format("{}prefs_02.tga", ArtPath));
}

auto MCLogSlider::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height,
                       [[maybe_unused]] std::string_view name) -> int32_t
{
    const int32_t result = MCLogObject::Init(xPos, yPos, width, height);
    SetTransparent(true);
    return result;
}

auto MCLogSlider::OnDestroy() -> void
{
    ThumbPort.reset();
    TrackArt.reset();
    MCLogObject::OnDestroy();
}

auto MCLogSlider::SetTrack(std::shared_ptr<MCLogPort> art, int32_t left, int32_t top, int32_t thumbInset) -> void
{
    TrackArt = std::move(art);
    TrackLeft = left;
    TrackTop = top;
    ThumbInset = thumbInset;
}

auto MCLogSlider::Travel() -> int32_t
{
    return Width() - 2 * ThumbInset - ThumbPort->Width();
}

auto MCLogSlider::ThumbX() -> int32_t
{
    // The value's share of the travel (x87).
    return ThumbInset + static_cast<int32_t>(static_cast<double>(Travel()) *
                                             (static_cast<double>(CurrentValue - MinValue) / (MaxValue - MinValue)));
}

auto MCLogSlider::Draw() -> void
{
    if (TrackArt != nullptr)
    {
        VfxPaneCopy(TrackArt->Frame(), TrackLeft, TrackTop, Lport()->Frame(), 0, 0, VfxNoColor);
    }
    else
    {
        VfxPaneWipe(Lport()->Frame(), 0xff);
    }

    ThumbPort->CopyTo(Lport()->Frame(), ThumbX(), 0, false);
}

auto MCLogSlider::SetCurrentValue(int32_t value) -> void
{
    // The minimum wins over the maximum (std::clamp needs them in order).
    CurrentValue = value < MinValue ? MinValue : std::min(value, MaxValue);
}

auto MCLogSlider::ValueAt(int32_t mouseX) -> int32_t
{
    // The offset (as a float) over the travel, times the range. (The original took the slider's own x: its sliders
    // were the screen's children, at the screen's origin.)
    const auto offset = static_cast<float>(mouseX - GlobalX() - ThumbInset);
    return static_cast<int32_t>(static_cast<double>(offset) / Travel() * (MaxValue - MinValue)) + MinValue;
}

auto MCLogSlider::HandleEvent(MCGuiEvent& event) -> void
{
    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            GuiSystem().Grab(weak_from_this());

            if (GuiSystem().GrabbedObject() != nullptr)
            {
                SetCurrentValue(ValueAt(event.X));
            }
            break;
        }
        case MCGuiEventType::LeftButtonUp:
        {
            GuiSystem().Release();
            SetCurrentValue(ValueAt(event.X));
            break;
        }
        case MCGuiEventType::MouseMove:
        {
            if (GuiSystem().GrabbedObject() != nullptr)
            {
                SetCurrentValue(ValueAt(event.X));
            }
            break;
        }
        default:
            break;
    }

    if (EventRoutine != nullptr)
    {
        EventRoutine(*this, event);
    }
}
