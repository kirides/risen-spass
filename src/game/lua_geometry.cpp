#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>

#include "common/pose.h"
#include "common/trace.h"
#include "game/abi.h"
#include "game/geometry_lua.h"
#include "game/lua_entity.h"

// Geometry (M13, docs/RE.md section 38, docs/LUA_API.md "Geometry, line of sight and randomness"): the natives under risen.math / risen.world.raycast /
// line_of_sight / can_see / is_seen_by.
//
//   risen.world.trace(x1, y1, z1, x2, y2, z2 [, ignore [, bodies]])   the engine's physics-scene ray test (eCPhysicsScene::TraceRayNearestHit): what the AI's
//                                                          `CanSee` / `eCGeometryEntity::IsFreeLOS` use
//   risen.world.engine_free_los(a, b)                       the engine's own entity-to-entity test, bounding box centres, both entities ignored
//   risen.world.camera()                                    the render camera (the player's view): position, direction, field of view
//   Entity:bounds()                                         the world bounding box
//   Entity:pose() / Entity:set_pose(spec)                   the world matrix as position + angles (common/pose.h), moved with Entity::MoveTo
//
// The Lua layer on top (vectors, yaw conventions, fields of view, random streams) is geometry_lua.h.

namespace spass::game {
namespace {

using Two = std::tuple<sol::object, sol::object>;

Two failResult(sol::state_view lua, const std::string& reason) { return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, reason)}; }

// A finite number of a sane size (positions beyond 1e7 make the physics SDK return garbage; NaN/inf would hang searches).
bool coord(const sol::object& o, float& out) {
    if (!o.valid() || o.get_type() != sol::type::number) {
        return false;
    }
    const double v = o.as<double>();
    if (!std::isfinite(v) || std::fabs(v) > 1.0e7) {
        return false;
    }
    out = static_cast<float>(v);
    return true;
}

// eCPhysicsScene layout read from the decompilation of TraceRay*: +0x48 the NxScene*, +0x128 the result record, +0x130 the ignore list (a bTPtrArray:
// vftable, data* at +8, count at +0x10, capacity at +0x14; reset by the scene after every query), +0x178 "do not trace" flag.
constexpr std::size_t kSceneNx = 0x48;
constexpr std::size_t kSceneResult = 0x128;
constexpr std::size_t kSceneIgnore = 0x130;
constexpr std::size_t kSceneBusy = 0x178;
// result record: +0 hit flag, +8 hit entity, +0xcc hit position (flag 0x80), +0xfc distance (flag 0x100)
// TraceRay* flags (bit 0 static shapes, bit 1 dynamic shapes, bit 3 = skip characters; bits 2 and 4 not understood, no effect on a peasant): the AI's test passes 0x1f, which
// only sees level geometry and objects (measured: a peasant standing in the ray does not block it); 0x3 lets characters and other dynamic bodies block as well.
constexpr unsigned long kFlagsLos = 0x1f;
constexpr unsigned long kFlagsBodies = 0x3;
constexpr unsigned long kFlagPosition = 0x80;
constexpr unsigned long kFlagDistance = 0x100;
constexpr std::size_t kMaxIgnore = 64;

struct PtrArray {  // the engine's bTPtrArray<eCGeometryEntity*> as the assignment operator reads it
    const void* vftable;
    void** data;
    int count;
    int capacity;
};

void* sceneOrNull() {
    const Symbols& s = sym();
    if (s.phys_instance == nullptr || s.phys_trace_nearest == nullptr || s.phys_set_ignore == nullptr) {
        return nullptr;
    }
    void* scene = nullptr;
    guarded([&] {
        scene = s.phys_instance();
        if (scene != nullptr) {
            const char* base = static_cast<const char*>(scene);
            if (*reinterpret_cast<void* const*>(base + kSceneNx) == nullptr || base[kSceneBusy] == 1) {
                scene = nullptr;  // TraceRay* answer "no hit" for such a scene: that must not look like open air
            }
        }
    });
    return scene;
}

struct TraceResult {
    bool hit = false;
    float distance = 0;
    float point[3] = {};
    void* entity = nullptr;
};

bool runTrace(void* scene, const float from[3], const float to[3], const std::vector<void*>& ignore, unsigned long flags, TraceResult& out) {
    const Symbols& s = sym();
    float dir[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    const float length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    out.distance = length;
    out.point[0] = to[0];
    out.point[1] = to[1];
    out.point[2] = to[2];
    if (!(length > 1e-3f)) {
        return true;  // nothing between a point and itself
    }
    return guarded([&] {
        const char* base = static_cast<const char*>(scene);
        // the ignore list: copied into the scene by SetQueryIgnoreEntities, which also reuses the vftable of the scene's own array as the type of ours
        PtrArray list{*reinterpret_cast<const void* const*>(base + kSceneIgnore), nullptr, 0, 0};
        std::vector<void*> storage(ignore.begin(), ignore.end());
        if (!storage.empty()) {
            list.data = storage.data();
            list.count = list.capacity = static_cast<int>(storage.size());
            s.phys_set_ignore(scene, &list);
        }
        const bool hit = s.phys_trace_nearest(scene, from, dir, nullptr, flags | kFlagPosition | kFlagDistance, nullptr, 0, 0);
        out.hit = hit;
        if (hit) {
            const char* result = *reinterpret_cast<const char* const*>(base + kSceneResult);
            if (result != nullptr) {
                out.entity = *reinterpret_cast<void* const*>(result + 8);
                const float d = *reinterpret_cast<const float*>(result + 0xfc);
                if (std::isfinite(d) && d >= 0.0f && d <= length * 1.01f + 1.0f) {
                    out.distance = d;
                } else {
                    out.distance = 0;
                }
                const float* p = reinterpret_cast<const float*>(result + 0xcc);
                if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2])) {
                    out.point[0] = p[0];
                    out.point[1] = p[1];
                    out.point[2] = p[2];
                }
            }
        }
        if (!storage.empty()) {
            // the scene clears the list itself after a query on the slow path; make sure nothing of ours stays for the engine's own queries
            PtrArray none{list.vftable, nullptr, 0, 0};
            s.phys_set_ignore(scene, &none);
        }
    });
}

// risen.world.trace(x1, y1, z1, x2, y2, z2 [, ignore [, bodies]]) -> blocked, distance, hx, hy, hz, entity | nil, reason
sol::variadic_results traceNative(sol::this_state ts, sol::object a1, sol::object a2, sol::object a3, sol::object a4, sol::object a5, sol::object a6,
                                  sol::object ignoreArg, sol::object bodiesArg) {
    sol::state_view L(ts);
    sol::variadic_results out;
    const auto fail = [&](const char* why) {
        out.push_back(sol::make_object(L, sol::lua_nil));
        out.push_back(sol::make_object(L, std::string(why)));
        return out;
    };
    float from[3], to[3];
    if (!coord(a1, from[0]) || !coord(a2, from[1]) || !coord(a3, from[2]) || !coord(a4, to[0]) || !coord(a5, to[1]) || !coord(a6, to[2])) {
        return fail("expected six finite coordinates x1, y1, z1, x2, y2, z2 (|value| <= 1e7)");
    }
    std::vector<void*> ignore;
    if (ignoreArg.valid() && ignoreArg.get_type() != sol::type::lua_nil) {
        const auto add = [&](const sol::object& o) {
            if (!o.is<LuaEntity>()) {
                return false;
            }
            void* e = ptr(o.as<LuaEntity>());
            if (e == nullptr) {
                return false;
            }
            if (std::find(ignore.begin(), ignore.end(), e) == ignore.end()) {
                ignore.push_back(e);
            }
            return true;
        };
        if (ignoreArg.is<LuaEntity>()) {
            if (!add(ignoreArg)) {
                return fail("the entity to ignore is a stale handle");
            }
        } else if (ignoreArg.is<sol::table>()) {
            sol::table t = ignoreArg.as<sol::table>();
            for (std::size_t i = 1; i <= kMaxIgnore + 1; ++i) {
                sol::object v = t[i];
                if (!v.valid() || v.get_type() == sol::type::lua_nil) {
                    break;
                }
                if (i > kMaxIgnore) {
                    return fail("at most 64 entities can be ignored");
                }
                if (!add(v)) {
                    return fail("the list of entities to ignore holds something that is not a valid entity");
                }
            }
        } else {
            return fail("the entities to ignore must be an entity or a list of entities");
        }
    }
    void* scene = sceneOrNull();
    if (scene == nullptr) {
        return fail("the physics scene is not available (no world loaded?)");
    }
    TraceResult r;
    const bool bodies = bodiesArg.valid() && bodiesArg.get_type() == sol::type::boolean && bodiesArg.as<bool>();
    if (!runTrace(scene, from, to, ignore, bodies ? kFlagsBodies : kFlagsLos, r)) {
        return fail("the engine raised an exception during the ray test");
    }
    SPASS_TRACE(trace::Channel::Script, "trace ({:.0f}, {:.0f}, {:.0f}) -> ({:.0f}, {:.0f}, {:.0f}): {} at {:.0f} entity {}", from[0], from[1], from[2], to[0], to[1], to[2],
                r.hit ? "blocked" : "clear", r.distance, r.entity != nullptr ? entityNameOf(r.entity) : std::string("-"));
    out.push_back(sol::make_object(L, r.hit));
    out.push_back(sol::make_object(L, r.distance));
    out.push_back(sol::make_object(L, r.point[0]));
    out.push_back(sol::make_object(L, r.point[1]));
    out.push_back(sol::make_object(L, r.point[2]));
    out.push_back(r.hit && r.entity != nullptr ? makeEntityObject(L, r.entity) : sol::make_object(L, sol::lua_nil));
    return out;
}

// risen.world.engine_free_los(a, b [, strict]) -> bool | nil, reason: eCGeometryEntity::IsFreeLOS(a, b), the entity-to-entity test of the AI (bounding box centres, a quarter of the
// height up; both entities themselves are ignored). `strict` false (default true) leaves out the engine's extra filter bit (flags 0xf instead of 0x1f).
Two engineFreeLos(sol::this_state ts, sol::object aArg, sol::object bArg, sol::object strictArg) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    if (!aArg.valid() || !aArg.is<LuaEntity>() || !bArg.valid() || !bArg.is<LuaEntity>()) {
        return failResult(L, "expected two entities");
    }
    void* a = ptr(aArg.as<LuaEntity>());
    void* b = ptr(bArg.as<LuaEntity>());
    if (a == nullptr || b == nullptr) {
        return failResult(L, "entity is gone");
    }
    if (s.geo_is_free_los == nullptr || sceneOrNull() == nullptr) {
        return failResult(L, "the engine's line-of-sight test is not available (no world loaded?)");
    }
    const bool flag = strictArg.valid() && strictArg.get_type() == sol::type::boolean && !strictArg.as<bool>();
    bool free = false;
    if (!guarded([&] { free = s.geo_is_free_los(a, b, flag); })) {
        return failResult(L, "the engine raised an exception during the test");
    }
    return {sol::make_object(L, free), sol::make_object(L, sol::lua_nil)};
}

// risen.world.camera() -> { x, y, z, dx, dy, dz, fov, aspect } | nil, reason: the render camera (the player's view; position, unit view direction, field of view in degrees)
Two cameraNative(sol::this_state ts) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    if (s.app_instance == nullptr || s.app_current_camera == nullptr || s.cam_get_position == nullptr || s.cam_get_view == nullptr || s.cam_get_fov == nullptr) {
        return failResult(L, "the engine's camera functions are not available");
    }
    float pos[3] = {}, dir[3] = {}, fov = 0, aspect = 0;
    bool ok = false;
    if (!guarded([&] {
            void* app = s.app_instance();
            if (app == nullptr) {
                return;
            }
            void* cam = s.app_current_camera(app);
            if (cam == nullptr) {
                return;
            }
            const float* p = s.cam_get_position(cam);
            // The engine's "Z axis" of the camera (eCCameraBase::GetZAxis) is the third ROW of its VIEW matrix, i.e. (right.z, up.z, forward.z): not the view direction (measured: it
            // looked mirrored). In a view matrix the camera's axes are the COLUMNS: right = column 0, up = column 1, forward = column 2.
            const float* m = s.cam_get_view(cam);
            if (m == nullptr) {
                return;
            }
            dir[0] = m[2];
            dir[1] = m[6];
            dir[2] = m[10];
            const float* f = s.cam_get_fov(cam);
            if (p == nullptr || f == nullptr) {
                return;
            }
            pos[0] = p[0];
            pos[1] = p[1];
            pos[2] = p[2];
            fov = *f;
            if (s.cam_get_aspect != nullptr) {
                if (const float* a = s.cam_get_aspect(cam)) {
                    aspect = *a;
                }
            }
            ok = true;
        }) ||
        !ok) {
        return failResult(L, "there is no camera (no world loaded?)");
    }
    for (float v : {pos[0], pos[1], pos[2], dir[0], dir[1], dir[2], fov}) {
        if (!std::isfinite(v)) {
            return failResult(L, "the camera is not set up yet");
        }
    }
    sol::table t = L.create_table();
    t["x"] = pos[0];
    t["y"] = pos[1];
    t["z"] = pos[2];
    t["dx"] = dir[0];
    t["dy"] = dir[1];
    t["dz"] = dir[2];
    t["fov"] = fov * 180.0f / 3.14159265358979f;  // the engine stores radians (1.05 = 60 degrees)
    t["aspect"] = aspect;
    return {sol::make_object(L, t), sol::make_object(L, sol::lua_nil)};
}

using Bounds = std::tuple<sol::optional<float>, sol::optional<float>, sol::optional<float>, sol::optional<float>, sol::optional<float>, sol::optional<float>>;

}  // namespace

void addGeometryMethods(sol::usertype<LuaEntity>& ut) {
    // Entity:pose() -> { x, y, z, yaw, pitch, roll, scale = { sx, sy, sz }, matrix = { 16 } } | nil: the world matrix (Entity::GetPose)
    ut["pose"] = [](sol::this_state ts, const LuaEntity& e) -> sol::object {
        sol::state_view L(ts);
        void* entity = ptr(e);
        if (entity == nullptr || sym().ent_get_pose == nullptr) {
            return sol::make_object(L, sol::lua_nil);
        }
        pose::Matrix m{};
        if (!guarded([&] {
                EntityHolder h{entity};
                sym().ent_get_pose(&h, m.data());
            }) ||
            !pose::sane(m)) {
            return sol::make_object(L, sol::lua_nil);
        }
        const pose::Angles a = pose::decompose(m);
        const auto sc = pose::scale_of(m);
        sol::table t = L.create_table();
        t["x"] = m[12];
        t["y"] = m[13];
        t["z"] = m[14];
        t["yaw"] = a.yaw;
        t["pitch"] = a.pitch;
        t["roll"] = a.roll;
        t["scale"] = L.create_table_with(1, sc[0], 2, sc[1], 3, sc[2]);
        sol::table mt = L.create_table(16, 0);
        for (int i = 0; i < 16; ++i) mt[i + 1] = m[i];
        t["matrix"] = mt;
        return sol::make_object(L, t);
    };
    // Entity:set_pose({ x?, y?, z?, yaw?, pitch?, roll? } | { matrix = { 16 } }) -> true | nil, reason: moves and turns the entity (Entity::MoveTo: also
    // resets its rigid body, navigation and movement). Missing coordinates / angles keep the current ones; the scale stays.
    ut["set_pose"] = [](sol::this_state ts, const LuaEntity& e, sol::object specArg) -> Two {
        sol::state_view L(ts);
        void* entity = ptr(e);
        if (entity == nullptr) {
            return failResult(L, "the entity is gone");
        }
        if (sym().ent_get_pose == nullptr || sym().ent_move_to == nullptr) {
            return failResult(L, "Entity::GetPose / MoveTo are not available in this build");
        }
        if (!specArg.is<sol::table>()) {
            return failResult(L, "expected a table { x, y, z, yaw, pitch, roll } or { matrix = { 16 numbers } }");
        }
        sol::table spec = specArg.as<sol::table>();
        pose::Matrix cur{};
        if (!guarded([&] {
                EntityHolder h{entity};
                sym().ent_get_pose(&h, cur.data());
            })) {
            return failResult(L, "the engine raised an exception reading the pose");
        }
        pose::Matrix m{};
        const sol::object mo = spec["matrix"];
        if (mo.valid() && mo.get_type() != sol::type::lua_nil) {
            if (!mo.is<sol::table>()) {
                return failResult(L, "matrix: expected 16 numbers (row-major, position in row 3)");
            }
            sol::table mt = mo.as<sol::table>();
            for (int i = 0; i < 16; ++i) {
                if (!coord(mt[i + 1], m[i])) {
                    return failResult(L, "matrix: expected 16 finite numbers (row-major, position in row 3)");
                }
            }
        } else {
            float pos[3] = {cur[12], cur[13], cur[14]};
            const char* keys[3] = {"x", "y", "z"};
            for (int i = 0; i < 3; ++i) {
                const sol::object v = spec[keys[i]];
                if (v.valid() && v.get_type() != sol::type::lua_nil && !coord(v, pos[i])) {
                    return failResult(L, std::string(keys[i]) + ": expected a finite number (|value| <= 1e7)");
                }
            }
            pose::Angles a = pose::decompose(cur);
            float* angles[3] = {&a.yaw, &a.pitch, &a.roll};
            const char* akeys[3] = {"yaw", "pitch", "roll"};
            bool turned = false;
            for (int i = 0; i < 3; ++i) {
                const sol::object v = spec[akeys[i]];
                if (!v.valid() || v.get_type() == sol::type::lua_nil) continue;
                if (!coord(v, *angles[i]) || std::fabs(*angles[i]) > 100000.0f) {
                    return failResult(L, std::string(akeys[i]) + ": expected a finite number of degrees");
                }
                turned = true;
            }
            if (turned) {
                m = pose::compose(pos[0], pos[1], pos[2], a, pose::scale_of(cur));
            } else {
                m = cur;  // the rotation as it is (exactly, no round trip through angles)
                m[12] = pos[0];
                m[13] = pos[1];
                m[14] = pos[2];
            }
        }
        if (!pose::sane(m)) {
            return failResult(L, "the pose is not finite");
        }
        if (!guarded([&] {
                EntityHolder h{entity};
                sym().ent_move_to(&h, m.data());
            })) {
            return failResult(L, "the engine raised an exception moving the entity");
        }
        SPASS_TRACE(trace::Channel::Spawn, "set_pose {:.0f} {:.0f} {:.0f}", m[12], m[13], m[14]);
        return {sol::make_object(L, true), sol::make_object(L, sol::lua_nil)};
    };
    // Entity:bounds() -> minx, miny, minz, maxx, maxy, maxz: the world bounding box (the virtual `GetWorldBoundary` at vftable +0x260 that eCGeometryEntity::IsFreeLOS
    // reads); nil for an entity without one (a freepoint, a stale handle)
    ut["bounds"] = [](const LuaEntity& e) -> Bounds {
        const Bounds none{sol::nullopt, sol::nullopt, sol::nullopt, sol::nullopt, sol::nullopt, sol::nullopt};
        void* entity = ptr(e);
        if (entity == nullptr) {
            return none;
        }
        float b[6] = {};
        bool ok = false;
        guarded([&] {
            using BoxFn = const float* (*)(void*);
            void** vtable = *reinterpret_cast<void***>(entity);
            const float* box = reinterpret_cast<BoxFn>(vtable[0x260 / 8])(entity);
            if (box != nullptr) {
                std::copy(box, box + 6, b);
                ok = true;
            }
        });
        if (!ok) {
            return none;
        }
        for (int i = 0; i < 6; ++i) {
            if (!std::isfinite(b[i]) || std::fabs(b[i]) > 1.0e7f) {
                return none;
            }
        }
        if (b[0] > b[3] || b[1] > b[4] || b[2] > b[5]) {
            return none;  // an empty (invalid) box
        }
        return {b[0], b[1], b[2], b[3], b[4], b[5]};
    };
}

void bindGeometry(sol::state_view lua, sol::table risen) {
    sol::table world = risen["world"];
    world.set_function("trace", traceNative);
    world.set_function("engine_free_los", engineFreeLos);
    world.set_function("camera", cameraNative);
    (void)lua;
}

void installGeometryHelpers(sol::state_view lua, sol::table risen) {
    for (const auto& [name, chunk] : {std::pair<const char*, const char*>{"=risen.math", kMathChunk}, {"=risen.world.line_of_sight", kLineOfSightChunk}}) {
        sol::load_result loaded = lua.load(chunk, name);
        if (!loaded.valid()) {
            spdlog::error("lua: geometry api ({}) failed to load: {}", name, loaded.get<sol::error>().what());
            return;
        }
        sol::protected_function fn = loaded;
        sol::protected_function_result res = fn(risen);
        if (!res.valid()) {
            spdlog::error("lua: geometry api ({}) failed to run: {}", name, res.get<sol::error>().what());
            return;
        }
    }
}

void installRandomModApi(sol::state_view lua, sol::table api, const std::string& modId) {
    sol::load_result loaded = lua.load(kRandomChunk, "=risen.random");
    if (!loaded.valid()) {
        spdlog::error("lua: random api failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(api, modId);
    if (!res.valid()) {
        spdlog::error("lua: random api failed to run: {}", res.get<sol::error>().what());
    }
}

}  // namespace spass::game
