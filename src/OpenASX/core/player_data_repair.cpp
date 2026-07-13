// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2026 LinXing0926. All rights reserved.
 */

#include "declarations_shared.hpp"

#include <cstdint>

namespace asx {

namespace {

constexpr uintptr_t kStoredDecodeGate = 0x7fb12a4;
constexpr uintptr_t kFileDecodeGate = 0x7fb1a3c;
constexpr uintptr_t kStoredDecryptCall = 0x7fb12d8;
constexpr uintptr_t kFileDecryptCall = 0x7fb1a70;
constexpr uintptr_t kDecryptFunction = 0x87583d4;

constexpr uint32_t kExpectedDecodeGate = 0x350001c0;
constexpr uint32_t kExpectedStoredDecryptCall = 0x941e9c3f;
constexpr uint32_t kExpectedFileDecryptCall = 0x941e9a59;
constexpr uint32_t kExpectedDecryptBranch = 0x17fffbfb;
constexpr uint32_t kUObjectRecordCountLimit = 0x000f4241;

using DecryptFunction = void (*)(uint8_t*, uint32_t, const char*);
DecryptFunction g_original_decrypt = nullptr;

bool looks_like_uobject_record_stream(const uint8_t* payload, uint32_t length) {
    uint32_t first_word = 0;
    return payload != nullptr && length >= sizeof(first_word) &&
        safe_read_u32(reinterpret_cast<uintptr_t>(payload), &first_word) &&
        first_word > 0 && first_word < kUObjectRecordCountLimit;
}

}  // namespace

extern "C" __attribute__((visibility("hidden"))) void ASX_PlayerDataDecryptGate(
    uint8_t* payload,
    uint32_t length,
    const char* key) {
    if (looks_like_uobject_record_stream(payload, length)) {
        return;
    }
    if (g_original_decrypt != nullptr) {
        g_original_decrypt(payload, length, key);
    }
}

bool playerdata_repair_prepare(PatchTransaction* transaction) {
    if (transaction == nullptr) {
        return false;
    }
    uint32_t decrypt_identity = 0;
    if (!safe_read_u32(ue4_addr(kDecryptFunction), &decrypt_identity) ||
        decrypt_identity != kExpectedDecryptBranch) {
        ASX_LOGE("PlayerData FAES::DecryptData identity mismatch");
        return false;
    }
    g_original_decrypt = reinterpret_cast<DecryptFunction>(ue4_addr(kDecryptFunction));

    return add_tbnz_relay(
               transaction,
               "PlayerData stored NetMode decode gate",
               ue4_addr(kStoredDecodeGate),
               kExpectedDecodeGate,
               0,
               0) &&
        add_tbnz_relay(
               transaction,
               "PlayerData file NetMode decode gate",
               ue4_addr(kFileDecodeGate),
               kExpectedDecodeGate,
               0,
               0) &&
        add_call_relay(
               transaction,
               "PlayerData stored native ASX header gate",
               ue4_addr(kStoredDecryptCall),
               kExpectedStoredDecryptCall,
               reinterpret_cast<uintptr_t>(&ASX_PlayerDataDecryptGate)) &&
        add_call_relay(
               transaction,
               "PlayerData file native ASX header gate",
               ue4_addr(kFileDecryptCall),
               kExpectedFileDecryptCall,
               reinterpret_cast<uintptr_t>(&ASX_PlayerDataDecryptGate));
}

}  // namespace asx
