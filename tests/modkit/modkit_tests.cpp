// Unit tests of risen-modkit's core (no GUI, no game data needed). Build target `modkit_tests`; run with ctest or directly.
//   modkit_tests [--data <Risen>\data] [filter]    --data also runs the tests that read the real game archives
// A tiny self-contained runner, like tests/test_main.cpp. Test groups live in *.inc files next to this one.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <sstream>
#include <set>
#include <string>
#include <vector>

#include "common/sound_file.h"
#include "core/assets.h"
#include "core/behavior.h"
#include "core/catalog.h"
#include "core/dialog.h"
#include "core/export.h"
#include "core/gamedata.h"
#include "core/history.h"
#include "core/image.h"
#include "core/imagelist.h"
#include "core/import.h"
#include "core/lua_writer.h"
#include "core/mesh.h"
#include "core/log.h"
#include "core/paths.h"
#include "core/project.h"
#include "core/quest.h"
#include "core/validate.h"
#include "core/world.h"
#include "pack2mod/util.h"

#include <luajit/lua.hpp>

namespace fs = std::filesystem;

namespace {

int g_checks = 0, g_failures = 0;
std::string g_current;
std::string g_data_dir;  // --data

std::map<std::string, std::function<void()>>& registry() {
    static std::map<std::string, std::function<void()>> r;
    return r;
}
struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry()[name] = std::move(fn); }
};

#define TEST(name)                                         \
    static void test_##name();                             \
    static Registrar reg_##name(#name, test_##name);        \
    static void test_##name()

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        ++g_checks;                                                                                  \
        if (!(cond)) {                                                                               \
            ++g_failures;                                                                            \
            std::printf("  %s:%d: %s: CHECK(%s) failed\n", __FILE__, __LINE__, g_current.c_str(), #cond); \
        }                                                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                                                              \
    do {                                                                                                            \
        ++g_checks;                                                                                                 \
        const auto va_ = (a);  /* by value: `render_mod(p).at("f")` is a reference into a temporary that is gone at the end of the statement */                                                                                      \
        const auto vb_ = (b);                                                                                      \
        if (!(va_ == vb_)) {                                                                                        \
            ++g_failures;                                                                                           \
            std::printf("  %s:%d: %s: CHECK_EQ(%s, %s) failed\n", __FILE__, __LINE__, g_current.c_str(), #a, #b); \
        }                                                                                                           \
    } while (0)

fs::path temp_dir(const std::string& name) {
    fs::path d = fs::temp_directory_path() / ("modkit_tests_" + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

}  // namespace

using namespace mk;

// ---- project model -------------------------------------------------------------------------------------------------------------------------------------

static Project sample_project() {
    Project p;
    p.mod.id = "sample";
    p.mod.name = "Sample";
    p.mod.author = "tests";
    p.mod.requires_mods = {"base"};
    ItemDef knife;
    knife.uid = "u1";
    knife.name = "MK_Knife";
    knife.base = "It_1H_Machete";
    knife.label = {{"English", "Hob's Knife"}, {"German", "Hobs Messer"}};
    knife.value = 250;
    knife.damage = 20;
    knife.requirements = IntMap{{"Strength", 10}};
    knife.properties["gCItem_PS"]["SortValue"] = 400;
    p.items.push_back(knife);
    ItemDef sword;
    sword.uid = "u2";
    sword.mode = Mode::Edit;
    sword.name = "It_1H_Sword";
    sword.value = 90;
    sword.modifies = IntMap{};
    p.items.push_back(sword);
    p.item_groups.push_back({"g1", "Items/Potions", "", "", {}, 0.5, std::nullopt});
    RecipeDef plan;
    plan.uid = "r1";
    plan.name = "MK_Plan";
    plan.station = "anvil";
    plan.ingredients = std::vector<Ingredient>{{"It_IronOre", 2}, {"It_1H_Sword", 1}};
    plan.result = "MK_Knife";
    plan.skills = IntMap{{"Smith", 1}};
    plan.learn = true;
    p.recipes.push_back(plan);
    NpcTemplateDef hob;
    hob.uid = "n1";
    hob.name = "MK_Hob_T";
    hob.base = "Peasant_1_01";
    hob.hp_max = 300;
    hob.loot = IntMap{{"It_Apple", 2}};
    p.npc_templates.push_back(hob);
    PlacedNpc inst;
    inst.uid = "p1";
    inst.name = "MK_Hob";
    inst.template_name = "MK_Hob_T";
    inst.position = {1200.5, 50, -3400};
    inst.yaw = 90;
    inst.items = {{"MK_Knife", 1, true}, {"It_Apple", 3, false}};
    inst.appear_when = json::array({"quest", "Q_X", "Running"});
    p.npcs.push_back(inst);
    DialogDef d;
    d.uid = "d1";
    d.name = "Hob";
    d.owner = "MK_Hob";
    GraphNode a{"na", "option", 10, 20, json::object({{"text", "Hello"}})};
    GraphNode b{"nb", "line", 200, 20, json::object()};
    d.graph.nodes = {a, b};
    d.graph.links = {{"l1", "na", "lines", "nb", "in"}};
    p.dialogs.push_back(d);
    QuestDef q;
    q.uid = "q1";
    q.name = "Q_X";
    q.title = {{"English", "The X"}};
    q.xp = 60;
    p.quests.push_back(q);
    p.world.spawns.push_back({"s1", "It_Apple", {1, 2, 3}, {90, 0, 0}, 5, ""});
    p.world.containers.push_back({"c1", "01234567-89ab-cdef-0123-456789abcdef", "Chest_01", "Harbour_Items.lrent", "", {{"It_Gold", 50, false}}});
    EntityRef rm;
    rm.uid = "e1";
    rm.guid = "11111111-2222-3333-4444-555555555555";
    p.world.removals.push_back(rm);
    EntityRef mv = rm;
    mv.uid = "e2";
    mv.position = Vec3{4, 5, 6};
    p.world.moves.push_back(mv);
    p.assets.push_back({"a1", "assets/X._xmsh", "mesh"});
    return p;
}

TEST(project_json_round_trip) {
    const Project p = sample_project();
    const std::string text = dump_project(p);
    std::vector<std::string> warnings;
    const Project q = project_from_json(json::parse(text), &warnings);
    CHECK(warnings.empty());
    CHECK_EQ(dump_project(q), text);  // byte-identical after a round trip
    CHECK_EQ(q.items.size(), size_t(2));
    CHECK(q.items[1].mode == Mode::Edit);
    CHECK(q.items[1].modifies.has_value() && q.items[1].modifies->empty());  // "clear the list" survives
    CHECK(!q.items[1].requirements.has_value());
    CHECK_EQ(q.items[0].properties.at("gCItem_PS").at("SortValue").get<int>(), 400);
    CHECK_EQ(q.npcs[0].items[0].equip, true);
    CHECK_EQ(q.npcs[0].position[0], 1200.5);
    CHECK_EQ(q.recipes[0].ingredients->size(), size_t(2));
    CHECK_EQ(q.dialogs[0].graph.links[0].to_pin, std::string("in"));
    CHECK(q.world.moves[0].position.has_value() && !q.world.removals[0].position.has_value());
    CHECK_EQ(q.mod.requires_mods.size(), size_t(1));
}

TEST(project_json_is_tolerant) {
    json j = json::parse(R"({"format": "risen-modkit-project", "format_version": 1,
        "items": [ {"name": "A", "value": "not a number"}, 5, {"name": "B", "mode": "weird"} ],
        "npcs": [ {"name": "N", "position": [1, 2]} ], "quests": "x"})");
    std::vector<std::string> warnings;
    const Project p = project_from_json(j, &warnings);
    CHECK_EQ(p.items.size(), size_t(2));
    CHECK(!p.items[0].value.has_value());
    CHECK(!p.items[0].uid.empty());  // a missing uid is made
    CHECK_EQ(p.npcs.size(), size_t(1));
    CHECK(warnings.size() >= 4);
    bool threw = false;
    try {
        project_from_json(json::parse(R"({"items": []})"));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        project_from_json(json::parse(R"({"format": "risen-modkit-project", "format_version": 99})"));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(project_file_save_load) {
    const fs::path dir = temp_dir("file");
    const Project p = sample_project();
    const std::string path = u8str(dir / "sample.rmk.json");
    save_project_file(p, path);
    const Project q = load_project_file(path);
    CHECK_EQ(dump_project(q), dump_project(p));
}

TEST(uids_are_unique) {
    std::map<std::string, int> seen;
    for (int i = 0; i < 1000; ++i) seen[new_uid()]++;
    CHECK_EQ(seen.size(), size_t(1000));
    CHECK_EQ(new_uid().size(), size_t(32));
}

TEST(history_undo_redo) {
    Project p;
    History h;
    h.reset(p);
    CHECK(!h.dirty());
    CHECK(!h.settle(p));  // nothing touched
    p.mod.name = "A";
    h.touch();
    CHECK(h.settle(p));
    CHECK(h.dirty());
    p.mod.name = "AB";
    h.touch();
    CHECK(h.settle(p));
    h.touch();
    CHECK(!h.settle(p));  // touched but unchanged: no step
    CHECK_EQ(h.undo_depth(), size_t(2));
    CHECK(h.undo(p));
    CHECK_EQ(p.mod.name, std::string("A"));
    CHECK(h.undo(p));
    CHECK_EQ(p.mod.name, std::string("My Mod"));
    CHECK(!h.dirty());
    CHECK(!h.undo(p));
    CHECK(h.redo(p));
    CHECK_EQ(p.mod.name, std::string("A"));
    p.mod.name = "C";  // a new edit drops the redo branch
    h.touch();
    h.settle(p);
    CHECK(!h.can_redo());
    h.mark_saved();
    CHECK(!h.dirty());
}

TEST(path_within) {
    const fs::path base = temp_dir("within");
    CHECK(path_within(base / "a" / "b", base));
    CHECK(path_within(base, base));
    CHECK(!path_within(base.parent_path(), base));
    CHECK(!path_within(base.string() + "x", base));
    CHECK(path_within(base / "a" / ".." / "c", base));
}

// ---- game data (needs --data) ----------------------------------------------------------------------------------------------------------------------------

static std::shared_ptr<GameData> game_data() {
    static std::shared_ptr<GameData> g;
    if (!g && !g_data_dir.empty()) g = GameData::load(g_data_dir);
    return g;
}

TEST(gamedata_rejects_wrong_dir) {
    bool threw = false;
    try {
        GameData::load(u8str(temp_dir("nodata")));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(gamedata_real_archives) {
    auto g = game_data();
    if (!g) {
        std::printf("  (skipped: no --data)\n");
        return;
    }
    CHECK(g->templates().size() > 2500);
    const TemplateInfo* sword = g->tpl("it_1h_sword");  // case-insensitive
    CHECK(sword && sword->kind == TplKind::Item);
    if (sword) {
        CHECK_EQ(sword->group, std::string("Items/Weapons_Swords"));
        CHECK(sword->get("gCItem_PS.GoldValue") != nullptr);
        CHECK(!sword->mesh.empty());
        CHECK(!sword->icon.empty());
    }
    CHECK_EQ(g->label("It_1H_Sword"), std::string("Sword"));
    CHECK(!g->label("It_1H_Sword", "German").empty());
    const TemplateInfo* wolf = g->tpl("Wolf");
    CHECK(wolf && wolf->kind == TplKind::Monster);
    const TemplateInfo* peasant = g->tpl("Peasant_1_01");
    CHECK(peasant && peasant->kind == TplKind::Npc);
    CHECK(g->tpl("It_Recipe_Health_01") && g->tpl("It_Recipe_Health_01")->kind == TplKind::Recipe);
    CHECK(std::find(g->languages().begin(), g->languages().end(), "German") != g->languages().end());
    CHECK(g->assets("_xmsh").size() > 1500);
    CHECK(g->exists("It_Wpn_1H_Sword._xmsh"));
    CHECK(std::find(g->worlds().begin(), g->worlds().end(), "World") != g->worlds().end());
    CHECK(g->paths_with_prefix("projects/World/").size() > 500);
    std::map<TplKind, int> kinds;
    for (auto& t : g->templates()) kinds[t->kind]++;
    for (auto& [k, n] : kinds) std::printf("  %-12s %d\n", kind_name(k), n);
    std::printf("  loaded in %.2f s\n", g->load_seconds());
}

#include "export_tests.inc"
#include "graph_tests.inc"
#include "import_tests.inc"
#include "asset_tests.inc"
#include "image_tests.inc"
#include "behavior_tests.inc"
#include "play_tests.inc"
#include "imagelist_tests.inc"
#include "inventory_tests.inc"
#include "spell_tests.inc"
#include "quest_branch_tests.inc"
#include "events_tests.inc"
#include "wizard_tests.inc"
#include "meshwrite_tests.inc"
#include "model_import_tests.inc"
#include "actor_tests.inc"
#include "sectors_tests.inc"
#include "genome_tests.inc"
#include "dialog_sim_tests.inc"
#include "chronicle_tests.inc"

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Log::get().set_echo(false);
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) g_data_dir = argv[++i];
        else filter = argv[i];
    }
    for (auto& [name, fn] : registry()) {
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;
        g_current = name;
        const int before = g_failures;
        try {
            fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::printf("  %s: exception: %s\n", name.c_str(), e.what());
        }
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", name.c_str());
    }
    std::printf("\n%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
