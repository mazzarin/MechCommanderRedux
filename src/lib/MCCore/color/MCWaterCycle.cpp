#include "stdafx.h"
#include "color/MCWaterCycle.h"
#include "color/MCPalette.h"
#include "gui/MCGuiSystem.h"
#include "mission/MCScenario.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "vfx/MCVfx.h"

uint8_t CurrentMagic = 0;

namespace
{
    constexpr float MillisecondsPerSecond = 1000.0f;

    /// <summary>When the cycle last stepped (or would have, with palette cycling off): set on the first call.</summary>
    std::optional<uint32_t> LastCycleTime;
}

auto StepWaterColors(MCPalette& palette, std::shared_ptr<MCDisplay> display) -> void
{
    for (size_t i = 0; i < WaterMagicColors.size(); ++i)
    {
        const MCVfxRgb color = palette.Colors()[WaterMagicColors[(CurrentMagic + i) % WaterMagicColors.size()]];
        palette.TweakPalette(FirstWaterColor + static_cast<int32_t>(i), std::span(&color, 1));
    }

    if (display != nullptr)
    {
        MCColorCycle cycle;
        cycle.First = FirstWaterColor;
        std::ranges::copy(WaterMagicColors, cycle.Sources.begin());
        cycle.Step = CurrentMagic;
        display->SetColorCycle(cycle);
    }

    CurrentMagic = static_cast<uint8_t>((CurrentMagic + 1) % WaterMagicColors.size());
}

auto CycleColors() -> void
{
    // Set on the first call; the original kept a start time it never read.
    if (!LastCycleTime.has_value())
    {
        LastCycleTime = MCPort::Milliseconds();
    }

    if (Scenario() == nullptr)
    {
        return;
    }

    if (Scenario()->CycleLength * MillisecondsPerSecond < static_cast<float>(MCPort::Milliseconds() - *LastCycleTime))
    {
        LastCycleTime = MCPort::Milliseconds();

        if (GuiSystem().PaletteCycle != 0)
        {
            StepWaterColors(GamePalette(), MCInput::Display());
        }
    }
}

auto WaterCyclePhase() -> float
{
    if (Scenario() == nullptr || GuiSystem().PaletteCycle == 0 || !LastCycleTime.has_value() ||
        Scenario()->CycleLength <= 0.0f)
    {
        return 0.0f;
    }

    const auto elapsed = static_cast<float>(MCPort::Milliseconds() - *LastCycleTime);
    return std::clamp(elapsed / (Scenario()->CycleLength * MillisecondsPerSecond), 0.0f, 1.0f);
}
