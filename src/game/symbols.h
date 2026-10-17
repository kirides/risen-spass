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
    X(loctable_instance, L"FFC.dll", "?GetInstance@CFXLocTable@@SAAEAV1@XZ", void* (*)(), true)     X(table_has_key, L"FFC.dll", "?HasKey@CFXTableMap@@QEBA_NAEBVbCUnicodeString@@@Z", bool (*)(const void* self, const void* key), true) \
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
    X(ent_start_say_svm, L"Script.dll", "?StartSaySVM@Entity@@QEAAXAEAV1@VbCString@@_N2@Z", void (*)(void* self, void* listener, void* byValueSvmId, bool, bool), false) \
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
    /* the condition sequence of an info (all members must hold, after the ConditionType): docs/RE.md section 34 */ \
    X(info_insert_condition, L"Game.dll", "?InsertCondSeqItem@gCInfo@@QEAAHPEAVgCInfoCondition@@@Z", int (*)(void* self, void* condition), true) \
    X(info_get_condition, L"Game.dll", "?GetCondSeqItem@gCInfo@@QEBAPEAVgCInfoCondition@@H@Z", void* (*)(const void* self, int index), true) \
    X(info_condition_count, L"Game.dll", "?GetCondSeqItemCount@gCInfo@@QEBAHXZ", int (*)(const void* self), true) \
    X(info_remove_conditions, L"Game.dll", "?RemoveAllCondSeqItems@gCInfo@@QEAAXXZ", void (*)(void* self), true) \
    X(condskill_access_value, L"Game.dll", "?AccessSkillValue@gCInfoConditionSkillValue@@QEAAAEAVgCSkillValue@@XZ", void* (*)(void* self), true) \
    X(condteach_access_skill, L"Game.dll", "?AccessTeachSkill@gCInfoConditionCanTeachSkill@@QEAAAEAVgCSkillValue@@XZ", void* (*)(void* self), true) \
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
    X(vfs_lookup_virtual, L"FFC.dll", "?LookupCachedVirtualPath@CFXVirtualFileSystem@@QEBA_NAEBVbCString@@AEAV2@@Z", bool (*)(const void* self, const void* name, void* virtualOut), false) \
    X(vfs_lookup_physical, L"FFC.dll", "?LookupCachedPhysicalPath@CFXVirtualFileSystem@@QEBA_NAEBVbCString@@AEAV2@@Z", bool (*)(const void* self, const void* name, void* physicalOut), false) \
    X(res_get, L"Engine.dll", "?GetResource@eCResource2@@SAPEAV1@AEBVbCString@@@Z", void* (*)(const void* name), false) \
    X(res_state, L"Engine.dll", "?GetResourceState@eCResource2@@QEBA?AW4eEResourceState@@XZ", int (*)(const void* self), false) \
    X(res_file_path, L"Engine.dll", "?GetFilePath@eCResource2@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), false) \
    X(gui_module_device, L"Engine.dll", "?GetDevice@eCGuiModule2@@SAPEAVeCGuiDevice2@@XZ", void* (*)(), false) \
    X(gui_load_bitmap, L"Engine.dll", "?LoadBitmapA@eCGuiDevice2@@QEAAPEAVIFFGuiBitmap2@@PEBDPEAUHINSTANCE__@@@Z", void* (*)(void* self, const char* name, void* hinst), false) \
    X(gui_query_bitmap, L"Engine.dll", "?QueryBitmapA@eCGuiDevice2@@QEAAPEAVeCDeferredGuiBitmap2@@PEBD@Z", void* (*)(void* self, const char* name), false) \
    X(gui_deferred_bitmap, L"Engine.dll", "?GetBitmap@eCDeferredGuiBitmap2@@QEBAPEAVeCGuiBitmap2@@XZ", void* (*)(const void* self), false) \
    X(gui_load_image_list, L"Engine.dll", "?LoadImageListA@eCGuiDevice2@@QEAAPEAVIFFGuiImageList2@@PEBDPEAUHINSTANCE__@@@Z", void* (*)(void* self, const char* name, void* hinst), false) \
    X(gui_load_collection, L"Engine.dll", "?LoadImageCollectionA@eCGuiDevice2@@QEAAPEAVeCGuiImageCollection2@@PEBDPEAUHINSTANCE__@@@Z", void* (*)(void* self, const char* name, void* hinst), false) \
    X(gui_collection_save_xml, L"Engine.dll", "?SaveXml@eCGuiImageCollection2@@IEAA_NPEBD@Z", bool (*)(void* self, const char* path), false) \
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
    X(ent_get_property_set, L"Engine.dll", "?GetPropertySet@eCEntity@@QEAAPEAVeCEntityPropertySet@@AEBVbCString@@@Z", void* (*)(void* self, const void* className), false) \
    X(acc_ctor_object, L"SharedBase.dll", "??0bCAccessorProperty@@QEAA@PEBVbCObjectBase@@@Z", void* (*)(void* self, const void* obj), false) \
    X(acc_next, L"SharedBase.dll", "??EbCAccessorProperty@@QEAAAEAV0@XZ", void* (*)(void* self), false) \
    X(acc_name, L"SharedBase.dll", "?GetPropertyName@bCAccessorProperty@@QEBAAEBVbCString@@XZ", const void* (*)(const void* self), false) \
    X(acc_type, L"SharedBase.dll", "?GetPropertyType@bCAccessorProperty@@QEBAAEBVbCString@@XZ", const void* (*)(const void* self), false) \
    X(ctext_description, L"Game.dll", "?AccessText@gCInfoCommandDescription@@QEAAAEAVgCInfoLocString@@XZ", void* (*)(void* self), false) \
    X(ctext_saysvm, L"Game.dll", "?AccessText@gCInfoCommandSaySVM@@QEAAAEAVgCInfoLocString@@XZ", void* (*)(void* self), false) \
    X(gamevar_exists, L"Script.dll", "?ExistsGameVariable@InfoSystem@@SA_NAEBVbCString@@@Z", bool (*)(const void*), false) \
    X(gamevar_remove, L"Script.dll", "?RemoveGameVariable@InfoSystem@@SA_NAEBVbCString@@@Z", bool (*)(const void*), false) \
    X(gamevar_sys_set, L"Game.dll", "?SetGameVariable@gCInfoSystem@@QEAA_NAEBVbCString@@H@Z", bool (*)(void* self, const void* name, int value), false) \
    X(gamevar_sys_remove, L"Game.dll", "?RemoveGameVariable@gCInfoSystem@@QEAA_NAEBVbCString@@@Z", bool (*)(void* self, const void* name), false) \
    X(is_refresh_children, L"Game.dll", "?RefreshParentChildInfos@gCInfoSystem@@QEAAXXZ", void (*)(void* self), false) \
    X(element_base_name, L"Engine.dll", "?GetFileBaseName@eCProcessibleElement@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), false) \
    /* ---- entity templates at runtime (eCTemplateEntity, docs/RE.md section 22) ---------------------------- */ \
    X(tpl_proxy_get_entity, L"Engine.dll", "?GetEntity@eCTemplateEntityProxy@@QEAAPEAVeCTemplateEntity@@XZ", void* (*)(void* proxy), false) \
    X(tple_get_name, L"Engine.dll", "?GetName@eCTemplateEntity@@UEBAAEBVbCString@@XZ", const void* (*)(const void* self), false) \
    X(tple_is_content_loaded, L"Engine.dll", "?IsContentLoaded@eCTemplateEntity@@QEBA_NXZ", bool (*)(const void* self), false) \
    X(tple_read_all, L"Engine.dll", "?ReadAll@eCTemplateEntity@@UEAA?AW4bEResult@@XZ", int (*)(void* self), false) \
    X(tple_get_ref_template, L"Engine.dll", "?GetRefTemplate@eCTemplateEntity@@QEBAPEBV1@XZ", const void* (*)(const void* self), false) \
    X(ent_ps_count, L"Engine.dll", "?GetPropertySetCount@eCEntity@@QEBAHXZ", int (*)(const void* self), false) \
    X(ent_ps_at, L"Engine.dll", "?GetPropertySetAt@eCEntity@@QEAAPEAVeCEntityPropertySet@@H@Z", void* (*)(void* self, int index), false) \
    X(ps_class_name, L"Engine.dll", "?GetPropertySetClassName@eCEntityPropertySet@@UEBAAEBVbCString@@XZ", const void* (*)(const void* self), false) \
    X(module_admin_instance, L"Engine.dll", "?GetInstance@eCModuleAdmin@@SAAEAV1@XZ", void* (*)(), false) \
    X(module_find_by_class, L"Engine.dll", "?FindModule@eCModuleAdmin@@QEBAPEAVeCEngineComponentBase@@AEBVbCString@@@Z", void* (*)(const void* self, const void* className), false) \
    X(scene_ctx_count, L"Engine.dll", "?GetDynamicEntityContextCount@eCSceneAdmin@@QEBAHXZ", int (*)(const void* self), false) \
    X(inv_template_item, L"Script.dll", "?GetTemplateItem@PSInventory@@QEBA?AVEntity@@H@Z", void* (*)(const void* self, void* ret, int stack), false) \
    /* ---- spells: the cast accounting (PSCastInfo), the stack in an equip slot, the shooter of a projectile (docs/RE.md section 29) ---- */ \
    X(inv_find_slot, L"Script.dll", "?FindStackIndex@PSInventory@@QEBAHW4gEEquipSlot@@@Z", int (*)(const void* self, int slot), false) \
    X(proj_get_shooter, L"Script.dll", "?GetShooter@PSProjectile@@QEBA?AVEntity@@XZ", void* (*)(const void* self, void* ret), false) \
    X(cast_can_cast, L"Script.dll", "?CanCast@PSCastInfo@@QEBA_NAEAVEntity@@@Z", bool (*)(const void* self, const void* caster), false) \
    X(cast_consume, L"Script.dll", "?Consume@PSCastInfo@@QEAA_NAEAVEntity@@H@Z", bool (*)(void* self, const void* caster, int stack), false) \
    X(tple_patch_entity, L"Engine.dll", "?PatchEntity@eCTemplateEntity@@QEBA?AW4bEResult@@PEAVeCEntity@@_N1@Z", int (*)(const void* self, void* entity, bool replaceAll, bool arg), false) \
    /* ---- the Discord rich-presence thread reads the live world; the portal room lookup it ends in (engine_guards.cpp, docs/RE.md section 43) ---- */ \
    X(session_hud2_location, L"Game.dll", "?GetHud2Location@gCSession@@QEAA?BVbCString@@XZ", void* (*)(void* self, void* ret), false) \
    X(portal_find_room, L"Engine.dll", "?FindPortalRoom@eCPortalAdmin@@QEAAPEAVeCPortalRoom_PS@@AEBVbCVector@@@Z", void* (*)(void* self, const void* position), false) \
    /* ---- item skill arrays: gCSkillValue (requirements) / gCModifySkill (modifiers), docs/RE.md section 23 ---- */ \
    X(skillvalue_ctor, L"Game.dll", "??0gCSkillValue@@QEAA@XZ", void* (*)(void* self), false) \
    X(skillvalue_dtor, L"Game.dll", "??1gCSkillValue@@UEAA@XZ", void (*)(void* self), false) \
    X(modifyskill_ctor, L"Game.dll", "??0gCModifySkill@@QEAA@XZ", void* (*)(void* self), false) \
    X(modifyskill_dtor, L"Game.dll", "??1gCModifySkill@@UEAA@XZ", void (*)(void* self), false) \
    X(item_set_required_skills, L"Game.dll", "?SetRequiredSkills@gCItem_PS@@QEAAXAEBV?$bTObjArray@VgCSkillValue@@@@@Z", void (*)(void* itemPs, const void* array), false) \
    X(item_set_modify_skills, L"Game.dll", "?SetModifySkills@gCItem_PS@@QEAAXAEBV?$bTObjArray@VgCModifySkill@@@@@Z", void (*)(void* itemPs, const void* array), false) \
    X(skills_has_required, L"Game.dll", "?HasRequiredSkills@gCSkills_PS@@QEBA_NAEBV?$bTObjArray@VgCSkillValue@@@@PEAV?$bTObjArray@UgSMissingSkill@@@@@Z", bool (*)(const void* skillsPs, const void* required, void* missing), false) \
    X(inv_consume_items, L"Script.dll", "?ConsumeItems@PSInventory@@QEAA_NHH@Z", bool (*)(void* self, int stack, int count), false) \
    X(sk_get_total, L"Script.dll", "?GetTotal@PSSkills@@QEBAHW4gESkill@@@Z", int (*)(const void* holder, int skill), false) \
    X(sk_get_base, L"Script.dll", "?GetBase@PSSkills@@QEBAHW4gESkill@@@Z", int (*)(const void* holder, int skill), false) \
    /* ---- NPC skills: gCSkills_PS storage and combat species (docs/RE.md section 24) ---- */ \
    X(skills_set_base, L"Game.dll", "?SetSkillBaseValue@gCSkills_PS@@QEAAXW4gESkill@@J@Z", void (*)(void* skillsPs, int skill, std::int32_t value), false) \
    X(skills_get_value, L"Game.dll", "?GetSkillValue@gCSkills_PS@@QEBAJW4gESkill@@@Z", std::int32_t (*)(const void* skillsPs, int skill), false) \
    X(skills_get_max, L"Game.dll", "?GetSkillMaxValue@gCSkills_PS@@QEBAJW4gESkill@@@Z", std::int32_t (*)(const void* skillsPs, int skill), false) \
    X(skills_infos, L"Game.dll", "?ms_arrSkillInfos@gCSkills_PS@@2PAUgSSkillInfo@@A", unsigned char*, false) \
    X(combat_admin_instance, L"Game.dll", "?GetInstance@gCCombatAdmin@@SAPEAV1@XZ", void* (*)(), false) \
    X(combat_get_species, L"Game.dll", "?GetSpecies@gCCombatAdmin@@QEBAPEAVgCCombatSpecies@@AEBVbCString@@@Z", void* (*)(const void* self, const void* name), false) \
    X(combat_current_style, L"Game.dll", "?GetCurrentStyle@gCCombatSystem_PS@@QEBAPEAVgCCombatStyle@@XZ", void* (*)(const void* combatPs), false) \
    X(combat_get_style, L"Game.dll", "?GetStyle@gCCombatSystem_PS@@QEBAPEAVgCCombatStyle@@W4gEItemUseType@@0@Z", void* (*)(const void* combatPs, int left, int right), false) \
    X(qm_get_chapter, L"Game.dll", "?GetCurrentChapter@gCQuestManager@@QEBAKXZ", std::uint32_t (*)(const void* self), false) \
    X(qm_set_chapter, L"Game.dll", "?SetCurrentChapter@gCQuestManager@@QEAAXK_N@Z", void (*)(void* self, std::uint32_t chapter, bool notify), false) \
    /* ---- new templates at runtime: clone + rename + id (docs/RE.md section 25) ---- */ \
    X(tple_create_copy, L"Engine.dll", "?CreateEntityCopy@eCTemplateEntity@@SAPEAV1@PEAV1@@Z", void* (*)(void* source), false) \
    X(tple_set_name, L"Engine.dll", "?SetName@eCTemplateEntity@@UEAAXAEBVbCString@@@Z", void (*)(void* self, const void* name), false) \
    X(tple_set_id, L"Engine.dll", "?SetID@eCTemplateEntity@@UEAAXAEBVbCPropertyID@@@Z", void (*)(void* self, const void* id), false) \
    X(tple_get_id, L"Engine.dll", "?GetID@eCTemplateEntity@@UEBAAEBVbCPropertyID@@XZ", const void* (*)(const void* self), false) \
    X(scene_get_entity_by_id, L"Engine.dll", "?GetEntity@eCSceneAdmin@@QEAAPEAVeCNode@@AEBVbCPropertyID@@W4eEEntityType@@@Z", void* (*)(void* self, const void* id, int type), false) \
    X(interaction_focus_name, L"Game.dll", "?GetPlainFocusName@gCInteraction_PS@@QEBA?AVbCUnicodeString@@XZ", void* (*)(const void* self, void* ret), false) \
    X(item_plain_description, L"Game.dll", "?GetPlainDescription@gCItem_PS@@QEBA?AVbCUnicodeString@@XZ", void* (*)(const void* self, void* ret), false) \
    X(mesh_ps_dependencies, L"Engine.dll", "?GetDependencies@eCMesh_PS@@UEAAXAEAV?$bTObjArray@VbCString@@@@0AEAV?$bTObjArray@VeCTemplateEntityProxy@@@@@Z", void (*)(void* ps, void* files, void* files2, void* proxies), false) \
    X(anim_ps_dependencies, L"Engine.dll", "?GetDependencies@eCAnimation_PS@@UEAAXAEAV?$bTObjArray@VbCString@@@@0AEAV?$bTObjArray@VeCTemplateEntityProxy@@@@@Z", void (*)(void* ps, void* files, void* files2, void* proxies), false) \
    X(bodypart_ps_dependencies, L"Engine.dll", "?GetDependencies@eCBodyPart_PS@@UEAAXAEAV?$bTObjArray@VbCString@@@@0AEAV?$bTObjArray@VeCTemplateEntityProxy@@@@@Z", void (*)(void* ps, void* files, void* files2, void* proxies), false) \
    X(nav_post_copy, L"Game.dll", "?OnPostCopy@gCNavigation_PS@@UEAAXAEBVeCEntityPropertySet@@@Z", void (*)(void* self, const void* source), false) \
    X(node_attach_child, L"Engine.dll", "?AttachChild@eCNode@@QEAA_NPEAV1@@Z", bool (*)(void* self, void* child), false) \
    X(scene_unregister_tpl_name, L"Engine.dll", "?UnregisterNameInfo@eCSceneAdmin@@QEAA_NAEAVeCTemplateEntity@@@Z", bool (*)(void* self, void* tpl), false) \
    X(scene_ctx_at, L"Engine.dll", "?GetDynamicEntityContext@eCSceneAdmin@@QEBAPEAVeCEntityDynamicContext@@H@Z", void* (*)(const void* self, int index), false) \
    /* ---- reliable gameplay events (docs/RE.md section 27): gCInventory_PS operations, listener callbacks, dialogue item commands ---- */ \
    X(gi_create_items_id, L"Game.dll", "?CreateItems@gCInventory_PS@@QEAAHAEBVbCPropertyID@@KW4gEStackType@@@Z", int (*)(void* self, const void* id, unsigned long count, int stackType), false) \
    X(gi_create_items_tpl, L"Game.dll", "?CreateItems@gCInventory_PS@@QEAAHPEBVeCTemplateEntity@@KW4gEStackType@@@Z", int (*)(void* self, const void* tpl, unsigned long count, int stackType), false) \
    X(gi_assure_items, L"Game.dll", "?AssureItems@gCInventory_PS@@QEAAHAEBVbCPropertyID@@KW4gEStackType@@@Z", int (*)(void* self, const void* id, unsigned long count, int stackType), false) \
    X(gi_delete_items, L"Game.dll", "?DeleteItems@gCInventory_PS@@QEAA_NHK@Z", bool (*)(void* self, int stack, unsigned long count), false) \
    X(gi_consume_items, L"Game.dll", "?ConsumeItems@gCInventory_PS@@QEAA_NHK@Z", bool (*)(void* self, int stack, unsigned long count), false) \
    X(gi_delete_stack, L"Game.dll", "?DeleteStack@gCInventory_PS@@QEAA_NH@Z", bool (*)(void* self, int stack), false) \
    X(gi_set_amount, L"Game.dll", "?SetAmount@gCInventory_PS@@QEAAXHAEBH_N@Z", void (*)(void* self, int stack, const int* amount, bool notify), false) \
    X(gi_set_money, L"Game.dll", "?SetMoneyAmount@gCInventory_PS@@QEAAXH@Z", void (*)(void* self, int amount), false) \
    X(gi_transfer_items, L"Game.dll", "?TransferItemsTo@gCInventory_PS@@QEAA_NPEAV1@HHW4gEStackType@@@Z", bool (*)(void* self, void* target, int stack, int count, int stackType), false) \
    X(gi_transfer_stack, L"Game.dll", "?TransferStackTo@gCInventory_PS@@QEAA_NPEAV1@HW4gEStackType@@@Z", bool (*)(void* self, void* target, int stack, int stackType), false) \
    X(gi_transfer_all, L"Game.dll", "?TransferAllTo@gCInventory_PS@@QEAA_NPEAV1@W4gEStackType@@@Z", bool (*)(void* self, void* target, int stackType), false) \
    X(gi_loot_selections, L"Game.dll", "?LootSelections@gCInventory_PS@@QEAA_NPEAV1@@Z", bool (*)(void* self, void* from), false) \
    X(gi_trade_selections, L"Game.dll", "?TradeSelections@gCInventory_PS@@QEAA_NPEAV1@@Z", bool (*)(void* self, void* with), false) \
    X(gi_drop_items, L"Game.dll", "?DropItemsFromStack@gCInventory_PS@@QEAAPEAVeCEntity@@HHPEBVbCMatrix@@@Z", void* (*)(void* self, int stack, int count, const float* pose), false) \
    X(gi_drop_stack, L"Game.dll", "?DropStack@gCInventory_PS@@QEAAPEAVeCEntity@@HPEBVbCMatrix@@@Z", void* (*)(void* self, int stack, const float* pose), false) \
    X(gi_equip_to_slot, L"Game.dll", "?EquipStackToSlot@gCInventory_PS@@QEAA_NHW4gEEquipSlot@@@Z", bool (*)(void* self, int stack, int slot), false) \
    X(gi_unequip, L"Game.dll", "?UnEquipStack@gCInventory_PS@@QEAA_NH_N@Z", bool (*)(void* self, int stack, bool unlink), false) \
    X(gi_on_stack_create, L"Game.dll", "?OnStackCreate@gCInventory_PS@@UEAAXHPEBVgCInventoryStack@@@Z", void (*)(void* listener, int index, const void* stack), false) \
    X(gi_on_stack_delete, L"Game.dll", "?OnStackDelete@gCInventory_PS@@UEAAXHPEBVgCInventoryStack@@@Z", void (*)(void* listener, int index, const void* stack), false) \
    X(gi_merge_stacks, L"Game.dll", "?MergeStacks@gCInventory_PS@@QEAAXXZ", void (*)(void* self), false) \
    X(gi_split_stack, L"Game.dll", "?SplitStack@gCInventory_PS@@QEAA_NHKAEAH@Z", bool (*)(void* self, int stack, unsigned long count, int* newStack), false) \
    X(infocmd_create_item, L"Game.dll", "?Execute@gCInfoCommandCreateItem@@UEAA_NAEAVgCRuntimeInfo@@_N@Z", bool (*)(void* self, void* runtimeInfo, bool flag), false) \
    X(infocmd_destroy_item, L"Game.dll", "?Execute@gCInfoCommandDestroyItem@@UEAA_NAEAVgCRuntimeInfo@@_N@Z", bool (*)(void* self, void* runtimeInfo, bool flag), false) \
    X(infocmd_give, L"Game.dll", "?Execute@gCInfoCommandGive@@UEAA_NAEAVgCRuntimeInfo@@_N@Z", bool (*)(void* self, void* runtimeInfo, bool flag), false) \
    X(eproxy_get_entity, L"Engine.dll", "?GetEntity@eCEntityProxy@@QEAAPEAVeCEntity@@XZ", void* (*)(void* proxy), false) \
    X(eent_kill, L"Engine.dll", "?Kill@eCEntity@@QEAAXXZ", void (*)(void* self), false) \
    X(eent_set_save_relevant, L"Engine.dll", "?SetSaveGameRelevant@eCEntity@@QEAAX_N@Z", void (*)(void* self, bool relevant), false) \
    X(eent_is_save_relevant, L"Engine.dll", "?IsSaveGameRelevant@eCEntity@@QEBA_NXZ", bool (*)(const void* self), false) \
    X(gdmg_do_damage, L"Game.dll", "?DoDamage@gCDamage_PS@@QEAA_NPEAVeCEntity@@@Z", bool (*)(void* self, void* victim), false) \
    X(sg_kill, L"Script_Game.dll", "?Kill@@YA_JPEAVgCScriptProcessingUnit@@PEAX1_J@Z", std::int64_t (*)(void* unit, void* self, void* other, std::int64_t arg), false) \
    X(sg_cheat_kill, L"Script_Game.dll", "?OnCheatKill@@YA_JPEAVgCScriptProcessingUnit@@PEAX1_J@Z", std::int64_t (*)(void* unit, void* self, void* other, std::int64_t arg), false) \
    X(sg_defeat, L"Script_Game.dll", "?Defeat@@YA_JPEAVgCScriptProcessingUnit@@PEAX1_J@Z", std::int64_t (*)(void* unit, void* self, void* other, std::int64_t arg), false) \
    X(ent_spawn_near, L"Script.dll", "?Spawn@Entity@@SA?AV1@VTemplate@@V1@_NW4gEDirection@@@Z", void* (*)(void* ret, void* byValueTemplate, void* byValueEntity, bool temporary, int direction), false) \
    X(psinv_transfer_amount, L"Script.dll", "?TransferAmountTo@PSInventory@@QEAA_NHHAEAVEntity@@@Z", bool (*)(void* self, int stack, int count, void* target), false) \
    X(psinv_loot, L"Script.dll", "?LootSelections@PSInventory@@QEAA_NAEAVEntity@@@Z", bool (*)(void* self, void* from), false) \
    X(psinv_trade, L"Script.dll", "?TradeSelections@PSInventory@@QEAA_NAEAVEntity@@@Z", bool (*)(void* self, void* with), false) \
    X(psinv_set_max_selection, L"Script.dll", "?SetMaxSelection@PSInventory@@QEAAXXZ", void (*)(void* self), false) \
    X(psinv_insert_stack, L"Script.dll", "?InsertStack@PSInventory@@QEAAHAEBVEntity@@@Z", int (*)(void* self, const void* item), false) \
    X(psinv_set_selection, L"Script.dll", "?SetSelection@PSInventory@@QEAAXHAEBH@Z", void (*)(void* self, int stack, const int* amount), false) \
    X(psinv_unequip_stack, L"Script.dll", "?UnEquipStack@PSInventory@@QEAA_NH@Z", bool (*)(void* self, int stack), false) \
    X(psinv_set_max_select_limit, L"Script.dll", "?SetMaxSelectLimit@PSInventory@@QEAAXXZ", void (*)(void* self), false) \
    X(psinv_drop_stack, L"Script.dll", "?DropStack@PSInventory@@QEAA?AVEntity@@HAEBVbCMatrix@@@Z", void* (*)(void* self, void* ret, int stack, const float* pose), false) \
    /* ---- crafting recipes: gCRecipe_PS arrays and the engine's craft functions (docs/RE.md section 30) ---- */ \
    X(craft_ingredient_ctor, L"Game.dll", "??0gCCraftIngredient@@QEAA@XZ", void* (*)(void* self), false) \
    X(craft_ingredient_dtor, L"Game.dll", "??1gCCraftIngredient@@UEAA@XZ", void (*)(void* self), false) \
    X(recipe_set_ingredients, L"Game.dll", "?SetCraftIngredients@gCRecipe_PS@@QEAAXAEBV?$bTObjArray@VgCCraftIngredient@@@@@Z", void (*)(void* recipePs, const void* array), false) \
    X(recipe_set_required_skills, L"Game.dll", "?SetRequiredSkills@gCRecipe_PS@@QEAAXAEBV?$bTObjArray@VgCSkillValue@@@@@Z", void (*)(void* recipePs, const void* array), false) \
    X(psinv_can_synthesize, L"Script.dll", "?CanSynthesize@PSInventory@@QEBAHAEBVEntity@@@Z", int (*)(const void* self, const void* recipe), false) \
    X(psinv_synthesize, L"Script.dll", "?Synthesize@PSInventory@@QEAAHAEBVEntity@@H@Z", int (*)(void* self, const void* recipe, int count), false) \
    X(psinter_begin, L"Script.dll", "?BeginInteraction@PSInteraction@@QEAA_NVEntity@@@Z", bool (*)(void* self, void* byValueUser), false) \
    X(psinter_can, L"Script.dll", "?CanInteractWith@PSInteraction@@QEAA_NVEntity@@@Z", bool (*)(void* self, void* byValueUser), false) \
    X(psinter_end, L"Script.dll", "?EndInteraction@PSInteraction@@QEAA_NXZ", bool (*)(void* self), false) \
    /* ---- custom voice lines (docs/RE.md section 28) ---- */ \
    X(ent_get_sample_title, L"Script.dll", "?GetSampleTitle@Entity@@QEBA?AVbCString@@V2@@Z", void* (*)(const void* self, void* ret, void* byValueKey), false) \
    X(loc_get_voice_language, L"Engine.dll", "?GetVoiceLanguage@eCLocAdmin2@@QEBA?AVbCString@@XZ", void* (*)(const void* self, void* ret), false) \
    X(rout_start_output, L"Game.dll", "?StartOutput@gCScriptRoutine_PS@@QEAA_NAEBVbCString@@W4gEOutputType@@@Z", bool (*)(void* self, const void* name, int type), false) \
    X(rout_stop_output, L"Game.dll", "?StopOutput@gCScriptRoutine_PS@@QEAA_NXZ", bool (*)(void* self), false) \
    X(rout_is_output_playing, L"Game.dll", "?IsOutputPlaying@gCScriptRoutine_PS@@QEBA_NXZ", bool (*)(const void* self), false) \
    /* ---- quick loot building blocks: focus, interaction lists, input, HUD log (docs/RE.md section 31) ---- */ \
    X(focus_instance, L"Game.dll", "?GetInstance@gCFocusModule2@@SAAEAV1@XZ", void* (*)(), false) \
    X(focus_get_entity, L"Game.dll", "?GetFocusEntity@gCFocusModule2@@QEBAPEAVeCEntity@@XZ", void* (*)(const void* self), false) \
    X(focus_set_entity, L"Game.dll", "?SetFocusEntity@gCFocusModule2@@QEAAXPEAVeCEntity@@@Z", void (*)(void* self, void* entity), false) \
    X(focus_enable_auto, L"Game.dll", "?EnableAutoUpdate@gCFocusModule2@@QEAAX_N@Z", void (*)(void* self, bool on), false) \
    X(focus_auto_enabled, L"Game.dll", "?IsAutoUpdateEnabled@gCFocusModule2@@QEBA_NXZ", bool (*)(const void* self), false) \
    X(inter_ps_count, L"Game.dll", "?GetInteractionCount@gCInteraction_PS@@QEBAHXZ", int (*)(const void* ps), false) \
    X(inter_ps_at, L"Game.dll", "?GetInteractionAt@gCInteraction_PS@@QEBAPEAVgCInteraction@@H@Z", void* (*)(const void* ps, int index), false) \
    X(inter_ps_target, L"Game.dll", "?GetCurrentTarget@gCInteraction_PS@@QEBAPEAVeCEntity@@XZ", void* (*)(const void* ps), false) \
    X(inter_ps_user, L"Game.dll", "?GetCurrentUser@gCInteraction_PS@@QEBAPEAVeCEntity@@XZ", void* (*)(const void* ps), false) \
    X(action_get_key, L"Game.dll", "?GetActionKey@gCActionAdmin@@SA?AW4gEActionKey@@AEBVbCString@@@Z", int (*)(const void* name), false) \
    X(action_key_info, L"Game.dll", "?GetActionKeyInfo@gCActionAdmin@@SAAEBUgSActionKeyInfo@@W4gEActionKey@@@Z", const void* (*)(int key), false) \
    X(action_for_scan, L"Game.dll", "?GetActionKey@gCActionAdmin@@SA?AW4gEActionKey@@W4eEScanCode@@@Z", int (*)(int scan), false) \
    X(scan_description, L"Game.dll", "?GetScanCodeDescription@gCActionAdmin@@SA?AVbCUnicodeString@@W4eEScanCode@@@Z", void* (*)(void* ret, int scan), false) \
    X(action_key_pressed, L"Game.dll", "?KeyPressed@gCActionAdmin@@UEAAXK@Z", void (*)(void* admin, unsigned long key), false) \
    X(action_key_released, L"Game.dll", "?KeyReleased@gCActionAdmin@@UEAAXK@Z", void (*)(void* admin, unsigned long key), false) \
    X(input_is_pressed, L"Script.dll", "?IsKeyPressed@Input@@QEBA_NW4gEActionKey@@@Z", bool (*)(const void* input, int key), false) \
    X(input_was_pressed, L"Script.dll", "?WasKeyPressedThisFrame@Input@@QEBA_NW4gEActionKey@@@Z", bool (*)(const void* input, int key), false) \
    X(input_press_duration, L"Script.dll", "?GetKeyPressDuration@Input@@QEBAMW4gEActionKey@@@Z", float (*)(const void* input, int key), false) \
    X(input_current_event, L"Script.dll", "?GetCurrentActionEvent@Input@@QEBAAEBUgSAction@@XZ", const void* (*)(const void* input), false) \
    X(input_object, L"Script.dll", "input", unsigned char*, false) \
    X(gui2_object, L"Script.dll", "gui2", unsigned char*, false) \
    X(snd_gui_play, L"Engine.dll", "?PlaySoundA@eCGuiWindow2@@QEBA_NAEBVbCString@@@Z", bool (*)(const void* self, const void* name), false) \
    X(snd_play2d_named, L"Engine.dll", "?Play2D@eCSoundResource2@@SA_NAEBVbCString@@W4eEAudioChannelGroup@@MPEAVeCAudioChannel@@@Z", bool (*)(const void* name, int group, float volume, void* channel), false) \
    X(snd_play3d_named, L"Engine.dll", "?Play3D@eCSoundResource2@@SA_NAEBVbCString@@AEBUSPlay3D@1@@Z", bool (*)(const void* name, const void* play), false) \
    X(snd_get_sound, L"Engine.dll", "?GetSound@eCResource2@@SAPEAVeCSoundResource2@@AEBVbCString@@@Z", void* (*)(const void* name), false) \
    X(fx_ps_start, L"Script.dll", "?StartEffect@PSEffect@@QEAA_NAEBVbCString@@_N@Z", bool (*)(void* self, const void* name, bool flag), false) \
    X(fx_start_e1, L"Script.dll", "?StartEffect@EffectSystem@@SA?AVEffect@@AEBVbCString@@AEBVEntity@@1@Z", void* (*)(void* ret, const void* name, const void* a, const void* b), false) \
    X(fx_start_e, L"Script.dll", "?StartEffect@EffectSystem@@SA?AVEffect@@AEBVbCString@@AEBVEntity@@@Z", void* (*)(void* ret, const void* name, const void* a, const void* b), false) \
    X(fx_start_m, L"Script.dll", "?StartEffect@EffectSystem@@SA?AVEffect@@AEBVbCString@@AEBVbCMatrix@@@Z", void* (*)(void* ret, const void* name, const void* a, const void* b), false) \
    X(fx_start_me, L"Script.dll", "?StartEffect@EffectSystem@@SA?AVEffect@@AEBVbCString@@AEBVbCMatrix@@AEBVEntity@@@Z", void* (*)(void* ret, const void* name, const void* a, const void* b), false) \
    X(fx_start_v, L"Script.dll", "?StartEffect@EffectSystem@@SA?AVEffect@@AEBVbCString@@AEBVbCVector@@@Z", void* (*)(void* ret, const void* name, const void* a, const void* b), false) \
    X(fx_start_ve, L"Script.dll", "?StartEffect@EffectSystem@@SA?AVEffect@@AEBVbCString@@AEBVbCVector@@AEBVEntity@@@Z", void* (*)(void* ret, const void* name, const void* a, const void* b), false) \
    X(tutorial_object, L"Script.dll", "tutorial", void*, false) \
    X(tut_stop_event, L"Script.dll", "?StopEvent@Tutorial@@QEAAXVbCString@@_N@Z", void (*)(void* self, void* byValueName, bool completed), false) \
    X(tut_is_completed, L"Script.dll", "?IsCompleted@Tutorial@@QEBA_NVbCString@@@Z", bool (*)(const void* self, void* byValueName), false) \
    X(gui_print_log, L"Script.dll", "?PrintGameLog@GUI2@@QEAAXAEBVbCUnicodeString@@W4gELogMessageType@@_NPEAD@Z", void (*)(void* gui, const void* text, int type, bool flag, char* extra), false) \
    X(gui_print_item_log, L"Script.dll", "?PrintItemLog@GUI2@@QEAAXVEntity@@H@Z", void (*)(void* gui, void* byValueItem, int amount), false) \
    X(gui_is_browsing, L"Script.dll", "?IsBrowsing@GUI2@@QEBA_NXZ", bool (*)(const void* gui), false) \
    X(gui_is_browsing_loot, L"Script.dll", "?IsBrowsingLoot@GUI2@@QEAA_NXZ", bool (*)(void* gui), false) \
    X(gui_is_browsing_game, L"Script.dll", "?IsBrowsingGame@GUI2@@QEBA_NXZ", bool (*)(const void* gui), false) \
    X(gui_is_visible, L"Script.dll", "?IsVisible@GUI2@@QEBA_NW4gEHudPage@@@Z", bool (*)(const void* gui, int page), false) \
    X(gui_get_mode, L"Script.dll", "?GetMode@GUI2@@QEBA?AW4gEHudMode@@XZ", int (*)(const void* gui), false) \
    /* ---- GUI windows (M16, docs/RE.md section 42): the page list of GUI2 and the eCGuiWindow2 tree under every page ---- */ \
    X(gui_find_page, L"Script.dll", "?FindPage@GUI2@@QEAAPEAVgCHudPage2@@W4gEHudPage@@@Z", void* (*)(void* gui, int page), false) \
    X(gui_open_page, L"Script.dll", "?Open@GUI2@@QEAA_NW4gEHudPage@@@Z", bool (*)(void* gui, int page), false) \
    X(gui_close_page, L"Script.dll", "?Close@GUI2@@QEAA_NW4gEHudPage@@@Z", bool (*)(void* gui, int page), false) \
    X(gui_close_menu, L"Script.dll", "?CloseMenu@GUI2@@QEAAXXZ", void (*)(void* gui), false) \
    X(gui_page_names, L"Game.dll", "?g_strStringIDPage@@3PAVbCString@@A", unsigned char*, false) \
    X(gui_module_instance, L"Engine.dll", "?GetInstance@eCGuiModule2@@SAAEAV1@XZ", void* (*)(), false) \
    X(gui_handle_message, L"Engine.dll", "?OnHandleMessage@eCGuiModule2@@QEAA_NIIJ@Z", bool (*)(void* self, unsigned msg, unsigned wparam, long lparam), false) \
    X(win_abs_position, L"Engine.dll", "?GetWindowAbsPosition@eCGuiWindow2@@QEAA?AVbCPoint@@XZ", void* (*)(void* w, int* ret), false) \
    X(app_handle_message, L"Engine.dll", "?OnHandleMessage@eCApplication@@MEAA_NI_K_JPEA_J@Z", bool (*)(void* self, unsigned msg, unsigned long long wparam, long long lparam, long long* result), false) \
    X(gui_show_page, L"Script.dll", "?Show@GUI2@@QEAA_NW4gEHudPage@@_N@Z", bool (*)(void* gui, int page, bool show), false) \
    X(app_do_render, L"Engine.dll", "?DoRender@eCApplication@@IEAAX_N@Z", void (*)(void* self, bool flag), false) \
    X(win_first_child, L"Engine.dll", "?GetFirstChild@eCGuiWindow2@@QEBAPEAV1@XZ", void* (*)(const void* w), false) \
    X(win_next_sibling, L"Engine.dll", "?GetNextSibling@eCGuiWindow2@@QEBAPEAV1@XZ", void* (*)(const void* w), false) \
    X(win_parent, L"Engine.dll", "?GetParent@eCGuiWindow2@@QEBAPEAV1@XZ", void* (*)(const void* w), false) \
    X(win_get_name, L"Engine.dll", "?GetName@eCGuiWindow2@@QEBAAEBVbCString@@XZ", const void* (*)(const void* w), false) \
    X(win_set_name, L"Engine.dll", "?SetName@eCGuiWindow2@@QEAAXAEBVbCString@@@Z", void (*)(void* w, const void* name), false) \
    X(win_get_id, L"Engine.dll", "?GetID@eCGuiWindow2@@QEBAGXZ", unsigned short (*)(const void* w), false) \
    X(win_set_id, L"Engine.dll", "?SetID@eCGuiWindow2@@QEAAXAEBG@Z", void (*)(void* w, const unsigned short* id), false) \
    X(win_get_text, L"Engine.dll", "?GetText@eCGuiWindow2@@QEBA?AVbCUnicodeString@@XZ", void* (*)(const void* w, void* ret), false) \
    X(win_set_text, L"Engine.dll", "?SetText@eCGuiWindow2@@QEAAXAEBVbCUnicodeString@@@Z", void (*)(void* w, const void* text), false) \
    X(win_get_position, L"Engine.dll", "?GetPosition@eCGuiWindow2@@QEBA?AVbCPoint@@XZ", void* (*)(const void* w, int* ret), false) \
    X(win_set_position, L"Engine.dll", "?SetPosition@eCGuiWindow2@@QEAAXAEBVbCPoint@@@Z", void (*)(void* w, const int* p), false) \
    X(win_get_size, L"Engine.dll", "?GetSize@eCGuiWindow2@@QEBA?AVbCPoint@@XZ", void* (*)(const void* w, int* ret), false) \
    X(win_set_size, L"Engine.dll", "?SetSize@eCGuiWindow2@@QEAAXAEBVbCPoint@@@Z", void (*)(void* w, const int* p), false) \
    X(win_get_visible, L"Engine.dll", "?GetVisible@eCGuiWindow2@@QEBA_NXZ", bool (*)(const void* w), false) \
    X(win_set_visible, L"Engine.dll", "?SetVisible@eCGuiWindow2@@QEAAXAEB_N@Z", void (*)(void* w, const bool* v), false) \
    X(win_get_disabled, L"Engine.dll", "?GetDisabled@eCGuiWindow2@@QEBA_NXZ", bool (*)(const void* w), false) \
    X(win_set_disabled, L"Engine.dll", "?SetDisabled@eCGuiWindow2@@QEAAXAEB_N@Z", void (*)(void* w, const bool* v), false) \
    X(win_clone_into, L"Engine.dll", "?Clone@eCGuiWindow2@@QEAAPEAV1@PEAV1@@Z", void* (*)(void* w, void* parent), false) \
    X(win_set_parent, L"Engine.dll", "?SetParent@eCGuiWindow2@@QEAA_NPEAV1@@Z", bool (*)(void* w, void* parent), false) \
    X(win_set_focus, L"Engine.dll", "?SetFocus@eCGuiWindow2@@QEAA_NXZ", bool (*)(void* w), false) \
    X(win_set_hot, L"Engine.dll", "?SetHot@eCGuiWindow2@@QEAA_NXZ", bool (*)(void* w), false) \
    X(win_perform_layout, L"Engine.dll", "?PerformLayout@eCGuiWindow2@@QEAAXXZ", void (*)(void* w), false) \
    X(type_class_name, L"SharedBase.dll", "?GetClassName@bCPropertyObjectTypeBase@@QEBAAEBVbCString@@XZ", const void* (*)(const void* type), false) \
    /* ---- trace of the engine's own script calls (the `calls` and `hud` trace channels) ---- */ \
    X(script_proxy_call, L"Engine.dll", "??ReCScriptProxyScript@@QEBA_JPEAVeCEntity@@0_J@Z", std::int64_t (*)(const void* proxy, void* self, void* other, std::int64_t arg), false) \
    X(script_proxy_name, L"Engine.dll", "?GetScriptName@eCScriptProxy@@QEBAAEBVbCString@@XZ", const void* (*)(const void* proxy), false) \
    X(admin_run_function, L"Game.dll", "?RunScriptFunction@gCScriptAdmin@@IEAA_NAEBVbCString@@AEAV?$bTObjStack@UgScriptRunTimeSingleState@@@@PEAVgCScriptProcessingUnit@@@Z", bool (*)(void* admin, const void* name, void* stack, void* unit), false) \
    X(unit_set_state, L"Game.dll", "?SetCurrentAIState@gCScriptProcessingUnit@@IEAA?AW4bEResult@@AEAVbCString@@@Z", int (*)(void* unit, void* name), false) \
    X(ent_get_npcs, L"Script.dll", "?GetNPCs@Entity@@SA?AV?$bTObjArray@VEntity@@@@XZ", void* (*)(void* ret), false) \
    X(action_send_event, L"Game.dll", "?SendActionEvent@gCActionAdmin@@IEAAXAEBUgSAction@@@Z", void (*)(void* admin, const void* action), false) \
    /* ---- routines (M10, docs/RE.md section 35): gCNavigation_PS holds the named routines; each has a working / relaxing / sleeping point ---- */ \
    X(nav_get_work, L"Game.dll", "?GetCurrentWorkingPoint@gCNavigation_PS@@QEBAPEAVeCEntity@@XZ", void* (*)(const void* nav), false) \
    X(nav_get_relax, L"Game.dll", "?GetCurrentRelaxingPoint@gCNavigation_PS@@QEBAPEAVeCEntity@@XZ", void* (*)(const void* nav), false) \
    X(nav_get_sleep, L"Game.dll", "?GetCurrentSleepingPoint@gCNavigation_PS@@QEBAPEAVeCEntity@@XZ", void* (*)(const void* nav), false) \
    X(nav_set_work, L"Game.dll", "?SetCurrentWorkingPoint@gCNavigation_PS@@QEAA_NPEAVeCEntity@@@Z", bool (*)(void* nav, void* point), false) \
    X(nav_set_relax, L"Game.dll", "?SetCurrentRelaxingPoint@gCNavigation_PS@@QEAA_NPEAVeCEntity@@@Z", bool (*)(void* nav, void* point), false) \
    X(nav_set_sleep, L"Game.dll", "?SetCurrentSleepingPoint@gCNavigation_PS@@QEAA_NPEAVeCEntity@@@Z", bool (*)(void* nav, void* point), false) \
    X(nav_select_routine, L"Game.dll", "?SetRoutine@gCNavigation_PS@@QEAA_NAEBVbCString@@@Z", bool (*)(void* nav, const void* name), false) \
    X(nav_get_routine_name, L"Game.dll", "?GetRoutine@gCNavigation_PS@@QEBAAEBVbCString@@XZ", const void* (*)(const void* nav), false) \
    X(rt_continue_routine, L"Script.dll", "?ContinueRoutine@PSRoutine@@QEAAXXZ", void (*)(void* self), false) \
    X(npc_get_guard_point, L"Script.dll", "?GetGuardPoint@PSNpc@@QEBA?AVEntity@@XZ", void* (*)(const void* self, void* ret), false) \
    X(npc_set_guard_point, L"Script.dll", "?SetGuardPoint@PSNpc@@QEBA_NAEBVEntity@@@Z", bool (*)(const void* self, const void* point), false) \
    X(world_get_time, L"Script.dll", "?GetWorldTime@Entity@@SAMXZ", float (*)(), false) \
    X(world_set_time, L"Script.dll", "?SetWorldTime@Entity@@SAXM@Z", void (*)(float), false) \
    X(ent_get_at, L"Script.dll", "?GetAtVector@Entity@@QEBA?AVbCVector@@XZ", void* (*)(const void* self, float* ret3), false) \
    X(ent_nearest_nav_point, L"Script.dll", "?GetNearestNavigationPoint@Entity@@QEAA?AVbCVector@@V2@M@Z", void* (*)(void* self, float* ret3, const float* position, float radius), false) \
    /* ---- AI scripting (M11, docs/RE.md section 36): the AI state / function / callback namespaces of the script registry, the processing unit of an NPC ---- */ \
    X(script_map_register_state, L"Engine.dll", "?RegisterScriptAIState@eCScriptFunctionMap@@QEAAPEAUeSScriptBase@@AEBVbCString@@@Z", void* (*)(void* self, const void* name), false) \
    X(script_map_register_function, L"Engine.dll", "?RegisterScriptAIFunction@eCScriptFunctionMap@@QEAAPEAUeSScriptBase@@AEBVbCString@@@Z", void* (*)(void* self, const void* name), false) \
    X(script_map_register_callback, L"Engine.dll", "?RegisterScriptAICallback@eCScriptFunctionMap@@QEAAPEAUeSScriptBase@@AEBVbCString@@@Z", void* (*)(void* self, const void* name), false) \
    X(script_map_get_state, L"Engine.dll", "?GetScriptAIState@eCScriptFunctionMap@@QEBAPEBUeSScriptBase@@AEBVbCString@@@Z", void* (*)(const void* self, const void* name), false) \
    X(script_map_get_function, L"Engine.dll", "?GetScriptAIFunction@eCScriptFunctionMap@@QEBAPEBUeSScriptBase@@AEBVbCString@@@Z", void* (*)(const void* self, const void* name), false) \
    X(script_map_get_callback, L"Engine.dll", "?GetScriptAICallback@eCScriptFunctionMap@@QEBAPEBUeSScriptBase@@AEBVbCString@@@Z", void* (*)(const void* self, const void* name), false) \
    X(unit_set_exit_script, L"Game.dll", "?AISetExitScript@gCScriptProcessingUnit@@QEAAXVbCString@@@Z", void (*)(void* unit, void* byValueName), false) \
    X(unit_set_task_callback, L"Game.dll", "?AISetTaskCallback@gCScriptProcessingUnit@@QEAAXVbCString@@@Z", void (*)(void* unit, void* byValueName), false) \
    X(unit_set_local_callback, L"Game.dll", "?AISetLocalCallback@gCScriptProcessingUnit@@QEAAXVbCString@@@Z", void (*)(void* unit, void* byValueName), false) \
    X(unit_get_state_time, L"Game.dll", "?AIGetStateTime@gCScriptProcessingUnit@@QEAAMXZ", float (*)(void* unit), false) \
    X(unit_get_task_time, L"Game.dll", "?AIGetTaskTime@gCScriptProcessingUnit@@QEAAMXZ", float (*)(void* unit), false) \
    /* ---- geometry (M13, docs/RE.md section 38): the physics scene's ray test (what the AI's CanSee uses), the camera ---- */ \
    X(phys_instance, L"Engine.dll", "?GetInstance@eCPhysicsScene@@SAAEAV1@XZ", void* (*)(), false) \
    X(phys_trace_nearest, L"Engine.dll", "?TraceRayNearestHit@eCPhysicsScene@@QEAA_NAEBVbCVector@@0PEBVeCGeometryEntity@@K1W4eEPropertySetType@@W4eEShapeGroup@@@Z", bool (*)(void* scene, const float* origin, const float* dir, const void* ignoreA, unsigned long flags, const void* ignoreB, int propSet, int group), false) \
    X(phys_set_ignore, L"Engine.dll", "?SetQueryIgnoreEntities@eCPhysicsScene@@QEAAXAEAV?$bTPtrArray@PEAVeCGeometryEntity@@@@@Z", void (*)(void* scene, void* array), false) \
    X(geo_is_free_los, L"Engine.dll", "?IsFreeLOS@eCGeometryEntity@@QEBA_NPEAV1@_N@Z", bool (*)(const void* self, void* other, bool flag), false) \
    X(app_instance, L"Engine.dll", "?GetInstance@eCApplication@@SAAEAV1@XZ", void* (*)(), false) \
    X(app_current_camera, L"Engine.dll", "?GetCurrentCamera@eCApplication@@QEAAAEAVeCCameraBase@@XZ", void* (*)(void* app), false) \
    X(cam_get_position, L"Engine.dll", "?GetPosition@eCCameraBase@@QEBAAEBVbCVector@@XZ", const float* (*)(const void* cam), false) \
    X(cam_get_view, L"Engine.dll", "?GetViewMatrix@eCCameraBase@@QEBAAEBVbCMatrix@@XZ", const float* (*)(const void* cam), false) \
    X(cam_get_fov, L"Engine.dll", "?GetFieldOfView@eCCameraBase@@QEBAAEBMXZ", const float* (*)(const void* cam), false) \
    X(cam_get_aspect, L"Engine.dll", "?GetAspectRatio@eCCameraBase@@QEBAAEBMXZ", const float* (*)(const void* cam), false) \
    /* ---- area string properties (M5e, docs/RE.md section 40): the security zones ("SL_Public", ...) the theft assessment reads ---- */ \
    X(area_access_names, L"Engine.dll", "?AccessPropertyNameList@eCArea_StringProperty_PS@@QEAAAEAV?$bTObjArray@VbCString@@@@XZ", void* (*)(void* self), false)

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
