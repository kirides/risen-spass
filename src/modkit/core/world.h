// A game world read offline (docs/editor/REQUIREMENTS.md WLD-1): every entity of every layer file (*.lrent, "GENOMEDL") below
// common/projects/<World>/ with its unique name, GUID, creator template, world matrix and bounding box. The properties are not kept (the World has
// ~600 layers); entity_properties() parses one entity's set again on demand.
//
// Entity record (RE.md section 47, extended here): `d6 00 <GUID16> 00 00 00 00 <u16 name string index> <16 floats world matrix, row-major,
// translation in row 3> <6 floats world box min, max> <4 floats bounding sphere radius, centre> <6 floats local box> 01 00 01 <GUID16 creator template>
// 00 00 00 00 d6 00 <16 floats local matrix> ...`; the layer's property stream belongs to the entity whose byte range contains it.
#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/gamedata.h"
#include "core/sectors.h"

namespace mk {

struct WorldEntity {
    std::string name;          // the entity name (a placed NPC's unique name, "Obj_..." for objects)
    std::string guid;          // risen.entity.by_id form: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (Windows byte order)
    std::string template_name; // the creator template ("" when unknown)
    std::string mesh;          // eCMesh_PS.MeshFileName of the entity, else of its template ("" = none)
    std::array<float, 16> matrix{};    // world matrix, row-major (D3D row vectors: position = row 3)
    std::array<float, 3> box_min{}, box_max{};  // world box (min > max: none)
    uint32_t layer = 0;        // index into World::layers
    uint32_t start = 0, end = 0;  // byte range of the record in the layer file
    TplKind kind = TplKind::Other;  // the template's kind, or derived from the entity's own property sets
    bool npc = false, inventory = false;  // has gCNPC_PS / gCInventory_PS

    float x() const { return matrix[12]; }
    float y() const { return matrix[13]; }
    float z() const { return matrix[14]; }
    bool has_box() const { return box_min[0] <= box_max[0] && box_min[0] > -1e9f && box_max[0] < 1e9f; }
    // yaw in degrees (0 = looking along +z, 90 = along +x), from the matrix's forward row
    float yaw() const;
};

struct WorldLayer {
    std::string path;   // "projects/World/Cities/Harbour_Town/Harbour_Town_NPC_01.lrent"
    std::string group;  // "Cities/Harbour_Town"
    std::string name;   // "Harbour_Town_NPC_01"
    uint32_t first = 0, count = 0;  // entities [first, first + count)
    int sector = -1;                // index into World::sectors of the sector file with the same name (-1: none)
};

struct World {
    std::string name;
    std::vector<WorldLayer> layers;
    std::vector<WorldEntity> entities;
    std::vector<SectorInfo> sectors;  // the world's sector files: which start switched off and what switches them on (docs/RE.md section 55)
    double load_seconds = 0;
    // the sector of an entity's layer, nullptr when the layer has none
    const SectorInfo* sector_of(const WorldEntity& e) const { return e.layer < layers.size() && layers[e.layer].sector >= 0 ? &sectors[static_cast<size_t>(layers[e.layer].sector)] : nullptr; }
    const WorldEntity* by_guid(const std::string& guid) const;
    const WorldEntity* by_name(const std::string& name) const;  // the first entity with this name
};

using WorldProgress = std::function<void(float, const std::string&)>;
// throws std::runtime_error for an unknown world; a damaged layer is logged and skipped
std::shared_ptr<World> load_world(const GameData& g, const std::string& world, const WorldProgress& progress = {}, const std::atomic<bool>* cancel = nullptr);

// the property records of one entity (parsed from its layer file again), with GUID references resolved to template names
std::vector<p2m::Prop> entity_properties(const GameData& g, const World& w, const WorldEntity& e);
// the items of an entity's inventory (containers, placed NPCs): template name and amount
std::vector<std::pair<std::string, int>> entity_inventory(const GameData& g, const World& w, const WorldEntity& e);

std::string guid_text(const char* bytes16);  // 16 raw bytes -> the Windows text form

}  // namespace mk
