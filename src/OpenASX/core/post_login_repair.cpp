// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2026 LinXing0926. All rights reserved.
 */

#include "declarations_shared.hpp"

namespace asx {

namespace {

constexpr uintptr_t kPostLoginTokenSetup = 0x7faa4b4;
constexpr uintptr_t kPostLoginNativeContinuation = 0x7faa4d0;
constexpr uint32_t kExpectedTokenSetup = 0xaa1303e0;

}  // namespace

bool postlogin_repair_prepare(PatchTransaction* transaction) {
    if (transaction == nullptr) {
        return false;
    }
    const uintptr_t source = ue4_addr(kPostLoginTokenSetup);
    return add_branch_relay(
        transaction,
        "PostLogin native continuation",
        source,
        kExpectedTokenSetup,
        ue4_addr(kPostLoginNativeContinuation));
}

}  // namespace asx
