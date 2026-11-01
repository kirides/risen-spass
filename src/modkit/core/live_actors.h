// The live mirror (stage E35): the player and the NPCs of the running game as the `inspect.actors` link command reports them (docs/LUA_API.md "Live link"),
// smoothed between the samples so the World view shows them moving continuously although the game is asked only a few times a second.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mk {

struct LiveActor {
    std::string id, name, tpl;
    bool player = false, dead = false;
    float x = 0, y = 0, z = 0, yaw = 0;      // shown (game units; yaw in degrees as `Entity:pose`)
    float tx = 0, ty = 0, tz = 0, tyaw = 0;  // the last sample
    float speed = 0;                         // horizontal units per second between the last two samples
};

class LiveMirror {
public:
    // A new `inspect.actors` result; false (and nothing changed) when it is not one. Actors missing from the sample are dropped, new ones appear at their place,
    // one that jumped far (a teleport, a load) is moved at once instead of gliding across the map.
    bool apply(const nlohmann::json& result, double now);
    // Moves every actor towards its last sample; `dt` in seconds.
    void update(double dt);
    void clear();

    const std::vector<LiveActor>& actors() const { return actors_; }
    const std::string& world() const { return world_; }
    double last_sample() const { return last_sample_; }
    bool has_data() const { return last_sample_ > 0; }
    const LiveActor* player() const;
    const LiveActor* find(const std::string& id) const;
    const LiveActor* find_name(const std::string& name) const;  // by the entity's unique name (a placed NPC of the project); nullptr when none is there

    static constexpr float kSnapDistance = 600.0f;

private:
    std::vector<LiveActor> actors_;
    std::string world_;
    double last_sample_ = 0;
};

// the shortest signed turn from `from` to `to` in degrees (-180..180]
float yaw_delta(float from, float to);

}  // namespace mk
