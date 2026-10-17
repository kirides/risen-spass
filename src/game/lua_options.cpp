#include "game/lua_options.h"

#include <cctype>
#include <fstream>

#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

#include "lua/runtime.h"

namespace spass::game {
namespace {

bool bareKey(const std::string& s) {
    if (s.empty() || s.size() > 64) {
        return false;
    }
    for (const unsigned char c : s) {
        if (!(std::isalnum(c) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

// Mod ids become file names: only the characters of an id that are safe in one.
bool safeModId(const std::string& id) {
    if (id.empty() || id.size() > 100 || id[0] == '.' || id[0] == '!' || id[0] == '@') {
        return false;
    }
    for (const unsigned char c : id) {
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    return true;
}

// The Lua side of risen.options (the registry, the validation, the stepping and the lists for the mod manager). `native` = { owner, read, write }.
const char* kOptionsChunk = R"LUA(
local risen, native = ...
local options = risen.options
local registry = {}   -- mod id -> { title, spec, entries = { internal entries }, by_id }

local TYPES = { boolean = true, number = true, string = true }
local KIND_ALIAS = {
    toggle = "toggle", checkbox = "toggle", switch = "toggle", boolean = "toggle", bool = "toggle",
    dropdown = "dropdown", ["drop-down"] = "dropdown", drop_down = "dropdown", select = "dropdown", choice = "dropdown", list = "dropdown",
    slider = "slider", number = "slider", stepper = "slider", range = "slider",
    text = "text", string = "text", input = "text",
}
local MAX_ENTRIES, MAX_VALUES = 64, 64

local function one_line(s)
    return (tostring(s):match("^[^\r\n]*"))
end

local function finite(v)
    return type(v) == "number" and v == v and v ~= math.huge and v ~= -math.huge
end

local function show(v)
    if type(v) == "boolean" then return v and "on" or "off" end
    if type(v) == "number" then
        if v == math.floor(v) and math.abs(v) < 1e15 then return ("%d"):format(v) end
        return ("%.4g"):format(v)
    end
    return tostring(v)
end

-- values = array of scalars | array of { value = v, name = "Label" } | map value -> label  ==>  { { value, name }, ... }
local function normalize_values(values, vtype)
    if type(values) ~= "table" then return nil, "values must be a list or a table" end
    local out, seen = {}, {}
    local function add(v, name)
        if type(v) ~= vtype then return nil, ("value %s is a %s, the option is a %s"):format(tostring(v), type(v), vtype) end
        if vtype == "number" and not finite(v) then return nil, "a value is not a finite number" end
        if seen[v] then return nil, "value " .. show(v) .. " is listed twice" end
        if #out >= MAX_VALUES then return nil, "at most " .. MAX_VALUES .. " values" end
        seen[v] = true
        out[#out + 1] = { value = v, name = one_line(name ~= nil and name or show(v)):sub(1, 60) }
        return true
    end
    local n = 0
    for i, v in ipairs(values) do
        n = i
        if type(v) == "table" then
            local ok, why = add(v.value, v.name)
            if not ok then return nil, why end
        else
            local ok, why = add(v)
            if not ok then return nil, why end
        end
    end
    if n == 0 then
        local keys = {}
        for k, name in pairs(values) do keys[#keys + 1] = { k, name } end
        table.sort(keys, function(a, b)
            local x, y = tostring(a[2]), tostring(b[2])
            if x ~= y then return x < y end
            return tostring(a[1]) < tostring(b[1])
        end)
        for _, kv in ipairs(keys) do
            local ok, why = add(kv[1], kv[2])
            if not ok then return nil, why end
        end
    end
    if #out == 0 then return nil, "values is empty" end
    return out
end

local function choice_index(e, v)
    for i, c in ipairs(e.choices or {}) do
        if c.value == v then return i end
    end
end

local function round_to_step(e, v)
    local snapped = e.min + math.floor((v - e.min) / e.step + 0.5) * e.step
    return tonumber(("%.6f"):format(snapped))
end

-- The value `v` as entry `e` stores it, or nil and why.
local function check(e, v)
    if type(v) ~= e.type then return nil, ("%s must be a %s, not a %s"):format(e.id, e.type, type(v)) end
    if e.type == "number" and not finite(v) then return nil, e.id .. " must be a finite number" end
    if e.type == "string" and #v > 200 then return nil, e.id .. " is too long" end
    if e.choices then
        if not choice_index(e, v) then return nil, ("%s: %s is not one of the values"):format(e.id, show(v)) end
        return v
    end
    if e.kind == "slider" then
        if v < e.min or v > e.max then return nil, ("%s must be between %s and %s"):format(e.id, show(e.min), show(e.max)) end
        return round_to_step(e, v)
    end
    return v
end

local function build_entry(raw, n)
    if type(raw) ~= "table" then return nil, ("entry %d is not a table"):format(n) end
    local id = raw.id
    if type(id) ~= "string" or not id:match("^[%w_%-]+$") or #id > 64 then
        return nil, ("entry %d: id must be a string of letters, digits, _ and - (it is the config.toml key)"):format(n)
    end
    local function bad(why) return nil, ("option '%s': %s"):format(id, why) end
    local ty = raw.type
    if ty == nil then ty = type(raw.value ~= nil and raw.value or raw.default) end
    if not TYPES[ty] then return bad("type must be \"boolean\", \"number\" or \"string\"") end
    local e = { id = id, type = ty, raw = raw, description = raw.description ~= nil and tostring(raw.description):sub(1, 600) or "", restart = raw.restart == true }
    e.name = one_line(raw.name ~= nil and raw.name or id):sub(1, 60)
    if raw.group ~= nil then e.group = one_line(raw.group):sub(1, 40) end
    if raw.values ~= nil then
        local list, why = normalize_values(raw.values, ty)
        if not list then return bad(why) end
        e.choices = list
    end
    local kind = nil
    if raw.kind ~= nil then
        kind = type(raw.kind) == "string" and KIND_ALIAS[raw.kind:lower()] or nil
        if not kind then return bad("kind must be \"toggle\", \"dropdown\", \"slider\" or \"text\"") end
    elseif e.choices then kind = "dropdown"
    elseif ty == "boolean" then kind = "toggle"
    elseif ty == "number" and finite(raw.min) and finite(raw.max) then kind = "slider"
    else kind = "text" end
    e.kind = kind
    if kind == "toggle" and ty ~= "boolean" then return bad("a toggle needs type \"boolean\"") end
    if kind == "dropdown" and not e.choices then return bad("a drop-down needs values") end
    if kind == "slider" then
        if ty ~= "number" then return bad("a slider needs type \"number\"") end
        if not (finite(raw.min) and finite(raw.max) and raw.min < raw.max) then return bad("a slider needs min < max") end
        e.min, e.max = raw.min, raw.max
        e.step = raw.step == nil and 1 or raw.step
        if not (finite(e.step) and e.step > 0) then return bad("step must be a number above 0") end
    end
    if kind ~= "dropdown" then e.choices = nil end
    local default = raw.default
    if default == nil then default = raw.value end
    if default == nil then return bad("needs a value (or a default)") end
    if type(default) ~= ty then return bad(("default is a %s, the option is a %s"):format(type(default), ty)) end
    if ty == "number" and not finite(default) then return bad("default is not a finite number") end
    e.default = default
    local value = raw.value
    if value == nil then value = default end
    if type(value) ~= ty then return bad(("value is a %s, the option is a %s"):format(type(value), ty)) end
    if ty == "number" and not finite(value) then return bad("value is not a finite number") end
    e.value = value   -- as the mod gave it (a value from config.toml outside the range is kept; the manager steps back into it)
    return e
end

function risen.options.define(spec)
    local mod = native.owner()
    if mod == "" then return nil, "risen.options.define must be called by a mod" end
    if type(spec) ~= "table" then return nil, "define expects a table { entries..., on_change = fn, save = fn }" end
    local list = spec.entries
    if list == nil then list = spec end
    if type(list) ~= "table" then return nil, "entries must be a list" end
    local entries, by_id = {}, {}
    for i, raw in ipairs(list) do
        if i > MAX_ENTRIES then return nil, "at most " .. MAX_ENTRIES .. " options per mod" end
        local e, why = build_entry(raw, i)
        if not e then return nil, why end
        if by_id[e.id] then return nil, ("option '%s' is defined twice"):format(e.id) end
        by_id[e.id] = e
        entries[#entries + 1] = e
    end
    if #entries == 0 then return nil, "define needs at least one entry" end
    for _, f in ipairs({ "on_change", "save" }) do
        if spec[f] ~= nil and type(spec[f]) ~= "function" then return nil, f .. " must be a function" end
    end
    local r = { mod = mod, title = spec.title ~= nil and one_line(spec.title):sub(1, 60) or nil, spec = spec, entries = entries, by_id = by_id }
    -- the player's choices of an earlier run (the layer's own store; a mod with its own `save` keeps its values itself)
    if not spec.save then
        local stored = native.read(mod)
        for id, v in pairs(stored or {}) do
            local e = by_id[id]
            local ok = e and check(e, v)
            if e and ok ~= nil then e.value = ok; e.raw.value = ok end
        end
    end
    registry[mod] = r
    return spec
end

local function find(mod, id)
    if type(mod) ~= "string" then return nil, "the mod id must be a string" end
    local r = registry[mod]
    if not r then return nil, ("mod '%s' has no options"):format(mod) end
    if id == nil then return r end
    if type(id) ~= "string" then return nil, "the option id must be a string" end
    local e = r.by_id[id]
    if not e then return nil, ("mod '%s' has no option '%s'"):format(mod, id) end
    return r, e
end

local function persist(r)
    if r.spec.save then
        local ok, err = pcall(r.spec.save)
        if not ok then
            risen.log.error(("options: [%s] save: %s"):format(r.mod, one_line(err)))
            return one_line(err)
        end
        return nil
    end
    local diff = {}
    for _, e in ipairs(r.entries) do
        if e.value ~= e.default then diff[e.id] = e.value end
    end
    local ok, why = native.write(r.mod, diff)
    if not ok then
        risen.log.warn(("options: [%s] could not store the options: %s"):format(r.mod, tostring(why)))
        return tostring(why)
    end
end

-- sets the (checked) value, tells the mod and stores; returns the report
local function apply(r, e, value)
    local old = e.value
    local report = { mod = r.mod, id = e.id, old = old, value = value, changed = value ~= old, restart = e.restart }
    if not report.changed then return report end
    e.value = value
    e.raw.value = value
    if r.spec.on_change then
        local ok, err = pcall(r.spec.on_change, e.id, value, old)
        if not ok then
            report.error = one_line(err)
            risen.log.error(("options: [%s] on_change(%s): %s"):format(r.mod, e.id, report.error))
        end
    end
    local why = persist(r)
    if why and not report.error then report.error = "not stored: " .. why end
    return report
end

function risen.options.get(mod, id)
    local r, e = find(mod, id)
    if not r or id == nil then return nil, e or "the option id must be a string" end
    return e.value
end

function risen.options.set(mod, id, value)
    local r, e = find(mod, id)
    if not r then return nil, e end
    if id == nil then return nil, "the option id must be a string" end
    local checked, why = check(e, value)
    if checked == nil then return nil, why end
    return true, apply(r, e, checked)
end

-- reset(mod, id): one option back to its default; reset(mod): all of them (the report of the last change, plus `count`)
function risen.options.reset(mod, id)
    local r, e = find(mod, id)
    if not r then return nil, e end
    if id ~= nil then return true, apply(r, e, e.default) end
    local count, last = 0, nil
    for _, en in ipairs(r.entries) do
        if en.value ~= en.default then
            last = apply(r, en, en.default)
            count = count + 1
        end
    end
    last = last or { mod = mod, changed = false }
    last.count = count
    return true, last
end

local function view(e)
    local out = {
        id = e.id, name = e.name, group = e.group, type = e.type, kind = e.kind, value = e.value, default = e.default, description = e.description,
        restart = e.restart, changed = e.value ~= e.default, text = show(e.value), default_text = show(e.default),
        min = e.min, max = e.max, step = e.step,
    }
    if e.choices then
        out.values = {}
        for i, c in ipairs(e.choices) do out.values[i] = { value = c.value, name = c.name } end
        local at = choice_index(e, e.value)
        out.index = at
        out.custom = at == nil
        out.text = at and e.choices[at].name or show(e.value)
        local d = choice_index(e, e.default)
        out.default_text = d and e.choices[d].name or show(e.default)
    end
    return out
end

function risen.options.list(only)
    local out = {}
    if only ~= nil and type(only) ~= "string" then return out end
    local mods = {}
    for mod in pairs(registry) do
        if only == nil or only == mod then mods[#mods + 1] = mod end
    end
    table.sort(mods)
    for _, mod in ipairs(mods) do
        local r = registry[mod]
        local item = { mod = mod, title = r.title, own_save = r.spec.save ~= nil, entries = {} }
        for i, e in ipairs(r.entries) do item.entries[i] = view(e) end
        out[#out + 1] = item
    end
    return out
end

-- one step to the next / previous choice or slider position; dir = 1 | -1 (a toggle flips). Returns the new value to hand to set(), or nil, why.
function risen.options.step(mod, id, dir)
    local r, e = find(mod, id)
    if not r then return nil, e end
    if id == nil then return nil, "the option id must be a string" end
    if dir ~= 1 and dir ~= -1 then return nil, "dir must be 1 or -1" end
    local v
    if e.kind == "toggle" then
        v = not e.value
    elseif e.kind == "dropdown" then
        local at = choice_index(e, e.value)
        local n = #e.choices
        local to = at and (((at - 1 + dir) % n) + 1) or (dir > 0 and 1 or n)
        v = e.choices[to].value
    elseif e.kind == "slider" then
        local cur = e.value
        if cur < e.min then cur = e.min - (dir > 0 and e.step or 0) end
        if cur > e.max then cur = e.max + (dir < 0 and e.step or 0) end
        v = round_to_step(e, math.max(e.min, math.min(e.max, cur + dir * e.step)))
    else
        return nil, ("'%s' is free text and cannot be stepped"):format(e.id)
    end
    return options.set(mod, id, v)
end

return function(mod)   -- the teardown of a mod instance
    local had = registry[mod] ~= nil
    registry[mod] = nil
    return had
end
)LUA";

}  // namespace

std::filesystem::path optionsStorePath(const std::string& modId) {
    const std::filesystem::path base = lua::Runtime::instance().userConfigPath().parent_path();
    if (base.empty() || !safeModId(modId)) {
        return {};
    }
    return base / "options" / (modId + ".toml");
}

void bindOptions(sol::state_view lua, sol::table risen) {
    sol::table options = lua.create_table();
    risen["options"] = options;

    sol::table native = lua.create_table();
    native.set_function("owner", []() { return lua::Runtime::currentOwner(); });
    // native.read(mod) -> { id = scalar } | nil: the player's stored choices of a mod
    native.set_function("read", [](sol::this_state ts, const std::string& mod) -> sol::object {
        sol::state_view L(ts);
        const std::filesystem::path file = optionsStorePath(mod);
        std::error_code ec;
        if (file.empty() || !std::filesystem::exists(file, ec)) {
            return sol::make_object(L, sol::lua_nil);
        }
        sol::table out = L.create_table();
        try {
            const toml::table parsed = toml::parse_file(file.string());
            for (const auto& [key, node] : parsed) {
                const std::string name(key.str());
                if (const auto* b = node.as_boolean()) {
                    out[name] = b->get();
                } else if (const auto* i = node.as_integer()) {
                    out[name] = static_cast<double>(i->get());
                } else if (const auto* f = node.as_floating_point()) {
                    out[name] = f->get();
                } else if (const auto* s = node.as_string()) {
                    out[name] = s->get();
                }
            }
        } catch (const toml::parse_error& e) {
            spdlog::warn("options: [{}] {}: {} (line {}); the stored choices are ignored", mod, file.filename().string(), std::string(e.description()),
                         e.source().begin.line);
            return sol::make_object(L, sol::lua_nil);
        }
        return out;
    });
    // native.write(mod, { id = scalar }) -> true | nil, reason: replaces the store (an empty table removes the file)
    native.set_function("write", [](sol::this_state ts, const std::string& mod, sol::table values) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        const auto fail = [&L](const std::string& why) { return std::make_tuple(sol::make_object(L, sol::lua_nil), sol::make_object(L, why)); };
        const std::filesystem::path file = optionsStorePath(mod);
        if (file.empty()) {
            return fail("no place to store options (no mods.toml directory)");
        }
        toml::table out;
        for (const auto& [k, v] : values) {
            if (k.get_type() != sol::type::string || !bareKey(k.as<std::string>())) {
                continue;
            }
            const std::string key = k.as<std::string>();
            switch (v.get_type()) {
                case sol::type::boolean: out.insert_or_assign(key, v.as<bool>()); break;
                case sol::type::string: out.insert_or_assign(key, v.as<std::string>()); break;
                case sol::type::number: {
                    const double d = v.as<double>();
                    if (d == static_cast<double>(static_cast<long long>(d)) && std::abs(d) < 1e15) {
                        out.insert_or_assign(key, static_cast<long long>(d));
                    } else {
                        out.insert_or_assign(key, d);
                    }
                    break;
                }
                default: break;
            }
        }
        std::error_code ec;
        if (out.empty()) {
            std::filesystem::remove(file, ec);
            return std::make_tuple(sol::make_object(L, true), sol::make_object(L, sol::lua_nil));
        }
        std::filesystem::create_directories(file.parent_path(), ec);
        const std::filesystem::path tmp = file.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) {
                return fail("cannot write " + tmp.string());
            }
            f << "# Written by the in-game options menu of the mod manager; delete a line (or the file) to get the default from config.toml.\n" << out << "\n";
            if (!f.good()) {
                return fail("cannot write " + tmp.string());
            }
        }
        std::filesystem::rename(tmp, file, ec);
        if (ec) {
            std::filesystem::remove(file, ec);
            ec.clear();
            std::filesystem::rename(tmp, file, ec);
            if (ec) {
                return fail("cannot replace " + file.string() + ": " + ec.message());
            }
        }
        return std::make_tuple(sol::make_object(L, true), sol::make_object(L, sol::lua_nil));
    });

    sol::load_result loaded = lua.load(kOptionsChunk, "=risen.options");
    if (!loaded.valid()) {
        spdlog::error("lua: options helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result r = fn(risen, native);
    if (!r.valid()) {
        spdlog::error("lua: options helpers failed to run: {}", r.get<sol::error>().what());
        return;
    }
    lua.registry()["spass.options.release"] = r.get<sol::object>();
}

void optionsReleaseOwner(const std::string& mod, std::vector<std::string>& released) {
    auto& rt = lua::Runtime::instance();
    sol::state_view lua = rt.lua();
    sol::object fn = lua.registry()["spass.options.release"];
    if (!fn.valid() || fn.get_type() != sol::type::function) {
        return;
    }
    sol::protected_function_result r = fn.as<sol::protected_function>()(mod);
    if (r.valid() && r.get_type() == sol::type::boolean && r.get<bool>()) {
        released.push_back("options menu entries");
    }
}

}  // namespace spass::game
