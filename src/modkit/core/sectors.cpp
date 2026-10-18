#include "core/sectors.h"

#include <map>

#include "core/log.h"

namespace mk {

const std::vector<std::pair<std::string, SectorTrigger>>& script_sector_triggers() {
    using T = SectorTrigger;
    // Script_Game.dll, every caller of World::SetSectorStatus (Script.dll 0x2365f0) in the decompiled functions; the RVAs are in RE.md section 55
    static const std::vector<std::pair<std::string, T>> t = {
        {"Game_Start_NPC_Story_01", {T::Script, "OnQuestSuccess_NoviceHensonsWolvesDead", "quest-success script, only while the game variable Henson_KillWolvesQuestStarted is set", true}},
        {"Game_Start_NPC_Story_01", {T::Script, "PS_Sleep", "an NPC sleeps while the game variable Henson_INQIsThere exists", false}},
        {"OutMain_Story_Chapter2_MonsterRefill_Off", {T::Script, "OnQuestSuccess_ChangeToChapter2", "chapter 2 begins", true}},
        {"Pirate_Coast_NPC_02_Story", {T::Script, "OnQuestSuccess_ChangeToChapter2", "chapter 2 begins", true}},
        {"Pirate_Coast_NPC_03_Story", {T::Script, "OnQuestSuccess_FindGregsHintAll", "quest-success script FindGregsHintAll", true}},
        {"OutMain_Teleport_Story_LizardScouts", {T::Script, "OnQuestSuccess_SpawnLizardScoutsWithTeleportStones", "quest-success script, once (game variable LizardScoutsWithTeleportStonesSpawned)", true}},
        {"OutMain_Story_Chapter3_MonsterRefill_Off", {T::Script, "OnQuestSuccess_ChangeToChapter3", "chapter 3 begins", true}},
        {"OutMain_Story_Chapter3_Lizards_Off", {T::Script, "OnQuestSuccess_ChangeToChapter3", "chapter 3 begins", true}},
        {"OutMain_Story_Chapter4_MonsterRefill_Off", {T::Script, "OnQuestSuccess_ChangeToChapter4", "chapter 4 begins", true}},
        {"Pirate_Coast_Monster_02_Story_Ch4Lizards", {T::Script, "OnQuestSuccess_ChangeToChapter4", "chapter 4 begins", true}},
        {"Monastery_Story_LizardAttack_Off", {T::Script, "OnQuestRunning_Inquisitor_OpenPortal", "quest-running script Inquisitor_OpenPortal, once", true}},
        {"Monastery_Story_LizardAttack_Off", {T::Script, "OnQuestSuccess_LizardAttack", "quest-success script LizardAttack (used by Hlp_Story_Erlan and the lever Obj_EVT_Lever_StoryLibrary)", true}},
        {"Dungeon_Titan_Story_MainGate_LeftSide", {T::Script, "OnQuestSuccess_Player_EntersFirstWinch", "quest-success script after Obj_EVT_DungeonTitan_MainGate_LeftSide was opened", true}},
        {"Druid_Outdoor_Story_LizardHunter_Off", {T::Script, "OnQuestRunning_Inquisitor_MainGateStomper_Off", "quest-running script, context Inquisitor_ScoutTunnel", true}},
        {"Dungeon_Titan_Story_Titan", {T::Script, "PS_Titan_Begin", "the Titan boss fight begins", true}},
        {"Dungeon_Titan_Story_Titan", {T::Script, "CON_EndBoss", "debug console command EndBoss", true}},
        {"Harbour_Town_NPC_Story_INQ", {T::Script, "OnQuestSuccess_DonsReturnToHarbour", "quest-success script DonsReturnToHarbour", false}},
    };
    return t;
}

std::vector<SectorInfo> load_sectors(const GameData& g, const std::string& world) {
    std::vector<SectorInfo> out;
    std::map<std::string, size_t> by_name;
    const std::string prefix = "projects/" + world + "/";
    for (const std::string& path : g.paths_with_prefix(prefix)) {
        if (!p2m::ends_with(p2m::lower(path), ".sec")) continue;
        SectorInfo s;
        s.path = path;
        s.name = p2m::bare_name(path);
        s.name.resize(s.name.size() - 4);
        try {
            const p2m::Template t = p2m::parse_template(g.read_path(path));
            for (const p2m::Prop& p : t.props)
                if (p.name == "EnabledOnStartup" && p.value.is_num()) s.enabled_on_startup = p.value.num() != 0;
        } catch (const std::exception& e) {
            log_warn("sector " + path + ": " + e.what());
        }
        by_name[p2m::lower(s.name)] = out.size();
        out.push_back(std::move(s));
    }
    for (const auto& [sector, trig] : script_sector_triggers())
        if (auto it = by_name.find(p2m::lower(sector)); it != by_name.end()) out[it->second].triggers.push_back(trig);
    // the dialogue command: every gCInfoCommandSetSectorStatus belongs to the gCInfo record before it
    try {
        const p2m::Template t = p2m::parse_template(g.read("compiled_infos.bin"));
        std::string info, owner, quest, name;
        bool have_name = false;
        for (const p2m::Prop& p : t.props) {
            if (p.pset == "gCInfo") {
                if (p.name == "Name" && p.value.is_str()) {
                    info = p.value.s;
                    owner.clear();
                    quest.clear();
                } else if (p.name == "Owner" && p.value.is_str()) owner = p.value.s;
                else if (p.name == "Quest" && p.value.is_str()) quest = p.value.s;
            } else if (p.pset == "gCInfoCommandSetSectorStatus") {
                if (p.name == "SectorName" && p.value.is_str()) {
                    name = p.value.s;
                    have_name = true;
                } else if (p.name == "SectorEnabled" && have_name) {
                    have_name = false;
                    auto it = by_name.find(p2m::lower(name));
                    if (it == by_name.end()) continue;
                    SectorTrigger tr;
                    tr.kind = SectorTrigger::Dialogue;
                    tr.what = info;
                    tr.detail = std::string(quest.empty() ? "a line" : "a line of quest " + quest) + (owner.empty() ? "" : " spoken by " + owner);
                    tr.enable = p.value.is_num() && p.value.num() != 0;
                    out[it->second].triggers.push_back(std::move(tr));
                }
            }
        }
    } catch (const std::exception& e) {
        log_warn(std::string("sector triggers from dialogues: ") + e.what());
    }
    return out;
}

}  // namespace mk
