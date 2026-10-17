#include "core/spells.h"

#include <luajit/lua.hpp>

#include <algorithm>
#include <set>

namespace mk {

const std::vector<ShippedSpell>& shipped_spells() {
    static const std::vector<ShippedSpell> v = [] {
        std::vector<ShippedSpell> out;
        out.push_back({"Fireball", true, "Fireball", "It_Crystal_Fire", {"It_Projectile_Fireball_S0", "It_Projectile_Fireball_S1", "It_Projectile_Fireball_S2"}, {"crystal", "scroll"}});
        out.push_back({"Frost", true, "Frost", "It_Crystal_Frost", {"It_Projectile_Frost_S0", "It_Projectile_Frost_S1", "It_Projectile_Frost_S2"}, {"crystal", "scroll"}});
        out.push_back({"Missile", true, "Missile", "It_Crystal_Magic", {"It_Projectile_Missile_S0"}, {"crystal", "scroll"}});
        for (const char* n : {"Beast", "Berserker", "BlueBarrier", "EnhancedHeal", "Illusion", "Inferno", "Joke", "Levitate", "Light", "MediumHeal", "MinorHeal", "Nautilus",
                              "OpenLock", "Protection", "RedBarrier", "Skeleton", "Speed", "Telekinesis"})
            out.push_back({n, false, "", "", {}, {"rune", "scroll", "orb"}});
        std::sort(out.begin(), out.end(), [](const ShippedSpell& a, const ShippedSpell& b) { return a.name < b.name; });
        return out;
    }();
    return v;
}

const ShippedSpell* shipped_spell(const std::string& name) {
    for (auto& s : shipped_spells())
        if (s.name == name) return &s;
    return nullptr;
}

std::string spell_form_template(const ShippedSpell& s, const std::string& form) {
    if (s.projectile) return form == "crystal" ? s.crystal : std::string();
    if (form == "rune") return "It_Ru_" + s.name;
    if (form == "scroll") return "It_Scr_" + s.name;
    if (form == "orb") return "It_Orb_" + s.name;
    return {};
}

std::string new_spell_template(const std::string& name, const std::string& form) {
    if (form == "scroll") return name + "_Scroll";
    if (form == "orb") return name + "_Orb";
    return name;
}

const std::vector<std::string>& spell_damage_types() {
    static const std::vector<std::string> v = {"None", "Blunt", "Edge", "Fire", "Ice", "Magic", "Physics", "Point"};
    return v;
}

namespace {

// one Lua state for many string.find calls (the group selection tests every template)
class PatternLua {
public:
    PatternLua() {
        L_ = luaL_newstate();
        if (!L_) return;
        lua_pushcfunction(L_, luaopen_base);
        lua_pushstring(L_, "");
        lua_call(L_, 1, 0);
        lua_pushcfunction(L_, luaopen_string);
        lua_pushstring(L_, "string");
        lua_call(L_, 1, 0);
    }
    ~PatternLua() {
        if (L_) lua_close(L_);
    }
    PatternLua(const PatternLua&) = delete;
    PatternLua& operator=(const PatternLua&) = delete;
    // string.find(text, pattern) ~= nil; `error` is set for a bad pattern
    bool find(const std::string& pattern, const std::string& text, std::string& error) {
        error.clear();
        if (!L_) {
            error = "no Lua state";
            return false;
        }
        const int top = lua_gettop(L_);
        lua_getglobal(L_, "string");
        lua_getfield(L_, -1, "find");
        lua_pushlstring(L_, text.data(), text.size());
        lua_pushlstring(L_, pattern.data(), pattern.size());
        bool hit = false;
        if (lua_pcall(L_, 2, 1, 0) != 0) error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "bad pattern";
        else hit = !lua_isnil(L_, -1);
        lua_settop(L_, top);
        return hit;
    }

private:
    lua_State* L_ = nullptr;
};

}  // namespace

bool lua_pattern_matches(const std::string& pattern, const std::string& text, std::string& error) {
    PatternLua lua;
    return lua.find(pattern, text, error);
}

std::vector<std::string> npc_group_templates(const NpcGroup& g, const GameData& game, std::string& error) {
    error.clear();
    std::vector<std::string> out;
    std::set<std::string> seen;
    PatternLua lua;
    auto push = [&](const std::string& n) {
        if (seen.insert(n).second) out.push_back(n);
    };
    auto passes = [&](const std::string& n) {
        std::string e;
        if (!g.match.empty()) {
            if (!lua.find(g.match, n, e)) {
                if (!e.empty()) error = "bad pattern '" + g.match + "'";
                return false;
            }
        }
        if (!g.exclude.empty()) {
            if (lua.find(g.exclude, n, e)) return false;
            if (!e.empty()) {
                error = "bad pattern '" + g.exclude + "'";
                return false;
            }
        }
        return true;
    };
    auto below = [](const std::string& group, const std::string& path) { return group == path || group.rfind(path + "/", 0) == 0; };
    const bool use_universe = !g.group.empty() || !g.match.empty() || !g.exclude.empty() || g.names.empty();
    if (use_universe) {
        for (auto& t : game.templates()) {
            if (!g.group.empty() ? !below(t->group, g.group) : !(below(t->group, "NPC") || below(t->group, "Monster"))) continue;
            if (t->kind != TplKind::Npc && t->kind != TplKind::Monster) continue;
            if (g.group.empty() && t->name == "PC_Hero") continue;
            if (!error.empty()) return {};
            if (passes(t->name)) push(t->name);
            if (!error.empty()) return {};
        }
    }
    for (auto& n : g.names) push(n);
    return out;
}

}  // namespace mk
