#include "stdafx.h"
#include "appear/MCAppearance.h"
#include "appear/MCAppearanceType.h"
#include "camera/MCCamera.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCLineElement.h"
#include "lib/MCFrameOfRef.h"
#include "lib/MCVector2D.h"
#include "lib/MCDice.h"
#include "object/MCBigGameObject.h"
#include "platform/MCFrameLog.h"
#include "sprite/MCHdArt.h"

namespace
{
    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): how far past the view's edges, as a share of its size,
    /// an appearance has the packets it would draw read ahead (MCAppearance::NearView).
    /// </summary>
    constexpr float NearViewShare = 0.5f;
    /// <summary>Port-only: the frame log's part for an appearance's asks (MCAppearance::ReadAheadNear).</summary>
    constexpr std::string_view FrameLogAsk = "hd.ask";

    /// <summary>The depth the selection marks draw at (in front of everything).</summary>
    constexpr int32_t SelectDepth = -50000;

    /// <summary>Adds a one-colour line from (x0, y0) to (x1, y1) at <see cref="SelectDepth"/>.</summary>
    void AddSelectLine(float x0, float y0, float x1, float y1, uint8_t color)
    {
        MCVector2D start;
        start.X = x0;
        start.Y = y0;
        MCVector2D end;
        end.X = x1;
        end.Y = y1;
        ElementList().Add(ElementList().Make<MCLineElement>(start, end, color, std::nullopt, SelectDepth, -1));
    }

    /// <summary>
    /// The screen box the selection marks go around: the type's "Bounds" about the screen position, or the appearance's own bounds when the type has none.
    /// </summary>
    /// <remarks>Port: on the screen over the view (the marks are overlays), so the box follows the sprite through the
    /// zoom and the marks keep their size.</remarks>
    void SelectBounds(MCAppearance& appearance, float& left, float& top, float& right, float& bottom)
    {
        const std::shared_ptr<MCAppearanceType> type = appearance.GetAppearanceType();

        if (type == nullptr || (type->BoundsUpperLeftX == 0 && type->BoundsUpperLeftY == 0 &&
                                type->BoundsLowerRightX == 0 && type->BoundsLowerRightY == 0))
        {
            left = appearance.UpperLeft.X;
            top = appearance.UpperLeft.Y;
            right = appearance.LowerRight.X;
            bottom = appearance.LowerRight.Y;
        }
        else
        {
            left = static_cast<float>(type->BoundsUpperLeftX) + appearance.ScreenPos.X;
            top = static_cast<float>(type->BoundsUpperLeftY) + appearance.ScreenPos.Y;
            right = static_cast<float>(type->BoundsLowerRightX) + appearance.ScreenPos.X;
            bottom = static_cast<float>(type->BoundsLowerRightY) + appearance.ScreenPos.Y;
        }

        left = MCOverlayX(left);
        top = MCOverlayY(top);
        right = MCOverlayX(right);
        bottom = MCOverlayY(bottom);
    }

    /// <summary>The marks' distance from the box, in pixels.</summary>
    constexpr float SelectMargin = 5.0f;
}

auto MCAppearance::GetScreenPos(const MCCamera& cam) const -> MCVector2D
{
    MCVector2D result;
    const MCVector3D position = Owner.lock()->GetPosition();
    const float sx = position.X - cam.Position.X;
    const float sy = position.Y - cam.Position.Y;
    result.X = sx * cam.CosAngle + sy * cam.CosAngle + cam.HalfWidth;
    result.Y = ((sx * cam.SinAngle + cam.HalfHeight) - sy * cam.SinAngle) - (position.Z - cam.Position.Z);
    return result;
}

auto MCAppearance::DrawSelectBox(uint8_t color) -> void
{
    float left;
    float top;
    float right;
    float bottom;
    SelectBounds(*this, left, top, right, bottom);
    ElementList().OpenGroup(SelectDepth, true);
    const float margin = SelectMargin;
    const float outLeft = left - margin;
    const float outTop = top - margin;
    const float outRight = margin + right;
    const float outBottom = margin + bottom;
    // A corner mark at each corner of the box grown by the margin.
    AddSelectLine(outLeft, outTop, outLeft, top, color);
    AddSelectLine(outLeft, outTop, left, outTop, color);
    AddSelectLine(outRight, outTop, outRight, top, color);
    AddSelectLine(outRight, outTop, right, outTop, color);
    AddSelectLine(outRight, outBottom, outRight, bottom, color);
    AddSelectLine(outRight, outBottom, right, outBottom, color);
    AddSelectLine(outLeft, outBottom, outLeft, bottom, color);
    AddSelectLine(outLeft, outBottom, left, outBottom, color);
}

auto MCAppearance::DrawSelectBrackets(uint8_t color) -> void
{
    const float margin = SelectMargin;
    float left;
    float top;
    float right;
    float bottom;
    SelectBounds(*this, left, top, right, bottom);
    ElementList().OpenGroup(SelectDepth, true);
    const float outLeft = left - margin;
    const float outTop = top - margin;
    const float outRight = right + margin;
    const float outBottom = bottom + margin;
    // A bar over and under the box, each with short ends turned toward it. (The second line lies on the third.)
    AddSelectLine(outLeft, top, outLeft, outTop, color);
    AddSelectLine(outLeft, outTop, left, outTop, color);
    AddSelectLine(outLeft, outTop, outRight, outTop, color);
    AddSelectLine(outRight, top, outRight, outTop, color);
    AddSelectLine(outLeft, bottom, outLeft, outBottom, color);
    AddSelectLine(outLeft, outBottom, outRight, outBottom, color);
    AddSelectLine(outRight, outBottom, outRight, bottom, color);
}

auto MCAppearance::NearView(const MCCamera& view) const -> bool
{
    const auto width = static_cast<float>(static_cast<int32_t>(std::floor(view.ViewWidth)));
    const auto height = static_cast<float>(static_cast<int32_t>(std::floor(view.ViewHeight)));
    const float marginX = width * NearViewShare;
    const float marginY = height * NearViewShare;
    return -marginX <= LowerRight.X && -marginY <= LowerRight.Y && UpperLeft.X <= width + marginX &&
           UpperLeft.Y <= height + marginY;
}

auto MCAppearance::ReadAheadNear(const MCCamera& view, bool shown) -> void
{
    if ((shown || NearView(view)) && MCHdArt::NumberedFolders())
    {
        const MCFrameLog::Scope part(FrameLogAsk);
        ReadAhead();
    }
}
