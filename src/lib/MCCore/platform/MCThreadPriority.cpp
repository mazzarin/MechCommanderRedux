// Built without the precompiled header on Windows (MCCore.vcxproj): windows.h must not meet the game's Win32
// stand-ins.

#include "platform/MCThreadPriority.h"

#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread/qos.h>
#endif

MCThreadPriority MCCurrentThreadPriority()
{
#ifdef _WIN32
    return {GetThreadPriority(GetCurrentThread())};
#elif defined(__APPLE__)
    return {static_cast<int32_t>(qos_class_self())};
#else
    return {};
#endif
}

bool MCSetCurrentThreadPriority(MCThreadPriority like, bool below)
{
#ifdef _WIN32
    // The game's thread's priority couldn't be read: this one's is left as it is.
    if (like.Value == THREAD_PRIORITY_ERROR_RETURN)
    {
        return false;
    }

    // One down, never past the lowest (idle is a class of its own); time-critical's one down is the highest.
    const int priority = !below ? like.Value
                         : like.Value >= THREAD_PRIORITY_TIME_CRITICAL
                             ? THREAD_PRIORITY_HIGHEST
                             : std::max(like.Value - 1, THREAD_PRIORITY_LOWEST);
    return SetThreadPriority(GetCurrentThread(), priority) != 0;
#elif defined(__APPLE__)
    const auto likeClass = static_cast<qos_class_t>(like.Value);

    // The classes' numbers rise with their priority (user-interactive's above default's, utility's below).
    if (likeClass == QOS_CLASS_UNSPECIFIED || (below && likeClass <= QOS_CLASS_DEFAULT))
    {
        return true;
    }

    return pthread_set_qos_class_self_np(below ? QOS_CLASS_DEFAULT : likeClass, 0) == 0;
#else
    (void)like;
    (void)below;
    return true;
#endif
}
