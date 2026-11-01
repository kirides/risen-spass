// Stage E35: the live mirror (core/live_actors.*)
#include "core/live_actors.h"

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
