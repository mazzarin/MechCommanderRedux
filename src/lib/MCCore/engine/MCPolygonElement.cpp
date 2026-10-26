#include "stdafx.h"
#include "engine/MCPolygonElement.h"
#include "camera/MCCamera.h"
#include "vfx/MCVfxFunctions.h"

MCPolygonElement::MCPolygonElement(MCPolyElementData data, int32_t depth) : MCElement(depth), Data(std::move(data))
{
}

auto MCPolygonElement::Draw() -> void
{
    if (Data.StatusBar)
    {
        AGStatusBar(*GlobalPane, Data.Vertices[0].X, Data.Vertices[0].Y, Data.Vertices[1].X, Data.Vertices[1].Y,
                    Data.BarColor, Data.BarPercent, Data.MeasuredBar);
        return;
    }

    if (Data.NumVertices == 0)
    {
        return;
    }

    const std::span<MCScreenVertex> vertices(Data.Vertices.data(), static_cast<size_t>(Data.NumVertices));

    const std::shared_ptr<MCWindow> texture = Data.Texture.lock();

    if (texture == nullptr)
    {
        if (Data.FadeTable.has_value())
        {
            VfxTranslatePolygon(*GlobalPane, vertices, *Data.FadeTable);
            return;
        }

        VfxGouraudPolygon(*GlobalPane, vertices);
        return;
    }

    if (Data.Translate && Data.FadeTable.has_value())
    {
        VfxTranslatePolygon(*GlobalPane, vertices, *Data.FadeTable);
        return;
    }

    if (!Data.TextureMapOff)
    {
        if (Data.FadeTable.has_value())
        {
            VfxMapLookaside(*Data.FadeTable);
            VfxMapPolygon(*GlobalPane, vertices, *texture, VfxMapXlat | VfxMapTransparent);
            return;
        }

        VfxMapPolygon(*GlobalPane, vertices, *texture, VfxMapTransparent);
    }
}
