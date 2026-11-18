#include "stdafx.h"
#include "lib/MCFirstUse.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

namespace
{
    /// <summary>The guard's state: whether it counts, whose thread, the frame, and the uses noted.</summary>
    struct MCGuard
    {
        std::atomic<bool> On = false;
        std::atomic<std::thread::id> Thread;
        std::atomic<int64_t> Frame = 0;
        std::mutex Lock;
        std::vector<MCFirstUse> Uses;
        size_t Dropped = 0;
    };

    MCGuard& Guard()
    {
        static MCGuard guard;
        return guard;
    }

    /// <summary>How many uses the calling thread is noting around its current code (only the outermost counts).</summary>
    thread_local int32_t NotingDepth = 0;
    /// <summary>The innermost context the calling thread is in (MCFirstUseContext), or none.</summary>
    thread_local MCFirstUseContext* InnerContext = nullptr;
}

void MCFirstUseContext::Enter()
{
    _Outer = InnerContext;
    InnerContext = this;
}

MCFirstUseContext::~MCFirstUseContext()
{
    InnerContext = _Outer;
}

std::string MCFirstUseContext::Names()
{
    std::string names;

    for (const MCFirstUseContext* context = InnerContext; context != nullptr; context = context->_Outer)
    {
        names.insert(0, context->_Call(context->_Name) + ": ");
    }

    return names;
}

void MCFirstUses::PlayStarted()
{
    MCGuard& guard = Guard();
    guard.Thread.store(std::this_thread::get_id());
    guard.Frame.store(0);
    guard.On.store(true);
}

void MCFirstUses::PlayEnded()
{
    Guard().On.store(false);
}

bool MCFirstUses::Counting()
{
    const MCGuard& guard = Guard();
    return guard.On.load(std::memory_order_relaxed) && guard.Thread.load() == std::this_thread::get_id();
}

void MCFirstUses::NextFrame()
{
    Guard().Frame.fetch_add(1, std::memory_order_relaxed);
}

void MCFirstUses::Note(MCFirstUse use)
{
    MCGuard& guard = Guard();
    use.Frame = guard.Frame.load(std::memory_order_relaxed);
    const std::scoped_lock lock(guard.Lock);

    if (guard.Uses.size() < MostKept)
    {
        guard.Uses.push_back(std::move(use));
    }
    else
    {
        ++guard.Dropped;
    }
}

std::vector<MCFirstUse> MCFirstUses::Take()
{
    MCGuard& guard = Guard();
    const std::scoped_lock lock(guard.Lock);
    guard.Dropped = 0;
    return std::exchange(guard.Uses, {});
}

size_t MCFirstUses::Dropped()
{
    MCGuard& guard = Guard();
    const std::scoped_lock lock(guard.Lock);
    return guard.Dropped;
}

std::string_view MCFirstUses::KindName(MCFirstUseKind kind)
{
    switch (kind)
    {
        case MCFirstUseKind::Open:
            return "open";
        case MCFirstUseKind::Read:
            return "read";
        case MCFirstUseKind::Unpack:
            return "unpack";
        case MCFirstUseKind::Sound:
            return "sound";
        case MCFirstUseKind::Wait:
            return "wait";
        case MCFirstUseKind::Movie:
            return "movie";
    }

    return "unknown";
}

std::string MCFirstUses::FileName(const std::filesystem::path& path)
{
    const std::u8string name = (path.parent_path().filename() / path.filename()).generic_u8string();
    return std::string(name.begin(), name.end());
}

MCFirstUseScope::MCFirstUseScope(MCFirstUseKind kind) : Kind(kind)
{
    if (NotingDepth++ == 0 && MCFirstUses::Counting())
    {
        _Counting = true;
        _Started = std::chrono::steady_clock::now();
    }
}

MCFirstUseScope::~MCFirstUseScope()
{
    --NotingDepth;

    if (_Counting)
    {
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _Started).count();
        MCFirstUses::Note({.Kind = Kind, .What = MCFirstUseContext::Names() + What, .Ms = ms, .Bytes = Bytes});
    }
}
