#include <cstdint>
#include <string>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/bindings.h"
#include "game/lua_entity.h"
#include "game/properties.h"
#include "hook/hook_manager.h"

// risen.interact.focus / set_focus / info / current (docs/RE.md section 31, docs/LUA_API.md "Interaction building blocks").
//
//   * The focus is the entity under the crosshair that the use key acts on: gCFocusModule2 (Game.dll) keeps a sorted candidate list that is rebuilt every
//     frame (Process -> UpdateFocus); GetFocusEntity returns the first candidate. The engine's own script wrapper `Focus::GetFocusEntity` is what
//     PS_Normal_Loop passes to PSInteraction::BeginInteraction when the use key is pressed.
//   * An interactive mob has one gCInteraction object per user kind in its gCInteraction_PS (property set 0x28, `GetInteractionCount/At`), each with the
//     scripts the engine runs for it: CanInteractScript (a plain script, asked by BeginInteraction), PreInteractScript / InteractScript (AI functions:
//     resumable scripts that walk, play animations and open the loot window) and PostInteractScript (a plain script, run by EndInteraction).
//   * The user's own gCInteraction_PS remembers the current target (+0x1b8 proxy), the mob's remembers the current user (+0x1a8).

namespace spass::game {
namespace {

// The pinned focus (risen.interact.set_focus(entity, true)): gCFocusModule2::GetFocusEntity, which the script wrapper `Focus::GetFocusEntity` (what the use
// key hands to BeginInteraction) and risen.interact.focus() both call, answers with this entity while it is set. The engine rebuilds its candidate list
// every frame (and scripts re-enable that), so SetFocusEntity alone lasts one frame.
Hook<void* (*)(const void*)> g_focusGet;
bool g_pinned = false;
LuaEntity g_pinnedEntity;

void* detourFocusGet(const void* self) {
    if (g_pinned) {
        if (void* e = ptr(g_pinnedEntity)) {
            return e;
        }
    }
    return g_focusGet.original(self);
}

void* interactionPs(void* entity) {
    const Symbols& s = sym();
    if (entity == nullptr || s.ent_get_property_set == nullptr) {
        return nullptr;
    }
    void* ps = nullptr;
    guarded([&] {
        const BString cls("gCInteraction_PS");
        ps = s.ent_get_property_set(entity, cls.get());
    });
    return ps;
}

void* focusModule() {
    const Symbols& s = sym();
    void* module = nullptr;
    if (s.focus_instance != nullptr) {
        guarded([&] { module = s.focus_instance(); });
    }
    return module;
}

sol::table propsOf(sol::state_view L, void* object) {
    sol::table props = L.create_table();
    for (const auto& [name, type] : propertyList(object)) {
        (void)type;
        if (const auto value = propertyGet(object, name)) {
            props[name] = *value;
        }
    }
    return props;
}

}  // namespace

// eCEntity::Kill is the one way an entity leaves the world: a pinned focus on it must go first, the engine would use the freed entity.
void focusEntityKilled(void* entity) {
    if (g_pinned && ptr(g_pinnedEntity) == entity) {
        g_pinned = false;
    }
}

void installFocusHook() {
    const Symbols& s = sym();
    if (s.focus_get_entity != nullptr && !g_focusGet.install("gCFocusModule2::GetFocusEntity", s.focus_get_entity, &detourFocusGet)) {
        spdlog::warn("hook 'gCFocusModule2::GetFocusEntity' could not be installed: risen.interact.set_focus(.., true) cannot pin");
    }
}

void bindInteractExtras(sol::state_view lua, sol::table risen) {
    sol::table interact = risen["interact"];
    if (!interact.valid()) {
        interact = lua.create_table();
        risen["interact"] = interact;
    }

    // risen.interact.focus() -> Entity | nil: what the engine's focus module has selected (the entity under the crosshair)
    interact.set_function("focus", [](sol::this_state ts) -> sol::object {
        const Symbols& s = sym();
        void* entity = nullptr;
        void* module = focusModule();
        if (module != nullptr && s.focus_get_entity != nullptr) {
            guarded([&] { entity = s.focus_get_entity(module); });
        }
        return makeEntityObject(ts, entity);
    });

    // risen.interact.set_focus(entity|nil [, pin]) -> bool: makes `entity` the focus for one frame (the engine rebuilds the candidate list every frame);
    // with `pin = true` it stays the focus (what risen.interact.focus() and the use key see) until set_focus(nil) / set_focus(x, false). Test aid, also
    // useful for guided interactions.
    interact.set_function("set_focus", [](sol::object target, sol::optional<bool> pin) -> bool {
        const Symbols& s = sym();
        void* module = focusModule();
        if (module == nullptr || s.focus_set_entity == nullptr) {
            return false;
        }
        void* entity = nullptr;
        if (target.is<LuaEntity>()) {
            entity = ptr(target.as<LuaEntity>());
            if (entity == nullptr) {
                return false;
            }
        }
        g_pinned = pin.value_or(false) && entity != nullptr;
        if (g_pinned) {
            g_pinnedEntity = target.as<LuaEntity>();
        }
        return guarded([&] { s.focus_set_entity(module, entity); });
    });

    // risen.interact.info(entity) -> table | nil, reason
    //   { use_type, interactions = { { type, can, pre, interact, post, props = { all properties of the gCInteraction } }, ... }, user, target }
    interact.set_function("info", [](sol::this_state ts, const LuaEntity& e) -> std::tuple<sol::object, sol::object> {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.inter_ps_count == nullptr || s.inter_ps_at == nullptr) {
            return {sol::make_object(L, sol::lua_nil), sol::make_object(L, "engine functions gCInteraction_PS::GetInteractionCount/At are unavailable")};
        }
        void* entity = ptr(e);
        if (entity == nullptr) {
            return {sol::make_object(L, sol::lua_nil), sol::make_object(L, "entity is gone")};
        }
        void* ps = interactionPs(entity);
        if (ps == nullptr) {
            return {sol::make_object(L, sol::lua_nil), sol::make_object(L, "entity has no gCInteraction_PS")};
        }
        sol::table out = L.create_table();
        if (const auto useType = propertyGet(ps, "UseType")) {
            out["use_type"] = *useType;
        }
        sol::table list = L.create_table();
        int count = 0;
        guarded([&] { count = s.inter_ps_count(ps); });
        if (count < 0 || count > 64) {
            count = 0;
        }
        for (int i = 0; i < count; ++i) {
            void* inter = nullptr;
            guarded([&] { inter = s.inter_ps_at(ps, i); });
            if (inter == nullptr) {
                continue;
            }
            sol::table t = L.create_table();
            const auto field = [&](const char* key, const char* prop) {
                if (const auto v = propertyGet(inter, prop)) {
                    t[key] = *v;
                }
            };
            field("type", "Type");
            field("can", "CanInteractScript");
            field("pre", "PreInteractScript");
            field("interact", "InteractScript");
            field("post", "PostInteractScript");
            t["props"] = propsOf(L, inter);
            list[i + 1] = t;
        }
        out["interactions"] = list;
        void* user = nullptr;
        void* target = nullptr;
        guarded([&] {
            if (s.inter_ps_user != nullptr) {
                user = s.inter_ps_user(ps);
            }
            if (s.inter_ps_target != nullptr) {
                target = s.inter_ps_target(ps);
            }
        });
        out["user"] = makeEntityObject(L, user);
        out["target"] = makeEntityObject(L, target);
        return {sol::make_object(L, out), sol::make_object(L, sol::lua_nil)};
    });

    // risen.entity.npcs([center [, radius]]) -> Entity[]: every NPC and monster of the world (Entity::GetNPCs: the player included), or those within `radius`
    // (game units, as Entity:distance_to) of the entity `center`.
    sol::table entityTable = risen["entity"];
    entityTable.set_function("npcs", [](sol::this_state ts, sol::optional<LuaEntity> center, sol::optional<double> radius) {
        sol::state_view L(ts);
        const Symbols& s = sym();
        sol::table out = L.create_table();
        if (s.ent_get_npcs == nullptr) {
            return out;
        }
        void* centerPtr = center ? ptr(*center) : nullptr;
        struct Array {  // bTObjArray<Entity>: { Entity* data; int count; int capacity }
            void** data = nullptr;
            std::int32_t count = 0;
            std::int32_t capacity = 0;
        } list;
        if (!guarded([&] { s.ent_get_npcs(&list); }) || list.data == nullptr) {
            return out;
        }
        int n = 0;
        guarded([&] {
            for (int i = 0; i < list.count && i < 100000; ++i) {
                void* e = list.data[i];
                if (e == nullptr) {
                    continue;
                }
                if (centerPtr != nullptr && radius) {
                    EntityHolder a{centerPtr};
                    EntityHolder b{e};
                    if (s.ent_distance_to(&a, &b) > static_cast<float>(*radius)) {
                        continue;
                    }
                }
                out[++n] = makeEntityObject(L, e);
            }
        });
        guarded([&] {
            if (s.mem_free != nullptr) {
                s.mem_free(s.mem_instance(), list.data);
            }
        });
        return out;
    });

    // risen.interact.current(user) -> target Entity | nil: the mob the character is interacting with right now (between BeginInteraction and
    // EndInteraction, i.e. while the AI state ZS_Interact_* runs)
    interact.set_function("current", [](sol::this_state ts, const LuaEntity& user) -> sol::object {
        const Symbols& s = sym();
        void* entity = ptr(user);
        void* ps = interactionPs(entity);
        void* target = nullptr;
        if (ps != nullptr && s.inter_ps_target != nullptr) {
            guarded([&] { target = s.inter_ps_target(ps); });
        }
        return makeEntityObject(ts, target);
    });
}

}  // namespace spass::game
