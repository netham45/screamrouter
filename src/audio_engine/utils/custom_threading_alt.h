/**
 * @file custom_threading_alt.h
 * @brief Custom dummy definition for MBEDTLS_THREADING_ALT.
 * @details This file defines `mbedtls_threading_mutex_t` as `void*` to ensure
 *          compatibility with the `bctoolbox` library, which expects to be able
 *          to store its own mutex implementation pointer.
 */
#ifndef CUSTOM_THREADING_ALT_H
#define CUSTOM_THREADING_ALT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The generic mutex type, defined as a void pointer for compatibility.
 * @note This type is defined as `void*` to be directly compatible with
 *       bctoolbox's usage, where it effectively treats
 *       `mbedtls_threading_mutex_t*` as `void**`. This allows bctoolbox
 *       to store its `std::mutex*` by dereferencing the
 *       `mbedtls_threading_mutex_t*` parameter in its callbacks.
 */
typedef void *mbedtls_threading_mutex_t;

#ifdef __cplusplus
}
#endif

#endif /* CUSTOM_THREADING_ALT_H */