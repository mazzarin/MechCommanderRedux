#pragma once

#include <span>
#include <vector>

#include "camera/MCCamera.h"
#include "gui/MCGuiRef.h"

class MCGuiObject;
class MCMainWindow;
class MCViewWindow;

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

    /// <summary>A view <see cref="DrawFirstViews"/> drew, and whether its world was under the screen before.</summary>
    struct MCFirstView
    {
        std::shared_ptr<MCViewWindow> View;
        bool WasUnderlaid = false;
    };

    /// <summary>
    /// Port-only (SPEC-memory.md's "Under the loading bar": the first view, MCMission::DrawFirstView): each active
    /// camera's shown view's ground drawn as play's first frame with the world will draw it, before play's first frame:
    /// the view's zoom started and its world set under the screen, the camera placed on screen where it is
    /// (MCCamera::PlaceOnScreen), its world rendered (<see cref="RenderView"/>) with the ground alone
    /// (MCCamera::DrawsGroundOnly). Play is left as it was: the camera's place on screen (MCCamera::PutBackOnScreen), the
    /// dice and the blocks the draw marks are put back (MCTerrain::RestoreMarkedBlocks). The views drawn.
    /// </summary>
    std::vector<MCFirstView> DrawFirstViews();

    /// <summary>
    /// Port-only: the views <see cref="DrawFirstViews"/> drew wiped to colour 0, as a world surface no frame has drawn
    /// holds, so the world play's first frame with it draws (MCScenario::Render's turn 2) starts from what it did.
    /// </summary>
    static void WipeFirstViews(std::span<const MCFirstView> views);

    /// <summary>
    /// Port-only: the worlds of the views <see cref="DrawFirstViews"/> drew taken from under the screen where they
    /// weren't before, once their wipe has run, so play's first frame shows the screen as it did (the view's
    /// MCViewWindow::Display sets them again).
    /// </summary>
    static void UnderlayFirstViewsAsBefore(std::span<const MCFirstView> views);

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
    /// <summary>Each active camera whose view is shown, with its view, each view once.</summary>
    std::vector<std::pair<std::shared_ptr<MCCamera>, std::shared_ptr<MCViewWindow>>> ShownViews();

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
