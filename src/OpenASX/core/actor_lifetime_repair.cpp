// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2026 LinXing0926. All rights reserved.
 */

#include "declarations_shared.hpp"

namespace asx {

namespace {

constexpr uintptr_t kUWorldDestroyActor = 0xa923d14;
constexpr uint32_t kUWorldDestroyActorExpected = 0xd10543ff;
constexpr uintptr_t kUWorldRemoveNetworkActor = 0xae913e0;

constexpr uintptr_t kStaggeredActorOwnershipCall = 0x7afd98c;
constexpr uint32_t kStaggeredActorOwnershipExpected = 0x94c25569;
constexpr uintptr_t kAActorIsOwnedOrControlledBy = 0xab92f30;

constexpr uintptr_t kGlobalObjectItems = 0xce70a30;
constexpr uintptr_t kObjectChunks = 0x0;
constexpr uintptr_t kPreallocatedObjects = 0x8;
constexpr uintptr_t kObjectCount = 0x14;
constexpr uintptr_t kChunkCount = 0x1c;
constexpr uintptr_t kObjectInternalIndex = 0xc;
constexpr uintptr_t kObjectClass = 0x10;
constexpr uintptr_t kClassCastFlags = 0xd0;
constexpr uintptr_t kActorState = 0x1f4;
constexpr uintptr_t kObjectItemSize = 0x18;
constexpr int32_t kObjectsPerChunk = 0x10000;
constexpr uint8_t kActorBeingDestroyedMask = 0x10;
constexpr uint64_t kActorClassCastFlag = 0x0000001000000000ULL;
constexpr int32_t kMaximumObjectCount = 4 * 1024 * 1024;

using DestroyActor = bool (*)(void*, void*, bool, bool);
using RemoveNetworkActor = void (*)(void*, void*);
using IsOwnedOrControlledBy = bool (*)(const void*, const void*);

DestroyActor g_original_destroy_actor = nullptr;
RemoveNetworkActor g_remove_network_actor = nullptr;
IsOwnedOrControlledBy g_is_owned_or_controlled_by = nullptr;

bool current_object_slot(const void* object) {
    if (object == nullptr) {
        return false;
    }

    const uintptr_t object_address = reinterpret_cast<uintptr_t>(object);
    if ((object_address & (alignof(void*) - 1U)) != 0 || object_address < 0x10000U) {
        return false;
    }

    const auto index = *reinterpret_cast<const int32_t*>(
        object_address + kObjectInternalIndex);
    const uintptr_t object_items = g_ue4.base + kGlobalObjectItems;
    const auto object_count = *reinterpret_cast<const int32_t*>(
        object_items + kObjectCount);
    if (index < 0 || object_count <= 0 || object_count > kMaximumObjectCount ||
        index >= object_count) {
        return false;
    }

    uintptr_t item_address = 0;
    const uintptr_t chunks = *reinterpret_cast<const uintptr_t*>(
        object_items + kObjectChunks);
    if (chunks != 0) {
        const auto chunk_count = *reinterpret_cast<const int32_t*>(
            object_items + kChunkCount);
        const int32_t chunk_index = index / kObjectsPerChunk;
        if (chunk_index < 0 || chunk_index >= chunk_count) {
            return false;
        }
        const uintptr_t chunk = *reinterpret_cast<const uintptr_t*>(
            chunks + static_cast<uintptr_t>(chunk_index) * sizeof(uintptr_t));
        if (chunk == 0) {
            return false;
        }
        const int32_t item_index = index % kObjectsPerChunk;
        item_address = chunk + static_cast<uintptr_t>(item_index) * kObjectItemSize;
    } else {
        const uintptr_t preallocated = *reinterpret_cast<const uintptr_t*>(
            object_items + kPreallocatedObjects);
        if (preallocated == 0) {
            return false;
        }
        item_address = preallocated + static_cast<uintptr_t>(index) * kObjectItemSize;
    }

    const void* slot_object = *reinterpret_cast<void* const*>(item_address);
    return slot_object == object;
}

bool current_actor(const void* object) {
    if (!current_object_slot(object)) {
        return false;
    }
    const uintptr_t object_address = reinterpret_cast<uintptr_t>(object);
    const uintptr_t object_class = *reinterpret_cast<const uintptr_t*>(
        object_address + kObjectClass);
    if (object_class == 0) {
        return false;
    }
    const uint64_t cast_flags = *reinterpret_cast<const uint64_t*>(
        object_class + kClassCastFlags);
    if ((cast_flags & kActorClassCastFlag) == 0) {
        return false;
    }
    const uint8_t actor_state = *reinterpret_cast<const uint8_t*>(
        object_address + kActorState);
    return (actor_state & kActorBeingDestroyedMask) == 0;
}

extern "C" __attribute__((visibility("hidden"))) bool ASX_DestroyActorGate(
    void* world,
    void* actor,
    bool net_force,
    bool modify_level) {
    if (g_original_destroy_actor == nullptr) {
        return false;
    }
    const bool result = g_original_destroy_actor(
        world, actor, net_force, modify_level);
    if (!result || world == nullptr || actor == nullptr ||
        g_remove_network_actor == nullptr) {
        return result;
    }

    const uint8_t actor_state = *reinterpret_cast<const uint8_t*>(
        reinterpret_cast<uintptr_t>(actor) + kActorState);
    if ((actor_state & kActorBeingDestroyedMask) != 0) {
        g_remove_network_actor(world, actor);
    }
    return result;
}

extern "C" __attribute__((visibility("hidden"))) bool ASX_IsOwnedOrControlledByGate(
    const void* actor,
    const void* possible_owner) {
    return current_actor(actor) &&
        g_is_owned_or_controlled_by != nullptr &&
        g_is_owned_or_controlled_by(actor, possible_owner);
}

}  // namespace

bool actor_lifetime_repair_prepare(PatchTransaction* transaction) {
    if (transaction == nullptr) {
        return false;
    }

    g_remove_network_actor = reinterpret_cast<RemoveNetworkActor>(
        ue4_addr(kUWorldRemoveNetworkActor));
    g_is_owned_or_controlled_by = reinterpret_cast<IsOwnedOrControlledBy>(
        ue4_addr(kAActorIsOwnedOrControlledBy));

    uintptr_t destroy_trampoline = 0;
    if (!add_entry_relay(
            transaction,
            "UWorldDestroyActor",
            ue4_addr(kUWorldDestroyActor),
            kUWorldDestroyActorExpected,
            reinterpret_cast<uintptr_t>(&ASX_DestroyActorGate),
            &destroy_trampoline)) {
        return false;
    }
    g_original_destroy_actor = reinterpret_cast<DestroyActor>(destroy_trampoline);

    return add_call_relay(
        transaction,
        "UWorldRemoveNetworkActor",
        ue4_addr(kStaggeredActorOwnershipCall),
        kStaggeredActorOwnershipExpected,
        reinterpret_cast<uintptr_t>(&ASX_IsOwnedOrControlledByGate));
}

}  // namespace asx
