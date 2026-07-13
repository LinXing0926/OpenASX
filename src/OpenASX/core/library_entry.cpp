#include "declarations_shared.hpp"

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <cinttypes>

#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 4
#endif

namespace asx {

namespace {

std::atomic<bool> g_installed{false};
std::atomic<bool> g_installing{false};

bool release_repairs_install() {
    PatchTransaction transaction;
    if (!postlogin_repair_prepare(&transaction) ||
        !host_mode_repair_prepare(&transaction) ||
        !playerdata_repair_prepare(&transaction) ||
        !tribe_repair_prepare(&transaction) ||
        !tutorial_repair_prepare(&transaction)) {
        relay_pool_reset();
        ASX_LOGE("release repair preparation failed; no instruction was patched");
        return false;
    }
    if (!transaction.preflight()) {
        relay_pool_reset();
        ASX_LOGE("release repair preflight failed; no instruction was patched");
        return false;
    }
    return transaction.commit();
}

void* installer_thread(void*) {
    for (int attempt = 0; attempt < 400; ++attempt) {
        void* handle = dlopen("libUE4.so", RTLD_NOW | RTLD_NOLOAD);
        if (handle != nullptr) {
            dlclose(handle);
        }
        if (refresh_ue4_module()) {
            break;
        }
        usleep(50000);
    }

    if (!g_ue4.found) {
        ASX_LOGE("libUE4.so was not mapped before the installer deadline");
        g_installing.store(false, std::memory_order_release);
        return nullptr;
    }

    const bool installed = release_repairs_install();
    g_installed.store(installed, std::memory_order_release);
    g_installing.store(false, std::memory_order_release);
    ASX_LOGI(
        "release install complete ok=%d base=0x%" PRIxPTR " patches are process-lifetime owned",
        installed ? 1 : 0, g_ue4.base);
    return nullptr;
}

void installer_start() {
    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    pthread_t thread = {};
    if (pthread_create(&thread, nullptr, installer_thread, nullptr) != 0) {
        g_installing.store(false, std::memory_order_release);
        ASX_LOGE("could not create installer thread");
        return;
    }
    pthread_detach(thread);
}

__attribute__((constructor)) void library_constructor() {
    installer_start();
}

}  // namespace

}  // namespace asx

extern "C" __attribute__((visibility("default"))) int ASX_Install() {
    if (asx::g_installed.load(std::memory_order_acquire)) {
        return 1;
    }
    asx::installer_start();
    return 0;
}
