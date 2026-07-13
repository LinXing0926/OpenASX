#include "declarations_shared.hpp"

#include <cstring>

namespace asx {

namespace {

constexpr size_t kRelayPageBytes = 64;

bool add_relay_patch(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected,
    uintptr_t veneer,
    bool link) {
    uint32_t jump = 0;
    const bool encoded = link
        ? encode_bl(source, veneer, &jump)
        : encode_b(source, veneer, &jump);
    if (!encoded) {
        return false;
    }
    return transaction->add(name, source, expected, jump);  // ASX-owned veneer only.
}

bool write_relative_branch(uintptr_t address, uintptr_t target) {
    uint32_t branch = 0;
    if (!encode_b(address, target, &branch)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(address), &branch, sizeof(branch));
    return true;
}

bool decode_compare_branch_target(
    uintptr_t source,
    uint32_t instruction,
    uintptr_t* target_out) {
    if (target_out == nullptr || (instruction & 0x7e000000U) != 0x34000000U) {
        return false;
    }
    int32_t immediate = static_cast<int32_t>((instruction >> 5U) & 0x0007ffffU);
    if ((immediate & 0x00040000) != 0) {
        immediate |= static_cast<int32_t>(0xfff80000U);
    }
    *target_out = static_cast<uintptr_t>(
        static_cast<int64_t>(source) + static_cast<int64_t>(immediate) * 4);
    return true;
}

bool encode_tbnz_local(
    uintptr_t source,
    uintptr_t target,
    uint8_t register_index,
    uint8_t bit_index,
    uint32_t* out) {
    if (out == nullptr || register_index > 31 || bit_index > 31) {
        return false;
    }
    const int64_t delta = static_cast<int64_t>(target) - static_cast<int64_t>(source);
    if ((delta & 3) != 0) {
        return false;
    }
    const int64_t immediate = delta / 4;
    if (immediate < -(1LL << 13) || immediate >= (1LL << 13)) {
        return false;
    }
    *out = 0x37000000U |
        (static_cast<uint32_t>(bit_index) << 19U) |
        ((static_cast<uint32_t>(immediate) & 0x00003fffU) << 5U) |
        register_index;
    return true;
}

uintptr_t allocate_relay(uintptr_t source) {
    return reinterpret_cast<uintptr_t>(allocate_near(source, kRelayPageBytes));
}

}  // namespace

bool add_skip_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected) {
    const uintptr_t veneer = allocate_relay(source);
    if (transaction == nullptr || veneer == 0 ||
        !write_relative_branch(veneer, source + sizeof(uint32_t))) {
        return false;
    }
    clear_code_cache(veneer, sizeof(uint32_t));
    return add_relay_patch(transaction, name, source, expected, veneer, false);
}

bool add_instruction_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected,
    uint32_t instruction) {
    const uintptr_t veneer = allocate_relay(source);
    if (transaction == nullptr || veneer == 0) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(veneer), &instruction, sizeof(instruction));
    if (!write_relative_branch(veneer + 4, source + 4)) {
        return false;
    }
    clear_code_cache(veneer, 8);
    return add_relay_patch(transaction, name, source, expected, veneer, false);
}

bool add_branch_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected,
    uintptr_t target) {
    const uintptr_t veneer = allocate_relay(source);
    if (transaction == nullptr || veneer == 0 || !write_relative_branch(veneer, target)) {
        return false;
    }
    clear_code_cache(veneer, sizeof(uint32_t));
    return add_relay_patch(transaction, name, source, expected, veneer, false);
}

bool add_tbnz_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected_cbnz,
    uint8_t register_index,
    uint8_t bit_index) {
    uintptr_t taken_target = 0;
    const uintptr_t veneer = allocate_relay(source);
    uint32_t condition = 0;
    if (transaction == nullptr || veneer == 0 ||
        !decode_compare_branch_target(source, expected_cbnz, &taken_target) ||
        !encode_tbnz_local(veneer, veneer + 8, register_index, bit_index, &condition)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(veneer), &condition, sizeof(condition));
    if (!write_relative_branch(veneer + 4, source + 4) ||
        !write_relative_branch(veneer + 8, taken_target)) {
        return false;
    }
    clear_code_cache(veneer, 12);
    return add_relay_patch(transaction, name, source, expected_cbnz, veneer, false);
}

bool add_call_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected,
    uintptr_t callback) {
    const uintptr_t veneer = allocate_relay(source);
    if (transaction == nullptr || veneer == 0) {
        return false;
    }
    write_absolute_branch_veneer(veneer, callback);
    return add_relay_patch(transaction, name, source, expected, veneer, true);
}

bool add_entry_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected,
    uintptr_t callback,
    uintptr_t* trampoline_out) {
    const uintptr_t veneer = allocate_relay(source);
    if (transaction == nullptr || veneer == 0 || trampoline_out == nullptr) {
        return false;
    }
    const uintptr_t trampoline = veneer + 0x20;
    write_absolute_branch_veneer(veneer, callback);
    std::memcpy(reinterpret_cast<void*>(trampoline), &expected, sizeof(expected));
    if (!write_relative_branch(trampoline + 4, source + 4)) {
        return false;
    }
    clear_code_cache(trampoline, 8);
    *trampoline_out = trampoline;
    return add_relay_patch(transaction, name, source, expected, veneer, false);
}

}  // namespace asx
