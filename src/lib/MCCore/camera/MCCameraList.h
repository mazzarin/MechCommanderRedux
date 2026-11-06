#pragma once

#include "camera/MCCamera.h"
#include "gui/MCGuiRef.h"

class MCGuiObject;
class MCMainWindow;

/// <summary>
/// The game's cameras, read from the camera FIT file ("CameraInfo", then "Camera%d" / "ObjectCamera%d" blocks), and
/// the main window that holds their panes. The scenario installs one in the game context (<see cref="CameraList"/>),
/// then loads it.
/// </summary>
class MCCameraList
{
public:
    /// <summary>The most cameras a camera file may list (a camera window per view of the original's interface).</summary>
    static constexpr uint32_t MaxCameras = 4;

    MCCameraList();

    /// <summary>Deletes the cameras, clears the pause and asked shapes and destroys the main holder.</summary>
    ~MCCameraList();

    MCCameraList(const MCCameraList&) = delete;
    MCCameraList& operator=(const MCCameraList&) = delete;

    /// <summary>
    /// Reads <paramref name="fileName"/> (in the camera path): "NumCameras" (at most <see cref="MaxCameras"/>), and
    /// a camera per "Camera%d" or "ObjectCamera%d" block.
    /// </summary>
    std::expected<void, std::string> Load(std::string_view fileName);

    /// <summary>Appends <paramref name="camera"/>.</summary>
    void Add(std::shared_ptr<MCCamera> camera);

    /// <summary>Makes the main holder (and opens it over the whole screen) the first time a camera asks.</summary>
    std::expected<void, std::string> EnsureMainHolder();

    /// <summary>The main holder (null until a main-window camera makes it).</summary>
    std::shared_ptr<MCMainWindow> MainHolder() const;

    /// <summary>Activates every ready camera; the first becomes current (null when none).</summary>
    std::shared_ptr<MCCamera> ActivateAllReady();

    /// <summary>Clears the drawn blocks and renders every active camera whose window is <paramref name="window"/>,
    /// making each the <see cref="Eye"/> while it renders.</summary>
    void RenderView(MCGuiObject& window);

    /// <summary>Updates the active cameras.</summary>
    void Update();

    /// <summary>The camera with id <paramref name="cameraId"/>, or null.</summary>
    std::shared_ptr<MCCamera> FindCameraFromIDNumber(int32_t cameraId);

    /// <summary>The camera the scene is seen through (see <see cref="::Eye"/>); null when none.</summary>
    std::shared_ptr<MCCamera> Eye() const { return _Eye.lock(); }

    /// <summary>Makes <paramref name="camera"/> the one the scene is seen through.</summary>
    void SetEye(std::weak_ptr<MCCamera> camera) { _Eye = std::move(camera); }

    /// <summary>The current camera.</summary>
    std::weak_ptr<MCCamera> CurrentCamera;

private:
    /// <summary>The cameras, in the file's order.</summary>
    std::vector<std::shared_ptr<MCCamera>> _Cameras;
    /// <summary>The main window holding the main cameras' panes: a child of the screen, closed with the list.</summary>
    MCGuiHandle<MCMainWindow> _MainHolder;
    /// <summary>The camera the scene is seen through.</summary>
    std::weak_ptr<MCCamera> _Eye;
};

/// <summary>The scenario's cameras (null outside a mission); the caller keeps it as its pin.</summary>
std::shared_ptr<MCCameraList> CameraList();
/// <summary>The main camera (id 1) when it exists and is active, else null.</summary>
std::shared_ptr<MCCamera> ActiveMainCamera();
/// <summary>
/// The haze level of a static object of which <paramref name="numVisible"/> corners the home team sees: the main
/// camera's haze level, lifted by its haze step for each seen corner (clamped at 0 from below zero). 0 is no haze.
/// </summary>
int32_t HazeLevelFor(int32_t numVisible);

/// <summary>The palette's haze table for <see cref="HazeLevelFor"/>(<paramref name="numVisible"/>), if any.</summary>
std::optional<MCColorTable> HazePaletteFor(int32_t numVisible);

/// <summary>Where the camera files are ("data\cameras\").</summary>
extern std::string CameraPath;
