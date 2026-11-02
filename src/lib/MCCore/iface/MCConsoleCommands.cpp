#include "stdafx.h"
#include "iface/MCConsoleCommands.h"
#include "iface/MCTacticalInterface.h"
#include "main/MCArtModes.h"
#include "object/MCBaseObject.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMover.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"

namespace
{
    /// <summary>A command's arguments: the line's words after the first.</summary>
    using MCConsoleArguments = std::span<const std::string_view>;

    /// <summary>A console command: its name and what it does.</summary>
    struct MCConsoleCommand
    {
        std::string_view Name;
        std::function<std::expected<void, std::string>(MCConsoleArguments)> Run;
    };

    /// <summary>The selected movers that are still in the game.</summary>
    std::vector<std::shared_ptr<MCMover>> SelectedMovers()
    {
        std::vector<std::shared_ptr<MCMover>> movers;
        const std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();

        if (iface == nullptr || ObjectList() == nullptr)
        {
            return movers;
        }

        for (const int32_t partId : iface->SelectedMovers)
        {
            std::shared_ptr<MCBaseObject> object = ObjectList()->FindObjectFromPart(partId);

            if (object != nullptr && IsMoverClass(object->ObjectClass))
            {
                movers.push_back(std::static_pointer_cast<MCMover>(object));
            }
        }

        return movers;
    }

    /// <summary><c>destroyarm right|left</c>: destroys that arm of each selected mech.</summary>
    std::expected<void, std::string> DestroyArm(MCConsoleArguments arguments)
    {
        if (arguments.size() != 1 || (!MCIEquals(arguments[0], "right") && !MCIEquals(arguments[0], "left")))
        {
            return std::unexpected("usage: destroyarm right|left");
        }

        const int32_t location = MCIEquals(arguments[0], "right") ? MechRightArm : MechLeftArm;
        int32_t mechs = 0;

        for (const std::shared_ptr<MCMover>& mover : SelectedMovers())
        {
            if (mover->ObjectClass == MCObjectClass::BattleMech)
            {
                mover->DestroyBodyLocation(location);
                mechs++;
            }
        }

        if (mechs == 0)
        {
            return std::unexpected("destroyarm: no mech selected");
        }

        return {};
    }

    /// <summary>
    /// <c>art original|hd|day|dusk|night</c>: shows that art mode (MCArtModes.h) from the next frame on, as HD ART and HD
    /// LIGHTING set so would, unsaved.
    /// </summary>
    std::expected<void, std::string> Art(MCConsoleArguments arguments)
    {
        const std::optional<MCArtMode> mode = arguments.size() == 1 ? ArtModeNamed(arguments[0]) : std::nullopt;

        if (!mode.has_value())
        {
            std::string names;

            for (const MCArtMode& each : ArtModes())
            {
                names += std::format("{}{}", names.empty() ? "" : "|", each.Name);
            }

            return std::unexpected(std::format("usage: art {}", names));
        }

        return ShowArtMode(*mode).transform_error([](const std::string& error)
                                                  { return std::format("art: {}", error); });
    }

    /// <summary>The commands.</summary>
    const std::array<MCConsoleCommand, 2> Commands = {{
        {"destroyarm", DestroyArm},
        {"art", Art},
    }};
}

auto RunConsoleCommand(std::string_view line) -> std::expected<void, std::string>
{
    std::vector<std::string_view> words;

    for (const auto word : std::views::split(line, ' '))
    {
        if (!word.empty())
        {
            words.emplace_back(word.begin(), word.end());
        }
    }

    if (words.empty())
    {
        return {};
    }

    const auto command = std::ranges::find_if(Commands, [&words](const MCConsoleCommand& entry)
                                              { return MCIEquals(entry.Name, words[0]); });

    if (command == Commands.end())
    {
        return std::unexpected(std::format("unknown command: {}", words[0]));
    }

    return command->Run(MCConsoleArguments(words).subspan(1));
}
