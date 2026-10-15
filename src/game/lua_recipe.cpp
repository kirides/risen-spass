#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/properties.h"
#include "game/recipe_lua.h"
#include "game/template_object.h"

// Crafting recipes (docs/RE.md section 30, docs/LUA_API.md "Recipes"): the natives of risen.recipe.
//
// A recipe is an item template with a gCRecipe_PS property set (86 shipped ones: smithing, alchemy, cooking, scrolls, trophies, goldsmith). The set holds
//   Craft             enum gERecipeCategory: the station that lists the recipe
//   CraftIngredients  bTObjArray<gCCraftIngredient> at +0x30: { ItemTemplate (proxy), ItemAmount }, stride 0x18, elements inline
//   RequiredSkills    bTObjArray<gCSkillValue> at +0x40 (handled by lua_balance.cpp as the array kind "recipe_required")
//   ResultItem        eCTemplateEntityProxy at +0x50, ResultAmount (never read by the engine's craft function)
// A character "knows" a recipe by carrying the recipe item in the inventory: the stations list the recipe items of their category from the inventory
// (InvFilter_Craft_*), and run PSInventory::CanSynthesize (how often the ingredients and skills allow it) and PSInventory::Synthesize (delete the
// ingredients, create the result item) -- the same two functions that risen.recipe.can_craft / craft call.

namespace spass::game {
namespace {

using Two = std::tuple<sol::object, sol::object>;
Two okResult(sol::state_view lua, sol::object value) { return {std::move(value), sol::make_object(lua, sol::lua_nil)}; }
Two failResult(sol::state_view lua, const std::string& reason) {
    return {sol::make_object(lua, sol::lua_nil), sol::make_object(lua, reason)};
}

constexpr std::size_t kIngredientsOffset = 0x30;
constexpr std::size_t kIngredientStride = 0x18;  // vftable, eCTemplateEntityProxy, unsigned long amount (+ padding)
constexpr std::size_t kMaxIngredients = 16;

// The engine's bTObjArray header ({ data*, count, capacity }, RE.md section 23).
struct ArrayHeader {
    void* data = nullptr;
    std::int32_t count = 0;
    std::int32_t capacity = 0;
};

struct Ingredient {
    std::string item;
    int count = 0;
};

// The gCRecipe_PS of a template (by name) or of an entity.
void* recipeSetOf(const sol::object& target, std::string& error) {
    if (target.get_type() == sol::type::string) {
        return templatePropertySet(target.as<std::string>(), "gCRecipe_PS", error);
    }
    if (target.is<LuaEntity>()) {
        void* entity = ptr(target.as<LuaEntity>());
        const Symbols& s = sym();
        if (entity == nullptr) {
            error = "entity is gone";
            return nullptr;
        }
        if (s.ent_get_property_set == nullptr) {
            error = "engine function eCEntity::GetPropertySet is unavailable";
            return nullptr;
        }
        void* ps = nullptr;
        const BString cls("gCRecipe_PS");
        if (!guarded([&] { ps = s.ent_get_property_set(entity, cls.get()); })) {
            error = "the engine raised an exception (not an entity?)";
            return nullptr;
        }
        if (ps == nullptr) {
            error = "entity is not a recipe (no gCRecipe_PS)";
        }
        return ps;
    }
    error = "expected a template name or a recipe entity";
    return nullptr;
}

bool readIngredients(void* recipePs, std::vector<Ingredient>& out, std::string& error) {
    bool ok = false;
    const bool ran = guarded([&] {
        const auto* hdr = reinterpret_cast<const ArrayHeader*>(static_cast<const char*>(recipePs) + kIngredientsOffset);
        if (hdr->count < 0 || hdr->count > 256 || (hdr->count > 0 && hdr->data == nullptr)) {
            error = "the ingredient array looks corrupt (count " + std::to_string(hdr->count) + ")";
            return;
        }
        for (int i = 0; i < hdr->count; ++i) {
            void* elem = static_cast<char*>(hdr->data) + static_cast<std::size_t>(i) * kIngredientStride;
            const auto item = propertyGet(elem, "ItemTemplate");
            const auto amount = propertyGet(elem, "ItemAmount");
            if (!item || !amount) {
                error = "ingredient " + std::to_string(i + 1) + " has no ItemTemplate/ItemAmount property";
                return;
            }
            Ingredient e;
            e.item = *item;
            e.count = std::atoi(amount->c_str());
            out.push_back(std::move(e));
        }
        ok = true;
    });
    if (!ran) {
        error = "the engine raised an exception while reading the ingredients";
        return false;
    }
    return ok;
}

// Replaces the ingredient array: the elements are built with the engine's constructor in a scratch buffer, checked by reading them back, and handed to
// gCRecipe_PS::SetCraftIngredients (which copies them, like SetRequiredSkills of items).
bool writeIngredients(void* recipePs, const std::vector<Ingredient>& list, std::string& error) {
    const Symbols& s = sym();
    if (s.craft_ingredient_ctor == nullptr || s.craft_ingredient_dtor == nullptr || s.recipe_set_ingredients == nullptr) {
        error = "engine functions for the recipe ingredients are unavailable";
        return false;
    }
    if (list.size() > kMaxIngredients) {
        error = "too many ingredients (max " + std::to_string(kMaxIngredients) + ")";
        return false;
    }
    alignas(16) unsigned char scratch[kMaxIngredients * kIngredientStride] = {};
    std::size_t built = 0;
    bool ok = false;
    const bool ran = guarded([&] {
        for (const Ingredient& e : list) {
            void* elem = scratch + built * kIngredientStride;
            s.craft_ingredient_ctor(elem);
            ++built;
            propertySet(elem, "ItemTemplate", e.item);
            propertySet(elem, "ItemAmount", std::to_string(e.count));
            const auto item = propertyGet(elem, "ItemTemplate");
            const auto amount = propertyGet(elem, "ItemAmount");
            if (!item || *item != e.item) {
                error = "the engine did not accept the item '" + e.item + "' (read back '" + (item ? *item : std::string("?")) + "')";
                return;
            }
            if (!amount || std::atoi(amount->c_str()) != e.count) {
                error = "the amount " + std::to_string(e.count) + " was not accepted";
                return;
            }
        }
        ArrayHeader hdr;
        hdr.data = scratch;
        hdr.count = static_cast<std::int32_t>(list.size());
        hdr.capacity = hdr.count;
        s.recipe_set_ingredients(recipePs, &hdr);
        ok = true;
    });
    guarded([&] {
        for (std::size_t i = built; i > 0; --i) {
            s.craft_ingredient_dtor(scratch + (i - 1) * kIngredientStride);
        }
    });
    if (!ran && error.empty()) {
        error = "the engine raised an exception while writing the ingredients";
    }
    return ok;
}

}  // namespace

void bindRecipe(sol::state_view lua, sol::table risen) {
    sol::table recipe = lua.create_table();
    risen["recipe"] = recipe;

    // risen.recipe.ingredients_raw(target) -> { { item = "It_IronOre", count = 2 }, ... } | nil, reason     (target: template name or recipe entity)
    recipe.set_function("ingredients_raw", [](sol::this_state ts, sol::object target) -> Two {
        sol::state_view L(ts);
        std::string error;
        void* ps = recipeSetOf(target, error);
        if (ps == nullptr) {
            return failResult(L, error);
        }
        std::vector<Ingredient> list;
        if (!readIngredients(ps, list, error)) {
            return failResult(L, error);
        }
        sol::table out = L.create_table();
        int i = 1;
        for (const Ingredient& e : list) {
            sol::table t = L.create_table();
            t["item"] = e.item;
            t["count"] = e.count;
            out[i++] = t;
        }
        return okResult(L, out);
    });

    // risen.recipe.set_ingredients_raw(target, list) -> true | nil, reason: replaces the whole ingredient array (list as returned by ingredients_raw)
    recipe.set_function("set_ingredients_raw", [](sol::this_state ts, sol::object target, sol::table entries) -> Two {
        sol::state_view L(ts);
        std::vector<Ingredient> list;
        for (std::size_t i = 1; i <= entries.size(); ++i) {
            sol::object o = entries[i];
            if (!o.is<sol::table>()) {
                return failResult(L, "ingredient " + std::to_string(i) + " must be a table { item =, count = }");
            }
            sol::table t = o.as<sol::table>();
            sol::optional<std::string> item = t["item"];
            sol::optional<double> count = t["count"];
            if (!item || !count) {
                return failResult(L, "ingredient " + std::to_string(i) + " needs item (string) and count (number)");
            }
            Ingredient e;
            e.item = *item;
            e.count = static_cast<int>(*count);
            list.push_back(std::move(e));
        }
        std::string error;
        void* ps = recipeSetOf(target, error);
        if (ps == nullptr) {
            return failResult(L, error);
        }
        if (!writeIngredients(ps, list, error)) {
            return failResult(L, error);
        }
        return okResult(L, sol::make_object(L, true));
    });

    // risen.recipe.can_craft_raw(who, recipe) -> n | nil, reason: PSInventory::CanSynthesize, how many times the character can craft the recipe
    // right now (the smallest "ingredient stock / needed" over all ingredients, 0 when a required skill is missing or the recipe has no ingredients).
    recipe.set_function("can_craft_raw", [](sol::this_state ts, const LuaEntity& who, const std::string& name) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.psinv_can_synthesize == nullptr) {
            return failResult(L, "engine function PSInventory::CanSynthesize is unavailable");
        }
        void* entity = ptr(who);
        if (entity == nullptr) {
            return failResult(L, "entity is gone");
        }
        std::string error;
        void* tpl = templateEntityPtr(name, error);
        if (tpl == nullptr) {
            return failResult(L, error);
        }
        int n = 0;
        if (!guarded([&] {
                EntityHolder inv{entity};
                EntityHolder rec{tpl};
                n = s.psinv_can_synthesize(&inv, &rec);
            })) {
            return failResult(L, "the engine raised an exception during the check");
        }
        return okResult(L, sol::make_object(L, n < 0 ? 0 : n));
    });

    // risen.recipe.craft_raw(who, recipe, count) -> stack | nil, reason: PSInventory::Synthesize WITHOUT the check: deletes `count` times the
    // ingredients that the inventory has and creates `count` result items. The stations only call it for a count that CanSynthesize allowed.
    recipe.set_function("craft_raw", [](sol::this_state ts, const LuaEntity& who, const std::string& name, int count) -> Two {
        sol::state_view L(ts);
        const Symbols& s = sym();
        if (s.psinv_synthesize == nullptr) {
            return failResult(L, "engine function PSInventory::Synthesize is unavailable");
        }
        void* entity = ptr(who);
        if (entity == nullptr) {
            return failResult(L, "entity is gone");
        }
        std::string error;
        void* tpl = templateEntityPtr(name, error);
        if (tpl == nullptr) {
            return failResult(L, error);
        }
        int stack = -1;
        if (!guarded([&] {
                EntityHolder inv{entity};
                EntityHolder rec{tpl};
                stack = s.psinv_synthesize(&inv, &rec, count);
            })) {
            return failResult(L, "the engine raised an exception while crafting");
        }
        if (stack < 0) {
            return failResult(L, "the engine refused (the character has no inventory or the template is not a recipe)");
        }
        return okResult(L, sol::make_object(L, stack));
    });

    // risen.interact: the engine's interaction of a character with a mob (the anvil, a chest, ...), docs/RE.md section 30. begin() is what the use key does:
    // gCInteraction::BeginInteraction checks the mob's CanInteract script and then sets the user's AI state ZS_Interact_PreInteract, which runs the PreInteract_* /
    // Interact_* / PostInteract_* scripts of the mob's use type (walk to the mob, open the station's inventory page, animation, craft).
    sol::table interact = lua.create_table();
    risen["interact"] = interact;
    const auto interactCall = [](const char* what, auto pick) {
        return [pick, what](sol::this_state ts, const LuaEntity& user, const LuaEntity& target) -> Two {
            sol::state_view L(ts);
            const auto fn = pick(sym());
            if (fn == nullptr) {
                return failResult(L, std::string("engine function PSInteraction::") + what + " is unavailable");
            }
            void* u = ptr(user);
            void* t = ptr(target);
            if (u == nullptr || t == nullptr) {
                return failResult(L, "entity is gone");
            }
            bool result = false;
            if (!guarded([&] {
                    EntityHolder self{u};            // the character that wants to use the mob is `this` (the mob lists its interactions per user kind)
                    EntityHolder byValueTarget{t};   // Entity is one pointer with a trivial destructor: the callee's destroy changes nothing
                    result = fn(&self, &byValueTarget);
                })) {
                return failResult(L, "the engine raised an exception");
            }
            return okResult(L, sol::make_object(L, result));
        };
    };
    // risen.interact.can(user, target) -> bool | nil, reason: gCInteraction::CanInteractWith (the mob's CanInteract script; it may print a HUD message)
    interact.set_function("can", interactCall("CanInteractWith", [](const Symbols& s) { return s.psinter_can; }));
    // risen.interact.begin(user, target) -> bool | nil, reason: starts the interaction like the use key (the user's AI state becomes ZS_Interact_PreInteract)
    interact.set_function("begin", interactCall("BeginInteraction", [](const Symbols& s) { return s.psinter_begin; }));
    // risen.interact.finish(user) -> bool: PSInteraction::EndInteraction of the character
    interact.set_function("finish", [](const LuaEntity& user) -> bool {
        const Symbols& s = sym();
        void* t = ptr(user);
        bool result = false;
        if (s.psinter_end != nullptr && t != nullptr) {
            guarded([&] {
                EntityHolder mob{t};
                result = s.psinter_end(&mob);
            });
        }
        return result;
    });

    installRecipeHelpers(lua, risen);
}

void installRecipeHelpers(sol::state_view lua, sol::table risen) {
    sol::load_result loaded = lua.load(kRecipeChunk, "=risen.recipe");
    if (!loaded.valid()) {
        spdlog::error("lua: recipe helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(risen);
    if (!res.valid()) {
        spdlog::error("lua: recipe helpers failed to run: {}", res.get<sol::error>().what());
    }
}

void installRecipeModApi(sol::state_view lua, sol::table api) {
    sol::load_result loaded = lua.load(kRecipeModChunk, "=risen.recipe.mod");
    if (!loaded.valid()) {
        spdlog::error("lua: recipe mod api failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result res = fn(api);
    if (!res.valid()) {
        spdlog::error("lua: recipe mod api failed to run: {}", res.get<sol::error>().what());
    }
}

}  // namespace spass::game
