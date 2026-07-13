// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2026 LinXing0926. All rights reserved.
 */

 #include "declarations_shared.hpp"

#include <cstdint>

namespace asx {

namespace {

constexpr uintptr_t kHostTribeLoadArg4True = 0x7fc7ecc;
constexpr uintptr_t kTribeStoredDecodeGate = 0x7fb4428;
constexpr uintptr_t kTribeFileDecodeGate = 0x7fb49bc;
constexpr uintptr_t kApplyClearTribe = 0x7953f54;
constexpr uintptr_t kApplyClearTribeAfterLoadFail = 0x7954000;
constexpr uintptr_t kApplyExtraAffiliationMapCall = 0x795413c;
constexpr uintptr_t kApplyExtraAffiliationScratchCall = 0x7954148;
constexpr uintptr_t kApplyExtraAffiliationLoopGate = 0x7954154;
constexpr uintptr_t kApplyStackTribeCleanupCall = 0x79541f0;
constexpr uintptr_t kApplyPlayerStateTribeCopyCall = 0x79541fc;
constexpr uintptr_t kApplyTargetingTeamComputeCall = 0x7954204;
constexpr uintptr_t kApplyLinkedActorTeamCall = 0x795421c;
constexpr uintptr_t kApplyLinkedActorTeamStore = 0x7954220;
constexpr uintptr_t kAddToTribeNativeSaveCall = 0x80abfb0;

constexpr uint32_t kHostTribeLoadArg4TrueWord = 0x52800024;
constexpr uint32_t kStoredDecodeGateExpected = 0x350001c0;
constexpr uint32_t kFileDecodeGateExpected = 0x35000060;
constexpr uint32_t kClearTribeExpected = 0xb9042e9f;
constexpr uint32_t kExtraMapCallExpected = 0x940016d0;
constexpr uint32_t kExtraScratchCallExpected = 0x940000e3;
constexpr uint32_t kExtraLoopGateExpected = 0x5400048b;
constexpr uintptr_t kExtraLoopResumeDelta = 0x90;
constexpr uint32_t kStackCleanupCallExpected = 0x9400014a;
constexpr uint32_t kPlayerStateTribeCopyCallExpected = 0x94000017;
constexpr uint32_t kTargetingTeamComputeExpected = 0x97fffdc5;
constexpr uint32_t kLoadTribeIdAsTargetingTeam = 0xb9442e80;
constexpr uint32_t kLinkedActorTeamCallExpected = 0xd63f0100;
constexpr uint32_t kClearX0 = 0xaa1f03e0;
constexpr uint32_t kLinkedActorTeamStoreExpected = 0xb9005015;
constexpr uint32_t kNativeSavePlayerDataCallExpected = 0x97e29cdd;

}  // namespace

bool tribe_repair_prepare(PatchTransaction* transaction) {
    if (transaction == nullptr) {
        return false;
    }
    uint32_t arg4_word = 0;
    uint32_t copy_word = 0;
    uint32_t native_save_word = 0;
    if (!safe_read_u32(ue4_addr(kHostTribeLoadArg4True), &arg4_word) ||
        arg4_word != kHostTribeLoadArg4TrueWord ||
        !safe_read_u32(ue4_addr(kApplyPlayerStateTribeCopyCall), &copy_word) ||
        copy_word != kPlayerStateTribeCopyCallExpected ||
        !safe_read_u32(ue4_addr(kAddToTribeNativeSaveCall), &native_save_word) ||
        native_save_word != kNativeSavePlayerDataCallExpected) {
        ASX_LOGE("tribe policy/copy/native-save identity mismatch");
        return false;
    }

    return add_tbnz_relay(
               transaction,
               "Tribe stored NetMode transform gate",
               ue4_addr(kTribeStoredDecodeGate),
               kStoredDecodeGateExpected,
               0,
               0) &&
        add_tbnz_relay(
               transaction,
               "Tribe file NetMode transform gate",
               ue4_addr(kTribeFileDecodeGate),
               kFileDecodeGateExpected,
               0,
               0) &&
        add_skip_relay(
               transaction,
               "ApplyToPlayerState tribe clear guard",
               ue4_addr(kApplyClearTribe),
               kClearTribeExpected) &&
        add_skip_relay(
               transaction,
               "ApplyToPlayerState load-fail tribe clear guard",
               ue4_addr(kApplyClearTribeAfterLoadFail),
               kClearTribeExpected) &&
        add_skip_relay(
               transaction,
               "ApplyToPlayerState extra affiliation map guard",
               ue4_addr(kApplyExtraAffiliationMapCall),
               kExtraMapCallExpected) &&
        add_skip_relay(
               transaction,
               "ApplyToPlayerState extra affiliation scratch guard",
               ue4_addr(kApplyExtraAffiliationScratchCall),
               kExtraScratchCallExpected) &&
        add_branch_relay(
               transaction,
               "ApplyToPlayerState extra affiliation loop guard",
               ue4_addr(kApplyExtraAffiliationLoopGate),
               kExtraLoopGateExpected,
               ue4_addr(kApplyExtraAffiliationLoopGate + kExtraLoopResumeDelta)) &&
        add_skip_relay(
               transaction,
               "ApplyToPlayerState stack tribe cleanup guard",
               ue4_addr(kApplyStackTribeCleanupCall),
               kStackCleanupCallExpected) &&
        add_instruction_relay(
               transaction,
               "ApplyToPlayerState TargetingTeam source",
               ue4_addr(kApplyTargetingTeamComputeCall),
               kTargetingTeamComputeExpected,
               kLoadTribeIdAsTargetingTeam) &&
        add_instruction_relay(
               transaction,
               "ApplyToPlayerState linked actor team call guard",
               ue4_addr(kApplyLinkedActorTeamCall),
               kLinkedActorTeamCallExpected,
               kClearX0) &&
        add_skip_relay(
               transaction,
               "ApplyToPlayerState linked actor team store guard",
               ue4_addr(kApplyLinkedActorTeamStore),
               kLinkedActorTeamStoreExpected);
}

}  // namespace asx
