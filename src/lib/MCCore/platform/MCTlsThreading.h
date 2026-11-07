#pragma once

// Port: Mbed TLS's locks. libdatachannel calls Mbed TLS from its own threads (a DTLS link per player, the WebSockets'
// TLS, PSA's shared key store), so Mbed TLS is built to lock its shared state with mutexes the game hands it
// (MBEDTLS_THREADING_ALT, src/lib/mbedtls-3.6.7/mc). Until then every lock of its fails, so nothing of it runs
// unlocked.

/// <summary>
/// Hands Mbed TLS the game's mutexes, the first time in a run (later calls do nothing). Mbed TLS asks for it before any
/// other call of its, from the thread that starts it: the ICE links call it before they start libdatachannel. Its
/// global mutexes are never freed (mbedtls_threading_free_alt): libdatachannel's threads may use them until the
/// process ends.
/// </summary>
void MCStartTlsThreading();
