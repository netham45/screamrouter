/* custom_threading_alt.h - Custom dummy for MBEDTLS_THREADING_ALT */
/* Defines mbedtls_threading_mutex_t as void* for compatibility with bctoolbox. */
#ifndef CUSTOM_THREADING_ALT_H
#define CUSTOM_THREADING_ALT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief   The generic mutex type (pointer version).
 *
 * \note    This type is defined as void* to be directly compatible with
 *          bctoolbox's usage, where it effectively treats
 *          mbedtls_threading_mutex_t* as void**. This allows bctoolbox
 *          to store its std::mutex* by dereferencing the
 *          mbedtls_threading_mutex_t* parameter in its callbacks.
 */
typedef void *mbedtls_threading_mutex_t;

#ifdef __cplusplus
}
#endif

#endif /* CUSTOM_THREADING_ALT_H */