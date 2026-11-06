#pragma once

// Only standard headers: the platforms' transfers (apps/MCRedux/MCHttpGetMac.mm, MCHttpGetWin.cpp) are built without
// the precompiled header.
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

/// <summary>Port-only: what the release's host source (update/MCHttpSource.h) asks of a platform's transfer.</summary>
namespace MCHttpSource
{
    /// <summary>An answer as it arrives: its status once, then its body in pieces.</summary>
    class MCReply
    {
    public:
        virtual ~MCReply() = default;

        /// <summary>The answer's status, and its Content-Range header as sent (empty: none).</summary>
        /// <returns>False to stop.</returns>
        virtual bool Status(int status, std::string_view contentRange) = 0;

        /// <summary>The body's next piece.</summary>
        /// <returns>False to stop.</returns>
        virtual bool Body(std::span<const char> bytes) = 0;
    };

    /// <summary>Why a transfer failed, and whether it's because the host couldn't be reached.</summary>
    struct MCGetError
    {
        /// <summary>
        /// The host couldn't be reached, or the connection to it was lost: no connection to the internet, its name not
        /// found, a connection refused, reset or timed out (the platform's own error says which).
        /// </summary>
        bool Unreachable = false;
        std::string Reason;
    };

    /// <summary>The platform's transfer.</summary>
    class MCGet
    {
    public:
        virtual ~MCGet() = default;

        /// <summary>
        /// GETs <paramref name="url"/>, never from a cache, asking for the bytes from <paramref name="from"/> on
        /// ("Range: bytes=from-") unless it's 0, and tells <paramref name="reply"/> what arrives, on this thread.
        /// </summary>
        /// <returns>Nothing once the answer ended or the reply stopped it; else what failed (no connection, a timeout).</returns>
        virtual std::expected<void, MCGetError> Get(const std::string& url, uint64_t from, MCReply& reply) = 0;

        /// <summary>
        /// From any thread: ends the transfer under way at once, and fails every later one, with "stopped". Here it
        /// does nothing (a transfer that can't be cut short ends at its next bytes, as its reply stops it).
        /// </summary>
        virtual void Cancel() {}
    };
}
