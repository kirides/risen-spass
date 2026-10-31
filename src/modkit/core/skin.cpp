#include "core/skin.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace mk {

using p2m::Bytes;
using p2m::Error;

int Skeleton::find(const std::string& name) const {
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return static_cast<int>(i);
    return -1;
}

ActorSkin merge_skins(const std::vector<ActorSkin>& parts) {
    ActorSkin out;
    // the bones of all parts by name; the first part that has a bone gives its bind transform and its parent
    std::unordered_map<std::string, int> index;
    std::vector<std::vector<uint16_t>> remap(parts.size());
    for (size_t k = 0; k < parts.size(); ++k) {
        const Skeleton& s = parts[k].skeleton;
        remap[k].resize(s.names.size());
        for (size_t i = 0; i < s.names.size(); ++i) {
            auto it = index.find(s.names[i]);
            if (it == index.end()) {
                it = index.emplace(s.names[i], static_cast<int>(out.skeleton.names.size())).first;
                out.skeleton.names.push_back(s.names[i]);
                out.skeleton.parent.push_back(-1);
                out.skeleton.bind.push_back(s.bind[i]);
            }
            remap[k][i] = static_cast<uint16_t>(it->second);
        }
    }
    std::vector<bool> parent_set(out.skeleton.names.size(), false);
    for (size_t k = 0; k < parts.size(); ++k) {
        const Skeleton& s = parts[k].skeleton;
        for (size_t i = 0; i < s.names.size(); ++i) {
            const int g = remap[k][i];
            if (parent_set[static_cast<size_t>(g)] || s.parent[i] < 0 || static_cast<size_t>(s.parent[i]) >= s.names.size()) continue;
            out.skeleton.parent[static_cast<size_t>(g)] = remap[k][static_cast<size_t>(s.parent[i])];
            parent_set[static_cast<size_t>(g)] = true;
        }
    }
    for (size_t k = 0; k < parts.size(); ++k) {
        for (size_t v = 0; v < parts[k].bone.size(); ++v) {
            std::array<uint16_t, 4> b = parts[k].bone[v];
            for (auto& x : b) x = x < remap[k].size() ? remap[k][x] : static_cast<uint16_t>(0);
            out.bone.push_back(b);
            out.weight.push_back(parts[k].weight[v]);
        }
    }
    return out;
}

// ---- the motion file ----------------------------------------------------------------------------------------------------------------------------

Motion decode_motion(const Bytes& file) {
    if (file.size() < 0x40 || file.compare(0, 8, "GR01MO01") != 0) throw Error("not a GR01/MO01 motion");
    const size_t xsm = file.find("XSM ");
    if (xsm == std::string::npos || xsm + 8 > file.size()) throw Error("no EMotion FX motion inside");
    if (static_cast<uint8_t>(file[xsm + 6]) != 0) throw Error("a big endian motion");
    Motion m;
    size_t p = xsm + 8;
    while (p + 12 <= file.size()) {
        const uint32_t id = p2m::rd32(file, p), size = p2m::rd32(file, p + 4);
        if (id == 202) {  // the sub-motions: its size field is not reliable, the records tell where they end
            size_t o = p + 12;
            const uint32_t n = p2m::rd32(file, o);
            o += 4;
            if (n > 4096) throw Error("implausible number of sub-motions");
            for (uint32_t i = 0; i < n; ++i) {
                MotionTrack tr;
                // four rotations (pose, bind pose, pose and bind scale rotation), then position and scale of the pose and of the bind pose
                for (int k = 0; k < 4; ++k) {
                    if (k == 0)
                        for (int j = 0; j < 4; ++j) tr.pose.rot[j] = p2m::rdf(file, o + static_cast<size_t>(j) * 4);
                    o += 16;
                }
                for (int j = 0; j < 3; ++j) tr.pose.pos[j] = p2m::rdf(file, o + static_cast<size_t>(j) * 4);
                for (int j = 0; j < 3; ++j) tr.pose.scale[j] = p2m::rdf(file, o + 12 + static_cast<size_t>(j) * 4);
                o += 48;
                const uint32_t np = p2m::rd32(file, o), nr = p2m::rd32(file, o + 4), ns = p2m::rd32(file, o + 8), nsr = p2m::rd32(file, o + 12);
                o += 20;  // the counts and the largest error
                const uint32_t ln = p2m::rd32(file, o);
                o += 4;
                if (ln > 256 || o + ln > file.size()) throw Error("a sub-motion's name runs past the end of the motion");
                tr.name.assign(file.data() + o, ln);
                o += ln;
                if (np > 100000 || nr > 100000 || ns > 100000 || nsr > 100000 || o + (static_cast<size_t>(np) + ns) * 16 + (static_cast<size_t>(nr) + nsr) * 20 > file.size())
                    throw Error("a sub-motion's keys run past the end of the motion");
                for (uint32_t k = 0; k < np; ++k, o += 16) tr.pos.push_back({p2m::rdf(file, o + 12), {p2m::rdf(file, o), p2m::rdf(file, o + 4), p2m::rdf(file, o + 8)}});
                for (uint32_t k = 0; k < nr; ++k, o += 20)
                    tr.rot.push_back({p2m::rdf(file, o + 16), {p2m::rdf(file, o), p2m::rdf(file, o + 4), p2m::rdf(file, o + 8), p2m::rdf(file, o + 12)}});
                for (uint32_t k = 0; k < ns; ++k, o += 16) tr.scale.push_back({p2m::rdf(file, o + 12), {p2m::rdf(file, o), p2m::rdf(file, o + 4), p2m::rdf(file, o + 8)}});
                o += static_cast<size_t>(nsr) * 20;  // scale rotation keys: not used
                if (!tr.pos.empty()) m.duration = std::max(m.duration, tr.pos.back().t);
                if (!tr.rot.empty()) m.duration = std::max(m.duration, tr.rot.back().t);
                if (!tr.scale.empty()) m.duration = std::max(m.duration, tr.scale.back().t);
                m.tracks.push_back(std::move(tr));
            }
            return m;
        }
        if (size > file.size() || p + 12 + size > file.size()) break;
        p += 12 + size;
    }
    throw Error("the motion has no sub-motions");
}

// ---- posing -------------------------------------------------------------------------------------------------------------------------------------

namespace {

// 1: the matrix of a quaternion turns vectors the way the engine's skeletons need (checked by the bounding boxes of animated actors, unit test motion_keeps_actors_together)
constexpr bool kConjugate = false;

Mat34 local_matrix(const Xform& x) {
    float qx = x.rot[0], qy = x.rot[1], qz = x.rot[2], qw = x.rot[3];
    if (kConjugate) qx = -qx, qy = -qy, qz = -qz;
    const float n = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (n > 1e-12f) qx /= n, qy /= n, qz /= n, qw /= n;
    else qx = qy = qz = 0, qw = 1;
    const float xx = qx * qx, yy = qy * qy, zz = qz * qz, xy = qx * qy, xz = qx * qz, yz = qy * qz, wx = qw * qx, wy = qw * qy, wz = qw * qz;
    const float r[9] = {1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy), 2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx), 2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy)};
    Mat34 m{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) m[static_cast<size_t>(row * 4 + col)] = r[row * 3 + col] * x.scale[col];
        m[static_cast<size_t>(row * 4 + 3)] = x.pos[row];
    }
    return m;
}

Mat34 mul(const Mat34& a, const Mat34& b) {  // a * b
    Mat34 o{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = a[static_cast<size_t>(r * 4 + 0)] * b[static_cast<size_t>(0 * 4 + c)] + a[static_cast<size_t>(r * 4 + 1)] * b[static_cast<size_t>(1 * 4 + c)] +
                      a[static_cast<size_t>(r * 4 + 2)] * b[static_cast<size_t>(2 * 4 + c)];
            if (c == 3) s += a[static_cast<size_t>(r * 4 + 3)];
            o[static_cast<size_t>(r * 4 + c)] = s;
        }
    return o;
}

Mat34 invert(const Mat34& m) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    float det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-20f) det = det < 0 ? -1e-20f : 1e-20f;
    const float inv = 1.0f / det;
    Mat34 o{};
    o[0] = A * inv;
    o[1] = -(b * i - c * h) * inv;
    o[2] = (b * f - c * e) * inv;
    o[4] = B * inv;
    o[5] = (a * i - c * g) * inv;
    o[6] = -(a * f - c * d) * inv;
    o[8] = C * inv;
    o[9] = -(a * h - b * g) * inv;
    o[10] = (a * e - b * d) * inv;
    for (int r = 0; r < 3; ++r) o[static_cast<size_t>(r * 4 + 3)] = -(o[static_cast<size_t>(r * 4)] * m[3] + o[static_cast<size_t>(r * 4 + 1)] * m[7] + o[static_cast<size_t>(r * 4 + 2)] * m[11]);
    return o;
}

std::vector<Mat34> globals(const Skeleton& s, const std::vector<Xform>& local) {
    const size_t n = s.names.size();
    std::vector<Mat34> g(n);
    std::vector<char> state(n, 0);  // 0 not yet, 1 in progress, 2 done
    for (size_t start = 0; start < n; ++start) {
        // walk up to a finished ancestor (or the root), then down again; a parent loop in broken data is cut
        std::vector<size_t> chain;
        for (size_t i = start; state[i] == 0;) {
            state[i] = 1;
            chain.push_back(i);
            const int par = s.parent[i];
            if (par < 0 || static_cast<size_t>(par) >= n || state[static_cast<size_t>(par)] != 0) break;
            i = static_cast<size_t>(par);
        }
        for (size_t k = chain.size(); k-- > 0;) {
            const size_t i = chain[k];
            const int par = s.parent[i];
            const Mat34 lm = local_matrix(local[i]);
            g[i] = par >= 0 && static_cast<size_t>(par) < n && state[static_cast<size_t>(par)] == 2 ? mul(g[static_cast<size_t>(par)], lm) : lm;
            state[i] = 2;
        }
    }
    return g;
}

void nlerp(const float* a, const float* b, float f, float* out) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    const float sgn = dot < 0 ? -1.0f : 1.0f;
    float n = 0;
    for (int i = 0; i < 4; ++i) {
        out[i] = a[i] * (1 - f) + b[i] * sgn * f;
        n += out[i] * out[i];
    }
    n = std::sqrt(n);
    if (n > 1e-12f)
        for (int i = 0; i < 4; ++i) out[i] /= n;
}

// the keys around `t` (looped over `duration`): the index of the earlier one and the fraction towards the next
template <class K>
void bracket(const std::vector<K>& keys, float t, size_t& i0, size_t& i1, float& f) {
    if (keys.size() == 1 || t <= keys.front().t) {
        i0 = i1 = 0;
        f = 0;
        return;
    }
    if (t >= keys.back().t) {
        i0 = i1 = keys.size() - 1;
        f = 0;
        return;
    }
    size_t lo = 0, hi = keys.size() - 1;
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        (keys[mid].t <= t ? lo : hi) = mid;
    }
    i0 = lo;
    i1 = hi;
    const float span = keys[hi].t - keys[lo].t;
    f = span > 1e-9f ? (t - keys[lo].t) / span : 0.0f;
}

}  // namespace

std::vector<Xform> motion_pose(const Skeleton& s, const Motion* motion, float t) {
    std::vector<Xform> local = s.bind;
    if (!motion) return local;
    if (motion->duration > 1e-6f) {
        t = std::fmod(t, motion->duration);
        if (t < 0) t += motion->duration;
    } else {
        t = 0;
    }
    std::unordered_map<std::string, size_t> idx;
    for (size_t i = 0; i < s.names.size(); ++i) idx.emplace(s.names[i], i);
    for (const MotionTrack& tr : motion->tracks) {
        auto it = idx.find(tr.name);
        if (it == idx.end()) continue;
        Xform& x = local[it->second];
        size_t i0, i1;
        float f;
        if (!tr.rot.empty()) {
            bracket(tr.rot, t, i0, i1, f);
            nlerp(tr.rot[i0].q, tr.rot[i1].q, f, x.rot);
        }
        if (!tr.pos.empty()) {
            bracket(tr.pos, t, i0, i1, f);
            for (int k = 0; k < 3; ++k) x.pos[k] = tr.pos[i0].v[k] * (1 - f) + tr.pos[i1].v[k] * f;
        }
        if (!tr.scale.empty()) {
            bracket(tr.scale, t, i0, i1, f);
            for (int k = 0; k < 3; ++k) x.scale[k] = tr.scale[i0].v[k] * (1 - f) + tr.scale[i1].v[k] * f;
        }
    }
    return local;
}

std::vector<Mat34> skin_palette(const Skeleton& s, const std::vector<Xform>& local) {
    const std::vector<Mat34> now = globals(s, local), bind = globals(s, s.bind);
    std::vector<Mat34> out(now.size());
    for (size_t i = 0; i < now.size(); ++i) out[i] = mul(now[i], invert(bind[i]));
    return out;
}

void skin_vertices(const ActorSkin& skin, const std::vector<Mat34>& palette, const std::string& bind, std::string& out) {
    out = bind;
    const size_t count = bind.size() / 32;
    if (skin.bone.size() != count || skin.weight.size() != count) return;
    const auto* src = reinterpret_cast<const float*>(bind.data());
    auto* dst = reinterpret_cast<float*>(out.data());
    for (size_t v = 0; v < count; ++v) {
        Mat34 m{};
        float total = 0;
        for (int k = 0; k < 4; ++k) {
            const float w = skin.weight[v][static_cast<size_t>(k)];
            const size_t b = skin.bone[v][static_cast<size_t>(k)];
            if (w <= 0 || b >= palette.size()) continue;
            for (size_t j = 0; j < 12; ++j) m[j] += palette[b][j] * w;
            total += w;
        }
        if (total <= 0) continue;  // not skinned: stays where it is
        const float* p = src + v * 8;
        float* o = dst + v * 8;
        for (int r = 0; r < 3; ++r) {
            const size_t R = static_cast<size_t>(r * 4);
            o[r] = (m[R] * p[0] + m[R + 1] * p[1] + m[R + 2] * p[2] + m[R + 3]) / total;  // the matrix is the weighted sum: the translation is already weighted
            o[3 + r] = (m[R] * p[3] + m[R + 1] * p[4] + m[R + 2] * p[5]) / total;
        }
        const float n = std::sqrt(o[3] * o[3] + o[4] * o[4] + o[5] * o[5]);
        if (n > 1e-9f) o[3] /= n, o[4] /= n, o[5] /= n;
    }
}

}  // namespace mk
