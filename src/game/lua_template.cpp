#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>

#include <cstring>

#include "common/template_id.h"
#include "common/trace.h"
#include "game/abi.h"
#include "game/events.h"
#include "game/lua_entity.h"
#include "game/properties.h"
#include "game/template_create_lua.h"
#include "game/template_object.h"
#include "hook/hook_manager.h"

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

// ---- runtime-created templates (docs/RE.md section 25) --------------------------------------------------------------------

// gCNavigation_PS::OnPostCopy (Game.dll) runs `Reset(false, true, true)` on every copied navigation set; while a world is loaded that raises an
// access violation when the copy is a *template's* set (an NPC template cloned after the world exists). Entities of a world need it, a template
// does not (its navigation state is the default), so it is skipped while this layer clones a template.
Hook<void (*)(void*, const void*)> g_navPostCopy;
bool g_skipNavPostCopy = false;

void detourNavPostCopy(void* self, const void* source) {
    if (g_skipNavPostCopy) {
        return;
    }
    g_navPostCopy.original(self, source);
}

struct SkipNavPostCopy {
    SkipNavPostCopy() { g_skipNavPostCopy = true; }
    ~SkipNavPostCopy() { g_skipNavPostCopy = false; }
};

// The engine's bCPropertyID: a 16 byte GUID and a 4 byte counter (always 0).
struct PropertyId {
    unsigned char guid[16] = {};
    std::uint32_t count = 0;
};

// Every clone this process created (clones live until the process ends; `retired` ones only gave up their names, see risen.template.remove).
struct CreatedTemplate {
    std::string base;
    void* root = nullptr;   // the hidden "<name>.tple" root entity
    void* clone = nullptr;  // the template entity itself
    bool retired = false;
};
std::map<std::string, CreatedTemplate>& createdTemplates() {
    static std::map<std::string, CreatedTemplate> created;
    return created;
}

std::string idTextOf(const void* tpl) {
    if (sym().tple_get_id == nullptr || tpl == nullptr) {
        return "";
    }
    std::string out;
    guarded([&] {
        const void* id = sym().tple_get_id(tpl);
        if (id != nullptr) {
            templates::TemplateId bytes{};
            std::memcpy(bytes.data(), id, bytes.size());
            out = templates::guidText(bytes);
        }
    });
    return out;
}

// The text the engine shows for an entity or template: gCInteraction_PS::GetPlainFocusName (key FO_<name>) or
// gCItem_PS::GetPlainDescription (key ITEMDESC_<name>). `ps` is the property set; empty string when it has no text.
std::string plainTextOf(void* ps, void* (*fn)(const void*, void*)) {
    std::string out;
    if (ps == nullptr || fn == nullptr) {
        return out;
    }
    guarded([&] {
        OutUString text;
        fn(ps, text.ptr());
        out = text.take();
    });
    return out;
}

// A bTObjArray<bCString> the engine fills (header { data*, count, capacity }; the callee grows it with the engine allocator).
struct StringArray {
    void* data = nullptr;
    std::int32_t count = 0;
    std::int32_t capacity = 0;
};

// The resource files a property set depends on, according to the engine (GetDependencies of eCMesh_PS / eCAnimation_PS / eCBodyPart_PS):
// the mesh/actor resources the set actually loaded. A set that has not requested its resources (a template's) reports nothing.
void collectDependencies(void* ps, void (*fn)(void*, void*, void*, void*), std::vector<std::string>& out) {
    if (ps == nullptr || fn == nullptr) {
        return;
    }
    StringArray files, files2, proxies;
    guarded([&] { fn(ps, &files, &files2, &proxies); });
    guarded([&] {
        for (int i = 0; i < files.count && i < 64 && files.data != nullptr; ++i) {
            const void* str = static_cast<const char*>(files.data) + static_cast<std::size_t>(i) * 8;
            out.emplace_back(sym().bstring_get_text(str));
        }
    });
    // release what the engine allocated (the strings and the array buffer)
    guarded([&] {
        for (int i = 0; i < files.count && files.data != nullptr; ++i) {
            sym().bstring_dtor(static_cast<char*>(files.data) + static_cast<std::size_t>(i) * 8);
        }
        if (files.data != nullptr && sym().mem_free != nullptr) {
            sym().mem_free(sym().mem_instance(), files.data);
        }
    });
}

template <class T>
T rawField(const void* base, std::size_t offset) {
    T v{};
    std::memcpy(&v, static_cast<const char*>(base) + offset, sizeof(T));
    return v;
}

using Two = std::tuple<sol::object, sol::object>;

Two okResult(sol::state_view lua, sol::object value) { return {std::move(value), sol::make_object(lua, sol::lua_nil)}; }
Two failResult(sol::state_view lua, const std::string& reason) {
    return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, reason)};
}

// Clones `baseName` under a new name with the deterministic ID of that name. See docs/RE.md section 25 for what each step does.
Two cloneTemplate(sol::state_view lua, const std::string& newName, const std::string& baseName) {
    const Symbols& s = sym();
    if (s.tple_create_copy == nullptr || s.tple_set_name == nullptr || s.tple_set_id == nullptr || s.tple_get_id == nullptr ||
        s.scene_get_entity_by_id == nullptr) {
        return failResult(lua, "engine functions for creating templates are unavailable (risen.engine.missing())");
    }
    if (const std::string why = templates::nameProblem(newName); !why.empty()) {
        return failResult(lua, why);
    }
    auto& created = createdTemplates();
    if (const Resolved existing = resolveTemplate(newName); existing.tpl != nullptr) {
        const auto it = created.find(newName);
        if (it != created.end() && !it->second.retired && it->second.base == baseName) {
            SPASS_TRACE(trace::Channel::Template, "create '{}': already created from '{}' in this process", newName, baseName);
            return okResult(lua, sol::make_object(lua, LuaTemplate{newName}));
        }
        if (it != created.end() && !it->second.retired) {
            return failResult(lua, "template '" + newName + "' was already created from '" + it->second.base + "' (not '" + baseName + "')");
        }
        return failResult(lua, "a template called '" + newName + "' already exists (choose a new name)");
    }
    if (const auto it = created.find(newName); it != created.end() && it->second.retired) {
        // a clone that was removed: its entities and ID are still registered, so it gets its names back instead of a second copy
        if (it->second.base != baseName) {
            return failResult(lua, "template '" + newName + "' was created from '" + it->second.base + "' and removed; it can only come back from that base");
        }
        const bool ran = guarded([&] {
            const BString rootName(newName + ".tple");
            s.tple_set_name(it->second.root, rootName.get());
            const BString name(newName);
            s.tple_set_name(it->second.clone, name.get());
        });
        const Resolved back = resolveTemplate(newName);
        if (!ran || back.tpl != it->second.clone) {
            return failResult(lua, "the removed template '" + newName + "' could not be registered again");
        }
        it->second.retired = false;
        SPASS_TRACE(trace::Channel::Template, "create '{}' from '{}': removed clone registered again", newName, baseName);
        return okResult(lua, sol::make_object(lua, LuaTemplate{newName}));
    }
    const Resolved base = resolveTemplate(baseName);
    if (base.tpl == nullptr) {
        return failResult(lua, "base template: " + base.error);
    }
    if (!ensureLoaded(base.tpl)) {
        return failResult(lua, "the content of the base template '" + baseName + "' could not be loaded");
    }
    void* admin = sceneAdmin();
    if (admin == nullptr) {
        return failResult(lua, "the scene admin is not available (game not ready?)");
    }
    // Every shipped template is a pair: a hidden root ("<name>.tple", helper-parent flag, no property sets) with the real template
    // entity ("<name>") as its only child (docs/RE.md section 25). The name lookup only finds the child whose root is a single template.
    void* baseRoot = nullptr;
    guarded([&] {
        void* parent = rawField<void*>(base.tpl, 0x10);
        if (parent != nullptr && rawField<void*>(parent, 0x10) == nullptr && rawField<void*>(parent, 0x20) == base.tpl) {
            baseRoot = parent;
        }
    });
    if (baseRoot == nullptr) {
        return failResult(lua, "the base template '" + baseName + "' has an unexpected structure (no single root): cannot clone it");
    }
    PropertyId id;
    PropertyId rootId;
    const templates::TemplateId bytes = templates::idForName(newName);
    const templates::TemplateId rootBytes = templates::rootIdForName(newName);
    std::memcpy(id.guid, bytes.data(), bytes.size());
    std::memcpy(rootId.guid, rootBytes.data(), rootBytes.size());

    // Is an ID already taken by a *registered* entity? (An ID that only a pending proxy of a loaded savegame knows has no node yet:
    // that is the good case, the clone claims it.)
    for (const auto* probe : {&id, &rootId}) {
        void* holder = nullptr;
        if (!guarded([&] { holder = s.scene_get_entity_by_id(admin, probe, 2); })) {
            return failResult(lua, "the engine raised an exception while checking the template ID");
        }
        if (holder != nullptr) {
            return failResult(lua, "the ID derived from '" + newName + "' (" +
                                       templates::guidText(probe == &id ? bytes : rootBytes) +
                                       ") is already used by another entity: choose another name");
        }
    }

    void* root = nullptr;
    void* clone = nullptr;
    std::string failure;
    int step = 0;  // where an engine exception happened (reported in the error)
    const SkipNavPostCopy skipNav;
    const bool ran = guarded([&] {
        step = 1;
        root = s.tple_create_copy(baseRoot);
        step = 2;
        clone = s.tple_create_copy(base.tpl);
        step = 3;
        if (root == nullptr || clone == nullptr) {
            failure = "the engine could not copy the template";
            return;
        }
        const BString rootName(newName + ".tple");
        s.tple_set_name(root, rootName.get());
        step = 4;
        s.tple_set_id(root, &rootId);
        step = 5;
        const BString name(newName);
        s.tple_set_name(clone, name.get());
        step = 6;
        s.tple_set_id(clone, &id);
        step = 7;
        if (!s.node_attach_child(root, clone)) {
            failure = "the engine refused to attach the template to its root";
            return;
        }
        step = 8;
        const void* now = s.tple_get_id(clone);
        const void* nowRoot = s.tple_get_id(root);
        if (now == nullptr || std::memcmp(now, id.guid, sizeof(id.guid)) != 0 || nowRoot == nullptr ||
            std::memcmp(nowRoot, rootId.guid, sizeof(rootId.guid)) != 0) {
            failure = "the engine did not accept the template ID (collision)";
        }
    });
    // a failed attempt must not leave half-made entities answering to the base's name: take them out of the name map
    auto discard = [&] {
        if (s.scene_unregister_tpl_name == nullptr) {
            return;
        }
        for (void* e : {clone, root}) {
            if (e != nullptr) {
                guarded([&] { s.scene_unregister_tpl_name(admin, e); });
            }
        }
    };
    if (!ran) {
        discard();
        SPASS_TRACE(trace::Channel::Template, "create '{}' from '{}': engine exception in step {}", newName, baseName, step);
        return failResult(lua, "the engine raised an exception while cloning '" + baseName + "' (step " + std::to_string(step) + ")");
    }
    if (!failure.empty()) {
        discard();
        return failResult(lua, failure);
    }
    // the name must now resolve to the clone, through the same lookup every API of this layer uses
    const Resolved back = resolveTemplate(newName);
    if (back.tpl != clone) {
        return failResult(lua, "the clone was created but the name '" + newName + "' does not resolve to it (" +
                                   (back.error.empty() ? std::string("another entity answers to that name") : back.error) + ")");
    }
    created[newName] = CreatedTemplate{baseName, root, clone, false};
    SPASS_TRACE(trace::Channel::Template, "create '{}' from '{}': id {} sets {}", newName, baseName, templates::guidText(bytes),
                propertySetsOf(clone).size());
    return okResult(lua, sol::make_object(lua, LuaTemplate{newName}));
}

}  // namespace

void installTemplateHooks() {
    g_navPostCopy.install("gCNavigation_PS::OnPostCopy", sym().nav_post_copy, &detourNavPostCopy);
}

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

void* templateEntityPtr(const std::string& templateName, std::string& error) {
    const Resolved r = resolveTemplate(templateName);
    if (r.tpl == nullptr) {
        error = r.error;
        return nullptr;
    }
    if (!ensureLoaded(r.tpl)) {
        error = "the template content could not be loaded";
        return nullptr;
    }
    return r.tpl;
}

// entity:template() -> Template | nil, reason; entity:focus_name() / entity:description() -> the texts the engine shows
void addTemplateMethods(sol::usertype<LuaEntity>& ut) {
    auto textMethod = [](const char* cls, void* (*Symbols::*fn)(const void*, void*)) {
        return [cls, fn](sol::this_state ts, const LuaEntity& e) -> Two {
            sol::state_view lua(ts);
            void* entity = ptr(e);
            if (entity == nullptr) {
                return failResult(lua, "entity is gone");
            }
            const Symbols& s = sym();
            if (s.*fn == nullptr || s.ent_get_property_set == nullptr) {
                return failResult(lua, "the engine function is unavailable");
            }
            void* ps = nullptr;
            guarded([&] {
                const BString name(cls);
                ps = s.ent_get_property_set(entity, name.get());
            });
            if (ps == nullptr) {
                return failResult(lua, std::string("entity has no ") + cls);
            }
            return okResult(lua, sol::make_object(lua, plainTextOf(ps, s.*fn)));
        };
    };
    // entity:dependencies() -> { "It_Wpn_1H_Sword.xmsh", ... }: the mesh/actor resource files the entity's visual property sets loaded, as the engine's
    // own GetDependencies reports them (what the entity really renders, as opposed to the property text)
    ut["dependencies"] = [](sol::this_state ts, const LuaEntity& e) -> Two {
        sol::state_view lua(ts);
        void* entity = ptr(e);
        if (entity == nullptr) {
            return failResult(lua, "entity is gone");
        }
        const Symbols& s = sym();
        std::vector<std::string> files;
        for (const auto& [setName, ps] : propertySetsOf(entity)) {
            if (setName == "eCMesh_PS") {
                collectDependencies(ps, s.mesh_ps_dependencies, files);
            } else if (setName == "eCAnimation_PS") {
                collectDependencies(ps, s.anim_ps_dependencies, files);
            } else if (setName == "eCBodyPart_PS") {
                collectDependencies(ps, s.bodypart_ps_dependencies, files);
            }
        }
        sol::table out = lua.create_table();
        int i = 1;
        for (const std::string& f : files) {
            out[i++] = f;
        }
        return okResult(lua, out);
    };
    // entity:id() -> "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" | nil: the entity's unique ID (a GUID the savegame stores with it, unlike its name).
    ut["id"] = [](const LuaEntity& e) -> sol::optional<std::string> {
        void* entity = ptr(e);
        if (entity == nullptr) {
            return sol::nullopt;
        }
        std::string out;
        guarded([&] {
            // eCEntity::GetID is virtual (vtable slot 0x148/8: the engine's own eCEntityDynamicContext::WriteSaveGame calls it this way)
            using Fn = const unsigned char* (*)(void*);
            void** vtable = *reinterpret_cast<void***>(entity);
            const unsigned char* id = reinterpret_cast<Fn>(vtable[0x148 / 8])(entity);
            if (id != nullptr) {
                templates::TemplateId bytes{};
                std::memcpy(bytes.data(), id, bytes.size());
                out = templates::guidText(bytes);
            }
        });
        if (out.empty()) {
            return sol::nullopt;
        }
        return out;
    };
    ut["focus_name"] = textMethod("gCInteraction_PS", &Symbols::interaction_focus_name);
    ut["description"] = textMethod("gCItem_PS", &Symbols::item_plain_description);
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
            SPASS_TRACE(trace::Channel::Template, "patch_instances '{}': {} of {} live entities patched", t.name, patched, found.size());
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
        },
        // template:focus_name() -> the label the engine shows for the template's items (localization key FO_<name>), "" when it has none
        "focus_name", [&s](sol::this_state ts, const LuaTemplate& t) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr || !ensureLoaded(r.tpl)) {
                return failResult(lua, r.tpl == nullptr ? r.error : "the template content could not be loaded");
            }
            if (s.interaction_focus_name == nullptr) {
                return failResult(lua, "engine function gCInteraction_PS::GetPlainFocusName is unavailable");
            }
            void* ps = findSet(r.tpl, "gCInteraction_PS");
            if (ps == nullptr) {
                return failResult(lua, "template '" + t.name + "' has no gCInteraction_PS");
            }
            return okResult(lua, sol::make_object(lua, plainTextOf(ps, s.interaction_focus_name)));
        },
        // template:description() -> the item description the engine shows (localization key ITEMDESC_<name>), "" when it has none
        "description", [&s](sol::this_state ts, const LuaTemplate& t) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr || !ensureLoaded(r.tpl)) {
                return failResult(lua, r.tpl == nullptr ? r.error : "the template content could not be loaded");
            }
            if (s.item_plain_description == nullptr) {
                return failResult(lua, "engine function gCItem_PS::GetPlainDescription is unavailable");
            }
            void* ps = findSet(r.tpl, "gCItem_PS");
            if (ps == nullptr) {
                return failResult(lua, "template '" + t.name + "' is not an item (no gCItem_PS)");
            }
            return okResult(lua, sol::make_object(lua, plainTextOf(ps, s.item_plain_description)));
        },
        // template:id() -> "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx": the template's ID (what savegames store); nil, reason when it does not resolve
        "id", [](sol::this_state ts, const LuaTemplate& t) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            const std::string text = idTextOf(r.tpl);
            if (text.empty()) {
                return failResult(lua, "the template has no ID");
            }
            return okResult(lua, sol::make_object(lua, text));
        },
        // template:info() -> { id, created, base, flags, has_parent, has_children, ref_id, property_sets } engine bookkeeping for diagnostics
        "info", [](sol::this_state ts, const LuaTemplate& t) -> Two {
            sol::state_view lua(ts);
            const Resolved r = resolveTemplate(t.name);
            if (r.tpl == nullptr) {
                return failResult(lua, r.error);
            }
            sol::table out = lua.create_table();
            guarded([&] {
                out["flags"] = static_cast<int>(rawField<std::uint8_t>(r.tpl, 0x58));
                out["has_parent"] = rawField<void*>(r.tpl, 0x10) != nullptr;
                out["has_children"] = rawField<void*>(r.tpl, 0x20) != nullptr;
                out["ref_id"] = rawField<void*>(r.tpl, 0x60) != nullptr;
                out["property_sets"] = rawField<std::int32_t>(r.tpl, 0x48);
            });
            out["id"] = idTextOf(r.tpl);
            guarded([&] {
                const void* parent = rawField<void*>(r.tpl, 0x10);
                if (parent != nullptr) {
                    sol::table p = lua.create_table();
                    p["flags"] = static_cast<int>(rawField<std::uint8_t>(parent, 0x58));
                    p["name"] = templateNameOf(parent);
                    p["id"] = idTextOf(parent);
                    p["first_child_is_this"] = rawField<void*>(parent, 0x20) == r.tpl;
                    p["has_sibling"] = rawField<void*>(r.tpl, 0x18) != nullptr;
                    p["parent_has_parent"] = rawField<void*>(parent, 0x10) != nullptr;
                    p["parent_sets"] = rawField<std::int32_t>(parent, 0x48);
                    out["parent"] = p;
                }
                out["own_name"] = templateNameOf(r.tpl);
            });
            const auto& created = createdTemplates();
            const auto it = created.find(t.name);
            out["created"] = it != created.end() && !it->second.retired;
            if (it != created.end() && !it->second.retired) {
                out["base"] = it->second.base;
            }
            return okResult(lua, out);
        });
    (void)ut;

    sol::table tpl = risen["template"];
    // risen.template.get("It_1H_Sword") -> Template | nil, reason
    tpl.set_function("get", [](sol::this_state ts, const std::string& name) -> Two {
        sol::state_view lua(ts);
        const Resolved r = resolveTemplate(name);
        if (r.tpl == nullptr) {
            SPASS_TRACE(trace::Channel::Template, "get '{}' -> {}", name, r.error);
            return failResult(lua, r.error);
        }
        SPASS_TRACE(trace::Channel::Template, "get '{}' -> ok", name);
        const std::string real = templateNameOf(r.tpl);
        return okResult(lua, sol::make_object(lua, LuaTemplate{real.empty() ? name : real}));
    });

    // risen.template.clone(new_name, base_name) -> Template | nil, reason: low-level clone of a template under a new unique name with the
    // deterministic ID of that name (see risen.template.create for the overrides)
    tpl.set_function("clone", [](sol::this_state ts, const std::string& newName, const std::string& baseName) -> Two {
        return cloneTemplate(sol::state_view(ts), newName, baseName);
    });
    // risen.template.id_for(name) -> the ID a clone of that name gets (pure function, no engine access)
    tpl.set_function("id_for", [](const std::string& name) { return templates::guidText(templates::idForName(name)); });
    // risen.template.name_problem(name) -> nil when the name is a valid name for a new template, else the reason
    tpl.set_function("name_problem", [](sol::this_state ts, const std::string& name) -> sol::object {
        const std::string why = templates::nameProblem(name);
        return why.empty() ? sol::make_object(ts, sol::lua_nil) : sol::make_object(ts, why);
    });
    // risen.template.created() -> { [name] = base name } the clones this process created
    tpl.set_function("created", [](sol::this_state ts) {
        sol::state_view lua(ts);
        sol::table out = lua.create_table();
        for (const auto& [name, rec] : createdTemplates()) {
            if (!rec.retired) {
                out[name] = rec.base;
            }
        }
        return out;
    });
    // risen.template.remove(name) -> true | nil, reason: gives a clone this layer created its names back (Template(name), spawn, give and the
    // dialogue commands no longer find it). Refused while live entities of it exist. Entities that already refer to it (saved stacks, items that were
    // handed out) keep working: the clone and its ID stay registered until the process ends, and create() with the same base brings the names back.
    tpl.set_function("remove", [](sol::this_state ts, const std::string& name) -> Two {
        sol::state_view lua(ts);
        const Symbols& s = sym();
        auto& created = createdTemplates();
        const auto it = created.find(name);
        if (it == created.end() || it->second.retired) {
            return failResult(lua, "'" + name + "' is not a template this mod layer created");
        }
        if (s.scene_unregister_tpl_name == nullptr) {
            return failResult(lua, "engine function eCSceneAdmin::UnregisterNameInfo is unavailable");
        }
        void* admin = sceneAdmin();
        if (admin == nullptr) {
            return failResult(lua, "the scene admin is not available (game not ready?)");
        }
        if (const sol::optional<int> n = countInstances(it->second.clone); n && *n > 0) {
            return failResult(lua, std::to_string(*n) + " live entities of '" + name + "' exist: remove them first (Entity:kill)");
        }
        const bool ran = guarded([&] {
            s.scene_unregister_tpl_name(admin, it->second.clone);
            s.scene_unregister_tpl_name(admin, it->second.root);
        });
        if (!ran || resolveTemplate(name).tpl != nullptr) {
            return failResult(lua, "the engine did not release the name '" + name + "'");
        }
        it->second.retired = true;
        SPASS_TRACE(trace::Channel::Template, "remove '{}'", name);
        return okResult(lua, sol::make_object(lua, true));
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

    // risen.entity.by_id("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx") -> Entity | nil: looks an entity up by its unique ID (see Entity:id()) in the scene admin.
    sol::table entityTable = risen["entity"];
    entityTable.set_function("by_id", [&s](sol::this_state ts, const std::string& text) -> sol::object {
        sol::state_view lua(ts);
        templates::TemplateId bytes{};
        {
            // parse the text form of Entity:id() (guidText's inverse)
            std::string hex;
            for (char c : text) {
                if (c != '-') {
                    hex.push_back(c);
                }
            }
            if (hex.size() != 32 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
                return sol::make_object(lua, sol::lua_nil);
            }
            unsigned char t[16];
            for (int i = 0; i < 16; ++i) {
                t[i] = static_cast<unsigned char>(std::stoi(hex.substr(static_cast<std::size_t>(i) * 2, 2), nullptr, 16));
            }
            // text order is byte 3 2 1 0 - 5 4 - 7 6 - 8 9 - 10..15
            const int order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
            for (int i = 0; i < 16; ++i) {
                bytes[static_cast<std::size_t>(order[i])] = t[i];
            }
        }
        void* admin = sceneAdmin();
        if (admin == nullptr || s.scene_get_entity_by_id == nullptr) {
            return sol::make_object(lua, sol::lua_nil);
        }
        PropertyId id;
        std::memcpy(id.guid, bytes.data(), bytes.size());
        void* node = nullptr;
        guarded([&] { node = s.scene_get_entity_by_id(admin, &id, 0); });
        if (node == nullptr) {
            return sol::make_object(lua, sol::lua_nil);
        }
        // the lookup returns the eCNode; an entity is the same object (eCEntity derives from it)
        return makeEntityObject(lua, node);
    });

    installTemplatePatchHelper(lua, risen);
}

void installTemplateCreateHelpers(sol::state_view lua, sol::table risen) {
    sol::load_result loaded = lua.load(kTemplateCreateChunk, "=risen.template.create");
    if (!loaded.valid()) {
        spdlog::error("lua: template create helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(risen);
    if (!res.valid()) {
        spdlog::error("lua: template create helpers failed to run: {}", res.get<sol::error>().what());
    }
}

void bindTemplateCreate(sol::state_view lua, sol::table risen) { installTemplateCreateHelpers(lua, risen); }

void installTemplatePatchHelper(sol::state_view lua, sol::table risen) {
    // risen.template.patch(name, { gCItem_PS = { GoldValue = 100 } } [, { instances = true, dry_run = true }]) -> true, count [, instance_count] | nil, reason
    // Everything is validated (template, property sets, properties) before the first value is written. With `instances = true` the
    // same values are also written into every live entity created from the template (only the listed properties: per-instance state
    // such as hit points, amount or inventory stays untouched, unlike Template:patch_instances()). `dry_run = true` only validates (returns
    // true, number of values that would be written).
    const char* chunk = R"LUA(
local risen = ...
local function tr(...) local t = risen.trace if t and t.enabled("template") then t("template", ...) end end

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
    if #problems > 0 then tr("patch", name, "refused:", table.concat(problems, "; ")) return nil, table.concat(problems, "; ") end
    if opts.dry_run then return true, #jobs end
    local applied, failed = 0, {}
    for _, job in ipairs(jobs) do
        local ok, err = tpl:set_property(job[1], job[2], job[3])
        if ok then applied = applied + 1 else failed[#failed + 1] = err or (job[1] .. "." .. job[2]) end
    end
    if #failed > 0 then tr("patch", name, "failed:", table.concat(failed, "; ")) return nil, table.concat(failed, "; ") end
    if opts.instances then
        local touched = 0
        for _, e in ipairs(tpl:instances(1000000) or {}) do
            local any = false
            for _, job in ipairs(jobs) do
                if e:set_property(job[1], job[2], job[3]) then any = true end
            end
            if any then touched = touched + 1 end
        end
        tr("patch", name, applied .. " value(s) written, " .. touched .. " live entit(ies) updated")
        return true, applied, touched
    end
    tr("patch", name, applied .. " value(s) written (template only)")
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
