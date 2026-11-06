#include "stdafx.h"
#include "gui/MCGuiTimerManager.h"
#include "gui/MCGuiInput.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFatal.h"
#include "main/MCMissionGlobals.h"

MCGuiTimerManager::MCGuiTimerManager()
{
    RunTimersCallback->SetExec([this] { RunTimers(); });
}

MCGuiTimerManager::~MCGuiTimerManager()
{
    _Timers.clear();

    if (HasGuiSystem())
    {
        GuiSystem().RemoveCallback(*RunTimersCallback);
    }
}

auto MCGuiTimerManager::AddUniqueTimer(std::weak_ptr<MCGuiObject> target, int16_t id, uint32_t interval,
                                       int32_t eventType, int32_t eventData, bool useScenarioTime) -> int32_t
{
    for (const std::shared_ptr<MCGuiTimer>& timer : _Timers)
    {
        if (MCSameObject(timer->Target, target) && timer->Id == id && timer->Interval == interval &&
            timer->EventType == eventType && timer->EventData == eventData)
        {
            return -1;
        }
    }

    return AddTimer(std::move(target), id, interval, eventType, eventData, useScenarioTime);
}

auto MCGuiTimerManager::AddTimer(std::weak_ptr<MCGuiObject> target, int16_t id, uint32_t interval, int32_t eventType,
                                 int32_t eventData, bool useScenarioTime) -> int32_t
{
    SDL_assert(!target.expired());

    if (_Timers.empty())
    {
        GuiSystem().AddCallback(RunTimersCallback);
    }

    auto timer = std::make_shared<MCGuiTimer>();
    timer->Target = std::move(target);
    timer->Id = id;
    timer->Interval = interval;
    timer->LastTime = useScenarioTime
                          ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<double>(ScenarioTime) * 1000.0))
                          : MCPort::Milliseconds();
    timer->EventType = eventType;
    timer->EventData = eventData;
    timer->UseScenarioTime = useScenarioTime;
    _Timers.push_back(std::move(timer));
    return 0;
}

auto MCGuiTimerManager::Erase(size_t index) -> void
{
    _Timers.erase(_Timers.begin() + static_cast<ptrdiff_t>(index));

    if (_Timers.empty())
    {
        GuiSystem().RemoveCallback(*RunTimersCallback);
    }
}

auto MCGuiTimerManager::RemoveTimersOf(const std::weak_ptr<MCGuiObject>& target, int32_t id) -> void
{
    if (id != -1)
    {
        const auto found = std::ranges::find_if(_Timers, [&](const std::shared_ptr<MCGuiTimer>& timer)
                                                { return timer->Id == id && MCSameObject(timer->Target, target); });

        if (found == _Timers.end())
        {
            return;
        }

        if (PutOff(**found))
        {
            _TimersToWhack.push_back({target, true, id});
            return;
        }

        Erase(static_cast<size_t>(found - _Timers.begin()));
        return;
    }

    for (size_t i = 0; i < _Timers.size();)
    {
        const MCGuiTimer& timer = *_Timers[i];

        if (!MCSameObject(timer.Target, target))
        {
            i++;
            continue;
        }

        if (PutOff(timer))
        {
            // Put off once per matching timer.
            _TimersToWhack.push_back({target, true, -1});
            i++;
            continue;
        }

        Erase(i);
    }
}

auto MCGuiTimerManager::RemoveTimers(const MCGuiObject& target) -> void
{
    RemoveTimersOf(MCWeakSelf(target), -1);
}

auto MCGuiTimerManager::RemoveTimer(const MCGuiObject& target, int16_t id) -> void
{
    RemoveTimersOf(MCWeakSelf(target), id);
}

auto MCGuiTimerManager::HandTimersOver(const MCGuiObject& from, const std::weak_ptr<MCGuiObject>& to) -> void
{
    const auto source = MCWeakSelf(from);

    for (const std::shared_ptr<MCGuiTimer>& timer : _Timers)
    {
        if (MCSameObject(timer->Target, source))
        {
            timer->Target = to;
        }
    }
}

auto MCGuiTimerManager::RemoveTimer(int32_t index) -> void
{
    if (index < 0 || index >= NumTimers())
    {
        return;
    }

    if (PutOff(*_Timers[static_cast<size_t>(index)]))
    {
        _TimersToWhack.push_back({{}, false, index});
        return;
    }

    Erase(static_cast<size_t>(index));
}

auto MCGuiTimerManager::GetTimer(int32_t index) -> std::shared_ptr<MCGuiTimer>
{
    return index >= 0 && index < NumTimers() ? _Timers[static_cast<size_t>(index)] : nullptr;
}

auto MCGuiTimerManager::GetTimer(const MCGuiObject& target, int16_t id) -> std::shared_ptr<MCGuiTimer>
{
    const auto found = std::ranges::find_if(_Timers, [&](const std::shared_ptr<MCGuiTimer>& timer)
                                            { return timer->Id == id && MCRefersTo(timer->Target, target); });
    return found != _Timers.end() ? *found : nullptr;
}

auto MCGuiTimerManager::LockTimersExcept(std::weak_ptr<MCGuiTimer> running) -> void
{
    _Locked = true;
    _LockedExcept = std::move(running);
}

auto MCGuiTimerManager::UnlockTimers() -> void
{
    _Locked = false;

    while (!_TimersToWhack.empty())
    {
        const TimerToWhack whack = _TimersToWhack.back();
        _TimersToWhack.pop_back();
        MCMouseThreadLock lock;

        if (whack.ByObject)
        {
            RemoveTimersOf(whack.Target, whack.Id);
        }
        else if (whack.Id != -1)
        {
            RemoveTimer(whack.Id);
        }
        else
        {
            Fatal(static_cast<int32_t>(_TimersToWhack.size()) + 1, " Illegal timersToWhack structure!");
        }
    }
}

auto MCGuiTimerManager::RunTimers() -> void
{
    const uint32_t now = MCPort::Milliseconds();
    int32_t count = NumTimers();

    // The index runs on past a timer removed by its own handler, so the timer after it waits for the next frame.
    for (int32_t i = 0; i < NumTimers(); i++)
    {
        const std::shared_ptr<MCGuiTimer> timer = GetTimer(i);
        const uint32_t due = timer->LastTime + timer->Interval;

        if (timer->UseScenarioTime)
        {
            if (!(static_cast<double>(due) < static_cast<double>(ScenarioTime) * 1000.0))
            {
                continue;
            }
        }
        else if (now <= due)
        {
            continue;
        }

        const tagPOINT cursor = GetMessageCursorLoc();
        MCGuiEvent event;
        event.X = cursor.x;
        event.Y = cursor.y;
        const bool oneShot = timer->EventType != 0;
        event.Data = oneShot ? timer->EventData : timer->Id;
        event.Type = oneShot ? timer->EventType : MCGuiEventType::Timer;
        LockTimersExcept(timer);

        // Destroying an object removes its timers, so the target is there.
        if (const std::shared_ptr<MCGuiObject> target = timer->Target.lock(); target != nullptr)
        {
            target->HandleEvent(event);
        }

        UnlockTimers();
        const int32_t numTimers = NumTimers();

        if (oneShot)
        {
            // A one-shot event: sent, then the timer goes, unless the handler changed the timers.
            if (count == numTimers)
            {
                MCMouseThreadLock lock;
                RemoveTimer(i);
            }
            else
            {
                count = numTimers;
            }

            continue;
        }

        // The time is set unless exactly one timer went (taken to be this one). Faithful: when the handler removed a
        // timer other than this one, this one's time is set too. The original also wrote the time into this timer
        // when the handler had removed it along with others; that write into the freed timer is skipped.
        const bool alive = std::ranges::find(_Timers, timer) != _Timers.end();

        if (count - 1 != numTimers && alive)
        {
            timer->LastTime = now;
        }

        count = numTimers;
    }
}
