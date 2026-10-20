// The game's own data, read offline from the player's copy of the archives (docs/editor/REQUIREMENTS.md DATA-1..5): the layered archive set
// (common and compiled, .pak then .p00 < .p01 ..., later wins), every entity template with its typed properties, the string tables and an
// index of the resource names. Built once on a worker thread; afterwards read-only and shared by the panels.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/imagelist.h"
#include "core/tpl_inventory.h"
#include "pack2mod/internal.h"

namespace mk {

enum class TplKind { Item, Recipe, Npc, Monster, Interactive, Object, Effect, Navigation, Sound, Other };
const char* kind_name(TplKind k);
constexpr int kTplKindCount = 10;

// a quest of the game (compiled_quests.bin): its id, as risen.quest and quest.status_changed name it, and the text id of its title (quests.tab)
struct GameQuest {
    std::string name;      // "Rufo_GoToDruidHut"
    std::string title_id;  // "QUEST_FILLERX2_2008-09-02_10-50-38_00" ("" for a silent quest without a log topic)
    bool silent = false;
};

// one command or condition of a dialogue info: its class without the "gCInfoCommand" / "gCInfoCondition" prefix and the values it has, as text
struct InfoMember {
    std::string cls;  // "Say", "GiveXP", "PlayerKnows" ...
    std::vector<std::pair<std::string, std::string>> props;
};

// a dialogue info of the game (compiled_infos.bin): what a modder needs to find the one to edit
struct GameInfo {
    std::string name;    // "FILLERX2_00941": how the dialogue data and risen.dialog.get know it
    std::string owner;   // the NPC that speaks it ("Pallas"; "sh" = the player)
    std::string quest;   // the quest it belongs to ("" = none)
    std::string parent;  // the info it answers ("" = a top level info)
    std::vector<std::pair<std::string, std::string>> props;  // the info's own values (Name, Owner, Quest, ConditionType, StartChapter ...)
    // its commands and its conditions in order: the numbers risen.dialog's command / condition functions take are positions in these lists (1-based). A run of
    // properties is one member; a new one starts when the class changes or a property repeats (the reading of risen-pack2mod's property edits).
    std::vector<InfoMember> commands, conditions;
};

// a row of a game string table (compiled/strings/*.tab) with the cells of its columns besides the texts (StageDir and WaveFile per language, Voice, Info,
// Order, Quest, Owner, Role ...); the texts themselves are GameData::text(id, language)
struct GameStringRow {
    std::string table;  // the table's file name without ".tab", lower case: "infos", "items", "svms"
    std::string id;     // "INFO_FILLERX2_00002231"
    std::vector<std::pair<std::string, std::string>> cols;  // column -> cell, only non-empty cells ("English_WaveFile", "Voice", ...)
    const std::string* col(const std::string& name) const;  // nullptr when the cell is empty
};

struct TemplateInfo {
    std::string name;   // the entity name the engine knows (RE.md section 48)
    std::string file;   // the file name without .tple
    std::string group;  // directory inside templates.pak: "Items/Weapons_Swords"
    std::string guid;   // "@" + 32 hex
    TplKind kind = TplKind::Other;
    p2m::Flat props;    // "Set.Name" -> value, GUID references resolved to template names
    std::vector<std::string> sets;  // property sets in file order
    std::string mesh, icon;         // eCMesh_PS.MeshFileName / gCItem_PS.IconImage (empty when none)
    bool multi = false;             // a multi template (refused as a clone base)
    std::vector<int> skills;        // NPCs: the 34 base values of gCSkills_PS (gESkill order), empty when not found
    // NPCs: what the template carries (gCInventory_PS stacks, RE.md section 52); `item` is the template name, or the entity GUID ("@...") of an item no
    // template of the game has (the engine skips such a stack too)
    struct Stack {
        std::string item;
        bool resolved = true;
        int amount = 1, type = 0, equip_slot = 0;
    };
    std::vector<Stack> inventory;
    std::vector<std::pair<int, std::string>> worn;  // NPCs: equipment slot -> item template (armour, helmet), the record's second list
    bool inventory_known = false;                    // the record was read (an empty `inventory` then means "carries nothing")

    const p2m::Val* get(const std::string& key) const { return props.get(key); }
    bool has_set(const std::string& set) const;
};

// what the asset index knows about a resource name
struct AssetEntry {
    std::string name;   // compiled file name as stored: "It_Wpn_1H_Club._xmsh"
    std::string path;   // the path inside its archive
    std::string archive;
};

class GameData {
public:
    using Progress = std::function<void(float fraction, const std::string& what)>;
    // throws std::runtime_error when `data_dir` has no common / compiled folders; a damaged archive or template is logged and skipped
    static std::shared_ptr<GameData> load(const std::string& data_dir, const Progress& progress = {}, const std::atomic<bool>* cancel = nullptr);

    const std::string& data_dir() const { return data_dir_; }
    const std::vector<std::unique_ptr<TemplateInfo>>& templates() const { return templates_; }  // sorted by name (case-insensitive)
    const TemplateInfo* tpl(const std::string& name) const;  // case-insensitive
    // the template a GUID reference names ("@" + 32 hex, as property values and world entities refer to templates), "" when unknown
    std::string template_by_guid(const std::string& at_hex) const;
    const std::unordered_map<std::string, std::string>& guid_names() const { return guid_names_; }
    // names of templates of one kind, sorted
    std::vector<std::string> names_of(TplKind k) const;

    // texts: string id -> per language; languages are named as the layer does ("English", "German", ...)
    const std::vector<std::string>& languages() const { return languages_; }
    std::string text(const std::string& id, const std::string& language = "English") const;  // empty when absent; falls back to English
    std::string text_exact(const std::string& id, const std::string& language) const;  // the language's own cell, no fallback
    // every row of every string table in table and file order (a row id repeated in another table is a row there too)
    const std::vector<GameStringRow>& string_rows() const { return string_rows_; }
    std::string label(const std::string& template_name, const std::string& language = "English") const { return text("FO_" + template_name, language); }

    // resources by compiled extension ("_xmsh", "_xmat", "_ximg", "_xmac", ...): sorted lists
    const std::vector<AssetEntry>& assets(const std::string& ext) const;
    // the newest version of a file by its bare name (case-insensitive); throws p2m::Error when missing or damaged
    p2m::Bytes read(const std::string& bare_name) const;
    bool exists(const std::string& bare_name) const;
    // the game's own quests, sorted by name (empty when compiled_quests.bin is missing or unreadable)
    const std::vector<GameQuest>& quests() const { return quests_; }
    const GameQuest* quest(const std::string& name) const;  // case-insensitive
    // the game's dialogue infos (compiled_infos.bin), sorted by name; empty when the file is missing or unreadable. The first line an info says is
    // `info_text(name)` (infos.tab).
    const std::vector<GameInfo>& infos() const { return infos_; }
    // the first line an info says (infos.tab: the row of the info with the lowest "Order"), "" when it says nothing or the table has no such row
    std::string info_text(const std::string& info, const std::string& language = "English") const;
    const GameInfo* info(const std::string& name) const;  // by name, case-insensitive; nullptr when the game has none
    // the classes of commands / conditions the game's infos use, with the property names each has (in the order they appear), for "add a condition" and the
    // property lookups
    const std::map<std::string, std::vector<std::string>>& command_classes() const { return command_classes_; }
    const std::map<std::string, std::vector<std::string>>& condition_classes() const { return condition_classes_; }
    // the shipped GUI images (compiled_imagelists.bin): icon name -> rectangle of a texture; empty when the file is missing or unreadable
    const ImageCatalog& images() const { return images_; }
    // world projects: names of the folders below common/projects that hold a .wrl
    const std::vector<std::string>& worlds() const { return worlds_; }
    // files of the common layer by "<archive base>/<path inside>", e.g. "projects/World/World.wrl" (case-insensitive), sorted
    std::vector<std::string> paths_with_prefix(const std::string& prefix) const;
    p2m::Bytes read_path(const std::string& path) const;  // throws p2m::Error when missing

    const p2m::Ctx& ctx() const { return ctx_; }  // enum and skill names (src/pack2mod/enums_data.inc)
    std::string enum_name(const std::string& type, const p2m::Val& v) const;  // "gEItemUseType_1H" or "" when unknown
    double load_seconds() const { return load_seconds_; }
    size_t archive_count() const { return paks_.size(); }

private:
    std::string data_dir_;
    std::vector<std::unique_ptr<p2m::Pak>> paks_;
    p2m::Layered common_, compiled_;
    std::unordered_map<std::string, const p2m::LFile*> by_path_;  // lower-case "<archive base>/<path>" -> file (common layer)
    std::vector<std::unique_ptr<TemplateInfo>> templates_;
    std::unordered_map<std::string, TemplateInfo*> by_name_;  // lower-case name
    std::unordered_map<std::string, std::string> guid_names_;  // "@hex" -> template name
    std::vector<std::string> languages_;
    std::unordered_map<std::string, std::vector<std::string>> texts_;
    std::vector<GameStringRow> string_rows_;  // id -> text per language (index into languages_)
    std::map<std::string, std::vector<AssetEntry>> assets_;
    std::vector<std::string> worlds_;
    std::vector<GameQuest> quests_;
    std::vector<GameInfo> infos_;
    std::map<std::string, std::vector<std::string>> command_classes_, condition_classes_;
    std::unordered_map<std::string, std::string> info_first_line_;  // info name -> id of its first row in infos.tab
    ImageCatalog images_;
    p2m::Ctx ctx_;
    double load_seconds_ = 0;
};

}  // namespace mk
