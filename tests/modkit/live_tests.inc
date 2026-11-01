// Stage E35: the live mirror (core/live_actors.*)
#include "core/live_actors.h"
#include "core/live_edit.h"
#include "core/live_motion.h"

namespace {

nlohmann::json live_sample(float px, float pz, bool with_wolf = true) {
    nlohmann::json actors = nlohmann::json::array();
    actors.push_back({{"id", "P"}, {"name", "Hero"}, {"template", "Hero"}, {"x", px}, {"y", 10.0}, {"z", pz}, {"yaw", 350.0}, {"dead", false}, {"player", true}});
    if (with_wolf) actors.push_back({{"id", "W1"}, {"name", "Wolf_1"}, {"template", "Mon_Wolf"}, {"x", 500.0}, {"y", 0.0}, {"z", 500.0}, {"yaw", 90.0}, {"dead", false}});
    return {{"world", "World"}, {"actors", actors}};
}

}  // namespace

TEST(live_yaw_takes_the_short_way) {
    CHECK(std::fabs(mk::yaw_delta(350, 10) - 20) < 1e-3f);
    CHECK(std::fabs(mk::yaw_delta(10, 350) + 20) < 1e-3f);
    CHECK(std::fabs(mk::yaw_delta(0, 180) - 180) < 1e-3f);
    CHECK(std::fabs(mk::yaw_delta(90, 90)) < 1e-3f);
}

TEST(live_mirror_rejects_garbage) {
    mk::LiveMirror m;
    CHECK(!m.apply(nlohmann::json(), 1.0));
    CHECK(!m.apply(nlohmann::json{{"actors", 5}}, 1.0));
    CHECK(!m.has_data());
    // rows without an id or a position are skipped, the rest is kept
    nlohmann::json bad = {{"actors", nlohmann::json::array({nlohmann::json{{"x", 1}}, nlohmann::json{{"id", "A"}, {"x", "no"}, {"y", 0}, {"z", 0}},
                                                            nlohmann::json{{"id", "B"}, {"x", 1}, {"y", 2}, {"z", 3}}})}};
    CHECK(m.apply(bad, 1.0));
    CHECK(m.actors().size() == 1 && m.actors()[0].id == "B");
    CHECK(m.actors()[0].yaw == 0 && !m.actors()[0].dead);
}

TEST(live_mirror_glides_between_samples_and_snaps_on_a_jump) {
    mk::LiveMirror m;
    CHECK(m.apply(live_sample(0, 0), 1.0));
    CHECK(m.has_data() && m.world() == "World" && m.actors().size() == 2);
    CHECK(m.player() && m.player()->id == "P" && m.find("W1") && !m.find("nope"));
    CHECK(m.find_name("Wolf_1") && m.find_name("Wolf_1")->id == "W1" && !m.find_name("Wolf_2") && !m.find_name(""));
    // a new place a little away: the shown position starts at the old one and approaches
    CHECK(m.apply(live_sample(100, 0), 1.2));
    const mk::LiveActor* p = m.player();
    CHECK(p && p->x == 0 && p->tx == 100);
    CHECK(p->speed > 400 && p->speed < 600);  // 100 units in 0.2 s
    m.update(0.05);
    CHECK(p->x > 0 && p->x < 100);
    for (int i = 0; i < 100; ++i) m.update(0.05);
    CHECK(std::fabs(m.player()->x - 100) < 0.01f);
    // the yaw goes 350 -> 10 over the short way, never through 180
    nlohmann::json s = live_sample(100, 0);
    s["actors"][0]["yaw"] = 10.0;
    CHECK(m.apply(s, 3.0));
    m.update(0.02);
    CHECK(m.player()->yaw > 349.0f || m.player()->yaw < 20.0f);
    for (int i = 0; i < 100; ++i) m.update(0.05);
    CHECK(std::fabs(m.player()->yaw - 10) < 0.01f);
    // a teleport (a load) is not a glide
    CHECK(m.apply(live_sample(9000, 9000), 3.2));
    CHECK(m.player()->x == 9000 && m.player()->z == 9000);
    // an actor missing from the next sample is gone; clear forgets everything
    CHECK(m.apply(live_sample(9000, 9000, false), 3.4));
    CHECK(m.actors().size() == 1 && !m.find("W1"));
    m.clear();
    CHECK(m.actors().empty() && !m.has_data() && m.world().empty());
}

TEST(live_edit_gate_waits_for_quiet_and_for_a_game) {
    mk::LiveEditGate g;
    // nothing connected: edits are not sent, and not remembered either
    CHECK(!g.step(true, false, false, 1, 10.0));
    CHECK(!g.step(true, false, false, 2, 20.0) && !g.pending());
    // a game is there, nothing changed
    CHECK(!g.step(true, true, false, 2, 21.0) && !g.pending());
    // a change: sent after the pause, once
    CHECK(!g.step(true, true, false, 3, 22.0) && g.pending());
    CHECK(!g.step(true, true, false, 3, 22.5));
    CHECK(g.step(true, true, false, 3, 22.9));
    CHECK(!g.step(true, true, false, 3, 23.5) && !g.pending());
    // a burst of changes: one send, after the last one
    CHECK(!g.step(true, true, false, 4, 30.0));
    CHECK(!g.step(true, true, false, 5, 30.5));
    CHECK(!g.step(true, true, false, 6, 31.0));
    CHECK(!g.step(true, true, false, 6, 31.5));
    CHECK(g.step(true, true, false, 6, 31.9));
    // a reload still on its way holds the next one back until it is done
    CHECK(!g.step(true, true, false, 7, 40.0));
    CHECK(!g.step(true, true, true, 7, 41.0) && g.pending());
    CHECK(g.step(true, true, false, 7, 41.1));
    // switched off: forgotten
    CHECK(!g.step(true, true, false, 8, 50.0) && g.pending());
    CHECK(!g.step(false, true, false, 8, 51.0) && !g.pending());
}

TEST(live_motion_is_chosen_by_speed) {
    const std::vector<std::string> hero = {
        "Hero_Stand_None_None_P0_Move_Walk_N_Fwd_00_%_00_P0_150._xmot",   "Hero_Stand_None_None_P0_Move_Stand_N_Fwd_00_%_00_P0_0._xmot",
        "Hero_Stand_None_None_P0_Move_Sprint_N_Fwd_00_%_00_P0_625._xmot", "Hero_Stand_None_None_P0_Move_Run_N_Fwd_00_%_00_P0_400._xmot",
        "Hero_Stand_None_None_P0_Move_Walk_N_Left_00_%_00_P0_150._xmot",  "Hero_Stand_None_None_P0_Move_Begin_N_Fwd_00_%_00_P0_400._xmot",
        "Hero_Stand_Sword_None_P0_Move_Walk_N_Fwd_00_%_00_P0_150._xmot",  "Hero_Stand_None_None_P0_Roar_Begin_N_Fwd_00_%_00_P0_0._xmot"};
    CHECK(mk::motion_cycle_speed(hero[0]) == 150 && mk::motion_cycle_speed(hero[1]) == 0 && mk::motion_cycle_speed("nothing") == 0);
    // standing
    mk::MotionChoice c = mk::choose_motion(hero, 0, false);
    CHECK(c.name == hero[1] && !c.moving);
    // walking at the pace the cycle was made for: the forward cycle without a weapon, not the sideways one nor the sword one
    c = mk::choose_motion(hero, 150, false);
    CHECK(c.name == hero[0] && c.moving && std::fabs(c.rate - 1.0f) < 1e-4f);
    c = mk::choose_motion(hero, 75, false);
    CHECK(c.name == hero[0] && std::fabs(c.rate - 0.5f) < 1e-4f);
    // running, sprinting, and the rate stays in a sane range
    CHECK(mk::choose_motion(hero, 380, true).name == hero[3]);
    CHECK(mk::choose_motion(hero, 700, true).name == hero[2]);
    c = mk::choose_motion(hero, 5000, true);
    CHECK(c.name == hero[2] && c.rate == 1.8f);
    // hysteresis: 20 units/s starts nothing, but keeps a walk going
    CHECK(!mk::choose_motion(hero, 20, false).moving);
    CHECK(mk::choose_motion(hero, 20, true).moving);
    CHECK(!mk::choose_motion(hero, 8, true).moving);
    // a family without move cycles stands in its bind pose; without a stand motion too
    CHECK(mk::choose_motion({}, 200, false).name.empty());
    CHECK(mk::choose_motion({hero[7]}, 0, false).name.empty());
    // the wolf walks at 60 and runs at 350
    const std::vector<std::string> wolf = {"Wolf_Stand_None_None_P0_Move_Walk_N_Fwd_00_%_00_P0_60._xmot", "Wolf_Stand_None_None_P0_Move_Run_N_Fwd_00_%_00_P0_350._xmot"};
    CHECK(mk::choose_motion(wolf, 70, false).name == wolf[0]);
    CHECK(mk::choose_motion(wolf, 300, false).name == wolf[1]);
}
