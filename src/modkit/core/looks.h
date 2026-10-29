// Object looks (docs/editor/REQUIREMENTS.md WLD-11, docs/RE.md section 62): a named variant of an object template with its own mesh and materials, so ONE
// object looks different without touching the shared mesh / material of every other object. A look is exported as
//   assets/<name>._xmsh            the base template's mesh, scaled, its sub-meshes naming
//   assets/<name>_<i>._xmat        a clone of the material of sub-mesh i with the look's changes (image per slot, shader flags)
//   data/looks.lua                 { name, base, mesh } : the runtime makes the template with risen.template.create (mesh = <name>.xmsh)
// and spawns use the look's name as their template. A look can be made from a placed world object ("give it its own look"): the original is removed and a spawn of the
// look takes its place.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "core/material.h"
#include "core/mesh_edit.h"

namespace mk {

class GameData;

struct ObjectLook {
    std::string uid;
    std::string name;          // the template the mod makes ("MyMod_BigRock"): letters, digits, underscore
    std::string base;          // the shipped object template it is made from
    std::string mesh;          // the shipped mesh to change ("X._xmsh"): the base template's mesh unless another is chosen
    std::array<float, 3> scale{1, 1, 1};
    std::map<int, MaterialEdit> materials;  // sub-mesh index -> a clone of the sub-mesh's material with these changes (MaterialEdit::material = the source)
    std::string notes;
    bool generated = false;  // a water body's template: its mesh is made by core/water.cpp, not by a mesh edit (never saved)
};

// the files of a look: relative name ("assets/Name._xmsh") -> bytes. False with `error` when something does not fit (no such mesh, a sub-mesh out of range, a
// material change that does not apply ...)
bool build_look(const GameData& game, const ObjectLook& look, std::map<std::string, std::string>& files, std::string& error);

// the compiled name of the clone made for sub-mesh i
std::string look_material_name(const ObjectLook& look, int sub);

}  // namespace mk
