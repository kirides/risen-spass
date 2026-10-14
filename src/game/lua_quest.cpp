#include <cstring>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/properties.h"

// Runtime-created quests (docs/RE.md §12). A quest is a gCQuest made by its class factory, named through the
// object's vtable (SetName, slot +0x108, exactly as gCQuestManager::AddQuest does), configured by property name
// (Type, LogTopic, ExperiencePoints, ...) and registered with gCQuestManager::AddQuest. Status changes use the
// existing risen.quest.open/run/succeed/... functions; log entries use an AddLogText dialogue command
// (risen.quest.add_log).

namespace spass::game {
namespace {

struct LuaQuest {
    void* p = nullptr;
};

// SLogEntry array inside gCQuest: { data*, int count, int capacity } at +0x50; element = 2 x eCLocString (0x18 each)
constexpr std::size_t kLogArrayOffset = 0x50;
constexpr std::size_t kLogElementSize = 0x30;
constexpr std::size_t kVtableSetName = 0x108 / 8;
constexpr std::size_t kVtableGetName = 0x100 / 8;

void* questManager() { return sym().qm_instance(); }

void* vcall(void* object, std::size_t slot) {
    return (*reinterpret_cast<void***>(object))[slot];
}

}  // namespace

void bindQuestObjects(sol::state_view lua, sol::table risen) {
    const Symbols& s = sym();

    lua.new_usertype<LuaQuest>(
        "Quest", sol::no_constructor,
        sol::meta_function::to_string, [](const LuaQuest& q) { return fmt::format("Quest({})", q.p); },
        "set_property", [](const LuaQuest& q, const std::string& name, const std::string& value) { return propertySet(q.p, name, value); },
        "get_property", [](const LuaQuest& q, const std::string& name) { return propertyGet(q.p, name); },
        "name", [](const LuaQuest& q) -> sol::optional<std::string> {
            std::string name;
            if (!guarded([&] {
                    OutString out;
                    reinterpret_cast<void (*)(void*, void*)>(vcall(q.p, kVtableGetName))(q.p, out.ptr());
                    name = out.take();
                })) {
                return sol::nullopt;
            }
            return name;
        },
        // quest:set_topic(key): the quest's title in the log (a localization key, e.g. served by risen.loc.set)
        "set_topic", [&s](const LuaQuest& q, const std::string& key) {
            bool ok = false;
            guarded([&] {
                const BString k(key);
                s.locstr_set_key(locStringBase(s.quest_access_topic(q.p)), k.get());
                ok = true;
            });
            return ok;
        },
        // quest:add_log_entry(key): appends a log entry whose text is the localization key.
        // The log is a bTObjArray<SLogEntry> at quest+0x50 { data*, int count, int capacity }; an SLogEntry is two
        // eCLocStrings (text, second text; 0x18 bytes each = 0x30), see DrawQuestLog/DrawLogEntry (docs/RE.md §12).
        // We grow the array with the engine's own allocator, exactly like its templates do.
        "add_log_entry", [&s](const LuaQuest& q, const std::string& key) {
            bool ok = false;
            guarded([&] {
                char* array = static_cast<char*>(q.p) + kLogArrayOffset;
                auto*& data = *reinterpret_cast<char**>(array);
                auto& count = *reinterpret_cast<int*>(array + 8);
                auto& capacity = *reinterpret_cast<int*>(array + 12);
                if (count >= capacity) {
                    const int newCap = capacity < 4 ? 4 : capacity + capacity / 2;
                    void* grown = s.mem_realloc(s.mem_instance(), data, static_cast<unsigned long>(newCap) * kLogElementSize,
                                                nullptr, nullptr, nullptr, -1);
                    if (grown == nullptr) {
                        return;
                    }
                    std::memset(static_cast<char*>(grown) + static_cast<std::size_t>(count) * kLogElementSize, 0,
                                static_cast<std::size_t>(newCap - count) * kLogElementSize);
                    data = static_cast<char*>(grown);
                    capacity = newCap;
                }
                char* element = data + static_cast<std::size_t>(count) * kLogElementSize;
                std::memset(element, 0, kLogElementSize);
                const BString k(key);
                s.locstring_ctor_key(element, k.get());               // text
                s.locstring_ctor_default(element + 0x18);              // second string (unused, empty)
                ++count;
                ok = true;
            });
            return ok;
        },
        // number of quest log entries
        "log_count", [&s](const LuaQuest& q) -> int {
            int n = 0;
            guarded([&] {
                const char* array = static_cast<const char*>(s.quest_get_log_text(q.p));
                n = *reinterpret_cast<const int*>(array + 8);  // bTObjArray: { T* data, int count, int capacity } (quest+0x50)
            });
            return n;
        },
        // Adds the quest to the quest manager (the engine logs and refuses duplicate names).
        "register", [&s](const LuaQuest& q) -> bool {
            bool ok = false;
            guarded([&] {
                std::string name;
                {
                    OutString out;
                    reinterpret_cast<void (*)(void*, void*)>(vcall(q.p, kVtableGetName))(q.p, out.ptr());
                    name = out.take();
                }
                const BString n(name);
                ok = s.qm_add_quest(questManager(), q.p, n.get());
            });
            return ok;
        });

    sol::table quest = risen["quest"];
    // risen.quest.new("Q_Name") -> Quest | nil (not yet registered; see risen.quest.define in LUA_API.md)
    quest.set_function("new", [&s](const std::string& name) -> sol::optional<LuaQuest> {
        void* q = nullptr;
        const bool ok = guarded([&] {
            q = s.quest_create_object();
            if (q != nullptr) {
                const BString n(name);
                reinterpret_cast<void (*)(void*, const void*)>(vcall(q, kVtableSetName))(q, n.get());
            }
        });
        if (!ok || q == nullptr) {
            spdlog::warn("quest.new('{}'): could not create the quest", name);
            return sol::nullopt;
        }
        return LuaQuest{q};
    });
    quest.set_function("get", [&s](const std::string& name) -> sol::optional<LuaQuest> {
        void* q = nullptr;
        guarded([&] {
            const BString n(name);
            q = s.qm_get_quest(questManager(), n.get());
        });
        if (q == nullptr) {
            return sol::nullopt;
        }
        return LuaQuest{q};
    });
    // risen.quest.count() / risen.quest.at(i) (1-based): enumerate every quest known to the manager
    quest.set_function("count", [&s]() {
        int n = 0;
        guarded([&] { n = s.qm_quest_count(questManager()); });
        return n;
    });
    quest.set_function("at", [&s](int index) -> sol::optional<LuaQuest> {
        void* q = nullptr;
        guarded([&] { q = s.qm_quest_at(questManager(), index - 1); });
        if (q == nullptr) {
            return sol::nullopt;
        }
        return LuaQuest{q};
    });
    quest.set_function("exists", [&s](const std::string& name) {
        void* q = nullptr;
        guarded([&] {
            const BString n(name);
            q = s.qm_get_quest(questManager(), n.get());
        });
        return q != nullptr;
    });
    quest.set_function("remove", [&s](const std::string& name) {
        bool r = false;
        guarded([&] {
            const BString n(name);
            r = s.qm_remove_quest(questManager(), n.get());
        });
        return r;
    });

    // risen.quest.define{ name=, title=, type=, xp=, silent= } -> Quest | nil   (registered, status "Open")
    // risen.quest.add_log(quest_name, text) -> bool   adds an entry to the quest log
    const char* chunk = R"LUA(
local risen = ...

local QUEST_TYPES = { Report = true, Kill = true, HasItems = true, EnterArea = true, BringNpc = true, FollowNpc = true,
    DriveAway = true, Defeat = true, Duel = true, Arena = true, Plunder = true, Sparring = true }

local function fail(what, msg)
    local text = what .. ": " .. msg
    risen.log.warn(text)
    return nil, text
end

-- risen.quest.define(spec) -> Quest | nil, error. Idempotent: an existing quest of that name is returned as is.
function risen.quest.define(spec)
    if type(spec) ~= "table" then return fail("quest.define", "the argument must be a table") end
    if type(spec.name) ~= "string" or spec.name == "" then return fail("quest.define", "'name' must be a non-empty string") end
    if spec.type ~= nil and not QUEST_TYPES[spec.type] then
        return fail("quest.define", ("unknown type '%s' (Report, Kill, HasItems, EnterArea, BringNpc, FollowNpc, DriveAway, Defeat, Duel, Arena, Plunder, Sparring)"):format(tostring(spec.type)))
    end
    if spec.title ~= nil and type(spec.title) ~= "string" then return fail("quest.define", "'title' must be a string") end
    if spec.xp ~= nil and (type(spec.xp) ~= "number" or spec.xp < 0) then return fail("quest.define", "'xp' must be a non-negative number") end
    if risen.quest.exists(spec.name) then
        risen.log.debug("quest.define: '" .. spec.name .. "' already exists")
        return risen.quest.get(spec.name)
    end
    local q = risen.quest.new(spec.name)
    if not q then return fail("quest.define", "the engine could not create the quest (game not ready?)") end
    local function prop(name, value)
        if not q:set_property(name, value) then risen.log.warn(("quest.define: cannot set %s=%s"):format(name, value)) end
    end
    prop("Type", "gEQuestType_" .. (spec.type or "Report"))
    local topic_key = spec.name .. "_Topic"
    risen.loc.set(topic_key, spec.title or spec.name)
    if not q:set_topic(topic_key) then risen.log.warn("quest.define: cannot set the log topic") end
    if spec.xp then prop("ExperiencePoints", tostring(spec.xp)) end
    if spec.silent ~= nil then prop("Silent", spec.silent and "true" or "false") end
    if not q:register() then return fail("quest.define", "the quest manager refused '" .. spec.name .. "'") end
    return q
end

-- risen.quest.add_log(quest_name, text) -> true | false, error
local log_counter = 0
function risen.quest.add_log(quest_name, text)
    if type(quest_name) ~= "string" or type(text) ~= "string" then return false, "quest.add_log: quest name and text must be strings" end
    local q = risen.quest.get(quest_name)
    if not q then
        local msg = "quest.add_log: quest '" .. quest_name .. "' does not exist"
        risen.log.warn(msg)
        return false, msg
    end
    log_counter = log_counter + 1
    local key = ("%s_Log%d"):format(quest_name, log_counter)
    risen.loc.set(key, text)
    return q:add_log_entry(key)
end
)LUA";
    sol::load_result loaded = lua.load(chunk, "=risen.quest");
    if (!loaded.valid()) {
        spdlog::error("lua: quest helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result r = fn(risen);
    if (!r.valid()) {
        spdlog::error("lua: quest helpers failed to run: {}", r.get<sol::error>().what());
    }
}

}  // namespace spass::game
