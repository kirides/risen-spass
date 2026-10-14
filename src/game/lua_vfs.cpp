#include <algorithm>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/bindings.h"

// risen.vfs: the engine's virtual file system (docs/RE.md §16). Everything goes through the engine's own exports, so
// the contents of the packed archives (*.pak mounted by mountlist_packed.ini) are listed and read exactly as the game
// sees them -- no archive format parsing.
//
//   risen.vfs.physical("#G3:/save")        physical path of a virtual path
//   risen.vfs.exists(path)                 file exists in the virtual file system
//   risen.vfs.find(dir, "*.dll")           names of files in `dir` matching a pattern
//   risen.vfs.find_ext(dir, "dll")         names of files with an extension
//   risen.vfs.dirs(dir)                    sub-directories
//   risen.vfs.size(path), risen.vfs.read(path [, max_bytes])

namespace spass::game {
namespace {

// bTObjArray<bCString> as laid out by the engine (docs/RE.md §12): { T* data; int count; int capacity }.
// The engine constructs `capacity` elements, so the cleanup mirrors gCScriptAdmin::LoadAllScriptGame.
struct StringArray {
    char* data = nullptr;  // elements are 8-byte bCStrings
    int count = 0;
    int capacity = 0;

    std::vector<std::string> take() {
        std::vector<std::string> out;
        for (int i = 0; i < count; ++i) {
            out.emplace_back(sym().bstring_get_text(data + static_cast<std::size_t>(i) * 8));
        }
        if (data != nullptr) {
            for (int i = 0; i < capacity; ++i) {
                sym().bstring_dtor(data + static_cast<std::size_t>(i) * 8);
            }
            sym().mem_free(sym().mem_instance(), data);
            data = nullptr;
        }
        count = capacity = 0;
        return out;
    }
};

// eCArchiveFile is 0x50 bytes in the decompilation; reserve generously.
struct alignas(16) ArchiveFileStorage {
    unsigned char bytes[256] = {};
};

sol::table toTable(sol::state_view lua, const std::vector<std::string>& names) {
    sol::table t = lua.create_table();
    int i = 1;
    for (const auto& n : names) {
        t[i++] = n;
    }
    return t;
}

}  // namespace

void bindVfs(sol::state_view lua, sol::table risen) {
    const Symbols& s = sym();
    sol::table vfs = lua.create_table();

    vfs.set_function("physical", [&s](const std::string& path) -> sol::optional<std::string> {
        if (s.vfs_instance == nullptr || s.vfs_virtual_to_physical == nullptr) {
            return sol::nullopt;
        }
        std::string result;
        bool ok = false;
        guarded([&] {
            const BString in(path);
            BString out;
            ok = s.vfs_virtual_to_physical(s.vfs_instance(), in.get(), out.raw());
            result = out.str();
        });
        if (!ok) {
            return sol::nullopt;
        }
        return result;
    });

    vfs.set_function("exists", [&s](const std::string& path) {
        bool r = false;
        if (s.evfs_instance != nullptr && s.evfs_exists_file != nullptr) {
            guarded([&] {
                const BString p(path);
                r = s.evfs_exists_file(s.evfs_instance(), p.get());
            });
        }
        return r;
    });

    // The three listing functions share their calling pattern.
    const auto list = [&s](sol::this_state ts, auto&& call) -> sol::object {
        sol::state_view L(ts);
        if (s.evfs_instance == nullptr || s.mem_free == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        StringArray arr;
        bool ok = false;
        if (!guarded([&] { ok = call(arr); })) {
            return sol::make_object(L, sol::lua_nil);
        }
        std::vector<std::string> names;
        guarded([&] { names = arr.take(); });
        if (!ok && names.empty()) {
            return sol::make_object(L, toTable(L, {}));
        }
        return sol::make_object(L, toTable(L, names));
    };
    vfs.set_function("find", [&s, list](sol::this_state ts, const std::string& dir, const std::string& pattern) {
        return list(ts, [&](StringArray& a) {
            const BString d(dir), p(pattern);
            return s.evfs_find_files != nullptr && s.evfs_find_files(s.evfs_instance(), d.get(), p.get(), &a);
        });
    });
    vfs.set_function("find_ext", [&s, list](sol::this_state ts, const std::string& dir, const std::string& ext) {
        return list(ts, [&](StringArray& a) {
            const BString d(dir), e(ext);
            return s.evfs_find_ext != nullptr && s.evfs_find_ext(s.evfs_instance(), d.get(), e.get(), &a);
        });
    });
    vfs.set_function("dirs", [&s, list](sol::this_state ts, const std::string& dir) {
        return list(ts, [&](StringArray& a) {
            const BString d(dir);
            return s.evfs_find_dirs != nullptr && s.evfs_find_dirs(s.evfs_instance(), d.get(), &a);
        });
    });

    vfs.set_function("size", [&s](const std::string& path) -> sol::optional<double> {
        if (s.vfs_instance == nullptr || s.vfs_file_size == nullptr) {
            return sol::nullopt;
        }
        unsigned long long size = 0;
        if (!guarded([&] {
                const BString p(path);
                size = s.vfs_file_size(s.vfs_instance(), p.get());
            })) {
            return sol::nullopt;
        }
        return static_cast<double>(size);
    });

    // risen.vfs.read(path [, max_bytes = 16 MiB]) -> string | nil
    vfs.set_function("read", [&s](sol::this_state ts, const std::string& path, sol::optional<double> maxBytes) -> sol::object {
        sol::state_view L(ts);
        if (s.archive_instance == nullptr || s.archive_get_file == nullptr || s.archive_file_ctor == nullptr ||
            s.archive_file_read == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        const std::size_t limit = static_cast<std::size_t>(maxBytes.value_or(16.0 * 1024 * 1024));
        std::string data;
        bool opened = false;
        guarded([&] {
            ArchiveFileStorage file;
            s.archive_file_ctor(file.bytes);
            const BString p(path);
            if (s.archive_get_file(s.archive_instance(), p.get(), file.bytes) &&
                s.archive_file_open(file.bytes, 0, true) && s.archive_file_is_open(file.bytes)) {
                opened = true;
                char chunk[64 * 1024];
                while (data.size() < limit) {
                    const unsigned long want = static_cast<unsigned long>(std::min(sizeof chunk, limit - data.size()));
                    const unsigned long got = s.archive_file_read(file.bytes, chunk, want);
                    if (got == 0) {
                        break;
                    }
                    data.append(chunk, got);
                }
                s.archive_file_close(file.bytes);
            }
            s.archive_file_dtor(file.bytes);
        });
        if (!opened) {
            return sol::make_object(L, sol::lua_nil);
        }
        return sol::make_object(L, data);
    });

    risen["vfs"] = vfs;

    // Lua helpers built on the primitives (readable and copyable by mods):
    //   risen.vfs.walk(dir [, pattern])  -> every file below `dir` as "dir/sub/name", recursively
    //   risen.template.list([group])     -> names of all entity templates (files *.tple below data/common/templates),
    //                                       optionally only those in sub-directory `group` ("NPC", "Items", "Monster", ...)
    const char* chunk = R"LUA(
local risen = ...
function risen.vfs.walk(dir, pattern)
    local out = {}
    local function go(d)
        for _, f in ipairs(risen.vfs.find(d, pattern or "*") or {}) do out[#out + 1] = d .. "/" .. f end
        for _, sub in ipairs(risen.vfs.dirs(d) or {}) do go(d .. "/" .. sub) end
    end
    go(dir)
    return out
end

function risen.template.list(group)
    local root = "data/common/templates" .. (group and ("/" .. group) or "")
    local names = {}
    for _, path in ipairs(risen.vfs.walk(root, "*.tple")) do
        names[#names + 1] = path:match("([^/]+)%.[^./]+$")
    end
    table.sort(names)
    return names
end
)LUA";
    sol::load_result loaded = lua.load(chunk, "=risen.vfs");
    if (!loaded.valid()) {
        spdlog::error("lua: vfs helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result r = fn(risen);
    if (!r.valid()) {
        spdlog::error("lua: vfs helpers failed to run: {}", r.get<sol::error>().what());
    }
}

}  // namespace spass::game
