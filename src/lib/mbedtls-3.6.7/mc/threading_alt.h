/* Port: the mutex Mbed TLS locks with MBEDTLS_THREADING_ALT (mc_mbedtls_config.h): a std::mutex the game makes
 * (MCCore's platform/MCTlsThreading.cpp), none before it's made and after it's freed. */
#ifndef MC_MBEDTLS_THREADING_ALT_H
#define MC_MBEDTLS_THREADING_ALT_H

typedef struct mbedtls_threading_mutex_t
{
    void *mutex;
} mbedtls_threading_mutex_t;

#endif
