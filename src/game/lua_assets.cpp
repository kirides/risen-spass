#include <filesystem>
#include <fstream>
#include <set>

#include <spdlog/spdlog.h>

#include "common/asset_names.h"
#include "common/paths.h"
#include "game/asset_files.h"
#include "game/abi.h"
#include "game/bindings.h"
#include "game/vfs_mount.h"
#include "lua/runtime.h"

// risen.assets: new meshes, materials, textures and icons from loose files (docs/RE.md section 26, docs/LUA_API.md "New meshes, textures and icons").
//
// How the engine finds a resource: a raw name from a property or a material (`X.xmsh`, `X.tga`, `X.xac`) is turned into the compiled file name
// (`X._xmsh`, `X._ximg`, `X._xmac`) and looked up by that bare, case-insensitive file name in a flat cache that holds every file of every
// mounted directory or packed volume (CFXVirtualFileSystem::LookupCachedVirtualPath). Mounting a directory with loose files therefore makes
// them resources, whatever sub-directory they sit in.
//
// Shared natives (risen.assets):
//   resource(name)        -> { exists, state, path } | nil   the engine's resource object (eCResource2::GetResource): state 3 = data loaded
//   load_bitmap(name)     -> bool                            eCGuiDevice2::LoadBitmapA: registers an image file as GUI bitmap (inventory icon)
//   bitmap(name)          -> { attached } | nil              eCGuiDevice2::QueryBitmapA + eCDeferredGuiBitmap2::GetBitmap
//   compiled_name(name)   -> compiled, kind | nil, reason    pure name mapping (asset_names.h)
//   info(name)            -> table                           name cache lookup + where the file comes from
//   mounted()             -> list of mounted asset directories
//   refresh()             -> number of directories remounted (files written since the mount become visible)
// Per mod (only for the calling mod):
//   write(relpath, data)  -> true | nil, reason              writes <spass>/generated/<mod>/<relpath> (confined to that directory)
//   remove(relpath), clear()                                 delete what write created
//   mount(reldir)         -> virtual name | nil, reason      mounts a directory of the mod (relative to the mod directory)

namespace spass::game {
namespace {

// eCGuiDevice2's IFFGuiDevice2 sub-object (its vftable is stored at +0x250 by the constructor, 0x516cf0): the virtual LoadBitmapA / LoadImageListA
// implementations expect this pointer (they reach the bitmap cache at device+0xa8 as `this - 0x1a8`); the other members take the device itself.
constexpr std::ptrdiff_t kGuiDeviceInterfaceOffset = 0x250;

std::set<std::string> g_dirty;  // virtual names of generated directories that received files after they were mounted

std::string lookupName(const std::string& name, bool physical) {
    const Symbols& s = sym();
    if (s.vfs_instance == nullptr || (physical ? s.vfs_lookup_physical : s.vfs_lookup_virtual) == nullptr) {
        return {};
    }
    std::string result;
    bool ok = false;
    guarded([&] {
        const BString in(name);
        BString out;
        ok = (physical ? s.vfs_lookup_physical : s.vfs_lookup_virtual)(s.vfs_instance(), in.get(), out.raw());
        if (ok) {
            result = out.str();
        }
    });
    return ok ? result : std::string();
}

int refreshDirty() {
    int n = 0;
    for (const auto& v : std::set<std::string>(g_dirty)) {
        if (remountAssetDirectory(v)) {
            ++n;
        }
    }
    g_dirty.clear();
    return n;
}

// Which mount a physical path belongs to ("" = the game's own data).
std::string mountOf(const std::string& physical) {
    const std::string p = assets::lowerAscii(physical);
    for (const auto& m : assetMounts()) {
        const std::string root = assets::lowerAscii(std::filesystem::path(m.physical).make_preferred().string());
        if (!root.empty() && p.compare(0, root.size(), root) == 0) {
            return m.mod + ":" + m.kind;
        }
    }
    return {};
}

}  // namespace

void bindAssetNatives(sol::state_view lua, sol::table risen) {
    sol::table assetsTable = lua.create_table();
    const Symbols& s = sym();

    assetsTable.set_function("resource", [&s](sol::this_state ts, const std::string& name) -> sol::object {
        sol::state_view L(ts);
        refreshDirty();
        if (s.res_get == nullptr || s.res_state == nullptr || name.empty()) {
            return sol::make_object(L, sol::lua_nil);
        }
        void* res = nullptr;
        int state = 0;
        std::string path;
        if (!guarded([&] {
                const BString n(name);
                res = s.res_get(n.get());
                if (res != nullptr) {
                    state = s.res_state(res);
                    if (s.res_file_path != nullptr) {
                        OutString out;
                        s.res_file_path(res, out.ptr());
                        path = out.take();
                    }
                }
            }) ||
            res == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table t = L.create_table();
        t["exists"] = state != 0;
        t["state"] = state;
        t["path"] = path;
        return sol::make_object(L, t);
    });

    assetsTable.set_function("load_bitmap", [&s](const std::string& name) {
        bool ok = false;
        refreshDirty();
        if (s.gui_module_device != nullptr && s.gui_load_bitmap != nullptr && !name.empty()) {
            if (!guarded([&] {
                void* dev = s.gui_module_device();
                if (dev != nullptr) {
                    ok = s.gui_load_bitmap(static_cast<char*>(dev) + kGuiDeviceInterfaceOffset, name.c_str(), nullptr) != nullptr;
                }
            })) {
                spdlog::warn("assets: eCGuiDevice2::LoadBitmapA raised an exception");
            }
        }
        return ok;
    });

    assetsTable.set_function("bitmap", [&s](sol::this_state ts, const std::string& name) -> sol::object {
        sol::state_view L(ts);
        if (s.gui_module_device == nullptr || s.gui_query_bitmap == nullptr || s.gui_deferred_bitmap == nullptr || name.empty()) {
            return sol::make_object(L, sol::lua_nil);
        }
        void* deferred = nullptr;
        bool attached = false;
        guarded([&] {
            void* dev = s.gui_module_device();
            if (dev != nullptr) {
                deferred = s.gui_query_bitmap(dev, name.c_str());
                if (deferred != nullptr) {
                    attached = s.gui_deferred_bitmap(deferred) != nullptr;
                }
            }
        });
        if (deferred == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table t = L.create_table();
        t["attached"] = attached;
        return sol::make_object(L, t);
    });

    // load_image_list("Name") -> bool: eCGuiDevice2::LoadImageListA finds `Name.ximl` (an XML image list) in the name cache and registers its images
    // as named GUI bitmaps (what IconImage / eCGuiBitmapProxy2 names resolve to).
    assetsTable.set_function("load_image_list", [&s](const std::string& name) {
        bool ok = false;
        refreshDirty();
        if (s.gui_module_device != nullptr && s.gui_load_image_list != nullptr && !name.empty()) {
            if (!guarded([&] {
                void* dev = s.gui_module_device();
                if (dev != nullptr) {
                    ok = s.gui_load_image_list(static_cast<char*>(dev) + kGuiDeviceInterfaceOffset, name.c_str(), nullptr) != nullptr;
                }
            })) {
                spdlog::warn("assets: eCGuiDevice2::LoadImageListA raised an exception");
            }
        }
        return ok;
    });

    // dump_collection("IMC_Icons", "C:/path/out.xml") -> bool: writes an image collection the engine loaded as XML (development aid: shows the
    // .ximl/.ximc format). Needs a physical path outside the sandbox rules: only allowed below <spass>/generated.
    assetsTable.set_function("dump_collection", [&s](const std::string& name, const std::string& rel) {
        bool ok = false;
        const auto clean = assets::cleanRelativePath(rel);
        if (!clean || s.gui_module_device == nullptr || s.gui_load_collection == nullptr || s.gui_collection_save_xml == nullptr) {
            return false;
        }
        const std::filesystem::path target = paths::dataDir() / "generated" / "_dump" / *clean;
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        guarded([&] {
            void* dev = s.gui_module_device();
            void* col = dev != nullptr ? s.gui_load_collection(dev, name.c_str(), nullptr) : nullptr;
            if (col != nullptr) {
                ok = s.gui_collection_save_xml(col, target.string().c_str());
            }
        });
        return ok;
    });

    assetsTable.set_function("compiled_name", [](sol::this_state ts, const std::string& name) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        const auto compiled = assets::compiledName(name);
        if (!compiled) {
            return {sol::make_object(L, sol::lua_nil),
                    sol::make_object(L, std::string("'") + name + "' is not a resource name (expected a bare file name ending in .xmsh, .xmat, .ximg, .tga, .dds, .xac, ...)")};
        }
        return {sol::make_object(L, *compiled), sol::make_object(L, std::string(assets::kindName(assets::kindOf(name))))};
    });

    assetsTable.set_function("info", [](sol::this_state ts, const std::string& name) -> sol::object {
        sol::state_view L(ts);
        refreshDirty();
        sol::table t = L.create_table();
        t["name"] = name;
        const auto compiled = assets::compiledName(name);
        if (!compiled) {
            t["found"] = false;
            return sol::make_object(L, t);
        }
        t["compiled"] = *compiled;
        t["kind"] = std::string(assets::kindName(assets::kindOf(name)));
        const std::string virt = lookupName(*compiled, false);
        t["found"] = !virt.empty();
        if (!virt.empty()) {
            const std::string phys = lookupName(*compiled, true);
            t["virtual"] = virt;
            t["physical"] = phys;
            t["loose"] = assets::isLoosePhysicalPath(phys);
            t["source"] = assets::isLoosePhysicalPath(phys) ? (mountOf(phys).empty() ? std::string("loose") : mountOf(phys)) : std::string("game");
        }
        return sol::make_object(L, t);
    });

    assetsTable.set_function("mounted", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table list = L.create_table();
        int i = 1;
        for (const auto& m : assetMounts()) {
            sol::table e = L.create_table();
            e["mod"] = m.mod;
            e["kind"] = m.kind;
            e["virtual"] = m.virtualPath;
            e["physical"] = m.physical;
            list[i++] = e;
        }
        return list;
    });

    assetsTable.set_function("refresh", [] { return refreshDirty(); });

    risen["assets"] = assetsTable;

    // Helpers written in Lua on top of the natives (readable and copyable by mods).
    const char* chunk = R"LUA(
local risen = ...
local A = risen.assets

-- risen.assets.read(name) -> the bytes of the file a resource name resolves to (a game file or a loose one), or nil, reason. Meant for deriving new
-- assets from existing ones: A.write("copy/New._xmsh", A.read("It_Wpn_1H_Club.xmsh"))
function A.read(name)
    local info = A.info(name)
    if not info.found then return nil, ("no file '%s' to read"):format(tostring(info.compiled or name)) end
    local data = risen.vfs.read(info.virtual, 256 * 1024 * 1024)
    if not data then return nil, ("cannot read '%s'"):format(info.virtual) end
    return data
end

-- risen.assets.exists(name) -> bool: a resource name (X.xmsh, X.tga, X._ximg, ...) resolves to a file in the game archives or a mounted directory
function A.exists(name)
    local i = A.info(name)
    return i.found == true
end

local WANT = { mesh = "mesh", actor = "actor", image = "image", material = "material" }

-- risen.assets.check(kind, name) -> true, info | nil, reason
-- kind "mesh" | "actor" | "image" | "material": the name must map to a compiled file the engine can find; "icon": the name must be a GUI bitmap the
-- engine has registered (shipped item icons are IC_It_*, a mod adds its own with an image list, see risen.assets.load_icons).
function A.check(kind, name)
    if type(name) ~= "string" or name == "" then return nil, "expected a file name" end
    if kind == "icon" then
        local b = A.bitmap(name)
        if b == nil or b.attached then return true end   -- nil: the GUI is not up yet, nothing can be said
        return nil, ("'%s' is not a GUI bitmap the engine knows (shipped item icons are called IC_It_..., new ones come from an image list: risen.assets.load_icons); pass check_assets = false to use the name anyway"):format(name)
    end
    local compiled, k = A.compiled_name(name)
    if not compiled then return nil, k end
    if WANT[kind] and k ~= WANT[kind] then
        return nil, ("'%s' is %s, expected a %s file name"):format(name, k == "unknown" and "not a resource name" or ("a " .. k), kind)
    end
    local info = A.info(name)
    if not info.found then
        return nil, ("no file '%s' in the game archives or in a mounted asset directory (a mod's assets/ directory is mounted at startup; risen.assets.mount and risen.assets.write add more); pass check_assets = false to skip this check"):format(compiled)
    end
    return true, info
end

-- risen.assets.load_icons("Spass_Icons") -> true | nil, reason
-- Registers the images of the XML image list Spass_Icons.ximl (found like any other asset file) as GUI bitmaps, so that item templates can use their
-- ImageName values as `icon`. Safe to call again; call it before creating templates that use the icons (the first tick is early enough).
function A.load_icons(list)
    if type(list) ~= "string" or list == "" then return nil, "expected an image list name such as Spass_Icons" end
    list = list:gsub("%.ximl$", "")
    A.refresh()
    if not risen.vfs.lookup(list .. ".ximl") then
        return nil, ("no file '%s.ximl' in a mounted asset directory"):format(list)
    end
    if not A.load_image_list(list) then
        return nil, ("the engine could not load the image list '%s' (is the XML valid? see docs/LUA_API.md for the format)"):format(list)
    end
    return true
end
)LUA";
    sol::load_result loaded = lua.load(chunk, "=risen.assets");
    if (!loaded.valid()) {
        spdlog::error("lua: assets helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result r = fn(risen);
    if (!r.valid()) {
        spdlog::error("lua: assets helpers failed to run: {}", r.get<sol::error>().what());
    }
}

// Writes <spass>/generated/<mod>/<rel> (atomically) and makes the directory known to the engine: a first write mounts it, later ones mark it for a
// remount before the next lookup (refreshAssetMounts() does it at once). Shared by risen.assets.write and the sound import.
bool writeGeneratedAsset(const std::string& id, const std::string& rel, const std::string& data, std::string& error) {
    const auto clean = assets::cleanRelativePath(rel);
    if (!clean) {
        error = "risen.assets.write: '" + rel + "' is not a relative path below the mod's generated directory (no '..', no drive, no empty parts)";
        return false;
    }
    if (data.size() > 256u * 1024u * 1024u) {
        error = "risen.assets.write: data larger than 256 MB";
        return false;
    }
    const std::filesystem::path dir = paths::dataDir() / "generated" / id;
    const std::filesystem::path target = dir / *clean;
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        error = "risen.assets.write: cannot create " + target.parent_path().string() + ": " + ec.message();
        return false;
    }
    const std::filesystem::path tmp = target.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.close();
        if (!f) {
            std::filesystem::remove(tmp, ec);
            error = "risen.assets.write: cannot write " + tmp.string();
            return false;
        }
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(target, ec);
        std::filesystem::rename(tmp, target, ec);
        if (ec) {
            error = "risen.assets.write: cannot replace " + target.string() + ": " + ec.message();
            return false;
        }
    }
    // make the new file known to the engine: a first write mounts the directory, later ones remount it before the next lookup
    bool mounted = false;
    for (const auto& m : assetMounts()) {
        if (m.mod == id && m.kind == "generated") {
            g_dirty.insert(m.virtualPath);
            mounted = true;
        }
    }
    if (!mounted && !mountAssetDirectory(id, "generated", dir.string())) {
        error = "risen.assets.write: the engine could not mount " + dir.string();
        return false;
    }
    return true;
}

int refreshAssetMounts() { return refreshDirty(); }

// Per-mod functions: they know which mod calls (registered with Runtime::addModBinding).
void bindAssetModApi(sol::state_view lua, sol::table api, const lua::ModInfo& mod) {
    {
        sol::table shared = api["assets"];
        sol::table own = lua.create_table();
        sol::table meta = lua.create_table();
        meta["__index"] = shared;
        own[sol::metatable_key] = meta;
        const std::string id = mod.id;
        const std::filesystem::path modDir = mod.dir;

        own.set_function("write", [id](sol::this_state ts, const std::string& rel, const std::string& data) -> std::tuple<sol::object, sol::object> {
            sol::state_view L(ts);
            std::string error;
            if (!writeGeneratedAsset(id, rel, data, error)) {
                return {sol::make_object(L, sol::lua_nil), sol::make_object(L, error)};
            }
            return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
        });

        // remove(relpath) deletes a file (or directory tree) written by write; clear() deletes everything the mod generated. Both -> number of entries removed.
        const auto markDirty = [id] {
            for (const auto& m : assetMounts()) {
                if (m.mod == id && m.kind == "generated") {
                    g_dirty.insert(m.virtualPath);
                }
            }
        };
        own.set_function("remove", [id, markDirty](sol::this_state ts, const std::string& rel) -> std::tuple<sol::object, sol::object> {
            sol::state_view L(ts);
            const auto clean = assets::cleanRelativePath(rel);
            if (!clean) {
                return {sol::make_object(L, sol::lua_nil), sol::make_object(L, "risen.assets.remove: '" + rel + "' is not a relative path below the mod's generated directory")};
            }
            std::error_code ec;
            const auto n = std::filesystem::remove_all(paths::dataDir() / "generated" / id / *clean, ec);
            markDirty();
            return {sol::make_object(L, static_cast<double>(n)), sol::make_object(L, sol::lua_nil)};
        });
        own.set_function("clear", [id, markDirty]() {
            std::error_code ec;
            double n = 0;
            const std::filesystem::path dir = paths::dataDir() / "generated" / id;
            if (std::filesystem::is_directory(dir, ec)) {
                for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
                    n += static_cast<double>(std::filesystem::remove_all(e.path(), ec));
                }
            }
            markDirty();
            return n;
        });

        own.set_function("mount", [id, modDir](sol::this_state ts, const std::string& rel) -> std::tuple<sol::object, sol::object> {
            sol::state_view L(ts);
            const auto fail = [&](const std::string& why) {
                return std::tuple<sol::object, sol::object>{sol::make_object(L, sol::lua_nil), sol::make_object(L, why)};
            };
            const auto clean = assets::cleanRelativePath(rel);
            if (!clean) {
                return fail("risen.assets.mount: '" + rel + "' is not a relative directory below the mod directory");
            }
            const std::filesystem::path dir = modDir / *clean;
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) {
                return fail("risen.assets.mount: " + dir.string() + " is not a directory");
            }
            if (!mountAssetDirectory(id, "runtime", dir.string())) {
                return fail("risen.assets.mount: the engine could not mount " + dir.string());
            }
            for (const auto& m : assetMounts()) {
                if (m.mod == id && m.physical == dir.string()) {
                    return {sol::make_object(L, m.virtualPath), sol::make_object(L, sol::lua_nil)};
                }
            }
            return fail("risen.assets.mount: internal error");
        });

        api["assets"] = own;
    }
}

}  // namespace spass::game
