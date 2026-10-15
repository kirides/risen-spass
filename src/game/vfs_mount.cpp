#include "game/vfs_mount.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include <spdlog/spdlog.h>

#include "common/asset_names.h"
#include "common/paths.h"
#include "game/abi.h"
#include "game/symbols.h"
#include "hook/hook_manager.h"
#include "lua/mods.h"

namespace spass::game {
namespace {

Hook<bool (*)(void*, const void*)> g_processMountList;
std::vector<std::string> g_mounted;  // virtual names of the mod overlays currently mounted
std::vector<AssetMount> g_assetMounts;

// Mounts <mod>/ for every mod that asks for it (`data_overlay = true` in mod.toml), in load order (a later mod shadows an earlier one).
// Called after every mount list the engine processes: earlier overlays are removed first, so the mods always sit on top.
void mountModDirectories() {
    for (const auto& v : g_mounted) {
        unmountDirectory(v);
    }
    g_mounted.clear();
    for (const auto& a : g_assetMounts) {
        unmountDirectory(a.virtualPath);
    }
    g_assetMounts.clear();
    const auto mods = lua::resolveLoadOrder(lua::discoverMods(paths::modsDir()));
    for (const auto& m : mods) {
        // Loose resource files: the mod's assets directory (when the whole mod directory is not mounted already) and the directory
        // risen.assets.write fills. Their file names enter the engine's name cache, which is how resources are found (RE.md §26).
        std::error_code ec;
        if (!m.dataOverlay && !m.assets.empty()) {
            const auto rel = assets::cleanRelativePath(m.assets);
            if (rel && std::filesystem::is_directory(m.dir / *rel, ec)) {
                const bool ok = mountAssetDirectory(m.id, "assets", (m.dir / *rel).string());
                spdlog::info("vfs: mod '{}' assets directory {}", m.id, ok ? "mounted" : "FAILED to mount");
            }
        }
        const auto generated = paths::dataDir() / "generated" / m.id;
        if (std::filesystem::is_directory(generated, ec)) {
            const bool ok = mountAssetDirectory(m.id, "generated", generated.string());
            spdlog::info("vfs: mod '{}' generated assets {}", m.id, ok ? "mounted" : "FAILED to mount");
        }
        if (!m.dataOverlay) {
            continue;
        }
        const std::string virt = "mods/" + m.id;
        const bool ok = mountDirectory(virt, m.dir.string());
        if (ok) {
            g_mounted.push_back(virt);
        }
        spdlog::info("vfs: mod '{}' data overlay {}", m.id, ok ? "mounted" : "FAILED to mount");
    }
}

// eCVirtualFileSystem::ProcessMountList(const bCString& file): reads mountlist_packed.ini and mounts everything in it.
bool detourProcessMountList(void* self, const void* file) {
    const bool ok = g_processMountList.original(self, file);
    spdlog::info("vfs: mount list '{}' processed", file != nullptr ? sym().bstring_get_text(file) : "?");
    guarded([] { mountModDirectories(); });
    return ok;
}

}  // namespace

bool mountDirectory(const std::string& virtualPath, const std::string& physicalPath) {
    const Symbols& s = sym();
    if (s.evfs_instance == nullptr || s.evfs_mount_dir == nullptr) {
        return false;
    }
    std::string physical = physicalPath;
    std::replace(physical.begin(), physical.end(), '/', '\\');
    bool ok = false;
    guarded([&] {
        BString v(virtualPath);
        void* fs = s.evfs_instance();
        if (s.evfs_make_abs_virtual != nullptr) {
            s.evfs_make_abs_virtual(fs, v.raw());
        }
        if (s.evfs_make_dir != nullptr) {
            s.evfs_make_dir(fs, v.get());
        }
        const BString p(physical);
        ok = s.evfs_mount_dir(fs, v.get(), p.get());
    });
    return ok;
}

bool mountAssetDirectory(const std::string& mod, const std::string& kind, const std::string& physicalDir) {
    for (const auto& a : g_assetMounts) {
        if (a.mod == mod && a.physical == physicalDir) {
            return true;
        }
    }
    int n = 0;
    for (const auto& a : g_assetMounts) {
        n += a.mod == mod && a.kind == kind ? 1 : 0;
    }
    AssetMount m{mod, "mods/" + mod + "/" + kind + (n > 0 ? "_" + std::to_string(n) : std::string()), physicalDir, kind};
    if (!mountDirectory(m.virtualPath, m.physical)) {
        return false;
    }
    g_assetMounts.push_back(std::move(m));
    return true;
}

bool remountAssetDirectory(const std::string& virtualPath) {
    for (const auto& a : g_assetMounts) {
        if (a.virtualPath == virtualPath) {
            unmountDirectory(a.virtualPath);
            return mountDirectory(a.virtualPath, a.physical);
        }
    }
    return false;
}

const std::vector<AssetMount>& assetMounts() { return g_assetMounts; }

bool unmountDirectory(const std::string& virtualPath) {
    const Symbols& s = sym();
    if (s.evfs_instance == nullptr || s.evfs_unmount == nullptr) {
        return false;
    }
    bool ok = false;
    guarded([&] {
        const BString v(virtualPath);
        ok = s.evfs_unmount(s.evfs_instance(), v.get());
    });
    return ok;
}

void installModMountHook() {
    const Symbols& s = sym();
    if (s.evfs_process_mountlist_file == nullptr) {
        spdlog::warn("vfs: ProcessMountList not found, mod data directories are not mounted at startup");
        return;
    }
    g_processMountList.install("eCVirtualFileSystem::ProcessMountList", s.evfs_process_mountlist_file, &detourProcessMountList);
}

}  // namespace spass::game
