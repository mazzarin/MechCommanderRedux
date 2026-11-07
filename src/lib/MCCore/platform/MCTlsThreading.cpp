#include "stdafx.h"
#include "platform/MCTlsThreading.h"

#include <mbedtls/threading.h>

namespace
{
    /// <summary>The game's mutex behind one of Mbed TLS's (none when it couldn't be made, or once freed).</summary>
    std::mutex* MutexOf(const mbedtls_threading_mutex_t* mutex)
    {
        return mutex != nullptr ? static_cast<std::mutex*>(mutex->mutex) : nullptr;
    }

    void MutexInit(mbedtls_threading_mutex_t* mutex)
    {
        if (mutex == nullptr)
        {
            return;
        }

        // Without one, its locks fail (as Mbed TLS asks of an init that fails).
        mutex->mutex = new (std::nothrow) std::mutex;

        if (mutex->mutex == nullptr)
        {
            SDL_Log("MCTlsThreading: out of memory making a mutex for Mbed TLS; its locks will fail");
        }
    }

    void MutexFree(mbedtls_threading_mutex_t* mutex)
    {
        if (mutex == nullptr)
        {
            return;
        }

        delete MutexOf(mutex);
        mutex->mutex = nullptr;
    }

    int MutexLock(mbedtls_threading_mutex_t* mutex)
    {
        std::mutex* lock = MutexOf(mutex);

        if (lock == nullptr)
        {
            return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
        }

        // An exception must not cross into Mbed TLS's C.
        try
        {
            lock->lock();
        }
        catch (const std::system_error& error)
        {
            SDL_Log("MCTlsThreading: locking a mutex for Mbed TLS failed: %s", error.what());
            return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
        }

        return 0;
    }

    int MutexUnlock(mbedtls_threading_mutex_t* mutex)
    {
        std::mutex* lock = MutexOf(mutex);

        if (lock == nullptr)
        {
            return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
        }

        lock->unlock();
        return 0;
    }
}

void MCStartTlsThreading()
{
    static std::once_flag started;
    std::call_once(started, [] { mbedtls_threading_set_alt(MutexInit, MutexFree, MutexLock, MutexUnlock); });
}
