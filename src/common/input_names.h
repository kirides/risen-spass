#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

// Names of keys for risen.input (docs/LUA_API.md "Interaction, input and HUD building blocks"): Windows virtual-key names ("RButton", "V", "F5", "Shift").
// Pure functions, unit-tested (tests/quickloot_tests.inc).
namespace spass::input {

namespace detail {
inline std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return r;
}
}  // namespace detail

// Virtual-key code of a key name (case-insensitive), or nullopt. Letters and digits are their own name ("v", "7"); the function keys
// "f1".."f24"; the keypad "num0".."num9"; the mouse buttons "lbutton", "rbutton", "mbutton", "xbutton1/2"; modifiers "shift", "ctrl"
// ("control"), "alt" ("menu") and the sided "lshift", "rshift", "lctrl", "rctrl", "lalt", "ralt".
inline std::optional<int> vkFromName(std::string_view name) {
    const std::string n = detail::lower(name);
    if (n.empty()) {
        return std::nullopt;
    }
    if (n.size() == 1) {
        const char c = n[0];
        if (c >= 'a' && c <= 'z') {
            return 0x41 + (c - 'a');
        }
        if (c >= '0' && c <= '9') {
            return 0x30 + (c - '0');
        }
    }
    if (n[0] == 'f' && n.size() >= 2 && n.size() <= 3 && std::isdigit(static_cast<unsigned char>(n[1])) != 0) {
        const int number = std::stoi(n.substr(1));
        if (number >= 1 && number <= 24) {
            return 0x70 + number - 1;
        }
        return std::nullopt;
    }
    if (n.size() == 4 && n.compare(0, 3, "num") == 0 && n[3] >= '0' && n[3] <= '9') {
        return 0x60 + (n[3] - '0');
    }
    struct Named {
        const char* name;
        int vk;
    };
    static constexpr Named kNamed[] = {
        {"lbutton", 0x01}, {"rbutton", 0x02}, {"mbutton", 0x04}, {"xbutton1", 0x05}, {"xbutton2", 0x06},
        {"backspace", 0x08}, {"back", 0x08}, {"tab", 0x09}, {"enter", 0x0D}, {"return", 0x0D},
        {"shift", 0x10}, {"ctrl", 0x11}, {"control", 0x11}, {"alt", 0x12}, {"menu", 0x12}, {"pause", 0x13}, {"capslock", 0x14},
        {"esc", 0x1B}, {"escape", 0x1B}, {"space", 0x20}, {"pageup", 0x21}, {"pagedown", 0x22}, {"end", 0x23}, {"home", 0x24},
        {"left", 0x25}, {"up", 0x26}, {"right", 0x27}, {"down", 0x28}, {"insert", 0x2D}, {"delete", 0x2E},
        {"multiply", 0x6A}, {"add", 0x6B}, {"subtract", 0x6D}, {"decimal", 0x6E}, {"divide", 0x6F},
        {"lshift", 0xA0}, {"rshift", 0xA1}, {"lctrl", 0xA2}, {"rctrl", 0xA3}, {"lalt", 0xA4}, {"ralt", 0xA5},
        {"semicolon", 0xBA}, {"plus", 0xBB}, {"comma", 0xBC}, {"minus", 0xBD}, {"period", 0xBE}, {"slash", 0xBF}, {"tilde", 0xC0},
    };
    for (const Named& k : kNamed) {
        if (n == k.name) {
            return k.vk;
        }
    }
    return std::nullopt;
}

// "SecondaryAction" -> "secondaryaction": action names compare without case.
inline std::string foldName(std::string_view name) { return detail::lower(name); }

// Strips white space around a key name from a config file.
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

}  // namespace spass::input
