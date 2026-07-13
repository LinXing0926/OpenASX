// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2026 LinXing0926. All rights reserved.
 */

 #include "declarations_shared.hpp"

namespace asx {

namespace {

constexpr uintptr_t kDoMobileControls = 0x8045a08;
constexpr uintptr_t kLocalPlayerControllerFlag = 0x740;
constexpr uint32_t kExpectedPrologue = 0xd10183ff;

using DoMobileControlsFunction = void (*)(void*, float);
DoMobileControlsFunction g_original_do_mobile_controls = nullptr;

}  // namespace

extern "C" __attribute__((visibility("hidden"))) void ASX_DoMobileControlsGate(
    void* player_controller,
    float delta_seconds) {
    if (player_controller == nullptr) {
        return;
    }
    const auto* is_local = reinterpret_cast<const uint8_t*>(
        reinterpret_cast<uintptr_t>(player_controller) + kLocalPlayerControllerFlag);
    if ((*is_local & 1U) == 0) {
        return;
    }
    if (g_original_do_mobile_controls != nullptr) {
        g_original_do_mobile_controls(player_controller, delta_seconds);
    }
}

bool tutorial_repair_prepare(PatchTransaction* transaction) {
    if (transaction == nullptr) {
        return false;
    }
    const uintptr_t target = ue4_addr(kDoMobileControls);
    uintptr_t trampoline = 0;
    if (!add_entry_relay(
            transaction,
            "DoMobileControls native ASX ownership gate",
            target,
            kExpectedPrologue,
            reinterpret_cast<uintptr_t>(&ASX_DoMobileControlsGate),
            &trampoline)) {
        return false;
    }
    g_original_do_mobile_controls = reinterpret_cast<DoMobileControlsFunction>(trampoline);
    return true;
}

}  // namespace asx
