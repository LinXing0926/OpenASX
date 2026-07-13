// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2026 LinXing0926. All rights reserved.
 */

#include "declarations_shared.hpp"

#include <cstring>

namespace asx {

namespace {

bool encode_branch(uint32_t opcode, uintptr_t source, uintptr_t target, uint32_t* out) {
    if (out == nullptr) {
        return false;
    }
    const int64_t delta = static_cast<int64_t>(target) - static_cast<int64_t>(source);
    if ((delta & 3) != 0) {
        return false;
    }
    const int64_t immediate = delta / 4;
    if (immediate < -(1LL << 25) || immediate >= (1LL << 25)) {
        return false;
    }
    *out = opcode | (static_cast<uint32_t>(immediate) & 0x03ffffffU);
    return true;
}

}  // namespace

bool encode_b(uintptr_t source, uintptr_t target, uint32_t* out) {
    return encode_branch(0x14000000U, source, target, out);
}

bool encode_bl(uintptr_t source, uintptr_t target, uint32_t* out) {
    return encode_branch(0x94000000U, source, target, out);
}

bool decode_bl_target(uintptr_t source, uint32_t instruction, uintptr_t* out) {
    if (out == nullptr || (instruction & 0xfc000000U) != 0x94000000U) {
        return false;
    }
    int32_t immediate = static_cast<int32_t>(instruction & 0x03ffffffU);
    if ((immediate & 0x02000000) != 0) {
        immediate |= static_cast<int32_t>(0xfc000000U);
    }
    *out = static_cast<uintptr_t>(
        static_cast<int64_t>(source) + static_cast<int64_t>(immediate) * 4);
    return true;
}

void write_absolute_branch_veneer(uintptr_t address, uintptr_t target) {
    constexpr uint32_t kLoadX16Literal = 0x58000050U;
    constexpr uint32_t kBranchX16 = 0xd61f0200U;
    std::memcpy(reinterpret_cast<void*>(address), &kLoadX16Literal, sizeof(kLoadX16Literal));
    std::memcpy(reinterpret_cast<void*>(address + 4), &kBranchX16, sizeof(kBranchX16));
    std::memcpy(reinterpret_cast<void*>(address + 8), &target, sizeof(target));
    clear_code_cache(address, 16);
}

}  // namespace asx
