// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2026 LinXing0926. All rights reserved.
 */

#include "declarations_shared.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

namespace asx {

ModuleInfo g_ue4;

namespace {

const char* basename_ptr(const char* path) {
    if (path == nullptr) {
        return "";
    }
    const char* slash = std::strrchr(path, '/');
    return slash == nullptr ? path : slash + 1;
}

int module_callback(dl_phdr_info* info, size_t, void* data) {
    auto* out = static_cast<ModuleInfo*>(data);
    if (std::strcmp(basename_ptr(info->dlpi_name), "libUE4.so") != 0) {
        return 0;
    }

    uintptr_t end = 0;
    for (size_t index = 0; index < info->dlpi_phnum; ++index) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[index];
        if (ph.p_type == PT_LOAD) {
            end = std::max(end, static_cast<uintptr_t>(ph.p_vaddr + ph.p_memsz));
        }
    }

    out->base = static_cast<uintptr_t>(info->dlpi_addr);
    out->end = out->base + end;
    out->phdr = info->dlpi_phdr;
    out->phnum = info->dlpi_phnum;
    std::snprintf(out->path, sizeof(out->path), "%s", info->dlpi_name == nullptr ? "" : info->dlpi_name);
    out->found = true;
    return 1;
}

bool range_has_permissions(uintptr_t address, size_t length, bool writable) {
    if (address == 0 || length == 0 || address + length < address) {
        return false;
    }
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (maps == nullptr) {
        return false;
    }

    const uintptr_t requested_end = address + length;
    char line[512];
    bool found = false;
    while (std::fgets(line, sizeof(line), maps) != nullptr) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        char permissions[5] = {};
        if (std::sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4s", &start, &end, permissions) != 3) {
            continue;
        }
        const bool permission_ok = permissions[0] == 'r' && (!writable || permissions[1] == 'w');
        if (permission_ok && address >= start && requested_end <= end) {
            found = true;
            break;
        }
    }
    std::fclose(maps);
    return found;
}

bool branch_range_contains(uintptr_t source, uintptr_t target) {
    uint32_t ignored = 0;
    return encode_b(source, target, &ignored);
}

struct RelayPage {
    void* memory = nullptr;
    size_t size = 0;
    size_t used = 0;
};

constexpr size_t kMaximumRelayPages = 8;
constexpr size_t kRelayAlignment = 16;
RelayPage g_relay_pages[kMaximumRelayPages];

size_t relay_reservation_size(size_t length) {
    return (length + kRelayAlignment - 1U) & ~(kRelayAlignment - 1U);
}

void* find_relay_slot(uintptr_t target, size_t reservation) {
    for (RelayPage& page : g_relay_pages) {
        if (page.memory == nullptr || page.used + reservation > page.size) {
            continue;
        }
        const uintptr_t candidate = reinterpret_cast<uintptr_t>(page.memory) + page.used;
        if (!branch_range_contains(target, candidate)) {
            continue;
        }
        page.used += reservation;
        return reinterpret_cast<void*>(candidate);
    }
    return nullptr;
}

bool remember_relay_page(void* memory, size_t size, size_t used) {
    for (RelayPage& page : g_relay_pages) {
        if (page.memory == nullptr) {
            page = RelayPage{memory, size, used};
            return true;
        }
    }
    return false;
}

}  // namespace

bool refresh_ue4_module() {
    ModuleInfo info;
    dl_iterate_phdr(module_callback, &info);
    if (!info.found) {
        return false;
    }
    g_ue4 = info;
    return true;
}

uintptr_t ue4_addr(uintptr_t offset) {
    return g_ue4.base + offset;
}

bool is_readable_range(uintptr_t address, size_t length) {
    return range_has_permissions(address, length, false);
}

bool safe_read_u32(uintptr_t address, uint32_t* out) {
    if (out == nullptr || !is_readable_range(address, sizeof(*out))) {
        return false;
    }
    std::memcpy(out, reinterpret_cast<const void*>(address), sizeof(*out));
    return true;
}

bool safe_read_u64(uintptr_t address, uint64_t* out) {
    if (out == nullptr || !is_readable_range(address, sizeof(*out))) {
        return false;
    }
    std::memcpy(out, reinterpret_cast<const void*>(address), sizeof(*out));
    return true;
}

bool safe_read_ptr(uintptr_t address, uintptr_t* out) {
    if (out == nullptr || !is_readable_range(address, sizeof(*out))) {
        return false;
    }
    std::memcpy(out, reinterpret_cast<const void*>(address), sizeof(*out));
    return true;
}

bool safe_write_u64(uintptr_t address, uint64_t value) {
    if (!range_has_permissions(address, sizeof(value), true)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
    return true;
}

size_t page_size() {
    static const size_t value = [] {
        const long result = sysconf(_SC_PAGESIZE);
        return result > 0 ? static_cast<size_t>(result) : static_cast<size_t>(4096);
    }();
    return value;
}

uintptr_t page_floor(uintptr_t value) {
    return value & ~(static_cast<uintptr_t>(page_size()) - 1U);
}

uintptr_t page_ceil(uintptr_t value) {
    return (value + page_size() - 1U) & ~(static_cast<uintptr_t>(page_size()) - 1U);
}

void clear_code_cache(uintptr_t address, size_t length) {
    __builtin___clear_cache(
        reinterpret_cast<char*>(address),
        reinterpret_cast<char*>(address + length));
}

bool write_u32(uintptr_t address, uint32_t word) {
    const uintptr_t start = page_floor(address);
    const uintptr_t end = page_ceil(address + sizeof(word));
    const size_t length = end - start;
    if (mprotect(reinterpret_cast<void*>(start), length, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        ASX_LOGE("mprotect RWX failed at 0x%" PRIxPTR ": %d", address, errno);
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(address), &word, sizeof(word));
    clear_code_cache(address, sizeof(word));
    if (mprotect(reinterpret_cast<void*>(start), length, PROT_READ | PROT_EXEC) != 0) {
        ASX_LOGW("mprotect RX restore failed at 0x%" PRIxPTR ": %d", address, errno);
    }
    return true;
}

void* allocate_near(uintptr_t target, size_t length) {
    if (target == 0 || length == 0) {
        return nullptr;
    }
    const size_t reservation = relay_reservation_size(length);
    if (void* existing = find_relay_slot(target, reservation); existing != nullptr) {
        return existing;
    }

    const size_t allocation_size = page_ceil(reservation);
    const uintptr_t target_page = page_floor(target);
    constexpr uintptr_t kMaximumDistance = 120U * 1024U * 1024U;
    constexpr uintptr_t kStep = 64U * 1024U;

    for (uintptr_t radius = page_size(); radius < kMaximumDistance; radius += kStep) {
        for (int direction : {-1, 1}) {
            if (direction < 0 && target_page <= radius) {
                continue;
            }
            const uintptr_t hint = page_floor(
                direction < 0 ? target_page - radius : target_page + radius);
            void* memory = mmap(
                reinterpret_cast<void*>(hint), allocation_size,
                PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (memory == MAP_FAILED && errno == EINVAL) {
                memory = mmap(
                    reinterpret_cast<void*>(hint), allocation_size,
                    PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            }
            if (memory != MAP_FAILED) {
                const uintptr_t actual = reinterpret_cast<uintptr_t>(memory);
                if (branch_range_contains(target, actual)) {
                    if (remember_relay_page(memory, allocation_size, reservation)) {
                        return memory;
                    }
                    munmap(memory, allocation_size);
                    ASX_LOGE("relay page registry is full");
                    return nullptr;
                }
                munmap(memory, allocation_size);
            }
        }
    }
    ASX_LOGE("could not allocate branch-reachable relay near 0x%" PRIxPTR, target);
    return nullptr;
}

void relay_pool_reset() {
    for (RelayPage& page : g_relay_pages) {
        if (page.memory != nullptr) {
            munmap(page.memory, page.size);
            page = RelayPage{};
        }
    }
}

}  // namespace asx
