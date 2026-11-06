#include "stdafx.h"
#include "camera/MCCameraList.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "camera/MCMainWindow.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGameContext.h"
#include "terrain/MCTerrain.h"

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
