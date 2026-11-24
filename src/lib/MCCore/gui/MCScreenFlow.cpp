#include "stdafx.h"
#include "gui/MCScreenFlow.h"
#include "gui/MCUpdateDisplay.h"
#include "platform/MCFrameLog.h"

namespace
{
    using MCScreenFlow::MCScreenId;
    using MCScreenFlow::MCScreenKind;
    using MCScreenFlow::NoScreen;

    /// <summary>The frame log's part a change of screen's own work goes in, and the next screen's rehearsal's.</summary>
    constexpr std::string_view ScreenPart = "change.screen";
    constexpr std::string_view RehearsalPart = "change.next screen";
    /// <summary>Between the stack's names in <see cref="MCScreenFlow::StackText"/>.</summary>
    constexpr std::string_view StackJoin = " > ";

    /// <summary>A change of screen asked for while another ran (a network's event in a fade), started after it.</summary>
    struct MCQueued
    {
        std::string Name;
        std::function<void()> Work;
        std::vector<MCScreenChange::MCPiece> Prepare;
    };

    /// <summary>How the stack moves with a screen the flow shows itself.</summary>
    enum class MCMove
    {
        Forward,
        Replace,
        Back
    };

    std::vector<MCScreenKind> Kinds;
    std::vector<MCScreenId> StackIds;
    std::vector<MCScreenId> Landed;
    std::deque<MCQueued> Queue;
    size_t UnroutedChanges = 0;

    bool Known(MCScreenId id)
    {
        return id >= 0 && static_cast<size_t>(id) < Kinds.size();
    }

    std::string_view NameOf(MCScreenId id)
    {
        return Known(id) ? std::string_view(Kinds[static_cast<size_t>(id)].Name) : std::string_view("none");
    }

    /// <summary>Runs <paramref name="run"/> when set.</summary>
    void RunIfSet(const std::function<void()>& run)
    {
        if (run != nullptr)
        {
            run();
        }
    }

    /// <summary>Whether kind <paramref name="id"/> says it's the screen shown now (its Shows).</summary>
    bool ShowsNow(MCScreenId id)
    {
        return Known(id) && Kinds[static_cast<size_t>(id)].Shows != nullptr && Kinds[static_cast<size_t>(id)].Shows();
    }

    /// <summary>
    /// The screen shown now, as the kinds say: the stack's top first (unchanged; one without a Shows, which only the flow
    /// shows, is taken at its word), then the landing's, the rest of the stack (a way back), then any (a way forward).
    /// </summary>
    MCScreenId ShownNow()
    {
        if (!StackIds.empty() &&
            (ShowsNow(StackIds.back()) ||
             (Known(StackIds.back()) && Kinds[static_cast<size_t>(StackIds.back())].Shows == nullptr)))
        {
            return StackIds.back();
        }

        if (!Landed.empty() && ShowsNow(Landed.back()))
        {
            return Landed.back();
        }

        for (auto at = StackIds.rbegin(); at != StackIds.rend(); ++at)
        {
            if (ShowsNow(*at))
            {
                return *at;
            }
        }

        for (size_t id = 0; id < Kinds.size(); id++)
        {
            if (ShowsNow(static_cast<MCScreenId>(id)))
            {
                return static_cast<MCScreenId>(id);
            }
        }

        return NoScreen;
    }

    /// <summary>Screen <paramref name="id"/> shown: the stack follows (<see cref="MCScreenFlow::NoteShown"/>) as <paramref name="move"/> says.</summary>
    void Follow(MCScreenId id, MCMove move)
    {
        if (!Known(id))
        {
            return;
        }

        const std::string before = MCScreenFlow::StackText();

        if (!Landed.empty() && Landed.back() == id)
        {
            StackIds = std::move(Landed);
            Landed.clear();
        }
        else if (const auto found = std::ranges::find(StackIds, id); found != StackIds.end())
        {
            StackIds.erase(found + 1, StackIds.end());
        }
        else
        {
            // Over a screen that gives way (a file screen), or replacing the top: in its place.
            if (!StackIds.empty() && (move == MCMove::Replace || Kinds[static_cast<size_t>(StackIds.back())].GivesWay))
            {
                StackIds.pop_back();
            }

            StackIds.push_back(id);
        }

        const std::string after = MCScreenFlow::StackText();

        if (after != before)
        {
            MCFrameLog::Note(std::format("flow: {}", after));
        }
    }

    /// <summary>The pieces of a change of screen's dark work: <paramref name="work"/>, the stack noted, then the rehearsal.</summary>
    std::vector<MCScreenChange::MCPiece> DarkPieces(std::function<void()> work, std::function<bool()> ready)
    {
        std::vector<MCScreenChange::MCPiece> pieces;
        pieces.push_back({.Part = ScreenPart, .Run = std::move(work), .Until = std::move(ready)});

        for (std::function<void()>& piece : IntoScreenPieces())
        {
            pieces.push_back({.Part = RehearsalPart, .Run = std::move(piece)});
        }

        return pieces;
    }

    /// <summary>
    /// Starts the change of screen named <paramref name="name"/>: <paramref name="prepare"/> from the press, live, then
    /// dark <paramref name="work"/> until <paramref name="ready"/>; at once when nothing is shown. Whether it started (or
    /// ran).
    /// </summary>
    bool StartChange(std::string name, std::function<void()> work, std::vector<MCScreenChange::MCPiece> prepare,
                     std::function<bool()> ready)
    {
        // A copy: a change that can't start (nothing shown) runs it below.
        if (MCScreenChange::Start({.Name = std::move(name),
                                   .Job = prepare,
                                   .FadeOut = MCScreenChange::MCFadeOut::AtPress,
                                   .Dark = DarkPieces(work, ready)}))
        {
            return true;
        }

        // Nothing shown (no display): the change at once.
        MCScreenChange::RunPieces(prepare);
        MCScreenChange::RunPieces(
            std::vector<MCScreenChange::MCPiece>{{.Run = std::move(work), .Until = std::move(ready)}});
        return true;
    }

    /// <summary>Shows screen <paramref name="id"/> in place of the one shown, the stack moving as <paramref name="move"/> says.</summary>
    bool ShowScreen(MCScreenId id, MCMove move)
    {
        if (!Known(id) || MCScreenChange::Running())
        {
            return false;
        }

        MCScreenFlow::Noted();
        const MCScreenId from = MCScreenFlow::Shown();
        // Copies: a kind's Prepare may register kinds (a logistics' start), which moves them.
        const std::string name = std::format("{} to {}", NameOf(from), Kinds[static_cast<size_t>(id)].Name);
        std::function<bool()> ready = Kinds[static_cast<size_t>(id)].Ready;
        const std::function<std::vector<MCScreenChange::MCPiece>()> makePrepare =
            Kinds[static_cast<size_t>(id)].Prepare;
        std::vector<MCScreenChange::MCPiece> prepare =
            makePrepare != nullptr ? makePrepare() : std::vector<MCScreenChange::MCPiece>{};
        return StartChange(
            name,
            [id, from, move]
            {
                if (Known(from))
                {
                    RunIfSet(Kinds[static_cast<size_t>(from)].Hide);
                }

                RunIfSet(Kinds[static_cast<size_t>(id)].Show);
                Follow(id, move);
            },
            std::move(prepare), std::move(ready));
    }
}

auto MCScreenFlow::Register(MCScreenKind kind) -> MCScreenId
{
    if (const MCScreenId found = Find(kind.Name); found != NoScreen)
    {
        Kinds[static_cast<size_t>(found)] = std::move(kind);
        return found;
    }

    Kinds.push_back(std::move(kind));
    return static_cast<MCScreenId>(Kinds.size() - 1);
}

auto MCScreenFlow::Find(std::string_view name) -> MCScreenId
{
    const auto found = std::ranges::find(Kinds, name, &MCScreenKind::Name);
    return found != Kinds.end() ? static_cast<MCScreenId>(found - Kinds.begin()) : NoScreen;
}

auto MCScreenFlow::Kind(MCScreenId id) -> MCOptionalRef<const MCScreenKind>
{
    return Known(id) ? MCOptionalRef<const MCScreenKind>(Kinds[static_cast<size_t>(id)]) : std::nullopt;
}

auto MCScreenFlow::Open(MCScreenId id) -> bool
{
    return ShowScreen(id, MCMove::Forward);
}

auto MCScreenFlow::Replace(MCScreenId id) -> bool
{
    return ShowScreen(id, MCMove::Replace);
}

auto MCScreenFlow::Back() -> bool
{
    if (MCScreenChange::Running())
    {
        return false;
    }

    // A screen shown outside a change (an old path the flow doesn't route yet): the stack follows it first.
    Noted();

    if (StackIds.empty())
    {
        return false;
    }

    const MCScreenKind& top = Kinds[static_cast<size_t>(StackIds.back())];
    MCFrameLog::Note(std::format("flow: back from {}", top.Name));

    if (top.Leave != nullptr)
    {
        // Copied: a Leave may register screens again (the menus made again).
        const std::function<void()> leave = top.Leave;
        leave();
        return true;
    }

    return ShowBelow();
}

auto MCScreenFlow::ShowBelow() -> bool
{
    if (StackIds.size() < 2)
    {
        return false;
    }

    return ShowScreen(StackIds[StackIds.size() - 2], MCMove::Back);
}

auto MCScreenFlow::Land(std::vector<MCScreenId> stack) -> void
{
    std::erase_if(stack, std::not_fn(Known));
    Landed = std::move(stack);

    if (!Landed.empty())
    {
        std::string text;

        for (const MCScreenId id : Landed)
        {
            text += text.empty() ? std::string() : std::string(StackJoin);
            text += NameOf(id);
        }

        MCFrameLog::Note(std::format("flow: lands on {}", text));
    }
}

auto MCScreenFlow::Landing() -> std::span<const MCScreenId>
{
    return Landed;
}

auto MCScreenFlow::Change(std::string name, std::function<void()> work, std::vector<MCScreenChange::MCPiece> prepare)
    -> void
{
    const auto noted = [work = std::move(work)]
    {
        RunIfSet(work);
        Noted();
    };

    // A change's own work (dark already, or its job), or nothing shown: at once, what it would prepare first.
    if (MCScreenChange::InPiece())
    {
        MCScreenChange::RunPieces(prepare);
        noted();
        return;
    }

    if (MCScreenChange::Running())
    {
        MCFrameLog::Note(std::format("flow: {} waits for change {}", name, MCScreenChange::Name()));
        Queue.push_back({.Name = std::move(name), .Work = noted, .Prepare = std::move(prepare)});
        return;
    }

    StartChange(std::move(name), noted, std::move(prepare), {});
}

auto MCScreenFlow::Via(std::string name, std::function<void()> work) -> std::function<void()>
{
    return [name = std::move(name), work = std::move(work)] { Change(name, work); };
}

auto MCScreenFlow::Tend() -> void
{
    if (Queue.empty() || MCScreenChange::Running())
    {
        return;
    }

    MCQueued next = std::move(Queue.front());
    Queue.pop_front();
    StartChange(std::move(next.Name), std::move(next.Work), std::move(next.Prepare), {});
}

auto MCScreenFlow::Noted() -> void
{
    if (const MCScreenId shown = ShownNow(); shown != NoScreen)
    {
        Follow(shown, MCMove::Forward);
    }
}

auto MCScreenFlow::NoteShown(MCScreenId id) -> void
{
    Follow(id, MCMove::Forward);
}

auto MCScreenFlow::Shown() -> MCScreenId
{
    return StackIds.empty() ? NoScreen : StackIds.back();
}

auto MCScreenFlow::Stack() -> std::span<const MCScreenId>
{
    return StackIds;
}

auto MCScreenFlow::StackText() -> std::string
{
    std::string text;

    for (const MCScreenId id : StackIds)
    {
        text += text.empty() ? std::string() : std::string(StackJoin);
        text += NameOf(id);
    }

    return text.empty() ? std::string("(empty)") : text;
}

auto MCScreenFlow::Clear() -> void
{
    StackIds.clear();
    Landed.clear();
    Queue.clear();
}

auto MCScreenFlow::Waiting() -> bool
{
    return !Queue.empty();
}

auto MCScreenFlow::Unrouted() -> size_t
{
    return UnroutedChanges;
}

auto MCScreenFlow::NoteUnrouted(std::string_view what) -> void
{
    UnroutedChanges++;
    MCFrameLog::Note(std::format("flow: a change of screen outside a change: {}", what));
    SDL_Log("flow: a change of screen outside a change: %.*s", static_cast<int>(what.size()), what.data());
}
