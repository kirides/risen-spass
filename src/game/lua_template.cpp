#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/properties.h"
#include "game/template_object.h"

// Runtime access to the engine's entity templates (docs/RE.md §22).
//
// The engine keeps one eCTemplateEntity per template name (the data of data/common/templates.pak). It is an eCEntity, so its
// property sets are the very objects the spawn code copies into new entities, and the generic bCAccessorProperty mechanism
// (properties.cpp) reads and writes them. `Template(name)` (Script.dll) + eCTemplateEntityProxy::GetEntity() resolve a
// name to the eCTemplateEntity*. The pointer is looked up again on every call (never cached): templates are owned by the
// scene admin and stay alive for the whole process, but resolving by name is cheap and cannot dangle.

namespace spass::game {
namespace {

struct LuaTemplate {
    std::string name;
};

// Result of the lookups below: either a pointer or the reason it could not be had.
struct Resolved {
    void* tpl = nullptr;
    std::string error;
};

Resolved resolveTemplate(const std::string& name) {
    const Symbols& s = sym();
    Resolved r;
    if (s.tpl_proxy_get_entity == nullptr) {
        r.error = "engine function eCTemplateEntityProxy::GetEntity is unavailable";
        return r;
    }
    if (name.empty()) {
        r.error = "empty template name";
        return r;
    }
    const bool ran = guarded([&] {
        const TemplateObject t(name);
        if (!t.valid()) {
            r.error = "unknown template '" + name + "'";
            return;
        }
        r.tpl = s.tpl_proxy_get_entity(const_cast<void*>(t.get()));
        if (r.tpl == nullptr) {
            r.error = "template '" + name + "' has no single entity (multi template)";
        }
    });
    if (!ran) {
        r.tpl = nullptr;
        r.error = "the engine raised an exception while resolving '" + name + "' (game not ready?)";
    }
    return r;
}

std::string templateNameOf(const void* tpl) {
    std::string out;
    if (sym().tple_get_name == nullptr || tpl == nullptr) {
        return out;
    }
    guarded([&] {
        const void* n = sym().tple_get_name(tpl);
        if (n != nullptr) {
            out = sym().bstring_get_text(n);
        }
    });
    return out;
}

// The template content is read from the .tple file lazily (eCTemplateEntity::ReadAll sets the "content loaded" flag); the
// property-set accessors below do not trigger that themselves, so make sure it happened. Returns false on a failed load.
bool ensureLoaded(void* tpl) {
    const Symbols& s = sym();
    if (s.tple_is_content_loaded == nullptr || s.tple_read_all == nullptr) {
        return true;
    }
    bool loaded = false;
    guarded([&] {
        if (!s.tple_is_content_loaded(tpl)) {
            s.tple_read_all(tpl);
        }
        loaded = s.tple_is_content_loaded(tpl);
    });
    return loaded;
}

std::vector<std::pair<std::string, void*>> propertySetsOf(void* tpl) {
    const Symbols& s = sym();
    std::vector<std::pair<std::string, void*>> out;
    if (s.ent_ps_count == nullptr || s.ent_ps_at == nullptr || s.ps_class_name == nullptr) {
        return out;
    }
    guarded([&] {
        const int n = s.ent_ps_count(tpl);
        for (int i = 0; i < n && i < 256; ++i) {
            void* ps = s.ent_ps_at(tpl, i);
            if (ps != nullptr) {
                out.emplace_back(s.bstring_get_text(s.ps_class_name(ps)), ps);
            }
        }
    });
    return out;
}

// A property set of the template by class name (own sets only: the engine resolves a reference template's sets separately).
void* findSet(void* tpl, const std::string& set) {
    for (const auto& [name, ps] : propertySetsOf(tpl)) {
        if (name == set) {
            return ps;
        }
    }
    return nullptr;
}

// The scene admin singleton (module "eCSceneAdmin" of the module admin), or nullptr.
void* sceneAdmin() {
    const Symbols& s = sym();
    void* admin = nullptr;
    if (s.module_admin_instance == nullptr || s.module_find_by_class == nullptr) {
        return nullptr;
    }
    guarded([&] {
        const BString cls("eCSceneAdmin");
        admin = s.module_find_by_class(s.module_admin_instance(), cls.get());
    });
    return admin;
}

// eCEntity::GetCreator() is virtual (vtable slot 0x170/8, see eCTemplateEntity::PatchInstances): the template an entity was
// spawned from, or nullptr for entities that have none.
const void* creatorOf(const void* entity) {
    using Fn = const void* (*)(const void*);
    void** vtable = *reinterpret_cast<void***>(const_cast<void*>(entity));
    return reinterpret_cast<Fn>(vtable[0x170 / 8])(entity);
}

// Walks the entity trees of the scene admin's dynamic contexts (the same traversal as eCTemplateEntity::PatchInstances: the
// context's root entity at +0x40, first child +0x20, next sibling +0x18) and calls `visit(entity, creator)` for every entity.
// Returns false when the scene is not available.
template <class F>
bool walkEntities(F&& visit) {
    const Symbols& s = sym();
    if (s.scene_ctx_count == nullptr || s.scene_ctx_at == nullptr) {
        return false;
    }
    void* admin = sceneAdmin();
    if (admin == nullptr) {
        return false;
    }
    return guarded([&] {
        const int contexts = s.scene_ctx_count(admin);
        std::vector<char*> stack;
        long visited = 0;
        for (int c = 0; c < contexts && c < 100000; ++c) {
            char* ctx = static_cast<char*>(s.scene_ctx_at(admin, c));
            if (ctx == nullptr) {
                continue;
            }
            stack.push_back(*reinterpret_cast<char**>(ctx + 0x40));
            while (!stack.empty() && visited < 4000000) {
                char* node = stack.back();
                stack.pop_back();
                if (node == nullptr) {
                    continue;
                }
                ++visited;
                visit(node, creatorOf(node), c);
                char* sibling = *reinterpret_cast<char**>(node + 0x18);
                char* child = *reinterpret_cast<char**>(node + 0x20);
                if (sibling != nullptr && sibling != node) {
                    stack.push_back(sibling);
                }
                if (child != nullptr && child != node) {
                    stack.push_back(child);
                }
            }
            stack.clear();
        }
    });
}

sol::optional<int> countInstances(void* tpl) {
    int count = 0;
    if (!walkEntities([&](char*, const void* creator, int) { count += creator == tpl ? 1 : 0; })) {
        return sol::nullopt;
    }
    return count;
}

using Two = std::tuple<sol::object, sol::object>;

Two okResult(sol::state_view lua, sol::object value) { return {std::move(value), sol::make_object(lua, sol::lua_nil)}; }
Two failResult(sol::state_view lua, const std::string& reason) {
    return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, reason)};
}

}  // namespace

void* templatePropertySet(const std::string& templateName, const std::string& set, std::string& error) {
    const Resolved r = resolveTemplate(templateName);
    if (r.tpl == nullptr) {
        error = r.error;
        return nullptr;
    }
    if (!ensureLoaded(r.tpl)) {
        error = "the template content could not be loaded";
        return nullptr;
    }
    void* ps = findSet(r.tpl, set);
    if (ps == nullptr) {
        error = "template '" + templateName + "' has no property set '" + set + "'";
    }
    return ps;
}

// entity:template() -> Template | nil, reason
void addTemplateMethods(sol::usertype<LuaEntity>& ut) {
    ut["template"] = [](sol::this_state ts, const LuaEntity& e) -> Two {
        sol::state_view lua(ts);
        void* entity = ptr(e);
        if (entity == nullptr) {
            return failResult(lua, "entity is gone");
        }
        const void* tpl = nullptr;
        if (!guarded([&] { tpl = creatorOf(entity); })) {
            return failResult(lua, "the engine raised an exception (not a dynamic entity?)");
        }
        if (tpl == nullptr) {
            return failResult(lua, "entity has no template");
        }
        const std::string name = templateNameOf(tpl);
        if (name.empty()) {
            return failResult(lua, "template has no name");
        }
        return okResult(lua, sol::make_object(lua, LuaTemplate{name}));
    };
}

void bindTemplates(sol::state_view lua, sol::table risen) {
    const Symbols& s = sym();

    auto ut = lua.new_usertype<LuaTemplate>(
        "Template", sol::no_constructor,
        sol::meta_function::to_string, [](const LuaTemplate& t) { return fmt::format("Template({})", t.name); },
        sol::meta_function::equal_to, [](const LuaTemplate& a, const LuaTemplate& b) { return a.name == b.name; },
        // template:name() -> string
        "name", [](const LuaTemplate& t) { return t.name; },
        // template:is_loaded() -> bool: whether the engine already read the template's content (.tple file)
        "is_loaded", [&s](sol::this_state ts, const LuaTemplate& t) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            if (s.tple_is_content_loaded == nullptr) {
                return failResult(lua, "engine function eCTemplateEntity::IsContentLoaded is unavailable");
            }
            bool v = false;
            guarded([&] { v = s.tple_is_content_loaded(r.tpl); });
            return okResult(lua, sol::make_object(lua, v));
        },
        // template:ref_template() -> name of the template this one is derived from, or nil
        "ref_template", [&s](sol::this_state ts, const LuaTemplate& t) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            const void* ref = nullptr;
            if (s.tple_get_ref_template != nullptr) {
                guarded([&] { ref = s.tple_get_ref_template(r.tpl); });
            }
            if (ref == nullptr) {
                return okResult(lua, sol::make_object(lua, sol::lua_nil));
            }
            return okResult(lua, sol::make_object(lua, templateNameOf(ref)));
        },
        // template:property_sets() -> { "gCItem_PS", "eCMesh_PS", ... }
        "property_sets", [](sol::this_state ts, const LuaTemplate& t) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            if (!ensureLoaded(r.tpl)) {
                return failResult(lua, "the template content could not be loaded");
            }
            sol::table out = lua.create_table();
            int i = 1;
            for (const auto& [name, ps] : propertySetsOf(r.tpl)) {
                out[i++] = name;
            }
            return okResult(lua, out);
        },
        // template:list_properties("gCItem_PS") -> { {name=, type=}, ... }
        "list_properties", [](sol::this_state ts, const LuaTemplate& t, const std::string& set) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            if (!ensureLoaded(r.tpl)) {
                return failResult(lua, "the template content could not be loaded");
            }
            void* ps = findSet(r.tpl, set);
            if (ps == nullptr) {
                return failResult(lua, "template '" + t.name + "' has no property set '" + set + "'");
            }
            return okResult(lua, propertyListTable(lua, ps));
        },
        // template:get_property("gCItem_PS", "GoldValue") -> "50"
        "get_property", [](sol::this_state ts, const LuaTemplate& t, const std::string& set, const std::string& prop) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            if (!ensureLoaded(r.tpl)) {
                return failResult(lua, "the template content could not be loaded");
            }
            void* ps = findSet(r.tpl, set);
            if (ps == nullptr) {
                return failResult(lua, "template '" + t.name + "' has no property set '" + set + "'");
            }
            const sol::optional<std::string> v = propertyGet(ps, prop);
            if (!v) {
                return failResult(lua, "property set '" + set + "' has no property '" + prop + "'");
            }
            return okResult(lua, sol::make_object(lua, *v));
        },
        // template:set_property("gCItem_PS", "GoldValue", "777") -> true | nil, reason   (numbers and booleans are accepted too)
        "set_property", [](sol::this_state ts, const LuaTemplate& t, const std::string& set, const std::string& prop,
                           const std::string& value) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            if (!ensureLoaded(r.tpl)) {
                return failResult(lua, "the template content could not be loaded");
            }
            void* ps = findSet(r.tpl, set);
            if (ps == nullptr) {
                return failResult(lua, "template '" + t.name + "' has no property set '" + set + "'");
            }
            if (!propertyGet(ps, prop)) {
                return failResult(lua, "property set '" + set + "' has no property '" + prop + "'");
            }
            if (!propertySet(ps, prop, value)) {
                return failResult(lua, "the engine refused to set '" + set + "." + prop + "'");
            }
            return okResult(lua, sol::make_object(lua, true));
        },
        // template:patch_instances() -> number of entities patched | nil, reason: copies the template's property sets into every live
        // entity created from it (eCTemplateEntity::PatchEntity on each)
        "patch_instances", [&s](sol::this_state ts, const LuaTemplate& t) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            if (s.tple_patch_entity == nullptr) {
                return failResult(lua, "engine function eCTemplateEntity::PatchEntity is unavailable");
            }
            std::vector<void*> found;
            if (!walkEntities([&](char* node, const void* creator, int) {
                    if (creator == r.tpl) {
                        found.push_back(node);
                    }
                })) {
                return failResult(lua, "the scene is not available (no world loaded?)");
            }
            int patched = 0;
            for (void* e : found) {
                int result = 0;
                if (guarded([&] { result = s.tple_patch_entity(r.tpl, e, false, false); }) && result == 1) {
                    ++patched;
                }
            }
            return okResult(lua, sol::make_object(lua, patched));
        },
        // template:patch_entity(entity) -> true | nil, reason: copies the template into one entity right now
        "patch_entity", [&s](sol::this_state ts, const LuaTemplate& t, const LuaEntity& e) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            void* entity = ptr(e);
            if (entity == nullptr) {
                return failResult(lua, "entity is gone");
            }
            if (s.tple_patch_entity == nullptr) {
                return failResult(lua, "engine function eCTemplateEntity::PatchEntity is unavailable");
            }
            int result = 0;
            if (!guarded([&] { result = s.tple_patch_entity(r.tpl, entity, false, false); })) {
                return failResult(lua, "the engine raised an exception while patching the entity");
            }
            return okResult(lua, sol::make_object(lua, result == 1));
        },
        // template:instances([limit]) -> { Entity, ... } the live entities created from this template (default limit 200)
        "instances", [](sol::this_state ts, const LuaTemplate& t, sol::optional<int> limit) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            std::vector<void*> found;
            const int max = limit.value_or(200);
            if (!walkEntities([&](char* node, const void* creator, int) {
                    if (creator == r.tpl && static_cast<int>(found.size()) < max) {
                        found.push_back(node);
                    }
                })) {
                return failResult(lua, "the scene is not available (no world loaded?)");
            }
            sol::table out = lua.create_table();
            int i = 1;
            for (void* e : found) {
                out[i++] = makeEntityObject(lua, e);
            }
            return okResult(lua, out);
        },
        // template:instance_count() -> number of live entities created from this template
        "instance_count", [](sol::this_state ts, const LuaTemplate& t) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            const sol::optional<int> n = countInstances(r.tpl);
            if (!n) {
                return failResult(lua, "the scene is not available (no world loaded?)");
            }
            return okResult(lua, sol::make_object(lua, *n));
        });
    (void)ut;

    sol::table tpl = risen["template"];
    // risen.template.get("It_1H_Sword") -> Template | nil, reason
    tpl.set_function("get", [](sol::this_state ts, const std::string& name) -> Two {
        sol::state_view lua(ts);
        const Resolved r = resolveTemplate(name);
        if (r.tpl == nullptr) {
            return failResult(lua, r.error);
        }
        const std::string real = templateNameOf(r.tpl);
        return okResult(lua, sol::make_object(lua, LuaTemplate{real.empty() ? name : real}));
    });

    // risen.template.census() -> { [template name] = number of live entities created from it }, total entities (one pass over the scene)
    tpl.set_function("census", [](sol::this_state ts) -> Two {
        sol::state_view lua(ts);
        std::map<const void*, int> counts;
        int total = 0;
        if (!walkEntities([&](char*, const void* creator, int) {
                ++total;
                if (creator != nullptr) {
                    ++counts[creator];
                }
            })) {
            return failResult(lua, "the scene is not available (no world loaded?)");
        }
        sol::table out = lua.create_table();
        for (const auto& [creator, n] : counts) {
            const std::string name = templateNameOf(creator);
            if (!name.empty()) {
                out[name] = n;
            }
        }
        return {sol::make_object(lua, out), sol::make_object(lua, total)};
    });

    installTemplatePatchHelper(lua, risen);
}

void installTemplatePatchHelper(sol::state_view lua, sol::table risen) {
    // risen.template.patch(name, { gCItem_PS = { GoldValue = 100 } } [, { instances = true, dry_run = true }]) -> true, count [, instance_count] | nil, reason
    // Everything is validated (template, property sets, properties) before the first value is written. With `instances = true` the
    // same values are also written into every live entity created from the template (only the listed properties: per-instance state
    // such as hit points, amount or inventory stays untouched, unlike Template:patch_instances()). `dry_run = true` only validates (returns
    // true, number of values that would be written).
    const char* chunk = R"LUA(
local risen = ...

-- Lua value -> the engine's text format
local function as_text(v)
    local t = type(v)
    if t == "number" then
        if v == math.floor(v) and math.abs(v) < 2^53 then return string.format("%d", v) end
        return string.format("%.9g", v)
    elseif t == "boolean" then
        return v and "true" or "false"
    elseif t == "string" then
        return v
    end
    return nil
end

function risen.template.patch(name, sets, opts)
    if type(name) ~= "string" then return nil, "template name must be a string" end
    if type(sets) ~= "table" then return nil, "the second argument must be a table { PropertySet = { Property = value } }" end
    opts = opts or {}
    local tpl, why = risen.template.get(name)
    if not tpl then return nil, why end
    local have = {}
    for _, set in ipairs(tpl:property_sets() or {}) do have[set] = true end
    -- validate everything first
    local jobs, problems = {}, {}
    for set, props in pairs(sets) do
        if type(props) ~= "table" then
            problems[#problems + 1] = "'" .. tostring(set) .. "' must be a table of properties"
        elseif not have[set] then
            problems[#problems + 1] = "template '" .. name .. "' has no property set '" .. tostring(set) .. "'"
        else
            local known = {}
            for _, p in ipairs(tpl:list_properties(set) or {}) do known[p.name] = true end
            for prop, value in pairs(props) do
                local text = as_text(value)
                if text == nil then
                    problems[#problems + 1] = set .. "." .. tostring(prop) .. ": unsupported value type " .. type(value)
                elseif not known[prop] then
                    problems[#problems + 1] = "property set '" .. set .. "' has no property '" .. tostring(prop) .. "'"
                else
                    jobs[#jobs + 1] = { set, prop, text }
                end
            end
        end
    end
    if #problems > 0 then return nil, table.concat(problems, "; ") end
    if opts.dry_run then return true, #jobs end
    local applied, failed = 0, {}
    for _, job in ipairs(jobs) do
        local ok, err = tpl:set_property(job[1], job[2], job[3])
        if ok then applied = applied + 1 else failed[#failed + 1] = err or (job[1] .. "." .. job[2]) end
    end
    if #failed > 0 then return nil, table.concat(failed, "; ") end
    if opts.instances then
        local touched = 0
        for _, e in ipairs(tpl:instances(1000000) or {}) do
            local any = false
            for _, job in ipairs(jobs) do
                if e:set_property(job[1], job[2], job[3]) then any = true end
            end
            if any then touched = touched + 1 end
        end
        return true, applied, touched
    end
    return true, applied
end
)LUA";
    sol::load_result loaded = lua.load(chunk, "=risen.template");
    if (!loaded.valid()) {
        spdlog::error("lua: template helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(risen);
    if (!res.valid()) {
        spdlog::error("lua: template helpers failed to run: {}", res.get<sol::error>().what());
    }
}

}  // namespace spass::game
