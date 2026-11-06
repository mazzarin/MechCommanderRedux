#pragma once

#include "gui/MCGuiCallback.h"

class MCGuiObject;

/// <summary>A running GUI timer: sends a timer event (or a given event, once) to an object every interval.</summary>
/// <remarks>Original source: <c>gui\asystem.cpp</c> (<c>aTimer</c>).</remarks>
struct MCGuiTimer
{
    /// <summary>The interval in milliseconds.</summary>
    uint32_t Interval = 0;
    /// <summary>When it last fired (ms, or scenario ms for a scenario-time timer).</summary>
    uint32_t LastTime = 0;
    /// <summary>The object it sends its events to (destroying the object removes its timers).</summary>
    std::weak_ptr<MCGuiObject> Target;
    /// <summary>The id passed back in the timer event's <see cref="MCGuiEvent::Data"/>.</summary>
    int16_t Id = 0;
    /// <summary>The event type to send instead of a timer event (then the timer fires once), or 0.</summary>
    int32_t EventType = 0;
    /// <summary>The <see cref="MCGuiEvent::Data"/> of that event.</summary>
    int32_t EventData = 0;
    /// <summary>Whether it counts in scenario time instead of real time.</summary>
    bool UseScenarioTime = false;
};

/// <summary>
/// The GUI timers, run by a frame callback (<see cref="RunTimers"/>) while any exist. Removing a timer while one's
/// event is being handled is put off until the handler returns, unless it is the running timer.
/// </summary>
/// <remarks>Original source: <c>gui\asystem.cpp</c> (<c>aTimerManager</c>).</remarks>
class MCGuiTimerManager
{
public:
    /// <summary>Makes the frame callback that runs the timers.</summary>
    MCGuiTimerManager();
    /// <summary>Frees the timers and takes the frame callback off the GUI system.</summary>
    ~MCGuiTimerManager();
    MCGuiTimerManager(const MCGuiTimerManager&) = delete;
    MCGuiTimerManager& operator=(const MCGuiTimerManager&) = delete;

    /// <summary>Adds a timer unless one with the same object, id, interval and event exists.</summary>
    /// <returns>0, or -1 when there is one.</returns>
    int32_t AddUniqueTimer(std::weak_ptr<MCGuiObject> target, int16_t id, uint32_t interval, int32_t eventType,
                           int32_t eventData, bool useScenarioTime);
    /// <summary>Adds a timer; the first one adds the frame callback to the GUI system.</summary>
    /// <returns>0.</returns>
    int32_t AddTimer(std::weak_ptr<MCGuiObject> target, int16_t id, uint32_t interval, int32_t eventType,
                     int32_t eventData, bool useScenarioTime);
    /// <summary>Removes every timer of <paramref name="target"/> (also while it is being destroyed).</summary>
    void RemoveTimers(const MCGuiObject& target);
    /// <summary>Removes <paramref name="target"/>'s timer <paramref name="id"/>.</summary>
    void RemoveTimer(const MCGuiObject& target, int16_t id);
    /// <summary>Removes timer number <paramref name="index"/>.</summary>
    void RemoveTimer(int32_t index);
    /// <summary>
    /// Port: hands <paramref name="from"/>'s timers to <paramref name="to"/> as they run (the same schedule and the same
    /// place in the list), for an object that takes over from another (a view of shared state on the next screen).
    /// </summary>
    void HandTimersOver(const MCGuiObject& from, const std::weak_ptr<MCGuiObject>& to);
    /// <summary>Timer number <paramref name="index"/>, or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCGuiTimer> GetTimer(int32_t index);
    /// <summary><paramref name="target"/>'s timer <paramref name="id"/>, or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCGuiTimer> GetTimer(const MCGuiObject& target, int16_t id);
    /// <summary>How many timers run.</summary>
    int32_t NumTimers() const { return static_cast<int32_t>(_Timers.size()); }

    /// <summary>
    /// Fires the due timers: a timer event with the timer's id, or the timer's own event (after which the timer goes,
    /// unless the handler changed the timers).
    /// </summary>
    void RunTimers();

    /// <summary>The frame callback that runs the timers.</summary>
    std::shared_ptr<MCGuiCallback> RunTimersCallback = std::make_shared<MCGuiCallback>();

private:
    /// <summary>
    /// A put-off removal: an object's timer <see cref="Id"/>, an object's timers (<see cref="Id"/> -1), or, without
    /// <see cref="ByObject"/>, timer number <see cref="Id"/>.
    /// </summary>
    struct TimerToWhack
    {
        // Not weak_ptr<const MCGuiObject>: converting to it would empty the reference of an object being destroyed
        // (see MCWeakSelf).
        std::weak_ptr<MCGuiObject> Target;
        bool ByObject = false;
        int32_t Id = 0;
    };

    /// <summary>Puts removals off while <paramref name="running"/>'s event is handled (it may remove itself).</summary>
    void LockTimersExcept(std::weak_ptr<MCGuiTimer> running);
    /// <summary>Does the put-off removals, last first.</summary>
    void UnlockTimers();
    /// <summary>Whether removing <paramref name="timer"/> is put off now.</summary>
    bool PutOff(const MCGuiTimer& timer) const { return _Locked && &timer != _LockedExcept.lock().get(); }
    /// <summary>Removes timer number <paramref name="index"/> now; the last one takes the frame callback away.</summary>
    void Erase(size_t index);
    /// <summary>Removes the timers of the object <paramref name="target"/> refers to (id -1: all of them).</summary>
    void RemoveTimersOf(const std::weak_ptr<MCGuiObject>& target, int32_t id);

    std::vector<std::shared_ptr<MCGuiTimer>> _Timers;
    std::vector<TimerToWhack> _TimersToWhack;
    bool _Locked = false;
    /// <summary>The timer whose event is being handled while locked.</summary>
    std::weak_ptr<MCGuiTimer> _LockedExcept;
};
