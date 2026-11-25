#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "camera/MCCamera.h"
#include "camera/MCCameraList.h"
#include "camera/MCMainWindow.h"
#include "engine/MCDeltaElement.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCVfxElement.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "object/MCGameObject.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectList.h"
#include "object/MCObjectSystem.h"
#include "object/MCVertexPlacement.h"
#include "sprite/MCSpriteMath.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTerrainWindow.h"

/// <summary>
/// A mission's cameras belong to the camera list. The eye is its current camera (camera 1 in mission 1), the main
/// view shows that camera and refers back to it, the camera draws through one of the terrain's windows, and outside a
/// render everything draws into the screen's frame.
/// </summary>
TEST_CASE_ISOLATED("game: the eye is the mission's current camera, shown by the main view")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCCameraList> list = CameraList();
    REQUIRE(list != nullptr);
    const std::shared_ptr<MCCamera> eye = Eye();
    REQUIRE(eye != nullptr);
    CHECK(eye == list->CurrentCamera.lock());
    CHECK(eye == list->FindCameraFromIDNumber(1));
    CHECK(eye == ActiveMainCamera());
    CHECK(list->FindCameraFromIDNumber(99) == nullptr);

    const std::shared_ptr<MCViewWindow> view = MCMainView();
    REQUIRE(view != nullptr);
    CHECK(view == eye->View());
    CHECK(view->GetCamera() == eye);
    CHECK(MainHolder() == list->MainHolder());
    CHECK(MainHolder() != nullptr);

    CHECK(!eye->TerrainWindow.expired());
    CHECK(GlobalPane == ScreenPort()->SharedFrame());

    // A render makes the view's world surface the global pane for its length only.
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(GlobalPane == ScreenPort()->SharedFrame());
    CHECK(Eye() == eye);
}

/// <summary>
/// Port-only: the loading bar's frames once the cameras are active (the load's fill, MCScenario::ShowLoadingBar) leave
/// out the main window their activation put up, so the mission's view is first shown in play: the interface's start
/// toggles its zoom at once, and the mission opens at that zoom rather than easing there from the view's first one.
/// </summary>
TEST_CASE_ISOLATED("game: the loading bar's last frames leave the main view unshown, so a mission opens at its zoom")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // The zoom as in play, not fixed.
    MCTestGame::BootOptions.FixedZoomHeight = 0.0f;
    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCViewWindow> view = MCMainView();
    REQUIRE(view != nullptr);
    const std::shared_ptr<MCMainWindow> holder = MainHolder();
    REQUIRE(holder != nullptr);
    REQUIRE(holder->IsShowing());

    // The view as the load's fill finds it: its window up (the cameras' activation), the view never shown, its zoom
    // not yet started. The bar's frame leaves the view undrawn (its first draw would start the zoom) and the window up.
    view->ZoomShown = false;
    view->ZoomHeight = 0.0f;
    MCScenario::ShowLoadingBar();
    CHECK(holder->IsShowing());
    CHECK(!view->ZoomShown);
    CHECK_EQ(view->ZoomHeight, 0.0f);

    // The interface's start (MCMission::LoadWindowStatus) toggles the zoom: it takes effect at once.
    CHECK(!view->ZoomedOut());
    const float first = view->ZoomHeight;
    holder->ZoomActivePane();
    CHECK(view->ZoomTarget != first);
    CHECK_EQ(view->ZoomHeight, view->ZoomTarget);

    // A frame of play shows the view: a zoom from then on eases.
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(view->ZoomShown);
}

/// <summary>
/// A camera that follows a mover lets go of it once the mover leaves the world: the reference reads as none from then
/// on (the original kept a pointer to the freed object, OB-170), and the camera keeps its place.
/// </summary>
TEST_CASE_ISOLATED("game: a camera following a mover lets go of it when the mover leaves the world")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCCamera> eye = Eye();
    REQUIRE(eye != nullptr);
    const std::shared_ptr<MCMover> mover = GetMoverFromPartId(0x200);
    REQUIRE(mover != nullptr);

    eye->ChangeTarget(mover, true);
    CHECK(LockObject(eye->TargetObject) == mover);
    CHECK(eye->CameraClass == MCCameraClass::Object);
    CHECK_EQ(eye->Position.X, mover->GetPosition().X);
    CHECK_EQ(eye->Position.Y, mover->GetPosition().Y);

    mover->MarkRemoved();
    CHECK(LockObject(eye->TargetObject) == nullptr);
    const MCVector3D before = eye->Position;
    eye->Update();
    CHECK_EQ(eye->Position.X, before.X);
    CHECK_EQ(eye->Position.Y, before.Y);

    // Scrolling no longer drops a target first: there is none.
    eye->ScrollCamera(10, 0);
    CHECK_EQ(eye->Position.X, before.X + 10.0f);
    CHECK_EQ(eye->Position.Y, before.Y + 10.0f);
}

namespace
{
    /// <summary>The screen box of the frames the watched object (DrawWatch) drew this frame, and how many.</summary>
    struct MCDrawnBox
    {
        float Left = FLT_MAX;
        float Top = FLT_MAX;
        float Right = -FLT_MAX;
        float Bottom = -FLT_MAX;
        int32_t Frames = 0;
    };

    MCDrawnBox WatchedDraws()
    {
        MCDrawnBox box;
        const MCDrawWatch& watch = DrawWatch();

        if (watch.RenderTurn != Turn)
        {
            return box;
        }

        const auto add = [&box](std::span<const uint8_t> shapes, int32_t frame, float x, float y, bool mirrored)
        {
            // A mirrored frame is drawn flipped about its place.
            const MCFrameBounds bounds = MCShapeFrameBounds(shapes, frame);
            const float left = mirrored ? x - bounds.MinX - bounds.Width : x + bounds.MinX;
            box.Left = std::min(box.Left, left);
            box.Top = std::min(box.Top, y + bounds.MinY);
            box.Right = std::max(box.Right, left + bounds.Width);
            box.Bottom = std::max(box.Bottom, y + bounds.MinY + bounds.Height);
            box.Frames++;
        };

        for (size_t i = watch.First; i < watch.End; ++i)
        {
            const MCElement& element = ElementList().Made(i);

            if (const auto* frame = dynamic_cast<const MCVfxElement*>(&element);
                frame != nullptr && !frame->ShapeTable.empty() && !frame->SunShadow)
            {
                add(frame->ShapeTable, frame->FrameNum, frame->X, frame->Y, frame->Reverse != 0);
            }

            // A delta animation (a swaying tree) draws its first shape whole; its frames only change it.
            else if (const auto* delta = dynamic_cast<const MCDeltaElement*>(&element);
                     delta != nullptr && !delta->ShapeTable.empty())
            {
                add(delta->ShapeTable, 0, static_cast<float>(delta->X), static_cast<float>(delta->Y), false);
            }
        }

        return box;
    }

    enum class MCViewEdge
    {
        Bottom,
        Top,
        Left,
        Right,
    };

    /// <summary>What a pan of an object in from an edge found.</summary>
    struct MCPanIn
    {
        /// <summary>The step at which it was first drawn in the view (none: not before the view reached it).</summary>
        std::optional<int32_t> FirstDrawn;
        /// <summary>The first step before that at which its frames, as first drawn, already showed in the view (none: drawn in time).</summary>
        std::optional<int32_t> FirstShows;
        /// <summary>Whether its block was drawn when it was first drawn (a map object's).</summary>
        bool BlockDrawn = true;
    };

    /// <summary>
    /// Pans the view so that <paramref name="object"/> comes in from <paramref name="edge"/>, two pixels a frame,
    /// starting with it <paramref name="beyond"/> pixels past it, until it is first drawn in the view, and finds whether
    /// its frames as then drawn already reached into the view by more than <paramref name="tolerance"/> pixels before (a
    /// unit's frames change as it plays).
    /// </summary>
    MCPanIn PanIn(MCGameObject& object, MCViewEdge edge, float beyond, float tolerance)
    {
        constexpr float FrameSeconds = 1.0f / 15.0f;
        constexpr float Step = 2.0f;
        MCCamera& eye = *Eye();
        const float width = std::floor(eye.ViewWidth);
        const float height = std::floor(eye.ViewHeight);
        const bool across = edge == MCViewEdge::Left || edge == MCViewEdge::Right;
        // The view moves away from the edge the object comes in from (screen pixels; ScrollCamera's units are half
        // a screen step each way).
        const float sign = edge == MCViewEdge::Bottom || edge == MCViewEdge::Right ? -1.0f : 1.0f;
        const auto scroll = [&](float pixels)
        {
            const float dx = across ? pixels / (2.0f * eye.CosAngle) : 0.0f;
            const float dy = across ? 0.0f : pixels / (2.0f * eye.SinAngle);
            eye.ScrollCamera(dx, dy);
        };

        int32_t block = -1;
        int32_t vertex = -1;
        object.GetBlockAndVertexNumber(block, vertex);
        const bool mapObject =
            object.ObjectClass != MCObjectClass::BattleMech && object.ObjectClass != MCObjectClass::GroundVehicle;
        // Where its frames are placed: a map object's vertex, a unit's position.
        const auto place = [&]
        { return mapObject ? VertexScreenPlace(block, vertex, eye) : ProjectToScreen(object.GetPosition(), eye); };

        const MCVector3D at = object.GetPosition();
        eye.SetPosition(MCVector3D(at.X, at.Y, eye.GetPosition().Z));
        const float out = (across ? width : height) / 2.0f + beyond;
        scroll(sign * out);
        DrawWatch().Object = object.weak_from_this();
        MCTestGame::RunFrame(FrameSeconds);

        struct MCStep
        {
            MCVector2D Place;
            MCDrawnBox Drawn;
            bool BlockDrawn = false;
        };

        // Whether a box reaches into the view by more than margin pixels.
        const auto reaches = [width, height](float left, float top, float right, float bottom, float margin)
        { return margin < right && margin < bottom && left < width - margin && top < height - margin; };
        // Drawn where it shows: an object out of the view can make a draw out of it too (placed where its vertex was
        // last projected), which nobody sees.
        const auto drawnInView = [&reaches](const MCDrawnBox& box)
        { return box.Frames > 0 && reaches(box.Left, box.Top, box.Right, box.Bottom, 0.0f); };
        std::vector<MCStep> steps;

        for (float moved = 0.0f; moved < out; moved += Step)
        {
            scroll(-sign * Step);
            MCTestGame::RunFrame(FrameSeconds);
            steps.push_back({place(), WatchedDraws(), !mapObject || Terrain()->BlockUsed(block)});

            if (drawnInView(steps.back().Drawn))
            {
                break;
            }
        }

        DrawWatch().Object.reset();
        MCPanIn found;

        if (steps.empty() || !drawnInView(steps.back().Drawn))
        {
            return found;
        }

        const MCStep& drawn = steps.back();
        found.FirstDrawn = static_cast<int32_t>(steps.size()) - 1;
        found.BlockDrawn = drawn.BlockDrawn;
        const float left = drawn.Drawn.Left - drawn.Place.X;
        const float top = drawn.Drawn.Top - drawn.Place.Y;
        const float right = drawn.Drawn.Right - drawn.Place.X;
        const float bottom = drawn.Drawn.Bottom - drawn.Place.Y;

        for (int32_t i = 0; i < *found.FirstDrawn; ++i)
        {
            const MCVector2D p = steps[static_cast<size_t>(i)].Place;

            if (reaches(p.X + left, p.Y + top, p.X + right, p.Y + bottom, tolerance))
            {
                found.FirstShows = i;
                break;
            }
        }

        return found;
    }

    /// <summary>
    /// The mission's objects of class <paramref name="wanted"/> with an appearance, nearest the map's middle first: the
    /// view can scroll past those on every side (it stops at the map's edges).
    /// </summary>
    std::vector<std::shared_ptr<MCGameObject>> ObjectsOfClass(MCObjectClass wanted)
    {
        std::vector<std::shared_ptr<MCGameObject>> found;

        for (const std::shared_ptr<MCObjectList>& list : ObjectList()->Lists())
        {
            for (const std::shared_ptr<MCBaseObject> object : *list)
            {
                if (object != nullptr && object->ObjectClass == wanted && object->GetAppearance() != nullptr)
                {
                    found.push_back(std::static_pointer_cast<MCGameObject>(object));
                }
            }
        }

        // The map's middle is the world's origin.
        const auto distance = [](const std::shared_ptr<MCGameObject>& object)
        {
            const MCVector3D at = object->GetPosition();
            return std::hypot(at.X, at.Y);
        };

        std::ranges::sort(found, std::ranges::less{}, distance);
        return found;
    }
}

/// <summary>
/// OB-192 (fixed): an object panned in from any edge of the view is drawn from the first frame its sprite reaches into
/// it, not once much of it is in view: buildings, trees, tree buildings, mechs and vehicles from each edge, each coming
/// in not yet drawn (a unit never drawn has the original's default box: each of them, as only the larger ones reach
/// past it), and objects on their block's bottom corner coming in from the top before any vertex of their block.
/// </summary>
TEST_CASE_ISOLATED("game: an object panned in from any edge is drawn from the first frame it reaches the view")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    Eye()->ChangeTarget(nullptr, false);
    RevealAll();
    MCTestGame::RunFrame(1.0f / 15.0f);
    // Past the edge by more than a sprite reaches from its place: a building's top is 173 pixels over its foot, a
    // unit's frames reach 54 pixels from it.
    constexpr float MapBeyond = 300.0f;
    constexpr float UnitBeyond = 120.0f;
    // A unit's frames change a pixel or two as it plays.
    constexpr float UnitTolerance = 2.0f;
    // A map object: the next of its kind in turn until one comes in not yet drawn. A unit: each of its kind, as only
    // the larger ones' frames reached past the original's box.
    constexpr size_t MapTries = 8;
    constexpr size_t UnitTries = 16;
    constexpr std::array<MCViewEdge, 4> edges = {MCViewEdge::Bottom, MCViewEdge::Top, MCViewEdge::Left,
                                                 MCViewEdge::Right};

    for (const MCObjectClass kind : {MCObjectClass::Building, MCObjectClass::Tree, MCObjectClass::TreeBuilding,
                                     MCObjectClass::BattleMech, MCObjectClass::GroundVehicle})
    {
        const std::vector<std::shared_ptr<MCGameObject>> objects = ObjectsOfClass(kind);
        REQUIRE(!objects.empty());
        const bool unit = kind == MCObjectClass::BattleMech || kind == MCObjectClass::GroundVehicle;
        const size_t tries = std::min(objects.size(), unit ? UnitTries : MapTries);

        for (size_t e = 0; e < edges.size(); ++e)
        {
            size_t cameIn = 0;

            for (size_t k = 0; k < tries && (unit || cameIn == 0); ++k)
            {
                MCGameObject& object = *objects[(e + k) % objects.size()];
                const MCPanIn pan = PanIn(object, edges[e], unit ? UnitBeyond : MapBeyond, unit ? UnitTolerance : 0.0f);

                // A unit can walk away from the view as it comes; a map object can't.
                if (!pan.FirstDrawn.has_value())
                {
                    REQUIRE(unit);
                    continue;
                }

                // In the view from the pan's first step (the view stops at the map's edges): it says nothing.
                if (*pan.FirstDrawn == 0)
                {
                    continue;
                }

                cameIn++;

                if (pan.FirstShows.has_value())
                {
                    FAIL_CHECK(std::format("object class {} from edge {}: in view at step {}, first drawn at step {}",
                                           static_cast<int32_t>(kind), e, *pan.FirstShows, *pan.FirstDrawn));
                }
            }

            if (cameIn == 0)
            {
                FAIL_CHECK(std::format("no object of class {} came in from edge {} not yet drawn",
                                       static_cast<int32_t>(kind), e));
            }
        }
    }

    // Map objects on their block's bottom corner coming in from the top: a block is drawn once a vertex of it is on
    // the screen, and an object's frames reach down from its vertex, so such an object shows before any vertex of its
    // block does. At least one must come in so, and each be drawn in time.
    constexpr int32_t CornerVertices = 2;
    constexpr size_t CornerTries = 12;
    const int32_t bottomCorner = 2 * (MCTerrain::VerticesBlockSide - 1);
    size_t tried = 0;
    size_t blockNotDrawn = 0;

    for (const MCObjectClass kind : {MCObjectClass::Building, MCObjectClass::TreeBuilding, MCObjectClass::Tree})
    {
        for (const std::shared_ptr<MCGameObject>& object : ObjectsOfClass(kind))
        {
            int32_t block = -1;
            int32_t vertex = -1;
            object->GetBlockAndVertexNumber(block, vertex);
            const int32_t row = vertex / MCTerrain::VerticesBlockSide;
            const int32_t column = vertex % MCTerrain::VerticesBlockSide;

            if (row + column < bottomCorner - CornerVertices || tried == CornerTries)
            {
                continue;
            }

            tried++;
            const MCPanIn pan = PanIn(*object, MCViewEdge::Top, MapBeyond, 0.0f);
            REQUIRE(pan.FirstDrawn.has_value());
            blockNotDrawn += *pan.FirstDrawn > 0 && !pan.BlockDrawn ? 1 : 0;

            if (pan.FirstShows.has_value())
            {
                FAIL_CHECK(
                    std::format("object class {} on block {}'s bottom corner: in view at step {}, first drawn at "
                                "step {}",
                                static_cast<int32_t>(kind), block, *pan.FirstShows, *pan.FirstDrawn));
            }
        }
    }

    REQUIRE(tried > 0);

    if (blockNotDrawn == 0)
    {
        FAIL_CHECK(
            std::format("none of the {} objects on a block's bottom corner came in with its block not drawn", tried));
    }
}
