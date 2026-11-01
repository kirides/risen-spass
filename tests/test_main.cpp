// Unit tests that run without the game: string file parser, mod discovery/ordering and the Lua runtime (sandbox, events,
// require). Build target `spass_tests`; run with `ctest` or directly. A tiny self-contained runner, no framework.

#include <windows.h>
#include <winsock2.h>
#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <map>
#include <regex>
#include <cmath>
#include <cstring>
#include <sstream>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "common/asset_names.h"
#include "common/crash.h"
#include "common/httpclient/httpclient.h"
#include "common/link.h"
#include "common/pipeclient.h"
#include "common/input_names.h"
#include "common/pose.h"
#include "common/sound_file.h"
#include "common/template_id.h"
#include "common/test_run.h"
#include "common/trace.h"
#include "game/ai_lua.h"
#include "game/abi.h"
#include "game/ai_names.h"
#include "game/combat_names.h"
#include "game/bindings.h"
#include "game/loc_files.h"
#include "game/migrate_lua.h"
#include "game/lua_config.h"
#include "game/lua_http.h"
#include "game/lua_options.h"
#include "game/lua_test.h"
#include "game/lua_entity.h"
#include "lua/mods.h"
#include "lua/runtime.h"
#include "pack2mod/convert.h"
#include "pack2mod/cs.h"
#include "pack2mod/json_in.h"
#include "pack2mod/lua_emit.h"
#include "pack2mod/pak.h"
#include "pack2mod/tab.h"
#include "pack2mod/text.h"
#include "pack2mod/tple.h"

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ws2_32.lib")

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

TEST(mods_assets_key) {
    TempDir t;
    t.write("plain/mod.toml", "id = \"plain\"\n");
    t.write("custom/mod.toml", "id = \"custom\"\nassets = \"files/res\"\n");
    t.write("off/mod.toml", "id = \"off\"\nassets = false\n");
    for (const auto& m : spass::lua::discoverMods(t.path)) {
        if (m.id == "plain") CHECK(m.assets == "assets");
        if (m.id == "custom") CHECK(m.assets == "files/res");
        if (m.id == "off") CHECK(m.assets.empty());
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

TEST(runtime_emit_list_passes_a_run_time_sized_argument_list) {
    // the queued gameplay events (event_bus.cpp) are dispatched with an argument list built at run time, including nil and trailing nil values
    TempDir t;
    t.write("one/mod.toml", "id = \"one\"\n");
    t.write("one/main.lua",
            "got = nil\n"
            "risen.on('test.list', function(...) got = { n = select('#', ...), ... } end)\n"
            "risen.on('test.probe', function() return got.n, got[1], got[2], got[3], got[4] end)\n");
    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, /*unsafeLua=*/false));
    {
        auto lock = rt.lock();
        std::vector<sol::object> args;
        args.push_back(sol::make_object(rt.lua(), std::string("PC_Hero")));
        args.push_back(sol::make_object(rt.lua(), sol::lua_nil));
        args.push_back(sol::make_object(rt.lua(), 3.0));
        args.push_back(sol::make_object(rt.lua(), sol::lua_nil));
        rt.emitList("test.list", args);
        rt.emitList("test.nobody", args);  // no handler: nothing happens
        const sol::object r = rt.emitUntilHandled("test.probe");
        CHECK(r.valid() && r.is<double>() && r.as<double>() == 4.0);  // all four arguments arrived, the trailing nil included
    }
    rt.stop();
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

#include "asset_tests.inc"
#include "sound_tests.inc"
#include "npc_tests.inc"
#include "template_create_tests.inc"
#include "spell_tests.inc"
#include "recipe_tests.inc"
#include "persistent_npc_tests.inc"
#include "routine_tests.inc"
#include "spawn_when_tests.inc"
#include "behavior_tests.inc"
#include "dialog_cond_tests.inc"
#include "ai_tests.inc"
#include "test_risen_test.inc"
#include "quickloot_tests.inc"
#include "hardening_tests.inc"
#include "geometry_tests.inc"
#include "combat_tests.inc"
#include "mods_tests.inc"
#include "modmanager_tests.inc"
#include "options_tests.inc"
#include "options_model_tests.inc"
#include "options_text_tests.inc"
#include "crash_tests.inc"
#include "pack2mod_tests.inc"

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

TEST(pose_angles_round_trip) {
    using namespace spass::pose;
    auto close = [](double a, double b) { return std::fabs(a - b) < 1e-3; };
    // yaw 0 looks along +z, 90 along +x (Entity:direction's convention); up stays +y
    Matrix m = compose(1, 2, 3, Angles{90, 0, 0});
    CHECK(close(m[8], 1) && close(m[9], 0) && close(m[10], 0));
    CHECK(close(m[4], 0) && close(m[5], 1) && close(m[6], 0));
    CHECK(close(m[0], 0) && close(m[2], -1));
    CHECK(m[12] == 1 && m[13] == 2 && m[14] == 3 && m[15] == 1);
    // pitch up raises the forward axis, roll lifts the right side
    CHECK(compose(0, 0, 0, Angles{0, 30, 0})[9] > 0.49f);
    CHECK(compose(0, 0, 0, Angles{0, 0, 20})[1] > 0.33f);
    // angles -> matrix -> angles, with a scale that stays
    for (float yaw : {0.f, 37.f, 123.f, 270.f, 359.f})
        for (float pitch : {-60.f, 0.f, 15.f})
            for (float roll : {-30.f, 0.f, 45.f}) {
                const Matrix a = compose(5, 6, 7, Angles{yaw, pitch, roll}, {2, 2, 2});
                const Angles b = decompose(a);
                CHECK(close(b.yaw, yaw) && close(b.pitch, pitch) && close(b.roll, roll));
                const auto s = scale_of(a);
                CHECK(close(s[0], 2) && close(s[1], 2) && close(s[2], 2));
                const Matrix c = compose(5, 6, 7, b, s);
                for (int i = 0; i < 16; ++i) CHECK(close(a[i], c[i]));
            }
    Matrix bad = m;
    bad[3] = std::nanf("");
    CHECK(!sane(bad) && sane(m));
}

// ---- network access (risen.http, risen.json) ---------------------------------------------------------------------------------------

TEST(manifest_network_key_and_player_switches) {
    TempDir dir;
    dir.write("a/mod.toml", "id = \"a\"\nnetwork = true\n");
    dir.write("b/mod.toml", "id = \"b\"\nnetwork = [\"API.Example.com\", \"*.cdn.test\"]\n");
    dir.write("c/mod.toml", "id = \"c\"\n");
    dir.write("d/mod.toml", "id = \"d\"\nnetwork = 3\n");
    std::string err;
    auto a = spass::lua::readManifest(dir.path / "a", err);
    auto b = spass::lua::readManifest(dir.path / "b", err);
    auto c = spass::lua::readManifest(dir.path / "c", err);
    CHECK(a && a->network && a->networkHosts.empty());
    CHECK(b && b->network && b->networkHosts.size() == 2 && b->networkHosts[0] == "api.example.com");
    CHECK(c && !c->network);
    CHECK(!spass::lua::readManifest(dir.path / "d", err) && err.find("network") != std::string::npos);

    spass::lua::UserConfig cfg;
    CHECK(cfg.networkAllowedFor("a") && cfg.empty());
    cfg.networkMods["a"] = false;
    CHECK(!cfg.networkAllowedFor("a") && cfg.networkAllowedFor("b") && !cfg.empty());
    cfg.networkEnabled = false;
    cfg.enabled["x"] = false;
    const fs::path file = dir.path / "mods.toml";
    std::string error;
    CHECK(spass::lua::saveUserConfig(file, cfg, error));
    std::vector<std::string> problems;
    const auto back = spass::lua::loadUserConfig(file, problems);
    CHECK(problems.empty() && !back.networkEnabled && back.networkMods.count("a") && !back.networkMods.at("a") && !back.isEnabled("x"));
    CHECK(!back.networkAllowedFor("b"));
}

TEST(http_url_helpers) {
    using namespace httpc;
    Url u;
    std::string e;
    CHECK(parse_url("https://Example.COM/a/b?x=1#frag", u, e) && u.https && u.host == "example.com" && u.port == 443 && u.path == "/a/b?x=1");
    CHECK(parse_url("http://127.0.0.1:8080", u, e) && !u.https && u.port == 8080 && u.path == "/");
    CHECK(parse_url("http://h?q=1", u, e) && u.path == "/?q=1");
    CHECK(!parse_url("ftp://x/", u, e));
    CHECK(!parse_url("https://user:pw@host/", u, e));
    CHECK(!parse_url("https://host:99999/", u, e));
    CHECK(!parse_url("https://ho st/", u, e));
    CHECK(!parse_url("https:///path", u, e));
    CHECK(host_allowed("a.com", {}));
    CHECK(host_allowed("API.a.com", {"api.a.com"}) && !host_allowed("evil-api.a.com", {"api.a.com"}));
    CHECK(host_allowed("x.a.com", {"*.a.com"}) && !host_allowed("a.com", {"*.a.com"}) && !host_allowed("xa.com", {"*.a.com"}));
    Url base;
    parse_url("https://h.test/dir/file?q=1", base, e);
    CHECK(resolve_location(base, "/abs") == "https://h.test/abs");
    CHECK(resolve_location(base, "rel") == "https://h.test/dir/rel");
    CHECK(resolve_location(base, "//o.test/x") == "https://o.test/x");
    CHECK(resolve_location(base, "http://o.test/x") == "http://o.test/x");
}

TEST(json_decode_encode_round_trip) {
    sol::state lua;
    std::string err;
    auto v = spass::game::jsonDecode(lua, R"({"a":1,"b":[1,null,"x"],"c":{"d":true,"e":null},"f":1.5})", err);
    CHECK(v.has_value());
    sol::table t = v->as<sol::table>();
    CHECK(t.get<double>("a") == 1 && t.get<double>("f") == 1.5);
    sol::table b = t["b"];
    CHECK(b.size() == 3 && b.get<std::string>(3) == "x" && b[2].get_type() == sol::type::lightuserdata);
    sol::table c = t["c"];
    CHECK(c.get<bool>("d") && !c["e"].valid());
    CHECK(!spass::game::jsonDecode(lua, "{bad", err).has_value() && !err.empty());
    // encode: arrays, objects, empty table, escaping
    lua["v"] = lua.create_table_with("name", "a\"b", "list", lua.create_table_with(1, 10, 2, 20), "empty", lua.create_table());
    sol::object vo = lua["v"];
    auto text = spass::game::jsonEncode(vo, false, err);
    CHECK(text && *text == R"({"empty":[],"list":[10,20],"name":"a\"b"})");
    lua.script("cyc = {} cyc.self = cyc");
    sol::object cyc = lua["cyc"];
    CHECK(!spass::game::jsonEncode(cyc, false, err).has_value() && err.find("deeper") != std::string::npos);
    lua.script("fn = function() end");
    sol::object fn = lua["fn"];
    CHECK(!spass::game::jsonEncode(fn, false, err).has_value());
    lua.script("nan = 0/0");
    sol::object nan = lua["nan"];
    CHECK(!spass::game::jsonEncode(nan, false, err).has_value());
}

namespace {

// A tiny HTTP server on 127.0.0.1 (loopback only: no firewall prompt), one connection at a time.
struct LoopbackServer {
    SOCKET listener = INVALID_SOCKET;
    int port = 0;
    std::thread thread;
    std::vector<std::thread> workers;
    std::mutex wm;
    std::atomic<bool> stop{false};

    LoopbackServer() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        int len = sizeof(addr);
        getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        listen(listener, 8);
        thread = std::thread([this] { run(); });
    }
    ~LoopbackServer() {
        stop = true;
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<u_short>(port));
        connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        closesocket(s);
        thread.join();
        for (std::thread& w : workers) w.join();
        closesocket(listener);
        WSACleanup();
    }
    std::string url(const std::string& path, const char* host = "127.0.0.1") const { return "http://" + std::string(host) + ":" + std::to_string(port) + path; }

    static void sendAll(SOCKET c, const std::string& data) {
        std::size_t off = 0;
        while (off < data.size()) {
            const int n = send(c, data.data() + static_cast<int>(off), static_cast<int>(data.size() - off), 0);
            if (n <= 0) return;
            off += static_cast<std::size_t>(n);
        }
    }
    static std::string reply(int status, const std::string& body, const std::string& extra = {}) {
        return "HTTP/1.1 " + std::to_string(status) + " X\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra + "\r\n" + body;
    }
    void run() {
        while (!stop) {
            SOCKET c = accept(listener, nullptr, nullptr);
            if (c == INVALID_SOCKET || stop) {
                if (c != INVALID_SOCKET) closesocket(c);
                continue;
            }
            std::lock_guard<std::mutex> g(wm);
            workers.emplace_back([this, c] { serve(c); });  // every connection on its own thread: the async tests keep several open at once
        }
    }
    void serve(SOCKET c) {
        {
            std::string req;
            char buf[4096];
            std::size_t headEnd = std::string::npos;
            while (headEnd == std::string::npos) {
                const int n = recv(c, buf, sizeof(buf), 0);
                if (n <= 0) break;
                req.append(buf, static_cast<std::size_t>(n));
                headEnd = req.find("\r\n\r\n");
            }
            if (headEnd != std::string::npos) {
                std::string lower = req;
                for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                const std::size_t cl = lower.find("content-length:");
                const std::size_t want = cl == std::string::npos ? 0 : static_cast<std::size_t>(std::atoi(lower.c_str() + cl + 15));
                while (req.size() < headEnd + 4 + want) {
                    const int n = recv(c, buf, sizeof(buf), 0);
                    if (n <= 0) break;
                    req.append(buf, static_cast<std::size_t>(n));
                }
                const std::size_t sp1 = req.find(' ');
                const std::string path = req.substr(sp1 + 1, req.find(' ', sp1 + 1) - sp1 - 1);
                if (path == "/text") sendAll(c, reply(200, "hello"));
                else if (path == "/json") sendAll(c, reply(200, R"({"a":1,"b":[1,2]})", "Content-Type: application/json\r\n"));
                else if (path == "/echo") sendAll(c, reply(200, req));
                else if (path == "/redir") sendAll(c, reply(302, "", "Location: /text\r\n"));
                else if (path == "/redir-host") sendAll(c, reply(302, "", "Location: http://localhost:" + std::to_string(port) + "/echo\r\n"));
                else if (path == "/loop") sendAll(c, reply(302, "", "Location: /loop\r\n"));
                else if (path == "/404") sendAll(c, reply(404, "nope"));
                else if (path == "/big") sendAll(c, reply(200, std::string(200000, 'x')));
                else if (path == "/slow") { Sleep(4000); sendAll(c, reply(200, "late")); }
                else if (path == "/hold") { for (int i = 0; i < 100 && !stop; ++i) Sleep(100); sendAll(c, reply(200, "held")); }  // up to 10 s, ends early when the server stops
                else if (path == "/trickle") { sendAll(c, "HTTP/1.1 200 X\r\nConnection: close\r\nContent-Length: 100000\r\n\r\n"); for (int i = 0; i < 100 && !stop; ++i) { sendAll(c, std::string(1000, 't')); Sleep(100); } }
                else sendAll(c, reply(200, "other"));
            }
            shutdown(c, SD_SEND);
            closesocket(c);
        }
    }
};

httpc::Response fetch(const httpc::Request& r) { return httpc::perform(r); }

}  // namespace

TEST(http_client_against_a_loopback_server) {
    LoopbackServer srv;
    using namespace httpc;
    Request r;
    r.url = srv.url("/text");
    Response a = fetch(r);
    CHECK(a.ok && a.status == 200 && a.body == "hello");

    r.url = srv.url("/json");
    a = fetch(r);
    CHECK(a.ok && a.body == R"({"a":1,"b":[1,2]})" && a.header("content-type") == "application/json");

    r.url = srv.url("/404");
    a = fetch(r);
    CHECK(a.ok && a.status == 404 && a.body == "nope");

    // POST body, custom headers, user agent
    r.method = "POST";
    r.url = srv.url("/echo");
    r.body = "payload";
    r.headers = {{"Authorization", "Bearer t"}, {"X-Test", "1"}};
    r.userAgent = "ua-test";
    a = fetch(r);
    CHECK(a.ok && a.body.find("POST /echo") == 0 && a.body.find("payload") != std::string::npos);
    CHECK(a.body.find("Authorization: Bearer t") != std::string::npos && a.body.find("X-Test: 1") != std::string::npos && a.body.find("ua-test") != std::string::npos);

    // a redirect to another host drops Authorization; 302 turns POST into GET
    r.url = srv.url("/redir-host");
    r.method = "GET";
    r.body.clear();
    a = fetch(r);
    CHECK(a.ok && a.redirects == 1 && a.body.find("GET /echo") == 0 && a.body.find("X-Test: 1") != std::string::npos && a.body.find("Authorization") == std::string::npos);
    r.url = srv.url("/redir");
    a = fetch(r);
    CHECK(a.ok && a.body == "hello" && a.redirects == 1);
    r.url = srv.url("/loop");
    a = fetch(r);
    CHECK(!a.ok && a.error.find("redirect") != std::string::npos);

    // host allow-list: a redirect to a host that is not listed is refused, so is the first hop
    r.url = srv.url("/redir-host");
    r.allowedHosts = {"127.0.0.1"};
    a = fetch(r);
    CHECK(!a.ok && a.error.find("not allowed") != std::string::npos);
    r.url = srv.url("/text", "localhost");
    a = fetch(r);
    CHECK(!a.ok && a.error.find("not allowed") != std::string::npos);
    r.allowedHosts.clear();

    // size limit and bad input
    r.url = srv.url("/big");
    r.maxBytes = 1000;
    a = fetch(r);
    CHECK(!a.ok && a.error.find("larger") != std::string::npos);
    r.maxBytes = 1u << 20;
    a = fetch(r);
    CHECK(a.ok && a.body.size() == 200000);
    r.headers = {{"Bad Name", "x"}};
    CHECK(!fetch(r).ok);
    r.headers = {{"X", "a\r\nInjected: 1"}};
    CHECK(!fetch(r).ok);
    r.headers.clear();

    // asynchronous use: submit with a completion queue, then poll
    {
        auto client = Client::create_winhttp();
        CompletionQueue queue;
        r.url = srv.url("/text");
        const std::uint64_t id1 = client->submit(r, queue.sink());
        const std::uint64_t id2 = client->submit(r, queue.sink());
        std::vector<Completed> done;
        for (int i = 0; i < 100 && done.size() < 2; ++i) {
            queue.poll(done);
            Sleep(50);
        }
        CHECK(done.size() == 2 && done[0].id != done[1].id && (done[0].id == id1 || done[0].id == id2) && done[0].response.body == "hello");
        CHECK(client->in_flight() == 0);
    }

    // timeout (last: the server is busy while it sleeps)
    r.url = srv.url("/slow");
    r.timeoutMs = 500;
    const auto t0 = std::chrono::steady_clock::now();
    a = fetch(r);
    CHECK(!a.ok && a.error.find("timed out") != std::string::npos);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
}

TEST(http_async_queue_cancel_deadline_and_shutdown) {
    LoopbackServer srv;
    using namespace httpc;
    const auto self = std::this_thread::get_id();
    const auto since = [](std::chrono::steady_clock::time_point t) { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count(); };

    {   // 40 requests through a client that runs 4 at a time: all arrive, none on the thread that submitted
        ClientOptions o;
        o.maxInFlight = 4;
        auto client = Client::create_winhttp(o);
        std::atomic<int> done{0}, good{0}, onCaller{0};
        Request r;
        r.url = srv.url("/text");
        for (int i = 0; i < 40; ++i) {
            client->submit(r, [&](std::uint64_t, Response res) {
                if (std::this_thread::get_id() == self) ++onCaller;
                if (res.ok && res.body == "hello") ++good;
                ++done;
            });
        }
        for (int i = 0; i < 300 && done < 40; ++i) Sleep(50);
        CHECK(done == 40 && good == 40 && onCaller == 0);
        CHECK(client->in_flight() == 0);
    }
    {   // cancel a running request, and one that still waits in the queue
        ClientOptions o;
        o.maxInFlight = 1;
        auto client = Client::create_winhttp(o);
        std::mutex m;
        std::map<std::uint64_t, Response> got;
        auto sink = [&](std::uint64_t id, Response res) {
            std::lock_guard<std::mutex> lock(m);
            got[id] = std::move(res);
        };
        Request hold, quick;
        hold.url = srv.url("/hold");
        quick.url = srv.url("/text");
        const std::uint64_t a = client->submit(hold, sink);
        const std::uint64_t b = client->submit(quick, sink);  // waits: maxInFlight is 1
        Sleep(300);
        CHECK(client->cancel(b));
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(client->cancel(a));
        for (int i = 0; i < 100; ++i) {
            {
                std::lock_guard<std::mutex> lock(m);
                if (got.size() == 2) break;
            }
            Sleep(20);
        }
        std::lock_guard<std::mutex> lock(m);
        CHECK(got.size() == 2 && got[a].cancelled && got[a].error == "cancelled" && got[b].cancelled && since(t0) < 2000);
        CHECK(!client->cancel(a) && !client->cancel(999999));
    }
    {   // the deadline covers the whole transfer: a server that trickles data forever is stopped although every phase is fast
        Request r;
        r.url = srv.url("/trickle");
        r.timeoutMs = 2000;
        r.totalTimeoutMs = 1200;
        r.maxBytes = 1 << 20;
        const auto t0 = std::chrono::steady_clock::now();
        const Response a = fetch(r);
        CHECK(!a.ok && a.error == "timed out" && since(t0) < 4000);
    }
    {   // errors that are known at once are still delivered from another thread
        auto client = Client::create_winhttp();
        std::atomic<bool> got{false}, onCaller{false};
        Request r;
        r.url = "ftp://example.org/";
        std::string err;
        client->submit(r, [&](std::uint64_t, Response res) {
            onCaller = std::this_thread::get_id() == self;
            err = res.error;
            got = true;
        });
        for (int i = 0; i < 100 && !got; ++i) Sleep(20);
        CHECK(got && !onCaller && err.find("http") != std::string::npos);
    }
    {   // destroying a client with requests in flight cancels them: every callback runs exactly once, before the destructor returns
        std::atomic<int> calls{0}, cancelled{0};
        const auto t0 = std::chrono::steady_clock::now();
        {
            auto client = Client::create_winhttp();
            Request r;
            r.url = srv.url("/hold");
            for (int i = 0; i < 3; ++i) {
                client->submit(r, [&](std::uint64_t, Response res) {
                    ++calls;
                    if (res.cancelled) ++cancelled;
                });
            }
            Sleep(300);
        }
        CHECK(calls == 3 && cancelled == 3 && since(t0) < 3000);
    }
}

TEST(http_connection_failure_is_an_error_not_a_crash) {
    httpc::Request r;
    r.url = "http://127.0.0.1:1/";  // nothing listens
    r.timeoutMs = 1000;
    const auto a = httpc::perform(r);
    CHECK(!a.ok && !a.error.empty());
}

// ---- the live link transport (named pipe) -------------------------------------------------------------------------------------------

namespace {

HANDLE openLinkClient(const std::string& path) {
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 50 && h == INVALID_HANDLE_VALUE; ++i) {
        h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) Sleep(40);
    }
    if (h != INVALID_HANDLE_VALUE) {
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
    }
    return h;
}

bool pollLink(spass::link::Inbox& in, int wantMessages, int wantConnects, int wantDisconnects) {
    for (int i = 0; i < 100; ++i) {
        spass::link::poll(in);
        if (static_cast<int>(in.messages.size()) >= wantMessages && in.connects >= wantConnects && in.disconnects >= wantDisconnects) return true;
        Sleep(30);
    }
    return false;
}

}  // namespace

TEST(link_pipe_round_trip_and_reconnect) {
    std::string err;
    CHECK(!spass::link::start("bad name", err) && !err.empty());
    CHECK(spass::link::start("spass-unit-" + std::to_string(GetCurrentProcessId()), err));
    const std::string path = spass::link::pipePath();
    CHECK(path.rfind("\\\\.\\pipe\\spass-unit-", 0) == 0);
    CHECK(!spass::link::send("nobody listens"));  // no client: dropped

    HANDLE c = openLinkClient(path);
    CHECK(c != INVALID_HANDLE_VALUE);
    DWORD n = 0;
    const std::string hello = R"({"cmd":"hello"})";
    CHECK(WriteFile(c, hello.data(), static_cast<DWORD>(hello.size()), &n, nullptr));
    spass::link::Inbox in;
    CHECK(pollLink(in, 1, 1, 0));
    CHECK(in.messages.size() == 1 && in.messages[0] == hello && spass::link::connected());

    // the reply, and a message bigger than the pipe's buffers in both directions
    CHECK(spass::link::send("reply-1"));
    std::vector<char> buf(400000);
    CHECK(ReadFile(c, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && std::string(buf.data(), n) == "reply-1");
    const std::string big(300000, 'x');
    CHECK(spass::link::send(big));
    std::string got;
    for (int i = 0; i < 20 && got.size() < big.size(); ++i) {
        CHECK(ReadFile(c, buf.data(), 65536, &n, nullptr) || GetLastError() == ERROR_MORE_DATA);
        got.append(buf.data(), n);
        if (GetLastError() != ERROR_MORE_DATA) break;
    }
    CHECK(got == big);
    CHECK(WriteFile(c, big.data(), static_cast<DWORD>(big.size()), &n, nullptr));
    in = {};
    CHECK(pollLink(in, 1, 0, 0) && in.messages[0] == big);

    // a second client is refused while the first one is connected
    HANDLE second = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(second == INVALID_HANDLE_VALUE);

    CloseHandle(c);
    in = {};
    CHECK(pollLink(in, 0, 0, 1) && !spass::link::connected());

    // reconnect
    c = openLinkClient(path);
    CHECK(c != INVALID_HANDLE_VALUE);
    in = {};
    CHECK(pollLink(in, 0, 1, 0) && spass::link::connected());
    CloseHandle(c);
    in = {};
    CHECK(pollLink(in, 0, 0, 1));
    spass::link::stop();
    CHECK(!spass::link::running() && !spass::link::connected());
}

TEST(pipeclient_lines_and_round_trip) {
    using spass::pipeclient::Client;
    CHECK(spass::pipeclient::validName("__TwitchIntegration_Kirides_Conn"));
    CHECK(!spass::pipeclient::validName("") && !spass::pipeclient::validName("a\\b") && !spass::pipeclient::validName("a b") && !spass::pipeclient::validName(std::string(101, 'a')));

    // the line splitter: CRLF, empty lines, a line split over two reads, an over-long line is dropped
    {
        Client c("unit-split", 1.0);
        c.feed("one\r\n\ntwo\nthr", 13);
        c.feed("ee\n", 3);
        auto lines = c.poll(10);
        CHECK(lines.size() == 3 && lines[0] == "one" && lines[1] == "two" && lines[2] == "three");
        const std::string big(spass::pipeclient::kMaxLineBytes + 10, 'x');
        c.feed(big.data(), big.size());
        c.feed("\nok\n", 4);
        lines = c.poll(10);
        CHECK(lines.size() == 1 && lines[0] == "ok" && c.dropped() == 1);
    }

    // a real pipe: the server comes AFTER the client started (it retries), lines flow both ways, the client reconnects after the server left
    const std::string name = "spass-unit-pc-" + std::to_string(GetCurrentProcessId());
    const std::string path = "\\\\.\\pipe\\" + name;
    Client c(name, 1.0);
    c.start();
    CHECK(!c.connected() && !c.send("nobody"));
    for (int round = 0; round < 2; ++round) {
        HANDLE srv = CreateNamedPipeA(path.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
        CHECK(srv != INVALID_HANDLE_VALUE);
        std::atomic<bool> ok{false};
        std::thread acceptor([&] { ok = ConnectNamedPipe(srv, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED; });
        acceptor.join();
        CHECK(ok);
        bool up = false;
        for (int i = 0; i < 100 && !up; ++i) { up = c.connected(); if (!up) Sleep(50); }
        CHECK(up);
        DWORD n = 0;
        const std::string msg = "REWARD_ADD user cmd a b\nCHAT x y\n";
        CHECK(WriteFile(srv, msg.data(), static_cast<DWORD>(msg.size()), &n, nullptr));
        std::vector<std::string> got;
        for (int i = 0; i < 100 && got.size() < 2; ++i) {
            for (auto& l : c.poll(10)) got.push_back(l);
            if (got.size() < 2) Sleep(30);
        }
        CHECK(got.size() == 2 && got[0] == "REWARD_ADD user cmd a b" && got[1] == "CHAT x y");
        CHECK(c.send("ack 1"));
        char buf[64] = {};
        CHECK(ReadFile(srv, buf, sizeof(buf), &n, nullptr) && std::string(buf, n) == "ack 1\n");
        CloseHandle(srv);  // the server goes away
        bool down = false;
        for (int i = 0; i < 100 && !down; ++i) { down = !c.connected(); if (!down) Sleep(30); }
        CHECK(down);
    }
    CHECK(c.connects() == 2);
    c.stop();
}
