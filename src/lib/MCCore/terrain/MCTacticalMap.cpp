#include "stdafx.h"
#include "terrain/MCTacticalMap.h"
#include "camera/MCCamera.h"
#include "camera/MCMainWindow.h"
#include "engine/MCByteFlag.h"
#include "engine/MCFont.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiScrollTextObject.h"
#include "gui/MCGuiChatWindow.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "main/MCGamePaths.h"
#include "logistics/MCPreferencesMenu.h"
#include "terrain/MCMapBlockManager.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCBigGameObject.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCObjectType.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "platform/MCRenderer.h"
#include "sound/MCRadio.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTacticalMapLayout.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfx.h"
#include "vfx/MCVfxFunctions.h"

using namespace MCTacmapLayout;

bool MCTacticalMap::MarkersLit = false;
float MCTacticalMap::MarkerBlinkTime = 0.0f;

namespace
{
    /// <summary>The auto-repeat timers of the scroll buttons: the first delay, then the repeat.</summary>
    constexpr int16_t ScrollStartTimer = 4;
    constexpr int16_t ScrollRepeatTimer = 5;
    /// <summary>The timer that blinks the chat tab.</summary>
    constexpr int16_t ChatBlinkTimer = 1;

    /// <summary>The support command ids (and the button's strike count).</summary>
    constexpr int32_t StrikeSmall = 0xf9;
    constexpr int32_t StrikeLarge = 0xf8;
    constexpr int32_t StrikeSensor = 0xfa;
    constexpr int32_t StrikeCameraDrone = 0x204;

    /// <summary>The palette button's action that toggles the zoom instead of choosing a mode.</summary>
    constexpr MCInterfaceMode ActionToggleZoom = MCInterfaceMode::ToggleZoom;
    /// <summary>The palette button's action of the jump mode (enabled only when the whole selection can jump).</summary>
    constexpr MCInterfaceMode ActionJump = MCInterfaceMode::Jump;

    /// <summary>The number of the command palette's mode buttons (the eighth toggles the zoom).</summary>
    constexpr size_t NumModeButtons = 7;

    /// <summary>The interface mode of each command palette button (<c>IntMode</c> values).</summary>
    constexpr std::array<MCInterfaceMode, 8> ButtonActions = {MCInterfaceMode::AttackFromPosition,
                                                              MCInterfaceMode::AttackShortRange,
                                                              MCInterfaceMode::AttackMediumRange,
                                                              MCInterfaceMode::AttackLongRange,
                                                              MCInterfaceMode::Guard,
                                                              MCInterfaceMode::Jump,
                                                              MCInterfaceMode::Run,
                                                              ActionToggleZoom};

    /// <summary>
    /// Whether a scroll position keeps the zoomed view's both edges on the map picture (<paramref name="side"/>
    /// pixels along that axis).
    /// </summary>
    bool ScrollInPicture(int32_t scroll, int32_t side, int32_t zoom)
    {
        const int32_t half = side >> 1;
        const int32_t zoomedHalf = half / zoom;
        const int32_t low = (scroll - zoomedHalf) + half;
        const int32_t high = (zoomedHalf - half) + side + scroll;
        return low >= 0 && low <= side - 1 && high >= 0 && high <= side - 1;
    }

    /// <summary>
    /// Places a text page's click areas along the right edge (the scroll-up button from <paramref name="upTop"/>,
    /// the scroll-down one from <paramref name="downTop"/>, the track between) and moves the markers there.
    /// </summary>
    void SetPageRects(MCTacticalMap& map, int32_t upTop, int32_t upBottom, int32_t downTop, int32_t downBottom)
    {
        map.PageRects[0] = {0x7d, upTop, 0x88, upBottom};
        map.ScrollUpMarker->MoveTo(0x7d, upTop, false);
        map.PageRects[1] = {0x7d, downTop, 0x88, downBottom};
        map.ScrollDownMarker->MoveTo(0x7d, downTop, false);
        map.PageRects[2] = {0x7d, upBottom, 0x88, downTop};
    }

    /// <summary>String <paramref name="id"/> of the string table.</summary>
    std::string TableString(uint32_t id)
    {
        return LoadGameString(id, 0xfe);
    }

    /// <summary>Loads string <paramref name="id"/> as a help text (at most 0x31 characters, as the original's).</summary>
    std::string LoadHelpText(uint32_t id)
    {
        std::string text = TableString(id);

        if (text.size() > 0x31)
        {
            text.resize(0x31);
        }

        return text;
    }

    /// <summary>
    /// Sets up a plain aObject showing a picture. (The original loaded the picture into the object's own port; it is
    /// the object's background now, and the object draws itself.)
    /// </summary>
    void SetUpPicture(MCGuiObject& picture, int32_t x, int32_t y, int32_t w, int32_t h, std::string_view fileName)
    {
        picture.SetDrawsLive();
        picture.Init(x, y, w, h, {});
        picture.SetBackground(fileName);
    }

    /// <summary>Sets up a button with its up, down and gray pictures.</summary>
    void SetUpButton(MCGuiButton& button, int32_t x, int32_t y, int32_t w, int32_t h, std::string_view up,
                     std::string_view down, std::string_view gray)
    {
        button.Init(x, y, w, h, {});
        button.SetUpPicture(up);
        button.SetDownPicture(down);

        if (!gray.empty())
        {
            button.SetGrayPicture(gray);
        }
    }

    /// <summary>Loads a background port for one of the MFD pages.</summary>
    std::unique_ptr<MCGuiPort> LoadBackground(std::string_view fileName, std::string_view error)
    {
        auto background = std::make_unique<MCGuiPort>();
        const int32_t result = background->Init(fileName);
        Assert(result == 0, static_cast<uint32_t>(result), error);
        return background;
    }

    /// <summary>Stops the pilot video (and its radio movie) when switching away from the map page.</summary>
    void StopVideo()
    {
        const std::shared_ptr<MCTacticalMap> map = TacticalMap();

        if (map->VideoWindow->Star.expired())
        {
            return;
        }

        SoundSystem()->CurrentMessage->CloseMovie();
        map->VideoWindow->SetStar(nullptr);
    }

    /// <summary>After a zoom, recentres the map on the main camera (unless at 1x).</summary>
    void RecentreAfterZoom(MCTacticalMap& map)
    {
        map.MetersPerPixel = (map.MapDiagonal * DiagonalToPixels) / static_cast<float>(map.Zoom);

        if (Eye() == nullptr)
        {
            return;
        }

        MCVector3D center = Eye()->Position;
        map.ScrollY = 0;
        map.ScrollX = 0;
        const auto zoom = static_cast<float>(map.Zoom);
        const float scaleX = (static_cast<float>(map.MapWidth) * PictureToPixels) / zoom;
        const float scaleY = (static_cast<float>(map.MapHeight) * PictureToPixels) / zoom;

        if (map.Zoom == 1)
        {
            return;
        }

        map.WorldToTacMap(center, true);
        center.X = (center.X - MapCenterX) * scaleX;
        center.Y = (center.Y - MapCenterY) * scaleY;
        map.SetScrollMapPosition(static_cast<int32_t>(center.X), static_cast<int32_t>(center.Y));
    }

    /// <summary>Callback of the zoom-in button (at most 8x).</summary>
    void ZoomIn()
    {
        MCTacticalMap& map = *TacticalMap();
        const int32_t oldZoom = map.Zoom;
        map.Zoom = oldZoom * 2;

        if (map.Zoom < 9)
        {
            SoundSystem()->PlayInterfaceSample(0x44);
            map.ZoomOffset += (map.MapVertexSide >> 1) / map.Zoom;
        }
        else
        {
            map.Zoom = 8;
        }

        RecentreAfterZoom(map);

        if (map.Zoom == 8)
        {
            map.ScrollButtons[4]->Disabled = true;
        }

        map.ScrollButtons[5]->Disabled = false;

        // Zoomed in, the map can scroll.
        for (size_t i = 0; i < 4; i++)
        {
            map.ScrollButtons[i]->Disabled = false;
        }

        map.RefreshPage();
    }

    /// <summary>Callback of the zoom-out button.</summary>
    void ZoomOut()
    {
        MCTacticalMap& map = *TacticalMap();
        map.ZoomOffset = std::max(map.ZoomOffset - (map.MapVertexSide >> 1) / map.Zoom, 0);
        map.Zoom >>= 1;

        if (map.Zoom == 0)
        {
            map.Zoom = 1;
        }
        else
        {
            SoundSystem()->PlayInterfaceSample(0x45);
        }

        if (map.Zoom == 1)
        {
            // At 1x the whole map shows: no zooming out or scrolling.
            map.ScrollButtons[5]->Disabled = true;

            for (size_t i = 0; i < 4; i++)
            {
                map.ScrollButtons[i]->Disabled = true;
            }
        }

        map.ScrollButtons[4]->Disabled = false;
        RecentreAfterZoom(map);
        map.RefreshPage();
    }

    /// <summary>
    /// Shared by the four scroll buttons: scroll once on press, then after the interface's scrollStart delay repeat
    /// five times as fast until release.
    /// </summary>
    void ScrollButtonEvent(MCGuiObject& obj, const MCGuiEvent& event, int32_t dx, int32_t dy)
    {
        if (event.Type == EventLeftDown)
        {
            GuiSystem().AddTimer(obj.weak_from_this(), ScrollStartTimer, TacticalInterface()->ScrollStart, 0, 0, false);
            TacticalMap()->ScrollMap(dx, dy);
        }
        else if (event.Type == EventLeftUp)
        {
            GuiSystem().RemoveTimer(obj, ScrollStartTimer);
            GuiSystem().RemoveTimer(obj, ScrollRepeatTimer);
        }
        else if (event.Type == EventTimer)
        {
            TacticalMap()->ScrollMap(dx, dy);

            if (event.Data == ScrollStartTimer)
            {
                GuiSystem().RemoveTimer(obj, ScrollStartTimer);
                GuiSystem().AddTimer(obj.weak_from_this(), ScrollRepeatTimer, TacticalInterface()->ScrollStart / 5, 0,
                                     0, false);
            }
        }
    }

    /// <summary>Event routines of the map scroll buttons.</summary>
    void ScrollUp(MCGuiObject& obj, MCGuiEvent& event)
    {
        ScrollButtonEvent(obj, event, 0, -TacticalInterface()->TacScrollSpeed);
    }

    void ScrollLeft(MCGuiObject& obj, MCGuiEvent& event)
    {
        ScrollButtonEvent(obj, event, TacticalInterface()->TacScrollSpeed, 0);
    }

    void ScrollDown(MCGuiObject& obj, MCGuiEvent& event)
    {
        ScrollButtonEvent(obj, event, 0, TacticalInterface()->TacScrollSpeed);
    }

    void ScrollRight(MCGuiObject& obj, MCGuiEvent& event)
    {
        ScrollButtonEvent(obj, event, -TacticalInterface()->TacScrollSpeed, 0);
    }

    /// <summary>Callbacks of the info page's data buttons.</summary>
    void ArmorFrontButton()
    {
        TacticalMap()->SetDataDisplayMode(0, 0);
    }

    void PayloadButton()
    {
        TacticalMap()->SetDataDisplayMode(2, 0);
    }

    void RearButton()
    {
        TacticalMap()->SetDataDisplayMode(1, 0);
    }

    /// <summary>Shows the MFD on <paramref name="page"/>; a page switch away from the map stops the pilot video.</summary>
    void SwitchPage(MCTacmapPage page)
    {
        const std::shared_ptr<MCTacticalMap> map = TacticalMap();
        map->HideMe(false);

        if (map->DisplayType != page)
        {
            map->SetDisplayType(page);

            if (page != MCTacmapPage::Map)
            {
                StopVideo();
            }
        }
    }

    /// <summary>Event routine of the chat tab blinkers: passes the event to what lies under the blinker.</summary>
    void BlinkerHandleEvent(MCGuiObject& obj, MCGuiEvent& event)
    {
        // The port's resize broadcast (see MCFollowWindowSize) already reaches every object, and has no position:
        // passed to what lies under (0, 0), the tactical map, it would come back here forever.
        if (event.Type == EventResize)
        {
            return;
        }

        // Hides itself to find what lies under it, and passes the event there.
        obj.ShowGuiWindow(false);
        std::shared_ptr<MCGuiObject> under = ScreenWindow()->FindObject(event.X, event.Y);
        obj.ShowGuiWindow(true);
        under->HandleEvent(event);
    }

    /// <summary>Event routine of the command palette buttons.</summary>
    void ToolPaletteButtonEvent(MCGuiObject& obj, MCGuiEvent& event)
    {
        if (event.Type != EventCallback)
        {
            return;
        }

        auto& button = static_cast<MCToolPalButton&>(obj);
        const MCInterfaceMode action = button.Action;

        if (action == ActionToggleZoom)
        {
            SoundSystem()->PlayInterfaceSample(0x2f);
            ToggleZoom();
            return;
        }

        if (button.Pushed != 0)
        {
            // One mode at a time.
            SoundSystem()->PlayInterfaceSample(0x35);

            for (size_t i = 0; i < NumModeButtons; i++)
            {
                MCToolPalButton& other = *TacticalMap()->ToolButtons[i];

                if (&other != &button && other.Pushed != 0)
                {
                    other.Pushed = false;
                }
            }

            TacticalInterface()->CurrentMode = action;
            TacticalInterface()->OneShotMode = true;
            return;
        }

        SoundSystem()->PlayInterfaceSample(0x34);
        TacticalInterface()->SetMode(MCInterfaceMode::None);
    }

    /// <summary>Event routine of the tab strip: its four tabs switch pages; its top toggles the MFD.</summary>
    void TabStripEvent(MCGuiObject& obj, MCGuiEvent& event)
    {
        const std::shared_ptr<MCTacticalMap> map = TacticalMap();

        if (event.Type == EventLeftDown)
        {
            // The strip's four tabs, top to bottom; clicking the open page's tab while shown does nothing.
            const int32_t offset = event.Y - obj.GlobalY();

            if (offset > 0x1b)
            {
                MCTacmapPage page = MCTacmapPage::Salvage;

                if (offset < 0x4c)
                {
                    page = MCTacmapPage::Map;
                }
                else if (offset < 0x74)
                {
                    page = MCTacmapPage::Info;
                }
                else if (offset < 0xae)
                {
                    page = MCTacmapPage::Mission;
                }
                else if (offset > 0xe2)
                {
                    return;
                }

                if (map->DisplayType == page && map->IsHidden() == 0)
                {
                    return;
                }

                SwitchPage(page);
                SoundSystem()->PlayInterfaceSample(0x36);
                return;
            }

            GuiSystem().Grab(obj.weak_from_this());
            map->HideMe(map->IsHidden() == 0);
        }
        else if (event.Type == EventLeftUp)
        {
            GuiSystem().Release();
        }
        else if (event.Type == EventMouseMove)
        {
            GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
        }
    }

    /// <summary>Event routine of the top tab: toggles the MFD.</summary>
    void TabTopEvent(MCGuiObject& /*obj*/, MCGuiEvent& event)
    {
        if (event.Type == EventLeftDown)
        {
            TacticalMap()->HideMe(TacticalMap()->IsHidden() == 0);
        }
        else if (event.Type == EventMouseMove)
        {
            GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
        }
    }

    /// <summary>Event routine of the bottom tab: the salvage (or chat) page.</summary>
    void TabBottomEvent(MCGuiObject& /*obj*/, MCGuiEvent& event)
    {
        if (event.Type == EventLeftDown)
        {
            if (TacticalMap()->DisplayType != MCTacmapPage::Salvage)
            {
                SoundSystem()->PlayInterfaceSample(0x36);
                SwitchPage(MCTacmapPage::Salvage);
            }
        }
        else if (event.Type == EventMouseMove)
        {
            GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
        }
    }
}

auto MCSalvageList::Add(std::shared_ptr<MCGameObject> obj) -> bool
{
    if (Contains(*obj))
    {
        return false;
    }

    _Objects.push_back(obj);
    return true;
}

auto MCSalvageList::Remove(const MCGameObject& obj) -> bool
{
    const auto found = std::ranges::find_if(_Objects, [&obj](const std::weak_ptr<MCGameObject>& listed)
                                            { return listed.lock().get() == &obj; });

    if (found == _Objects.end())
    {
        return false;
    }

    _Objects.erase(found);
    return true;
}

auto MCSalvageList::RemoveIf(const std::function<bool(const std::shared_ptr<MCGameObject>&)>& drop) -> void
{
    std::erase_if(_Objects, [&drop](const std::weak_ptr<MCGameObject>& listed) { return drop(LockObject(listed)); });
}

auto MCSalvageList::operator[](size_t index) const -> std::shared_ptr<MCGameObject>
{
    return LockObject(_Objects[index]);
}

auto MCSalvageList::Pins() const -> std::vector<std::shared_ptr<MCGameObject>>
{
    std::vector<std::shared_ptr<MCGameObject>> pins;
    pins.reserve(_Objects.size());

    for (const std::weak_ptr<MCGameObject>& listed : _Objects)
    {
        pins.push_back(LockObject(listed));
    }

    return pins;
}

auto MCSalvageList::Contains(const MCGameObject& obj) const -> bool
{
    return std::ranges::any_of(_Objects, [&obj](const std::weak_ptr<MCGameObject>& listed)
                               { return listed.lock().get() == &obj; });
}

auto TogglePalette() -> void
{
    TacticalMap()->TogglePalette();
}

MCTacticalMap::MCTacticalMap() = default;

MCTacticalMap::~MCTacticalMap() = default;

auto MCTacticalMap::TogglePalette() -> void
{
    if (IsShowing() == 0)
    {
        return;
    }

    PaletteFrame->ShowGuiWindow(PaletteFrame->IsShowing() == 0);

    if (PaletteFrame->IsShowing() == 0)
    {
        SoundSystem()->PlayInterfaceSample(0x41);
    }
    else
    {
        // Opening the palette drops the chosen mode.
        for (size_t i = 0; i < NumModeButtons; i++)
        {
            if (ToolButtons[i]->Pushed != 0)
            {
                ToolButtons[i]->Pushed = false;
                TacticalInterface()->SetMode(MCInterfaceMode::None);
            }
        }

        SoundSystem()->PlayInterfaceSample(0x40);
    }

    PaletteButton->Pushed = PaletteFrame->IsShowing();
}

auto MCTacticalMap::ShowStatus(std::optional<std::string> text) -> void
{
    if (!StatusLocked)
    {
        StatusText = std::move(text);
    }
}

auto MCTacticalMap::ReleaseStatusLine() -> void
{
    StatusLocked = false;
    StatusText.reset();
    GuiSystem().CursorHidden = false;
    GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
}

auto MCTacticalMap::SetRevealedBitmap(std::string_view fileName) const -> void
{
    const std::string gifName = GamePath(TerrainPath, fileName, ".gif");

    if (!FileExists(gifName))
    {
        return;
    }

    MCFile gifFile;
    gifFile.Open(gifName);
    const uint32_t size = gifFile.FileSize();

    if (size == 0)
    {
        return;
    }

    std::vector<uint8_t> gif(size);
    gifFile.Read(gif);
    gifFile.Close();

    // The mission's picture of the revealed area: one the game can't draw leaves the map unrevealed.
    if (const std::expected<int32_t, std::string> drawn = VfxGifDraw(VisibilityPort->Frame(), gif); !drawn.has_value())
    {
        SDL_Log("MCTacticalMap: %s not drawn: %s", gifName.c_str(), drawn.error().c_str());
    }
}

auto MCTacticalMap::Init(int32_t xPos, int32_t yPos) -> int32_t
{
    Zoom = 1;
    InfoObject.reset();
    ZoomOffset = 0;
    ObjectivesRevealed = false;
    PartShapes = {};
    MapVertexSide = MCTerrain::VerticesBlockSide * MCTerrain::BlocksMapSide;

    // The map picture.
    MapPort = std::make_unique<MCGuiPort>();
    std::string pictureName = GamePath(TerrainPath, Terrain()->Name, ".tga");
    int32_t result = MapPort->Init(pictureName.data());
    Assert(result == 0, static_cast<uint32_t>(result), " could not start tacticalMap ");
    MapWidth = MapPort->Bitmap().XMax;
    MapHeight = MapPort->Bitmap().YMax;

    // The fog of war: a port whose pixels are the home side's visible bits.
    VisibilityPort = std::make_unique<MCGuiPort>();
    VisibilityPort->Init(MapVertexSide, MapVertexSide);
    MCRenderer::DestroyTexture(VisibilityPort->Bitmap());
    VisibilityPort->Bitmap().Pixels = Terrain()->HomeVisibleBits().Window().Pixels;
    // Port: the fog of war is a kept frame surface: the reveals draw it on the GPU as well as in the flags the game
    // reads, and the map page samples the GPU's copy instead of uploading the flags whenever they change.
    MCRenderer::AddFrameSurface(VisibilityPort->SharedBitmap(), true);

    const float side = static_cast<float>(MapVertexSide) * static_cast<float>(MapVertexSide);
    MapDiagonal = std::sqrt(side + side) * MCTerrain::MetersPerVertex;
    MetersPerPixel = (MapDiagonal * DiagonalToPixels) / static_cast<float>(Zoom);

    MapBackground = LoadBackground("mfdmwn00.tga", "Error reading tacmap MFD background");
    InfoBackground = LoadBackground("mfddwn00.tga", "Error reading info MFD background");
    MissionBackground = LoadBackground("mfdbwn00.tga", "Error reading mission MFD background");

    // Port: the info page's data view backgrounds, which the original loaded each time it drew one (and skipped
    // when one failed to load).
    static constexpr std::array<std::string_view, 3> viewBackgroundNames = {"mfddwn01.tga", "mfddwn02.tga",
                                                                            "mfddwn03.tga"};

    for (size_t i = 0; i < viewBackgroundNames.size(); i++)
    {
        InfoViewBackgrounds[i] = std::make_unique<MCGuiPort>();

        if (InfoViewBackgrounds[i]->Init(viewBackgroundNames[i]) != 0)
        {
            InfoViewBackgrounds[i].reset();
        }
    }

    result = MCGuiObject::Init(xPos, yPos, 0x8c, 0xef, {});

    if (result != 0)
    {
        return result;
    }

    if (MultiPlayer() == nullptr)
    {
    }
    else
    {
        _OwnedSalvageBackground = LoadBackground("mfdswn01.tga", "Error reading salvage MFD background");
        ChatWindow =
            CreateChild<MCGuiChatWindow>([&](MCGuiChatWindow& chat) { result = chat.Init(6, 0x22, 0x82, 0x99, {}); });
        Assert(result == 0, static_cast<uint32_t>(result), "Error initing chat object");
        result = 0;
    }

    ChatPending = false;

    // The tabs along the MFD's right edge.
    TabTop = CreateChild<MCGuiObject>(
        [this](MCGuiObject& tab)
        {
            SetUpPicture(tab, Width(), 0, 0xc, 4, "mfdmts00.tga");
            tab.SetEventRoutine(TabTopEvent);
            tab.SetTransparent(true);
        });

    // The chat blinkers are the map's children, made (and added) while the strip is set up, before it is added.
    TabStrip = CreateChild<MCGuiObject>(
        [&](MCGuiObject& strip)
        {
            strip.SetDrawsLive();
            strip.Init(Width(), 4, 0xc, 0xe7, {});

            if (MultiPlayer() == nullptr)
            {
                strip.SetBackground("mfdmts01.tga");
            }
            else
            {
                const int32_t blinkerTop = strip.Bottom() - 0x3a;
                strip.SetBackground("mfdmts03.tga");

                const auto makeBlinker = [&](int32_t x, std::string_view picture)
                {
                    MCGuiHandle<MCGuiObject> blinker = CreateChild<MCGuiObject>(
                        [&](MCGuiObject& made)
                        {
                            made.SetDrawsLive();
                            result = made.Init(x, blinkerTop, 10, 0x3a, {});
                            Assert(result == 0, static_cast<uint32_t>(result), "Error initing chat blinker object");
                            made.SetBackground(picture);
                            made.SetDepth(10);
                        });
                    blinker->ShowGuiWindow(false);
                    blinker->SetEventRoutine(BlinkerHandleEvent);
                    return blinker;
                };

                ChatBlinkerOff = makeBlinker(Width() + 2, "mfdsts05.tga");
                ChatBlinkerOn = makeBlinker(Width(), "mfdsts04.tga");
            }

            strip.SetEventRoutine(TabStripEvent);
        });

    TabBottom = CreateChild<MCGuiObject>(
        [this](MCGuiObject& tab)
        {
            SetUpPicture(tab, Width(), 0xeb, 0xc, 4, "mfdmts02.tga");
            tab.SetEventRoutine(TabBottomEvent);
            tab.SetTransparent(true);
        });

    ObjectType = 6;
    ShowGuiWindow(false);
    SetBackColor(0x10);
    SetHideDirection(static_cast<MCDirection>(0));

    // The support buttons.
    struct SupportButton
    {
        int32_t X;
        int32_t CommandId;
        uint32_t HelpId;
        std::array<std::string_view, 3> Pictures;
    };

    static constexpr std::array<SupportButton, 4> supportButtons = {{
        {6, StrikeSmall, 0x93, {"mfdtbh00.tga", "mfdtbg00.tga", "mfdtbn00.tga"}},
        {0x26, StrikeLarge, 0x94, {"mfdtbh01.tga", "mfdtbg01.tga", "mfdtbn01.tga"}},
        {0x47, StrikeSensor, 0x95, {"mfdtbh02.tga", "mfdtbg02.tga", "mfdtbn02.tga"}},
        {0x68, StrikeCameraDrone, 0x96, {"mfdtbh03.tga", "mfdtbg03.tga", "mfdtbn03.tga"}},
    }};

    for (size_t i = 0; i < supportButtons.size(); i++)
    {
        const SupportButton& spec = supportButtons[i];
        ArtilleryButtons[i] = CreateChild<MCArtilleryButton>(
            [&spec](MCArtilleryButton& button)
            {
                SetUpButton(button, spec.X, 6, 0x1f, 0x16, spec.Pictures[0], spec.Pictures[1], spec.Pictures[2]);
                button.CommandId = spec.CommandId;
                button.HelpText = LoadHelpText(spec.HelpId);
            });
    }

    // The info page's data buttons.
    struct DataButton
    {
        size_t Index;
        int32_t X;
        void (&Callback)();
        std::array<std::string_view, 3> Pictures;
    };

    static constexpr std::array<DataButton, 3> dataButtonSpecs = {{
        {0, 0xf, ArmorFrontButton, {"mfddbh01.tga", "mfddbg01.tga", "mfddbn01.tga"}},
        {2, 0x56, PayloadButton, {"mfddbh02.tga", "mfddbg02.tga", "mfddbn02.tga"}},
        {1, 0x35, RearButton, {"mfddbh03.tga", "mfddbg03.tga", "mfddbn03.tga"}},
    }};

    for (const DataButton& spec : dataButtonSpecs)
    {
        DataButtons[spec.Index] = CreateChild<MCGuiToolButton>(
            [&spec](MCGuiToolButton& button)
            {
                SetUpButton(button, spec.X, 0xcc, 0x20, 0xc, spec.Pictures[0], spec.Pictures[1], spec.Pictures[2]);
                button.Framed = false;
                button.Callback().SetExec(spec.Callback);
            });
    }

    // The scroll buttons (disabled at 1x) and the zoom buttons.
    struct ScrollButton
    {
        int32_t X;
        int32_t Y;
        void (&Routine)(MCGuiObject&, MCGuiEvent&);
        std::array<std::string_view, 3> Pictures;
    };

    static constexpr std::array<ScrollButton, 4> scrollSpecs = {{
        {0x4b, 0xb5, ScrollUp, {"mfdmbh00.tga", "mfdmbg00.tga", "mfdmbn00.tga"}},
        {0x59, 0xc0, ScrollLeft, {"mfdmbh01.tga", "mfdmbg01.tga", "mfdmbn01.tga"}},
        {0x4b, 0xcd, ScrollDown, {"mfdmbh02.tga", "mfdmbg02.tga", "mfdmbn02.tga"}},
        {0x3f, 0xc0, ScrollRight, {"mfdmbh03.tga", "mfdmbg03.tga", "mfdmbn03.tga"}},
    }};

    for (size_t i = 0; i < scrollSpecs.size(); i++)
    {
        const ScrollButton& spec = scrollSpecs[i];
        ScrollButtons[i] = CreateChild<MCGuiButton>(
            [&spec](MCGuiButton& button)
            {
                SetUpButton(button, spec.X, spec.Y, 0xd, 0xb, spec.Pictures[0], spec.Pictures[1], spec.Pictures[2]);
                button.SetEventRoutine(spec.Routine);
            });
    }

    ScrollButtons[5] = CreateChild<MCGuiButton>(
        [](MCGuiButton& button)
        {
            SetUpButton(button, 0x74, 0xca, 0xd, 0xd, "mfdmbh04.tga", "mfdmbg04.tga", "mfdmbn04.tga");
            button.Callback().SetExec(ZoomOut);
            button.Disabled = true;
        });
    ScrollButtons[4] = CreateChild<MCGuiButton>(
        [](MCGuiButton& button)
        {
            SetUpButton(button, 0x74, 0xb6, 0xd, 0xd, "mfdmbh05.tga", "mfdmbg05.tga", "mfdmbn05.tga");
            button.Callback().SetExec(ZoomIn);
        });

    for (size_t i = 0; i < 4; i++)
    {
        ScrollButtons[i]->Disabled = true;
    }

    // The command palette, hidden until the palette button opens it.
    PaletteFrame = CreateChild<MCGuiObject>(
        [](MCGuiObject& frame)
        {
            frame.SetDrawsLive();
            frame.Init(0, 0xe9, 0x8c, 0x35, {});
            frame.SetDepth(10);
            frame.SetBackground("mfdcwn00.tga");
        });
    PaletteFrame->ShowGuiWindow(false);
    PaletteBottom = PaletteFrame->CreateChild<MCGuiObject>(
        [width = PaletteFrame->Width()](MCGuiObject& bottom)
        {
            bottom.SetDrawsLive();
            bottom.Init(width, 0, 2, 0x35, {});
            bottom.SetBackground("mfdcwn01.tga");
            bottom.SetTransparent(true);
        });

    PaletteButton = CreateChild<MCGuiToolButton>(
        [](MCGuiToolButton& button)
        {
            SetUpButton(button, 6, 0xe0, 9, 9, "mfdcwn02.tga", "mfdcwn03.tga", {});
            button.Framed = false;
            button.Callback().SetExec(::TogglePalette);
        });

    const auto makeMarker = [this](std::string_view picture)
    {
        return CreateChild<MCGuiObject>(
            [picture](MCGuiObject& marker)
            {
                SetUpPicture(marker, 0, 0, 0xb, 0xb, picture);
                marker.ShowGuiWindow(false);
            });
    };

    ScrollUpMarker = makeMarker("mfddbg04.tga");
    ScrollDownMarker = makeMarker("mfddbg05.tga");

    LastMapTime = -999.0f;

    // The palette's mode buttons, two rows of four.
    int32_t buttonX = 6;
    int32_t buttonY = 2;

    for (size_t i = 0; i < ToolButtons.size(); i++)
    {
        ToolButtons[i] = PaletteFrame->CreateChild<MCToolPalButton>(
            [&](MCToolPalButton& button)
            {
                button.Init(buttonX, buttonY, 0x1f, 0x16, {});
                button.Callback().SetMessage(button.weak_from_this(), EventCallback);
                button.SetEventRoutine(ToolPaletteButtonEvent);
                const bool zoomButton = i == 7;
                std::string pictureName = std::format("{}{:02}.tga", zoomButton ? "mfdcbn" : "mfdcbh", i);
                button.SetUpPicture(pictureName.data());
                pictureName = std::format("{}{:02}.tga", zoomButton ? "mfdcbh" : "mfdcbg", i);
                button.SetDownPicture(pictureName.data());
                pictureName = std::format("mfdcbn{:02}.tga", i);
                button.SetGrayPicture(pictureName.data());
                button.Framed = false;

                if (zoomButton)
                {
                    // The original also disabled zoom in multiplayer; the port allows it.
                    if (!Only45Pixel)
                    {
                        button.Action = ActionToggleZoom;
                        button.HelpText = LoadHelpText(0x91);
                    }
                    else
                    {
                        // No zoom: only the 45-pixel art is loaded.
                        button.SetGrayPicture("mfdcbn07a.tga");
                        button.Disabled = true;
                        button.HelpText = LoadHelpText(0x92);
                    }
                }
                else
                {
                    button.Action = ButtonActions[i];
                    button.HelpText = LoadHelpText(0x8a + static_cast<uint32_t>(i));
                }
            });
        buttonX += 1 + ToolButtons[i]->Width();

        if (i == 3)
        {
            buttonX = 6;
            buttonY = 0x19;
        }
    }

    VideoWindow = CreateChild<MCVideoWindow>(
        [](MCVideoWindow& video)
        {
            video.Init(6, 0xaa, 0x30, 0x30, {});
            video.SetBackground("mfdmwn01.tga");
        });

    const auto initText = [](MCGuiScrollTextObject& text) { text.Init(5, 0x22, 0x76, 0xb8, {}); };
    InfoText = CreateChild<MCGuiScrollTextObject>(initText);
    InfoText->ShowGuiWindow(false);
    SalvageText = CreateChild<MCGuiScrollTextObject>(initText);
    SalvageText->ShowGuiWindow(false);

    Salvage = {};
    ScrollX = 0;
    ScrollY = 0;
    SetDisplayType(MCTacmapPage::Map);

    // The map area, as rectangles and as a pane on the MFD's window.
    MapPane.Window = DisplayPort->Frame().Window;
    MapRect = {6, 0x22, 0x87, 0xa3};
    MapPane.X0 = 6;
    MapPane.Y0 = 0x22;
    MapPane.X1 = 0x87;
    MapPane.Y1 = 0xa3;
    ZoomInRect = {0x70, 0xb2, 0x89, 199};
    ZoomOutRect = {0x70, 199, 0xb2, 0xdb};

    for (std::unique_ptr<MCGuiPort>& port : InfoPorts)
    {
        port = std::make_unique<MCGuiPort>();
    }

    SetDataDisplayMode(0, -1);
    LastRefreshTime = 0;
    RefreshPage();

    for (size_t i = 0; i < TypeStrings.size(); i++)
    {
        TypeStrings[i] = TableString(0x7c + static_cast<uint32_t>(i));
    }

    for (size_t i = 0; i < StatusStrings.size(); i++)
    {
        StatusStrings[i] = TableString(0x78 + static_cast<uint32_t>(i));
    }

    ColorRemap = MCRegisteredBlock(256, MCDataKind::Tables);
    std::ranges::fill(ColorRemap.Bytes(), 0xff);
    ColorRemap.Bytes()[0xe6] = 0x13;
    ColorRemap.Bytes()[0xe8] = 0x13;
    return result;
}

auto MCTacticalMap::OnDestroy() -> void
{
    MapPort.reset();

    if (VisibilityPort != nullptr)
    {
        // The bitmap's pixels are the visible bits'; the original clears the pane's window first.
        MCRenderer::RemoveFrameSurface(VisibilityPort->Bitmap());
        VisibilityPort->Frame().Window.reset();
        VisibilityPort.reset();
    }

    for (std::unique_ptr<MCGuiPort>& port : InfoPorts)
    {
        port.reset();
    }

    MapBackground.reset();
    InfoBackground.reset();

    for (std::unique_ptr<MCGuiPort>& background : InfoViewBackgrounds)
    {
        background.reset();
    }

    MissionBackground.reset();
    _OwnedSalvageBackground.reset();
    PartShapes = {};
    ColorRemap = {};
    MouseInside = false;

    for (std::string& text : TypeStrings)
    {
        text.clear();
    }

    for (std::string& text : StatusStrings)
    {
        text.clear();
    }

    MCGuiObject::OnDestroy();
}

auto MCTacticalMap::RefreshPage() -> void
{
    if (IsShowing() == 0)
    {
        return;
    }

    if (DisplayType == MCTacmapPage::Info)
    {
        const std::shared_ptr<MCGameObject> obj = LockObject(InfoObject);
        const int32_t firstPixel = InfoText->FirstPixel;

        if (obj == nullptr || !IsMoverClass(obj->ObjectClass))
        {
            InfoText->ShowGuiWindow(false);
            return;
        }

        if (DataDisplayMode == 2)
        {
            // Payload: the weapon list.
            InfoText->ShowGuiWindow(true);
            DrawWeapons();
        }
        else
        {
            // Armor: the part diagram's colours.
            GetColors();
            InfoText->ShowGuiWindow(false);
        }

        if (obj->ObjectClass == MCObjectClass::BattleMech)
        {
            InfoText->FirstPixel = firstPixel;
            InfoText->PositionScrollTab();
        }
        else if (obj->ObjectClass == MCObjectClass::GroundVehicle)
        {
            if (DataDisplayMode == 1)
            {
                SetDataDisplayMode(0, 0);
            }

            InfoText->FirstPixel = firstPixel;
            InfoText->PositionScrollTab();
        }

        return;
    }

    if (DisplayType == MCTacmapPage::Mission)
    {
        // The home side's objectives, each with its type and its timer or status.
        MCGuiScrollTextObject& text = *InfoText;
        const int32_t firstPixel = text.FirstPixel;
        text.Clear();

        if (Turn > 1)
        {
            const auto count = static_cast<int32_t>(HomeTeam()->NumObjectives);
            int32_t objectiveNum = HomeTeam()->FirstObjective;

            for (int32_t i = 0; i < count; i++, objectiveNum++)
            {
                const MCScenarioObjective& objective = Scenario()->Objectives[objectiveNum];
                uint8_t color = 0;

                if (objective.Status == 0)
                {
                    color = 0xf2;
                }
                else if (objective.Status == 1)
                {
                    color = 0xb;
                }
                else if (objective.Status == 2)
                {
                    color = 0xef;
                }

                std::string line = std::format("{}--{}", i + 1, objective.Name);
                text.PrintWrapped(line.data(), color, -1);
                const uint32_t type = objective.Type + 1 > 3 ? 4 : objective.Type + 1;
                line = std::format("      {}", TypeStrings[type]);
                text.PrintWrapped(line.data(), color, -1);
                const float timeLeft = Scenario()->CheckObjectiveTimer(objectiveNum);

                if (timeLeft > 0.0)
                {
                    const auto seconds =
                        static_cast<int32_t>(std::floor(std::fmod(static_cast<double>(timeLeft), 60.0)));
                    const auto minutes = static_cast<int32_t>(timeLeft * (1.0 / 60.0));
                    line = std::format("{:02}:{:02}", minutes, seconds);
                }
                else
                {
                    const uint32_t status = objective.Status > 2 ? 3 : objective.Status;
                    line = std::format("      {}", StatusStrings[status]);
                }

                text.PrintWrapped(line.data(), color, -1);
                text.PrintBlank(0x1f);
            }
        }

        text.FirstPixel = firstPixel;
        text.ResetPortSize();
        text.PositionScrollTab();
    }
}

auto MCTacticalMap::HandleEvent(MCGuiEvent& event) -> void
{
    const int32_t screenX = event.X;
    const int32_t screenY = event.Y;
    const POINT local{screenX - GlobalX(), screenY - GlobalY()};

    // The zoom buttons' areas take the event whole.
    if (DisplayType == MCTacmapPage::Map)
    {
        if (PtInRect(ZoomInRect, local) != 0)
        {
            ScrollButtons[4]->HandleEvent(event);
            return;
        }

        if (PtInRect(ZoomOutRect, local) != 0)
        {
            ScrollButtons[5]->HandleEvent(event);
            return;
        }
    }

    const bool textPage = DisplayType != MCTacmapPage::Map;
    // The info and mission pages scroll InfoText, the salvage page SalvageText.
    MCGuiScrollTextObject& text = DisplayType == MCTacmapPage::Salvage ? *SalvageText : *InfoText;

    switch (event.Type)
    {
        case EventLeftDown:
        {
            if (DisplayType == MCTacmapPage::Map && PtInRect(MapRect, local) != 0)
            {
                MapDragging = true;
            }

            if (local.y > 0xe0)
            {
                // Below the pages: the palette toggle.
                TogglePalette();
                break;
            }

            if (textPage)
            {
                // Press and hold repeats.
                if (PtInRect(PageRects.front(), local) != 0)
                {
                    GuiSystem().Grab(weak_from_this());
                    ScrollUpMarker->ShowGuiWindow(true);
                    GuiSystem().AddTimer(weak_from_this(), ScrollStartTimer, TacticalInterface()->ScrollStart, 0, 0,
                                         false);
                    text.ReceiveClick(-1, 0);
                }
                else if (PtInRect(PageRects[1], local) != 0)
                {
                    GuiSystem().Grab(weak_from_this());
                    ScrollDownMarker->ShowGuiWindow(true);
                    GuiSystem().AddTimer(weak_from_this(), ScrollStartTimer, TacticalInterface()->ScrollStart, 0, 0,
                                         false);
                    text.ReceiveClick(1, 0);
                }
                else if (PtInRect(PageRects[2], local) != 0)
                {
                    // The info pages measure the click from InfoText's screen top, the salvage page from the track.
                    const int32_t yPos =
                        DisplayType == MCTacmapPage::Salvage ? local.y - PageRects[2].top : local.y - text.GlobalY();
                    text.ReceiveClick(0, yPos);
                }
            }
            break;
        }

        case EventLeftUp:
        {
            if (DisplayType == MCTacmapPage::Map && MapDragging)
            {
                // A click on the map moves the active camera there.
                MapDragging = false;

                if (PtInRect(MapRect, local) != 0)
                {
                    MCVector3D target(static_cast<float>(local.x), static_cast<float>(local.y), 0.0f);
                    TacMapToWorld(target, true);
                    const std::shared_ptr<MCCamera> camera = MainHolder()->GetActivePane()->GetCamera();
                    camera->ChangeTarget(nullptr, false);
                    camera->SetPosition(target);
                }
            }

            GuiSystem().RemoveTimer(*this, ScrollStartTimer);
            GuiSystem().RemoveTimer(*this, ScrollRepeatTimer);

            if (GuiSystem().Grabs(*this))
            {
                // Released on the top-right corner: toggles the MFD.
                if (local.x > 0x8c && local.y < 0x1c)
                {
                    HideMe(IsHidden() == 0);
                }
            }

            GuiSystem().Release();
            ScrollUpMarker->ShowGuiWindow(false);
            ScrollDownMarker->ShowGuiWindow(false);
            break;
        }

        case EventTimer:
        {
            if (event.Data == ChatBlinkTimer)
            {
                // The chat tab blinks while a message is unread.
                if (!ChatPending)
                {
                    const std::shared_ptr<MCGuiObject> blinker =
                        (DisplayType == MCTacmapPage::Salvage ? ChatBlinkerOn : ChatBlinkerOff).Lock();

                    if (blinker != nullptr)
                    {
                        blinker->ShowGuiWindow(true);
                    }

                    ChatPending = true;
                }
                else
                {
                    for (const std::shared_ptr<MCGuiObject>& blinker : {ChatBlinkerOff.Lock(), ChatBlinkerOn.Lock()})
                    {
                        if (blinker != nullptr)
                        {
                            blinker->ShowGuiWindow(false);
                        }
                    }

                    ChatPending = false;
                }
                break;
            }

            if (event.Data == ScrollStartTimer)
            {
                GuiSystem().RemoveTimer(*this, ScrollStartTimer);
                GuiSystem().AddTimer(weak_from_this(), ScrollRepeatTimer, TacticalInterface()->ScrollStart / 5, 0, 0,
                                     false);
            }
            else if (event.Data != ScrollRepeatTimer)
            {
                break;
            }

            if (textPage)
            {
                if (PtInRect(PageRects.front(), local) != 0)
                {
                    text.ReceiveClick(-1, 0);
                }
                else if (PtInRect(PageRects[1], local) != 0)
                {
                    text.ReceiveClick(1, 0);
                }
            }
            break;
        }

        case EventZoomIn:
        {
            ZoomIn();
            break;
        }

        case EventZoomOut:
        {
            ZoomOut();
            break;
        }
    }

    MCGuiObject::HandleEvent(event);
}

auto MCTacticalMap::MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) -> bool
{
    if (DisplayType != MCTacmapPage::Map)
    {
        MCGuiScrollTextObject& text = DisplayType == MCTacmapPage::Salvage ? *SalvageText : *InfoText;
        return text.MouseWheel(steps, xPos, yPos);
    }

    // As clicking the zoom buttons (up in, down out), which are disabled at the ends.
    for (; steps != 0; steps += steps < 0 ? 1 : -1)
    {
        if (ScrollButtons[steps < 0 ? 4 : 5]->Disabled != 0)
        {
            break;
        }

        if (steps < 0)
        {
            ZoomIn();
        }
        else
        {
            ZoomOut();
        }
    }

    return true;
}

auto MCTacticalMap::Display() -> void
{
    if (IsShowing() == 0)
    {
        return;
    }

    // Hidden and at rest the MFD lies off the screen (but for its tabs); the original showed its picture as it was,
    // without updating it.
    const bool atRest = IsHidden() != 0 && HideOffset == 0;

    if (!atRest && HideOffset != 0)
    {
        // Sliding: step, then stop once off screen (hiding) or back home (showing).
        MoveTo(X() + HideOffset, Y(), true);

        if (Hidden != 0)
        {
            const tagRECT screen = {2, 0, GuiSystem().Width(), GuiSystem().Height()};

            if (RectIntersect(screen) == 0)
            {
                HideOffset = 0;
            }
        }
        else
        {
            const bool home = HideOffset < 0 ? (HomeX >= GlobalX() && HomeY >= GlobalY())
                                             : (HomeX <= GlobalX() && HomeY <= GlobalY());

            if (home)
            {
                MoveTo(HomeX - ParentObject()->GlobalX(), HomeY - ParentObject()->GlobalY(), true);
                HideOffset = 0;
            }
        }
    }

    const bool updating = !atRest && (IsHidden() == 0 || HideOffset != 0);

    if (updating)
    {
        switch (DisplayType)
        {
            case MCTacmapPage::Map:
            {
                UpdateMapPage();
                break;
            }
            case MCTacmapPage::Info:
            case MCTacmapPage::Mission:
            {
                // OB-177: every frame, where the original rebuilt the page every 500 ms and on each click.
                RefreshPage();
                break;
            }
            case MCTacmapPage::Salvage:
            {
                if (MultiPlayer() == nullptr && MCPort::Milliseconds() > LastRefreshTime + 500)
                {
                    LastRefreshTime = MCPort::Milliseconds();
                    UpdateSalvage();
                }
                break;
            }
        }
    }

    if (MouseInside)
    {
        GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
    }

    // Port: the MFD draws itself, then its children (the original copied its picture, then displayed them).
    DrawInFramePass(*DisplayPort);

    // The original revealed the objectives' areas as it drew the map's units, after the fog of war.
    if (updating && DisplayType == MCTacmapPage::Map)
    {
        RevealObjectives();
    }
}

auto MCTacticalMap::UpdateMapPage() -> void
{
    // The mission timer, rewritten once a second.
    if (Scenario()->TimeLimit >= 0 && LastMapTime + 1.0f < ActualTime)
    {
        LastMapTime = ActualTime;
        const float remaining = static_cast<float>(Scenario()->TimeLimit) - ActualTime;

        if (remaining >= 0.0f)
        {
            const auto seconds = static_cast<int32_t>(std::fmod(static_cast<double>(remaining), 60.0));
            const auto minutes = static_cast<int32_t>(remaining * (1.0f / 60.0f));
            MapTimeText = std::format("{:02}:{:02}", minutes, seconds);
            MapTimeRed = false;
        }
        else
        {
            MapTimeText = "00:00";
            MapTimeRed = true;
        }

        MapTimeShown = true;
    }

    // Markers blink five times a second.
    MarkerBlinkTime = FrameLength + MarkerBlinkTime;

    if (MarkerBlinkTime > 0.2)
    {
        MarkerBlinkTime = 0.0f;
        MarkersLit = !MarkersLit;
    }
}

auto MCTacticalMap::RevealObjectives() -> void
{
    if (ObjectivesRevealed)
    {
        return;
    }

    // Once a pending objective with a position is found, every objective's area is revealed in the fog of war.
    const auto numObjectives = static_cast<int32_t>(HomeTeam()->NumObjectives);

    for (int32_t i = 0; i < numObjectives; i++)
    {
        const MCScenarioObjective& objective = Scenario()->Objectives[HomeTeam()->FirstObjective + i];

        if (objective.Position[0] == -99.0f || objective.Position[1] == -99.0f || objective.Position[2] == -99.0f ||
            objective.Status != 0)
        {
            continue;
        }

        for (int32_t j = 0; j < numObjectives; j++)
        {
            const MCScenarioObjective& area = Scenario()->Objectives[HomeTeam()->FirstObjective + j];

            if (area.Radius <= 0.0)
            {
                continue;
            }

            const auto column = static_cast<float>(std::floor(static_cast<double>(
                MCTerrain::OneOvermetersPerVertex * (area.Position[0] - MCTerrain::MapTopLeft3d100.X))));
            const auto row = static_cast<float>(std::floor(static_cast<double>(
                MCTerrain::OneOvermetersPerVertex * (MCTerrain::MapTopLeft3d100.Y - area.Position[1]))));
            const auto xc = static_cast<int32_t>(std::floor(static_cast<double>(column)));
            const auto yc = static_cast<int32_t>(std::floor(static_cast<double>(row)));
            const auto radius = static_cast<int32_t>(area.Radius / MetersPerPixel * WorldUnitsPerMeter);
            VfxEllipseFill(VisibilityPort->Frame(), xc, yc, radius, radius, 0x14);
        }

        ObjectivesRevealed = true;
        return;
    }
}

auto MCTacticalMap::HideMe(bool hide) -> void
{
    if (HideOffset != 0)
    {
        return;
    }

    if (hide == 0)
    {
        // Shown: the chat tab stops blinking.
        GuiSystem().RemoveTimer(*this, ChatBlinkTimer);

        for (const std::shared_ptr<MCGuiObject>& blinker : {ChatBlinkerOn.Lock(), ChatBlinkerOff.Lock()})
        {
            if (blinker != nullptr)
            {
                blinker->ShowGuiWindow(false);
            }
        }
    }
    else
    {
        // Port fix: StopVideo checks the movie window, which the original ends without checking.
        StopVideo();
    }

    if (Hidden == hide)
    {
        return;
    }

    if (Turn > 1)
    {
        SoundSystem()->PlayInterfaceSample(0x3b);
    }

    if (hide != 0)
    {
        // Slide off the left edge, from here.
        HomeX = GlobalX();
        HomeY = GlobalY();
        HideOffset = (2 - GlobalX()) - Width();
        Hidden = hide;
        return;
    }

    // Slide back home.
    Hidden = false;
    HideOffset = HomeX != GlobalX() ? HomeX - GlobalX() : HomeY - GlobalY();
}

auto MCTacmapProjection::WorldToMap(MCVector3D& pos, bool scrolled) const -> void
{
    // Rotate 45 degrees (the map is drawn diamond-wise), then scale to pixels about the map's centre.
    const float worldX = pos.X;
    const float worldY = pos.Y;
    pos.X = worldX * MapRotation + worldY * MapRotation;
    const float rotatedY = worldY * MapRotation - worldX * MapRotation;
    pos.Y = rotatedY;

    if (scrolled)
    {
        pos.X = pos.X / MetersPerPixel;
        pos.Y = rotatedY / MetersPerPixel;
        pos.Z = pos.Z / MetersPerPixel;
        const auto zoom = static_cast<float>(Zoom);
        pos.X =
            (pos.X + MapCenterX) - (MapPictureSide / static_cast<float>(MapWidth)) * zoom * static_cast<float>(ScrollX);
        pos.Y = ((MapHalfSide - pos.Y) + MapTop) -
                (MapPictureSide / static_cast<float>(MapHeight)) * zoom * static_cast<float>(ScrollY);
        return;
    }

    const float scale = static_cast<float>(Zoom) * MetersPerPixel;
    const float pixelX = pos.X / scale;
    const float pixelY = rotatedY / scale;
    pos.Z = pos.Z / scale;
    pos.X = pixelX + MapHalfSide;
    pos.Y = MapHalfSide - pixelY;
}

auto MCTacmapProjection::MapToWorld(MCVector3D& pos, bool scrolled) const -> void
{
    pos.Z = 0.0f;

    if (scrolled)
    {
        const auto zoom = static_cast<float>(Zoom);
        pos.X =
            ((MapPictureSide / static_cast<float>(MapWidth)) * zoom * static_cast<float>(ScrollX) + pos.X) - MapLeft;
        pos.Y =
            ((MapPictureSide / static_cast<float>(MapHeight)) * zoom * static_cast<float>(ScrollY) + pos.Y) - MapTop;
    }

    const float pixelX = pos.X - MapHalfSide;
    const float pixelY = MapHalfSide - pos.Y;

    if (scrolled)
    {
        pos.X = pixelX * MetersPerPixel;
        pos.Y = pixelY * MetersPerPixel;
    }
    else
    {
        const float scale = static_cast<float>(Zoom) * MetersPerPixel;
        pos.X = pixelX * scale;
        pos.Y = pixelY * scale;
    }

    // Rotate back.
    const float rotatedX = pos.X;
    const float rotatedY = pos.Y;
    pos.X = rotatedX * MapRotation + rotatedY * -MapRotation;
    pos.Y = rotatedY * MapRotation - rotatedX * -MapRotation;
}

auto MCTacticalMap::WorldToTacMap(MCVector3D& pos, bool scrolled) const -> void
{
    Projection().WorldToMap(pos, scrolled);
}

auto MCTacticalMap::TacMapToWorld(MCVector3D& pos, bool scrolled) const -> void
{
    // Back to the world, and stand the point on the ground.
    Projection().MapToWorld(pos, scrolled);
    pos.Z = TerrainElevationAt(pos);
}

auto MCTacticalMap::SetDisplayType(MCTacmapPage type) -> void
{
    DisplayType = type;

    // Hide every page's parts, then show the new page's.
    SalvageText->ShowGuiWindow(false);

    if (MultiPlayer() != nullptr)
    {
        ChatWindow->ShowGuiWindow(false);

        if (GuiSystem().TextObject().get() == ChatWindow->ChatInput.Lock().get())
        {
            GuiSystem().ReleaseText();
        }
    }

    InfoText->ShowGuiWindow(false);
    InfoText->Clear();

    for (const MCGuiHandle<MCGuiButton>& button : ScrollButtons)
    {
        button->ShowGuiWindow(false);
    }

    VideoWindow->ShowGuiWindow(false);
    StopVideo();

    for (const MCGuiHandle<MCGuiToolButton>& button : DataButtons)
    {
        button->ShowGuiWindow(false);
    }

    switch (type)
    {
        case MCTacmapPage::Map:
        {
            if (TabHighlighted)
            {
                TabBottom->SetBackground("mfdmts02.tga");
            }

            TabStrip->SetBackground(MultiPlayer() == nullptr ? "mfdmts01.tga" : "mfdmts03.tga");
            TabHighlighted = false;
            // (The original copied the page's background into the MFD's picture here; draw shows it each frame.)

            for (const MCGuiHandle<MCGuiButton>& button : ScrollButtons)
            {
                button->ShowGuiWindow(true);
            }

            VideoWindow->ShowGuiWindow(true);
            break;
        }

        case MCTacmapPage::Info:
        {
            for (const MCGuiHandle<MCGuiToolButton>& button : DataButtons)
            {
                button->ShowGuiWindow(true);
            }

            if (TabHighlighted)
            {
                TabBottom->SetBackground("mfddts02.tga");
            }

            TabStrip->SetBackground(MultiPlayer() == nullptr ? "mfddts01.tga" : "mfddts03.tga");
            TabHighlighted = false;

            // (The original wiped the picture without a unit, and copied the page's background; draw does both.)
            if (const std::shared_ptr<MCGameObject> info = LockObject(InfoObject))
            {
                SetID(info->PartId);
            }

            InfoText->MoveTo(7, 0x5e, false);
            InfoText->Resize(0x74, 0x69);
            InfoText->ShowGuiWindow(true);
            SetPageRects(*this, 0x5d, 0x68, 0xbe, 0xc9);
            InfoText->FirstPixel = 0;
            RefreshPage();
            return;
        }

        case MCTacmapPage::Mission:
        {
            if (TabHighlighted)
            {
                TabBottom->SetBackground("mfdbts02.tga");
            }

            TabStrip->SetBackground(MultiPlayer() == nullptr ? "mfdbts01.tga" : "mfdbts03.tga");
            TabHighlighted = false;
            InfoText->MoveTo(5, 0x22, false);
            InfoText->Resize(0x76, 0xb8);
            InfoText->ShowGuiWindow(true);
            SetPageRects(*this, 0x22, 0x2d, 0xcf, 0xda);
            PageRects[2].bottom = 0xce;
            RefreshPage();
            return;
        }

        case MCTacmapPage::Salvage:
        {
            TabBottom->SetBackground("mfdsts02.tga");
            TabStrip->SetBackground(MultiPlayer() == nullptr ? "mfdsts01.tga" : "mfdsts03.tga");
            TabHighlighted = true;

            if (MultiPlayer() == nullptr)
            {
                SalvageText->ShowGuiWindow(true);
                RefreshSalvageList();
                SetPageRects(*this, 0x22, 0x2d, 0xcf, 0xda);
                PageRects[2].bottom = 0xce;
                RefreshPage();
                return;
            }

            // Multiplayer: the chat window, and the tab stops blinking.
            GuiSystem().RemoveTimer(*this, ChatBlinkTimer);

            for (const std::shared_ptr<MCGuiObject>& blinker : {ChatBlinkerOn.Lock(), ChatBlinkerOff.Lock()})
            {
                if (blinker != nullptr)
                {
                    blinker->ShowGuiWindow(false);
                }
            }

            ChatWindow->ShowGuiWindow(true);
            break;
        }
    }

    RefreshPage();
}

auto MCTacticalMap::ScrollMap(int32_t dx, int32_t dy) -> void
{
    if (ScrollInPicture(ScrollX + dx, MapWidth, Zoom))
    {
        ScrollX += dx;
    }

    if (ScrollInPicture(ScrollY + dy, MapHeight, Zoom))
    {
        ScrollY += dy;
    }
}

auto MCTacticalMap::SetScrollMapPosition(int32_t x, int32_t y) -> void
{
    const int32_t oldX = ScrollX;
    const int32_t oldY = ScrollY;
    ScrollY = y;
    ScrollX = x;
    const int32_t speed = TacticalInterface()->TacScrollSpeed;
    const int32_t stepX = oldX < x ? -speed : speed;
    const int32_t stepY = oldY < y ? -speed : speed;
    bool doneX = false;
    bool doneY = false;

    while (!doneX || !doneY)
    {
        if (!ScrollInPicture(ScrollX, MapWidth, Zoom))
        {
            ScrollX += stepX;
        }
        else
        {
            doneX = true;
        }

        if (!ScrollInPicture(ScrollY, MapHeight, Zoom))
        {
            ScrollY += stepY;
        }
        else
        {
            doneY = true;
        }
    }
}

auto MCTacticalMap::GetVideoRect() const -> tagRECT
{
    // The name line's height (unscaled), then lineFont back to its double scale.
    LineFont()->Scaled = false;
    LineFont()->Scale = 1.0f;
    const uint8_t lineHeight = LineFont()->FontHeight;
    LineFont()->Scale = 2.0f;
    LineFont()->Scaled = true;
    tagRECT rect;
    rect.left = VideoWindow->GlobalX();
    rect.top = VideoWindow->GlobalY() + lineHeight + 4;
    rect.right = VideoWindow->Width();
    rect.bottom = VideoWindow->Height() - (lineHeight + 4);
    return rect;
}

auto MCTacticalMap::AddSalvage(std::shared_ptr<MCGameObject> obj) -> int
{
    if (obj->ObjectClass != MCObjectClass::BattleMech && obj->ObjectClass != MCObjectClass::GroundVehicle &&
        obj->IsBuilding() == 0)
    {
        return 0;
    }

    // A mech whose status byte is 2 isn't salvage.
    if (static_cast<uint8_t>(obj->Status) == 2 && obj->ObjectClass == MCObjectClass::BattleMech)
    {
        return 0;
    }

    if (Salvage.Contains(*obj))
    {
        return -1;
    }

    if (MultiPlayer() == nullptr && obj->IsBuilding() != 0)
    {
        SoundSystem()->PlayBettySample(2);
    }

    Salvage.Add(obj);
    AddSalvageString(*obj);
    return -1;
}

auto MCTacticalMap::RemoveSalvage(const MCGameObject& obj, int refresh) -> int
{
    if (!Salvage.Remove(obj))
    {
        return 0;
    }

    if (refresh != 0)
    {
        RefreshSalvageList();
    }

    return -1;
}

auto MCTacticalMap::UpdateSalvage() -> void
{
    // Drop destroyed units.
    Salvage.RemoveIf([](const std::shared_ptr<MCGameObject>& obj)
                     { return obj != nullptr && IsMoverClass(obj->ObjectClass) && obj->IsDestroyed() != 0; });
    RefreshSalvageList();
}

auto MCTacticalMap::RefreshSalvageList() -> void
{
    const int32_t firstPixel = SalvageText->FirstPixel;
    SalvageText->Clear();

    // Port: an object gone from the world (OB-170) has no line; the original read the freed object.
    for (const std::shared_ptr<MCGameObject>& obj : Salvage.Pins())
    {
        if (obj != nullptr)
        {
            AddSalvageString(*obj);
        }
    }

    SalvageText->FirstPixel = firstPixel;
    SalvageText->ResetPortSize();
    SalvageText->PositionScrollTab();
}

auto MCTacticalMap::SetID(int32_t partId) -> void
{
    auto obj = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(partId));

    if (DisplayType != MCTacmapPage::Info)
    {
        InfoObject = obj;
        return;
    }

    // The pictures of the unit shown before go: each port starts empty again.
    for (std::unique_ptr<MCGuiPort>& port : InfoPorts)
    {
        if (port != nullptr)
        {
            port = std::make_unique<MCGuiPort>();
        }
    }

    if (obj == nullptr || !IsMoverClass(obj->ObjectClass))
    {
        return;
    }

    // Port fix: the original leaves the shape file's name unset for elementals and other movers (whatever the stack
    // held); the port keeps the last one, starting with the generic vehicle's.
    static std::string shapeName = "vr106";

    if (obj->ObjectClass == MCObjectClass::BattleMech)
    {
        // A mech: front, rear and payload views, and the pilot's picture.
        MCGuiToolButton& front = *DataButtons[0];
        front.SetUpPicture("mfddbh01.tga");
        front.SetDownPicture("mfddbg01.tga");
        front.SetGrayPicture("mfddbn01.tga");
        front.MoveTo(0xf, 0xcc, false);
        DataButtons[2]->MoveTo(0x56, 0xcc, false);
        DataButtons[1]->ShowGuiWindow(true);
        shapeName = std::format("mechrep{:02}", obj->GetObjectType()->IconNumber);
        InfoPorts[0]->Init(obj->GetPilot()->Picture.data());
    }
    else if (obj->ObjectClass == MCObjectClass::GroundVehicle)
    {
        // A vehicle: no rear view; its passengers' pictures.
        MCGuiToolButton& front = *DataButtons[0];
        front.SetUpPicture("mfddbh00.tga");
        front.SetDownPicture("mfddbg00.tga");
        front.SetGrayPicture("mfddbn00.tga");
        front.MoveTo(0x21, 0xcc, false);
        DataButtons[2]->MoveTo(0x4b, 0xcc, false);
        DataButtons[1]->ShowGuiWindow(false);

        if (DataDisplayMode == 1)
        {
            SetDataDisplayMode(0, 0);
        }

        const auto vehicle = std::static_pointer_cast<MCGroundVehicle>(obj);

        // Original behaviour: the pictures go by seat, while the info page shows the passengers packed.
        for (int32_t seat = 0; seat < vehicle->Seats; seat++)
        {
            if (vehicle->Passengers[seat].lock() != nullptr)
            {
                InfoPorts[static_cast<size_t>(seat)]->Init(vehicle->Passengers[seat].lock()->Picture.data());
            }
        }

        const auto icon = obj->GetObjectType()->IconNumber;
        shapeName = icon == 0 ? std::string("vr106") : std::format("vr{}", icon);
    }

    // The part diagram's shapes.
    MCFile shapeFile;

    if (shapeFile.Open(GamePath(ArtPath, shapeName, ".shp")) != 0)
    {
        Fatal(0, "Unable to open damage display shape file");
    }

    PartShapes = MCRegisteredBlock(shapeFile.GetLength(), MCDataKind::Shapes);
    shapeFile.Read(PartShapes.Bytes());
    shapeFile.Close();
    InfoObject = obj;
    RefreshPage();
}

auto MCTacticalMap::UpdateOrderPalette() -> void
{
    // A mode chosen: only its button pushed.
    const MCInterfaceMode mode = TacticalInterface()->CurrentMode;

    if (mode != MCInterfaceMode::None)
    {
        for (const MCGuiHandle<MCToolPalButton>& button : ToolButtons)
        {
            if (button->Action == mode)
            {
                if (button->Pushed == 0)
                {
                    button->Pushed = true;
                }
            }
            else if (button->Pushed != 0)
            {
                button->Pushed = false;
            }
        }

        return;
    }

    if (!TacticalInterface()->AnySelected())
    {
        // Nothing selected: every mode button released and grayed.
        for (size_t i = 0; i < NumModeButtons; i++)
        {
            MCToolPalButton& button = *ToolButtons[i];

            if (button.Pushed != 0)
            {
                button.Pushed = false;
                TacticalInterface()->SetMode(MCInterfaceMode::None);
            }

            if (button.Disabled == 0)
            {
                button.Disabled = true;
            }
        }

        return;
    }

    // A selection: released and enabled, the jump button only if every selected unit can jump.
    for (size_t i = 0; i < NumModeButtons; i++)
    {
        MCToolPalButton& button = *ToolButtons[i];
        button.Pushed = false;

        if (button.Action == ActionJump)
        {
            button.Disabled = !TacticalInterface()->CanSelectionJump();
        }
        else
        {
            button.Disabled = false;
        }
    }
}

auto MCTacticalMap::SetDataDisplayMode(char mode, int silent) -> void
{
    if (DataDisplayMode == mode)
    {
        return;
    }

    DataDisplayMode = mode;

    for (size_t i = 0; i < DataButtons.size(); i++)
    {
        DataButtons[i]->Pushed = static_cast<size_t>(mode) == i;
    }

    if (silent == 0)
    {
        SoundSystem()->PlayInterfaceSample(0x47);
    }
}

auto MCTacticalMap::ToggleZoom() -> void
{
    MCToolPalButton& button = *ToolButtons[7];
    button.Pushed = button.Pushed == 0;
}

auto MCTacticalMap::HandleChatMessage(uint32_t fromID, std::span<const uint8_t> message) -> void
{
    ChatWindow->HandleNetworkMessage(fromID, message);

    if (IsHidden() != 0 || DisplayType != MCTacmapPage::Salvage)
    {
        // Not on show: blink the chat tab.
        ChatPending = true;
        GuiSystem().AddTimer(weak_from_this(), ChatBlinkTimer, 500, 0, 0, false);
        const std::shared_ptr<MCGuiObject> blinker =
            (DisplayType == MCTacmapPage::Salvage ? ChatBlinkerOn : ChatBlinkerOff).Lock();

        if (blinker != nullptr)
        {
            blinker->ShowGuiWindow(true);
        }
    }

    if (SoundSystem() != nullptr)
    {
        SoundSystem()->PlayInterfaceSample(0x11);
    }
}

auto MCTacticalMap::ActivateArtillery(int32_t button, int arm) -> void
{
    if (button < 0 || button >= 4)
    {
        return;
    }

    MCArtilleryButton& strike = *ArtilleryButtons[static_cast<size_t>(button)];
    MCGuiEvent event;

    if (arm == 0)
    {
        // Disarm as if Escape were pressed.
        strike.KeyArmed = false;
        event.Clear();
        event.Type = EventKeyUp;
        event.Key = 0x1b;
    }
    else
    {
        // Arm as if clicked (pressed, then released).
        if (strike.KeyArmed)
        {
            return;
        }

        strike.KeyArmed = true;
        event.Clear();
        event.Type = EventLeftDown;
        strike.HandleEvent(event);
        event.Type = EventLeftUp;
    }

    strike.HandleEvent(event);
}
