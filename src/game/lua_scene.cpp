#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <windows.h>

#include <spdlog/spdlog.h>

#include "common/pose.h"
#include "game/abi.h"
#include "game/bindings.h"
#include "game/lua_entity.h"
#include "game/properties.h"
#include "game/symbols.h"

// Renderer testing, phase 2 (docs/RE.md section 59; the D3D9-vs-Vulkan reference comparison of risen-enhanced-graphics): the scene a capture shows.
//   risen.camera.*        gCCameraAdmin::SetFixCamera (eye overload, camera type cam_cinema) / StopFixCamera / IsCameraInDestinationPose
//   risen.world.weather*  eCWeatherAdmin mode 2 ("forced": every eCWeatherStates contributes with its blend factor) / mode 0 (the weather zones)
//   risen.debug.console   the console commands CON_<name> of Script_Game.dll, phase 2 (execute)

namespace spass::game {
namespace {

using Two = std::tuple<sol::object, sol::object>;

Two okv(sol::state_view L, sol::object v) { return {std::move(v), sol::make_object(L, sol::lua_nil)}; }
Two fail(sol::state_view L, const std::string& why) { return {sol::make_object(L, sol::lua_nil), sol::make_object(L, why)}; }

// An engine module by class name (eCModuleAdmin::FindModule). The result is only used when its vtable is the class's own (the exported `??_7<class>@@6B@`):
// the engine's own lookups dynamic_cast the module, a plain pointer is right only for a class without a second base in front, which the vtable check proves.
void* findModule(const char* cls, const unsigned char* vtable) {
    const Symbols& s = sym();
    if (s.module_admin_instance == nullptr || s.module_find_by_class == nullptr || vtable == nullptr) {
        return nullptr;
    }
    void* m = nullptr;
    guarded([&] {
        const BString name(cls);
        void* found = s.module_find_by_class(s.module_admin_instance(), name.get());
        if (found != nullptr && *static_cast<const void* const*>(found) == vtable) {
            m = found;
        }
    });
    return m;
}

bool finite3(const std::array<float, 3>& v) {
    for (float f : v)
        if (!std::isfinite(f) || std::fabs(f) > 1.0e7f) return false;
    return true;
}

// {x, y, z} or {x = .., y = .., z = ..}
bool readVec(const sol::object& o, std::array<float, 3>& out) {
    if (o.get_type() != sol::type::table) {
        return false;
    }
    sol::table t = o.as<sol::table>();
    const char* keys[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        sol::object v = t[i + 1];
        if (v.get_type() != sol::type::number) {
            v = t[keys[i]];
        }
        if (v.get_type() != sol::type::number) {
            return false;
        }
        out[i] = static_cast<float>(v.as<double>());
    }
    return finite3(out);
}

bool readNumber(const sol::table& t, const char* key, double& out, bool& present) {
    sol::object v = t[key];
    present = v.get_type() != sol::type::lua_nil;
    if (!present) {
        return true;
    }
    if (v.get_type() != sol::type::number) {
        return false;
    }
    out = v.as<double>();
    return std::isfinite(out);
}

// ------------------------------------------------------------------------------------------------------------------------------------------
// Camera

void* cameraAdmin() { return findModule("gCCameraAdmin", sym().cam_admin_vtable); }

struct CameraRequest {
    bool active = false;
    std::array<float, 3> eye{}, target{};
    float fov = 0;
};
CameraRequest g_camRequest;

// The speeds and accelerations handed to SetFixCamera: large, so that the cinema camera does not blend (with `instant` the camera admin also runs up to 50
// update steps in the first frame, gCCameraAdmin::Process +0x332).
constexpr float kCamFast = 1.0e5f;

Two cameraSet(sol::this_state ts, sol::object specObj) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    if (s.cam_set_fix_eye == nullptr) {
        return fail(L, "engine function gCCameraAdmin::SetFixCamera is unavailable");
    }
    if (specObj.get_type() != sol::type::table) {
        return fail(L, "expected a table { pos, target } or { target, yaw, pitch, distance } or { pos, yaw, pitch }");
    }
    sol::table spec = specObj.as<sol::table>();
    std::array<float, 3> eye{}, target{};
    const sol::object posObj = spec["pos"], targetObj = spec["target"];
    const bool havePos = posObj.get_type() != sol::type::lua_nil, haveTarget = targetObj.get_type() != sol::type::lua_nil;
    if (havePos && !readVec(posObj, eye)) {
        return fail(L, "pos must be { x, y, z } (finite numbers)");
    }
    if (haveTarget && !readVec(targetObj, target)) {
        return fail(L, "target must be { x, y, z } (finite numbers)");
    }
    double yaw = 0, pitch = 0, distance = 0, fovDeg = 0;
    bool haveYaw = false, havePitch = false, haveDistance = false, haveFov = false;
    if (!readNumber(spec, "yaw", yaw, haveYaw) || !readNumber(spec, "pitch", pitch, havePitch) || !readNumber(spec, "distance", distance, haveDistance) ||
        !readNumber(spec, "fov", fovDeg, haveFov)) {
        return fail(L, "yaw, pitch, distance and fov must be finite numbers");
    }
    const bool angles = haveYaw || havePitch;
    if (havePos && haveTarget && (angles || haveDistance)) {
        return fail(L, "give pos + target, or angles (yaw/pitch) with one of them, not both");
    }
    if (!havePos && !haveTarget) {
        return fail(L, "pos or target is required");
    }
    if (!(havePos && haveTarget) && !angles) {
        return fail(L, "with only pos or only target, yaw and/or pitch are required");
    }
    if (std::fabs(pitch) > 89.0) {
        return fail(L, "pitch must be within -89..89 degrees");
    }
    if (haveDistance && !(distance >= 1.0 && distance <= 100000.0)) {
        return fail(L, "distance must be within 1..100000");
    }
    if (haveFov && !(fovDeg >= 5.0 && fovDeg <= 170.0)) {
        return fail(L, "fov must be within 5..170 degrees");
    }
    if (angles) {
        // the view direction in the convention of Entity:pose (yaw 0 looks along +z, 90 along +x; pitch > 0 looks up)
        double r[3], u[3], f[3];
        pose::axes(pose::Angles{static_cast<float>(yaw), static_cast<float>(pitch), 0}, r, u, f);
        const double d = haveDistance ? distance : (havePos ? 100.0 : 300.0);
        for (int i = 0; i < 3; ++i) {
            if (havePos) {
                target[i] = static_cast<float>(eye[i] + f[i] * d);
            } else {
                eye[i] = static_cast<float>(target[i] - f[i] * d);
            }
        }
    }
    const double dx = double(target[0]) - eye[0], dy = double(target[1]) - eye[1], dz = double(target[2]) - eye[2];
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1.0) {
        return fail(L, "pos and target must be at least 1 unit apart");
    }
    if (std::fabs(dy) / len > std::sin(89.0 * 3.14159265358979 / 180.0)) {
        return fail(L, "the view must not be (nearly) vertical: |pitch| <= 89 degrees");
    }
    bool instant = true;
    if (sol::object v = spec["instant"]; v.get_type() != sol::type::lua_nil) {
        if (v.get_type() != sol::type::boolean) {
            return fail(L, "instant must be a boolean");
        }
        instant = v.as<bool>();
    }
    void* admin = cameraAdmin();
    if (admin == nullptr) {
        return fail(L, "no camera admin (gCCameraAdmin) yet");
    }
    float fov = 0;
    std::string why;
    if (!guarded([&] {
            fov = haveFov ? static_cast<float>(fovDeg) : (s.cam_default_fov != nullptr ? s.cam_default_fov(admin) : 60.0f);
            if (!std::isfinite(fov) || fov <= 0) {
                fov = 60.0f;
            }
            // the target pose: identity rotation, translation = target (row-major, row 3 = position); SetFixCamera flattens it to a yaw-only frame and measures
            // the eye's yaw against its z axis, so the orientation of this frame does not matter for the view
            pose::Matrix m{};
            m[0] = m[5] = m[10] = m[15] = 1.0f;
            m[12] = target[0];
            m[13] = target[1];
            m[14] = target[2];
            s.cam_set_fix_eye(admin, m.data(), eye.data(), instant, fov, kCamFast, kCamFast, kCamFast, kCamFast, kCamFast, kCamFast, kCamFast, false);
        })) {
        return fail(L, "the engine raised an exception");
    }
    g_camRequest = CameraRequest{true, eye, target, fov};
    spdlog::info("camera: fixed, eye ({:.1f}, {:.1f}, {:.1f}) target ({:.1f}, {:.1f}, {:.1f}) fov {:.2f}{}", eye[0], eye[1], eye[2], target[0], target[1], target[2], fov,
                 instant ? " instant" : "");
    return okv(L, sol::make_object(L, true));
}

Two cameraRelease(sol::this_state ts) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    if (s.cam_stop_fix == nullptr) {
        return fail(L, "engine function gCCameraAdmin::StopFixCamera is unavailable");
    }
    void* admin = cameraAdmin();
    if (admin == nullptr) {
        return fail(L, "no camera admin (gCCameraAdmin) yet");
    }
    bool was = false;
    if (!guarded([&] {
            was = s.cam_fix_active != nullptr && s.cam_fix_active(admin);
            s.cam_stop_fix(admin);
        })) {
        return fail(L, "the engine raised an exception");
    }
    g_camRequest.active = false;
    if (was) {
        spdlog::info("camera: released (back to cam_normal)");
    }
    return okv(L, sol::make_object(L, true));
}

sol::object cameraSettled(sol::this_state ts) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    void* admin = cameraAdmin();
    if (admin == nullptr || s.cam_in_dest_pose == nullptr) {
        return sol::make_object(L, sol::lua_nil);
    }
    bool settled = false;
    if (!guarded([&] { settled = s.cam_in_dest_pose(admin); })) {
        return sol::make_object(L, sol::lua_nil);
    }
    return sol::make_object(L, settled);
}

Two cameraGet(sol::this_state ts) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    if (s.app_instance == nullptr || s.app_current_camera == nullptr || s.cam_get_position == nullptr || s.cam_get_view == nullptr || s.cam_get_fov == nullptr) {
        return fail(L, "the engine's camera functions are not available");
    }
    float pos[3] = {}, view[16] = {}, fov = 0, aspect = 0;
    bool ok = false;
    if (!guarded([&] {
            void* app = s.app_instance();
            void* cam = app != nullptr ? s.app_current_camera(app) : nullptr;
            if (cam == nullptr) {
                return;
            }
            const float* p = s.cam_get_position(cam);
            const float* m = s.cam_get_view(cam);
            const float* f = s.cam_get_fov(cam);
            if (p == nullptr || m == nullptr || f == nullptr) {
                return;
            }
            std::memcpy(pos, p, sizeof pos);
            std::memcpy(view, m, sizeof view);
            fov = *f;
            if (s.cam_get_aspect != nullptr) {
                if (const float* a = s.cam_get_aspect(cam)) {
                    aspect = *a;
                }
            }
            ok = true;
        }) ||
        !ok) {
        return fail(L, "there is no camera (no world loaded?)");
    }
    for (float v : view)
        if (!std::isfinite(v)) return fail(L, "the camera is not set up yet");
    for (float v : pos)
        if (!std::isfinite(v)) return fail(L, "the camera is not set up yet");
    // the camera's axes are the COLUMNS of the view matrix (lua_geometry.cpp, risen.world.camera): forward = column 2
    const double d[3] = {view[2], view[6], view[10]};
    sol::table t = L.create_table();
    t["x"] = pos[0];
    t["y"] = pos[1];
    t["z"] = pos[2];
    t["dx"] = d[0];
    t["dy"] = d[1];
    t["dz"] = d[2];
    double yaw = std::atan2(d[0], d[2]) * 180.0 / 3.14159265358979;
    if (yaw < 0) yaw += 360.0;
    t["yaw"] = yaw;
    t["pitch"] = std::asin(std::clamp(d[1], -1.0, 1.0)) * 180.0 / 3.14159265358979;
    t["fov"] = fov * 180.0f / 3.14159265358979f;
    t["aspect"] = aspect;
    sol::table vt = L.create_table(16, 0);
    for (int i = 0; i < 16; ++i) vt[i + 1] = view[i];
    t["view"] = vt;
    if (void* admin = cameraAdmin()) {
        bool fixed = false, settled = false;
        std::string name;
        guarded([&] {
            if (s.cam_fix_active != nullptr) fixed = s.cam_fix_active(admin);
            if (s.cam_in_dest_pose != nullptr) settled = s.cam_in_dest_pose(admin);
            if (s.cam_current_name != nullptr) {
                if (const void* n = s.cam_current_name(admin)) name = s.bstring_get_text(n);
            }
        });
        t["fixed"] = fixed;
        t["settled"] = settled;
        t["type"] = name;
        // the entities of SetCameraTargets: the followed one (proxy +0x40) and the one the yaw is relative to (+0x50)
        if (s.eproxy_get_entity != nullptr) {
            void *target = nullptr, *orient = nullptr;
            guarded([&] {
                target = s.eproxy_get_entity(static_cast<char*>(admin) + 0x40);
                orient = s.eproxy_get_entity(static_cast<char*>(admin) + 0x50);
            });
            if (target != nullptr) t["target"] = makeEntityObject(L, target);
            if (orient != nullptr) t["orientation"] = makeEntityObject(L, orient);
        }
        if (fixed && g_camRequest.active) {
            sol::table r = L.create_table();
            r["pos"] = L.create_table_with(1, g_camRequest.eye[0], 2, g_camRequest.eye[1], 3, g_camRequest.eye[2]);
            r["target"] = L.create_table_with(1, g_camRequest.target[0], 2, g_camRequest.target[1], 3, g_camRequest.target[2]);
            r["fov"] = g_camRequest.fov;
            t["request"] = r;
        }
    }
    return okv(L, sol::make_object(L, t));
}

// ------------------------------------------------------------------------------------------------------------------------------------------
// Camera types (RE.md section 61): the gCCameraTypeBase objects of compiled_camera.bin (`cam_normal`, `cam_melee`, ...), property objects the camera
// admin and gCCharacterControl read every frame. GetCameraTypes returns an eTDocObjectMap: a chained hash map, map+0x10 Node** buckets, map+0x18 uint32
// bucket count; a node is { bCString key, gCCameraTypeBase* value, ?, Node* next } (key +0x00, value +0x08, next +0x18). Keys are lowercase (the engine's
// lookup lowercases the name first); the layer walks the map and compares case-insensitively instead of rehashing.

struct CamTypeNode {
    const char* key;  // bCString: a single pointer to the text
    void* value;
    void* unknown;
    CamTypeNode* next;
};

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// name -> type for every camera type; empty without a camera admin. `stopAt` ends the walk at the first match (lowercase).
std::vector<std::pair<std::string, void*>> cameraTypes(const std::string& stopAt = {}) {
    std::vector<std::pair<std::string, void*>> out;
    const Symbols& s = sym();
    void* admin = cameraAdmin();
    if (admin == nullptr || s.cam_get_types == nullptr) {
        return out;
    }
    guarded([&] {
        const auto* map = static_cast<const unsigned char*>(s.cam_get_types(admin));
        if (map == nullptr) return;
        auto* const* buckets = *reinterpret_cast<CamTypeNode* const* const*>(map + 0x10);
        const uint32_t count = *reinterpret_cast<const uint32_t*>(map + 0x18);
        if (buckets == nullptr || count == 0 || count > 1u << 20) return;
        for (uint32_t i = 0; i < count; ++i) {
            int guard = 0;
            for (const CamTypeNode* n = buckets[i]; n != nullptr && guard < 10000; n = n->next, ++guard) {
                if (n->value == nullptr) continue;
                std::string name = n->key != nullptr ? n->key : "";
                if (!stopAt.empty() && lowerAscii(name) == stopAt) {
                    out.clear();
                    out.emplace_back(std::move(name), n->value);
                    return;
                }
                if (stopAt.empty()) out.emplace_back(std::move(name), n->value);
            }
        }
        if (!stopAt.empty()) out.clear();
    });
    return out;
}

bool validCamTypeName(const std::string& n) {
    if (n.empty() || n.size() > 96) return false;
    for (unsigned char c : n)
        if (!std::isalnum(c) && c != '_' && c != '-') return false;
    return true;
}

void* cameraTypeByName(const std::string& name) {
    auto found = cameraTypes(lowerAscii(name));
    return found.empty() ? nullptr : found.front().second;
}

Two cameraTypeNames(sol::this_state ts) {
    sol::state_view L(ts);
    if (sym().cam_get_types == nullptr) return fail(L, "engine function gCCameraAdmin::GetCameraTypes is unavailable");
    if (cameraAdmin() == nullptr) return fail(L, "no camera admin (gCCameraAdmin) yet");
    auto all = cameraTypes();
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    sol::table t = L.create_table(static_cast<int>(all.size()), 0);
    int i = 1;
    for (const auto& [name, p] : all) t[i++] = name;
    return okv(L, sol::make_object(L, t));
}

Two cameraTypeGet(sol::this_state ts, const std::string& name) {
    sol::state_view L(ts);
    if (!validCamTypeName(name)) return fail(L, "bad camera type name");
    if (cameraAdmin() == nullptr) return fail(L, "no camera admin (gCCameraAdmin) yet");
    void* type = cameraTypeByName(name);
    if (type == nullptr) return fail(L, "no camera type '" + name + "'");
    sol::table t = L.create_table();
    for (const auto& [prop, ptype] : propertyList(type)) {
        if (auto v = propertyGet(type, prop)) t[prop] = *v;
    }
    return okv(L, sol::make_object(L, t));
}

// patch_type(name, { Prop = value, ... }): every name is checked before the first write, values are numbers, booleans or engine text.
Two cameraTypePatch(sol::this_state ts, const std::string& name, sol::table props) {
    sol::state_view L(ts);
    if (!validCamTypeName(name)) return fail(L, "bad camera type name");
    if (cameraAdmin() == nullptr) return fail(L, "no camera admin (gCCameraAdmin) yet");
    void* type = cameraTypeByName(name);
    if (type == nullptr) return fail(L, "no camera type '" + name + "'");
    std::vector<std::pair<std::string, std::string>> writes;
    for (const auto& [k, v] : props) {
        if (k.get_type() != sol::type::string) return fail(L, "property names must be strings");
        const std::string prop = k.as<std::string>();
        if (!propertyGet(type, prop)) return fail(L, "no such property: " + prop.substr(0, 60));
        std::string text;
        switch (v.get_type()) {
            case sol::type::boolean: text = v.as<bool>() ? "true" : "false"; break;
            case sol::type::number: {
                const double d = v.as<double>();
                if (!std::isfinite(d)) return fail(L, prop + ": not a finite number");
                text = (d == std::floor(d) && std::fabs(d) < 1e9) ? std::to_string(static_cast<long long>(d)) : std::to_string(d);
                break;
            }
            case sol::type::string: text = v.as<std::string>(); break;
            default: return fail(L, prop + ": value must be a number, boolean or string");
        }
        if (text.size() > 256) return fail(L, prop + ": value too long");
        writes.emplace_back(prop, std::move(text));
    }
    for (const auto& [prop, text] : writes) {
        if (!propertySet(type, prop, text)) return fail(L, "the engine refused " + prop);
    }
    spdlog::info("camera: patched type {} ({} properties)", name, writes.size());
    return okv(L, sol::make_object(L, true));
}

// set_targets(target, orientation|nil [, { head, offset, force }]): gCCameraAdmin::SetCameraTargets. The camera follows `target` (proxy admin+0x40) and its yaw is
// relative to `orientation`'s rotation (proxy +0x50; none = world axes). The engine recomputes yaw/pitch/distance from the current camera position, so the view
// does not jump. A dialogue camera ignores the call; a fixed camera needs force. The game sets (hero, hero) again when the controlled character changes.
Two cameraSetTargets(sol::this_state ts, sol::object targetObj, sol::object orientObj, sol::optional<sol::table> opts) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    if (s.cam_set_targets == nullptr) return fail(L, "engine function gCCameraAdmin::SetCameraTargets is unavailable");
    if (!targetObj.is<LuaEntity>()) return fail(L, "target must be an entity");
    if (orientObj.get_type() != sol::type::lua_nil && !orientObj.is<LuaEntity>()) return fail(L, "orientation must be an entity or nil");
    bool head = true, force = false;
    std::array<float, 3> offset{0, 0, 0};
    if (opts) {
        sol::table o = *opts;
        for (const auto& [k, v] : o) {
            const std::string key = k.get_type() == sol::type::string ? k.as<std::string>() : std::string();
            if (key == "head" && v.get_type() == sol::type::boolean) head = v.as<bool>();
            else if (key == "force" && v.get_type() == sol::type::boolean) force = v.as<bool>();
            else if (key == "offset" && readVec(v, offset)) {}
            else return fail(L, "bad option '" + key.substr(0, 40) + "' (head: boolean, force: boolean, offset: {x, y, z})");
        }
    }
    void* target = ptr(targetObj.as<LuaEntity>());
    if (target == nullptr) return fail(L, "stale target entity");
    void* orient = nullptr;
    if (orientObj.is<LuaEntity>()) {
        orient = ptr(orientObj.as<LuaEntity>());
        if (orient == nullptr) return fail(L, "stale orientation entity");
    }
    void* admin = cameraAdmin();
    if (admin == nullptr) return fail(L, "no camera admin (gCCameraAdmin) yet");
    // speed -1 like gCSession::OnControlEntity
    if (!guarded([&] { s.cam_set_targets(admin, target, orient, head, offset.data(), force, nullptr, -1.0f); })) {
        return fail(L, "the engine raised an exception");
    }
    return okv(L, sol::make_object(L, true));
}

// use_type(name [, instant]): SetCurrentCameraByName(name, instant, keepFixCamera = true) - a running dialogue / fixed camera is not ended.
Two cameraTypeUse(sol::this_state ts, const std::string& name, sol::optional<bool> instant) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    if (s.cam_set_current == nullptr) return fail(L, "engine function gCCameraAdmin::SetCurrentCameraByName is unavailable");
    if (!validCamTypeName(name)) return fail(L, "bad camera type name");
    void* admin = cameraAdmin();
    if (admin == nullptr) return fail(L, "no camera admin (gCCameraAdmin) yet");
    if (cameraTypeByName(name) == nullptr) return fail(L, "no camera type '" + name + "'");
    bool ok = false;
    if (!guarded([&] {
            const BString n(lowerAscii(name));
            ok = s.cam_set_current(admin, n.get(), instant.value_or(false), true);
        })) {
        return fail(L, "the engine raised an exception");
    }
    if (!ok) return fail(L, "the engine did not switch to '" + name + "'");
    return okv(L, sol::make_object(L, true));
}

// ------------------------------------------------------------------------------------------------------------------------------------------
// Weather

void* weatherAdmin() { return findModule("eCWeatherAdmin", sym().weather_admin_vtable); }

bool weatherBound() {
    const Symbols& s = sym();
    return s.weather_admin_vtable != nullptr && s.weather_states_count != nullptr && s.weather_states_at != nullptr && s.weather_get_mode != nullptr &&
           s.weather_set_mode != nullptr && s.wstate_get_name != nullptr && s.wstate_get_blend != nullptr && s.wstate_set_blend != nullptr;
}

struct WeatherState {
    void* ptr = nullptr;
    std::string name;
    float weight = 0;
};

// every eCWeatherStates in index order; false when the engine raised
bool listStates(void* admin, std::vector<WeatherState>& out) {
    const Symbols& s = sym();
    return guarded([&] {
        const int n = s.weather_states_count(admin);
        for (int i = 0; i < n && i < 4096; ++i) {
            void* st = s.weather_states_at(admin, i);
            if (st == nullptr) continue;
            const void* nm = s.wstate_get_name(st);
            out.push_back(WeatherState{st, nm != nullptr ? s.bstring_get_text(nm) : std::string(), s.wstate_get_blend(st)});
        }
    });
}

// thunder suppression: the original ThunderProbability / EffectProbability of each state, while suppressed
std::map<void*, std::pair<float, float>> g_thunderSaved;

bool eqCi(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

const WeatherState* findState(const std::vector<WeatherState>& states, const std::string& name) {
    for (const auto& st : states)
        if (st.name == name) return &st;
    for (const auto& st : states)
        if (eqCi(st.name, name)) return &st;
    return nullptr;
}

// false = the engine raised
bool setThunder(const std::vector<WeatherState>& states, bool suppress) {
    const Symbols& s = sym();
    if (s.wstate_get_thunder == nullptr || s.wstate_set_thunder == nullptr || s.wstate_get_effect_prob == nullptr || s.wstate_set_effect_prob == nullptr) {
        return true;
    }
    return guarded([&] {
        if (suppress) {
            const float zero = 0;
            for (const auto& st : states) {
                if (!g_thunderSaved.contains(st.ptr)) {
                    g_thunderSaved[st.ptr] = {*s.wstate_get_thunder(st.ptr), *s.wstate_get_effect_prob(st.ptr)};
                }
                s.wstate_set_thunder(st.ptr, &zero);
                s.wstate_set_effect_prob(st.ptr, &zero);
            }
        } else {
            for (const auto& st : states) {
                auto it = g_thunderSaved.find(st.ptr);
                if (it == g_thunderSaved.end()) continue;
                s.wstate_set_thunder(st.ptr, &it->second.first);
                s.wstate_set_effect_prob(st.ptr, &it->second.second);
            }
            g_thunderSaved.clear();
        }
    });
}

const char* modeName(int mode) {
    switch (mode) {
        case 0: return "zones";
        case 1: return "camera";
        case 2: return "forced";
        case 3: return "reset";
        default: return "unknown";
    }
}

Two weatherStates(sol::this_state ts) {
    sol::state_view L(ts);
    if (!weatherBound()) {
        return fail(L, "the engine's weather functions are not available");
    }
    void* admin = weatherAdmin();
    if (admin == nullptr) {
        return fail(L, "no weather admin (eCWeatherAdmin) yet");
    }
    std::vector<WeatherState> states;
    if (!listStates(admin, states)) {
        return fail(L, "the engine raised an exception");
    }
    sol::table t = L.create_table(static_cast<int>(states.size()), 0);
    int i = 1;
    for (const auto& st : states) t[i++] = st.name;
    return okv(L, sol::make_object(L, t));
}

Two weatherGet(sol::this_state ts) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    if (!weatherBound()) {
        return fail(L, "the engine's weather functions are not available");
    }
    void* admin = weatherAdmin();
    if (admin == nullptr) {
        return fail(L, "no weather admin (eCWeatherAdmin) yet");
    }
    std::vector<WeatherState> states;
    int mode = -1;
    if (!listStates(admin, states) || !guarded([&] { mode = s.weather_get_mode(admin); })) {
        return fail(L, "the engine raised an exception");
    }
    sol::table t = L.create_table();
    t["mode"] = mode;
    t["mode_name"] = std::string(modeName(mode));
    sol::table w = L.create_table();
    std::string dominant;
    float best = 0;
    for (const auto& st : states) {
        if (st.weight > 0) {
            w[st.name] = st.weight;
            if (st.weight > best) {
                best = st.weight;
                dominant = st.name;
            }
        }
    }
    t["weights"] = w;
    if (!dominant.empty()) t["dominant"] = dominant;
    t["thunder"] = g_thunderSaved.empty();
    return okv(L, sol::make_object(L, t));
}

Two weatherSet(sol::this_state ts, sol::object spec, sol::object optsObj) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    if (!weatherBound()) {
        return fail(L, "the engine's weather functions are not available");
    }
    sol::optional<bool> thunder;
    if (optsObj.get_type() != sol::type::lua_nil) {
        if (optsObj.get_type() != sol::type::table) {
            return fail(L, "opts must be a table { thunder = boolean }");
        }
        sol::object th = optsObj.as<sol::table>()["thunder"];
        if (th.get_type() == sol::type::boolean) {
            thunder = th.as<bool>();
        } else if (th.get_type() != sol::type::lua_nil) {
            return fail(L, "opts.thunder must be a boolean");
        }
    }
    void* admin = weatherAdmin();
    if (admin == nullptr) {
        return fail(L, "no weather admin (eCWeatherAdmin) yet");
    }
    std::vector<WeatherState> states;
    if (!listStates(admin, states)) {
        return fail(L, "the engine raised an exception");
    }
    if (states.empty()) {
        return fail(L, "the engine has no weather states");
    }
    // "auto": back to the weather zones (mode 0); thunder comes back unless opts say otherwise
    if (spec.get_type() == sol::type::string && eqCi(spec.as<std::string>(), "auto")) {
        if (!guarded([&] { s.weather_set_mode(admin, 0); }) || !setThunder(states, thunder.has_value() && !*thunder)) {
            return fail(L, "the engine raised an exception");
        }
        spdlog::info("weather: back to the weather zones (mode 0)");
        return okv(L, sol::make_object(L, true));
    }
    std::vector<std::pair<void*, float>> weights;
    std::string desc;
    if (spec.get_type() == sol::type::string) {
        const std::string name = spec.as<std::string>();
        const WeatherState* st = name.size() <= 128 ? findState(states, name) : nullptr;
        if (st == nullptr) {
            return fail(L, "unknown weather state '" + name.substr(0, 64) + "' (risen.world.weather_states() lists them)");
        }
        weights.emplace_back(st->ptr, 1.0f);
        desc = st->name;
    } else if (spec.get_type() == sol::type::table) {
        double sum = 0;
        int count = 0;
        for (const auto& [k, v] : spec.as<sol::table>()) {
            if (++count > 256) {
                return fail(L, "too many weights");
            }
            if (k.get_type() != sol::type::string || v.get_type() != sol::type::number) {
                return fail(L, "weights must be { [state name] = number }");
            }
            const std::string name = k.as<std::string>();
            const double w = v.as<double>();
            if (!std::isfinite(w) || w < 0 || w > 1000) {
                return fail(L, "the weight of '" + name.substr(0, 64) + "' must be within 0..1000");
            }
            const WeatherState* st = name.size() <= 128 ? findState(states, name) : nullptr;
            if (st == nullptr) {
                return fail(L, "unknown weather state '" + name.substr(0, 64) + "' (risen.world.weather_states() lists them)");
            }
            if (w > 0) {
                weights.emplace_back(st->ptr, static_cast<float>(w));
                desc += (desc.empty() ? "" : ", ") + st->name + "=" + std::to_string(w);
                sum += w;
            }
        }
        if (sum <= 0) {
            return fail(L, "at least one weight must be > 0 (use \"auto\" for the weather zones)");
        }
    } else {
        return fail(L, "expected a weather state name, a table { [state name] = weight } or \"auto\"");
    }
    // mode 2: eCWeatherAdmin::Process sums the states with their blend factors (normalised) every frame: no blending over time, no zones, no random transitions
    if (!guarded([&] {
            for (const auto& st : states) s.wstate_set_blend(st.ptr, 0.0f);
            for (const auto& [p, w] : weights) s.wstate_set_blend(p, w);
            s.weather_set_mode(admin, 2);
        })) {
        return fail(L, "the engine raised an exception");
    }
    if (thunder.has_value() && !setThunder(states, !*thunder)) {
        return fail(L, "the engine raised an exception");
    }
    spdlog::info("weather: forced (mode 2) {}{}", desc, g_thunderSaved.empty() ? "" : ", no thunder");
    return okv(L, sol::make_object(L, true));
}

// ------------------------------------------------------------------------------------------------------------------------------------------
// Console commands: `__int64 CON_<name>(gCScriptProcessingUnit*, void* self, void* other, __int64 args)` exported by Script_Game.dll

using ConsoleFn = std::int64_t (*)(void* unit, void* self, void* other, std::int64_t args);

struct ConsoleArgs {           // the 0x20-byte argument block (docs/RE.md section 59)
    std::int32_t phase = 2;    // 0 help text for argument `argc`, 1 tab completion, 2 execute
    std::int32_t pad0 = 0;
    void* argv = nullptr;      // bCUnicodeString[argc]
    std::int32_t argc = 0;
    std::int32_t pad1 = 0;
    unsigned char message[8];  // bCUnicodeString written by the command
};
static_assert(sizeof(ConsoleArgs) == 0x20);
static_assert(sizeof(UString) == 8, "bCUnicodeString is a single pointer");

constexpr const char* kConPrefix = "?CON_";
constexpr const char* kConSuffix = "@@YA_JPEAVgCScriptProcessingUnit@@PEAX1_J@Z";

// name -> function, from the export table of Script_Game.dll (read once it is loaded)
std::vector<std::pair<std::string, ConsoleFn>> g_console;
bool g_consoleRead = false;

const std::vector<std::pair<std::string, ConsoleFn>>& consoleCommands() {
    if (g_consoleRead) {
        return g_console;
    }
    HMODULE mod = GetModuleHandleW(L"Script_Game.dll");
    if (mod == nullptr) {
        return g_console;
    }
    g_consoleRead = true;
    guarded([&] {
        const auto* base = reinterpret_cast<const unsigned char*>(mod);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (dir.VirtualAddress == 0) return;
        const auto* ex = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
        const auto* names = reinterpret_cast<const DWORD*>(base + ex->AddressOfNames);
        const size_t prefix = std::strlen(kConPrefix), suffix = std::strlen(kConSuffix);
        for (DWORD i = 0; i < ex->NumberOfNames; ++i) {
            const char* n = reinterpret_cast<const char*>(base + names[i]);
            const size_t len = std::strlen(n);
            if (len <= prefix + suffix || std::strncmp(n, kConPrefix, prefix) != 0 || std::strcmp(n + len - suffix, kConSuffix) != 0) continue;
            if (auto* fn = reinterpret_cast<ConsoleFn>(GetProcAddress(mod, n))) {
                g_console.emplace_back(std::string(n + prefix, len - prefix - suffix), fn);
            }
        }
    });
    std::sort(g_console.begin(), g_console.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    spdlog::info("console: {} commands in Script_Game.dll", g_console.size());
    return g_console;
}

Two consoleRun(sol::this_state ts, const std::string& cmd, sol::variadic_args va) {
    sol::state_view L(ts);
    const Symbols& s = sym();
    if (cmd.empty() || cmd.size() > 64) {
        return fail(L, "the command name must be 1..64 characters");
    }
    ConsoleFn fn = nullptr;
    std::string name;
    for (const auto& [n, f] : consoleCommands()) {
        if (eqCi(n, cmd)) {
            fn = f;
            name = n;
            if (n == cmd) break;
        }
    }
    if (fn == nullptr) {
        return fail(L, "unknown console command '" + cmd + "' (risen.debug.console_commands() lists them)");
    }
    std::vector<std::wstring> args;
    // not `va.size()` / a range loop: after fixed parameters sol's variadic_args can have a negative size (lua_balance.cpp `collect`)
    for (int idx = va.stack_index(), i = 0; idx <= va.top(); ++idx, ++i) {
        const sol::object a(va.lua_state(), idx);
        std::string v;
        if (a.get_type() == sol::type::string) {
            v = a.as<std::string>();
        } else if (a.get_type() == sol::type::number) {
            const double d = a.as<double>();
            if (!std::isfinite(d)) return fail(L, "argument " + std::to_string(i + 1) + " is not a finite number");
            v = d == std::floor(d) && std::fabs(d) < 1e15 ? std::to_string(static_cast<long long>(d)) : std::to_string(d);
        } else {
            return fail(L, "argument " + std::to_string(i + 1) + " must be a string or a number");
        }
        if (v.size() > 256 || v.find('\0') != std::string::npos) {
            return fail(L, "argument " + std::to_string(i + 1) + " is too long or contains NUL");
        }
        args.push_back(UString::widen(v));
        if (args.size() > 16) return fail(L, "at most 16 arguments");
    }
    // self/other: Entity handles (a pointer to an Entity, i.e. to the eCEntity*); the player when there is one, else a null Entity (never a null pointer:
    // the commands would ask the processing unit, which is null here)
    EntityHolder self{}, other{};
    if (s.ent_get_player != nullptr) {
        guarded([&] {
            EntityHolder ret;
            s.ent_get_player(&ret);
            self.entity = other.entity = ret.entity;
        });
    }
    std::int64_t result = 0;
    std::string message;
    const size_t slots = std::max<size_t>(args.size(), 1);  // at least one (empty) string: a command that reads argv[0] without checking argc stays safe
    if (!guarded([&] {
            std::unique_ptr<UString[]> argv(new UString[slots]);
            for (size_t i = 0; i < args.size(); ++i) s.ustring_assign_wide(argv[i].raw(), args[i].c_str());
            ConsoleArgs ca;
            s.ustring_ctor_wide(ca.message, L"");
            ca.phase = 2;
            ca.argv = argv.get();
            ca.argc = static_cast<std::int32_t>(args.size());
            result = fn(nullptr, &self, &other, reinterpret_cast<std::int64_t>(&ca));
            if (const wchar_t* w = s.ustring_get_text(ca.message)) message = UString::narrow(w);
            s.ustring_dtor(ca.message);
        })) {
        return fail(L, "the engine raised an exception in CON_" + name);
    }
    spdlog::info("console: {}{}{} -> {}{}{}", name, args.empty() ? "" : " ", [&] {
        std::string j;
        for (const auto& a : args) j += (j.empty() ? "" : " ") + UString::narrow(a.c_str());
        return j;
    }(), result, message.empty() ? "" : ": ", message);
    return {sol::make_object(L, result != 0), sol::make_object(L, message)};
}

}  // namespace

void bindScene(sol::state_view lua, sol::table risen) {
    sol::table camera = lua.create_table();
    camera.set_function("set", cameraSet);
    camera.set_function("release", cameraRelease);
    camera.set_function("settled", cameraSettled);
    camera.set_function("get", cameraGet);
    camera.set_function("types", cameraTypeNames);
    camera.set_function("type", cameraTypeGet);
    camera.set_function("patch_type", cameraTypePatch);
    camera.set_function("use_type", cameraTypeUse);
    camera.set_function("set_targets", cameraSetTargets);
    risen["camera"] = camera;

    sol::table world = risen["world"];
    world.set_function("weather_states", weatherStates);
    world.set_function("weather", weatherGet);
    world.set_function("set_weather", weatherSet);

    sol::table dbg = risen["debug"];
    dbg.set_function("console", consoleRun);
    dbg.set_function("console_commands", [](sol::this_state ts) -> sol::table {
        sol::state_view L(ts);
        const auto& cmds = consoleCommands();
        sol::table t = L.create_table(static_cast<int>(cmds.size()), 0);
        int i = 1;
        for (const auto& c : cmds) t[i++] = c.first;
        return t;
    });
}

}  // namespace spass::game
