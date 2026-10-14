#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"

// Templates, spawning and inventories (Script.dll). Verified from the decompilation (docs/RE.md §6):
//  * `Template` derives from eCTemplateEntityProxy; Template(const char*) fills it from a template name
//    such as "It_1H_Machete" (the names are listed in data/common/templates.pak).
//  * Entity::Spawn takes the Template BY VALUE (the callee destroys it) and a 4x4 pose (bCMatrix, 64 bytes,
//    row-major, translation in the last row).
//  * PSInventory is, like Entity, a holder whose first qword is the eCEntity*.

namespace spass::game {
namespace {

// Generous upper bound for sizeof(Template) (eCTemplateEntityProxy plus a few members).
struct alignas(16) TemplateStorage {
    unsigned char bytes[128] = {};
};

// Owns a Template; the destructor is skipped once ownership was handed to a by-value parameter.
class TemplateObject {
public:
    explicit TemplateObject(const std::string& name) { sym().tpl_ctor_cstr(storage_.bytes, name.c_str()); }
    TemplateObject(const TemplateObject&) = delete;
    TemplateObject& operator=(const TemplateObject&) = delete;
    ~TemplateObject() {
        if (owned_) {
            sym().tpl_dtor(storage_.bytes);
        }
    }
    bool valid() const { return sym().tpl_is_valid(storage_.bytes); }
    const void* get() const { return storage_.bytes; }
    void* giveAway() {
        owned_ = false;
        return storage_.bytes;
    }

private:
    TemplateStorage storage_;
    bool owned_ = true;
};

bool templateValid(const std::string& name) {
    bool ok = false;
    guarded([&] {
        const TemplateObject t(name);
        ok = t.valid();
    });
    return ok;
}

}  // namespace

void bindItems(sol::state_view lua, sol::table risen) {
    const Symbols& s = sym();

    sol::table tpl = lua.create_table();
    // risen.template.exists("It_1H_Machete") -> bool
    tpl.set_function("exists", [](const std::string& name) { return templateValid(name); });
    risen["template"] = tpl;

    // risen.entity.spawn("It_1H_Machete", x, y, z [, temporary]) -> Entity | nil
    sol::table entity = risen["entity"];
    entity.set_function("spawn", [&s](sol::this_state ts, const std::string& name, float x, float y, float z,
                                      sol::optional<bool> temporary) -> sol::object {
        EntityHolder ret;
        float pose[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
        bool valid = false;
        const bool ok = guarded([&] {
            TemplateObject t(name);
            valid = t.valid();
            if (valid) {
                s.ent_spawn(&ret, t.giveAway(), pose, temporary.value_or(false));  // callee destroys the template
            }
        });
        if (!ok) {
            spdlog::warn("entity.spawn('{}'): engine raised an exception (game not running?)", name);
            return sol::make_object(ts, sol::lua_nil);
        }
        if (!valid) {
            spdlog::warn("entity.spawn('{}'): unknown template", name);
        }
        return makeEntityObject(ts, ret.entity);
    });

    sol::table item = lua.create_table();
    // risen.item.give(entity, "It_1H_Machete", 1 [, stackType]) -> created amount, or -1 on failure
    item.set_function("give", [&s](const LuaEntity& owner, const std::string& name, sol::optional<int> count,
                                   sol::optional<int> stackType) -> int {
        int result = -1;
        guarded([&] {
            EntityHolder inv{owner.e};
            const TemplateObject t(name);
            if (t.valid()) {
                result = s.inv_create_items(&inv, t.get(), count.value_or(1), stackType.value_or(0));
            }
        });
        return result;
    });
    // risen.item.count(entity, "It_1H_Machete") -> integer
    item.set_function("count", [&s](const LuaEntity& owner, const std::string& name) -> int {
        int result = 0;
        guarded([&] {
            EntityHolder inv{owner.e};
            const TemplateObject t(name);
            if (t.valid()) {
                result = s.inv_item_amount(&inv, t.get());
            }
        });
        return result;
    });
    // risen.item.money(entity) -> number
    item.set_function("money", [&s](const LuaEntity& owner) -> float {
        float result = 0;
        guarded([&] {
            EntityHolder inv{owner.e};
            result = s.inv_money(&inv);
        });
        return result;
    });
    risen["item"] = item;
}

}  // namespace spass::game
