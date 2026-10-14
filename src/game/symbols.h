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
    X(ustring_assign_wide, L"SharedBase.dll", "??4bCUnicodeString@@QEAAAEAV0@PEB_W@Z", void* (*)(void* self, const wchar_t*), true) \
    /* ---- FFC.dll: string tables (every localized string read passes through LookupValue) ------------ */      \
    X(table_lookup_value3, L"FFC.dll", "?LookupValue@CFXTableMap@@QEBA_NAEBVbCUnicodeString@@0AEAV2@@Z", bool (*)(const void* self, const void* lang, const void* key, void* out), true) \
    X(table_lookup_value2, L"FFC.dll", "?LookupValue@CFXTableMap@@QEBA_NAEBVbCUnicodeString@@AEAV2@@Z", bool (*)(const void* self, const void* key, void* out), true) \
    X(table_has_key, L"FFC.dll", "?HasKey@CFXTableMap@@QEBA_NAEBVbCUnicodeString@@@Z", bool (*)(const void* self, const void* key), true) \
    X(locstr_get_key, L"FFC.dll", "?GetKey@CFXLocString@@QEBA?AVbCUnicodeString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(locstr_get_text, L"FFC.dll", "?GetText@CFXLocString@@QEBA?AVbCUnicodeString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(locstr_get_text_col_a, L"FFC.dll", "?GetText@CFXLocString@@QEBA?AVbCUnicodeString@@AEBVbCString@@@Z", void* (*)(const void* self, void* ret, const void* col), true) \
    X(locstr_get_text_col_w, L"FFC.dll", "?GetText@CFXLocString@@QEBA?AVbCUnicodeString@@AEBV2@@Z", void* (*)(const void* self, void* ret, const void* col), true) \
    X(locstr_get_plain, L"FFC.dll", "?GetPlainText@CFXLocString@@QEBA?AVbCUnicodeString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(locstr_get_plain_col_a, L"FFC.dll", "?GetPlainText@CFXLocString@@QEBA?AVbCUnicodeString@@AEBVbCString@@@Z", void* (*)(const void* self, void* ret, const void* col), true) \
    X(locstr_get_plain_col_w, L"FFC.dll", "?GetPlainText@CFXLocString@@QEBA?AVbCUnicodeString@@AEBV2@@Z", void* (*)(const void* self, void* ret, const void* col), true) \
    X(locstr_to_bstring, L"FFC.dll", "??BCFXLocString@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(locstrex_get_text, L"FFC.dll", "?GetText@CFXLocStringEx@@QEBA?AVbCUnicodeString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(locstrex_get_plain, L"FFC.dll", "?GetPlainText@CFXLocStringEx@@QEBA?AVbCUnicodeString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(locstrex_to_bstring, L"FFC.dll", "??BCFXLocStringEx@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(locstrex_to_ustring, L"FFC.dll", "??BCFXLocStringEx@@QEBA?AVbCUnicodeString@@XZ", void* (*)(const void* self, void* ret), true) \
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
    X(world_quick_save, L"Script.dll", "?QuickSave@World@@QEAAXXZ", void (*)(void* self), false) \
    X(world_auto_save, L"Script.dll", "?AutoSave@World@@QEAAXAEBVbCString@@@Z", void (*)(void* self, const void* name), false) \
    X(world_is_save_allowed, L"Script.dll", "?IsSaveAllowed@World@@QEBA_NXZ", bool (*)(const void* self), false) \
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
    X(tpl_ctor_cstr, L"Script.dll", "??0Template@@QEAA@PEBD@Z", void* (*)(void* self, const char* name), true)   \
    X(tpl_dtor, L"Script.dll", "??1Template@@QEAA@XZ", void (*)(void* self), true)                              \
    X(tpl_is_valid, L"Script.dll", "?IsValid@Template@@QEBA_NXZ", bool (*)(const void* self), true)            \
    X(ent_spawn, L"Script.dll", "?Spawn@Entity@@SA?AV1@VTemplate@@AEBVbCMatrix@@_N@Z", void* (*)(void* ret, void* byValueTemplate, const float* pose16, bool temporary), true) \
    X(inv_create_items, L"Script.dll", "?CreateItems@PSInventory@@QEAAHAEBVTemplate@@HW4gEStackType@@@Z", int (*)(void* self, const void* tpl, int count, int stackType), true) \
    X(inv_item_amount, L"Script.dll", "?GetItemAmount@PSInventory@@QEBAHAEBVTemplate@@@Z", int (*)(const void* self, const void* tpl), true) \
    X(inv_money, L"Script.dll", "?GetMoneyAmount@PSInventory@@QEBAMXZ", float (*)(const void* self), true)    \
    /* PSNpc property wrappers: `this` is a holder whose first qword is the eCEntity* (property set id 0x1e) */   \
    X(npc_set_guild, L"Script.dll", "??4PropertyGuild@PSNpc@@QEAAAEAV01@AEBW4gEGuild@@@Z", void* (*)(void* self, const int* v), true) \
    X(npc_get_guild, L"Script.dll", "??BPropertyGuild@PSNpc@@QEBA?BW4gEGuild@@XZ", int (*)(const void* self), true) \
    X(npc_set_group, L"Script.dll", "??4PropertyGroup@PSNpc@@QEAAAEAV01@AEBVbCString@@@Z", void* (*)(void* self, const void* bstr), true) \
    X(npc_get_group, L"Script.dll", "??BPropertyGroup@PSNpc@@QEBA?BVbCString@@XZ", void* (*)(const void* self, void* ret), true) \
    /* PSSkills (holder + property set): set functions take (value, gESkillModifier) */                          \
    X(sk_get_hp, L"Script.dll", "?GetHitPoints@PSSkills@@QEBAHXZ", int (*)(const void*), true)                  \
    X(sk_get_hp_max, L"Script.dll", "?GetHitPointsMax@PSSkills@@QEBAHXZ", int (*)(const void*), true)           \
    X(sk_get_level, L"Script.dll", "?GetLevel@PSSkills@@QEBAHXZ", int (*)(const void*), true)                   \
    X(sk_get_mana, L"Script.dll", "?GetManaPoints@PSSkills@@QEBAHXZ", int (*)(const void*), true)               \
    X(sk_get_mana_max, L"Script.dll", "?GetManaPointsMax@PSSkills@@QEBAHXZ", int (*)(const void*), true)        \
    X(sk_set_hp, L"Script.dll", "?SetHitPoints@PSSkills@@QEAAXHW4gESkillModifier@@@Z", void (*)(void*, int, int), true) \
    X(sk_set_hp_max, L"Script.dll", "?SetHitPointsMax@PSSkills@@QEAAXHW4gESkillModifier@@@Z", void (*)(void*, int, int), true) \
    X(sk_set_level, L"Script.dll", "?SetLevel@PSSkills@@QEAAXHW4gESkillModifier@@@Z", void (*)(void*, int, int), true) \
    X(sk_set_mana, L"Script.dll", "?SetManaPoints@PSSkills@@QEAAXHW4gESkillModifier@@@Z", void (*)(void*, int, int), true) \
    X(sk_set_mana_max, L"Script.dll", "?SetManaPointsMax@PSSkills@@QEAAXHW4gESkillModifier@@@Z", void (*)(void*, int, int), true) \
    /* PSRoutine: AI state machine (property set 0x24) */                                                        \
    X(rt_set_state, L"Script.dll", "?SetState@PSRoutine@@QEAAXVbCString@@@Z", void (*)(void* self, void* byValueName), true) \
    X(rt_get_state, L"Script.dll", "?GetCurrentState@PSRoutine@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), true) \
    X(rt_full_stop, L"Script.dll", "?FullStop@PSRoutine@@QEAAXXZ", void (*)(void* self), true)                  \
    X(ent_goto_entity, L"Script.dll", "?StartGoto@Entity@@QEAA_NAEAV1@W4gEWalkMode@@@Z", bool (*)(void* self, void* target, int walkMode), true) \
    X(ent_goto_pos, L"Script.dll", "?StartGoto@Entity@@QEAA_NAEBVbCVector@@W4gEWalkMode@@@Z", bool (*)(void* self, const float* xyz, int walkMode), true) \
    X(ent_stop_goto, L"Script.dll", "?StopGoto@Entity@@QEAAXXZ", void (*)(void* self), true)                    \
    X(ent_turn_to, L"Script.dll", "?StartTurnTo@Entity@@QEAA_NAEBV1@@Z", bool (*)(void* self, const void* target), true) \
    X(inv_equip_stack, L"Script.dll", "?EquipStack@PSInventory@@QEAA_NH@Z", bool (*)(void* self, int stack), true) \
    X(inv_stack_count, L"Script.dll", "?GetStackCount@PSInventory@@QEBAHXZ", int (*)(const void* self), true)   \
    /* ---- dialogue (gCInfo / gCInfoSystem, Game.dll) -- see docs/RE.md §11 ----------------------------------- */ \
    X(qm_instance, L"Game.dll", "?GetInstance@gCQuestManager@@SAPEAV1@XZ", void* (*)(), true)                    \
    X(qm_info_system, L"Game.dll", "?GetInfoManager@gCQuestManager@@QEAAPEAVgCInfoSystem@@XZ", void* (*)(void* self), true) \
    X(is_create_info, L"Game.dll", "?CreateInfo@gCInfoSystem@@QEAAPEAVgCInfo@@AEBVbCString@@@Z", void* (*)(void* self, const void* name), true) \
    X(is_get_info, L"Game.dll", "?GetInfo@gCInfoSystem@@IEAAPEAVgCInfo@@AEBVbCString@@@Z", void* (*)(void* self, const void* name), true) \
    X(is_rebuild_owner_map, L"Game.dll", "?BuildMapInfosByOwners@gCInfoSystem@@IEAAXXZ", void (*)(void* self), true) \
    X(is_execute_info, L"Game.dll", "?ExecuteDialogInfo@gCInfoSystem@@QEAA_NPEAVgCInfo@@@Z", bool (*)(void* self, void* info), true) \
    X(is_has_infos, L"Game.dll", "?HasInfos@gCInfoSystem@@QEAA_NPEAVeCEntity@@@Z", bool (*)(void* self, void* entity), true) \
    X(is_is_running, L"Game.dll", "?IsRunning@gCInfoSystem@@QEAA_NXZ", bool (*)(void* self), true)              \
    /* generic property access by name (the engine's editor mechanism): works on any bCObjectBase */             \
    X(acc_ctor, L"SharedBase.dll", "??0bCAccessorProperty@@QEAA@PEBVbCObjectBase@@AEBVbCString@@@Z", void* (*)(void* self, const void* obj, const void* name), true) \
    X(acc_dtor, L"SharedBase.dll", "??1bCAccessorProperty@@QEAA@XZ", void (*)(void* self), true)                \
    X(acc_is_valid, L"SharedBase.dll", "?IsValid@bCAccessorProperty@@QEBA_NXZ", bool (*)(const void* self), true) \
    X(acc_set_string, L"SharedBase.dll", "?ConvertStringToValue@bCAccessorProperty@@QEAAXAEBVbCString@@@Z", void (*)(void* self, const void* value), true) \
    X(acc_get_string, L"SharedBase.dll", "?ConvertValueToString@bCAccessorProperty@@QEAA?AVbCString@@XZ", void* (*)(void* self, void* ret), true) \
    /* quest creation (gCQuest factory, manager) -- docs/RE.md §12 */                                            \
    X(quest_create_object, L"Game.dll", "?CreateObject@gCQuest@@SAPEAVbCObjectBase@@XZ", void* (*)(), true)     \
    X(qm_add_quest, L"Game.dll", "?AddQuest@gCQuestManager@@QEAA_NPEAVgCQuest@@AEBVbCString@@@Z", bool (*)(void* self, void* quest, const void* name), true) \
    X(qm_get_quest, L"Game.dll", "?GetQuest@gCQuestManager@@QEAAPEAVgCQuest@@AEBVbCString@@@Z", void* (*)(void* self, const void* name), true) \
    X(quest_access_topic, L"Game.dll", "?AccessLogTopic@gCQuest@@QEAAAEAVgCQuestLocString@@XZ", void* (*)(void* self), true) \
    X(mem_instance, L"SharedBase.dll", "?GetInstance@bCMemoryAdmin@@SAAEAV1@XZ", void* (*)(), true)             \
    X(mem_realloc, L"SharedBase.dll", "?Realloc@bCMemoryAdmin@@QEAAPEAXPEAXKPEBD11H@Z", void* (*)(void* self, void* ptr, unsigned long size, const char*, const char*, const char*, int), true) \
    X(locstring_ctor_key, L"Engine.dll", "??0eCLocString@@QEAA@AEBVbCString@@@Z", void* (*)(void* self, const void* key), true) \
    X(locstring_ctor_default, L"Engine.dll", "??0eCLocString@@QEAA@XZ", void* (*)(void* self), true)             \
    X(qm_quest_count, L"Game.dll", "?GetQuestCount@gCQuestManager@@QEBAHXZ", int (*)(const void* self), true)   \
    X(qm_quest_at, L"Game.dll", "?GetQuestAt@gCQuestManager@@QEAAPEAVgCQuest@@H@Z", void* (*)(void* self, int index), true) \
    X(qm_remove_quest, L"Game.dll", "?RemoveQuest@gCQuestManager@@QEAA_NAEBVbCString@@@Z", bool (*)(void* self, const void* name), true) \
    X(quest_get_log_text, L"Game.dll", "?GetLogText@gCQuest@@QEBAAEBV?$bTObjArray@USLogEntry@gCQuest@@@@XZ", const void* (*)(const void* self), true) \
    X(is_available_count, L"Game.dll", "?GetAvailableInfoCount@gCInfoSystem@@QEAAHXZ", int (*)(void* self), true) \
    X(is_get_available, L"Game.dll", "?GetAvailableInfo@gCInfoSystem@@QEAAPEAVgCInfo@@H@Z", void* (*)(void* self, int index), true) \
    X(info_access_owner, L"Game.dll", "?AccessOwner@gCInfo@@QEAAAEAVbCString@@XZ", void* (*)(void* self), true) \
    X(info_access_name, L"Game.dll", "?AccessName@gCInfo@@QEAAAEAVbCString@@XZ", void* (*)(void* self), true)   \
    X(info_access_parent, L"Game.dll", "?AccessParent@gCInfo@@QEAAAEAVbCString@@XZ", void* (*)(void* self), true) \
    X(info_access_quest, L"Game.dll", "?AccessQuest@gCInfo@@QEAAAEAVbCString@@XZ", void* (*)(void* self), true) \
    X(info_access_type, L"Game.dll", "?AccessType@gCInfo@@QEAAAEAV?$bTPropertyContainer@W4gEInfoType@@@@XZ", void* (*)(void* self), true) \
    X(info_access_condition, L"Game.dll", "?AccessConditionType@gCInfo@@QEAAAEAV?$bTPropertyContainer@W4gEInfoCondType@@@@XZ", void* (*)(void* self), true) \
    X(info_access_permanent, L"Game.dll", "?AccessPermanent@gCInfo@@QEAAAEA_NXZ", bool* (*)(void* self), true)   \
    X(info_access_given, L"Game.dll", "?AccessInfoGiven@gCInfo@@QEAAAEA_NXZ", bool* (*)(void* self), true)      \
    X(info_conditions_ok, L"Game.dll", "?AreConditionsFulfilled@gCInfo@@QEAA_NXZ", bool (*)(void* self), true)  \
    X(info_is_available, L"Game.dll", "?IsAvailable@gCInfo@@QEAA_NXZ", bool (*)(void* self), true)             \
    X(info_insert_command, L"Game.dll", "?InsertComSeqItem@gCInfo@@QEAAHVbCString@@@Z", int (*)(void* self, void* byValueName), true) \
    X(info_insert_command_type, L"Game.dll", "?InsertComSeqItem@gCInfo@@QEAAHW4gEInfoCommand@@@Z", int (*)(void* self, int type), true) \
    X(info_insert_command_ptr, L"Game.dll", "?InsertComSeqItem@gCInfo@@QEAAHPEAVgCInfoCommand@@@Z", int (*)(void* self, void* command), true) \
    X(runscript_create, L"Game.dll", "?CreateObject@gCInfoCommandRunScript@@SAPEAVbCObjectBase@@XZ", void* (*)(), true) \
    X(addlog_create, L"Game.dll", "?CreateObject@gCInfoCommandAddLogText@@SAPEAVbCObjectBase@@XZ", void* (*)(), true) \
    X(info_get_command, L"Game.dll", "?GetComSeqItem@gCInfo@@QEBAPEAVgCInfoCommand@@H@Z", void* (*)(const void* self, int index), true) \
    X(info_command_count, L"Game.dll", "?GetComSeqItemCount@gCInfo@@QEBAHXZ", int (*)(const void* self), true)  \
    X(say_access_text, L"Game.dll", "?AccessText@gCInfoCommandSay@@QEAAAEAVgCInfoLocString@@XZ", void* (*)(void* self), true) \
    X(say_set_speaker, L"Game.dll", "?SetSpeaker@gCInfoCommandSay@@QEAAXAEBVbCString@@@Z", void (*)(void* self, const void* s), true) \
    X(say_set_listener, L"Game.dll", "?SetListener@gCInfoCommandSay@@QEAAXAEBVbCString@@@Z", void (*)(void* self, const void* s), true) \
    X(runscript_access_script, L"Game.dll", "?AccessScript@gCInfoCommandRunScript@@QEAAAEAVeCScriptProxyScript@@XZ", void* (*)(void* self), true) \
    X(addlog_access_quest, L"Game.dll", "?AccessQuestName@gCInfoCommandAddLogText@@QEAAAEAVbCString@@XZ", void* (*)(void* self), true) \
    X(addlog_access_text, L"Game.dll", "?AccessText@gCInfoCommandAddLogText@@QEAAAEAVgCInfoLocString@@XZ", void* (*)(void* self), true) \
    X(locstr_set_key, L"FFC.dll", "?SetKey@CFXLocString@@QEAAXAEBVbCString@@@Z", void (*)(void* self, const void* key), true) \
    X(bstring_assign_cstr, L"SharedBase.dll", "??4bCString@@QEAAAEAV0@PEBD@Z", void* (*)(void* self, const char*), true) \
    X(script_proxy_ctor_name, L"Engine.dll", "??0eCScriptProxyScript@@QEAA@AEBVbCString@@@Z", void* (*)(void* self, const void* name), true) \
    X(script_proxy_dtor, L"Engine.dll", "??1eCScriptProxyScript@@QEAA@XZ", void (*)(void* self), true)          \
    X(script_proxy_assign, L"Engine.dll", "??4eCScriptProxyScript@@QEAAAEAV0@AEBV0@@Z", void* (*)(void* self, const void* other), true) \
    X(dialog_start, L"Script.dll", "?StartInfoSystem@InfoSystem@@SA_NAEBVEntity@@0@Z", bool (*)(const void* player, const void* npc), true) \
    X(ent_teleport_name, L"Script.dll", "?Teleport@Entity@@QEAA_NAEBVbCString@@@Z", bool (*)(void* self, const void* name), true) \
    X(ent_teleport_entity, L"Script.dll", "?Teleport@Entity@@QEAA_NAEBV1@@Z", bool (*)(void* self, const void* other), true) \
    /* ---- lifecycle events (hooked, optional: a missing symbol only disables that event) -- docs/RE.md §14 ---- */ \
    X(save_write, L"Game.dll", "?Save@gCSaveGameManager@@AEAA_NAEBVbCString@@AEBVbCUnicodeString@@K_NAEAVgCSaveGame@@@Z", bool (*)(void* self, const void* name, const void* title, unsigned long flags, bool overwrite, void* saveGame), false) \
    X(save_load, L"Game.dll", "?Load@gCSaveGameManager@@AEAA_NAEBVbCString@@PEAVIFFFileStream2@@@Z", bool (*)(void* self, const void* name, void* stream), false) \
    X(session_start, L"Game.dll", "?Start@gCSession@@QEAAXW4gESession_StartMode@@@Z", void (*)(void* self, int mode), false) \
    X(session_stop, L"Game.dll", "?Stop@gCSession@@QEAAXXZ", void (*)(void* self), false)                          \
    X(world_activate, L"Game.dll", "?Activate@gCWorld@@UEAA_NXZ", bool (*)(void* self), false)                      \
    X(quest_set_status, L"Game.dll", "?SetStatus@gCQuest@@QEAA_NAEBW4gEQuestStatus@@_N1@Z", bool (*)(void* self, const int* status, bool a, bool b), false) \
    X(qm_on_npc_killed, L"Game.dll", "?OnNPCKilled@gCQuestManager@@QEAA_NAEBVeCEntity@@@Z", bool (*)(void* self, void* entity), false) \
    X(qm_on_npc_defeated, L"Game.dll", "?OnNPCDefeated@gCQuestManager@@QEAA_NAEBVeCEntity@@@Z", bool (*)(void* self, void* entity), false) \
    X(is_on_info_finished, L"Game.dll", "?OnInfoFinished@gCInfoSystem@@QEAAXPEBVgCRuntimeInfo@@@Z", void (*)(void* self, const void* runtimeInfo), false) \
    X(inv_insert_stack, L"Game.dll", "?InsertStack@gCInventory_PS@@QEAAHPEAVeCEntity@@@Z", int (*)(void* self, void* item), false) \
    X(ps_get_entity, L"Engine.dll", "?GetEntity@eCEntityPropertySet@@UEAAPEAVeCEntity@@XZ", void* (*)(void* self), false) \
    X(vfs_instance, L"FFC.dll", "?GetInstance@CFXVirtualFileSystem@@SAAEAV1@XZ", void* (*)(), false) \
    X(vfs_virtual_to_physical, L"FFC.dll", "?VirtualToPhysicalPath@CFXVirtualFileSystem@@QEBA_NAEBVbCString@@AEAV2@@Z", bool (*)(const void* self, const void* virtualPath, void* physicalOut), false) \
    X(evfs_instance, L"Engine.dll", "?GetInstance@eCVirtualFileSystem@@SAAEAV1@XZ", void* (*)(), false) \
    X(evfs_exists_file, L"Engine.dll", "?ExistsFile@eCVirtualFileSystem@@QEAA_NAEBVbCString@@@Z", bool (*)(void* self, const void* path), false) \
    X(evfs_find_files, L"Engine.dll", "?FindFiles@eCVirtualFileSystem@@QEAA_NAEBVbCString@@0AEAV?$bTObjArray@VbCString@@@@@Z", bool (*)(void* self, const void* dir, const void* pattern, void* outArray), false) \
    X(evfs_find_ext, L"Engine.dll", "?FindFilesWithExt@eCVirtualFileSystem@@QEAA_NAEBVbCString@@0AEAV?$bTObjArray@VbCString@@@@@Z", bool (*)(void* self, const void* dir, const void* ext, void* outArray), false) \
    X(evfs_find_dirs, L"Engine.dll", "?FindDirectories@eCVirtualFileSystem@@QEAA_NAEBVbCString@@AEAV?$bTObjArray@VbCString@@@@@Z", bool (*)(void* self, const void* dir, void* outArray), false) \
    X(archive_instance, L"Engine.dll", "?GetInstance@eCArchiveManager@@SAAEAV1@XZ", void* (*)(), false) \
    X(archive_get_file, L"Engine.dll", "?GetFile@eCArchiveManager@@QEAA_NAEBVbCString@@AEAVeCArchiveFile@@@Z", bool (*)(void* self, const void* path, void* fileOut), false) \
    X(archive_file_ctor, L"Engine.dll", "??0eCArchiveFile@@QEAA@XZ", void* (*)(void* self), false) \
    X(archive_file_dtor, L"Engine.dll", "??1eCArchiveFile@@UEAA@XZ", void (*)(void* self), false) \
    X(archive_file_open, L"Engine.dll", "?Open@eCArchiveFile@@QEAA_NW4bEArchiveFileAccessMode@1@_N@Z", bool (*)(void* self, int mode, bool flag), false) \
    X(archive_file_is_open, L"Engine.dll", "?IsOpen@eCArchiveFile@@QEBA_NXZ", bool (*)(const void* self), false) \
    X(archive_file_read, L"Engine.dll", "?Read@eCArchiveFile@@UEAAKPEAXK@Z", unsigned long (*)(void* self, void* buffer, unsigned long count), false) \
    X(archive_file_close, L"Engine.dll", "?Close@eCArchiveFile@@QEAAXXZ", void (*)(void* self), false) \
    X(vfs_file_size, L"FFC.dll", "?GetFileSize@CFXVirtualFileSystem@@QEBA_KAEBVbCString@@@Z", unsigned long long (*)(const void* self, const void* path), false) \
    X(mem_free, L"SharedBase.dll", "?Free@bCMemoryAdmin@@QEAAXPEAX@Z", void (*)(void* self, void* ptr), false) \
    X(evfs_mount_dir, L"Engine.dll", "?MountDirectory@eCVirtualFileSystem@@IEAA_NAEBVbCString@@0@Z", bool (*)(void* self, const void* virtualPath, const void* physicalPath), false) \
    X(evfs_unmount, L"Engine.dll", "?Unmount@eCVirtualFileSystem@@IEAA_NAEBVbCString@@@Z", bool (*)(void* self, const void* virtualPath), false) \
    X(evfs_make_abs_virtual, L"Engine.dll", "?MakeAbsVirtualPath@eCVirtualFileSystem@@QEAA_NAEAVbCString@@@Z", bool (*)(void* self, void* path), false) \
    X(evfs_make_dir, L"Engine.dll", "?MakeDir@eCVirtualFileSystem@@QEAA_NAEBVbCString@@@Z", bool (*)(void* self, const void* path), false) \
    X(evfs_process_mountlist_file, L"Engine.dll", "?ProcessMountList@eCVirtualFileSystem@@IEAA_NAEBVbCString@@@Z", bool (*)(void* self, const void* file), false) \
    X(psitem_is_valid, L"Script.dll", "?IsValid@PSItem@@QEBA_NXZ", bool (*)(const void* self), false) \
    X(psitem_gold_value, L"Script.dll", "?GetGoldValue@PSItem@@QEBAHXZ", int (*)(const void* self), false) \
    X(psitem_amount, L"Script.dll", "?GetAmount@PSItem@@QEBAKXZ", unsigned long (*)(const void* self), false) \
    X(psitem_set_amount, L"Script.dll", "?SetAmount@PSItem@@QEAAXK@Z", void (*)(void* self, unsigned long amount), false) \
    X(psitem_category, L"Script.dll", "?GetCategory@PSItem@@QEBA?AW4gEItemCategory@@XZ", int (*)(const void* self), false) \
    X(psitem_use_type, L"Script.dll", "?GetUseType@PSItem@@QEBA?AW4gEItemUseType@@XZ", int (*)(const void* self), false) \
    X(psitem_icon, L"Script.dll", "?GetIconImage@PSItem@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), false) \
    X(psitem_is_mission_item, L"Script.dll", "?IsMissionItem@PSItem@@QEBA_NXZ", bool (*)(const void* self), false) \
    X(inv_find_stack, L"Script.dll", "?FindStackIndex@PSInventory@@QEBAHAEBVTemplate@@H_N@Z", int (*)(const void* self, const void* tpl, int startIndex, bool skipEquipped), false) \
    X(inv_delete_items, L"Script.dll", "?DeleteItems@PSInventory@@QEAA_NHH@Z", bool (*)(void* self, int stack, int count), false) \
    X(inv_stack_amount, L"Script.dll", "?GetAmount@PSInventory@@QEBAHH@Z", int (*)(const void* self, int stack), false) \
    X(element_base_name, L"Engine.dll", "?GetFileBaseName@eCProcessibleElement@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), false)

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
