// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2026 LinXing0926. All rights reserved.
 */

 #pragma once

#include <android/log.h>
#include <link.h>

#include <cstddef>
#include <cstdint>

#define ASX_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "libASX", __VA_ARGS__)
#define ASX_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "libASX", __VA_ARGS__)
#define ASX_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "libASX", __VA_ARGS__)

namespace asx {

struct ModuleInfo {
    uintptr_t base = 0;
    uintptr_t end = 0;
    const ElfW(Phdr)* phdr = nullptr;
    size_t phnum = 0;
    char path[512] = {};
    bool found = false;
};

struct WordPatch {
    const char* name = nullptr;
    uintptr_t address = 0;
    uint32_t expected = 0;
    uint32_t replacement = 0;
    uint32_t original = 0;
};

class PatchTransaction {
public:
    static constexpr size_t kMaximumPatches = 64;

    bool add(const char* name, uintptr_t address, uint32_t expected, uint32_t replacement);
    bool preflight();
    bool commit();
    bool rollback(size_t applied_count);
    size_t size() const { return count_; }

private:
    WordPatch patches_[kMaximumPatches] = {};
    size_t count_ = 0;
    bool preflight_ok_ = false;
};

extern ModuleInfo g_ue4;

bool refresh_ue4_module();
uintptr_t ue4_addr(uintptr_t offset);
bool is_readable_range(uintptr_t address, size_t length);
bool safe_read_u32(uintptr_t address, uint32_t* out);
bool safe_read_u64(uintptr_t address, uint64_t* out);
bool safe_read_ptr(uintptr_t address, uintptr_t* out);
bool safe_write_u64(uintptr_t address, uint64_t value);
bool write_u32(uintptr_t address, uint32_t word);
size_t page_size();
uintptr_t page_floor(uintptr_t value);
uintptr_t page_ceil(uintptr_t value);
void clear_code_cache(uintptr_t address, size_t length);
void* allocate_near(uintptr_t target, size_t length);
void relay_pool_reset();

bool encode_b(uintptr_t source, uintptr_t target, uint32_t* out);
bool encode_bl(uintptr_t source, uintptr_t target, uint32_t* out);
bool decode_bl_target(uintptr_t source, uint32_t instruction, uintptr_t* out);
void write_absolute_branch_veneer(uintptr_t address, uintptr_t target);

bool add_skip_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected);
bool add_instruction_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected,
    uint32_t instruction);
bool add_branch_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected,
    uintptr_t target);
bool add_tbnz_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected_cbnz,
    uint8_t register_index,
    uint8_t bit_index);
bool add_call_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected,
    uintptr_t callback);
bool add_entry_relay(
    PatchTransaction* transaction,
    const char* name,
    uintptr_t source,
    uint32_t expected,
    uintptr_t callback,
    uintptr_t* trampoline_out);

bool postlogin_repair_prepare(PatchTransaction* transaction);
bool host_mode_repair_prepare(PatchTransaction* transaction);
bool playerdata_repair_prepare(PatchTransaction* transaction);
bool tribe_repair_prepare(PatchTransaction* transaction);
bool tutorial_repair_prepare(PatchTransaction* transaction);
bool actor_lifetime_repair_prepare(PatchTransaction* transaction);

}  // namespace asx
