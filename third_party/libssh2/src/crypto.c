/* Copyright (C) Viktor Szakats
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define LIBSSH2_CRYPTO_C
#include "libssh2_priv.h"

#if defined(LIBSSH2_OPENSSL)
#include "openssl.c"
#elif defined(LIBSSH2_WINCNG)
#include "wincng.c"
#endif
