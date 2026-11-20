#include "stdafx.h"
#include "object/MCObjectDrawing.h"
#include "camera/MCCamera.h"
#include "camera/MCCameraList.h"
#include "camera/MCViewWindow.h"
#include "color/MCPalette.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCEllipseElement.h"
#include "mission/MCScenario.h"
#include "terrain/MCTerrain.h"

bool DrawExtents = false;

MCVector2D ProjectToScreen(const MCVector3D& position, const MCCamera& camera)
{
    MCVector2D screen100;

    if (Terrain() != nullptr)
    {
        screen100 = MCTerrain::ProjectTerrain(position);
    }

    MCVector2D screen;
    screen.X = (screen100.X - camera.ScreenUL.X) + camera.HalfWidth;
    screen.Y = (screen100.Y - camera.ScreenUL.Y) + camera.HalfHeight;
    return screen;
}

void DrawExtentEllipse(const MCVector3D& position, MCVector2D size)
{
    const float sx = position.X - Eye()->Position.X;
    const float sy = position.Y - Eye()->Position.Y;
    MCVector2D center;
    center.X = sx * Eye()->CosAngle + sy * Eye()->CosAngle + Eye()->HalfWidth;
    center.Y = ((sx * Eye()->SinAngle + Eye()->HalfHeight) - sy * Eye()->SinAngle) - (position.Z - Eye()->Position.Z);
    ElementList().OpenGroup(-50000, true);
    // Port: an overlay, on the screen over the view: it follows the object through the zoom. Port-only
    // (HDSmoothCamera): placed from the camera's place unrounded, which the world's picture already follows, so without
    // the overlays' shift.
    center = MCOverlayPoint(MCVector2D(center.X - MCOverlay.Shift[0], center.Y - MCOverlay.Shift[1]));
    size.X *= MCOverlay.ScaleX;
    size.Y *= MCOverlay.ScaleY;
    ElementList().Add(ElementList().Make<MCEllipseElement>(center, size, 0xfe, -50000));
}
