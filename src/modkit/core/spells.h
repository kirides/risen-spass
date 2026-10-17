// What the Spells and NPC groups panels (and their validation) know about the layer's sections `spells`, `new_spells` and `npc_groups`
// (docs/LUA_API.md "Spells" and "Balancing NPCs", docs/SPELL_PROPERTIES.md): the 21 shipped spells, their forms, and the template selection of a group.
#pragma once

#include <string>
#include <vector>

#include "core/gamedata.h"
#include "core/project.h"

namespace mk {

struct ShippedSpell {
    std::string name;                // "Fireball", "Light"
    bool projectile = false;         // Fireball, Frost, Missile: a crystal and projectile templates; every other spell is a rune (rune, scroll, orb)
    std::string skill;               // projectile spells: the skill that casts them
    std::string crystal;             // projectile spells: "It_Crystal_Fire"
    std::vector<std::string> projectiles;  // "It_Projectile_Fireball_S0", ...
    std::vector<std::string> forms;  // the forms a new spell of it can have: {"crystal","scroll"} or {"rune","scroll","orb"}
};
const std::vector<ShippedSpell>& shipped_spells();  // sorted by name
const ShippedSpell* shipped_spell(const std::string& name);
// the template of one form: crystal / rune "It_Ru_<name>", scroll "It_Scr_<name>", orb "It_Orb_<name>" ("" for a form the spell does not have)
std::string spell_form_template(const ShippedSpell& s, const std::string& form);
// the template names a NEW spell `name` creates, per form (crystal / rune <name>, scroll <name>_Scroll, orb <name>_Orb)
std::string new_spell_template(const std::string& name, const std::string& form);
const std::vector<std::string>& spell_damage_types();  // None, Blunt, Edge, Fire, Ice, Magic, Physics, Point

// ---- NPC groups ----
// A Lua pattern is checked / matched with the engine's own Lua dialect (LuaJIT, the same string.find the layer calls). Empty `error` = fine.
bool lua_pattern_matches(const std::string& pattern, const std::string& text, std::string& error);

// the NPC / monster templates a group selects (the layer's select_templates): `group` = a template group path (the templates below it), `match` /
// `exclude` = Lua patterns on the name, `names` = extra names; without group, match and exclude: every template below NPC and Monster (PC_Hero excepted).
// `error` names a bad pattern; the result is empty without game data.
std::vector<std::string> npc_group_templates(const NpcGroup& g, const GameData& game, std::string& error);

}  // namespace mk
