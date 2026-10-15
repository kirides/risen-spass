#include <windows.h>

#include <spdlog/spdlog.h>

#include "game/abi.h"
#include "game/lua_entity.h"
#include "game/properties.h"
#include "hook/exports.h"

// Runtime-created dialogues (docs/RE.md §11). A dialogue option is a gCInfo owned by an NPC (by entity name).
// It runs a sequence of commands (Say, RunScript, AddLogText, ...). The *text of the first command* is the
// option label shown to the player. Everything here goes through exported engine functions:
//   gCInfoSystem::CreateInfo(name)  ->  set owner/type/condition via the Access* references
//   gCInfo::InsertComSeqItem("Say") ->  configure the returned command object
// and the texts are plain localization keys that the loc overlay (risen.loc.set) answers.

namespace spass::game {
namespace {

struct LuaInfo {
    void* p = nullptr;
};

void* infoSystem() {
    const Symbols& s = sym();
    void* qm = s.qm_instance();
    return qm != nullptr ? s.qm_info_system(qm) : nullptr;
}

void setString(void* bstringField, const std::string& value) {
    sym().bstring_assign_cstr(bstringField, value.c_str());
}

// bTPropertyContainer<enum>: { vftable (8 bytes), int value } -- verified from gCInfoSystem decompilation
void setEnum(void* container, int value) {
    *reinterpret_cast<int*>(static_cast<char*>(container) + 8) = value;
}
int getEnum(const void* container) {
    return *reinterpret_cast<const int*>(static_cast<const char*>(container) + 8);
}

template <class R, class F>
sol::optional<R> tryGet(const char* what, F&& f) {
    R r{};
    if (!guarded([&] { r = f(); })) {
        spdlog::warn("dialog.{}: engine raised an exception", what);
        return sol::nullopt;
    }
    return r;
}

template <class F>
bool tryDo(const char* what, F&& f) {
    if (!guarded([&] { f(); })) {
        spdlog::warn("dialog.{}: engine raised an exception", what);
        return false;
    }
    return true;
}

}  // namespace

void bindDialog(sol::state_view lua, sol::table risen) {
    const Symbols& s = sym();

    lua.new_usertype<LuaInfo>(
        "Info", sol::no_constructor,
        sol::meta_function::to_string, [](const LuaInfo& i) { return fmt::format("Info({})", i.p); },
        // info:set_property("ConditionType", "gEInfoCondType_General") / info:get_property("Type")
        "set_property", [](const LuaInfo& i, const std::string& name, const std::string& value) { return propertySet(i.p, name, value); },
        "get_property", [](const LuaInfo& i, const std::string& name) { return propertyGet(i.p, name); },
        "list_properties", [](sol::this_state ts, const LuaInfo& i) { return propertyListTable(ts, i.p); },
        // Parameters of a command in the sequence (index from add_command): any property by name, e.g. a GiveXP's amount.
        "list_command_properties", [&s](sol::this_state ts, const LuaInfo& i, int index) -> sol::object {
            void* cmd = nullptr;
            guarded([&] { cmd = s.info_get_command(i.p, index - 1); });
            return cmd != nullptr ? sol::make_object(ts, propertyListTable(ts, cmd)) : sol::make_object(ts, sol::lua_nil);
        },
        "get_command_property", [&s](const LuaInfo& i, int index, const std::string& name) -> sol::optional<std::string> {
            void* cmd = nullptr;
            guarded([&] { cmd = s.info_get_command(i.p, index - 1); });
            return cmd != nullptr ? propertyGet(cmd, name) : sol::nullopt;
        },
        // info:set_command_text(index, "ShowSubtitle", key): the localized text of a command that has one
        // (Say, SaySVM, PickPocket, Description, ShowSubtitle, AddLogText); the text is the key of a risen.loc string.
        "set_command_text", [&s](const LuaInfo& i, int index, const std::string& command, const std::string& key) {
            void* cmd = nullptr;
            guarded([&] { cmd = s.info_get_command(i.p, index - 1); });
            if (cmd == nullptr) {
                return false;
            }
            using AccessFn = void* (*)(void*);
            AccessFn access = nullptr;
            if (command == "Say") {
                access = s.say_access_text;
            } else if (command == "Description" || command == "ShowSubtitle") {
                access = s.ctext_description;
            } else if (command == "SaySVM" || command == "PickPocket") {
                access = s.ctext_saysvm;
            }
            if (access == nullptr) {
                return false;
            }
            const BString k(key);
            return guarded([&] { s.locstr_set_key(locStringBase(access(cmd)), k.get()); });
        },
        "set_command_property", [&s](const LuaInfo& i, int index, const std::string& name, const std::string& value) {
            void* cmd = nullptr;
            guarded([&] { cmd = s.info_get_command(i.p, index - 1); });
            return cmd != nullptr && propertySet(cmd, name, value);
        },
        "set_owner", [&s](const LuaInfo& i, const std::string& v) { return tryDo("set_owner", [&] { setString(s.info_access_owner(i.p), v); }); },
        "set_parent", [&s](const LuaInfo& i, const std::string& v) { return tryDo("set_parent", [&] { setString(s.info_access_parent(i.p), v); }); },
        "set_quest", [&s](const LuaInfo& i, const std::string& v) { return tryDo("set_quest", [&] { setString(s.info_access_quest(i.p), v); }); },
        "set_type", [&s](const LuaInfo& i, int v) { return tryDo("set_type", [&] { setEnum(s.info_access_type(i.p), v); }); },
        "set_condition", [&s](const LuaInfo& i, int v) { return tryDo("set_condition", [&] { setEnum(s.info_access_condition(i.p), v); }); },
        "set_permanent", [&s](const LuaInfo& i, bool v) { return tryDo("set_permanent", [&] { *s.info_access_permanent(i.p) = v; }); },
        "set_given", [&s](const LuaInfo& i, bool v) { return tryDo("set_given", [&] { *s.info_access_given(i.p) = v; }); },
        "name", [&s](const LuaInfo& i) {
            return tryGet<std::string>("name", [&] {
                return std::string(sym().bstring_get_text(s.info_access_name(i.p)));
            });
        },
        "owner", [&s](const LuaInfo& i) {
            return tryGet<std::string>("owner", [&] {
                return std::string(sym().bstring_get_text(s.info_access_owner(i.p)));
            });
        },
        "type", [&s](const LuaInfo& i) { return tryGet<int>("type", [&] { return getEnum(s.info_access_type(i.p)); }); },
        "condition", [&s](const LuaInfo& i) { return tryGet<int>("condition", [&] { return getEnum(s.info_access_condition(i.p)); }); },
        "given", [&s](const LuaInfo& i) { return tryGet<bool>("given", [&] { return *s.info_access_given(i.p); }); },
        "conditions_fulfilled", [&s](const LuaInfo& i) { return tryGet<bool>("conditions_fulfilled", [&] { return s.info_conditions_ok(i.p); }).value_or(false); },
        "available", [&s](const LuaInfo& i) { return tryGet<bool>("available", [&] { return s.info_is_available(i.p); }).value_or(false); },
        "command_count", [&s](const LuaInfo& i) { return tryGet<int>("command_count", [&] { return s.info_command_count(i.p); }).value_or(0); },
        // info:add_command("Say") -> 1-based index, nil if the command name is unknown (names: docs/LUA_API.md)
        "add_command", [&s](const LuaInfo& i, const std::string& name) -> sol::optional<int> {
            int idx = -1;
            const bool ok = guarded([&] {
                ByValueString arg(name);  // by-value bCString: the callee destroys it
                idx = s.info_insert_command(i.p, arg.ptr());
            });
            if (!ok || idx < 0) {
                return sol::nullopt;
            }
            return idx + 1;
        },
        // info:add_script_command() / info:add_log_command(): RunScript and AddLogText cannot be created by name
        // or enum; they are created through their class factory and inserted as objects.
        "add_script_command", [&s](const LuaInfo& i) -> sol::optional<int> {
            int idx = -1;
            if (!guarded([&] {
                    if (void* cmd = s.runscript_create()) {
                        idx = s.info_insert_command_ptr(i.p, cmd);
                    }
                }) || idx < 0) {
                return sol::nullopt;
            }
            return idx + 1;
        },
        "add_log_command", [&s](const LuaInfo& i) -> sol::optional<int> {
            int idx = -1;
            if (!guarded([&] {
                    if (void* cmd = s.addlog_create()) {
                        idx = s.info_insert_command_ptr(i.p, cmd);
                    }
                }) || idx < 0) {
                return sol::nullopt;
            }
            return idx + 1;
        },
        // info:add_command_type(enum) -> 1-based index (the engine's gEInfoCommand value; see risen.dialog.command_table)
        "add_command_type", [&s](const LuaInfo& i, int type) -> sol::optional<int> {
            int idx = -1;
            if (!guarded([&] { idx = s.info_insert_command_type(i.p, type); }) || idx < 0) {
                return sol::nullopt;
            }
            return idx + 1;
        },
        // info:set_say(index, speaker, listener, text_key)
        "set_say", [&s](const LuaInfo& i, int index, const std::string& speaker, const std::string& listener,
                        const std::string& key) {
            // Staged so a fault reports which engine call is at fault.
            void* cmd = nullptr;
            if (!guarded([&] { cmd = s.info_get_command(i.p, index - 1); }) || cmd == nullptr) {
                spdlog::warn("dialog.set_say: no command at index {}", index);
                return false;
            }
            const BString sp(speaker), li(listener), k(key);
            if (!guarded([&] { s.say_set_speaker(cmd, sp.get()); })) {
                spdlog::warn("dialog.set_say: SetSpeaker faulted (cmd={})", cmd);
                return false;
            }
            if (!guarded([&] { s.say_set_listener(cmd, li.get()); })) {
                spdlog::warn("dialog.set_say: SetListener faulted");
                return false;
            }
            void* text = nullptr;
            if (!guarded([&] { text = s.say_access_text(cmd); })) {
                spdlog::warn("dialog.set_say: AccessText faulted");
                return false;
            }
            if (!guarded([&] { s.locstr_set_key(locStringBase(text), k.get()); })) {
                spdlog::warn("dialog.set_say: SetKey faulted (text={})", text);
                return false;
            }
            return true;
        },
        // info:set_script(index, "ScriptName") for a RunScript command: runs a (Lua-defined) script
        "set_script", [&s](const LuaInfo& i, int index, const std::string& script) {
            return tryDo("set_script", [&] {
                void* cmd = s.info_get_command(i.p, index - 1);
                if (cmd == nullptr) {
                    return;
                }
                const BString name(script);
                alignas(16) unsigned char proxy[64] = {};
                s.script_proxy_ctor_name(proxy, name.get());
                s.script_proxy_assign(s.runscript_access_script(cmd), proxy);
                s.script_proxy_dtor(proxy);
            });
        },
        // info:set_log(index, "QuestName", text_key) for an AddLogText command
        "set_log", [&s](const LuaInfo& i, int index, const std::string& quest, const std::string& key) {
            return tryDo("set_log", [&] {
                void* cmd = s.info_get_command(i.p, index - 1);
                if (cmd == nullptr) {
                    return;
                }
                const BString k(key);
                setString(s.addlog_access_quest(cmd), quest);
                s.locstr_set_key(locStringBase(s.addlog_access_text(cmd)), k.get());
            });
        },
        // Runs this info right now (as if the player had chosen it).
        "run", [&s](const LuaInfo& i) {
            return tryGet<bool>("run", [&] { return s.is_execute_info(infoSystem(), i.p); }).value_or(false);
        });

    sol::table dialog = lua.create_table();
    // risen.dialog.new_info("Unique_Info_Name", "OwnerEntityName") -> Info | nil
    dialog.set_function("new_info", [&s](const std::string& name, const std::string& owner) -> sol::optional<LuaInfo> {
        void* info = nullptr;
        const bool ok = guarded([&] {
            void* is = infoSystem();
            if (is == nullptr) {
                return;
            }
            const BString n(name);
            info = s.is_create_info(is, n.get());
            if (info != nullptr) {
                setString(s.info_access_owner(info), owner);
            }
        });
        if (!ok || info == nullptr) {
            spdlog::warn("dialog.new_info('{}'): could not create the info (game not ready?)", name);
            return sol::nullopt;
        }
        return LuaInfo{info};
    });
    dialog.set_function("get", [&s](const std::string& name) -> sol::optional<LuaInfo> {
        void* info = nullptr;
        guarded([&] {
            void* is = infoSystem();
            const BString n(name);
            info = is ? s.is_get_info(is, n.get()) : nullptr;
        });
        if (info == nullptr) {
            return sol::nullopt;
        }
        return LuaInfo{info};
    });
    // Must be called after creating infos so the engine's owner -> infos map includes them.
    dialog.set_function("rebuild", [&s] {
        return tryDo("rebuild", [&] {
            s.is_rebuild_owner_map(infoSystem());
            if (s.is_refresh_children != nullptr) {
                s.is_refresh_children(infoSystem());  // links child infos (Parent = <name>) to their parent infos
            }
        });
    });
    dialog.set_function("has_infos", [&s](const LuaEntity& npc) {
        return tryGet<bool>("has_infos", [&] { return s.is_has_infos(infoSystem(), ptr(npc)); }).value_or(false);
    });
    // Dev aid: the engine's own table of command names -> gEInfoCommand values, a static array of 59 entries
    // { int value; <4 pad>; bCString name } at Game.dll + 0x122ef00 (docs/RE.md §11). Version specific.
    dialog.set_function("command_table", [](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table out = L.create_table();
        const auto base = exports::moduleBase(L"Game.dll");
        guarded([&] {
            for (int k = 0; k < 59; ++k) {
                const char* entry = reinterpret_cast<const char*>(base + 0x122ef00 + static_cast<std::size_t>(k) * 16);
                const int value = *reinterpret_cast<const int*>(entry);
                const char* name = sym().bstring_get_text(entry + 8);
                out[name] = value;
            }
        });
        return out;
    });
    // Options currently offered by the running conversation: { Info, ... } (names via info:name())
    dialog.set_function("available", [&s](sol::this_state ts) {
        sol::state_view L(ts);
        sol::table out = L.create_table();
        guarded([&] {
            void* is = infoSystem();
            const int n = s.is_available_count(is);
            for (int k = 0; k < n; ++k) {
                if (void* info = s.is_get_available(is, k)) {
                    out[k + 1] = LuaInfo{info};
                }
            }
        });
        return out;
    });
    dialog.set_function("is_running", [&s] {
        return tryGet<bool>("is_running", [&] { return s.is_is_running(infoSystem()); }).value_or(false);
    });
    // risen.dialog.start(player, npc): opens the conversation like pressing the talk key
    dialog.set_function("start", [&s](const LuaEntity& player, const LuaEntity& npc) {
        return tryGet<bool>("start", [&] {
                   EntityHolder p{ptr(player)};
                   EntityHolder n{ptr(npc)};
                   return s.dialog_start(&p, &n);
               })
            .value_or(false);
    });
    risen["dialog"] = dialog;

    // risen.dialog.create{ name=, owner=, type=, condition=, quest=, lines={...} } -> Info | nil
    //   lines: { speaker="player"|"npc"|<entity name>, listener="npc"|"player"|..., text="..." }
    //          { script="LuaScriptName" }            -- runs a script defined with risen.script.define
    //          { log = { quest="Q_Name", text="..." } }  -- adds a quest log entry
    // Texts are stored in the localization overlay under "<info name>_<line number>".
    const char* chunk = R"LUA(
local risen = ...

-- Names are the engine's own enum names (docs/RE.md §11). Anything else is refused before the engine is touched.
local INFO_TYPES = { Info = true, Important = true, Comment = true, News = true, Refuse = true, Parent = true }
-- Conditions that evaluate a quest: the engine pops up a blocking message box when the quest does not exist.
local QUEST_CONDITIONS = { Open = true, Running = true, Success = true, Failed = true, Cancelled = true, Won = true,
    Lost = true, Ready = true, Overtime = true, NotYetFinished = true, Delivery = true, PartDelivery = true }
local PLAIN_CONDITIONS = { General = true, Hello = true, End = true, Back = true, Trade = true, Heal = true, Join = true,
    Dismiss = true, MobJoin = true, MobDismiss = true, SlaveJoin = true, Duel = true, Crime = true, PoliticalCrime = true,
    FirstWarn = true, SecondWarn = true, PickPocket = true, MasterThief = true, Reactivator = true, Activator = true,
    Wait = true, LongTimeNoSee = true, NothingToSay = true, Finished = true, DoCancel = true }

-- Parameters of every command that can be added by name (generated from the engine's reflection: docs/PROPERTIES.md).
local COMMAND_PROPS = {
    Attack = { Entity1 = true, Entity2 = true, Reason = true },
    Back = {  },
    BoostAttribs = { Entity1 = true, BoostTarget = true },
    ClearGameEvent = { GameEvent = true },
    CloseQuest = { QuestName = true },
    Description = {  },
    DoLoot = { Entity1 = true, Entity2 = true },
    DoTrade = { Entity1 = true, Entity2 = true },
    End = {  },
    EndGame = {  },
    FullStop = { Entity1 = true },
    Give = { Entity1 = true, Entity2 = true, Item = true, Amount = true },
    GiveXP = { Amount = true },
    Heal = { Entity1 = true, Entity2 = true },
    InfoWait = { Time = true },
    MoveTo = { Entity1 = true, Entity2 = true },
    PickPocket = { Difficulty = true, Speaker = true, Listener = true, SVMID = true, Gesture = true },
    PlayMusic = { Situation = true },
    RunInfo = { Info = true },
    RunQuest = { QuestName = true },
    Say = { Speaker = true, Listener = true, Gesture = true },
    SayConfirm = { Speaker = true, Listener = true },
    SayHoldingPosition = { Speaker = true, Listener = true },
    SayKeepItUp = { Speaker = true, Listener = true },
    SaySVM = { Speaker = true, Listener = true, SVMID = true, Gesture = true },
    SaySound = { Speaker = true, SoundName = true },
    SetGameEvent = { GameEvent = true },
    SetGuardStatus = { Entity1 = true, GuardStatus = true },
    SetMobEnabled = { Entity1 = true, MobEnabled = true },
    SetPartyEnabled = { Entity1 = true, PartyEnabled = true },
    SetRelaxingPoint = { Entity1 = true, Entity2 = true },
    SetRoutine = { Entity1 = true, RoutineName = true },
    SetSectorStatus = { SectorName = true, SectorEnabled = true },
    SetSlaveryEnabled = { Entity1 = true, SlaveryEnabled = true },
    SetSleepingPoint = { Entity1 = true, Entity2 = true },
    SetTeachEnabled = { Entity1 = true, TeachEnabled = true },
    SetTradeEnabled = { Entity1 = true, TradeEnabled = true },
    SetWorkingPoint = { Entity1 = true, Entity2 = true },
    ShowPicture = { FileName = true },
    ShowSubtitle = {  },
    Sleep = { Entity1 = true, TargetHour = true },
    StartEffect = { Entity1 = true, EffectName = true },
    SucceedQuest = { QuestName = true },
    Trigger = { TargetEntity = true, DoUntrigger = true },
    Use = { Entity1 = true, Item = true, UseAnimationName = true },
    Wear = { Entity1 = true, Item = true },
    RunScript = { Script = true, Self = true, Other = true, OtherType = true, Param = true },
    AddLogText = { QuestName = true },
}
local TEXT_COMMANDS = { Say = true, SaySVM = true, PickPocket = true, Description = true, ShowSubtitle = true }

local function nonempty(v) return type(v) == "string" and v ~= "" end

-- Returns an error message for a spec the engine could not handle, nil when it is fine.
local function check_dialog(spec)
    if type(spec) ~= "table" then return "the argument must be a table" end
    if not nonempty(spec.name) then return "'name' (unique info name) must be a non-empty string" end
    if not nonempty(spec.owner) then return "'owner' (NPC entity name) must be a non-empty string" end
    if spec.type ~= nil and not INFO_TYPES[spec.type] then
        return ("unknown type '%s' (Info, Important, Comment, News, Refuse, Parent)"):format(tostring(spec.type))
    end
    local cond = spec.condition or "General"
    if not QUEST_CONDITIONS[cond] and not PLAIN_CONDITIONS[cond] then
        return ("unknown condition '%s'"):format(tostring(cond))
    end
    if QUEST_CONDITIONS[cond] and not nonempty(spec.quest) then
        return ("condition '%s' needs 'quest' (the quest it depends on)"):format(cond)
    end
    if spec.quest ~= nil then
        if not nonempty(spec.quest) then return "'quest' must be a non-empty string" end
        if not risen.quest.exists(spec.quest) then
            return ("quest '%s' does not exist yet: define it first (risen.quest.define) - the engine cannot evaluate a missing quest"):format(spec.quest)
        end
    end
    if spec.parent ~= nil and not nonempty(spec.parent) then return "'parent' must be a non-empty string" end
    if type(spec.lines) ~= "table" or #spec.lines == 0 then return "'lines' must be a non-empty list" end
    for n, line in ipairs(spec.lines) do
        if type(line) ~= "table" then return ("line %d must be a table"):format(n) end
        if line.script ~= nil then
            if not nonempty(line.script) then return ("line %d: 'script' must be a script name"):format(n) end
            if not risen.script.exists(line.script) then
                return ("line %d: script '%s' is not registered (risen.script.define it first)"):format(n, line.script)
            end
        elseif line.command ~= nil then
            local props = COMMAND_PROPS[line.command]
            if not props then
                return ("line %d: unknown command '%s' (see docs/PROPERTIES.md; scripts and log entries have their own line forms)"):format(n, tostring(line.command))
            end
            for key, value in pairs(line) do
                if key ~= "command" and key ~= "text" then
                    if not props[key] then
                        return ("line %d: command %s has no parameter '%s'"):format(n, line.command, tostring(key))
                    end
                    if type(value) ~= "string" and type(value) ~= "number" and type(value) ~= "boolean" then
                        return ("line %d: parameter '%s' must be a string, number or boolean"):format(n, key)
                    end
                end
            end
            if line.text ~= nil and (type(line.text) ~= "string" or not TEXT_COMMANDS[line.command]) then
                return ("line %d: command %s has no text"):format(n, line.command)
            end
        elseif line.log ~= nil then
            if type(line.log) ~= "table" or not nonempty(line.log.quest) or type(line.log.text) ~= "string" then
                return ("line %d: 'log' needs { quest = ..., text = ... }"):format(n)
            end
            if not risen.quest.exists(line.log.quest) then
                return ("line %d: log quest '%s' does not exist"):format(n, line.log.quest)
            end
        else
            if type(line.text) ~= "string" then return ("line %d: 'text' must be a string"):format(n) end
            for _, key in ipairs({ "speaker", "listener" }) do
                if line[key] ~= nil and not nonempty(line[key]) then
                    return ("line %d: '%s' must be 'player', 'npc' or an entity name"):format(n, key)
                end
            end
        end
    end
    return nil
end

local function fail(what, msg)
    local text = what .. ": " .. msg
    risen.log.warn(text)
    return nil, text
end

-- risen.dialog.create(spec) -> Info | nil, error. Idempotent: an info that already exists is returned as is,
-- so the call can sit in a "create if missing" block that runs on every session start and load.
function risen.dialog.create(spec)
    local err = check_dialog(spec)
    if err then return fail("dialog.create", err) end
    local existing = risen.dialog.get(spec.name)
    if existing then return existing end

    local info = risen.dialog.new_info(spec.name, spec.owner)
    if not info then return fail("dialog.create", "the engine could not create the info '" .. spec.name .. "' (game not ready?)") end
    -- spec.type: "Info" (default), "Important", "Comment", "News", "Refuse", "Parent"
    -- spec.condition: "General" (default), "Hello", "End", "Back", "Trade", "NothingToSay", quest states ("Running", ...)
    local type_name = "gEInfoType_" .. (spec.type or "Info")
    local cond_name = "gEInfoCondType_" .. (spec.condition or "General")
    if not info:set_property("Type", type_name) then risen.log.warn("dialog: cannot set Type " .. type_name) end
    if not info:set_property("ConditionType", cond_name) then risen.log.warn("dialog: cannot set ConditionType " .. cond_name) end
    if spec.quest then info:set_quest(spec.quest) end
    if spec.parent then info:set_parent(spec.parent) end
    info:set_permanent(spec.permanent == true)
    for n, line in ipairs(spec.lines) do
        if line.script then
            local idx = info:add_script_command()
            if not idx or not info:set_script(idx, line.script) then risen.log.warn("dialog: bad script line " .. n) end
        elseif line.command then
            local idx = info:add_command(line.command)
            if not idx then
                risen.log.warn(("dialog: the engine refused command %s on line %d"):format(line.command, n))
            else
                for key, value in pairs(line) do
                    if key == "text" then
                        local tkey = ("%s_%d"):format(spec.name, n)
                        risen.loc.set(tkey, value)
                        if not info:set_command_text(idx, line.command, tkey) then risen.log.warn("dialog: cannot set the text of line " .. n) end
                    elseif key ~= "command" then
                        if not info:set_command_property(idx, key, tostring(value)) then
                            risen.log.warn(("dialog: cannot set %s.%s on line %d"):format(line.command, key, n))
                        end
                    end
                end
            end
        elseif line.log then
            local idx = info:add_log_command()
            local key = ("%s_%d"):format(spec.name, n)
            risen.loc.set(key, line.log.text)
            if not idx or not info:set_log(idx, line.log.quest, key) then risen.log.warn("dialog: bad log line " .. n) end
        else
            local idx = info:add_command("Say")
            local key = ("%s_%d"):format(spec.name, n)
            risen.loc.set(key, line.text)
            if not idx or not info:set_say(idx, line.speaker or "npc", line.listener or "player", key) then
                risen.log.warn("dialog: bad say line " .. n)
            end
        end
    end
    risen.dialog.rebuild()
    return info
end
)LUA";
    sol::load_result loaded = lua.load(chunk, "=risen.dialog");
    if (!loaded.valid()) {
        spdlog::error("lua: dialog helpers failed to load: {}", loaded.get<sol::error>().what());
        return;
    }
    sol::protected_function fn = loaded;
    sol::protected_function_result r = fn(risen);
    if (!r.valid()) {
        spdlog::error("lua: dialog helpers failed to run: {}", r.get<sol::error>().what());
    }
}

}  // namespace spass::game
