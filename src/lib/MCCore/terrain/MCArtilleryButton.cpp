#include "stdafx.h"
#include "terrain/MCArtilleryButton.h"
#include "camera/MCCamera.h"
#include "camera/MCMainWindow.h"
#include "gui/MCGuiFont.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "logistics/MCConnectMenu.h"
#include "main/MCGameStrings.h"
#include "object/MCForces.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>The event types of MCGuiEvent::Type the button handles.</summary>
    constexpr int32_t EventLeftDown = 1;
    constexpr int32_t EventLeftUp = 4;
    constexpr int32_t EventKeyUp = 8;
    constexpr int32_t EventResize = 0x12;

    /// <summary>The support command ids (and the button's strike count).</summary>
    constexpr int32_t StrikeSmall = 0xf9;
    constexpr int32_t StrikeLarge = 0xf8;
    constexpr int32_t StrikeSensor = 0xfa;
    constexpr int32_t StrikeCameraDrone = 0x204;

    /// <summary>String <paramref name="id"/> of the string table.</summary>
    std::string TableString(uint32_t id)
    {
        return LoadGameString(id, 0xfe);
    }
}

auto MCStrikesLeft(int32_t commandId) -> std::optional<int32_t>
{
    switch (commandId)
    {
        case StrikeSensor:
            return HomeCommander()->NumSensorStrikes;
        case StrikeLarge:
            return HomeCommander()->NumLargeStrikes;
        case StrikeSmall:
            return HomeCommander()->NumSmallStrikes;
        case StrikeCameraDrone:
            return HomeCommander()->NumCameraDrones;
        default:
            return std::nullopt;
    }
}

auto MCArtilleryButton::Init(int32_t xPos, int32_t yPos, int32_t w, int32_t h, std::string_view fileName) -> int32_t
{
    const int32_t result = MCGuiButton::Init(xPos, yPos, w, h, fileName);
    Armed = false;
    KeyArmed = false;
    Disabled = false;
    return result;
}

auto MCArtilleryButton::Draw() -> void
{
    if (const std::optional<int32_t> before = MCStrikesLeft(CommandId); before.has_value())
    {
        Disabled = *before < 1 ? -1 : false;
    }

    MCGuiButton::Draw();

    // The count, in the button's corner.
    std::string count;

    if (const std::optional<int32_t> left = MCStrikesLeft(CommandId); left.has_value())
    {
        count = std::format("{:02}", *left);
    }

    std::shared_ptr<MCGuiFont> font = BlueFont;

    if (Disabled != 0)
    {
        font = GreyFont;
    }
    else if (GuiSystem().Grabs(*this) && GuiSystem().CurrentObject().get() == this)
    {
        font = WhiteFont;
    }

    font->WriteString(DisplayPort->Frame(), 0x13, 9, count.data(), -1);
}

auto MCArtilleryButton::HandleEvent(MCGuiEvent& event) -> void
{
    const std::shared_ptr<MCTacticalMap> map = TacticalMap();

    if (Disabled != 0)
    {
        if (GuiSystem().Grabs(*this))
        {
            GuiSystem().Release();
        }

        if (event.Type == EventLeftDown)
        {
            SoundSystem()->PlayInterfaceSample(0x46);
        }

        return;
    }

    if (event.Type == EventLeftDown)
    {
        if (!Armed)
        {
            GuiSystem().Grab(weak_from_this());
            Draw();
        }

        return;
    }

    if (event.Type == EventLeftUp)
    {
        if (!Armed)
        {
            // Armed: "Calling <strike>..." holds the status line until the target click.
            const std::string format = TableString(0x98);
            map->CallingText = MCFormatPrintf(format.c_str(), HelpText.c_str());
            map->ShowStatus(map->CallingText);
            map->StatusLocked = true;
            Armed = true;
            GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(9));
            GuiSystem().CursorHidden = true;
            Draw();
            return;
        }

        const int32_t screenX = event.X;
        const int32_t screenY = event.Y;
        const POINT inMap{screenX - map->GlobalX(), screenY - map->GlobalY()};
        Armed = false;
        GuiSystem().Release();
        Draw();

        if (PtInRect(map->MapRect, inMap) == 0 || map->DisplayType != MCTacmapPage::Map)
        {
            // Outside the tactical map: the click must land in the active view.
            std::shared_ptr<MCGuiObject> target = ScreenWindow()->FindObject(screenX, screenY);

            if (target != MainHolder()->GetActivePane() && target != TacticalInterface()->MechBar.Lock())
            {
                Armed = false;
                GuiSystem().Release();
                map->ReleaseStatusLine();
                Draw();
                return;
            }

            const std::shared_ptr<MCGuiObject> pane = MainHolder()->GetActivePane()->PointInside(screenX, screenY) == 0
                                                          ? MainHolder()->GetInactivePane()
                                                          : MainHolder()->GetActivePane();

            if (pane == nullptr)
            {
                return;
            }

            const std::shared_ptr<MCCamera> camera = pane->GetCamera();

            if (camera == nullptr)
            {
                return;
            }

            // Port: on the view's world surface, through the zoom.
            const MCVector2D screenPos = MCWindowPoint(*pane, screenX, screenY);
            MCVector3D target3d;
            camera->InverseProject(screenPos, target3d);
            MCTacticalInterface::CallStrike(CommandId, target3d, nullptr, true, false, -1.0f);
        }
        else
        {
            MCVector3D target3d(static_cast<float>(inMap.x), static_cast<float>(inMap.y), 0.0f);
            map->TacMapToWorld(target3d, true);
            MCTacticalInterface::CallStrike(CommandId, target3d, nullptr, true, false, -1.0f);
        }

        map->ReleaseStatusLine();
        return;
    }

    if (event.Type == EventKeyUp)
    {
        // Backspace or Escape disarms.
        if (event.Key == 8 || event.Key == 0x1b)
        {
            GuiSystem().Release();
            Armed = false;
            map->ReleaseStatusLine();
            Draw();
        }

        return;
    }

    // The port's resize broadcast has no position: passed to the tactical map under (0, 0), it would come back here
    // forever.
    if (event.Type == EventResize)
    {
        return;
    }

    // Anything else goes to what lies under the mouse.
    std::shared_ptr<MCGuiObject> under = ScreenWindow()->FindObject(event.X, event.Y);

    if (under.get() != this && under != nullptr)
    {
        event.Target = under;
        under->HandleEvent(event);
    }
}

auto MCArtilleryButton::Enter() -> void
{
    TacticalMap()->ShowStatus(HelpText);
}

auto MCArtilleryButton::Leave() -> void
{
    TacticalMap()->ShowStatus(std::nullopt);
}
