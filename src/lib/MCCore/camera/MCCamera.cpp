#include "stdafx.h"
#include "camera/MCCamera.h"
#include "camera/MCCameraList.h"
#include "camera/MCMainWindow.h"
#include "color/MCPalette.h"
#include "engine/MCCraterManager.h"
#include "engine/MCElementBuffer.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiTitleWindow.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "object/MCBattleMech.h"
#include "platform/MCNeverDestroyed.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectType.h"
#include "object/MCForces.h"
#include "platform/MCRenderer.h"
#include "sprite/MCParticles.h"
#include "sprite/MCWeather.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCSpriteManager.h"
#include "terrain/MCMapBlockManager.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"
#include "object/MCObjectTypeManager.h"

bool LeaveSwoopyOff = false;
auto PauseShape() -> MCRegisteredBlock&
{
    static MCNeverDestroyed<MCRegisteredBlock> shape;
    return *shape;
}

auto AskedShape() -> MCRegisteredBlock&
{
    static MCNeverDestroyed<MCRegisteredBlock> shape;
    return *shape;
}

std::shared_ptr<MCPane> GlobalPane;

namespace
{
    constexpr double DegreesToRadians = 0x1.1df46a2526c7ap-6;
    constexpr double RadiansToDegrees = 0x1.ca5dc1a6402aap+5;
    /// <summary>60 degrees in radians, as MCX.EXE stores it (0x0077cd28).</summary>
    constexpr double SixtyDegrees = 0x1.0c152382d45b2p+0;

    /// <summary>
    /// Set once the first render has emptied the sprite cache: the original did on every switch between camera scale
    /// 1 and 100, and the first render switched from neither.
    /// </summary>
    bool SpriteCacheDumped = false;

    /// <summary>The error of the first of <paramref name="values"/> that failed, if any.</summary>
    template <typename... T> std::optional<std::string> FirstError(const std::expected<T, std::string>&... values)
    {
        std::optional<std::string> error;
        ((error.has_value() || values.has_value() ? void() : void(error = values.error())), ...);
        return error;
    }

    /// <summary>
    /// Entry <paramref name="name"/> of the camera's block, or what is wrong with it.
    /// </summary>
    template <typename T> std::expected<T, std::string> Need(MCFitIniFile& file, std::string_view name)
    {
        MCFitResult<T> value = file.Read<T>(name);

        if (!value.has_value())
        {
            return std::unexpected(
                std::format("no {} in the camera file ({:#x})", name, static_cast<uint32_t>(value.error())));
        }

        return *value;
    }

    /// <summary>Entry <paramref name="name"/> of the camera's block, or <paramref name="fallback"/>.</summary>
    template <typename T> T Optional(MCFitIniFile& file, std::string_view name, T fallback)
    {
        return file.Read<T>(name).value_or(fallback);
    }

    /// <summary>Loads a shape file of the art path (the pause and asked shapes), registered as draw data.</summary>
    MCRegisteredBlock LoadShape(std::string_view name, std::string_view errorMessage)
    {
        MCFile shapeFile;
        const int32_t result = shapeFile.Open(GamePath(ArtPath, name, ".shp"));
        Assert(result == 0, static_cast<uint32_t>(result), errorMessage);
        MCRegisteredBlock shape(shapeFile.FileSize(), MCDataKind::Shapes);
        shapeFile.Read(shape.Bytes());
        shapeFile.Close();
        return shape;
    }

    /// <summary>Darkens the whole screen for the pause and asked overlays.</summary>
    void DarkenScreen()
    {
        const std::optional<MCColorTable> hazePalette = GamePalette().GetHazePalette(-7);
        std::array<MCScreenVertex, 4> vertices{};
        vertices[1].X = GuiSystem().Width() - 1;
        vertices[2].X = GuiSystem().Width() - 1;
        vertices[2].Y = GuiSystem().Height() - 1;
        vertices[3].Y = GuiSystem().Height() - 1;
        VfxTranslatePolygon(ScreenPort()->Frame(), vertices, *hazePalette);
    }

    /// <summary>The length of <paramref name="v"/> in single precision, adding x, z, then y as the original did.</summary>
    float LengthXZY(const MCVector3D& v)
    {
        return std::sqrt(v.X * v.X + v.Z * v.Z + v.Y * v.Y);
    }

    /// <summary><paramref name="v"/> divided by its length (unchanged when that is 0).</summary>
    MCVector3D Normalized(MCVector3D v)
    {
        const auto length = static_cast<float>(v.Magnitude());

        if (length != 0.0f)
        {
            v.X /= length;
            v.Y /= length;
            v.Z /= length;
        }

        return v;
    }
}

MCCamera::MCCamera() = default;

MCCamera::~MCCamera()
{
    _View.reset();
    _TitleWindow.reset();
}

auto MCCamera::GetPosition() const -> MCVector3D
{
    return Position;
}

auto MCCamera::SetViewSize(float width, float height) -> void
{
    ViewWidth = width;
    ViewHeight = height;
    const double angle = static_cast<double>(ProjectionAngle) * DegreesToRadians;
    SinAngle = static_cast<float>(std::sin(angle));
    CosAngle = static_cast<float>(std::cos(angle));
    HalfWidth = ViewWidth * 0.5f;
    HalfHeight = ViewHeight * 0.5f;
}

auto MCCamera::Load(MCFitIniFile& cameraFile, bool objectCamera, int32_t cameraId, MCCameraList& list)
    -> std::expected<void, std::string>
{
    // PixelScalar, BackgroundColor and the window's MainWindow are read (and the first two required), then unused.
    const auto pixelScalar = Need<float>(cameraFile, "PixelScalar");

    if (!pixelScalar.has_value())
    {
        return std::unexpected(pixelScalar.error());
    }

    const auto projectionAngle = Need<float>(cameraFile, "ProjectionAngle");
    const auto positionX = Need<float>(cameraFile, "PositionX");
    const auto positionY = Need<float>(cameraFile, "PositionY");
    const auto positionZ = Need<float>(cameraFile, "PositionZ");

    if (const std::optional<std::string> error = FirstError(projectionAngle, positionX, positionY, positionZ);
        error.has_value())
    {
        return std::unexpected(*error);
    }

    ProjectionAngle = *projectionAngle;
    Position = MCVector3D(*positionX, *positionY, *positionZ);

    const auto backgroundColor = Need<uint8_t>(cameraFile, "BackgroundColor");
    const auto ready = Need<uint8_t>(cameraFile, "Ready");

    if (const std::optional<std::string> error = FirstError(backgroundColor, ready); error.has_value())
    {
        return std::unexpected(*error);
    }

    Ready = *ready != 0;
    const auto hazeLevel = Need<int32_t>(cameraFile, "HazeLevel");
    const auto hazeInc = Need<int32_t>(cameraFile, "HazeInc");
    const auto cameraScale = Need<int32_t>(cameraFile, "CameraScale");

    if (const std::optional<std::string> error = FirstError(hazeLevel, hazeInc, cameraScale); error.has_value())
    {
        return std::unexpected(*error);
    }

    HazeLevel = *hazeLevel;
    HazeInc = *hazeInc;
    // Port: always the full-size (1x) art and layout; the zoom is the world surface's size (MCViewWindow), which
    // starts zoomed out where the camera started at scale 1.
    const bool startsZoomedOut = *cameraScale == 1;

    if (const auto windowLeft = cameraFile.Read<uint32_t>("WindowLeft"); windowLeft.has_value())
    {
        const auto windowTop = Need<uint32_t>(cameraFile, "WindowTop");
        const auto windowRight = Need<uint32_t>(cameraFile, "WindowRight");
        const auto windowBottom = Need<uint32_t>(cameraFile, "WindowBottom");

        if (const std::optional<std::string> error = FirstError(windowTop, windowRight, windowBottom);
            error.has_value())
        {
            return std::unexpected(*error);
        }

        const bool mainWindow = Optional<int32_t>(cameraFile, "MainWindow", 0) != 0;
        _View = MCMakeGui<MCViewWindow>();
        const auto left = static_cast<int32_t>(*windowLeft);
        const auto top = static_cast<int32_t>(*windowTop);
        const auto paneWidth = static_cast<int32_t>(*windowRight - *windowLeft);
        const auto paneHeight = static_cast<int32_t>(*windowBottom - *windowTop);

        if (!mainWindow)
        {
            // Its own titled window holding the view.
            _TitleWindow = MCMakeGui<MCGuiEmptyTitleWindow>();

            if (const int32_t result = _TitleWindow->Init(left, top, paneWidth, paneHeight, {}); result != 0)
            {
                return std::unexpected(
                    std::format("could not open camera {}'s window ({:#x})", cameraId, static_cast<uint32_t>(result)));
            }

            if (_TitleWindow->TitleBar != nullptr)
            {
                _TitleWindow->TitleBar->ShowZoomButton(true);
            }

            if (_TitleWindow->ResizeButton != nullptr)
            {
                _TitleWindow->ResizeButton->ShowGuiWindow(true);
            }

            if (_TitleWindow->TitleBar != nullptr)
            {
                _TitleWindow->TitleBar->ShowCloseButton(true);
            }
        }
        else if (std::expected<void, std::string> holder = list.EnsureMainHolder(); !holder.has_value())
        {
            return std::unexpected(holder.error());
        }

        if (const int32_t result = _View->Init(left, top, paneWidth, paneHeight, {}); result != 0)
        {
            return std::unexpected(
                std::format("could not open camera {}'s view ({:#x})", cameraId, static_cast<uint32_t>(result)));
        }

        _View->ObjectType = 4;

        if (!mainWindow)
        {
            _TitleWindow->AddPane(_View);
            _View->MoveTo(0, 0, false);
        }
        else
        {
            if (Ready)
            {
                list.MainHolder()->AddPane(_View);
            }

            _View->InterfaceWindow = true;
        }

        CameraId = cameraId;
        _View->SetWindowCamera(shared_from_this());
        // Port: the view is the world surface.
        _View->ZoomStartsOut = startsZoomedOut;
        _View->UpdateWorldSurface();
        SetViewSize(static_cast<float>(_View->WorldWidth()), static_cast<float>(_View->WorldHeight()));
    }
    else
    {
        // No window: the camera covers the global pane.
        SetViewSize(static_cast<float>(GlobalPane->X1 - GlobalPane->X0),
                    static_cast<float>(GlobalPane->Y1 - GlobalPane->Y0));
    }

    SwoopDone = false;

    if (!objectCamera)
    {
        CameraClass = MCCameraClass::Position;
        return {};
    }

    const auto objectClassId = Need<int32_t>(cameraFile, "ObjectClassId");
    const auto partNumber = Need<int32_t>(cameraFile, "partNumber");

    if (const std::optional<std::string> error = FirstError(objectClassId, partNumber); error.has_value())
    {
        return std::unexpected(*error);
    }

    ObjectClassId = *objectClassId;
    PartNumber = *partNumber;
    LeaveSwoopyOff = Optional<int32_t>(cameraFile, "SwoopyCamOff", 0) != 0;
    ScrollyCam = Optional<int32_t>(cameraFile, "ScrollyCam", 0) != 0;
    DistanceThreshold = Optional<float>(cameraFile, "DistanceThreshold", 10.0f);

    // Original behaviour (OB-034): a missing MinScrollSpeed sets DistanceThreshold to 90 instead (and MinScrollSpeed
    // itself is never read).
    if (!cameraFile.Read<float>("MinScrollSpeed").has_value())
    {
        DistanceThreshold = 90.0f;
    }

    SpeedFactor = Optional<float>(cameraFile, "SpeedFactor", 50.0f);
    CamDistance = Optional<float>(cameraFile, "CamDistance", 50.0f);
    DistanceFactor = Optional<float>(cameraFile, "DistanceFactor", 25.0f);
    CamSpeed = Optional<float>(cameraFile, "CamSpeed", 50.0f);
    JumpThreshold = Optional<float>(cameraFile, "JumpThreshold", 250.0f);
    CameraClass = MCCameraClass::Object;
    return {};
}

auto MCCamera::Project(const MCVector3D& point) const -> MCVector2D
{
    const MCVector2D screen100 = MCTerrain::ProjectTerrain(point);
    return MCVector2D((screen100.X - ScreenUL.X) + HalfWidth, (screen100.Y - ScreenUL.Y) + HalfHeight);
}

auto MCCamera::InverseProject(const MCVector2D& screenPos, MCVector3D& point) const -> void
{
    const std::shared_ptr<MCTerrainWindow> terrainWindow = TerrainWindow.lock();
    const MCTerrainWindow& terrain = *terrainWindow;
    std::optional<uint32_t> closestVertex;
    float distance = 1e7f;
    int32_t vertexIndex = 0;
    const int32_t screenX = static_cast<int16_t>(static_cast<int32_t>(std::floor(screenPos.X)));
    const int32_t screenY = static_cast<int16_t>(static_cast<int32_t>(std::floor(screenPos.Y)));

    // The vertex nearest the point on screen.
    int32_t closest = 0x40000000;

    for (size_t i = 0; i < terrain.Vertices.size(); i++)
    {
        const MCVertex& vertex = terrain.Vertices[i];
        const int32_t dx = screenX - vertex.Px;
        const int32_t dy = screenY - vertex.Py;
        const int32_t distanceSquared = dy * dy + dx * dx;

        if (distanceSquared < closest)
        {
            closest = distanceSquared;
            closestVertex = static_cast<uint32_t>(i);
            vertexIndex = static_cast<int32_t>(i);
        }
    }

    // The block around it that holds the point: the corners' angles from the point turn by at most 180 degrees
    // between neighbours. Measure from its first corner.
    float angle = 0.0f;
    MCOptionalRef<const MCTerrainBlock> foundBlock;

    for (const MCTerrainBlock& block : terrain.Blocks)
    {
        if (!closestVertex.has_value() || !std::ranges::contains(block.Vertices, *closestVertex))
        {
            continue;
        }

        std::array<float, 4> cornerAngle{};

        for (size_t corner = 0; corner < 4; corner++)
        {
            MCVector3D toCorner;
            toCorner.X = screenPos.X - static_cast<float>(terrain.Vertices[block.Vertices[corner]].Px);
            toCorner.Y = screenPos.Y - static_cast<float>(terrain.Vertices[block.Vertices[corner]].Py);
            toCorner.Z = 0.0f;
            const float length = std::sqrt(toCorner.X * toCorner.X + toCorner.Y * toCorner.Y + toCorner.Z * toCorner.Z);

            if (length != 0.0f)
            {
                toCorner.X /= length;
                toCorner.Y /= length;
                toCorner.Z /= length;
            }

            const double degrees = AcosMatherr(static_cast<double>(toCorner.X)) * RadiansToDegrees;
            cornerAngle[corner] = static_cast<float>(degrees);

            if (toCorner.Y < 0.0f)
            {
                cornerAngle[corner] = static_cast<float>(360.0 - degrees);
            }
        }

        bool inside = true;

        for (size_t corner = 0; corner < 4; corner++)
        {
            float turn = cornerAngle[(corner + 1) & 3] - cornerAngle[corner];

            if (turn < 0.0f)
            {
                turn = static_cast<float>(turn + 360.0);
            }

            inside = inside && !(turn > 180.0f);
        }

        if (!inside)
        {
            continue;
        }

        foundBlock = block;
        vertexIndex = static_cast<int32_t>(block.Vertices[0]);
        const MCVertex& corner = terrain.Vertices[block.Vertices[0]];
        const float dx = screenPos.X - static_cast<float>(corner.Px);
        const float dy = screenPos.Y - static_cast<float>(corner.Py);

        if (dy != 0.0f)
        {
            angle = static_cast<float>(std::atan(static_cast<double>(dx / dy)) * RadiansToDegrees);
        }
        else
        {
            angle = 90.0f;
        }

        distance = std::sqrt(dy * dy + dx * dx);
        break;
    }

    // Back from the corner's screen offset to the world, on the corner's row and column of the window's grid.
    // Original behaviour (OB-103): with no block found, the distance is still 1e7, far off the map.
    const double turn = (60.0 - static_cast<double>(angle)) * DegreesToRadians;
    const int32_t row = vertexIndex / terrain.Side();
    const int32_t col = vertexIndex % terrain.Side();
    const auto side = static_cast<float>(std::sin(turn) * distance / std::sin(SixtyDegrees));
    point.X = static_cast<float>(std::cos(turn) * distance + std::cos(SixtyDegrees) * side +
                                 static_cast<double>(col) * MCTerrain::MetersPerVertex + terrain.TopLeftX);
    point.Y = static_cast<float>(static_cast<double>(terrain.TopLeftY) -
                                 static_cast<double>(row) * MCTerrain::MetersPerVertex - side);

    if (!foundBlock)
    {
        point.Z = 0.0f;
        return;
    }

    const MCVertex& corner = terrain.Vertices[foundBlock->get().Vertices[0]];
    point.Z =
        static_cast<float>(terrain.Cache.lock()->Vertex(corner.MapVertex).Elevation) * MCTerrain::MetersPerElevLevel;
}

auto MCCamera::Update() -> void
{
    // Port: the zoom eases here, before the objects update: they place themselves on screen from the view's size
    // (screenPos, onScreen), so it must be this frame's size by then, as the terrain drawn later uses.
    if (_View != nullptr)
    {
        _View->EaseZoom();
        const MCPane& surface = _View->WorldFrame();
        const auto surfaceWidth = static_cast<float>(surface.X1 - surface.X0);
        const auto surfaceHeight = static_cast<float>(surface.Y1 - surface.Y0);

        if (surfaceWidth != ViewWidth || surfaceHeight != ViewHeight)
        {
            SetViewSize(surfaceWidth, surfaceHeight);
        }
    }

    MCVector3D newPosition = Position;

    // Follows the target, when there is one; returns whether the camera jumped onto it (the terrain window is then
    // rebuilt at once, when the target changed or the zoom forced it).
    const auto follow = [this, &newPosition](MCGameObject& target) -> bool
    {
        const MCVector3D targetPosition = target.GetPosition();
        MCFrameOfRef targetFrame = target.GetFrame();
        MCVector3D velocity = target.GetVelocity();

        // The target's facing: its frame turned by the torso (a mech) plus 45 degrees.
        float torso = 0.0f;

        if (target.ObjectClass == MCObjectClass::BattleMech)
        {
            torso = static_cast<MCBattleMech&>(target).TorsoRotation;
        }

        const double radians = (static_cast<double>(torso) + 45.0) * DegreesToRadians;
        const float s = static_cast<float>(std::sin(radians));
        const float c = static_cast<float>(std::cos(radians));
        const MCVector3D oldI = targetFrame.I;
        targetFrame.I = targetFrame.I * c + targetFrame.J * s;
        targetFrame.J = targetFrame.J * c - oldI * s;

        if (TargetChanged)
        {
            if (!ScrollyCam)
            {
                newPosition = targetPosition;
                return true;
            }

            // Start scrolling toward the new target.
            ScrollStart = newPosition;
            ScrollJumped = false;
            const MCVector3D step = Normalized(targetPosition - newPosition);
            Scrolling = true;
            newPosition.X += step.X * CamSpeed * FrameLength;
            newPosition.Y += step.Y * CamSpeed * FrameLength;
            newPosition.Z += step.Z * CamSpeed * FrameLength;
            TargetChanged = false;
            return false;
        }

        if (ForceUpdate || (!Swoopy && (!ScrollyCam || !Scrolling)))
        {
            newPosition = targetPosition;
            return true;
        }

        if (ScrollyCam && Scrolling)
        {
            MCVector3D step;
            float speed = 0.0f;

            if (!ScrollJumped)
            {
                const MCVector3D toStart = ScrollStart - newPosition;
                const float distanceToStart = LengthXZY(toStart);
                const MCVector3D toTarget = targetPosition - newPosition;
                step = toTarget;

                if (distanceToStart > JumpThreshold)
                {
                    // Too far: jump the rest.
                    newPosition = LengthXZY(toTarget) <= distanceToStart ? targetPosition : toStart + targetPosition;
                    ScrollJumped = true;
                    return false;
                }

                speed = distanceToStart / JumpThreshold * SpeedFactor + CamSpeed;
            }
            else
            {
                if (0.0f < DistanceThreshold)
                {
                    Scrolling = false;
                    newPosition = targetPosition;
                    return false;
                }

                // Port fix: MCX.EXE steps along a direction left unset on this path (reachable only with a
                // DistanceThreshold of 0 or less); the port steps along none.
                speed = 0.0f / JumpThreshold * SpeedFactor + CamSpeed;
            }

            step = Normalized(step);
            step.X = step.X * speed * FrameLength;
            step.Y = step.Y * speed * FrameLength;
            step.Z = step.Z * speed * FrameLength;

            if (LengthXZY(targetPosition - newPosition) <
                std::sqrt(step.X * step.X + step.Y * step.Y + step.Z * step.Z))
            {
                newPosition = targetPosition;
                return false;
            }

            newPosition.X += step.X;
            newPosition.Y += step.Y;
            newPosition.Z += step.Z;
            return false;
        }

        // Swoop: close in on a point CamDistance behind the target, slowing as it nears.
        if (std::sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y + velocity.Z * velocity.Z) == 0.0f && SwoopDone)
        {
            return false;
        }

        if (LastTargetFacing.X != targetFrame.J.X || LastTargetFacing.Y != targetFrame.J.Y ||
            LastTargetFacing.Z != targetFrame.J.Z)
        {
            LastTargetFacing = targetFrame.J;
        }

        const float behind = -CamDistance;
        const MCVector3D goal = targetPosition + MCVector3D(behind * LastTargetFacing.X, behind * LastTargetFacing.Y,
                                                            behind * LastTargetFacing.Z);
        velocity = goal - newPosition;
        const auto rate = static_cast<float>(velocity.Magnitude() / DistanceFactor);
        velocity.Normalize();
        velocity *= CamSpeed;
        velocity *= rate;
        velocity *= FrameLength;
        newPosition += velocity;

        if (velocity.Magnitude() < 0.5)
        {
            SwoopDone = true;
        }

        return false;
    };

    const std::shared_ptr<MCTerrainWindow> terrainWindow = TerrainWindow.lock();

    if (const std::shared_ptr<MCBaseObject> targetObject = LockObject(TargetObject);
        CameraClass == MCCameraClass::Object && targetObject != nullptr)
    {
        auto& target = static_cast<MCGameObject&>(*targetObject);

        if (target.IsRevealed() == 0)
        {
            ChangeTarget(nullptr, false);
            return;
        }

        if (follow(target))
        {
            if (terrainWindow != nullptr && (TargetChanged || ForceUpdate))
            {
                terrainWindow->Update();
            }

            TargetChanged = false;
        }
    }
    else if (CameraClass != MCCameraClass::Position && CameraClass != MCCameraClass::Object)
    {
        return;
    }

    LastScreenUL = ScreenUL;
    SetPosition(newPosition);

    if (terrainWindow != nullptr && (TargetChanged || ForceUpdate || GamePaused != 0))
    {
        terrainWindow->Update();
    }

    if (Terrain() != nullptr)
    {
        ScreenUL = MCTerrain::ProjectTerrain(Position);
    }

    TargetChanged = false;
    ForceUpdate = false;
}

auto MCCamera::Render() -> void
{
    const std::shared_ptr<MCPane> savedPane = GlobalPane;

    // Port: the world goes into the view's world surface; the unit overlays onto the screen over the view (they are
    // drawn there at the screen's scale).
    const MCOverlayTarget savedOverlay = MCOverlay;
    MCOverlay = MCOverlayTarget{GlobalPane, 1.0f, 1.0f};

    if (_View != nullptr)
    {
        GlobalPane = _View->SharedWorldFrame();
        MCOverlay = MCOverlayTarget{_View->FramePane, _View->WorldScaleX(), _View->WorldScaleY()};
    }

    const std::shared_ptr<MCTerrainWindow> terrainWindow = TerrainWindow.lock();

    const auto paneWidth = static_cast<float>(GlobalPane->X1 - GlobalPane->X0);
    const auto paneHeight = static_cast<float>(GlobalPane->Y1 - GlobalPane->Y0);

    if (paneWidth != ViewWidth || paneHeight != ViewHeight)
    {
        SetViewSize(paneWidth, paneHeight);
    }

    if (!SpriteCacheDumped)
    {
        SpriteManager()->DumpAll();
        SpriteCacheDumped = true;
    }

    ElementList().Reset();
    // Port-only (HD lighting): the shots, fires and explosions drawn now light the world they're drawn into.
    MCSceneLights::Begin(*GlobalPane);
    terrainWindow->Render(HazeLevel);

    if (DrawTerrainGrid != 0)
    {
        Terrain()->DrawLines();
    }

    CraterManager().Render();
    ObjectList()->Render();
    // Port-only (HD particles): what flies about the battle, moved on to the game's time.
    MCParticles::Draw(*this);
    // Port-only (HD weather): the rain, moved on to the game's time.
    MCWeather::Draw(*this);
    MCSceneLights::AddShadows();
    ElementList().Sort();
    ElementList().Draw();
    MCSceneLights::Finish();

    // Port: the pause and asked shapes are drawn on the screen over the view, at its scale (centred as the original
    // centred them in the view).
    const MCPane& overlayPane = *MCOverlay.Pane;
    const auto overlayHalfWidth = static_cast<float>(overlayPane.X1 - overlayPane.X0) * 0.5f;
    const auto overlayHalfHeight = static_cast<float>(overlayPane.Y1 - overlayPane.Y0) * 0.5f;

    if (GamePaused != 0)
    {
        DarkenScreen();

        if (PauseShape().Empty())
        {
            PauseShape() = LoadShape("pause", " Could not find Pause Shape ");
        }

        AGShapeDraw(overlayPane, PauseShape().Bytes(), 0, static_cast<int32_t>(overlayHalfWidth), 60);
    }

    if (GameAsked != 0)
    {
        DarkenScreen();

        if (AskedShape().Empty())
        {
            AskedShape() = LoadShape("asked", " Could not find Asked Shape ");
        }

        AGShapeDraw(overlayPane, AskedShape().Bytes(), 0, static_cast<int32_t>(overlayHalfWidth),
                    static_cast<int32_t>(overlayHalfHeight));
    }

    if (_View != nullptr)
    {
        GlobalPane = savedPane;
    }

    MCOverlay = savedOverlay;
}

auto MCCamera::Activate() -> int32_t
{
    if (Ready)
    {
        if (Active)
        {
            return 0;
        }

        Active = true;
    }

    if (_View != nullptr && !_View->Parent.expired())
    {
        ScreenWindow()->AddChild(_View->Parent);
    }

    // Port fix: MCX.EXE draws the window unguarded; a camera without one ("WindowLeft" missing) would crash.
    if (_View != nullptr)
    {
        _View->Draw();
    }

    if (Terrain() != nullptr && TerrainWindow.expired())
    {
        const std::shared_ptr<MCTerrainWindow> terrainWindow = Terrain()->NewWindow(weak_from_this());

        if (terrainWindow == nullptr)
        {
            return -1;
        }

        TerrainWindow = terrainWindow;
        terrainWindow->Update();
    }

    if (Active && CameraClass == MCCameraClass::Object)
    {
        ChangeTarget(PartNumber, ObjectClassId, true);
    }

    return 0;
}

auto MCCamera::Deactivate() -> void
{
    Active = false;
    TargetObject.reset();
}

auto MCCamera::ChangeTarget(std::shared_ptr<MCBaseObject> target, bool jumpTo) -> void
{
    TargetObject = target;
    CameraClass = target == nullptr ? MCCameraClass::Position : MCCameraClass::Object;
    TargetChanged = true;
    MCTerrain::ForceRedraw = true;

    if (!jumpTo)
    {
        Update();
    }
    else if (target != nullptr)
    {
        Position = static_cast<MCGameObject&>(*target).GetPosition();
    }

    if (const std::shared_ptr<MCTerrainWindow> terrainWindow = TerrainWindow.lock(); terrainWindow != nullptr)
    {
        terrainWindow->Update();
    }
}

auto MCCamera::ChangeTarget(int32_t newPartNumber, int32_t objectId, bool jumpTo) -> void
{
    if (newPartNumber == 0)
    {
        if (objectId != -1)
        {
            TargetObject = ObjectList()->FindObjectId(objectId);
        }
    }
    else if (Scenario() != nullptr)
    {
        TargetObject = ObjectList()->FindObjectFromPart(newPartNumber);
    }

    const std::shared_ptr<MCBaseObject> target = LockObject(TargetObject);
    CameraClass = target != nullptr ? MCCameraClass::Object : MCCameraClass::Position;
    TargetChanged = true;
    MCTerrain::ForceRedraw = true;

    if (!jumpTo)
    {
        Update();
    }
    else if (target != nullptr)
    {
        Position = static_cast<MCGameObject&>(*target).GetPosition();
    }

    if (const std::shared_ptr<MCTerrainWindow> terrainWindow = TerrainWindow.lock(); terrainWindow != nullptr)
    {
        terrainWindow->Update();
    }
}

auto MCCamera::VertexProject(int32_t blockNum, int32_t vertexNum, MCVector2D& screenPos) -> int
{
    const std::shared_ptr<const MCTerrain> terrain = Terrain();
    blockNum = std::clamp(blockNum, 0, MCTerrain::TotalBlocks - 1);
    vertexNum = std::clamp(vertexNum, 0, VerticesPerBlock - 1);
    const int32_t index = terrain->BlockOffsets[blockNum] + vertexNum;

    if (terrain->ScreenPosX[index] == 0x11111111)
    {
        screenPos.Y = 10000.0f;
        screenPos.X = 10000.0f;
        return 0;
    }

    screenPos.X = static_cast<float>(terrain->ScreenPosX[index]);
    screenPos.Y = static_cast<float>(terrain->ScreenPosY[index]);
    return 1;
}

auto MCCamera::ScrollCamera(float dx, float dy) -> void
{
    const MCVector3D oldPosition = Position;

    if (LockObject(TargetObject) != nullptr)
    {
        ChangeTarget(nullptr, false);
    }

    MCVector3D newPosition;
    newPosition.X = dy + dx + oldPosition.X;
    newPosition.Y = (dx + oldPosition.Y) - dy;
    newPosition.Z = oldPosition.Z;
    SetPosition(newPosition);
}

auto MCCamera::SetPosition(MCVector3D newPosition) -> void
{
    // The map is a diamond on screen: keep the camera inside it, a margin (smaller toward the corners) from the
    // edges.
    const float halfMap =
        static_cast<float>(MCTerrain::VerticesBlockSide * MCTerrain::BlocksMapSide) * MCTerrain::MetersPerVertex * 0.5f;
    Position = newPosition;
    const float x = newPosition.X;
    const float y = newPosition.Y;
    const float z = newPosition.Z;
    const float toRight = std::sqrt(z * z + y * y + (x - halfMap) * (x - halfMap));
    const float toTop = std::sqrt(z * z + (y - halfMap) * (y - halfMap) + x * x);
    const float toLeft = std::sqrt(z * z + y * y + (x + halfMap) * (x + halfMap));
    const float toBottom = std::sqrt(z * z + x * x + (y + halfMap) * (y + halfMap));

    // Near a corner the margin shrinks with the distance to it. (The original's zoomed-out scale used 1850 and
    // 1.3636364 here.)
    float cornerOffset = 375.0f;

    if (toRight < 1675.0f)
    {
        cornerOffset = toRight - 1300.0f;
    }
    else if (toTop < 1675.0f)
    {
        cornerOffset = toTop - 1300.0f;
    }
    else if (toLeft < 1675.0f)
    {
        cornerOffset = toLeft - 1300.0f;
    }
    else if (toBottom < 1675.0f)
    {
        cornerOffset = toBottom - 1300.0f;
    }

    const float margin = cornerOffset > 0.0f ? 1300.0f - cornerOffset * 2.4666667f : 1300.0f;
    const float limit = halfMap - margin;

    // y - x and x + y measure along the diamond's axes.
    const float across = y - x;
    const float along = x + y;
    const bool alongAbove = along > limit;
    const bool alongBelow = along < -limit;

    if (across > limit)
    {
        if (alongBelow)
        {
            Position.Y = 0.0f;
            Position.X = -limit;
        }
        else if (alongAbove)
        {
            Position.Y = limit;
            Position.X = 0.0f;
        }
        else
        {
            Position.X = static_cast<float>((along - limit) * 0.5);
            Position.Y = Position.X + limit;
        }

        return;
    }

    if (across < -limit)
    {
        if (alongBelow)
        {
            Position.Y = -limit;
            Position.X = 0.0f;
        }
        else if (alongAbove)
        {
            Position.X = limit;
            Position.Y = 0.0f;
        }
        else
        {
            Position.X = static_cast<float>((along + limit) * 0.5);
            Position.Y = Position.X - limit;
        }

        return;
    }

    if (alongAbove)
    {
        Position.X = static_cast<float>(((x - y) + limit) * 0.5);
        Position.Y = limit - Position.X;
        return;
    }

    if (alongBelow)
    {
        Position.X = static_cast<float>(((x - y) - limit) * 0.5);
        Position.Y = -Position.X - limit;
        return;
    }

    // Inside: sit on the ground. (MCX.EXE also has an unreachable " Impossible Camera Clip Situation " Fatal.)
    if (Terrain() != nullptr)
    {
        Position.Z = MCTerrain::GetTerrainElevation(Position);
    }
}
