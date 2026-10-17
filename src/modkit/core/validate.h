// Validation of a project (the Problems panel; the export refuses while errors exist). With the game data the checks also cover template names,
// bases, property sets, meshes and icons; without it only what the project itself says.
#pragma once

#include <string>
#include <vector>

#include "core/gamedata.h"
#include "core/project.h"

namespace mk {

enum class Severity { Error, Warning };

struct Problem {
    Severity severity = Severity::Error;
    std::string area;     // "Mod", "Items", "Recipes", ... (also the panel title)
    std::string uid;      // the object (empty for project-wide problems)
    std::string subject;  // the object's name as shown to the user
    std::string message;
};

std::vector<Problem> validate(const Project& p, const GameData* game);
size_t error_count(const std::vector<Problem>& v);

// the rules of the layer (shared with the panels' inline hints)
std::string mod_id_problem(const std::string& id);
std::string template_name_problem(const std::string& name);  // risen.template.name_problem

}  // namespace mk
