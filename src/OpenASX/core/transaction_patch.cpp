#include "declarations_shared.hpp"

#include <cinttypes>

namespace asx {

bool PatchTransaction::add(
    const char* name,
    uintptr_t address,
    uint32_t expected,
    uint32_t replacement) {
    if (count_ >= kMaximumPatches || name == nullptr || address == 0) {
        return false;
    }
    patches_[count_++] = WordPatch{name, address, expected, replacement, 0};
    preflight_ok_ = false;
    return true;
}

bool PatchTransaction::preflight() {
    for (size_t index = 0; index < count_; ++index) {
        WordPatch& patch = patches_[index];
        if (!safe_read_u32(patch.address, &patch.original) || patch.original != patch.expected) {
            ASX_LOGE(
                "transaction blocked at %s 0x%" PRIxPTR ": got=0x%08x expected=0x%08x",
                patch.name, patch.address, patch.original, patch.expected);
            preflight_ok_ = false;
            return false;
        }
    }
    preflight_ok_ = true;
    return true;
}

bool PatchTransaction::rollback(size_t applied_count) {
    bool ok = true;
    for (size_t index = applied_count; index > 0; --index) {
        WordPatch& patch = patches_[index - 1];
        uint32_t actual = 0;
        const bool restored = write_u32(patch.address, patch.original) &&
            safe_read_u32(patch.address, &actual) && actual == patch.original;
        if (!restored) {
            ASX_LOGE("rollback failed at %s 0x%" PRIxPTR, patch.name, patch.address);
        }
        ok = restored && ok;
    }
    return ok;
}

bool PatchTransaction::commit() {
    if (!preflight_ok_ && !preflight()) {
        return false;
    }

    size_t applied_count = 0;
    for (; applied_count < count_; ++applied_count) {
        WordPatch& patch = patches_[applied_count];
        uint32_t actual = 0;
        const bool applied = write_u32(patch.address, patch.replacement) &&
            safe_read_u32(patch.address, &actual) && actual == patch.replacement;
        if (!applied) {
            ASX_LOGE("transaction write failed at %s 0x%" PRIxPTR, patch.name, patch.address);
            rollback(applied_count + 1);
            return false;
        }
    }
    ASX_LOGI("transaction committed %zu instruction patches", count_);
    return true;
}

}  // namespace asx
