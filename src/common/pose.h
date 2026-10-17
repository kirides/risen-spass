// An entity's pose: the engine's world matrix (bCMatrix, 4x4 floats, row-major, D3D row vectors: row 0 = right, row 1 = up, row 2 = forward,
// row 3 = position; +y is up; RE.md sections 38, 49) and the angles the Lua API and risen-modkit use for it. Header-only, shared by the layer
// (Entity:pose / set_pose) and the editor, so both turn angles into the same matrix.
//
// Angles in degrees: yaw 0 looks along +z, 90 along +x (Entity:direction's convention); pitch > 0 tilts the forward axis up; roll > 0 lifts the
// right side. Rows keep their length (an entity's scale) when the angles change.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace spass::pose {

using Matrix = std::array<float, 16>;

struct Angles {
    float yaw = 0, pitch = 0, roll = 0;
};

namespace detail {
constexpr double kPi = 3.14159265358979323846;
inline double rad(double d) { return d * kPi / 180.0; }
inline double deg(double r) { return r * 180.0 / kPi; }
inline double len3(const float* v) { return std::sqrt(double(v[0]) * v[0] + double(v[1]) * v[1] + double(v[2]) * v[2]); }
}  // namespace detail

// the rotation rows for the angles: right, up, forward (unit length)
inline void axes(const Angles& a, double right[3], double up[3], double fwd[3]) {
    using namespace detail;
    const double y = rad(a.yaw), p = rad(a.pitch), r = rad(a.roll);
    fwd[0] = std::sin(y) * std::cos(p);
    fwd[1] = std::sin(p);
    fwd[2] = std::cos(y) * std::cos(p);
    const double rf[3] = {std::cos(y), 0.0, -std::sin(y)};  // the right axis without roll (horizontal)
    // up without roll = forward x right (left-handed: yaw 0 gives (0, 1, 0))
    const double uf[3] = {fwd[1] * rf[2] - fwd[2] * rf[1], fwd[2] * rf[0] - fwd[0] * rf[2], fwd[0] * rf[1] - fwd[1] * rf[0]};
    for (int i = 0; i < 3; ++i) {
        right[i] = std::cos(r) * rf[i] + std::sin(r) * uf[i];
        up[i] = -std::sin(r) * rf[i] + std::cos(r) * uf[i];
    }
}

// a matrix for a position and angles; `scale` (per row) keeps an entity's size
inline Matrix compose(float x, float y, float z, const Angles& a, const std::array<float, 3>& scale = {1, 1, 1}) {
    double r[3], u[3], f[3];
    axes(a, r, u, f);
    Matrix m{};
    for (int i = 0; i < 3; ++i) {
        m[0 + i] = static_cast<float>(r[i] * scale[0]);
        m[4 + i] = static_cast<float>(u[i] * scale[1]);
        m[8 + i] = static_cast<float>(f[i] * scale[2]);
    }
    m[12] = x;
    m[13] = y;
    m[14] = z;
    m[15] = 1;
    return m;
}

inline std::array<float, 3> scale_of(const Matrix& m) {
    return {static_cast<float>(detail::len3(&m[0])), static_cast<float>(detail::len3(&m[4])), static_cast<float>(detail::len3(&m[8]))};
}

// the angles of a matrix (rows may be scaled); compose(decompose(m)) gives the rotation back for any matrix without shear
inline Angles decompose(const Matrix& m) {
    using namespace detail;
    const auto s = scale_of(m);
    double f[3], r[3];
    for (int i = 0; i < 3; ++i) {
        f[i] = s[2] > 0 ? m[8 + i] / s[2] : 0;
        r[i] = s[0] > 0 ? m[0 + i] / s[0] : 0;
    }
    Angles a;
    a.pitch = static_cast<float>(deg(std::asin(std::clamp(f[1], -1.0, 1.0))));
    if (std::fabs(f[1]) < 0.9999) {
        a.yaw = static_cast<float>(deg(std::atan2(f[0], f[2])));
    } else {
        a.yaw = static_cast<float>(deg(std::atan2(-r[2], r[0])));  // looking straight up / down: the yaw is the right axis's
    }
    double rf[3], uf[3], fwd[3];
    axes(Angles{a.yaw, a.pitch, 0}, rf, uf, fwd);
    a.roll = static_cast<float>(deg(std::atan2(r[0] * uf[0] + r[1] * uf[1] + r[2] * uf[2], r[0] * rf[0] + r[1] * rf[1] + r[2] * rf[2])));
    if (a.yaw < 0) a.yaw += 360.0f;
    return a;
}

// finite and of a sane size (the physics SDK returns garbage beyond 1e7, NaN hangs searches)
inline bool sane(const Matrix& m) {
    for (float v : m)
        if (!std::isfinite(v) || std::fabs(v) > 1.0e7f) return false;
    return true;
}

}  // namespace spass::pose
