#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/properties.h"
#include "game/template_object.h"

// Item balancing: native access to the two array properties of gCItem_PS that hold an item's "conditions" and "effects"
// (docs/RE.md section 23).
//
//   RequiredSkills  bTObjArray<gCSkillValue>   +0x70 of the property set   equip condition: skill >= amount
//   ModifySkills    bTObjArray<gCModifySkill>  +0x80 of the property set   applied to the wearer when the item is equipped/used
//
// bTObjArray is { element data*, int count, int capacity }; the elements are stored inline (stride 0x20 / 0x30). An element is a
// bCObjectBase with the properties Skill (enum gESkill), Amount (int) and, for gCModifySkill, Modifier (enum gESkillModifier), so the
// ordinary accessor mechanism reads and writes them, enum names included. Writing builds the elements with the engine's own
// constructors in a scratch buffer and hands a temporary array to gCItem_PS::SetRequiredSkills/SetModifySkills (the engine copies it).

namespace spass::game {
namespace {

struct ArrayKind {
    const char* name;      // the Lua-side kind: "required" | "modifiers"
    const char* property;  // the property in gCItem_PS (documentation)
    std::size_t offset;    // of the bTObjArray inside the property set
    std::size_t stride;    // sizeof(element)
    bool modifier;         // gCModifySkill (has a Modifier member)
};

constexpr ArrayKind kRequired{"required", "RequiredSkills", 0x70, 0x20, false};
constexpr ArrayKind kModifiers{"modifiers", "ModifySkills", 0x80, 0x30, true};

const ArrayKind* kindByName(const std::string& n) {
    if (n == kRequired.name) {
        return &kRequired;
    }
    if (n == kModifiers.name) {
        return &kModifiers;
    }
    return nullptr;
}

struct Entry {
    std::string skill;     // "gESkill_Stat_STR"
    int amount = 0;
    std::string modifier;  // "gESkillModifier_AddValue" (modifiers only)
};

// The engine's bTObjArray header (verified in the copy routine FUN_18050b1a0 and ApplySkillModifyValues).
struct ArrayHeader {
    void* data = nullptr;
    std::int32_t count = 0;
    std::int32_t capacity = 0;
};

using Two = std::tuple<sol::object, sol::object>;
Two okResult(sol::state_view lua, sol::object value) { return {std::move(value), sol::make_object(lua, sol::lua_nil)}; }
Two failResult(sol::state_view lua, const std::string& reason) {
    return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, reason)};
}

// The gCItem_PS of a template (by name) or of an item entity.
void* itemSetOf(const sol::object& target, std::string& error) {
    if (target.get_type() == sol::type::string) {
        return templatePropertySet(target.as<std::string>(), "gCItem_PS", error);
    }
    if (target.is<LuaEntity>()) {
        const LuaEntity& e = target.as<LuaEntity>();
        void* entity = ptr(e);
        if (entity == nullptr) {
            error = "entity is gone";
            return nullptr;
        }
        const Symbols& s = sym();
        if (s.ent_get_property_set == nullptr) {
            error = "engine function eCEntity::GetPropertySet is unavailable";
            return nullptr;
        }
        void* ps = nullptr;
        const BString cls("gCItem_PS");
        if (!guarded([&] { ps = s.ent_get_property_set(entity, cls.get()); })) {
            error = "the engine raised an exception (not an entity?)";
            return nullptr;
        }
        if (ps == nullptr) {
            error = "entity is not an item (no gCItem_PS)";
        }
        return ps;
    }
    error = "expected a template name or an item entity";
    return nullptr;
}

// Reads the array of `kind` from a gCItem_PS.
bool readArray(void* itemPs, const ArrayKind& kind, std::vector<Entry>& out, std::string& error) {
    bool ok = false;
    const bool ran = guarded([&] {
        const char* base = static_cast<const char*>(itemPs) + kind.offset;
        const ArrayHeader* hdr = reinterpret_cast<const ArrayHeader*>(base);
        if (hdr->count < 0 || hdr->count > 256 || (hdr->count > 0 && hdr->data == nullptr)) {
            error = "the array looks corrupt (count " + std::to_string(hdr->count) + ")";
            return;
        }
        for (int i = 0; i < hdr->count; ++i) {
            void* elem = static_cast<char*>(hdr->data) + static_cast<std::size_t>(i) * kind.stride;
            Entry e;
            const auto skill = propertyGet(elem, "Skill");
            const auto amount = propertyGet(elem, "Amount");
            if (!skill || !amount) {
                error = std::string("array element ") + std::to_string(i) + " has no Skill/Amount property";
                return;
            }
            e.skill = *skill;
            e.amount = std::atoi(amount->c_str());
            if (kind.modifier) {
                const auto mod = propertyGet(elem, "Modifier");
                if (!mod) {
                    error = "array element has no Modifier property";
                    return;
                }
                e.modifier = *mod;
            }
            out.push_back(std::move(e));
        }
        ok = true;
    });
    if (!ran) {
        error = "the engine raised an exception while reading the array";
        return false;
    }
    return ok;
}

// Replaces the array of `kind` in a gCItem_PS. Everything is validated (names round-trip through the engine's enum parser)
// before the item is touched.
bool writeArray(void* itemPs, const ArrayKind& kind, const std::vector<Entry>& entries, std::string& error) {
    const Symbols& s = sym();
    auto ctor = kind.modifier ? s.modifyskill_ctor : s.skillvalue_ctor;
    auto dtor = kind.modifier ? s.modifyskill_dtor : s.skillvalue_dtor;
    auto setter = kind.modifier ? s.item_set_modify_skills : s.item_set_required_skills;
    if (ctor == nullptr || dtor == nullptr || setter == nullptr) {
        error = "engine functions for the skill arrays are unavailable";
        return false;
    }
    if (entries.size() > 64) {
        error = "too many entries (max 64)";
        return false;
    }
    alignas(16) unsigned char scratch[64 * 0x30] = {};  // up to 64 inline elements of either kind
    unsigned char* const buffer = scratch;
    std::size_t built = 0;
    bool ok = false;
    const bool ran = guarded([&] {
        for (const Entry& e : entries) {
            void* elem = buffer + built * kind.stride;
            ctor(elem);
            ++built;
            propertySet(elem, "Skill", e.skill);
            propertySet(elem, "Amount", std::to_string(e.amount));
            const auto skill = propertyGet(elem, "Skill");
            if (!skill || *skill != e.skill) {
                error = "unknown skill '" + e.skill + "'";
                return;
            }
            if (kind.modifier) {
                propertySet(elem, "Modifier", e.modifier);
                const auto mod = propertyGet(elem, "Modifier");
                if (!mod || *mod != e.modifier) {
                    error = "unknown skill modifier '" + e.modifier + "'";
                    return;
                }
            }
            const auto amount = propertyGet(elem, "Amount");
            if (!amount || std::atoi(amount->c_str()) != e.amount) {
                error = "amount " + std::to_string(e.amount) + " was not accepted";
                return;
            }
        }
        ArrayHeader hdr;
        hdr.data = buffer;
        hdr.count = static_cast<std::int32_t>(entries.size());
        hdr.capacity = hdr.count;
        setter(itemPs, &hdr);
        ok = true;
    });
    // destroy what was built (also after an exception), whatever happened
    guarded([&] {
        for (std::size_t i = built; i > 0; --i) {
            dtor(buffer + (i - 1) * kind.stride);
        }
    });
    if (!ran && error.empty()) {
        error = "the engine raised an exception while writing the array";
    }
    return ok;
}

// PSSkills holder (an Entity): the entity pointer must have a gCSkills_PS.
void* skillsSetOf(void* entity) {
    const Symbols& s = sym();
    void* ps = nullptr;
    if (s.ent_get_property_set != nullptr) {
        const BString cls("gCSkills_PS");
        guarded([&] { ps = s.ent_get_property_set(entity, cls.get()); });
    }
    return ps;
}

// gESkill value of an enum name ("gESkill_Stat_STR"), found by letting a scratch element parse it (the raw int sits at +0x10).
std::map<std::string, int>& skillIdCache() {
    static std::map<std::string, int> cache;
    return cache;
}

sol::optional<int> skillId(const std::string& name) {
    auto& cache = skillIdCache();
    if (const auto it = cache.find(name); it != cache.end()) {
        return it->second;
    }
    const Symbols& s = sym();
    if (s.skillvalue_ctor == nullptr || s.skillvalue_dtor == nullptr) {
        return sol::nullopt;
    }
    alignas(16) unsigned char elem[0x40] = {};
    sol::optional<int> id;
    bool built = false;
    guarded([&] {
        s.skillvalue_ctor(elem);
        built = true;
        propertySet(elem, "Skill", name);
        const auto back = propertyGet(elem, "Skill");
        if (back && *back == name) {
            id = *reinterpret_cast<int*>(elem + 0x10);
        }
    });
    if (built) {
        guarded([&] { s.skillvalue_dtor(elem); });
    }
    if (id) {
        cache[name] = *id;
    }
    return id;
}

}  // namespace

// ---- the Lua part --------------------------------------------------------------------------------------------------------------

namespace {

// Friendly skill names, requirement/modifier tables, risen.balance.apply and its section registry. Pure Lua on top of
// risen.item.skill_array/set_skill_array and risen.template.patch, so the unit tests can run it against mocks.
const char* kBalanceChunk = R"LUA(
local risen = ...
local item = risen.item
local balance = risen.balance or {}
risen.balance = balance
local function tr(...) local t = risen.trace if t and t.enabled("balance") then t("balance", ...) end end

-- gESkill enum names (docs/RE.md section 23): friendly name -> engine suffix (gESkill_<suffix>), in the engine's order
local SKILLS = {
    { "Strength", "Stat_STR" }, { "Dexterity", "Stat_DEX" }, { "Intelligence", "Stat_INT" },
    { "MaxHitPoints", "Stat_HP" }, { "MaxManaPoints", "Stat_MP" }, { "LearnPoints", "Stat_LP" },
    { "Experience", "Stat_XP" }, { "Level", "Stat_LV" }, { "HitPoints", "Atrib_HP" }, { "ManaPoints", "Atrib_MP" },
    { "Axe", "Combat_Axe" }, { "Sword", "Combat_Sword" }, { "Staff", "Combat_Staff" }, { "Bow", "Combat_Bow" }, { "CrossBow", "Combat_CrossBow" },
    { "ProtEdge", "Prot_Edge" }, { "ProtBlunt", "Prot_Blunt" }, { "ProtPoint", "Prot_Point" },
    { "ProtFire", "Prot_Fire" }, { "ProtIce", "Prot_Ice" }, { "ProtMagic", "Prot_Magic" },
    { "Missile", "Magic_Missile" }, { "Frost", "Magic_Frost" }, { "Fireball", "Magic_Fireball" }, { "Circle", "Magic_Circle" },
    { "Scribe", "Misc_Scribe" }, { "Trophy", "Misc_Trophy" }, { "Acrobat", "Misc_Acrobat" }, { "Pickpocket", "Misc_Pickpocket" },
    { "Lockpick", "Misc_Lockpick" }, { "Sneak", "Misc_Sneak" }, { "Mining", "Misc_Mining" }, { "Smith", "Misc_Smith" }, { "Alchemy", "Misc_Alchemy" },
}
local MODES = { { "add", "AddValue" }, { "set", "SetToValue" }, { "max", "SetToMax" }, { "percent", "AddPercentOfMax" } }

local by_friendly, by_suffix, skill_names = {}, {}, {}
for _, s in ipairs(SKILLS) do
    by_friendly[s[1]] = s[2]
    by_suffix[s[2]] = s[1]
    skill_names[#skill_names + 1] = s[1]
end
local mode_by_name, mode_by_engine, mode_names = {}, {}, {}
for _, m in ipairs(MODES) do
    mode_by_name[m[1]] = "gESkillModifier_" .. m[2]
    mode_by_engine["gESkillModifier_" .. m[2]] = m[1]
    mode_names[#mode_names + 1] = m[1]
end
local skill_list_text = table.concat(skill_names, ", ")
local mode_list_text = table.concat(mode_names, ", ")

-- engine enum name for a friendly name ("Strength"), an engine suffix ("Stat_STR") or the full enum name; nil if unknown
local function engine_skill(name)
    if type(name) ~= "string" then return nil end
    local suffix = by_friendly[name]
    if not suffix then
        local bare = name:match("^gESkill_(.+)$") or name
        if by_suffix[bare] then suffix = bare end
    end
    return suffix and ("gESkill_" .. suffix) or nil
end
local function friendly_skill(engine)
    local suffix = type(engine) == "string" and engine:match("^gESkill_(.+)$")
    return suffix and by_suffix[suffix] or engine
end
local function engine_mode(name)
    if name == nil then return mode_by_name.add end
    if type(name) ~= "string" then return nil end
    return mode_by_name[name] or (mode_by_engine[name] and name) or nil
end

-- risen.balance.skills() / modes() -> the valid friendly names
function risen.balance.skills() local out = {} for i, n in ipairs(skill_names) do out[i] = n end return out end
function risen.balance.modes() local out = {} for i, n in ipairs(mode_names) do out[i] = n end return out end

local function sorted_keys(t)
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    return keys
end

-- { Strength = 15, ... } | { { skill = "Strength", amount = 15 [, mode = "add"] }, ... } -> engine entries | nil, problems
-- Map values may also be { amount = 15, mode = "set" }. Nothing is touched here.
local function to_entries(spec, what, with_modes)
    local entries, problems = {}, {}
    local function add(skill, amount, mode)
        local engine = engine_skill(skill)
        if not engine then
            problems[#problems + 1] = ("%s: unknown skill '%s' (valid: %s)"):format(what, tostring(skill), skill_list_text)
            return
        end
        if type(amount) ~= "number" or amount ~= math.floor(amount) or math.abs(amount) >= 2 ^ 31 then
            problems[#problems + 1] = ("%s.%s: the amount must be an integer, got %s"):format(what, tostring(skill), tostring(amount))
            return
        end
        local entry = { skill = engine, amount = amount }
        if with_modes then
            entry.modifier = engine_mode(mode)
            if not entry.modifier then
                problems[#problems + 1] = ("%s.%s: unknown mode '%s' (valid: %s)"):format(what, tostring(skill), tostring(mode), mode_list_text)
                return
            end
        end
        entries[#entries + 1] = entry
    end
    if type(spec) ~= "table" then
        return nil, { what .. ": expected a table { Skill = amount, ... } or a list of { skill =, amount = }, got " .. type(spec) }
    end
    if spec[1] ~= nil then
        for i, e in ipairs(spec) do
            if type(e) ~= "table" then
                problems[#problems + 1] = ("%s[%d]: expected { skill =, amount = }"):format(what, i)
            else
                add(e.skill, e.amount, e.mode)
            end
        end
    else
        for _, k in ipairs(sorted_keys(spec)) do
            local v = spec[k]
            if type(v) == "table" then add(k, v.amount, v.mode) else add(k, v, nil) end
        end
    end
    if #problems > 0 then return nil, problems end
    return entries
end

-- a template name, a Template handle or an Entity -> what the native functions accept
local function target_of(x)
    if type(x) == "string" then return x end
    local name = tostring(x):match("^Template%((.*)%)$")
    return name or x
end

-- writes `entries` into the template (name) or the item entity, and with opts.instances into every live entity of the template
local function write_array(x, kind, entries, opts)
    local target = target_of(x)
    local ok, why = item.set_skill_array(target, kind, entries)
    if not ok then return nil, why end
    local touched = 0
    if opts and opts.instances and type(target) == "string" then
        local tpl = risen.template.get(target)
        for _, e in ipairs(tpl and tpl:instances(1000000) or {}) do
            if item.set_skill_array(e, kind, entries) then touched = touched + 1 end
        end
    end
    return true, touched
end

-- risen.item.skill_total(character, "Strength") -> total, base | nil, reason   (also Entity:skill_total(name))
function risen.item.skill_total(who, name)
    local engine = engine_skill(name)
    if not engine then return nil, ("unknown skill '%s' (valid: %s)"):format(tostring(name), skill_list_text) end
    return item.skill_total_raw(who, engine)
end

-- risen.item.requirements(item) -> { Strength = 10, ... } | nil, reason     (item: template name, Template or item Entity)
function risen.item.requirements(x)
    local list, why = item.skill_array(target_of(x), "required")
    if not list then return nil, why end
    local out = {}
    for _, e in ipairs(list) do
        local k = friendly_skill(e.skill)
        out[k] = math.max(out[k] or e.amount, e.amount)
    end
    return out
end

-- risen.item.set_requirements(item, { Strength = 15 } [, { instances = true }]) -> true [, live entities written] | nil, reason
-- Replaces the whole list ({} removes every requirement).
function risen.item.set_requirements(x, spec, opts)
    local entries, problems = to_entries(spec, "requirements", false)
    if not entries then return nil, table.concat(problems, "; ") end
    return write_array(x, "required", entries, opts)
end

-- risen.item.modifiers(item) -> { { skill = "ProtEdge", amount = 20, mode = "add" }, ... } | nil, reason
function risen.item.modifiers(x)
    local list, why = item.skill_array(target_of(x), "modifiers")
    if not list then return nil, why end
    local out = {}
    for i, e in ipairs(list) do
        out[i] = { skill = friendly_skill(e.skill), amount = e.amount, mode = mode_by_engine[e.modifier] or e.modifier }
    end
    return out
end

-- risen.item.set_modifiers(item, { ProtEdge = 20 } | { { skill =, amount =, mode = } } [, { instances = true }]) -> true [, n] | nil, reason
function risen.item.set_modifiers(x, spec, opts)
    local entries, problems = to_entries(spec, "modifiers", true)
    if not entries then return nil, table.concat(problems, "; ") end
    return write_array(x, "modifiers", entries, opts)
end

-- ---- risen.balance.apply -------------------------------------------------------------------------------------------------------

-- Section handlers: balance.sections[name](data, opts) -> jobs, problems. A job is { label = "...", run = function(opts) -> ok, err, touched }.
-- Later milestones register their own sections ("npcs", "spells", ...) with risen.balance.register_section.
balance.sections = balance.sections or {}

function risen.balance.register_section(name, handler)
    if type(name) ~= "string" or type(handler) ~= "function" then error("register_section(name, handler)", 2) end
    balance.sections[name] = handler
end

function risen.balance.section_names()
    return sorted_keys(balance.sections)
end

-- keys of one entry of `items`
local ITEM_PROPS = {
    value = { "gCItem_PS", "GoldValue" },
    use_type = { "gCItem_PS", "UseType" },
    category = { "gCItem_PS", "Category" },
    damage = { "gCDamage_PS", "DamageAmount" },
    mana_cost = { "gCCastInfo_PS", "ManaCost" },
}
local ITEM_KEYS = { "category", "damage", "mana_cost", "modifies", "properties", "requires", "use_type", "value" }

local function items_section(data, _)
    local jobs, problems = {}, {}
    if type(data) ~= "table" then return jobs, { "items: expected a table { TemplateName = { ... } }" } end
    for _, name in ipairs(sorted_keys(data)) do
        local entry = data[name]
        local function bad(msg) problems[#problems + 1] = ("items.%s: %s"):format(tostring(name), msg) end
        if type(name) ~= "string" or type(entry) ~= "table" then
            bad("expected a table of settings")
        else
            local known = true
            for key in pairs(entry) do
                local ok = false
                for _, k in ipairs(ITEM_KEYS) do if k == key then ok = true end end
                if not ok then
                    known = false
                    bad(("unknown setting '%s' (valid: %s)"):format(tostring(key), table.concat(ITEM_KEYS, ", ")))
                end
            end
            local tpl, why = risen.template.get(name)
            local current = tpl and item.skill_array(name, "required")
            if not tpl then
                bad((why or "unknown template") .. " (names: docs/TEMPLATES.md, risen.template.list('Items'))")
            elseif not current then
                bad("not an item template (no gCItem_PS)")
            elseif known then
                for _, key in ipairs({ "value", "damage", "mana_cost" }) do
                    local v = entry[key]
                    if v ~= nil and not (type(v) == "number" and v == math.floor(v) and v >= 0 and v < 2 ^ 31) then
                        bad(("%s must be a non-negative integer, got %s"):format(key, tostring(v)))
                    end
                end
                for _, key in ipairs({ "use_type", "category" }) do
                    if entry[key] ~= nil and type(entry[key]) ~= "string" then bad(("%s must be a string (an enum name), got %s"):format(key, type(entry[key]))) end
                end
                -- plain properties: validated by a dry run of risen.template.patch
                local sets, any = {}, false
                for key, loc in pairs(ITEM_PROPS) do
                    if entry[key] ~= nil then
                        sets[loc[1]] = sets[loc[1]] or {}
                        sets[loc[1]][loc[2]] = entry[key]
                        any = true
                    end
                end
                if entry.properties ~= nil then
                    if type(entry.properties) ~= "table" then
                        bad("properties must be a table { PropertySet = { Property = value } }")
                    else
                        for set, props in pairs(entry.properties) do
                            if type(props) == "table" then
                                sets[set] = sets[set] or {}
                                for p, v in pairs(props) do sets[set][p] = v end
                                any = true
                            else
                                bad(("properties.%s must be a table"):format(tostring(set)))
                            end
                        end
                    end
                end
                if any then
                    local ok, err = risen.template.patch(name, sets, { dry_run = true })
                    if not ok then bad(tostring(err)) end
                end
                local req, mod
                if entry.requires ~= nil then
                    local e, p = to_entries(entry.requires, "requires", false)
                    if e then req = e else for _, m in ipairs(p) do bad(m) end end
                end
                if entry.modifies ~= nil then
                    local e, p = to_entries(entry.modifies, "modifies", true)
                    if e then mod = e else for _, m in ipairs(p) do bad(m) end end
                end
                local labels = {}
                for _, key in ipairs(ITEM_KEYS) do if entry[key] ~= nil then labels[#labels + 1] = key end end
                jobs[#jobs + 1] = {
                    label = ("%s (%s)"):format(name, table.concat(labels, ", ")),
                    run = function(opts)
                        local touched = 0
                        if any then
                            local ok, err, n = risen.template.patch(name, sets, { instances = opts.instances })
                            if not ok then return nil, err end
                            touched = touched + (n or 0)
                        end
                        if req then
                            local ok, err = write_array(name, "required", req, opts)
                            if not ok then return nil, err end
                            touched = touched + (err or 0)
                        end
                        if mod then
                            local ok, err = write_array(name, "modifiers", mod, opts)
                            if not ok then return nil, err end
                            touched = touched + (err or 0)
                        end
                        return true, nil, touched
                    end,
                }
            end
        end
    end
    return jobs, problems
end
balance.sections.items = items_section

-- risen.balance.apply(spec [, { instances = true, dry_run = false }]) -> report | nil, reason, problems
-- spec = { items = { It_1H_Sword = { value = 120, requires = { Strength = 15 }, modifies = { ProtEdge = 5 }, damage = 20,
--                                    properties = { gCItem_PS = { ... } } } } }
-- Every section is validated before anything is written (nil + a joined reason + the list of problems when something is wrong).
-- report = { ok, applied = { "label", ... }, failed = { "label: reason", ... }, values = jobs run, instances = live entities written, dry_run }.
-- With `instances` (default true) the values are also written into the live entities of each template (items on the ground, in containers).
function risen.balance.apply(spec, opts)
    opts = opts or {}
    if type(spec) ~= "table" then return nil, "risen.balance.apply: expected a table { items = { ... } }", {} end
    local instances = opts.instances ~= false
    local jobs, problems = {}, {}
    for _, section in ipairs(sorted_keys(spec)) do
        local handler = balance.sections[section]
        if not handler then
            problems[#problems + 1] = ("unknown section '%s' (valid: %s)"):format(tostring(section), table.concat(balance.section_names(), ", "))
        else
            local j, p = handler(spec[section], opts)
            for _, job in ipairs(j or {}) do jobs[#jobs + 1] = job end
            for _, msg in ipairs(p or {}) do problems[#problems + 1] = msg end
        end
    end
    if #problems > 0 then return nil, table.concat(problems, "; "), problems end
    local report = { ok = true, applied = {}, failed = {}, values = 0, instances = 0, dry_run = opts.dry_run and true or false }
    if opts.dry_run then
        for _, job in ipairs(jobs) do report.applied[#report.applied + 1] = job.label end
        return report
    end
    for _, job in ipairs(jobs) do
        local ok, err, touched = job.run({ instances = instances })
        tr("job", job.label, ok and "ok" or ("FAILED: " .. tostring(err)), "live entities written: " .. tostring(touched or 0), instances and "(instances)" or "(templates only)")
        if ok then
            report.applied[#report.applied + 1] = job.label
            report.values = report.values + 1
            report.instances = report.instances + (touched or 0)
        else
            report.ok = false
            report.failed[#report.failed + 1] = job.label .. ": " .. tostring(err)
        end
    end
    return report
end
)LUA";

// The per-mod part: risen.balance.install registers handlers on behalf of the calling mod.
const char* kBalanceModChunk = R"LUA(
local api = ...
local shared = api.balance
local b = setmetatable({}, { __index = shared })
api.balance = b
local installed = setmetatable({}, { __mode = "k" })

-- risen.balance.install(spec [, { instances = true, on_applied = function(when, report) end }]) -> true | false (already installed)
-- Applies `spec` (see risen.balance.apply) at the right moments and logs a summary: on the first `tick` (the earliest moment templates
-- resolve; covers a savegame loaded from the menu) into the templates, and on every `session.start` (new game or load) again with the
-- live entities of each template, because a world keeps its own copies of the values (docs/RE.md section 22).
function b.install(spec, opts)
    if type(spec) ~= "table" then error("risen.balance.install: expected a spec table { items = { ... } }", 2) end
    if installed[spec] then return false end
    installed[spec] = true
    opts = opts or {}
    if spec.new_spells ~= nil and api.spell and api.spell.enable_overrides then api.spell.enable_overrides() end   -- per-shot numbers of new projectile spells
    local with_instances = opts.instances ~= false
    local on_applied = opts.on_applied
    local log = api.log
    local function run(instances, when)
        local report, why, problems = shared.apply(spec, { instances = instances })
        if not report then
            for _, p in ipairs(problems or { why }) do log.error("balance: " .. tostring(p)) end
            return
        end
        log.info(("balance: %s: applied %d of %d entries%s"):format(when, #report.applied, #report.applied + #report.failed,
            instances and (", %d live entities written"):format(report.instances) or ""))
        for _, f in ipairs(report.failed) do log.warn("balance: failed " .. f) end
        if type(on_applied) == "function" then
            local ok, err = pcall(on_applied, when, report)
            if not ok then log.error("balance: on_applied: " .. tostring(err)) end
        end
    end
    local first = true
    api.on("tick", function()
        if not first then return end
        first = false
        run(false, "first tick")
    end)
    api.on("session.start", function(mode)
        run(with_instances, "session.start (" .. tostring(mode) .. ")")
    end)
    return true
end
)LUA";

bool runChunk(sol::state_view lua, const char* chunk, const char* name, sol::object arg) {
    sol::load_result loaded = lua.load(chunk, name);
    if (!loaded.valid()) {
        spdlog::error("lua: {} failed to load: {}", name, loaded.get<sol::error>().what());
        return false;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(arg);
    if (!res.valid()) {
        spdlog::error("lua: {} failed to run: {}", name, res.get<sol::error>().what());
        return false;
    }
    return true;
}

std::vector<sol::object> collect(sol::variadic_args va) {
    std::vector<sol::object> out;
    for (auto a : va) {
        out.push_back(sol::object(a));
    }
    return out;
}

// Entity:requirements() etc. forward to the Lua functions of risen.item (kept in the registry: `risen` is not a global).
sol::variadic_results forward(sol::this_state ts, const char* fn, sol::object self, const std::vector<sol::object>& extra = {}) {
    sol::state_view L(ts);
    sol::variadic_results out;
    sol::object holder = L.registry()["spass.item"];
    if (!holder.is<sol::table>()) {
        out.push_back(sol::make_object(L, sol::lua_nil));
        out.push_back(sol::make_object(L, std::string("the item API is not available")));
        return out;
    }
    sol::protected_function f = holder.as<sol::table>()[fn];
    std::vector<sol::object> args;
    args.push_back(self);
    args.insert(args.end(), extra.begin(), extra.end());
    sol::protected_function_result r = f(sol::as_args(args));
    if (!r.valid()) {
        out.push_back(sol::make_object(L, sol::lua_nil));
        out.push_back(sol::make_object(L, std::string(r.get<sol::error>().what())));
        return out;
    }
    for (auto v : r) {
        out.push_back(sol::object(v));
    }
    return out;
}

}  // namespace

void installBalanceHelpers(sol::state_view lua, sol::table risen) {
    if (!risen["balance"].valid()) {
        risen["balance"] = lua.create_table();
    }
    runChunk(lua, kBalanceChunk, "=risen.balance", risen);
    sol::object item = risen["item"];
    if (item.is<sol::table>()) {
        lua.registry()["spass.item"] = item;
    }
}

void installBalanceModApi(sol::state_view lua, sol::table api) {
    runChunk(lua, kBalanceModChunk, "=risen.balance.install", api);
}

void bindBalance(sol::state_view lua, sol::table risen) {
    sol::table item = risen["item"];
    installBalanceHelpers(lua, risen);

    // risen.item.skill_array(target, kind) -> { { skill = "gESkill_Stat_STR", amount = 10, modifier = "gESkillModifier_AddValue" }, ... } | nil, reason
    // target: template name or item entity; kind: "required" (gCItem_PS.RequiredSkills) or "modifiers" (gCItem_PS.ModifySkills).
    // Low level: engine enum names; risen.item.requirements/modifiers give friendly names.
    item.set_function("skill_array", [](sol::this_state ts, sol::object target, const std::string& kindName) -> Two {
        sol::state_view L(ts);
        const ArrayKind* kind = kindByName(kindName);
        if (kind == nullptr) {
            return failResult(L, "kind must be \"required\" or \"modifiers\"");
        }
        std::string error;
        void* ps = itemSetOf(target, error);
        if (ps == nullptr) {
            return failResult(L, error);
        }
        std::vector<Entry> entries;
        if (!readArray(ps, *kind, entries, error)) {
            return failResult(L, error);
        }
        sol::table out = L.create_table();
        int i = 1;
        for (const Entry& e : entries) {
            sol::table t = L.create_table();
            t["skill"] = e.skill;
            t["amount"] = e.amount;
            if (kind->modifier) {
                t["modifier"] = e.modifier;
            }
            out[i++] = t;
        }
        return okResult(L, out);
    });

    // risen.item.set_skill_array(target, kind, entries) -> true | nil, reason: replaces the whole array (entries as returned by skill_array)
    item.set_function("set_skill_array", [](sol::this_state ts, sol::object target, const std::string& kindName, sol::table entries) -> Two {
        sol::state_view L(ts);
        const ArrayKind* kind = kindByName(kindName);
        if (kind == nullptr) {
            return failResult(L, "kind must be \"required\" or \"modifiers\"");
        }
        std::vector<Entry> list;
        for (std::size_t i = 1; i <= entries.size(); ++i) {
            sol::object o = entries[i];
            if (!o.is<sol::table>()) {
                return failResult(L, "entry " + std::to_string(i) + " must be a table { skill =, amount = [, modifier =] }");
            }
            sol::table t = o.as<sol::table>();
            Entry e;
            sol::optional<std::string> skill = t["skill"];
            sol::optional<double> amount = t["amount"];
            if (!skill || !amount) {
                return failResult(L, "entry " + std::to_string(i) + " needs skill (string) and amount (number)");
            }
            e.skill = *skill;
            e.amount = static_cast<int>(*amount);
            if (kind->modifier) {
                sol::optional<std::string> mod = t["modifier"];
                e.modifier = mod.value_or("gESkillModifier_AddValue");
            }
            list.push_back(std::move(e));
        }
        std::string error;
        void* ps = itemSetOf(target, error);
        if (ps == nullptr) {
            return failResult(L, error);
        }
        if (!writeArray(ps, *kind, list, error)) {
            return failResult(L, error);
        }
        return okResult(L, sol::make_object(L, true));
    });

    // risen.item.can_use(who, item) -> bool | nil, reason: the engine's own equip-condition check (gCSkills_PS::HasRequiredSkills against the
    // item's RequiredSkills; the first step of every CanEquip script). `who` is a character entity, `item` a template name or item entity.
    item.set_function("can_use", [](sol::this_state ts, const LuaEntity& who, sol::object target) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.skills_has_required == nullptr) {
            return failResult(L, "engine function gCSkills_PS::HasRequiredSkills is unavailable");
        }
        void* entity = ptr(who);
        if (entity == nullptr) {
            return failResult(L, "entity is gone");
        }
        std::string error;
        void* itemPs = itemSetOf(target, error);
        if (itemPs == nullptr) {
            return failResult(L, error);
        }
        void* skills = skillsSetOf(entity);
        if (skills == nullptr) {
            return failResult(L, "the character has no skills (gCSkills_PS)");
        }
        bool result = false;
        if (!guarded([&] { result = s.skills_has_required(skills, static_cast<char*>(itemPs) + kRequired.offset, nullptr); })) {
            return failResult(L, "the engine raised an exception during the check");
        }
        return okResult(L, sol::make_object(L, result));
    });

    // risen.item.consume(owner, "It_Po_Health_01" [, count]) -> true | nil, reason: what the engine does when a potion/food item is used up
    // (gCInventory_PS::ConsumeItems): applies the item's ModifySkills to the owner permanently (base values: HitPoints +75 heals,
    // MaxHitPoints +10 raises the maximum) and deletes `count` (default 1) items from the first matching stack.
    item.set_function("consume", [](sol::this_state ts, const LuaEntity& owner, const std::string& name, sol::optional<int> count) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.inv_consume_items == nullptr || s.inv_find_stack == nullptr) {
            return failResult(L, "engine function PSInventory::ConsumeItems is unavailable");
        }
        void* entity = ptr(owner);
        if (entity == nullptr) {
            return failResult(L, "entity is gone");
        }
        const int n = count.value_or(1);
        if (n < 1) {
            return failResult(L, "count must be at least 1");
        }
        int stack = -1;
        bool used = false;
        bool valid = true;
        if (!guarded([&] {
                EntityHolder inv{entity};
                const TemplateObject t(name);
                valid = t.valid();
                if (!valid) {
                    return;
                }
                stack = s.inv_find_stack(&inv, t.get(), -1, true);
                if (stack >= 0) {
                    used = s.inv_consume_items(&inv, stack, n);
                }
            })) {
            return failResult(L, "the engine raised an exception");
        }
        if (!valid) {
            return failResult(L, "unknown template '" + name + "'");
        }
        if (stack < 0) {
            return failResult(L, "the inventory holds no unequipped '" + name + "'");
        }
        if (!used) {
            return failResult(L, "the engine refused (fewer than " + std::to_string(n) + " items in the stack?)");
        }
        return okResult(L, sol::make_object(L, true));
    });

    // risen.item.skill_total_raw(entity, "gESkill_Stat_STR") -> total, base | nil, reason (engine enum names; use risen.item.skill_total)
    item.set_function("skill_total_raw", [](sol::this_state ts, const LuaEntity& e, const std::string& name) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.sk_get_total == nullptr || s.sk_get_base == nullptr) {
            return failResult(L, "engine functions PSSkills::GetTotal/GetBase are unavailable");
        }
        const auto id = skillId(name);
        if (!id) {
            return failResult(L, "unknown skill '" + name + "'");
        }
        int total = 0, base = 0;
        if (!guarded([&] {
                EntityHolder h{ptr(e)};
                total = s.sk_get_total(&h, *id);
                base = s.sk_get_base(&h, *id);
            })) {
            return failResult(L, "the engine raised an exception (no skills on this entity?)");
        }
        return {sol::make_object(L, total), sol::make_object(L, base)};
    });

    // risen.item.skill_id(name) -> number | nil: numeric gESkill of an enum name (diagnostics)
    item.set_function("skill_id", [](const std::string& name) -> sol::optional<int> { return skillId(name); });
}

void addBalanceMethods(sol::usertype<LuaEntity>& ut) {
    // entity:skill_total(skill) -> total, base | nil, reason: the character's current value of a skill/attribute (PSSkills::GetTotal / GetBase).
    ut["skill_total"] = [](sol::this_state ts, sol::object self, sol::variadic_args va) { return forward(ts, "skill_total", self, collect(va)); };
    // entity:requirements() / set_requirements(spec [, opts]) / modifiers() / set_modifiers(spec [, opts]) / can_use(item): the item API
    // (risen.item.*) for one item entity; can_use asks whether *this character* meets the item's requirements.
    ut["requirements"] = [](sol::this_state ts, sol::object self) { return forward(ts, "requirements", self); };
    ut["modifiers"] = [](sol::this_state ts, sol::object self) { return forward(ts, "modifiers", self); };
    ut["set_requirements"] = [](sol::this_state ts, sol::object self, sol::variadic_args va) { return forward(ts, "set_requirements", self, collect(va)); };
    ut["set_modifiers"] = [](sol::this_state ts, sol::object self, sol::variadic_args va) { return forward(ts, "set_modifiers", self, collect(va)); };
    ut["can_use"] = [](sol::this_state ts, sol::object self, sol::variadic_args va) { return forward(ts, "can_use", self, collect(va)); };
}


}  // namespace spass::game
