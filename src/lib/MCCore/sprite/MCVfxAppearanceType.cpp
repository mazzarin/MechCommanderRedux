#include "stdafx.h"
#include "sprite/MCVfxAppearanceType.h"
#include "main/MCMissionGlobals.h"
#include "sprite/MCShape.h"

namespace
{
    /// <summary>Reads the entries states and sub-states share.</summary>
    auto ReadStateData(MCAppearanceFit& fit, MCActorData& data) -> void
    {
        data.NumFrames = fit.Read<uint32_t>("NumFrames");
        data.FrameRate = fit.Read<float>("FrameRate");
        data.BasePacketNumber = fit.Read<uint32_t>("BasePacketNumber");
        data.NumRotations = fit.Read<uint8_t>("NumRotations");
        data.Symmetrical = fit.Read<uint8_t>("Symmetrical") != 0;
    }
}

auto MCVfxAppearanceType::Load(MCFile& apprFile, uint32_t fileSize) -> MCAppearanceLoad
{
    MCAppearanceFit fit(apprFile, fileSize);
    fit.SeekBlock("Main Info");
    Delta = fit.Read<int32_t>("delta", 0) != 0;
    fit.SeekBlock("States");
    NumStates = fit.Read<uint8_t>("NumStates");
    Scaled = fit.Read<uint8_t>("Scaled", 0) != 0;
    States.assign(std::max<size_t>(ActorStateCount, NumStates), MCActorData{});

    for (int32_t i = 0; i < NumStates && !fit.Failed(); i++)
    {
        MCActorData& data = States[i];
        fit.SeekBlock(std::format("State{}", i));
        data.State = static_cast<MCActorState>(fit.Read<uint8_t>("State"));
        const uint8_t numSubStates = fit.Read<uint8_t>("SubStates", 0);
        ReadStateData(fit, data);

        // Only the first state with sub-states gets them (in practice state 0).
        if (numSubStates == 0 || !SubStates.empty())
        {
            continue;
        }

        SubStates.assign(numSubStates, MCActorData{});

        for (int32_t j = 0; j < numSubStates; j++)
        {
            MCActorData& subData = SubStates[j];
            fit.SeekBlock(std::format("Sub{}State{}", j, i));
            subData.State = static_cast<MCActorState>(fit.Read<uint8_t>("State"));
            subData.SubState = fit.Read<uint8_t>("Sub");
            ReadStateData(fit, subData);
            subData.Loop = fit.Read<uint8_t>("Loop") != 0;
        }
    }

    if (fit.Failed())
    {
        return fit.Result();
    }

    return MakeShapeList(ShapeList);
}

auto MCVfxAppearanceType::DrawnShapes() -> std::vector<std::shared_ptr<MCShape>>
{
    std::vector<uint32_t> packets;
    // As GetShape finds them: from the state's first packet, a facing each (every other packet in a scaled type).
    const auto addState = [&](const MCActorData& data)
    {
        if (data.NumFrames == 0)
        {
            return;
        }

        for (uint32_t facing = 0; facing < std::max<uint32_t>(data.NumRotations, 1); ++facing)
        {
            packets.push_back(data.BasePacketNumber + (Scaled ? facing << 1 : facing));
        }
    };

    std::ranges::for_each(States, addState);
    std::ranges::for_each(SubStates, addState);
    std::ranges::sort(packets);
    packets.erase(std::ranges::unique(packets).begin(), packets.end());
    std::vector<std::shared_ptr<MCShape>> shapes;

    for (const uint32_t packet : packets)
    {
        if (packet < ShapeList.size())
        {
            if (std::shared_ptr<MCShape> shape = PacketShape(ShapeList[packet], packet); shape != nullptr)
            {
                shapes.push_back(std::move(shape));
            }
        }
    }

    return shapes;
}

auto MCVfxAppearanceType::GetShape(MCActorState state, uint8_t subState, int32_t rotation, float& frameRate,
                                   bool& reverse) -> std::shared_ptr<MCShape>
{
    const MCActorData& data = StateData(state);
    reverse = false;

    if (data.NumFrames == 0)
    {
        return nullptr;
    }

    // Symmetrical states mirror the negative facings; the others wrap them.
    if (rotation < 0 && data.Symmetrical)
    {
        reverse = true;
        rotation = -rotation;
    }
    else if (rotation < 0)
    {
        rotation += 360;
    }

    int32_t rotationIndex = static_cast<int32_t>(
        std::floor(static_cast<double>(static_cast<int32_t>(data.NumRotations) * rotation) * (1.0 / 360.0)));
    frameRate = data.FrameRate;
    uint32_t basePacket = data.BasePacketNumber;

    if (subState != NoSubState && !SubStates.empty())
    {
        basePacket = SubStates[subState].BasePacketNumber;
        frameRate = SubStates[subState].FrameRate;
    }

    // Scaled types alternate full size and zoomed out packets (the original's zoomed-out mode).
    if (Scaled)
    {
        rotationIndex <<= 1;
    }

    const uint32_t packet = basePacket + static_cast<uint32_t>(rotationIndex);

    if (ShapeList.size() <= packet)
    {
        return nullptr;
    }

    return PacketShape(ShapeList[packet], packet);
}
