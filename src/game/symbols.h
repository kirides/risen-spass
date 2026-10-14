#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

// Every engine function we call or hook, bound by its decorated export name.
// To add one: find the symbol in %TEMP%\risen_re\<dll>.demangled.txt (tools/dump_exports.py +
// tools/demangle_exports.py), add a line to SPASS_SYMBOLS and document it in docs/RE.md.
//
//   X(field, module, mangled-name, function-pointer type, required)
//
// x64 has a single calling convention, so `this` is simply the first argument. Rules verified by
// decompilation (docs/RE.md §6):
//   * class return values that are not trivially copyable (bCString, Entity, bCVector, ...) are returned
//     through a hidden pointer: RCX for static functions, RDX (after `this`) for members;
//   * parameters of such classes passed *by value* are passed as a pointer to a caller-made copy and the
//     CALLEE destroys it -- never destroy a by-value argument after the call.
#define SPASS_SYMBOLS(X)                                                                                         \
    /* ---- SharedBase.dll: strings -------------------------------------------------------------------- */      \
    X(bstring_ctor_cstr, L"SharedBase.dll", "??0bCString@@QEAA@PEBD@Z", void* (*)(void* self, const char*), true) \
    X(bstring_ctor_copy, L"SharedBase.dll", "??0bCString@@QEAA@AEBV0@@Z", void* (*)(void* self, const void*), true) \
    X(bstring_dtor, L"SharedBase.dll", "??1bCString@@QEAA@XZ", void (*)(void* self), true)                      \
    X(bstring_get_text, L"SharedBase.dll", "?GetText@bCString@@QEBAPEBDXZ", const char* (*)(const void* self), true) \
    X(ustring_ctor_wide, L"SharedBase.dll", "??0bCUnicodeString@@QEAA@PEB_W@Z", void* (*)(void* self, const wchar_t*), true) \
    X(ustring_dtor, L"SharedBase.dll", "??1bCUnicodeString@@QEAA@XZ", void (*)(void* self), true)               \
    X(ustring_get_text, L"SharedBase.dll", "?GetText@bCUnicodeString@@QEBAPEB_WXZ", const wchar_t* (*)(const void* self), true) \
    /* ---- Engine.dll: script registry, localization ------------------------------------------------- */      \
    X(script_map_instance, L"Engine.dll", "?GetInstance@eCScriptFunctionMap@@SAAEAV1@XZ", void* (*)(), true)    \
    X(script_map_register, L"Engine.dll", "?RegisterScript@eCScriptFunctionMap@@QEAAPEAUeSScriptBase@@AEBVbCString@@@Z", void* (*)(void* self, const void* name), true) \
    X(script_map_get, L"Engine.dll", "?GetScript@eCScriptFunctionMap@@QEBAPEBUeSScriptBase@@AEBVbCString@@@Z", void* (*)(const void* self, const void* name), true) \
    X(loc_instance, L"Engine.dll", "?GetInstance@eCLocAdmin2@@SAAEAV1@XZ", void* (*)(), true)                   \
    X(loc_set_string, L"Engine.dll", "?SetString@eCLocAdmin2@@QEAA_NAEBVbCString@@AEBVbCUnicodeString@@@Z", bool (*)(void* self, const void* id, const void* text), true) \
    X(loc_get_string, L"Engine.dll", "?GetString@eCLocAdmin2@@QEBA?AVbCUnicodeString@@AEBVbCString@@@Z", void* (*)(const void* self, void* ret, const void* id), true) \
    X(loc_exists_id, L"Engine.dll", "?ExistsID@eCLocAdmin2@@QEBA_NAEBVbCString@@@Z", bool (*)(const void* self, const void* id), true) \
    X(loc_set_string_lang, L"Engine.dll", "?SetString@eCLocAdmin2@@QEAA_NAEBVbCString@@0AEBVbCUnicodeString@@@Z", bool (*)(void* self, const void* lang, const void* id, const void* text), true) \
    X(loc_get_text_language, L"Engine.dll", "?GetTextLanguage@eCLocAdmin2@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(loc_reserve_id, L"Engine.dll", "?ReserveID@eCLocAdmin2@@QEAAXAEBVbCString@@@Z", void (*)(void* self, const void* id), true) \
    /* ---- Game.dll: script processing, session ------------------------------------------------------- */      \
    X(script_admin_post_init, L"Game.dll", "?PostInitialize@gCScriptAdmin@@UEAA?AW4bEResult@@XZ", int (*)(void* self), true) \
    X(script_admin_call, L"Game.dll", "?CallScript@gCScriptAdmin@@IEAA_JAEBVbCString@@PEAVeCEntity@@1_J@Z", std::int64_t (*)(void* self, const void* name, void* selfEntity, void* otherEntity, std::int64_t arg), true) \
    X(unit_get_self, L"Game.dll", "?GetSelfEntity@gCScriptProcessingUnit@@QEAAPEAVeCGeometryEntity@@XZ", void* (*)(void* unit), true) \
    X(unit_get_other, L"Game.dll", "?GetOtherEntity@gCScriptProcessingUnit@@QEAAPEAVeCGeometryEntity@@XZ", void* (*)(void* unit), true) \
    X(unit_get_arg, L"Game.dll", "?GetIntParameter@gCScriptProcessingUnit@@QEAA_JXZ", std::int64_t (*)(void* unit), true) \
    X(session_process, L"Game.dll", "?Process@gCSession@@UEAAXXZ", void (*)(void* self), true)                 \
    /* ---- Script.dll: the API the game's own scripts use -------------------------------------------- */      \
    X(quest_open, L"Script.dll", "?OpenQuest@QuestManager@@SA_NAEBVbCString@@@Z", bool (*)(const void*), true)  \
    X(quest_run, L"Script.dll", "?RunQuest@QuestManager@@SA_NAEBVbCString@@@Z", bool (*)(const void*), true)    \
    X(quest_close, L"Script.dll", "?CloseQuest@QuestManager@@SA_NAEBVbCString@@@Z", bool (*)(const void*), true) \
    X(quest_fail, L"Script.dll", "?FailQuest@QuestManager@@SA_NAEBVbCString@@@Z", bool (*)(const void*), true)  \
    X(quest_succeed, L"Script.dll", "?SucceedQuest@QuestManager@@SA_NAEBVbCString@@@Z", bool (*)(const void*), true) \
    X(quest_is_running, L"Script.dll", "?IsRunning@QuestManager@@SA_NAEBVbCString@@@Z", bool (*)(const void*), true) \
    X(world_new_game, L"Script.dll", "?NewGame@World@@QEAAXXZ", void (*)(void* self), true)                     \
    X(world_quick_load, L"Script.dll", "?QuickLoad@World@@QEAAXXZ", void (*)(void* self), true)                 \
    X(world_is_game_running, L"Script.dll", "?IsGameRunning@World@@QEBA_NXZ", bool (*)(const void* self), true) \
    X(gamevar_get, L"Script.dll", "?GetGameVariable@InfoSystem@@SA_NAEBVbCString@@AEAH@Z", bool (*)(const void*, int*), true) \
    X(gamevar_set, L"Script.dll", "?SetGameVariable@InfoSystem@@SA_NAEBVbCString@@H@Z", bool (*)(const void*, int), true) \
    X(ent_get_entity, L"Script.dll", "?GetEntity@Entity@@SA?AV1@VbCString@@@Z", void* (*)(void* ret, void* byValueName), true) \
    X(ent_get_player, L"Script.dll", "?GetPlayer@Entity@@SA?AV1@XZ", void* (*)(void* ret), true)                \
    X(ent_get_name, L"Script.dll", "?GetName@Entity@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(ent_get_display_name, L"Script.dll", "?GetDisplayName@Entity@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(ent_set_name, L"Script.dll", "?SetName@Entity@@QEAAXAEBVbCString@@@Z", void (*)(void* self, const void* name), true) \
    X(ent_get_position, L"Script.dll", "?GetPosition@Entity@@QEBA?AVbCVector@@XZ", void* (*)(const void* self, float* ret3), true) \
    X(ent_distance_to, L"Script.dll", "?GetDistanceTo@Entity@@QEBAMAEBV1@@Z", float (*)(const void* self, const void* other), true) \
    X(ent_is_npc, L"Script.dll", "?IsNPC@Entity@@QEBA_NXZ", bool (*)(const void*), true)                        \
    X(ent_is_player, L"Script.dll", "?IsPlayer@Entity@@QEBA_NXZ", bool (*)(const void*), true)                  \
    X(ent_is_dead, L"Script.dll", "?IsDead@Entity@@QEBA_NXZ", bool (*)(const void*), true)                      \
    X(ent_is_item, L"Script.dll", "?IsItem@Entity@@QEBA_NXZ", bool (*)(const void*), true)                      \
    X(ent_is_character, L"Script.dll", "?IsCharacter@Entity@@QEBA_NXZ", bool (*)(const void*), true)            \
    X(ent_kill, L"Script.dll", "?Kill@Entity@@QEAAXXZ", void (*)(void* self), true)                             \
    X(ent_set_frozen, L"Script.dll", "?SetFrozen@Entity@@QEAAX_N@Z", void (*)(void* self, bool), true)          \
    X(ent_enable_rendering, L"Script.dll", "?EnableRendering@Entity@@QEAAX_N@Z", void (*)(void* self, bool), true) \
    X(ent_teleport_name, L"Script.dll", "?Teleport@Entity@@QEAA_NAEBVbCString@@@Z", bool (*)(void* self, const void* name), true) \
    X(ent_teleport_entity, L"Script.dll", "?Teleport@Entity@@QEAA_NAEBV1@@Z", bool (*)(void* self, const void* other), true)

namespace spass::game {

#define SPASS_DECLARE(field, module, name, type, required) std::type_identity_t<type> field = nullptr;
struct Symbols {
    SPASS_SYMBOLS(SPASS_DECLARE)
};
#undef SPASS_DECLARE

// The table, filled by resolveSymbols().
const Symbols& sym();

// Resolves every not-yet-resolved symbol of SPASS_SYMBOLS whose module is currently loaded. Logs each
// missing symbol; returns the names of missing *required* ones (empty = all good). Safe to call under
// the loader lock. Call again once Script.dll is loaded (see symbols.cpp).
std::vector<std::string> resolveSymbols();

}  // namespace spass::game
