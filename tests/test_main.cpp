// Unit tests that run without the game: string file parser, mod discovery/ordering and the Lua runtime (sandbox, events,
// require). Build target `spass_tests`; run with `ctest` or directly. A tiny self-contained runner, no framework.

#include <windows.h>
#include <dbghelp.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "common/test_run.h"
#include "common/trace.h"
#include "game/loc_files.h"
#include "game/lua_test.h"
#include "game/lua_entity.h"
#include "lua/mods.h"
#include "lua/runtime.h"

#pragma comment(lib, "dbghelp.lib")

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;
std::string g_current;

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        ++g_checks;                                                                                  \
        if (!(cond)) {                                                                               \
            ++g_failures;                                                                            \
            std::printf("  FAIL [%s] %s:%d: %s\n", g_current.c_str(), __FILE__, __LINE__, #cond);    \
        }                                                                                            \
    } while (0)

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("spass_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void write(const std::string& rel, const std::string& content) const {
        const fs::path p = path / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << content;
    }
};

std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}
struct Register {
    Register(const char* name, std::function<void()> fn) { registry().emplace_back(name, std::move(fn)); }
};
#define TEST(name)                           \
    static void name();                      \
    static Register reg_##name(#name, name); \
    static void name()

// ---- string files -----------------------------------------------------------------------------------------------------

TEST(csv_semicolon_and_comments) {
    const auto rows = spass::game::parseStringCsv("# comment\nFO_A;Alpha\n\nFO_B;Beta; with semicolon\n");
    CHECK(rows.size() == 2);
    CHECK(rows[0].id == "FO_A" && rows[0].text == "Alpha");
    CHECK(rows[1].id == "FO_B" && rows[1].text == "Beta");  // only the first two fields are used
}

TEST(csv_comma_quotes_and_multiline) {
    const auto rows = spass::game::parseStringCsv("id,text\r\nFO_A,\"Hello, \"\"world\"\"\"\r\nFO_B,\"two\nlines\"\r\n");
    CHECK(rows.size() == 2);  // the header row is skipped
    CHECK(rows.size() == 2 && rows[0].text == "Hello, \"world\"");
    CHECK(rows.size() == 2 && rows[1].text == "two\nlines");
}

TEST(csv_bom_and_utf8) {
    const auto rows = spass::game::parseStringCsv("\xEF\xBB\xBF" "FO_Bob;K\xC3\xA4se\n");
    CHECK(rows.size() == 1 && rows[0].id == "FO_Bob" && rows[0].text == "K\xC3\xA4se");
}

TEST(csv_ignores_incomplete_rows) {
    const auto rows = spass::game::parseStringCsv("lonely\n;no id\nGOOD;ok\n");
    CHECK(rows.size() == 1 && rows[0].id == "GOOD");
}

// ---- mods ---------------------------------------------------------------------------------------------------------------

TEST(mods_discovery_skips_invalid_and_disabled) {
    TempDir t;
    t.write("good/mod.toml", "id = \"good\"\nname = \"Good\"\nversion = \"1.2.3\"\n");
    t.write("bad_id/mod.toml", "id = \"Not Valid!\"\n");
    t.write("noid/mod.toml", "name = \"x\"\n");
    t.write("off.off/mod.toml", "id = \"off\"\n");
    t.write("_hidden/mod.toml", "id = \"hidden\"\n");
    t.write("plain/readme.txt", "no manifest");
    const auto mods = spass::lua::discoverMods(t.path);
    CHECK(mods.size() == 1);
    CHECK(!mods.empty() && mods[0].id == "good" && mods[0].version == "1.2.3" && mods[0].main == "main.lua");
}

TEST(mods_load_order_and_requirements) {
    TempDir t;
    t.write("a/mod.toml", "id = \"a\"\nload_after = [\"b\"]\n");
    t.write("b/mod.toml", "id = \"b\"\n");
    t.write("c/mod.toml", "id = \"c\"\nrequires = [\"missing\"]\n");
    t.write("d/mod.toml", "id = \"d\"\nrequires = [\"c\"]\n");
    const auto ordered = spass::lua::resolveLoadOrder(spass::lua::discoverMods(t.path));
    std::vector<std::string> ids;
    for (const auto& m : ordered) {
        ids.push_back(m.id);
    }
    CHECK(ids == (std::vector<std::string>{"b", "a"}));  // c is dropped (missing requirement), d with it
}

TEST(mods_cycle_is_dropped) {
    TempDir t;
    t.write("x/mod.toml", "id = \"x\"\nload_after = [\"y\"]\n");
    t.write("y/mod.toml", "id = \"y\"\nload_after = [\"x\"]\n");
    t.write("z/mod.toml", "id = \"z\"\n");
    const auto ordered = spass::lua::resolveLoadOrder(spass::lua::discoverMods(t.path));
    CHECK(ordered.size() == 1 && ordered[0].id == "z");
}

TEST(mods_data_overlay_flag) {
    TempDir t;
    t.write("plain/mod.toml", "id = \"plain\"\n");
    t.write("overlay/mod.toml", "id = \"overlay\"\ndata_overlay = true\n");
    const auto mods = spass::lua::discoverMods(t.path);
    CHECK(mods.size() == 2);
    for (const auto& m : mods) {
        CHECK(m.dataOverlay == (m.id == "overlay"));
    }
}

TEST(mods_api_version_is_checked) {
    TempDir t;
    t.write("old/mod.toml", "id = \"old\"\napi_version = 1\n");
    t.write("future/mod.toml", "id = \"future\"\napi_version = 99\n");
    t.write("plain/mod.toml", "id = \"plain\"\n");
    const auto mods = spass::lua::discoverMods(t.path);
    std::vector<std::string> ids;
    for (const auto& m : mods) {
        ids.push_back(m.id);
    }
    CHECK(ids == (std::vector<std::string>{"old", "plain"}));  // "future" is skipped
}

// ---- Lua runtime --------------------------------------------------------------------------------------------------------

TEST(runtime_sandbox_events_and_isolation) {
    TempDir t;
    // A mod registers its handler only after its own sanity checks passed, so "the handler exists" proves them.
    t.write("one/mod.toml", "id = \"one\"\n");
    t.write("one/main.lua",
            "leaked = 'from one'\n"
            "assert(io == nil and os.execute == nil and debug == nil and ffi == nil and package == nil, 'sandbox open')\n"
            "assert(loadfile == nil and dofile == nil, 'file loaders exposed')\n"
            "assert(type(os.time) == 'function' and type(string.format) == 'function' and type(bit.band) == 'function')\n"
            "local helper = require('lib.util')\n"
            "assert(helper.answer == 42 and require('lib.util') == helper, 'require')\n"
            "assert(mod.id == 'one' and type(risen.version) == 'string')\n"
            "assert(not pcall(require, '../escape'), 'require must reject path traversal')\n"
            "risen.on('test.ping', function(a, b) return a + b end)\n");
    t.write("one/lib/util.lua", "return { answer = 42 }\n");
    t.write("two/mod.toml", "id = \"two\"\nload_after = [\"one\"]\n");
    t.write("two/main.lua",
            "assert(leaked == nil, 'globals leak between mods')\n"
            "risen.on('test.ping', function(a, b) return nil end, 5)\n"
            "risen.on('test.ping', function(a, b) return a * b end, 10)\n");

    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, /*unsafeLua=*/false));
    CHECK(rt.running());
    // priority 10 (two) runs first; emitUntilHandled stops at the first non-nil result: 6*7, not 6+7
    {
        const sol::object r = rt.emitUntilHandled("test.ping", 6, 7);  // must not outlive the Lua state
        CHECK(r.valid() && r.is<double>() && r.as<double>() == 42.0);
    }
    CHECK(rt.hasHandlers("test.ping"));
    CHECK(!rt.hasHandlers("test.nobody"));
    rt.stop();
    CHECK(!rt.running());
}

TEST(runtime_unsafe_mode_opens_the_sandbox) {
    TempDir t;
    t.write("dev/mod.toml", "id = \"dev\"\n");
    t.write("dev/main.lua", "assert(io ~= nil and debug ~= nil, 'unsafe mode should expose io/debug')\nrisen.on('ok', function() return 1 end)\n");
    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, /*unsafeLua=*/true));
    CHECK(rt.hasHandlers("ok"));
    rt.stop();
}

TEST(runtime_survives_broken_mods) {
    TempDir t;
    t.write("syntax/mod.toml", "id = \"syntax\"\n");
    t.write("syntax/main.lua", "this is not lua (\n");
    t.write("runtime/mod.toml", "id = \"runtime\"\n");
    t.write("runtime/main.lua", "error('boom')\n");
    t.write("fine/mod.toml", "id = \"fine\"\n");
    t.write("fine/main.lua", "risen.on('t', function() error('handler fails') end)\nrisen.on('t', function() return 1 end, -1)\n");
    t.write("bytecode/mod.toml", "id = \"bytecode\"\n");
    t.write("bytecode/main.lua", std::string("\x1b") + "LJ junk");
    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, false));
    // a handler that raises is logged and skipped; the next one still runs
    {
        const sol::object r = rt.emitUntilHandled("t");
        CHECK(r.valid() && r.is<double>() && r.as<double>() == 1.0);
    }
    rt.stop();
}

// ---- risen.template.patch (Lua helper on top of the template handle; the engine part is mocked) -------------------------------

TEST(template_patch_validates_before_writing) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    // A fake template database: It_Sword has gCItem_PS{GoldValue, Amount}; every write is recorded in `log`.
    lua.script(R"LUA(
        log = {}
        local function make(name)
            local t = { sets = { gCItem_PS = { GoldValue = "50", Amount = "1" } } }
            function t:property_sets() local l = {} for k in pairs(self.sets) do l[#l + 1] = k end return l end
            function t:list_properties(set)
                if not self.sets[set] then return nil, "no set" end
                local l = {} for k in pairs(self.sets[set]) do l[#l + 1] = { name = k, type = "long" } end return l
            end
            function t:set_property(set, prop, value) log[#log + 1] = name .. "." .. set .. "." .. prop .. "=" .. value; self.sets[set][prop] = value; return true end
            local inst = { set_property = function(_, set, prop, value) log[#log + 1] = "inst." .. set .. "." .. prop .. "=" .. value; return set == "gCItem_PS" end }
            function t:instances() return { inst, inst } end
            return t
        end
        risen = { template = { get = function(name)
            if name == "It_Sword" then return make(name) end
            return nil, "unknown template '" .. name .. "'"
        end } }
    )LUA");
    spass::game::installTemplatePatchHelper(lua, lua["risen"]);

    // success: values are converted to the engine's text format
    auto run = [&](const char* code) { return lua.script(code).get<std::string>(); };
    CHECK(run("local ok, n = risen.template.patch('It_Sword', { gCItem_PS = { GoldValue = 777, Amount = 2.5 } }) "
              "table.sort(log) return tostring(ok) .. ',' .. tostring(n) .. ',' .. table.concat(log, ';')") ==
          "true,2,It_Sword.gCItem_PS.Amount=2.5;It_Sword.gCItem_PS.GoldValue=777");
    // unknown template, unknown set, unknown property, bad value type, bad argument: nil + a reason, and nothing was written
    lua.script("log = {}");
    for (const char* call : {"'It_Nope', { gCItem_PS = { GoldValue = 1 } }", "'It_Sword', { gCNPC_PS = { Guild = 1 } }",
                             "'It_Sword', { gCItem_PS = { GoldValue = 1, Nope = 2 } }", "'It_Sword', { gCItem_PS = { GoldValue = {} } }",
                             "'It_Sword', 'not a table'"}) {
        const std::string code = std::string("local ok, why = risen.template.patch(") + call + ") return tostring(ok) .. '|' .. type(why)";
        CHECK(run(code.c_str()) == "nil|string");
    }
    CHECK(lua.script("return #log").get<int>() == 0);
    // instances = true also writes the listed properties into the live entities and counts them
    CHECK(run("local ok, n, touched = risen.template.patch('It_Sword', { gCItem_PS = { GoldValue = true } }, { instances = true }) "
              "return tostring(ok) .. ',' .. n .. ',' .. touched .. ',' .. table.concat(log, ';')") ==
          "true,1,2,It_Sword.gCItem_PS.GoldValue=true;inst.gCItem_PS.GoldValue=true;inst.gCItem_PS.GoldValue=true");
}

// ---- risen.item requirements/modifiers and risen.balance (Lua helpers; the native skill-array functions are mocked) ----------------------------

namespace {

// A fake item database: It_Sword (template, one live instance) and It_Potion. `store[name][kind]` holds the engine-style entries, `log`
// records every array write ("target:kind:SKILL=amount[/modifier]|...").
void setupBalanceMock(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
    lua.script(R"LUA(
        log = {}
        store = {
            It_Sword = { required = { { skill = "gESkill_Stat_STR", amount = 10 } }, modifiers = {} },
            It_Potion = { required = {}, modifiers = { { skill = "gESkill_Atrib_HP", amount = 75, modifier = "gESkillModifier_AddValue" } } },
        }
        instance = { store = { required = {}, modifiers = {} } }
        local function describe(entries)
            local parts = {}
            for _, e in ipairs(entries) do parts[#parts + 1] = e.skill .. "=" .. e.amount .. (e.modifier and ("/" .. e.modifier) or "") end
            return table.concat(parts, "|")
        end
        risen = { item = {}, template = {}, log = {} }
        function risen.item.skill_array(target, kind)
            local s = target == instance and instance.store or store[target]
            if not s then return nil, "unknown template '" .. tostring(target) .. "'" end
            return s[kind]
        end
        function risen.item.set_skill_array(target, kind, entries)
            local name = target == instance and "instance" or target
            local s = target == instance and instance.store or store[target]
            if not s then return nil, "unknown template '" .. tostring(target) .. "'" end
            log[#log + 1] = name .. ":" .. kind .. ":" .. describe(entries)
            s[kind] = entries
            return true
        end
        function risen.item.skill_total_raw(who, name) return 7, 5 end
        function risen.template.get(name)
            if not store[name] then return nil, "unknown template '" .. name .. "'" end
            local t = { sets = { gCItem_PS = { GoldValue = "50" }, gCDamage_PS = { DamageAmount = "10" } } }
            function t:property_sets() local l = {} for k in pairs(self.sets) do l[#l + 1] = k end return l end
            function t:list_properties(set) local l = {} for k in pairs(self.sets[set]) do l[#l + 1] = { name = k, type = "long" } end return l end
            function t:set_property(set, prop, value) log[#log + 1] = name .. "." .. set .. "." .. prop .. "=" .. value return true end
            function t:instances() if name == "It_Sword" then return { instance } end return {} end
            return setmetatable(t, { __tostring = function() return "Template(" .. name .. ")" end })
        end
        instance.set_property = function(_, set, prop, value) log[#log + 1] = "instance." .. set .. "." .. prop .. "=" .. value return true end
    )LUA");
    spass::game::installTemplatePatchHelper(lua, lua["risen"]);
    spass::game::installBalanceHelpers(lua, lua["risen"]);
}

}  // namespace

TEST(balance_requirements_and_modifiers_convert_names_and_validate) {
    sol::state lua;
    setupBalanceMock(lua);
    auto run = [&](const char* code) { return lua.script(code).get<std::string>(); };
    // friendly names -> engine enums, in a deterministic order; the read-back uses the friendly names again
    CHECK(run("local ok = risen.item.set_requirements('It_Sword', { Strength = 15, Dexterity = 12 }) return tostring(ok) .. ';' .. log[#log]") ==
          "true;It_Sword:required:gESkill_Stat_DEX=12|gESkill_Stat_STR=15");
    CHECK(run("local r = risen.item.requirements('It_Sword') return r.Strength .. ',' .. r.Dexterity") == "15,12");
    // a Template handle and the engine suffix / full enum names work too; {} clears the list
    CHECK(run("risen.item.set_requirements(risen.template.get('It_Sword'), { Stat_STR = 3, gESkill_Prot_Edge = 1 }) return log[#log]") ==
          "It_Sword:required:gESkill_Stat_STR=3|gESkill_Prot_Edge=1");
    CHECK(run("risen.item.set_requirements('It_Sword', {}) return log[#log]") == "It_Sword:required:");
    // modifiers: default mode add, explicit modes, list form
    CHECK(run("risen.item.set_modifiers('It_Potion', { HitPoints = 40, ProtEdge = { amount = 5, mode = 'set' } }) return log[#log]") ==
          "It_Potion:modifiers:gESkill_Atrib_HP=40/gESkillModifier_AddValue|gESkill_Prot_Edge=5/gESkillModifier_SetToValue");
    CHECK(run("risen.item.set_modifiers('It_Potion', { { skill = 'Strength', amount = -2, mode = 'percent' } }) return log[#log]") ==
          "It_Potion:modifiers:gESkill_Stat_STR=-2/gESkillModifier_AddPercentOfMax");
    CHECK(run("local m = risen.item.modifiers('It_Potion') return m[1].skill .. ',' .. m[1].amount .. ',' .. m[1].mode") == "Strength,-2,percent");
    // invalid input: nil + a reason (valid names listed), nothing written
    lua.script("log = {}");
    CHECK(run("local ok, why = risen.item.set_requirements('It_Sword', { Strenght = 5 }) return tostring(ok) .. '|' .. why") .find("nil|requirements: unknown skill 'Strenght' (valid: Strength, Dexterity") == 0);
    CHECK(run("local ok, why = risen.item.set_requirements('It_Sword', { Strength = 1.5 }) return tostring(ok) .. '|' .. why") ==
          "nil|requirements.Strength: the amount must be an integer, got 1.5");
    CHECK(run("local ok, why = risen.item.set_modifiers('It_Sword', { ProtEdge = { amount = 1, mode = 'bogus' } }) return tostring(ok) .. '|' .. why").find("unknown mode 'bogus' (valid: add, set, max, percent)") != std::string::npos);
    CHECK(run("local ok, why = risen.item.set_requirements('It_Sword', 5) return tostring(ok)") == "nil");
    CHECK(run("local ok, why = risen.item.set_requirements('It_Nope', { Strength = 1 }) return tostring(ok) .. '|' .. why") == "nil|unknown template 'It_Nope'");
    CHECK(run("return tostring(#log)") == "0");
    // instances = true also writes the live entities of the template
    CHECK(run("local ok, n = risen.item.set_requirements('It_Sword', { Strength = 9 }, { instances = true }) return tostring(ok) .. ',' .. n .. ';' .. table.concat(log, ';')") ==
          "true,1;It_Sword:required:gESkill_Stat_STR=9;instance:required:gESkill_Stat_STR=9");
    // the character's skill: friendly names are mapped, unknown ones reported
    CHECK(run("local t, b = risen.item.skill_total({}, 'Strength') return t .. ',' .. b") == "7,5");
    CHECK(run("local t, why = risen.item.skill_total({}, 'Nope') return tostring(t) .. '|' .. why").find("nil|unknown skill 'Nope'") == 0);
}

TEST(balance_apply_validates_everything_before_writing) {
    sol::state lua;
    setupBalanceMock(lua);
    auto run = [&](const char* code) { return lua.script(code).get<std::string>(); };
    // a spec with several problems: all reported, nothing written (not even the valid entry)
    CHECK(run("local rep, why, problems = risen.balance.apply({ items = { It_Sword = { value = 5, requires = { Strenght = 1 } }, It_Nope = { value = 1 }, "
              "It_Potion = { weight = 3 } }, spells = {} }) log[#log + 1] = '' return tostring(rep) .. '|' .. #problems .. '|' .. #log") == "nil|4|1");
    CHECK(run("local _, why = risen.balance.apply({ items = { It_Potion = { value = 'x', properties = { gCItem_PS = { Nope = 1 } } } } }) return why")
              .find("has no property 'Nope'") != std::string::npos);
    CHECK(run("local _, why = risen.balance.apply({ bogus = {} }) return why") == "unknown section 'bogus' (valid: items)");
    CHECK(run("local _, why = risen.balance.apply({ items = { It_Potion = { weight = 3 } } }) return why")
              .find("unknown setting 'weight' (valid: category, damage, mana_cost, modifies, properties, requires, use_type, value)") != std::string::npos);
    // a valid spec: plain properties go through risen.template.patch, arrays through the skill-array writer, instances included
    lua.script("log = {}");
    CHECK(run("local rep = assert(risen.balance.apply({ items = { It_Sword = { value = 120, damage = 25, requires = { Strength = 5 }, modifies = { ProtEdge = 2 } } } })) "
              "table.sort(log) return tostring(rep.ok) .. ',' .. #rep.applied .. ',' .. rep.instances .. ';' .. table.concat(log, ';')") ==
          "true,1,3;It_Sword.gCDamage_PS.DamageAmount=25;It_Sword.gCItem_PS.GoldValue=120;It_Sword:modifiers:gESkill_Prot_Edge=2/gESkillModifier_AddValue;"
          "It_Sword:required:gESkill_Stat_STR=5;instance.gCDamage_PS.DamageAmount=25;instance.gCItem_PS.GoldValue=120;"
          "instance:modifiers:gESkill_Prot_Edge=2/gESkillModifier_AddValue;instance:required:gESkill_Stat_STR=5");
    // instances = false and dry_run
    lua.script("log = {}");
    CHECK(run("local rep = assert(risen.balance.apply({ items = { It_Sword = { value = 1 } } }, { instances = false })) return rep.instances .. ';' .. table.concat(log, ';')") ==
          "0;It_Sword.gCItem_PS.GoldValue=1");
    lua.script("log = {}");
    CHECK(run("local rep = assert(risen.balance.apply({ items = { It_Sword = { value = 1 } } }, { dry_run = true })) return tostring(rep.dry_run) .. ';' .. #log") == "true;0");
    // other milestones add sections
    CHECK(run("risen.balance.register_section('npcs', function(data) return { { label = 'npc ' .. tostring(data.x), run = function() return true, nil, 3 end } }, {} end) "
              "local rep = assert(risen.balance.apply({ npcs = { x = 1 } })) return rep.applied[1] .. ',' .. rep.instances .. ',' .. table.concat(risen.balance.section_names(), '/')") ==
          "npc 1,3,items/npcs");
}

TEST(balance_install_registers_handlers_once_per_spec) {
    sol::state lua;
    setupBalanceMock(lua);
    lua.script(R"LUA(
        handlers, lines = {}, {}
        api = setmetatable({ log = { info = function(m) lines[#lines + 1] = "I " .. m end, warn = function(m) lines[#lines + 1] = "W " .. m end,
                                     error = function(m) lines[#lines + 1] = "E " .. m end },
                              on = function(event, fn) handlers[#handlers + 1] = { event = event, fn = fn } end }, { __index = risen })
    )LUA");
    spass::game::installBalanceModApi(lua, lua["api"]);
    auto run = [&](const char* code) { return lua.script(code).get<std::string>(); };
    CHECK(run("spec = { items = { It_Sword = { value = 9 } } } return tostring(api.balance.install(spec)) .. ',' .. tostring(api.balance.install(spec)) .. ',' .. #handlers") == "true,false,2");
    // first tick: templates only; session.start: with the live entities (the mock's instance counts one written entity per array/property)
    CHECK(run("for _, h in ipairs(handlers) do if h.event == 'tick' then h.fn() h.fn() end end return lines[1] .. '|' .. #lines") == "I balance: first tick: applied 1 of 1 entries|1");
    CHECK(run("for _, h in ipairs(handlers) do if h.event == 'session.start' then h.fn('new') end end return lines[2]") ==
          "I balance: session.start (new): applied 1 of 1 entries, 1 live entities written");
    // a broken spec is reported as errors, not thrown
    CHECK(run("api.balance.install({ items = { It_Nope = { value = 1 } } }) for _, h in ipairs(handlers) do if h.event == 'tick' then h.fn() end end "
              "return tostring(lines[#lines]):sub(1, 30)") == "E balance: items.It_Nope: unkn");
    CHECK(lua.script("return pcall(api.balance.install, 5)").get<bool>() == false);
}

TEST(balance_install_reports_each_application_to_on_applied) {
    sol::state lua;
    setupBalanceMock(lua);
    lua.script(R"LUA(
        handlers, lines = {}, {}
        api = setmetatable({ log = { info = function(m) lines[#lines + 1] = "I " .. m end, warn = function(m) lines[#lines + 1] = "W " .. m end,
                                     error = function(m) lines[#lines + 1] = "E " .. m end },
                              on = function(event, fn) handlers[#handlers + 1] = { event = event, fn = fn } end }, { __index = risen })
    )LUA");
    spass::game::installBalanceModApi(lua, lua["api"]);
    auto run = [&](const char* code) { return lua.script(code).get<std::string>(); };
    CHECK(run("seen = {} api.balance.install({ items = { It_Sword = { value = 4 } } }, { on_applied = function(when, report) seen[#seen + 1] = when .. ':' .. #report.applied end }) "
              "for _, h in ipairs(handlers) do if h.event == 'tick' then h.fn() end end "
              "for _, h in ipairs(handlers) do if h.event == 'session.start' then h.fn('load') end end "
              "return table.concat(seen, ',')") == "first tick:1,session.start (load):1");
    // a callback that raises is logged, never thrown into the engine event
    CHECK(run("api.balance.install({ items = { It_Sword = { value = 5 } } }, { on_applied = function() error('boom') end }) "
              "for _, h in ipairs(handlers) do if h.event == 'tick' then h.fn() end end "
              "return tostring(lines[#lines]:find('E balance: on_applied: .*boom') ~= nil)") == "true");
}

#include "npc_tests.inc"
#include "test_risen_test.inc"

}  // namespace

// Prints the faulting stack with symbol names when a test crashes (the PDB is next to the exe).
LONG WINAPI crashHandler(EXCEPTION_POINTERS* info) {
    std::printf("CRASH: exception 0x%08lx at %p\n", info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    void* frames[48];
    const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    for (USHORT i = 0; i < n; ++i) {
        DWORD64 disp = 0;
        if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), &disp, sym)) {
            std::printf("  #%u %s +0x%llx\n", i, sym->Name, static_cast<unsigned long long>(disp));
        } else {
            std::printf("  #%u %p\n", i, frames[i]);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int main() {
    SetUnhandledExceptionFilter(crashHandler);
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // keep the output when a test crashes
    spdlog::set_level(spdlog::level::off);
    for (auto& [name, fn] : registry()) {
        g_current = name;
        const int before = g_failures;
        fn();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", name.c_str());
    }
    std::printf("\n%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
