#include "game/properties.h"

#include <windows.h>

#include "game/abi.h"

namespace spass::game {
namespace {

// sizeof(bCAccessorProperty) is not exported; reserve a generous, aligned buffer (GetTitle uses 32 bytes).
struct AccessorBuffer {
    alignas(16) unsigned char bytes[256] = {};
};

}  // namespace

bool propertySet(void* object, const std::string& name, const std::string& value) {
    const Symbols& s = sym();
    bool ok = false;
    guarded([&] {
        AccessorBuffer acc;
        const BString n(name), v(value);
        s.acc_ctor(acc.bytes, object, n.get());
        if (s.acc_is_valid(acc.bytes)) {
            s.acc_set_string(acc.bytes, v.get());
            ok = true;
        }
        s.acc_dtor(acc.bytes);
    });
    return ok;
}

sol::optional<std::string> propertyGet(void* object, const std::string& name) {
    const Symbols& s = sym();
    std::string value;
    bool ok = false;
    guarded([&] {
        AccessorBuffer acc;
        const BString n(name);
        s.acc_ctor(acc.bytes, object, n.get());
        if (s.acc_is_valid(acc.bytes)) {
            OutString out;
            s.acc_get_string(acc.bytes, out.ptr());
            value = out.take();
            ok = true;
        }
        s.acc_dtor(acc.bytes);
    });
    if (!ok) {
        return sol::nullopt;
    }
    return value;
}

std::vector<std::pair<std::string, std::string>> propertyList(void* object) {
    const Symbols& s = sym();
    std::vector<std::pair<std::string, std::string>> out;
    if (s.acc_ctor_object == nullptr || s.acc_next == nullptr || s.acc_name == nullptr || s.acc_type == nullptr) {
        return out;
    }
    guarded([&] {
        AccessorBuffer acc;
        s.acc_ctor_object(acc.bytes, object);
        for (int guard = 0; guard < 2000 && s.acc_is_valid(acc.bytes); ++guard) {
            out.emplace_back(s.bstring_get_text(s.acc_name(acc.bytes)), s.bstring_get_text(s.acc_type(acc.bytes)));
            s.acc_next(acc.bytes);
        }
        s.acc_dtor(acc.bytes);
    });
    return out;
}

sol::table propertyListTable(sol::state_view lua, void* object) {
    sol::table out = lua.create_table();
    int i = 1;
    for (const auto& [name, type] : propertyList(object)) {
        sol::table entry = lua.create_table();
        entry["name"] = name;
        entry["type"] = type;
        out[i++] = entry;
    }
    return out;
}

void* locStringBase(void* text) {
    const auto first = *reinterpret_cast<void**>(text);
    HMODULE owner = nullptr;
    if (first != nullptr &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(first), &owner) &&
        owner != nullptr) {
        return static_cast<char*>(text) + 8;
    }
    return text;
}

}  // namespace spass::game