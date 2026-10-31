// The walkable ground as the editor can know it offline (docs/RE.md section 68). The game's navigation map file (`compiled_navigation_<World>.xnav`) only lists zones, obstacle
// circles and connections; the shapes of the zones are the world's own entities: every `Nav_Zone_Pos` / `Nav_Zone_Arena` entity has `gCNavZone_PS.Point` (a polygon in its local
// space, moved by the entity's matrix) with a vertical tolerance (`TopToleranz` above, `BottomToleranz` below). A point is "on walkable ground" when it lies inside one of the polygons
// (seen from above) and within the tolerance of its height. The engine also subtracts negative zones and collision circles: the answer here is "probably walkable".
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/gamedata.h"
#include "core/world.h"

namespace mk {

struct NavZone {
    std::string name;
    std::vector<std::array<double, 2>> poly;  // x, z in world coordinates
    double min_x = 0, max_x = 0, min_z = 0, max_z = 0;
    double y = 0;              // the mean height of the polygon (for drawing)
    double low = 0, high = 0;  // the heights of the polygon's points, widened by the tolerances: a point is in the zone when low <= y <= high
};

class NavMap {
public:
    std::vector<NavZone> zones;
    // true when (x, y, z) is inside a zone: its polygon and its height range
    bool on_ground(double x, double y, double z) const;
    // the horizontal distance to the nearest zone polygon that is within the height range of `y` (0 inside); a negative value when no zone is near (within `max_distance`)
    double distance_to_ground(double x, double y, double z, double max_distance = 3000.0) const;
};

// reads the zone shapes of the world's entities (about a second); a world without navigation entities gives an empty map
std::shared_ptr<NavMap> build_navmap(const GameData& g, const World& w);

// point in polygon (x, z), the even-odd rule; exposed for tests
bool point_in_polygon(const std::vector<std::array<double, 2>>& poly, double x, double z);

}  // namespace mk
