#include "core/gamedata.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <set>
#include <stdexcept>

#include "core/log.h"
#include "core/paths.h"
#include "pack2mod/tab.h"

namespace mk {

const char* kind_name(TplKind k) {
    switch (k) {
        case TplKind::Item: return "Item";
        case TplKind::Recipe: return "Recipe";
        case TplKind::Npc: return "NPC";
        case TplKind::Monster: return "Monster";
        case TplKind::Interactive: return "Interactive";
        case TplKind::Object: return "Object";
        case TplKind::Effect: return "Effect";
        case TplKind::Navigation: return "Navigation";
        case TplKind::Sound: return "Sound";
        case TplKind::Other: return "Other";
    }
    return "?";
}

bool TemplateInfo::has_set(const std::string& set) const { return std::find(sets.begin(), sets.end(), set) != sets.end(); }

namespace {

// "pNN" -> NN, ".pak" -> -1, anything else -> -2
int volume_number(const std::string& ext) {
    const std::string e = p2m::lower(ext);
    if (e == ".pak") return -1;
    if (e.size() == 4 && e[1] == 'p' && std::isdigit(static_cast<unsigned char>(e[2])) && std::isdigit(static_cast<unsigned char>(e[3]))) return (e[2] - '0') * 10 + (e[3] - '0');
    return -2;
}

TplKind classify(const TemplateInfo& t) {
    const std::string g = p2m::lower(t.group);
    if (t.has_set("gCRecipe_PS")) return TplKind::Recipe;
    if (t.has_set("gCItem_PS")) return TplKind::Item;
    if (t.has_set("gCNPC_PS")) return p2m::starts_with(g, "monster") ? TplKind::Monster : TplKind::Npc;
    if (p2m::starts_with(g, "vfx") || t.has_set("gCEffect_PS")) return TplKind::Effect;
    if (p2m::starts_with(g, "navigation")) return TplKind::Navigation;
    if (p2m::starts_with(g, "sfx")) return TplKind::Sound;
    if (t.has_set("gCInteraction_PS") || t.has_set("gCInventory_PS")) return TplKind::Interactive;
    if (t.has_set("eCMesh_PS") || t.has_set("eCSpeedTree_PS") || p2m::starts_with(g, "objects")) return TplKind::Object;
    return TplKind::Other;
}

// "projects/World/World.wrl": the archive's base name (label "common/projects.p01") and the path inside it
std::string archive_path(const p2m::LFile& f) {
    std::string base = f.label.substr(f.label.find('/') + 1);
    base = base.substr(0, base.rfind('.'));
    return base + "/" + f.e->path;
}

// gCSkills_PS is not reflected (docs/NPC_PROPERTIES.md): the template stores the set as `c9 00 <u32 0x90> c9 00 <u32 0> <u16 4>` and then the
// 34 int32 base values in gESkill order. The record follows the u16 string index of the set's name. Verified against the engine's values of every
// NPC (modkit_tests --data: gamedata_npc_skills).
std::vector<int> decode_skills(const p2m::Bytes& b, const std::vector<std::string>& strings) {
    static const std::string head("\xc9\x00\x90\x00\x00\x00\xc9\x00\x00\x00\x00\x00\x04\x00", 14);
    int idx = -1;
    for (size_t i = 0; i < strings.size(); ++i)
        if (strings[i] == "gCSkills_PS") idx = static_cast<int>(i);
    if (idx < 0) return {};
    const char lo = static_cast<char>(idx & 0xff), hi = static_cast<char>((idx >> 8) & 0xff);
    for (size_t p = b.find(head); p != std::string::npos; p = b.find(head, p + 1)) {
        bool named = false;
        for (size_t k = p >= 12 ? p - 12 : 0; k + 1 < p; ++k)
            if (b[k] == lo && b[k + 1] == hi) named = true;
        if (!named || p + head.size() + 34 * 4 > b.size()) continue;
        std::vector<int> v(34);
        for (size_t i = 0; i < 34; ++i) v[i] = static_cast<int>(p2m::rd32(b, p + head.size() + 4 * i));
        return v;
    }
    return {};
}

std::string language_of_column(const std::string& col) {
    // "English_Text" -> "English"
    const auto u = col.find('_');
    return u == std::string::npos ? col : col.substr(0, u);
}

}  // namespace

std::shared_ptr<GameData> GameData::load(const std::string& data_dir, const Progress& progress, const std::atomic<bool>* cancel) {
    const auto t0 = std::chrono::steady_clock::now();
    auto gd = std::make_shared<GameData>();
    gd->data_dir_ = data_dir;
    const fs::path root = u8(data_dir);
    std::error_code ec;
    if (!fs::is_directory(root / "common", ec) || !fs::is_directory(root / "compiled", ec))
        throw std::runtime_error("'" + data_dir + "' is not Risen's data directory (it needs the folders common and compiled)");
    auto report = [&](float f, const std::string& what) {
        if (progress) progress(f, what);
    };
    auto cancelled = [&] { return cancel && cancel->load(); };

    // ---- archives: per base name the .pak, then its volumes in number order (the engine's patch chain); the speech and sound archives give the
    //      sounds dialogue lines can use (only their directories are read)
    report(0.0f, "archives");
    for (const char* kind : {"common", "compiled"}) {
        const fs::path kd = root / kind;
        struct Arc {
            int number;
            std::string base;
            fs::path path;
        };
        std::vector<Arc> arcs;
        for (auto it = fs::directory_iterator(kd, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const int n = volume_number(u8str(it->path().extension()));
            if (n == -2) continue;
            const std::string base = p2m::lower(u8str(it->path().stem()));
            if (p2m::ends_with(base, "_back")) continue;
            arcs.push_back({n, base, it->path()});
        }
        std::sort(arcs.begin(), arcs.end(), [](const Arc& a, const Arc& b) { return std::tie(a.number, a.base) < std::tie(b.number, b.base); });
        for (auto& a : arcs) {
            try {
                gd->paks_.push_back(p2m::Pak::open(u8str(a.path)));
                (std::string(kind) == "common" ? gd->common_ : gd->compiled_).add(std::string(kind) + "/" + u8str(a.path.filename()), gd->paks_.back().get());
            } catch (const std::exception& e) {
                log_warn(std::string("archive skipped: ") + e.what());
            }
        }
    }
    // archive paths are relative to their archive ("World/World.wrl" in projects.pak): the index prefixes the archive's base name
    for (auto& [key, f] : gd->common_.files) gd->by_path_[p2m::lower(archive_path(f))] = &f;
    log_info("game data: " + std::to_string(gd->paks_.size()) + " archives, " + std::to_string(gd->common_.files.size()) + " common and " +
             std::to_string(gd->compiled_.files.size()) + " compiled files");

    // ---- templates
    std::vector<std::string> tple_keys;
    for (const std::string& k : gd->common_.order)
        if (p2m::ends_with(k, ".tple")) tple_keys.push_back(k);
    std::sort(tple_keys.begin(), tple_keys.end());
    struct Parsed {
        p2m::Template t;
        const p2m::LFile* lf;
        std::vector<int> skills;
        RawInventory inv;
    };
    std::vector<Parsed> parsed;
    parsed.reserve(tple_keys.size());
    std::unordered_map<std::string, std::string>& guid_names = gd->guid_names_;
    size_t done = 0;
    for (const std::string& k : tple_keys) {
        if (cancelled()) return nullptr;
        if (++done % 100 == 0) report(0.05f + 0.75f * static_cast<float>(done) / static_cast<float>(tple_keys.size()), "templates");
        const p2m::LFile& lf = gd->common_.files.at(k);
        try {
            const p2m::Bytes raw = lf.read();
            p2m::Template t = p2m::parse_template(raw);
            // the GUID records of the header start with the file's format version (0xd9, 0xda, 0xdb ...): the shared parser only knows 0xdb (and takes a stray "db 00" of the data for a GUID in the others), so the entity GUID
            // of every other template (the armour and head parts of Sara and Gyrger among them) would stay unresolved
            if (raw.size() > 0x1c && raw.compare(0, 8, "GENOMFLE") == 0 && raw.compare(14, 8, "GENOMETP") == 0 && raw[0x17] == '\0' && static_cast<uint8_t>(raw[0x16]) >= 0xd0 && static_cast<uint8_t>(raw[0x16]) != 0xdb) {
                t.guids.clear();
                const char ver = raw[0x16];
                for (size_t i = 0x1c; i + 18 <= raw.size() && i < 0x200; ++i)
                    if (raw[i] == ver && raw[i + 1] == '\0') t.guids.push_back("@" + p2m::hex_of(raw.substr(i + 2, 16)));
                t.guid = t.guids.size() > 1 ? t.guids[1] : (t.guids.empty() ? std::string() : t.guids[0]);
            }
            for (const std::string& g : t.guids) guid_names[g] = t.base_name();
            std::vector<int> skills;
            RawInventory inv;
            if (raw.find("gCNPC_PS") != std::string::npos) {
                skills = decode_skills(raw, t.strings);
                inv = decode_inventory(raw, t.strings);
            }
            parsed.push_back({std::move(t), &lf, std::move(skills), std::move(inv)});
        } catch (const std::exception& e) {
            log_warn("template " + lf.e->path + " skipped: " + e.what());
        }
    }
    for (auto& [t, lf, skills, inv] : parsed) {
        auto info = std::make_unique<TemplateInfo>();
        info->skills = std::move(skills);
        info->inventory_known = inv.found;
        auto item_name = [&](const std::string& guid, bool& resolved) {
            auto g = guid_names.find(guid);
            resolved = g != guid_names.end();
            return resolved ? g->second : guid;
        };
        for (auto& s : inv.stacks) {
            TemplateInfo::Stack st;
            st.item = item_name(s.guid, st.resolved);
            st.amount = s.amount;
            st.type = s.type;
            st.equip_slot = s.equip_slot;
            info->inventory.push_back(std::move(st));
        }
        for (auto& w : inv.worn) {
            bool ok = false;
            const std::string n = item_name(w.guid, ok);
            if (ok) info->worn.push_back({w.slot, n});
        }
        info->name = t.base_name();
        info->file = t.file_name();
        const std::string& path = lf->e->path;
        const auto slash = path.rfind('/');
        info->group = slash == std::string::npos ? std::string() : path.substr(0, slash);
        info->guid = t.guid;
        info->props = p2m::flat_typed(t, guid_names);
        for (const p2m::Prop& p : t.props)
            if (std::find(info->sets.begin(), info->sets.end(), p.pset) == info->sets.end()) info->sets.push_back(p.pset);
        if (const p2m::Val* m = info->get("eCMesh_PS.MeshFileName"); m && m->is_str()) info->mesh = m->s;
        if (const p2m::Val* ic = info->get("gCItem_PS.IconImage"); ic && ic->is_str()) info->icon = ic->s;
        info->multi = p2m::contains(p2m::lower(info->group), "_multitemplates");
        info->kind = classify(*info);
        const std::string key = p2m::lower(info->name);
        if (gd->by_name_.count(key)) {
            log_debug("template name " + info->name + " appears twice (" + path + "), the later file wins");
            auto& old = gd->by_name_[key];
            *old = std::move(*info);
            continue;
        }
        gd->by_name_[key] = info.get();
        gd->templates_.push_back(std::move(info));
    }
    std::sort(gd->templates_.begin(), gd->templates_.end(),
              [](const std::unique_ptr<TemplateInfo>& a, const std::unique_ptr<TemplateInfo>& b) { return p2m::lower(a->name) < p2m::lower(b->name); });
    log_info("game data: " + std::to_string(gd->templates_.size()) + " templates");

    // ---- string tables (compiled/strings/*.tab): every table, the languages from the column names
    report(0.82f, "strings");
    std::map<std::string, size_t> lang_ix;
    for (const std::string& k : gd->compiled_.order) {
        if (!p2m::ends_with(k, ".tab")) continue;
        try {
            const p2m::TabTable tab = p2m::read_tab(gd->compiled_.files.at(k).read());
            std::vector<size_t> col_lang;  // column index (1..) -> language index
            for (size_t c = 1; c < tab.columns.size(); ++c) {
                // only "<Language>_Text" columns are texts (svms.tab and infos.tab also have StageDir, WaveFile, Voice, ... columns)
                if (!p2m::ends_with(p2m::lower(tab.columns[c]), "_text")) {
                    col_lang.push_back(SIZE_MAX);
                    continue;
                }
                const std::string lang = language_of_column(tab.columns[c]);
                auto [it, fresh] = lang_ix.emplace(lang, gd->languages_.size());
                if (fresh) gd->languages_.push_back(lang);
                col_lang.push_back(it->second);
            }
            for (const std::string& id : tab.ids) {
                auto& cells = tab.rows.at(id);
                auto& dst = gd->texts_[id];
                for (size_t c = 0; c < cells.size() && c < col_lang.size(); ++c) {
                    if (col_lang[c] == SIZE_MAX) continue;
                    if (dst.size() <= col_lang[c]) dst.resize(col_lang[c] + 1);
                    if (dst[col_lang[c]].empty()) dst[col_lang[c]] = cells[c];
                }
            }
        } catch (const std::exception& e) {
            log_warn("string table " + k + " skipped: " + e.what());
        }
    }
    log_info("game data: " + std::to_string(gd->texts_.size()) + " strings in " + std::to_string(gd->languages_.size()) + " languages");

    // ---- resources by compiled extension
    // ---- the game's quests (compiled_quests.bin, docs/RE.md section 43): one gCQuest record per quest. The quest names are the first strings of the
    // table ("<name>.xqst", in record order); each record has one LogTopic (type gCQuestLocString, payload = u16 string index of its title id in quests.tab)
    // and one Silent flag, also in record order.
    for (const std::string& k : gd->compiled_.order) {
        if (!p2m::ends_with(p2m::lower(k), "compiled_quests.bin")) continue;
        try {
            const p2m::Bytes raw = gd->compiled_.files.at(k).read();
            const p2m::Template t = p2m::parse_template(raw);
            std::vector<std::string> names;
            for (const std::string& s : t.strings)
                if (p2m::ends_with(s, ".xqst")) names.push_back(s.substr(0, s.size() - 5));
            std::vector<std::string> topics;
            const auto& st = t.strings;
            const auto li = std::find(st.begin(), st.end(), std::string("LogTopic")), ti = std::find(st.begin(), st.end(), std::string("gCQuestLocString"));
            if (li != st.end() && ti != st.end()) {
                const uint16_t hdr[3] = {static_cast<uint16_t>(li - st.begin()), static_cast<uint16_t>(ti - st.begin()), 0x1e};
                const std::string pat(reinterpret_cast<const char*>(hdr), 6);
                for (size_t at = raw.find(pat); at != std::string::npos; at = raw.find(pat, at + 1)) {
                    if (at + 12 > raw.size()) break;
                    uint32_t size = 0;
                    std::memcpy(&size, raw.data() + at + 6, 4);
                    uint16_t idx = 0xffff;
                    if (size >= 2) std::memcpy(&idx, raw.data() + at + 10, 2);
                    topics.push_back(idx < st.size() ? st[idx] : std::string());
                }
            }
            std::vector<bool> silent;
            for (const p2m::Prop& p : t.props)
                if (p.pset == "gCQuest" && p.name == "Silent") silent.push_back(p.value.k == p2m::Val::Bool && p.value.i != 0);
            const bool aligned = topics.size() == names.size() && silent.size() == names.size();
            if (!aligned) log_warn("compiled_quests.bin: " + std::to_string(names.size()) + " quests, " + std::to_string(topics.size()) + " titles, " +
                                   std::to_string(silent.size()) + " flags: titles left out");
            for (size_t i = 0; i < names.size(); ++i)
                gd->quests_.push_back({names[i], aligned ? topics[i] : std::string(), aligned && silent[i]});
        } catch (const std::exception& e) {
            log_warn(std::string("compiled_quests.bin skipped: ") + e.what());
        }
    }
    std::sort(gd->quests_.begin(), gd->quests_.end(), [](const GameQuest& a, const GameQuest& b) { return p2m::lower(a.name) < p2m::lower(b.name); });
    log_info("game data: " + std::to_string(gd->quests_.size()) + " quests");

    // ---- the shipped icons (compiled_imagelists.bin, docs/RE.md section 51): image name -> rectangle of a texture
    for (const std::string& k : gd->compiled_.order) {
        if (!p2m::ends_with(p2m::lower(k), "compiled_imagelists.bin")) continue;
        try {
            gd->images_ = ImageCatalog::parse(gd->compiled_.files.at(k).read());
            log_info("game data: " + std::to_string(gd->images_.all().size()) + " GUI images in " + std::to_string(gd->images_.collections()) + " collections");
        } catch (const std::exception& e) {
            log_warn(std::string("compiled_imagelists.bin skipped: ") + e.what());
        }
        break;
    }

    report(0.9f, "resources");
    for (const p2m::Layered* layer : {&gd->common_, &gd->compiled_}) {
        for (const std::string& k : layer->order) {
            const auto dot = k.rfind('.');
            if (dot == std::string::npos) continue;
            const std::string ext = k.substr(dot + 1);
            if (ext.size() < 3 || ext[0] != '_' || ext[1] != 'x') continue;
            const p2m::LFile& f = layer->files.at(k);
            gd->assets_[ext].push_back({p2m::bare_name(f.e->path), f.e->path, f.label});
        }
    }
    for (auto& [ext, v] : gd->assets_)
        std::sort(v.begin(), v.end(), [](const AssetEntry& a, const AssetEntry& b) { return p2m::lower(a.name) < p2m::lower(b.name); });

    // ---- worlds: common/projects/<World>/<World>.wrl
    std::set<std::string> worlds;
    for (auto& [path, f] : gd->by_path_) {
        if (!p2m::starts_with(path, "projects/") || !p2m::ends_with(path, ".wrl")) continue;
        const std::string& real = f->e->path;
        const auto slash = real.find('/');
        if (slash != std::string::npos) worlds.insert(real.substr(0, slash));
    }
    gd->worlds_.assign(worlds.begin(), worlds.end());

    gd->load_seconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    report(1.0f, "done");
    log_info("game data loaded in " + std::to_string(gd->load_seconds_).substr(0, 4) + " s");
    return gd;
}

std::string GameData::template_by_guid(const std::string& at_hex) const {
    const auto it = guid_names_.find(p2m::lower(at_hex));
    return it == guid_names_.end() ? std::string() : it->second;
}

const TemplateInfo* GameData::tpl(const std::string& name) const {
    const auto it = by_name_.find(p2m::lower(name));
    return it == by_name_.end() ? nullptr : it->second;
}

std::vector<std::string> GameData::names_of(TplKind k) const {
    std::vector<std::string> v;
    for (auto& t : templates_)
        if (t->kind == k) v.push_back(t->name);
    return v;
}

std::string GameData::text(const std::string& id, const std::string& language) const {
    const auto it = texts_.find(id);
    if (it == texts_.end()) return {};
    auto pick = [&](const std::string& lang) -> std::string {
        for (size_t i = 0; i < languages_.size() && i < it->second.size(); ++i)
            if (languages_[i] == lang) return it->second[i];
        return {};
    };
    std::string s = pick(language);
    if (s.empty() && language != "English") s = pick("English");
    return s;
}

const std::vector<AssetEntry>& GameData::assets(const std::string& ext) const {
    static const std::vector<AssetEntry> none;
    const auto it = assets_.find(p2m::lower(ext));
    return it == assets_.end() ? none : it->second;
}

bool GameData::exists(const std::string& bare_name) const {
    const std::string k = p2m::lower(bare_name);
    return common_.find(k) || compiled_.find(k);
}

p2m::Bytes GameData::read(const std::string& bare_name) const {
    const std::string k = p2m::lower(bare_name);
    if (const p2m::LFile* f = common_.find(k)) return f->read();
    if (const p2m::LFile* f = compiled_.find(k)) return f->read();
    throw p2m::Error("no file " + bare_name + " in the game archives");
}

std::vector<std::string> GameData::paths_with_prefix(const std::string& prefix) const {
    const std::string p = p2m::lower(prefix);
    std::vector<std::string> v;
    for (auto& [path, f] : by_path_)
        if (p2m::starts_with(path, p)) v.push_back(archive_path(*f));
    std::sort(v.begin(), v.end());
    return v;
}

p2m::Bytes GameData::read_path(const std::string& path) const {
    const auto it = by_path_.find(p2m::lower(path));
    if (it == by_path_.end()) throw p2m::Error("no file " + path + " in the game archives");
    return it->second->read();
}

std::string GameData::enum_name(const std::string& type, const p2m::Val& v) const {
    auto n = ctx_.enum_name(type, v);
    return n ? *n : std::string();
}

const GameQuest* GameData::quest(const std::string& name) const {
    const std::string low = p2m::lower(name);
    for (const GameQuest& q : quests_)
        if (p2m::lower(q.name) == low) return &q;
    return nullptr;
}

}  // namespace mk
