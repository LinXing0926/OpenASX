// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2026 LinXing0926. All rights reserved.
 */

#include "declarations_shared.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace asx {

namespace {

constexpr uintptr_t kHostSessionFinishLoadMap = 0x7c4363c;
constexpr uintptr_t kHostSessionPreparedTravelUrl = 0x790;
constexpr uintptr_t kIsLocalPlayerOnline = 0x7f89fd4;
constexpr uintptr_t kFMemoryRealloc = 0x8669258;
constexpr uintptr_t kGEngine = 0xcfd64d8;
constexpr uintptr_t kEngineGameViewport = 0x780;
constexpr uintptr_t kGameViewportGameInstance = 0x78;
constexpr uintptr_t kGameInstanceLocalPlayers = 0x38;
constexpr uintptr_t kUObjectGetWorldVtableSlot = 0x160;
constexpr uintptr_t kWorldGameInstance = 0x320;
constexpr uint32_t kExpectedFinishLoadMapPrologue = 0xd10103ff;

using FinishLoadMapFunction = void (*)(void*);
using MemoryReallocFunction = void* (*)(void*, size_t, uint32_t);
using GetWorldFunction = void* (*)(void*);
using IsLocalPlayerOnlineFunction = bool (*)(void*, void*);

FinishLoadMapFunction g_original_finish_load_map = nullptr;

bool safe_read_i32(uintptr_t address, int32_t* out) {
    if (out == nullptr || !is_readable_range(address, sizeof(*out))) {
        return false;
    }
    std::memcpy(out, reinterpret_cast<const void*>(address), sizeof(*out));
    return true;
}

void append_utf8(std::string* out, uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        out->push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        out->push_back(static_cast<char>(0xc0 | (codepoint >> 6U)));
        out->push_back(static_cast<char>(0x80 | (codepoint & 0x3fU)));
    } else {
        out->push_back(static_cast<char>(0xe0 | (codepoint >> 12U)));
        out->push_back(static_cast<char>(0x80 | ((codepoint >> 6U) & 0x3fU)));
        out->push_back(static_cast<char>(0x80 | (codepoint & 0x3fU)));
    }
}

std::string read_utf16_z(const char16_t* text, size_t maximum_characters) {
    if (text == nullptr || !is_readable_range(
            reinterpret_cast<uintptr_t>(text), sizeof(char16_t))) {
        return {};
    }

    std::string out;
    out.reserve(128);
    for (size_t index = 0; index < maximum_characters; ++index) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(text) + index * sizeof(char16_t);
        if (!is_readable_range(address, sizeof(char16_t))) {
            break;
        }
        char16_t character = 0;
        std::memcpy(&character, reinterpret_cast<const void*>(address), sizeof(character));
        if (character == 0) {
            break;
        }
        append_utf8(&out, static_cast<uint32_t>(character));
    }
    return out;
}

std::string lower_ascii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

std::string trim_ascii(std::string value) {
    const auto is_space = [](char character) {
        return character == ' ' || character == '\t' ||
            character == '\r' || character == '\n';
    };
    while (!value.empty() && is_space(value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(value.back())) {
        value.pop_back();
    }
    return value;
}

bool starts_with_ascii_ci(const std::string& value, const char* prefix) {
    const size_t length = std::strlen(prefix);
    if (value.size() < length) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        char left = value[index];
        char right = prefix[index];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<char>(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

struct PreparedStartCommand {
    std::string prefix;
    std::string travel_url;
};

PreparedStartCommand parse_start_command(const std::string& original) {
    PreparedStartCommand out;
    const std::string trimmed = trim_ascii(original);
    constexpr const char* kOpenPrefix = "open ";
    if (starts_with_ascii_ci(trimmed, kOpenPrefix)) {
        out.prefix = trimmed.substr(0, std::strlen(kOpenPrefix));
        out.travel_url = trim_ascii(trimmed.substr(std::strlen(kOpenPrefix)));
    } else {
        out.travel_url = trimmed;
    }
    return out;
}

bool is_local_map_url(const std::string& original) {
    const std::string trimmed = trim_ascii(original);
    if (trimmed.empty()) {
        return false;
    }

    const std::string lowered = lower_ascii(trimmed);
    if (lowered.find("://") != std::string::npos ||
        lowered.find("travel ") != std::string::npos ||
        lowered.find("admincheat ") != std::string::npos ||
        lowered.find("127.0.0.1") != std::string::npos ||
        lowered.find("localhost") != std::string::npos) {
        return false;
    }

    const size_t question = lowered.find('?');
    const std::string path = lowered.substr(0, question);
    if (path.empty() || path == "entry" || path == "mainmenu" ||
        path.find("mainmenu") != std::string::npos ||
        path.find("shootergameentry") != std::string::npos) {
        return false;
    }
    if (path.rfind("/game/maps/", 0) == 0 || path.rfind("/game/mods/", 0) == 0) {
        return true;
    }

    const char first = path.front();
    const bool valid_first = (first >= 'a' && first <= 'z') ||
        (first >= '0' && first <= '9') || first == '_';
    return valid_first && path.find('/') == std::string::npos &&
        path.find('\\') == std::string::npos;
}

bool option_key_equals(const std::string& option, const char* key) {
    std::string normalized = trim_ascii(option);
    const size_t equals = normalized.find('=');
    if (equals != std::string::npos) {
        normalized = trim_ascii(normalized.substr(0, equals));
    }
    return lower_ascii(normalized) == key;
}

std::string remove_host_options(const std::string& original) {
    const std::string trimmed = trim_ascii(original);
    const size_t first_question = trimmed.find('?');
    if (first_question == std::string::npos) {
        return trimmed;
    }

    std::string out = trimmed.substr(0, first_question);
    size_t start = first_question + 1;
    while (start <= trimmed.size()) {
        const size_t end = trimmed.find('?', start);
        const std::string option = trimmed.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (!option_key_equals(option, "listen") &&
            !option_key_equals(option, "bIsLanMatch")) {
            out += '?';
            out += option;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

std::string add_host_options(const std::string& original) {
    std::string out = original;
    std::string lowered = lower_ascii(out);
    if (lowered.find("listen") == std::string::npos) {
        out += "?listen";
        lowered = lower_ascii(out);
    }
    if (lowered.find("bIsLanMatch") == std::string::npos) {
        out += "?bIsLanMatch=true";
    }
    return out;
}

std::string read_fstring(uintptr_t address) {
    uintptr_t data = 0;
    int32_t count = 0;
    if (!safe_read_ptr(address, &data) ||
        !safe_read_i32(address + sizeof(uintptr_t), &count) ||
        data == 0 || count <= 0 || count > 4096) {
        return {};
    }
    return read_utf16_z(reinterpret_cast<const char16_t*>(data), static_cast<size_t>(count));
}

bool write_fstring(uintptr_t address, const std::string& value) {
    uintptr_t old_data = 0;
    if (!safe_read_ptr(address, &old_data)) {
        return false;
    }

    std::u16string utf16;
    utf16.reserve(value.size() + 1);
    for (const unsigned char character : value) {
        utf16.push_back(static_cast<char16_t>(character));
    }
    utf16.push_back(u'\0');
    if (utf16.size() > 4096) {
        return false;
    }

    auto realloc_memory = reinterpret_cast<MemoryReallocFunction>(ue4_addr(kFMemoryRealloc));
    if (!is_readable_range(reinterpret_cast<uintptr_t>(realloc_memory), sizeof(uint32_t))) {
        return false;
    }
    void* new_data = realloc_memory(
        reinterpret_cast<void*>(old_data), utf16.size() * sizeof(char16_t), 0);
    if (new_data == nullptr) {
        return false;
    }

    std::memcpy(new_data, utf16.data(), utf16.size() * sizeof(char16_t));
    const int32_t count = static_cast<int32_t>(utf16.size());
    std::memcpy(reinterpret_cast<void*>(address), &new_data, sizeof(new_data));
    std::memcpy(reinterpret_cast<void*>(address + sizeof(uintptr_t)), &count, sizeof(count));
    std::memcpy(
        reinterpret_cast<void*>(address + sizeof(uintptr_t) + sizeof(int32_t)),
        &count, sizeof(count));
    return true;
}

bool resolve_game_instance_from_object(void* context, void** game_instance) {
    uintptr_t vtable = 0;
    uintptr_t get_world_address = 0;
    if (context == nullptr || game_instance == nullptr ||
        !safe_read_ptr(reinterpret_cast<uintptr_t>(context), &vtable) || vtable == 0 ||
        !safe_read_ptr(vtable + kUObjectGetWorldVtableSlot, &get_world_address) ||
        !is_readable_range(get_world_address, sizeof(uint32_t))) {
        return false;
    }

    auto get_world = reinterpret_cast<GetWorldFunction>(get_world_address);
    const uintptr_t world = reinterpret_cast<uintptr_t>(get_world(context));
    uintptr_t result = 0;
    if (world == 0 || !safe_read_ptr(world + kWorldGameInstance, &result) || result == 0) {
        return false;
    }
    *game_instance = reinterpret_cast<void*>(result);
    return true;
}

bool resolve_game_instance_from_engine(void** game_instance) {
    uintptr_t engine = 0;
    uintptr_t viewport = 0;
    uintptr_t result = 0;
    if (game_instance == nullptr ||
        !safe_read_ptr(ue4_addr(kGEngine), &engine) || engine == 0 ||
        !safe_read_ptr(engine + kEngineGameViewport, &viewport) || viewport == 0 ||
        !safe_read_ptr(viewport + kGameViewportGameInstance, &result) || result == 0) {
        return false;
    }
    *game_instance = reinterpret_cast<void*>(result);
    return true;
}

bool local_player_is_online(void* context) {
    void* game_instance = nullptr;
    if (!resolve_game_instance_from_object(context, &game_instance) &&
        !resolve_game_instance_from_engine(&game_instance)) {
        return false;
    }

    const uintptr_t players = reinterpret_cast<uintptr_t>(game_instance) + kGameInstanceLocalPlayers;
    uintptr_t player_array = 0;
    int32_t player_count = 0;
    uintptr_t local_player = 0;
    if (!safe_read_ptr(players, &player_array) ||
        !safe_read_i32(players + sizeof(uintptr_t), &player_count) ||
        player_array == 0 || player_count <= 0 || player_count > 16 ||
        !safe_read_ptr(player_array, &local_player) || local_player == 0) {
        return false;
    }

    auto is_online = reinterpret_cast<IsLocalPlayerOnlineFunction>(ue4_addr(kIsLocalPlayerOnline));
    return is_readable_range(reinterpret_cast<uintptr_t>(is_online), sizeof(uint32_t)) &&
        is_online(game_instance, reinterpret_cast<void*>(local_player));
}

void rewrite_start_url(void* host_session, bool online) {
    const uintptr_t fstring = reinterpret_cast<uintptr_t>(host_session) +
        kHostSessionPreparedTravelUrl;
    const std::string original = read_fstring(fstring);
    const PreparedStartCommand command = parse_start_command(original);
    if (!is_local_map_url(command.travel_url)) {
        return;
    }

    const std::string rewritten_url = online
        ? add_host_options(command.travel_url)
        : remove_host_options(command.travel_url);
    const std::string rewritten = command.prefix + rewritten_url;
    if (!rewritten.empty() && rewritten != original) {
        write_fstring(fstring, rewritten);
    }
}

}  // namespace

extern "C" __attribute__((visibility("hidden"))) void ASX_FinishLoadMapGate(
    void* host_session) {
    rewrite_start_url(host_session, local_player_is_online(host_session));
    if (g_original_finish_load_map != nullptr) {
        g_original_finish_load_map(host_session);
    }
}

bool host_mode_repair_prepare(PatchTransaction* transaction) {
    if (transaction == nullptr) {
        return false;
    }
    uintptr_t trampoline = 0;
    if (!add_entry_relay(
            transaction,
            "FinishLoadMap online host-mode gate",
            ue4_addr(kHostSessionFinishLoadMap),
            kExpectedFinishLoadMapPrologue,
            reinterpret_cast<uintptr_t>(&ASX_FinishLoadMapGate),
            &trampoline)) {
        return false;
    }
    g_original_finish_load_map = reinterpret_cast<FinishLoadMapFunction>(trampoline);
    return true;
}

}  // namespace asx
