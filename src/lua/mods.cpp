#include "lua/mods.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

namespace spass::lua {
namespace {

bool validId(const std::string& id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::vector<std::string> readStringArray(const toml::table& t, std::string_view key) {
    std::vector<std::string> out;
    if (const auto* arr = t[key].as_array()) {
        for (const auto& el : *arr) {
            if (auto s = el.value<std::string>()) {
                out.push_back(*s);
            }
        }
    }
    return out;
}

}  // namespace

std::optional<ModInfo> readManifest(const std::filesystem::path& modDir, std::string& error) {
    const auto manifest = modDir / "mod.toml";
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec)) {
        error = "there is no mod.toml in " + modDir.string();
        return std::nullopt;
    }
    try {
        const toml::table t = toml::parse_file(manifest.string());
        ModInfo m;
        m.id = t["id"].value_or(std::string{});
        if (!validId(m.id)) {
            error = "missing or invalid 'id' (allowed: a-z 0-9 _)";
            return std::nullopt;
        }
        m.name = t["name"].value_or(m.id);
        m.version = t["version"].value_or(std::string{"0.0.0"});
        m.author = t["author"].value_or(std::string{});
        m.description = t["description"].value_or(std::string{});
        m.main = t["main"].value_or(std::string{"main.lua"});
        m.loadAfter = readStringArray(t, "load_after");
        m.requires_ = readStringArray(t, "requires");
        m.dataOverlay = t["data_overlay"].value_or(false);
        if (const auto* av = t["assets"].as_boolean()) {
            if (!av->get()) {
                m.assets.clear();  // assets = false
            }
        } else if (auto as = t["assets"].value<std::string>()) {
            m.assets = *as;
        }
        m.apiVersion = static_cast<int>(t["api_version"].value_or(std::int64_t{1}));
        if (m.apiVersion > kApiVersion) {
            error = "needs Lua API version " + std::to_string(m.apiVersion) + " but this build provides " + std::to_string(kApiVersion) +
                    ": update risen-spass";
            m.dir = modDir;
            return std::nullopt;
        }
        if (const toml::table* retire = t["retire"].as_table()) {
            for (const auto& [key, value] : *retire) {
                if (const auto* arr = value.as_array()) {
                    std::vector<std::string> names;
                    for (const auto& el : *arr) {
                        if (auto s = el.value<std::string>()) {
                            names.push_back(*s);
                        }
                    }
                    m.retire[std::string(key.str())] = std::move(names);
                }
            }
        }
        m.dir = modDir;
        return m;
    } catch (const toml::parse_error& e) {
        error = std::string(e.description());
        return std::nullopt;
    }
}

std::vector<ModInfo> discoverMods(const std::filesystem::path& root, std::vector<ModIssue>* issues) {
    std::vector<ModInfo> mods;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        spdlog::info("mods: directory {} does not exist, no mods loaded", root.string());
        return mods;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        // Convention: rename a mod folder to "<name>.off" (or prefix it with '_' or '.') to disable it.
        const std::string folder = entry.path().filename().string();
        if (folder.empty() || folder[0] == '.' || folder[0] == '_' ||
            (folder.size() > 4 && folder.compare(folder.size() - 4, 4, ".off") == 0)) {
            continue;
        }
        const auto manifest = entry.path() / "mod.toml";
        if (!std::filesystem::exists(manifest)) {
            continue;
        }
        std::string error;
        std::optional<ModInfo> m = readManifest(entry.path(), error);
        if (!m) {
            spdlog::error("mods: {}: {}{}", manifest.string(), error, error.find("update risen-spass") != std::string::npos ? ", mod skipped" : "");
            if (issues != nullptr) {
                issues->push_back(ModIssue{folder, {}, error});
            }
            continue;
        }
        mods.push_back(std::move(*m));
    }
    std::sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) { return a.id < b.id; });
    return mods;
}

// ---- the user's configuration (mods.toml) ---------------------------------------------------------------------------------------

UserConfig loadUserConfig(const std::filesystem::path& file, std::vector<std::string>& problems) {
    UserConfig cfg;
    std::error_code ec;
    if (file.empty() || !std::filesystem::exists(file, ec)) {
        return cfg;
    }
    try {
        const toml::table t = toml::parse_file(file.string());
        if (const auto* arr = t["order"].as_array()) {
            std::set<std::string> seen;
            for (const auto& el : *arr) {
                if (auto s = el.value<std::string>(); s && validId(*s)) {
                    if (seen.insert(*s).second) {
                        cfg.order.push_back(*s);
                    } else {
                        problems.push_back("order lists '" + *s + "' twice (the first entry counts)");
                    }
                } else {
                    problems.push_back("order contains an entry that is not a mod id");
                }
            }
        }
        if (const toml::table* en = t["enabled"].as_table()) {
            for (const auto& [key, value] : *en) {
                const std::string id(key.str());
                if (auto b = value.value<bool>(); b && validId(id)) {
                    cfg.enabled[id] = *b;
                } else {
                    problems.push_back("enabled." + id + " must be true or false");
                }
            }
        }
    } catch (const toml::parse_error& e) {
        problems.push_back(std::string(e.description()) + " (line " + std::to_string(e.source().begin.line) + "); the file was ignored");
        cfg = UserConfig{};
    }
    return cfg;
}

std::string serializeUserConfig(const UserConfig& cfg) {
    std::ostringstream out;
    out << "# risen-spass: choices of the player outside the mods (written by risen.mods.enable / disable / set_order, safe to edit by hand)\n";
    if (!cfg.order.empty()) {
        out << "order = [";
        for (std::size_t i = 0; i < cfg.order.size(); ++i) {
            out << (i ? ", " : "") << '"' << cfg.order[i] << '"';
        }
        out << "]\n";
    }
    if (!cfg.enabled.empty()) {
        out << "\n[enabled]\n";
        for (const auto& [id, on] : cfg.enabled) {
            out << id << " = " << (on ? "true" : "false") << "\n";
        }
    }
    return out.str();
}

bool saveUserConfig(const std::filesystem::path& file, const UserConfig& cfg, std::string& error) {
    try {
        if (file.empty()) {
            error = "there is no configuration file path";
            return false;
        }
        std::filesystem::create_directories(file.parent_path());
        const auto tmp = std::filesystem::path(file).concat(".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << serializeUserConfig(cfg);
            if (!out) {
                error = "cannot write " + tmp.string();
                return false;
            }
        }
        std::filesystem::rename(tmp, file);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

// ---- load plan ---------------------------------------------------------------------------------------------------------------------

LoadPlan planLoad(std::vector<ModInfo> mods, const UserConfig& cfg, bool report) {
    LoadPlan plan;
    std::map<std::string, ModInfo> byId;
    for (auto& m : mods) {
        if (!byId.emplace(m.id, m).second) {
            if (report) {
                spdlog::error("mods: duplicate id '{}' ({}) ignored", m.id, m.dir.string());
            }
            plan.skipped.push_back(SkippedMod{m, "duplicate", "another mod folder already uses the id '" + m.id + "'"});
        }
    }

    // switched off by the user
    for (auto it = byId.begin(); it != byId.end();) {
        if (!cfg.isEnabled(it->first)) {
            if (report) {
                spdlog::info("mods: '{}' is switched off in mods.toml", it->first);
            }
            plan.skipped.push_back(SkippedMod{it->second, "disabled", "switched off in mods.toml"});
            it = byId.erase(it);
        } else {
            ++it;
        }
    }

    // Drop mods whose hard requirements are not available (missing, switched off, or themselves dropped), transitively.
    std::set<std::string> gone;  // ids that exist but are not loaded
    for (const auto& s : plan.skipped) {
        gone.insert(s.mod.id);
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = byId.begin(); it != byId.end();) {
            const auto missing = std::find_if(it->second.requires_.begin(), it->second.requires_.end(),
                                              [&](const std::string& r) { return !byId.contains(r); });
            if (missing != it->second.requires_.end()) {
                const bool known = gone.contains(*missing);
                const std::string why = known ? "requires '" + *missing + "' which is not loaded (switched off or failed to qualify)"
                                              : "requires '" + *missing + "' which is not installed";
                if (report) {
                    spdlog::error("mods: '{}' {}, skipping", it->first, why);
                }
                plan.skipped.push_back(SkippedMod{it->second, known ? "dependency_unavailable" : "missing_dependency", why});
                gone.insert(it->first);
                it = byId.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }

    // custom order positions
    std::map<std::string, std::size_t> position;
    for (const std::string& id : cfg.order) {
        if (!position.contains(id)) {
            position.emplace(id, position.size());
        }
    }
    for (const std::string& id : cfg.order) {
        if (!byId.contains(id)) {
            plan.warnings.push_back("mods.toml order lists '" + id + "' which is not loaded: ignored");
        }
    }

    std::set<std::string> done;
    const auto ready = [&](const ModInfo& m) {
        const auto ok = [&](const std::vector<std::string>& deps) {
            return std::all_of(deps.begin(), deps.end(), [&](const std::string& d) {
                return done.contains(d) || !byId.contains(d);  // load_after on a missing mod is ignored
            });
        };
        return ok(m.requires_) && ok(m.loadAfter);
    };
    while (done.size() < byId.size()) {
        const ModInfo* best = nullptr;
        std::size_t bestPos = 0;
        for (const auto& [id, m] : byId) {  // std::map iterates in id order => deterministic
            if (done.contains(id) || !ready(m)) {
                continue;
            }
            const auto p = position.find(id);
            const std::size_t pos = p != position.end() ? p->second : position.size() + 1;
            if (best == nullptr || pos < bestPos) {
                best = &m;
                bestPos = pos;
            }
        }
        if (best == nullptr) {
            for (const auto& [id, m] : byId) {
                if (!done.contains(id)) {
                    if (report) {
                        spdlog::error("mods: '{}' is part of a dependency cycle, skipping", id);
                    }
                    plan.skipped.push_back(SkippedMod{m, "cycle", "part of a dependency cycle (requires / load_after)"});
                }
            }
            break;
        }
        plan.ordered.push_back(*best);
        done.insert(best->id);
    }

    // where the custom order was not followed because of a constraint
    if (!cfg.order.empty()) {
        std::map<std::string, std::size_t> placed;
        for (std::size_t i = 0; i < plan.ordered.size(); ++i) {
            placed[plan.ordered[i].id] = i;
        }
        for (std::size_t i = 0; i + 1 < cfg.order.size(); ++i) {
            for (std::size_t j = i + 1; j < cfg.order.size(); ++j) {
                const auto a = placed.find(cfg.order[i]);
                const auto b = placed.find(cfg.order[j]);
                if (a != placed.end() && b != placed.end() && a->second > b->second) {
                    plan.warnings.push_back("mods.toml order puts '" + cfg.order[i] + "' before '" + cfg.order[j] +
                                            "' but a requires / load_after constraint forces the other way round");
                }
            }
        }
    }
    return plan;
}

std::vector<ModInfo> resolveLoadOrder(std::vector<ModInfo> mods) { return planLoad(std::move(mods), UserConfig{}).ordered; }

std::string checkOrder(const std::vector<ModInfo>& mods, const std::vector<std::string>& order) {
    std::map<std::string, const ModInfo*> byId;
    for (const auto& m : mods) {
        byId[m.id] = &m;
    }
    std::map<std::string, std::size_t> position;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (!byId.contains(order[i])) {
            return "'" + order[i] + "' is not a known mod";
        }
        if (!position.emplace(order[i], i).second) {
            return "'" + order[i] + "' is listed twice";
        }
    }
    for (const std::string& id : order) {
        const ModInfo& m = *byId[id];
        for (const auto* deps : {&m.requires_, &m.loadAfter}) {
            for (const std::string& d : *deps) {
                const auto p = position.find(d);
                if (p != position.end() && p->second > position[id]) {
                    return "'" + id + "' cannot be loaded before '" + d + "' (" + (deps == &m.requires_ ? "requires" : "load_after") + ")";
                }
            }
        }
    }
    return {};
}

std::vector<std::string> dependentsOf(const std::vector<ModInfo>& mods, const std::string& id) {
    std::set<std::string> found;
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& m : mods) {
            if (found.contains(m.id) || m.id == id) {
                continue;
            }
            for (const std::string& r : m.requires_) {
                if (r == id || found.contains(r)) {
                    found.insert(m.id);
                    changed = true;
                    break;
                }
            }
        }
    }
    // dependency order: a dependent comes after what it needs
    std::vector<std::string> out;
    std::set<std::string> placed;
    while (out.size() < found.size()) {
        bool progressed = false;
        for (const std::string& f : found) {
            if (placed.contains(f)) {
                continue;
            }
            const auto it = std::find_if(mods.begin(), mods.end(), [&](const ModInfo& m) { return m.id == f; });
            const bool ok = std::all_of(it->requires_.begin(), it->requires_.end(), [&](const std::string& r) { return !found.contains(r) || placed.contains(r); });
            if (ok) {
                out.push_back(f);
                placed.insert(f);
                progressed = true;
            }
        }
        if (!progressed) {
            break;
        }
    }
    return out;
}

// ---- versions -----------------------------------------------------------------------------------------------------------------------

namespace {

struct ParsedVersion {
    std::vector<long long> parts;
    std::string suffix;  // "beta" of "1.0-beta"
};

ParsedVersion parseVersion(const std::string& v) {
    ParsedVersion p;
    std::string core = v;
    if (const auto dash = core.find('-'); dash != std::string::npos) {
        p.suffix = core.substr(dash + 1);
        core = core.substr(0, dash);
    }
    std::string num;
    const auto flush = [&] {
        p.parts.push_back(num.empty() ? 0 : std::strtoll(num.c_str(), nullptr, 10));
        num.clear();
    };
    for (const char c : core) {
        if (c == '.') {
            flush();
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            num += c;
        }  // anything else ("v" in "v1.2") is ignored
    }
    flush();
    while (p.parts.size() > 1 && p.parts.back() == 0) {
        p.parts.pop_back();  // 1.0 == 1.0.0
    }
    return p;
}

}  // namespace

int compareVersions(const std::string& a, const std::string& b) {
    const ParsedVersion pa = parseVersion(a), pb = parseVersion(b);
    const std::size_t n = std::max(pa.parts.size(), pb.parts.size());
    for (std::size_t i = 0; i < n; ++i) {
        const long long x = i < pa.parts.size() ? pa.parts[i] : 0;
        const long long y = i < pb.parts.size() ? pb.parts[i] : 0;
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    if (pa.suffix == pb.suffix) {
        return 0;
    }
    if (pa.suffix.empty()) {
        return 1;  // 1.0 is newer than 1.0-beta
    }
    if (pb.suffix.empty()) {
        return -1;
    }
    return pa.suffix < pb.suffix ? -1 : 1;
}

std::optional<bool> versionMatches(const std::string& spec, const std::string& version) {
    if (spec.empty()) {
        return std::nullopt;
    }
    std::stringstream ss(spec);
    std::string term;
    bool any = false;
    bool result = true;
    while (std::getline(ss, term, ',')) {
        term.erase(0, term.find_first_not_of(" 	"));
        term.erase(term.find_last_not_of(" 	") + 1);
        if (term.empty()) {
            return std::nullopt;
        }
        any = true;
        if (term == "*") {
            continue;
        }
        std::string op;
        std::size_t i = 0;
        while (i < term.size() && (term[i] == '<' || term[i] == '>' || term[i] == '=' || term[i] == '!')) {
            op += term[i++];
        }
        std::string rhs = term.substr(i);
        rhs.erase(0, rhs.find_first_not_of(" 	"));
        if (rhs.empty() || (!std::isdigit(static_cast<unsigned char>(rhs[0])) && rhs[0] != 'v')) {
            return std::nullopt;
        }
        const int c = compareVersions(version, rhs);
        bool ok = false;
        if (op.empty() || op == "=" || op == "==") ok = c == 0;
        else if (op == "<") ok = c < 0;
        else if (op == "<=") ok = c <= 0;
        else if (op == ">") ok = c > 0;
        else if (op == ">=") ok = c >= 0;
        else if (op == "!=") ok = c != 0;
        else return std::nullopt;
        result = result && ok;
    }
    if (!any) {
        return std::nullopt;
    }
    return result;
}

}  // namespace spass::lua
