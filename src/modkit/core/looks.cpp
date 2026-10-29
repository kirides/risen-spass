#include "core/looks.h"

#include "core/gamedata.h"

namespace mk {

std::string look_material_name(const ObjectLook& look, int sub) { return look.name + "_" + std::to_string(sub) + "._xmat"; }

bool build_look(const GameData& game, const ObjectLook& look, std::map<std::string, std::string>& files, std::string& error) {
    if (look.name.empty() || look.mesh.empty()) {
        error = "the look needs a name and a mesh";
        return false;
    }
    if (!game.exists(look.mesh)) {
        error = "the game has no mesh " + look.mesh;
        return false;
    }
    MeshEdit e;
    e.mesh = look.mesh;
    e.scale = look.scale;
    std::map<std::string, std::string> out;
    for (auto& [i, m] : look.materials) {
        if (m.material.empty() || !game.exists(m.material)) {
            error = "sub-mesh " + std::to_string(i) + ": the game has no material " + m.material;
            return false;
        }
        try {
            Material mat = Material::parse(game.read(m.material));
            std::string why;
            if (!apply_material_edit(mat, m, why)) {
                error = "sub-mesh " + std::to_string(i) + ": " + why;
                return false;
            }
            out["assets/" + look_material_name(look, i)] = mat.write();
        } catch (const std::exception& ex) {
            error = "sub-mesh " + std::to_string(i) + ": " + ex.what();
            return false;
        }
        e.materials[i] = look_material_name(look, i);
    }
    p2m::Bytes mesh;
    if (!apply_mesh_edit(game.read(look.mesh), e, mesh, error)) return false;
    out["assets/" + look.name + "._xmsh"] = std::move(mesh);
    files = std::move(out);
    return true;
}

}  // namespace mk
