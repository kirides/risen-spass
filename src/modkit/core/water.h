// Water bodies painted in the World view (docs/editor/REQUIREMENTS.md WLD-14, docs/RE.md section 63): a polygon on the ground, a surface height and one of the
// game's water materials. In the game a water surface is an ordinary mesh entity with a `Special_Water_*` material; the export writes a flat mesh of the polygon
// (assets/<name>._xmsh, double sided, local to the polygon's centre), a template made from a shipped object (Obj_Nat_Waterfall_Large: a mesh and an illuminated
// set, no collision) with that mesh, and a spawn at the centre and height. Wading and swimming are a separate game object (gCWaterZone_PS) and are not part of
// the visual body.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace mk {

struct Project;

struct WaterBody {
    std::string uid;
    std::string name;      // the template the mod makes ("MyMod_Water_1")
    std::string material;  // "Special_Water_Island_01._xmat"
    double height = 0;     // the surface, world y
    std::vector<std::array<double, 2>> points;  // x, z of the outline, in order
    std::string world;     // "" = the project's world
    std::string notes;
};

// the game's water materials, for the picker
const std::vector<std::string>& water_materials();

std::array<double, 2> water_center(const WaterBody& w);
double water_area(const WaterBody& w);        // absolute, square game units
bool water_polygon_simple(const WaterBody& w);  // at least three points, no crossing edges, an area
// the mesh of the body: relative name ("assets/Name._xmsh") -> bytes. False with `error` when the polygon cannot be made into a mesh
bool build_water(const WaterBody& w, std::map<std::string, std::string>& files, std::string& error);

// the project with every water body expanded into what the exporter understands: a look entry (template, generated mesh) and a spawn
Project with_water(const Project& p);

}  // namespace mk
