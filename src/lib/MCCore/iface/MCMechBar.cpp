#include "stdafx.h"
#include "iface/MCMechBar.h"
#include "camera/MCMainWindow.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCMsvcSort.h"
#include "object/MCMoverGroup.h"
#include "platform/MCRenderer.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>A button's place in <see cref="MCMechBar::PlaceButtons"/>'s sort.</summary>
    struct MCButtonSortEntry
    {
        /// <summary>The button's index, or -1 for an empty slot.</summary>
        int32_t Index = 0;
        int32_t Key = 0;
    };
}

auto MCMechBar::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view bitmapName) -> int32_t
{
    MCGuiObject::Init(xPos, yPos, width, height, bitmapName);
    // The bar draws on its parent: no bitmap of its own.
    MCRenderer::DestroyTexture(Port().Bitmap());
    Port().Bitmap().Pixels.reset();
    ShowGuiWindow(false);
    Dancing = false;
    SpacingX = 0x34;
    SpacingY = 0x2e;
    return 0;
}

auto MCMechBar::OnDestroy() -> void
{
    CleanUp();
    MCGuiObject::OnDestroy();
}

auto MCMechBar::Display() -> void
{
    if (ShowWindow == 0)
    {
        return;
    }

    for (size_t i = 0; i < ChildList.size(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = ChildList[i].Lock(); child != nullptr)
        {
            child->Display();
        }
    }

    const std::vector<std::shared_ptr<MCFriendlyMechIcon>> buttons = Buttons();

    if (!Dancing)
    {
        // A separator after each lance icon shown, and around each button outside a lance.
        for (const std::shared_ptr<MCLanceIcon>& lanceIcon : LanceIcons())
        {
            if (lanceIcon != nullptr && lanceIcon->IsShowing() != 0)
            {
                const int32_t lanceRight = lanceIcon->Right();
                VfxLineDraw(Frame(), lanceRight - 1, 0xf, lanceRight - 1, Height() - 1, 0x10);
            }
        }

        for (const std::shared_ptr<MCFriendlyMechIcon>& button : buttons)
        {
            button->DrawBox(0x35, -1, -1, -1, -1);

            if (button->IsShowing() != 0 && button->Lance == NoLance)
            {
                const int32_t buttonRight = button->Right();
                VfxLineDraw(Frame(), buttonRight, 0xf, buttonRight, Height() - 1, 0x10);
                VfxLineDraw(Frame(), 0, 0xf, buttonRight, 0xf, 0x10);
            }
        }
    }

    // Each button's frame: red on the video pilot, else yellow when selected, else white under the mouse.
    for (const std::shared_ptr<MCFriendlyMechIcon>& button : buttons)
    {
        const int32_t buttonWidth = button->Width();
        const int32_t buttonHeight = button->Height();

        if (VideoId == button->PartId)
        {
            button->DrawBox(0xef, 1, 1, buttonWidth - 2, buttonHeight - 2);
        }
        else if (TacticalInterface()->IsSelected(button->PartId))
        {
            button->DrawBox(0x1f, 1, 1, buttonWidth - 2, buttonHeight - 2);
        }
        else if (HighlightId == button->PartId)
        {
            button->DrawBox(0xb, 1, 1, buttonWidth - 2, buttonHeight - 2);
        }
    }
}

auto MCMechBar::HandleEvent(MCGuiEvent& event) -> void
{
    // A broadcast (a resolution change): back to the bottom of the screen.
    if (event.Type == 0x12)
    {
        MoveTo(1, GuiSystem().Height() - Height() - 1, false);
    }

    event.Target = MainHolder()->GetActivePane();
    TacticalInterface()->HandleEvent(event);
}

auto MCMechBar::Resize(int32_t width, int32_t height) -> void
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (width == WinWidth && height == WinHeight)
    {
        return;
    }

    WinWidth = width;
    WinHeight = height;
    FramePane->X1 = FramePane->X0 - 1 + width;
    FramePane->Y1 = FramePane->Y0 - 1 + height;
}

auto MCMechBar::CleanUp() -> void
{
    // Last to first, as the original removed them.
    while (!_Buttons.empty())
    {
        CloseGuiObject(_Buttons.back());
        _Buttons.pop_back();
    }

    for (const MCGuiHandle<MCFriendlyMechIcon>& icon : _ReserveIcons)
    {
        CloseGuiObject(icon);
    }

    _ReserveIcons.clear();

    for (MCGuiHandle<MCLanceIcon>& lanceIcon : _LanceIcons)
    {
        CloseGuiObject(lanceIcon);
        lanceIcon.reset();
    }
}

auto MCMechBar::Buttons() const -> std::vector<std::shared_ptr<MCFriendlyMechIcon>>
{
    std::vector<std::shared_ptr<MCFriendlyMechIcon>> buttons;
    buttons.reserve(_Buttons.size());

    for (const MCGuiHandle<MCFriendlyMechIcon>& button : _Buttons)
    {
        if (std::shared_ptr<MCFriendlyMechIcon> pinned = button.Lock(); pinned != nullptr)
        {
            buttons.push_back(std::move(pinned));
        }
    }

    return buttons;
}

auto MCMechBar::LanceIcons() const -> std::array<std::shared_ptr<MCLanceIcon>, NumLances>
{
    std::array<std::shared_ptr<MCLanceIcon>, NumLances> icons;

    for (size_t i = 0; i < NumLances; i++)
    {
        icons[i] = _LanceIcons[i].Lock();
    }

    return icons;
}

auto MCMechBar::GetButton(size_t index) const -> std::shared_ptr<MCFriendlyMechIcon>
{
    return index < _Buttons.size() ? _Buttons[index].Lock() : nullptr;
}

auto MCMechBar::GetButtonFromID(int32_t partId) const -> std::shared_ptr<MCFriendlyMechIcon>
{
    for (const MCGuiHandle<MCFriendlyMechIcon>& button : _Buttons)
    {
        if (std::shared_ptr<MCFriendlyMechIcon> pinned = button.Lock(); pinned != nullptr && pinned->PartId == partId)
        {
            return pinned;
        }
    }

    return nullptr;
}

auto MCMechBar::GetReserveIcon(int32_t partId) const -> std::shared_ptr<MCFriendlyMechIcon>
{
    for (const MCGuiHandle<MCFriendlyMechIcon>& icon : _ReserveIcons)
    {
        if (std::shared_ptr<MCFriendlyMechIcon> pinned = icon.Lock(); pinned != nullptr && pinned->PartId == partId)
        {
            return pinned;
        }
    }

    return nullptr;
}

auto MCMechBar::PlaceButtons(bool animate) -> void
{
    // Sort the buttons by lance, then by x; a lance's point first; inactive movers last. The sort runs over all
    // twelve slots, the empty ones first, so equal keys come out as in the original (rule R5).
    std::array<MCButtonSortEntry, MaxButtons> order;

    for (size_t i = 0; i < MaxButtons; i++)
    {
        std::shared_ptr<MCFriendlyMechIcon> button = GetButton(i);

        if (button == nullptr)
        {
            order[i] = {-1, -999999};
            continue;
        }

        int32_t key = button->X() + button->Lance * 10000;

        if (button->IsPoint)
        {
            key -= 1000;
        }

        if (!button->Active)
        {
            key += 100000;
        }

        order[i] = {static_cast<int32_t>(i), key};
    }

    MCMsvcSort(std::span<MCButtonSortEntry>(order),
               [](const MCButtonSortEntry& a, const MCButtonSortEntry& b)
               {
                   if (a.Key == b.Key)
                   {
                       return 0;
                   }

                   return b.Key < a.Key ? 1 : -1;
               });

    for (const std::shared_ptr<MCLanceIcon>& lanceIcon : LanceIcons())
    {
        if (lanceIcon != nullptr)
        {
            lanceIcon->ShowTest();
        }
    }

    // Each lance starts with its icon, one pixel after the previous group; an inactive mover goes off the bar.
    int32_t nextX = 0;
    int32_t lastLance = -1;

    for (const MCButtonSortEntry& entry : order)
    {
        if (entry.Index < 0)
        {
            continue;
        }

        const std::shared_ptr<MCFriendlyMechIcon> button = _Buttons[entry.Index].Lock();
        const int32_t buttonLance = button->Lance;
        int32_t xPos = nextX;

        if (buttonLance != lastLance || buttonLance == NoLance)
        {
            if (nextX != 0)
            {
                xPos = nextX + 1;
            }

            lastLance = buttonLance;

            if (buttonLance < static_cast<int32_t>(NumLances) && _LanceIcons[buttonLance] != nullptr)
            {
                _LanceIcons[buttonLance]->MoveTo(xPos, 2, false);
            }

            xPos++;
        }

        button->TargetX = button->Active ? xPos : xPos + 1000;
        button->ShuffleStep = (xPos - button->X()) / TacticalInterface()->ShuffleFrames;
        nextX = xPos - 1 + SpacingX;
    }

    if (!animate)
    {
        for (const std::shared_ptr<MCFriendlyMechIcon>& button : Buttons())
        {
            button->MoveTo(button->TargetX, button->Y(), false);
        }

        const int32_t linkWidth = SpacingX - 1;

        for (const std::shared_ptr<MCLanceIcon>& lanceIcon : LanceIcons())
        {
            lanceIcon->Resize(lanceIcon->GetNumActiveMovers() * linkWidth + 3, lanceIcon->Height());
        }

        return;
    }

    if (_DanceCallback == nullptr)
    {
        _DanceStep = 0;
        _DanceFrames = 0;
        Dancing = true;
        _DanceCallback = std::make_shared<MCGuiCallback>();
        _DanceCallback->SetExec([] { TacticalInterface()->MechBar->Dance(); });
        GuiSystem().AddCallback(_DanceCallback);
        SoundSystem()->PlayInterfaceSample(0x42);
    }
}

auto MCMechBar::InitLances() -> void
{
    for (size_t i = 0; i < NumLances; i++)
    {
        _LanceIcons[i] = CreateChild<MCLanceIcon>([i](MCLanceIcon& icon) { icon.Init(static_cast<int16_t>(i)); });
    }

    PlaceButtons(false);
}

auto MCMechBar::GetLanceIconFromID(int32_t lanceId) const -> std::shared_ptr<MCLanceIcon>
{
    for (const std::shared_ptr<MCLanceIcon>& lanceIcon : LanceIcons())
    {
        if (lanceIcon != nullptr && lanceIcon->LanceId == lanceId)
        {
            return lanceIcon;
        }
    }

    return nullptr;
}

auto MCMechBar::Dance() -> void
{
    // Three phases: the lance icons drop out of sight while the buttons that move left rise; the buttons slide to
    // their places; then the lance icons come back up and the buttons drop into line.
    const int32_t step = _LanceIcons[0]->Height() / 3 + 1;
    const int16_t shuffleFrames = TacticalInterface()->ShuffleFrames;

    if (_DanceFrames == 3)
    {
        _DanceStep++;
    }

    if (_DanceFrames == shuffleFrames + 3)
    {
        for (const std::shared_ptr<MCFriendlyMechIcon>& button : Buttons())
        {
            button->MoveTo(button->TargetX, button->Y(), false);
        }

        _DanceStep++;
    }

    if (_DanceStep == 0)
    {
        for (const std::shared_ptr<MCLanceIcon>& lanceIcon : LanceIcons())
        {
            lanceIcon->MoveTo(lanceIcon->X(), lanceIcon->Y() + step, false);
            lanceIcon->SetDepth(-10);
        }

        for (const std::shared_ptr<MCFriendlyMechIcon>& button : Buttons())
        {
            if (button->TargetX < button->X())
            {
                button->MoveTo(button->X(), button->Y() - step, false);
                button->SetDepth(10);
            }
        }
    }
    else if (_DanceStep == 1)
    {
        for (const std::shared_ptr<MCFriendlyMechIcon>& button : Buttons())
        {
            button->MoveTo(button->X() + button->ShuffleStep, button->Y(), false);
        }

        for (const std::shared_ptr<MCLanceIcon>& lanceIcon : LanceIcons())
        {
            lanceIcon->ShowGuiWindow(false);
        }
    }
    else if (_DanceStep == 2)
    {
        for (const std::shared_ptr<MCLanceIcon>& lanceIcon : LanceIcons())
        {
            if (!lanceIcon->Group.expired())
            {
                lanceIcon->NumActiveMovers = lanceIcon->GetNumActiveMovers();
            }

            lanceIcon->MoveTo(lanceIcon->X(), lanceIcon->Y() - step, false);
            lanceIcon->ShowTest();
        }

        for (const std::shared_ptr<MCFriendlyMechIcon>& button : Buttons())
        {
            if (button->Y() != 0xf)
            {
                button->MoveTo(button->X(), button->Y() + step, false);
            }
        }

        if (shuffleFrames + 6 <= _DanceFrames)
        {
            // Done: everything at rest, the lance icons sized to their lances.
            for (const std::shared_ptr<MCFriendlyMechIcon>& button : Buttons())
            {
                button->SetDepth(0);
            }

            for (const std::shared_ptr<MCLanceIcon>& lanceIcon : LanceIcons())
            {
                lanceIcon->SetDepth(0);
                lanceIcon->MoveTo(lanceIcon->X(), 2, false);
                lanceIcon->Resize(lanceIcon->GetNumActiveMovers() * (SpacingX - 1) + 3, lanceIcon->Height());
            }

            Dancing = false;
            GuiSystem().RemoveCallback(*_DanceCallback);
            Draw();
            SoundSystem()->PlayInterfaceSample(0x43);
            _DanceFrames++;
            // The callback running this goes last (aCallback's run allows it, OB-109).
            _DanceCallback.reset();
            return;
        }
    }

    _DanceFrames++;
}
