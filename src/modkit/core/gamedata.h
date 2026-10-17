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
    std::string label(const std::string& template_name, const std::string& language = "English") const { return text("FO_" + template_name, language); }

    // resources by compiled extension ("_xmsh", "_xmat", "_ximg", "_xmac", ...): sorted lists
    const std::vector<AssetEntry>& assets(const std::string& ext) const;
    // the newest version of a file by its bare name (case-insensitive); throws p2m::Error when missing or damaged
    p2m::Bytes read(const std::string& bare_name) const;
    bool exists(const std::string& bare_name) const;
    // the game's own quests, sorted by name (empty when compiled_quests.bin is missing or unreadable)
    const std::vector<GameQuest>& quests() const { return quests_; }
    const GameQuest* quest(const std::string& name) const;  // case-insensitive
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
    std::unordered_map<std::string, std::vector<std::string>> texts_;  // id -> text per language (index into languages_)
    std::map<std::string, std::vector<AssetEntry>> assets_;
    std::vector<std::string> worlds_;
    std::vector<GameQuest> quests_;
    ImageCatalog images_;
    p2m::Ctx ctx_;
    double load_seconds_ = 0;
};

}  // namespace mk
