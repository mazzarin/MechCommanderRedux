/* Port: the game's additions to Mbed TLS's default configuration (include/mbedtls/mbedtls_config.h), read after it
 * (MBEDTLS_USER_CONFIG_FILE, set for everything that includes Mbed TLS's headers, so every unit sees one layout).
 *
 * Threading: libdatachannel calls Mbed TLS from its own threads (a DTLS link per player, the service's WebSocket's TLS
 * and PSA's shared key store), so Mbed TLS locks its shared state, with the game's mutexes on every platform
 * (threading_alt.h; MCCore's platform/MCTlsThreading.cpp hands them over before any other Mbed TLS call). */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT

/* DTLS-SRTP: libdatachannel's DTLS offers its profiles whatever it carries (impl/dtlstransport.cpp), as WebRTC's
 * peers expect; with no audio or video nothing uses the keys. */
#define MBEDTLS_SSL_DTLS_SRTP
