#include "stdafx.h"
#include "camera/MCCameraList.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "camera/MCMainWindow.h"
#include "camera/MCViewWindow.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGameContext.h"
#include "platform/MCRenderer.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

std::string CameraPath = "data\\cameras\\";

auto CameraList() -> std::shared_ptr<MCCameraList>
{
    return MCGameContext::Current().CameraList();
}

auto Eye() -> std::shared_ptr<MCCamera>
{
    const std::shared_ptr<MCCameraList> list = CameraList();
    return list != nullptr ? list->Eye() : nullptr;
}

auto EyeRef() -> MCOptionalRef<MCCamera>
{
    const std::shared_ptr<MCCamera> eye = Eye();
    return eye != nullptr ? MCOptionalRef<MCCamera>(*eye) : std::nullopt;
}

MCCameraList::MCCameraList() = default;

MCCameraList::~MCCameraList()
{
    _Cameras.clear();
    CurrentCamera.reset();
    PauseShape() = {};
    AskedShape() = {};
    CloseGuiObject(_MainHolder);
}

auto MCCameraList::Load(std::string_view fileName) -> std::expected<void, std::string>
{
    MCFitIniFile cameraFile;

    if (const int32_t result = cameraFile.Open(GamePath(CameraPath, fileName, ".fit")); result != 0)
    {
        return std::unexpected(
            std::format("could not open the camera file {} ({:#x})", fileName, static_cast<uint32_t>(result)));
    }

    if (cameraFile.SeekBlock("CameraInfo") != 0)
    {
        return std::unexpected("no CameraInfo block in the camera file");
    }

    const MCFitResult<uint32_t> numCameras = cameraFile.Read<uint32_t>("NumCameras");

    if (!numCameras.has_value())
    {
        return std::unexpected("no NumCameras in the camera file");
    }

    if (*numCameras > MaxCameras)
    {
        return std::unexpected(std::format("{} cameras; the most is {}", *numCameras, MaxCameras));
    }

    // Cameras whose count reads as negative (2^31 or more) are none.
    for (int32_t i = 0; i < static_cast<int32_t>(*numCameras); i++)
    {
        bool objectCamera = false;

        if (cameraFile.SeekBlock(std::format("Camera{}", i)) != 0)
        {
            if (cameraFile.SeekBlock(std::format("ObjectCamera{}", i)) != 0)
            {
                return std::unexpected(std::format("no Camera{0} or ObjectCamera{0} block in the camera file", i));
            }

            objectCamera = true;
        }

        auto camera = std::make_shared<MCCamera>();

        if (std::expected<void, std::string> loaded = camera->Load(cameraFile, objectCamera, i + 1, *this); !loaded)
        {
            return loaded;
        }

        Add(std::move(camera));
    }

    return {};
}

auto MCCameraList::Add(std::shared_ptr<MCCamera> camera) -> void
{
    _Cameras.push_back(std::move(camera));
}

auto MCCameraList::MainHolder() const -> std::shared_ptr<MCMainWindow>
{
    return _MainHolder.Lock();
}

auto MCCameraList::EnsureMainHolder() -> std::expected<void, std::string>
{
    if (_MainHolder == nullptr)
    {
        // On the screen, hidden until a camera in it first activates (the original added it to the screen then).
        const std::expected<MCGuiHandle<MCMainWindow>, int32_t> holder = ScreenWindow()->TryCreateChild<MCMainWindow>(
            [this](MCMainWindow& made)
            {
                _MainHolder = made.WeakAs<MCMainWindow>();
                const int32_t result = made.Init();
                made.ShowGuiWindow(false);
                return result;
            });

        if (!holder.has_value())
        {
            return std::unexpected(
                std::format("could not open the main window ({:#x})", static_cast<uint32_t>(holder.error())));
        }

        _MainHolder = *holder;
    }

    return {};
}

auto MCCameraList::ActivateAllReady() -> std::shared_ptr<MCCamera>
{
    CurrentCamera.reset();
    std::shared_ptr<MCCamera> first;

    for (const std::shared_ptr<MCCamera>& camera : _Cameras)
    {
        if (camera->Ready && camera->Activate() == 0 && first == nullptr)
        {
            first = camera;
        }
    }

    CurrentCamera = first;
    return first;
}

auto MCCameraList::RenderView(MCGuiObject& window) -> void
{
    Terrain()->ClearUsedBlocks();
    const std::weak_ptr<MCCamera> savedEye = _Eye;

    for (const std::shared_ptr<MCCamera>& camera : _Cameras)
    {
        if (camera->Active && camera->View().get() == &window)
        {
            _Eye = camera;
            camera->Render();
        }

        _Eye = savedEye;
    }
}

auto MCCameraList::ShownViews() -> std::vector<std::pair<std::shared_ptr<MCCamera>, std::shared_ptr<MCViewWindow>>>
{
    std::vector<std::pair<std::shared_ptr<MCCamera>, std::shared_ptr<MCViewWindow>>> shown;

    for (const std::shared_ptr<MCCamera>& camera : _Cameras)
    {
        if (std::shared_ptr<MCViewWindow> view = camera->Active ? camera->View() : nullptr;
            view != nullptr && view->ShowsWorld() &&
            std::ranges::none_of(shown, [&view](const auto& taken) { return taken.second == view; }))
        {
            shown.emplace_back(camera, std::move(view));
        }
    }

    return shown;
}

namespace
{
    /// <summary>
    /// While one lives, the first view's draw leaves play as it would be without it. The camera draws the ground alone
    /// (MCCamera::DrawsGroundOnly): the original's objects do part of play as they draw (a building steps its animation
    /// and its fire's damage and starts its sound), the units have no place on screen before their first update, and
    /// the particles and weather step from the time they were last drawn. The dice are put back where they were
    /// (MCPort::RandState): a draw that rolled them would change every roll after it, and in a match a machine not
    /// drawing on the GPU draws no first view. And the blocks the draw marks are put back: the map's objects in the
    /// blocks drawn are the ones an update moves on (MCObjectList::Update).
    /// </summary>
    class MCPlayAsItWas
    {
    public:
        MCPlayAsItWas()
            : _GroundOnly(std::exchange(MCCamera::DrawsGroundOnly, true))
            , _Dice(MCPort::RandState())
            , _Blocks(Terrain()->MarkedBlocks())
        {
        }

        ~MCPlayAsItWas()
        {
            MCCamera::DrawsGroundOnly = _GroundOnly;
            MCPort::SeedRand(_Dice);
            Terrain()->RestoreMarkedBlocks(std::move(_Blocks));
        }

        MCPlayAsItWas(const MCPlayAsItWas&) = delete;
        MCPlayAsItWas& operator=(const MCPlayAsItWas&) = delete;

    private:
        bool _GroundOnly;
        uint32_t _Dice;
        MCTerrain::MCMarkedBlocks _Blocks;
    };

    /// <summary>Whether <paramref name="view"/>'s world is under the screen (its underlay set).</summary>
    bool Underlaid(const MCViewWindow& view)
    {
        return std::ranges::any_of(MCRenderer::Underlays(), [key = MCAddressKey::Of(view)](const MCUnderlay& underlay)
                                   { return underlay.Owner == key; });
    }
}

auto MCCameraList::DrawFirstViews() -> std::vector<MCFirstView>
{
    std::vector<MCFirstView> drawn;

    if (Terrain() == nullptr)
    {
        return drawn;
    }

    const MCPlayAsItWas asItWas;

    for (const auto& [camera, view] : ShownViews())
    {
        drawn.push_back({view, Underlaid(*view)});
        view->StartZoom();
        view->ShowWorldUnder();
        const MCCamera::MCScreenPlace was = camera->PlaceOnScreen();
        RenderView(*view);
        camera->PutBackOnScreen(was);
    }

    return drawn;
}

auto MCCameraList::WipeFirstViews(std::span<const MCFirstView> views) -> void
{
    for (const MCFirstView& drawn : views)
    {
        VfxPaneWipe(drawn.View->WorldFrame(), 0);
    }
}

auto MCCameraList::UnderlayFirstViewsAsBefore(std::span<const MCFirstView> views) -> void
{
    for (const MCFirstView& drawn : views)
    {
        if (!drawn.WasUnderlaid)
        {
            MCRenderer::RemoveUnderlay(MCAddressKey::Of(*drawn.View));
        }
    }
}

auto MCCameraList::Update() -> void
{
    for (const std::shared_ptr<MCCamera>& camera : _Cameras)
    {
        if (camera->Active)
        {
            camera->Update();
        }
    }
}

auto MCCameraList::FindCameraFromIDNumber(int32_t cameraId) -> std::shared_ptr<MCCamera>
{
    for (const std::shared_ptr<MCCamera>& camera : _Cameras)
    {
        if (camera->CameraId == cameraId)
        {
            return camera;
        }
    }

    return nullptr;
}

std::shared_ptr<MCCamera> ActiveMainCamera()
{
    std::shared_ptr<MCCamera> camera = CameraList()->FindCameraFromIDNumber(1);
    return camera != nullptr && camera->Active ? camera : nullptr;
}

int32_t HazeLevelFor(int32_t numVisible)
{
    const std::shared_ptr<MCCamera> eye = Eye();
    const int32_t hazeLevel = eye->HazeLevel;

    if (hazeLevel < 0 && 0 < eye->HazeInc * numVisible + hazeLevel)
    {
        return 0;
    }

    return hazeLevel + eye->HazeInc * numVisible;
}

std::optional<MCColorTable> HazePaletteFor(int32_t numVisible)
{
    return GamePalette().GetHazePalette(HazeLevelFor(numVisible));
}
