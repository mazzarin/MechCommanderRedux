#include "stdafx.h"
#include "gui/MCGuiAnimation.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "main/MCGamePaths.h"
#include "platform/MCRenderer.h"
#include "sprite/MCShapeFile.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>
    /// The original's <c>clock()</c>: MSVC's counts wall-clock milliseconds since the process started
    /// (<c>CLOCKS_PER_SEC</c> is 1000).
    /// </summary>
    int32_t ClockTicks()
    {
        return static_cast<int32_t>(MCPort::Milliseconds());
    }
}

MCGuiAnimation::~MCGuiAnimation()
{
    Unload();
}

auto MCGuiAnimation::Unload() -> void
{
    // The times are left alone.
    CurFrame = 0;
    NumFrames = 0;
    ShapeWidth = 0;
    ShapeHeight = 0;
    Rate = 15.0f;

    Shapes = {};
}

auto MCGuiAnimation::Load(std::string_view fileName) -> int32_t
{
    Unload();
    // Port-only: with HD art's numbered folders drawn, the pack's copy of the shape file (MCShapeFile).
    MCShapeFile file;
    const std::string path = std::format("{}{}", std::string_view(ArtPath), fileName);

    if (!FileExists(path))
    {
        GeneralMsg(std::format("Unable to find '{}'", path));
    }

    file.Open(path);
    const uint32_t size = file.FileSize();

    if (size == 0)
    {
        return -2;
    }

    Shapes = MCRegisteredBlock(size, MCDataKind::Shapes);
    file.Read(Shapes.Bytes());
    file.Close();
    NumFrames = VfxShapeCount(Shapes.Bytes());
    CurFrame = 0;

    if (NumFrames != 0)
    {
        const int32_t bounds = VfxShapeBounds(Shapes.Bytes(), 0);
        ShapeWidth = bounds >> 16;
        ShapeHeight = bounds & 0xffff;
    }

    LastTime = ClockTicks();
    return 0;
}

auto MCGuiAnimation::DrawFrame(int32_t frame, const MCPane& pane, int32_t xPos, int32_t yPos) const -> void
{
    AGShapeDraw(pane, Shapes.Bytes(), frame, xPos, yPos);
}

auto MCGuiAnimation::Draw(const MCPane& pane, int32_t xPos, int32_t yPos) -> void
{
    AGShapeDraw(pane, Shapes.Bytes(), CurFrame, xPos, yPos);
    ThisTime = ClockTicks();

    if (1.0f / Rate < static_cast<float>(ThisTime - LastTime) * 0.001f)
    {
        SetFrame(NextFrame());
        LastTime = ThisTime;
    }
}

auto MCGuiAnimation::NextFrame() const -> int32_t
{
    const int32_t next = CurFrame + 1;
    return next < NumFrames ? next : 0;
}
