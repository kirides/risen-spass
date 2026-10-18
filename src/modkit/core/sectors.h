// World sectors that start switched off and the story events that switch them on (docs/RE.md section 55). A sector is a world file (`X.sec` with its layer
// `X.lrent`); `EnabledOnStartup = false` keeps its entities out of the world until `World::SetSectorStatus(name, true)` runs: from a dialogue (the info command
// SetSectorStatus, read from compiled_infos.bin) or from a game script (a fixed table, read from the decompiled Script_Game.dll).
#pragma once

#include <string>
#include <vector>

#include "core/gamedata.h"

namespace mk {

struct SectorTrigger {
    enum Kind { Dialogue, Script } kind = Script;
    std::string what;    // the info name ("PANKRATZX2_00610") or the script function ("OnQuestSuccess_ChangeToChapter2")
    std::string detail;  // what starts it: "quest Patty_FollowToGregsCasketChest, spoken by Patty" / "chapter 2 begins"
    bool enable = true;  // false: the trigger switches the sector off again
};

struct SectorInfo {
    std::string name;  // the file base name, how scripts and infos name it
    std::string path;  // "projects/World/Outdoor/OutMain/OutMain_Story_Chapter3_Lizards_Off.sec"
    bool enabled_on_startup = true;
    std::vector<SectorTrigger> triggers;  // who switches it (empty for a sector that starts on and is never touched)
};

// every sector of the world with its start state and triggers; never throws (a missing file leaves the sector's defaults)
std::vector<SectorInfo> load_sectors(const GameData& g, const std::string& world);

// the script triggers known from Script_Game.dll (RE.md section 55), exposed for the tests
const std::vector<std::pair<std::string, SectorTrigger>>& script_sector_triggers();

}  // namespace mk
