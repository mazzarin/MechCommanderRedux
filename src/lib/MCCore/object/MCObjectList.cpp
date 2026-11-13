#include "stdafx.h"
#include "object/MCObjectList.h"
#include "appear/MCAppearance.h"
#include "appear/MCAppearanceType.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiSystem.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBigGameObject.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectType.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCMiscTerrainObjectType.h"
#include "terrain/MCTerrain.h"

int UpdateObjects = 1;
int UpdateTerrainObjects = 0;
int RenderObjects = 1;
int RenderTerrainObjects = 1;

MCObjectList::MCObjectList(std::string_view name, int32_t blockNumber) : BlockNumber(blockNumber), _Name(name)
{
}

MCObjectList::~MCObjectList()
{
    Clear();
}

auto MCObjectList::Clear() -> void
{
    // First to last, as the original's destroy deleted them; each goes with its slot already empty.
    for (std::shared_ptr<MCBaseObject>& slot : _Objects)
    {
        if (slot != nullptr)
        {
            _Count--;
            std::shared_ptr<MCBaseObject> object = std::move(slot);
            object->MarkRemoved();
        }
    }

    _Objects.clear();
    _Count = 0;
}

auto MCObjectList::IsTerrainList() const -> bool
{
    return _Name.contains("TBlk") || _Name.contains("RBlk");
}

auto MCObjectList::After(const MCBaseObject& current) const -> std::shared_ptr<MCBaseObject>
{
    auto position = std::ranges::find_if(*this, [&current](const std::shared_ptr<MCBaseObject>& object)
                                         { return object.get() == &current; });

    if (position != end())
    {
        ++position;
    }

    if (position == end())
    {
        return nullptr;
    }

    return *position;
}

auto MCObjectList::Add(std::shared_ptr<MCBaseObject> object) -> std::shared_ptr<MCBaseObject>
{
    if (object == nullptr)
    {
        return nullptr;
    }

    _Count++;
    return _Objects.emplace_back(std::move(object));
}

auto MCObjectList::Release(const MCBaseObject& object) -> std::shared_ptr<MCBaseObject>
{
    const auto position = std::ranges::find(_Objects, &object, &std::shared_ptr<MCBaseObject>::get);

    if (position == _Objects.end())
    {
        return nullptr;
    }

    // The slot stays, empty, for a walk that is on it.
    _Count--;
    return std::move(*position);
}

auto MCObjectList::Remove(MCBaseObject& object) -> bool
{
    // Out of the list before it goes: its destructor may walk the lists.
    std::shared_ptr<MCBaseObject> removed = Release(object);

    if (removed == nullptr)
    {
        return false;
    }

    removed->MarkRemoved();
    return true;
}

auto MCObjectList::Compact() -> void
{
    // Only a list holding an empty slot is walked: a walk of every list every frame went through every object on the
    // map (on the M4 Pro about 1% of the game thread's work).
    if (_Count != _Objects.size())
    {
        std::erase(_Objects, nullptr);
    }
}

auto MCObjectList::Render() -> void
{
    const bool terrain = IsTerrainList();

    if (!((terrain && RenderTerrainObjects != 0) || (!terrain && RenderObjects != 0)))
    {
        return;
    }

    // The original breaks into the debugger (int 3) when "TBlk" is found other than at the start of the name.
    if (!BlockInList(BlockNumber))
    {
        return;
    }

    for (std::shared_ptr<MCBaseObject> object : *this)
    {
        object->Render();
    }
}

auto MCObjectList::Update() -> void
{
    const bool terrain = IsTerrainList();

    if (!((terrain && UpdateTerrainObjects != 0) || (!terrain && UpdateObjects != 0)))
    {
        return;
    }

    if (!BlockInList(BlockNumber))
    {
        return;
    }

    // An update may delete objects of the list or add some at its end; the walk goes on from the object it updated,
    // as the original's did.
    for (auto position = _Objects.begin(); position != _Objects.end(); ++position)
    {
        // Pinned for its update: removed during it, it goes when the walk moves on.
        std::shared_ptr<MCBaseObject> object = *position;

        if (object == nullptr)
        {
            continue;
        }

        MCObjectQueue::ObjectsInList++;

        if (object->Update() == 0 && *position != nullptr && object->GetObjectType() != nullptr)
        {
            _Count--;
            std::shared_ptr<MCBaseObject> dead = std::move(*position);
            dead->MarkRemoved();
        }
    }
}

auto MCObjectList::FindObjectFromEvent(MCObjectEvent& event, int skipDisabled) -> std::shared_ptr<MCBaseObject>
{
    if (!BlockInList(BlockNumber))
    {
        return nullptr;
    }

    for (std::shared_ptr<MCBaseObject> object : *this)
    {
        auto& gameObject = static_cast<MCGameObject&>(*object);
        std::shared_ptr<MCAppearance> appearance = object->GetAppearance();

        if (appearance == nullptr || appearance->Visible == 0)
        {
            // The original also tests objectClass 0x14 against floats at +0x94..+0xa0, but no class ever sets
            // 0x14 (see MCObjectClass), so that branch is dead and left out.
            if (object->ObjectClass != MCObjectClass::MiscTerrainObject)
            {
                continue;
            }

            const std::shared_ptr<MCGuiObject> window = event.Event.TargetObject();
            const std::shared_ptr<MCCamera> cam = window != nullptr ? window->GetCamera() : nullptr;

            if (cam == nullptr)
            {
                continue;
            }

            // Port: on the view's world surface, through the zoom.
            const MCVector2D mouse = MCWindowPoint(*window, event.Event.X, event.Event.Y);
            const float mouseX = mouse.X;
            const float mouseY = mouse.Y;
            auto& misc = static_cast<MCMiscTerrainObject&>(*object);
            int32_t block = misc.BlockNumber;
            int32_t vertex = misc.VertexNumber;

            if (block < 0)
            {
                block = 0;
            }

            if (block >= MCTerrain::TotalBlocks)
            {
                block = MCTerrain::TotalBlocks - 1;
            }

            if (vertex < 0)
            {
                vertex = 0;
            }

            if (vertex >= VerticesPerBlock)
            {
                vertex = VerticesPerBlock - 1;
            }

            const int32_t screenX = Terrain()->ScreenPosX[Terrain()->BlockOffsets[block] + vertex];

            if (screenX == 0x11111111)
            {
                continue;
            }

            // The box sits 70 pixels below the vertex: 50 pixels each way for a bridge, 30 otherwise.
            const float halfSize = misc.Kind == MCMiscTerrainKind::Bridge ? 50.0f : 30.0f;
            const float centerX = static_cast<float>(screenX);
            const float centerY =
                70.0f + static_cast<float>(Terrain()->ScreenPosY[Terrain()->BlockOffsets[block] + vertex]);

            if (centerX - halfSize <= mouseX && mouseX <= halfSize + centerX && centerY - halfSize <= mouseY &&
                mouseY <= halfSize + centerY)
            {
                return object;
            }

            continue;
        }

        if (gameObject.GetWindowsVisible() <= Turn - 3)
        {
            continue;
        }

        const std::shared_ptr<MCGuiObject> window = event.Event.TargetObject();
        const std::shared_ptr<MCCamera> camera = window->GetCamera();
        appearance->RecalcBounds(camera != nullptr ? MCOptionalRef<MCCamera>(*camera) : std::nullopt);
        // Port: on the view's world surface, through the zoom.
        const MCVector2D mouse = MCWindowPoint(*window, event.Event.X, event.Event.Y);
        const float mouseX = mouse.X;
        const float mouseY = mouse.Y;
        const std::shared_ptr<MCAppearanceType> type = appearance->GetAppearanceType();

        if (type != nullptr && (type->BoundsUpperLeftX != 0 || type->BoundsUpperLeftY != 0 ||
                                type->BoundsLowerRightX != 0 || type->BoundsLowerRightY != 0))
        {
            const int32_t left = type->BoundsUpperLeftX;
            const int32_t top = type->BoundsUpperLeftY;
            const int32_t right = type->BoundsLowerRightX;
            const int32_t bottom = type->BoundsLowerRightY;

            if (!(static_cast<float>(left) + appearance->GetScreenPos().X <= mouseX &&
                  mouseX <= static_cast<float>(right) + appearance->GetScreenPos().X &&
                  static_cast<float>(top) + appearance->GetScreenPos().Y <= mouseY &&
                  mouseY <= static_cast<float>(bottom) + appearance->GetScreenPos().Y))
            {
                continue;
            }
        }
        else if (mouseX < appearance->UpperLeft.X || appearance->LowerRight.X < mouseX ||
                 mouseY < appearance->UpperLeft.Y || appearance->LowerRight.Y < mouseY)
        {
            continue;
        }

        if (gameObject.IsDisabled() == 0 && gameObject.IsDestroyed() == 0)
        {
            return object;
        }

        if (skipDisabled == 0)
        {
            return object;
        }
    }

    return nullptr;
}

auto MCObjectList::HandleEvent(MCObjectEvent& event) -> std::shared_ptr<MCBaseObject>
{
    std::shared_ptr<MCBaseObject> object = FindObjectFromEvent(event, 0);

    if (object != nullptr)
    {
        object->HandleEvent(event);
    }

    return object;
}

auto MCObjectList::FindObject(MCVector3D position, float& distance) -> std::shared_ptr<MCBaseObject>
{
    std::shared_ptr<MCBaseObject> result;

    for (std::shared_ptr<MCBaseObject> object : *this)
    {
        if (static_cast<int32_t>(object->ObjectClass) <= 0)
        {
            continue;
        }

        auto& gameObject = static_cast<MCGameObject&>(*object);

        if (gameObject.InTransport() != 0)
        {
            continue;
        }

        const auto objectDistance = static_cast<float>(gameObject.DistanceFrom(position));
        std::shared_ptr<MCObjectType> type = gameObject.GetObjectType();
        const float extent = type != nullptr ? type->ExtentRadius : 0.0f;

        if (objectDistance < extent && objectDistance < distance)
        {
            distance = objectDistance;
            result = object;
        }
    }

    return result;
}

auto MCObjectList::FindPart(int32_t partId) const -> std::shared_ptr<MCBaseObject>
{
    for (std::shared_ptr<MCBaseObject> object : *this)
    {
        if (object->PartId == partId)
        {
            return object;
        }
    }

    return nullptr;
}

auto BlockInList(int32_t blockNumber) -> int
{
    return blockNumber == -1 || (Terrain() != nullptr && Terrain()->BlockUsed(blockNumber)) ? 1 : 0;
}
