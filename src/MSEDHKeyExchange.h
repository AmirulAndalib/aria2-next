/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 The aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */

#ifndef D_MSE_DH_KEY_EXCHANGE_H
#define D_MSE_DH_KEY_EXCHANGE_H

#include <array>
#include <cstddef>

namespace aria2 {

constexpr size_t MSE_DH_PUBLIC_KEY_LENGTH = 96;
constexpr size_t MSE_DH_PRIVATE_KEY_LENGTH = 20;

constexpr char MSE_DH_PRIME_HEX[] =
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B"
    "139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485"
    "B576625E7EC6F44C42E9A63A36210000000000090563";

using MSEDHPublicKey =
    std::array<unsigned char, MSE_DH_PUBLIC_KEY_LENGTH>;
using MSEDHPrivateKey =
    std::array<unsigned char, MSE_DH_PRIVATE_KEY_LENGTH>;

} // namespace aria2

#endif // D_MSE_DH_KEY_EXCHANGE_H
