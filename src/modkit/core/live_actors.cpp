#include "core/live_actors.h"

#include <cmath>

namespace mk {

float yaw_delta(float from, float to) {
    float d = std::fmod(to - from, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d <= -180.0f) d += 360.0f;
    return d;
}

namespace {

bool number(const nlohmann::json& j, const char* key, float& out) {
    if (!j.contains(key) || !j[key].is_number()) return false;
    out = j[key].get<float>();
    return std::isfinite(out);
}

}  // namespace

bool LiveMirror::apply(const nlohmann::json& result, double now) {
    if (!result.is_object() || !result.contains("actors") || !result["actors"].is_array()) return false;
    std::vector<LiveActor> next;
    next.reserve(result["actors"].size());
    for (const nlohmann::json& a : result["actors"]) {
        if (!a.is_object() || !a.contains("id") || !a["id"].is_string()) continue;
        LiveActor n;
        n.id = a["id"].get<std::string>();
        if (!number(a, "x", n.tx) || !number(a, "y", n.ty) || !number(a, "z", n.tz)) continue;
        if (!number(a, "yaw", n.tyaw)) n.tyaw = 0;
        if (a.contains("name") && a["name"].is_string()) n.name = a["name"].get<std::string>();
        if (a.contains("template") && a["template"].is_string()) n.tpl = a["template"].get<std::string>();
        n.dead = a.value("dead", false);
        n.player = a.value("player", false);
        n.x = n.tx;
        n.y = n.ty;
        n.z = n.tz;
        n.yaw = n.tyaw;
        next.push_back(std::move(n));
    }
    const double dt = last_sample_ > 0 ? now - last_sample_ : 0;
    for (LiveActor& n : next) {
        if (const LiveActor* old = find(n.id)) {
            const float jump = std::hypot(n.tx - old->tx, n.tz - old->tz) + std::fabs(n.ty - old->ty);
            if (jump < kSnapDistance) {  // continues from where it is shown now
                n.x = old->x;
                n.y = old->y;
                n.z = old->z;
                n.yaw = old->yaw;
                if (dt > 0.001) n.speed = static_cast<float>(std::hypot(n.tx - old->tx, n.tz - old->tz) / dt);
            }
        }
    }
    actors_ = std::move(next);
    world_ = result.value("world", std::string());
    last_sample_ = now > 0 ? now : 1e-9;
    return true;
}

void LiveMirror::update(double dt) {
    if (dt <= 0) return;
    // exponential approach: about 90% of the way after 0.2 s, so a 5 Hz sample stream looks continuous
    const float k = static_cast<float>(1.0 - std::exp(-dt * 12.0));
    for (LiveActor& a : actors_) {
        a.x += (a.tx - a.x) * k;
        a.y += (a.ty - a.y) * k;
        a.z += (a.tz - a.z) * k;
        a.yaw += yaw_delta(a.yaw, a.tyaw) * k;
        if (a.yaw < 0) a.yaw += 360.0f;
        if (a.yaw >= 360.0f) a.yaw -= 360.0f;
    }
}

void LiveMirror::clear() {
    actors_.clear();
    world_.clear();
    last_sample_ = 0;
}

const LiveActor* LiveMirror::player() const {
    for (const LiveActor& a : actors_)
        if (a.player) return &a;
    return nullptr;
}

const LiveActor* LiveMirror::find(const std::string& id) const {
    for (const LiveActor& a : actors_)
        if (a.id == id) return &a;
    return nullptr;
}

const LiveActor* LiveMirror::find_name(const std::string& name) const {
    if (name.empty()) return nullptr;
    for (const LiveActor& a : actors_)
        if (a.name == name) return &a;
    return nullptr;
}

}  // namespace mk
