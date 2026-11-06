#pragma once

#include "camera/MCViewWindow.h"
#include "gui/MCGuiRef.h"
#include "lib/MCOptionalRef.h"
#include "lib/MCVector2D.h"
#include "lib/MCVector3D.h"
#include "platform/MCRegisteredBlock.h"

class MCBaseObject;
class MCCameraList;
class MCFitIniFile;
class MCGuiEmptyTitleWindow;
class MCTerrainWindow;
struct MCPane;
struct MCWindow;

/// <summary>What a camera looks at.</summary>
enum class MCCameraClass : int32_t
{
    /// <summary>Not set up yet.</summary>
    None = -1,
    /// <summary>A fixed position (the "Camera%d" blocks, or an object camera without a target).</summary>
    Position = 0,
    /// <summary>Follows its target object (the "ObjectCamera%d" blocks).</summary>
    Object = 1,
};

/// <summary>
/// A camera: projects the terrain and objects into its <see cref="MCViewWindow"/>, either from a fixed position or
/// following (swooping to, scrolling after) a target object.
/// </summary>
/// <remarks>The camera list owns its cameras by <c>shared_ptr</c>; a view refers back to its camera by
/// <c>weak_ptr</c>.</remarks>
class MCCamera : public std::enable_shared_from_this<MCCamera>
{
public:
    /// <summary>The defaults: scale 100, a 400x400 view, 30 degrees, haze 4/-2, no class.</summary>
    MCCamera();

    /// <summary>Destroys the view (and its own titled window).</summary>
    ~MCCamera();

    MCCamera(const MCCamera&) = delete;
    MCCamera& operator=(const MCCamera&) = delete;

    /// <summary>
    /// Reads the camera's FIT block: projection, position, colour, haze, scale, the window (made as a pane of
    /// <paramref name="list"/>'s main holder, or its own titled window), and for an object camera its target and
    /// swoop/scroll settings.
    /// </summary>
    std::expected<void, std::string> Load(MCFitIniFile& cameraFile, bool objectCamera, int32_t cameraId,
                                          MCCameraList& list);

    /// <summary>Moves the camera after its target (swoop, scroll or jump), then projects the terrain.</summary>
    void Update();

    /// <summary>
    /// Draws the scene through the camera: the terrain, the craters and the objects into the view's world surface,
    /// then the pause and asked overlays.
    /// </summary>
    void Render();

    /// <summary>Marks a ready camera active, shows its window, makes its terrain window and retargets it.</summary>
    /// <returns>0, or -1 when no terrain window is free.</returns>
    int32_t Activate();

    /// <summary>Not active; no target.</summary>
    void Deactivate();

    /// <summary>Targets the object with that part number (when nonzero and a scenario runs) or id.</summary>
    void ChangeTarget(int32_t partNumber, int32_t objectId, bool jumpTo);

    /// <summary>Targets <paramref name="target"/> (null: none, a position camera); jumps there, or updates first.</summary>
    void ChangeTarget(std::shared_ptr<MCBaseObject> target, bool jumpTo);

    MCVector3D GetPosition() const;

    /// <summary>
    /// The world point under a screen point: the corner of the grid quad around it plus the offset turned back
    /// through the view angle, at the corner's height.
    /// </summary>
    void InverseProject(const MCVector2D& screenPos, MCVector3D& point) const;

    /// <summary>
    /// The screen point of the terrain at <paramref name="point"/>, as the terrain's projection places it in this
    /// camera's view.
    /// </summary>
    MCVector2D Project(const MCVector3D& point) const;

    /// <summary>The screen position of vertex <paramref name="vertexNum"/> of block <paramref name="blockNum"/>
    /// (both clamped); 0 and 10000,10000 when it wasn't drawn this frame.</summary>
    static int VertexProject(int32_t blockNum, int32_t vertexNum, MCVector2D& screenPos);

    /// <summary>Drops the target and moves the camera by a screen-aligned offset.</summary>
    /// <remarks>Port: fractional, so a zoomed-in view scrolls by less than a whole world pixel a frame.</remarks>
    void ScrollCamera(float dx, float dy);

    /// <summary>Moves the camera to <paramref name="newPosition"/>, kept inside the map's diamond.</summary>
    void SetPosition(MCVector3D newPosition);

    /// <summary>Sets the view's size and the projection's sine and cosine.</summary>
    void SetViewSize(float width, float height);

    /// <summary>The pane the camera renders into (null for a camera with no window).</summary>
    std::shared_ptr<MCViewWindow> View() const { return _View.Lock(); }

    /// <summary>"ProjectionAngle" in degrees (30 by default).</summary>
    float ProjectionAngle = 30.0f;
    /// <summary>sin of the projection angle.</summary>
    float SinAngle = 0.0f;
    /// <summary>cos of the projection angle.</summary>
    float CosAngle = 0.0f;
    /// <summary>The view's width in pixels (400 by default).</summary>
    float ViewWidth = 400.0f;
    /// <summary>The view's height in pixels (400 by default).</summary>
    float ViewHeight = 400.0f;
    /// <summary>Half the view's width.</summary>
    float HalfWidth = 0.0f;
    /// <summary>Half the view's height.</summary>
    float HalfHeight = 0.0f;
    /// <summary>
    /// The camera position projected (<see cref="MCTerrain::ProjectTerrain"/>): subtracted from a projected point to
    /// place it on screen.
    /// </summary>
    MCVector2D ScreenUL;
    /// <summary><see cref="ScreenUL"/> before the last update (the terrain's render takes the scroll from it).</summary>
    MCVector2D LastScreenUL;
    /// <summary>What the camera looks at.</summary>
    MCCameraClass CameraClass = MCCameraClass::None;
    /// <summary>"HazeLevel" (4 by default).</summary>
    int32_t HazeLevel = 4;
    /// <summary>"HazeInc" (-2 by default).</summary>
    int32_t HazeInc = -2;
    /// <summary>The object followed (read through <see cref="LockObject"/>: none once it left the world).</summary>
    std::weak_ptr<MCBaseObject> TargetObject;
    /// <summary>"partNumber" of the target.</summary>
    int32_t PartNumber = 0;
    /// <summary>"ObjectClassId" of the target.</summary>
    int32_t ObjectClassId = 0;
    /// <summary>The target's facing when last followed.</summary>
    MCVector3D LastTargetFacing;
    /// <summary>Where a scroll toward the target started.</summary>
    MCVector3D ScrollStart;
    /// <summary>Set once the scroll has jumped the rest of the way.</summary>
    bool ScrollJumped = false;
    /// <summary>"JumpThreshold": beyond this distance the camera jumps (250).</summary>
    float JumpThreshold = 250.0f;
    /// <summary>"CamSpeed" (50).</summary>
    float CamSpeed = 50.0f;
    /// <summary>"CamDistance": how far behind the target a swoop settles (50).</summary>
    float CamDistance = 50.0f;
    /// <summary>Set by ChangeTarget: the next update moves straight to the target.</summary>
    bool TargetChanged = true;
    /// <summary>Forces a terrain window update on the next update (zoom).</summary>
    bool ForceUpdate = false;
    /// <summary>"DistanceFactor" (25).</summary>
    float DistanceFactor = 25.0f;
    /// <summary>The terrain drawn through this camera (one of the terrain's windows; none until activated).</summary>
    std::weak_ptr<MCTerrainWindow> TerrainWindow;
    /// <summary>"PositionX/Y/Z": where the camera looks.</summary>
    MCVector3D Position;
    /// <summary>1-based index in the camera file.</summary>
    int32_t CameraId = 0;
    /// <summary>"Ready": activate may make it active.</summary>
    bool Ready = false;
    /// <summary>Whether it is active (rendering).</summary>
    bool Active = false;
    /// <summary>Swoops to the target: set when SwoopyCamOff is clear and the window is at least 250x250.</summary>
    bool Swoopy = false;
    /// <summary>Set once a swoop has settled.</summary>
    bool SwoopDone = false;
    /// <summary>"ScrollyCam": scroll to a new target rather than jump.</summary>
    bool ScrollyCam = false;
    /// <summary>Set while scrolling to the target.</summary>
    bool Scrolling = false;
    /// <summary>"SpeedFactor" (10 by default, 50 when missing).</summary>
    float SpeedFactor = 10.0f;
    /// <summary>"DistanceThreshold" (10).</summary>
    float DistanceThreshold = 10.0f;

private:
    /// <summary>The pane it renders into: a child of its title window or of the main window, closed with the camera.</summary>
    MCGuiHandle<MCViewWindow> _View;
    /// <summary>
    /// The titled window holding the view, for a camera not in the main window: a child of the screen, closed with the
    /// camera.
    /// </summary>
    MCGuiHandle<MCGuiEmptyTitleWindow> _TitleWindow;
    /// <summary>Whether the view is a pane of a window (the original's view had a parent then).</summary>
    bool _ViewOnWindow = false;
    /// <summary>Whether this camera's activation has shown its window (the original added it to the screen then).</summary>
    bool _WindowShown = false;
};

/// <summary>
/// The camera the scene is seen through: the camera list's current one, or the camera being rendered while
/// <see cref="MCCameraList::RenderView"/> runs. Null outside a mission and while a scenario loads; the caller keeps it
/// as its pin.
/// </summary>
std::shared_ptr<MCCamera> Eye();

/// <summary>
/// <see cref="Eye"/> as a reference for one call, absent while there is none (during a scenario's load). The camera
/// list owns the camera for the whole scenario.
/// </summary>
MCOptionalRef<MCCamera> EyeRef();

/// <summary>"SwoopyCamOff": when set, cameras don't swoop to their target.</summary>
extern bool LeaveSwoopyOff;
/// <summary>The pause shape (loaded on the first pause; cleared with the camera list).</summary>
MCRegisteredBlock& PauseShape();
/// <summary>The "asked" shape, drawn while the game waits on a question (cleared with the camera list).</summary>
/// <remarks>The name is the port's (the binary kept no symbol for it).</remarks>
MCRegisteredBlock& AskedShape();
/// <summary>
/// The pane everything is drawn into: the rendering camera's world surface, else the screen's frame. Shared with its
/// owner (the view, the screen port), whose resizes it follows.
/// </summary>
extern std::shared_ptr<MCPane> GlobalPane;
