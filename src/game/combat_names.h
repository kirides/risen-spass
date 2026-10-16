#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Pure logic of the damage hooks (lua_combat.cpp, docs/LUA_API.md "Combat", docs/RE.md section 39); kept free of the engine so tests/combat_tests.inc can run it.

namespace spass::game::combat {

// What a hit is, derived from the `DamageScript` of the damaging item (gCDamage_PS.DamageScript of the weapon, the projectile, the monster's attack):
//   kind  "spell" (DoMagic*Damage, DoInfernoDamage), "melee" (DoSwordDamage, DoAxeDamage, DoStaffDamage, DoFistDamage, DoMeleeDamage, DoSpecialMonsterDamage),
//         "ranged" (DoProjectileDamage, DoArrowDamage, DoBoltDamage), "other" (fire, falling objects, touch, titan, everything unknown or missing).
//   spell "fireball" / "frost" / "missile" / "inferno" for a spell, else empty.
struct Source {
    std::string kind = "other";
    std::string spell;
};

inline Source classifyDamageScript(const std::string& script) {
    Source s;
    if (script == "DoMagicFireballDamage") {
        s.kind = "spell";
        s.spell = "fireball";
    } else if (script == "DoMagicFrostDamage") {
        s.kind = "spell";
        s.spell = "frost";
    } else if (script == "DoMagicMissileDamage") {
        s.kind = "spell";
        s.spell = "missile";
    } else if (script == "DoInfernoDamage") {
        s.kind = "spell";
        s.spell = "inferno";
    } else if (script == "DoSwordDamage" || script == "DoAxeDamage" || script == "DoStaffDamage" || script == "DoFistDamage" || script == "DoMeleeDamage" ||
               script == "DoSpecialMonsterDamage") {
        s.kind = "melee";
    } else if (script == "DoProjectileDamage" || script == "DoArrowDamage" || script == "DoBoltDamage") {
        s.kind = "ranged";
    }
    return s;
}

// "gEDamageType_Magic" -> "Magic" (anything without the prefix is returned as is).
inline std::string damageTypeName(const std::string& text) {
    static const std::string prefix = "gEDamageType_";
    return text.rfind(prefix, 0) == 0 ? text.substr(prefix.size()) : text;
}

// The gESkill that protects against a damage type (RE.md section 23: Prot_Edge .. Prot_Magic are 10..15), -1 for a type that has no protection skill.
inline int protectionSkill(const std::string& type) {
    static const char* const kNames[] = {"Edge", "Blunt", "Point", "Fire", "Ice", "Magic"};
    for (int i = 0; i < 6; ++i) {
        if (type == kNames[i]) {
            return 10 + i;
        }
    }
    return -1;
}

// Largest damage a handler can ask for: DoDamage and the scripts around it compute with 32-bit ints ((damage + 600) * 5 for the stumble).
constexpr int kMaxDamage = 1000000;

// What a damage handler returned.
//   Keep     nil (or true): the damage stays as it is
//   Replace  a number >= 0.5: this (rounded) damage goes into the engine's DoDamage, which still subtracts the victim's protection
//   Cancel   false, or a number that rounds to 0: the hit does nothing (DoDamage is not called)
//   Invalid  anything else (negative or non-finite number, string, table, ...): reported, the damage stays as it is
enum class Verdict { Keep, Replace, Cancel, Invalid };

struct Decision {
    Verdict verdict = Verdict::Keep;
    int value = 0;           // the damage for Replace
    std::string why;         // for Invalid
};

inline Decision interpretResult(bool isNil, bool isBool, bool boolValue, bool isNumber, double number) {
    Decision d;
    if (isNil) {
        return d;
    }
    if (isBool) {
        if (!boolValue) {
            d.verdict = Verdict::Cancel;
        }
        return d;   // true: unchanged
    }
    if (isNumber) {
        if (!std::isfinite(number)) {
            d.verdict = Verdict::Invalid;
            d.why = "the damage is not a finite number";
            return d;
        }
        if (number < 0.0) {
            d.verdict = Verdict::Invalid;
            d.why = "the damage is negative (return false or 0 to cancel the hit)";
            return d;
        }
        const double rounded = std::floor(std::min(number, static_cast<double>(kMaxDamage)) + 0.5);
        if (rounded < 1.0) {
            d.verdict = Verdict::Cancel;
            return d;
        }
        d.verdict = Verdict::Replace;
        d.value = static_cast<int>(rounded);
        return d;
    }
    d.verdict = Verdict::Invalid;
    d.why = "a damage handler returns nil (unchanged), a number (the damage) or false (cancel the hit)";
    return d;
}

// One hit as the hook sees it, with the names the filter looks at.
struct Hit {
    std::string kind = "other";
    std::string spell;
    std::string type;               // "Magic", "Edge", ... (empty when unknown)
    std::string weapon;             // template name of the damaging item (weapon, projectile)
    std::string script;             // its DamageScript
    std::string attacker;           // unique entity name, empty = none
    std::string victim;
    bool attackerIsPlayer = false;
    bool victimIsPlayer = false;
};

// Which hits a handler wants. An empty list matches everything; a non-empty one matches when the hit's value is in it. The attacker / victim lists hold
// unique entity names; `attackerPlayer` / `victimPlayer` add "the player" to them.
struct Filter {
    std::vector<std::string> kinds, spells, types, weapons, scripts, attackers, victims;
    bool attackerPlayer = false;
    bool victimPlayer = false;
};

inline bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

inline bool matches(const Filter& f, const Hit& h) {
    if (!f.kinds.empty() && !contains(f.kinds, h.kind)) {
        return false;
    }
    if (!f.spells.empty() && !contains(f.spells, h.spell)) {
        return false;
    }
    if (!f.types.empty() && !contains(f.types, h.type)) {
        return false;
    }
    if (!f.weapons.empty() && !contains(f.weapons, h.weapon)) {
        return false;
    }
    if (!f.scripts.empty() && !contains(f.scripts, h.script)) {
        return false;
    }
    if (!f.attackers.empty() || f.attackerPlayer) {
        const bool ok = (f.attackerPlayer && h.attackerIsPlayer) || (!h.attacker.empty() && contains(f.attackers, h.attacker));
        if (!ok) {
            return false;
        }
    }
    if (!f.victims.empty() || f.victimPlayer) {
        const bool ok = (f.victimPlayer && h.victimIsPlayer) || (!h.victim.empty() && contains(f.victims, h.victim));
        if (!ok) {
            return false;
        }
    }
    return true;
}

// The keys a filter table may have.
inline bool validFilterKey(const std::string& key) {
    static const char* const kKeys[] = {"kind", "spell", "type", "weapon", "script", "attacker", "victim"};
    for (const char* k : kKeys) {
        if (key == k) {
            return true;
        }
    }
    return false;
}

inline bool validKind(const std::string& s) { return s == "spell" || s == "melee" || s == "ranged" || s == "other"; }
inline bool validSpell(const std::string& s) { return s == "fireball" || s == "frost" || s == "missile" || s == "inferno"; }

}  // namespace spass::game::combat
