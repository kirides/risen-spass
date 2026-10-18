// Internal model shared by the converter stages (items, strings, assets, dialogue, world, report). Not a public API.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "pack2mod/convert.h"
#include "pack2mod/library.h"
#include "pack2mod/lua_emit.h"
#include "pack2mod/pak.h"
#include "pack2mod/tple.h"

namespace p2m {

// ---------------------------------------------------------------------------------------------- archives

struct LFile {
    std::string label;  // "common/templates.p01"
    Pak* pak = nullptr;
    const PakEntry* e = nullptr;
    Bytes read() const { return pak->read(*e); }
};

// bare lower-case file name -> newest layer; history keeps every layer's version in order
struct Layered {
    std::vector<std::string> order;  // keys in first-insertion order
    std::unordered_map<std::string, LFile> files;
    std::unordered_map<std::string, std::vector<LFile>> history;
    void add(const std::string& label, Pak* pak);
    const LFile* find(const std::string& key) const {
        const auto it = files.find(key);
        return it == files.end() ? nullptr : &it->second;
    }
    std::vector<std::string> sorted_keys() const;
};

struct ArcRef {
    std::string path, kind, base;
    int number = 0;  // 0..99 for .pNN, -1 for .pak
    std::string label() const;
};

// ---------------------------------------------------------------------------------------------- properties

// An ordered property map "Set.Name" -> value, with the property types
struct Flat {
    std::vector<std::string> keys;
    std::vector<Val> vals;
    std::vector<std::string> types;
    std::unordered_map<std::string, size_t> ix;

    const Val* get(const std::string& k) const {
        const auto it = ix.find(k);
        return it == ix.end() ? nullptr : &vals[it->second];
    }
    const std::string* type_of(const std::string& k) const {
        const auto it = ix.find(k);
        return it == ix.end() ? nullptr : &types[it->second];
    }
    bool has(const std::string& k) const { return ix.count(k) != 0; }
    void put(const std::string& k, Val v, const std::string& type);
    size_t size() const { return keys.size(); }
};

struct Elem {  // one element of an array property: field -> value
    std::vector<std::pair<std::string, Val>> f;
    const Val* get(const std::string& k) const {
        for (const auto& p : f)
            if (p.first == k) return &p.second;
        return nullptr;
    }
    bool has(const std::string& k) const { return get(k) != nullptr; }
};

// ---------------------------------------------------------------------------------------------- context

struct Unconverted {
    std::string kind, name, reason, needs;
};

struct Profile {
    std::map<std::string, std::pair<std::string, std::string>> recipe_result_fix;  // template -> (fallback, note)
    std::map<std::string, std::string> recipe_ingredient_fix;                      // template -> replacement for an unresolved ingredient
    std::map<std::string, std::set<std::string>> string_only_rows;                 // table -> the only rows applied
    std::set<std::string> string_changed_with_combat;                              // tables whose changed rows are applied when a combat table is used
    std::set<std::string> string_changed_all;                                      // tables whose changed rows are all applied
    struct Group {
        std::string name, about;
        std::vector<std::string> prefix, exclude_prefix;
    };
    std::vector<Group> asset_groups;
    std::string combat_about, weather_name, weather_about;
    std::string id, name;
    bool loaded = false;
};

struct Ctx {
    std::vector<std::string> warn;
    std::vector<std::string> notes;  // observations for the report that are not problems
    std::vector<Unconverted> unconverted;
    std::map<std::string, std::map<int, std::string>> enums;
    std::vector<std::string> skills;  // index = gESkill number, "" = unknown
    std::map<std::string, int> stats;
    // --classic: per category how many differences of the mod from the baseline are the classic game's own (noise) and how many the author's (docs/PACK2MOD.md)
    std::map<std::string, int> classic;
    std::vector<std::string> classic_notes;
    Profile profile;

    Ctx();
    std::string skill(const Val* n) const;                                // "" when unknown
    std::optional<std::string> enum_name(const std::string& typ, const Val& v) const;  // "gEItemUseType_Axe"
    void bad(const std::string& kind, const std::string& name, const std::string& reason, const std::string& needs);
};

struct Data {
    Layered base_common, base_compiled;
    Layered classic_common, classic_compiled;  // --classic: the game the mod was made against (empty without)
    bool has_classic = false;
    std::unordered_map<std::string, Template> cla;  // the classic game's templates by entity name
    Layered mod_common, mod_compiled;
    std::vector<std::string> van_order;  // internal template names in first-insertion order
    std::unordered_map<std::string, Template> van;
    std::unordered_map<std::string, std::string> vdir;  // template -> directory path inside its baseline archive
    std::vector<std::string> mod_order;
    std::unordered_map<std::string, Template> mod;
    std::unordered_map<std::string, std::string> mlabel;
    std::unordered_map<std::string, std::string> guid;  // "@hex" -> template name
    std::vector<std::unique_ptr<Pak>> paks;

    Layered& base(const std::string& kind) { return kind == "common" ? base_common : base_compiled; }
    Layered& classic(const std::string& kind) { return kind == "common" ? classic_common : classic_compiled; }
    // what the mod's changes are measured against: the classic game when given, else the baseline
    Layered& reference(const std::string& kind) { return has_classic ? classic(kind) : base(kind); }
    Layered& layers(const std::string& kind) { return kind == "common" ? mod_common : mod_compiled; }
    std::vector<std::string> mod_sorted() const;
    std::vector<std::string> van_sorted() const;
};

// the shared notion of "all names the mod or the game knows" (lower case -> spelling)
std::unordered_map<std::string, std::string> known_names(const Data& d);

Flat flat_typed(const Template& t, const std::unordered_map<std::string, std::string>& guid);
// the baseline's properties with the author's changes (mod != classic) of the mod applied: see items.cpp
Flat rebase_on_baseline(const Flat& enh, const Flat& cla, const Flat& mod);
std::vector<Elem> arrays_of(const Flat& P, const std::string& prefix);
LVal to_lval(const Val& v);

// ---------------------------------------------------------------------------------------------- stages

struct RecipeInfo {
    std::optional<std::string> station;
    Val craft;
    std::vector<std::pair<Val, Val>> ingredients;  // (template, amount)
    std::vector<std::pair<std::string, Val>> skills;
    Val result, amount;
};
LVal recipe_lua(const RecipeInfo& r);

struct ItemsResult {
    LVal items = LVal::dict(true), patches = LVal::dict(true);
    std::vector<std::pair<std::string, RecipeInfo>> recipes_new;  // existing items that become recipes
};
ItemsResult gen_items(Ctx& ctx, Data& d);
struct NewResult {
    LVal items = LVal::dict(true), recipes = LVal::dict(true);
};
NewResult gen_new(Ctx& ctx, Data& d);

struct StringsResult {
    std::map<std::string, std::map<std::string, std::string>> rows;  // language -> id -> text
    std::vector<std::string> languages;                              // in table column order, English first when present
    std::map<std::string, int> counts;
    std::vector<std::string> not_applied;  // "table: N changed rows ..." lines for the report
};
StringsResult gen_strings(Ctx& ctx, Data& d, const std::set<std::string>& wanted, bool with_combat, bool all_rows);
void write_strings(const StringsResult& s, const std::string& out_dir);

struct AssetsResult {
    std::vector<std::string> added, replaced, duplicates;
    int identical = 0;
    long long bytes = 0;
};
AssetsResult gen_assets(Ctx& ctx, Data& d, const std::string& out_dir, bool copy);

CombatResult gen_combat(Ctx& ctx, Data& d, const std::string& out_dir);  // writes variants/combat_*.bin when out_dir is not empty
std::vector<WeatherFile> gen_weather(Ctx& ctx, Data& d, const std::string& out_dir);

// maps a template name the mod misspelled to the one template that fits (reported as a warning); nullopt when none or several fit
std::optional<std::string> resolve_name(Ctx& ctx, const std::unordered_map<std::string, std::string>& known, const std::string& item, const std::string& where);

struct DialogResult {
    LVal data = LVal::dict();
    LVal changes = LVal::list();  // --classic: property edits of shipped infos (dialog.lua `changes`)
    int xp = 0, trade_entries = 0, infos_other = 0, property_infos = 0;
    std::vector<std::string> notes;
};
DialogResult gen_dialog(Ctx& ctx, Data& d);

struct WorldResult {
    LVal data = LVal::dict();
    int containers = 0, equipment = 0, teachers = 0;
};
WorldResult gen_world(Ctx& ctx, Data& d);

}  // namespace p2m
