#include <cmath>
#include <string>
#include <tuple>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/bindings.h"
#include "game/lua_entity.h"

// risen.vegetation (docs/RE.md section 64, docs/LUA_API.md "Vegetation"): the grass, bushes and plants of the world are the grid of an eCVegetation_PS (one entity
// per `*_Vegetation` layer). `info` checks the entity, `remove` takes every entry within a radius out of the grid (eCVegetation_Grid::RemoveEntries), `add` puts
// an entry in (eCVegetation_Grid::InsertEntry: 44 bytes = mesh id, position, quaternion, horizontal and vertical scale, ARGB colour) and `update` refreshes the
// bounds (eCVegetation_PS::UpdateBounds). The grid is rebuilt from the layer on every world load, so a mod applies its changes again every time.

namespace spass::game {

namespace {

struct Entry {  // eSVegetationNodeEntry
    std::uint32_t id;
    float pos[3];
    float quat[4];
    float scaleH, scaleV;
    std::uint32_t color;
};
static_assert(sizeof(Entry) == 0x2c, "eSVegetationNodeEntry is 44 bytes");

using Fail = std::tuple<sol::object, sol::object>;

Fail fail(sol::this_state ts, const std::string& why) { return {sol::make_object(ts, sol::lua_nil), sol::make_object(ts, why)}; }
Fail ok(sol::this_state ts) { return {sol::make_object(ts, true), sol::make_object(ts, sol::lua_nil)}; }

// the entity's eCVegetation_PS and its grid, or a reason
bool vegetation(const Symbols& s, const LuaEntity& h, void*& ps, void*& grid, std::string& why) {
    ps = grid = nullptr;
    if (s.ent_get_property_set == nullptr || s.veg_get_grid == nullptr) {
        why = "the engine functions of the vegetation are missing";
        return false;
    }
    void* e = ptr(h);
    if (e == nullptr) {
        why = "the entity is gone";
        return false;
    }
    guarded([&] {
        const BString cls("eCVegetation_PS");
        ps = s.ent_get_property_set(e, cls.get());
    });
    if (ps == nullptr) {
        why = "the entity has no vegetation";
        return false;
    }
    guarded([&] { grid = s.veg_get_grid(ps); });
    if (grid == nullptr) {
        why = "the vegetation has no grid";
        return false;
    }
    return true;
}

bool finite(double v) { return std::isfinite(v) && std::fabs(v) < 1.0e7; }

}  // namespace

void bindVegetation(sol::state_view lua, sol::table risen) {
    sol::table veg = lua.create_table();
    risen["vegetation"] = veg;
    const Symbols& s = sym();

    // risen.vegetation.info(entity) -> true | nil, reason: whether the entity holds a vegetation grid
    veg.set_function("info", [&s](sol::this_state ts, const LuaEntity& h) -> Fail {
        void *ps, *grid;
        std::string why;
        if (!vegetation(s, h, ps, grid, why)) return fail(ts, why);
        return ok(ts);
    });

    // risen.vegetation.remove(entity, x, z, radius) -> true | nil, reason: every entry (every plant) within the radius of the point, all meshes
    veg.set_function("remove", [&s](sol::this_state ts, const LuaEntity& h, double x, double z, double radius) -> Fail {
        if (!finite(x) || !finite(z) || !finite(radius) || radius <= 0 || radius > 100000) return fail(ts, "x, z and a radius between 0 and 100000 are needed");
        void *ps, *grid;
        std::string why;
        if (!vegetation(s, h, ps, grid, why)) return fail(ts, why);
        if (s.veg_remove_region == nullptr) return fail(ts, "the engine function RemoveEntries is missing");
        // the engine looks at the cells of [pos - r, pos + r] with a truncating rule while the shipped data uses nearest cells: one cell more on every side
        const float xz[2] = {static_cast<float>(x), static_cast<float>(z)};
        if (!guarded([&] { s.veg_remove_region(grid, xz, static_cast<float>(radius), nullptr); })) return fail(ts, "the engine call failed");
        return ok(ts);
    });

    // risen.vegetation.add(entity, { mesh = <mesh id>, x, y, z, yaw = degrees, scale = 1, scale_v = scale, color = 0xAARRGGBB }) -> true | nil, reason
    veg.set_function("add", [&s](sol::this_state ts, const LuaEntity& h, sol::table t) -> Fail {
        auto num = [&](const char* k, double def, bool required) -> std::tuple<bool, double> {
            sol::object o = t[k];
            if (o.get_type() == sol::type::lua_nil) return {!required, def};
            if (o.get_type() != sol::type::number) return {false, def};
            const double v = o.as<double>();
            return {std::isfinite(v) && std::fabs(v) < 1.0e10, v};
        };
        auto [okx, x] = num("x", 0, true);
        auto [oky, y] = num("y", 0, true);
        auto [okz, z] = num("z", 0, true);
        auto [oki, id] = num("mesh", 0, true);
        auto [okw, yaw] = num("yaw", 0, false);
        auto [oks, sc] = num("scale", 1, false);
        auto [okv, scv] = num("scale_v", sc, false);
        auto [okc, color] = num("color", 4279584039.0, false);
        if (!(okx && oky && okz && oki && okw && oks && okv && okc)) return fail(ts, "x, y, z and mesh (the mesh id of the vegetation) are numbers; yaw, scale, scale_v, color are optional numbers");
        if (id < 0 || id > 4294967295.0 || sc <= 0 || scv <= 0 || sc > 100 || scv > 100 || color < 0 || color > 4294967295.0) return fail(ts, "mesh, scale or color is out of range");
        void *ps, *grid;
        std::string why;
        if (!vegetation(s, h, ps, grid, why)) return fail(ts, why);
        if (s.veg_insert == nullptr) return fail(ts, "the engine function InsertEntry is missing");
        const double half = yaw * 3.14159265358979323846 / 360.0;
        Entry e{};
        e.id = static_cast<std::uint32_t>(id);
        e.pos[0] = static_cast<float>(x);
        e.pos[1] = static_cast<float>(y);
        e.pos[2] = static_cast<float>(z);
        e.quat[1] = static_cast<float>(std::sin(half));  // a turn about the vertical axis
        e.quat[3] = static_cast<float>(std::cos(half));
        e.scaleH = static_cast<float>(sc);
        e.scaleV = static_cast<float>(scv);
        e.color = static_cast<std::uint32_t>(color);
        if (!guarded([&] { s.veg_insert(grid, &e); })) return fail(ts, "the engine call failed");
        return ok(ts);
    });

    // risen.vegetation.update(entity) -> true | nil, reason: refreshes the bounds after a batch of add / remove calls
    veg.set_function("update", [&s](sol::this_state ts, const LuaEntity& h) -> Fail {
        void *ps, *grid;
        std::string why;
        if (!vegetation(s, h, ps, grid, why)) return fail(ts, why);
        if (s.veg_update_bounds == nullptr) return fail(ts, "the engine function UpdateBounds is missing");
        if (!guarded([&] { s.veg_update_bounds(ps); })) return fail(ts, "the engine call failed");
        return ok(ts);
    });
}

}  // namespace spass::game
