#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace fluid {

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3f() = default;
    constexpr Vec3f(float x_, float y_, float z_)
        : x(x_)
        , y(y_)
        , z(z_)
    {
    }

    constexpr float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }

    constexpr Vec3f operator+(Vec3f o) const { return { x + o.x, y + o.y, z + o.z }; }
    constexpr Vec3f operator-(Vec3f o) const { return { x - o.x, y - o.y, z - o.z }; }
    constexpr Vec3f operator*(float s) const { return { x * s, y * s, z * s }; }
    constexpr Vec3f operator/(float s) const { return { x / s, y / s, z / s }; }
    constexpr Vec3f operator-() const { return { -x, -y, -z }; }
    constexpr Vec3f& operator+=(Vec3f o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr Vec3f& operator-=(Vec3f o)
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    constexpr Vec3f& operator*=(float s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    constexpr bool operator==(const Vec3f&) const = default;
};

constexpr Vec3f operator*(float s, Vec3f v) { return v * s; }
constexpr float dot(Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3f cross(Vec3f a, Vec3f b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
constexpr Vec3f cwiseMin(Vec3f a, Vec3f b) { return { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) }; }
constexpr Vec3f cwiseMax(Vec3f a, Vec3f b) { return { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) }; }
constexpr Vec3f lerp(Vec3f a, Vec3f b, float t) { return a + (b - a) * t; }
inline float length(Vec3f v) { return std::sqrt(dot(v, v)); }
inline Vec3f normalized(Vec3f v)
{
    const float len = length(v);
    return len > 0.0f ? v / len : Vec3f {};
}

constexpr float radians(float degrees) { return degrees * std::numbers::pi_v<float> / 180.0f; }

/** Axis-aligned bounding box. Default-constructed boxes are empty (invalid). */
struct Aabb {
    Vec3f min { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    Vec3f max { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

    constexpr bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    constexpr void extend(Vec3f p)
    {
        min = cwiseMin(min, p);
        max = cwiseMax(max, p);
    }
    constexpr void extend(const Aabb& o)
    {
        if (o.valid()) {
            extend(o.min);
            extend(o.max);
        }
    }
    constexpr Vec3f center() const { return (min + max) * 0.5f; }
    constexpr Vec3f size() const { return valid() ? max - min : Vec3f {}; }
    constexpr bool contains(Vec3f p) const
    {
        return p.x >= min.x && p.y >= min.y && p.z >= min.z && p.x <= max.x && p.y <= max.y && p.z <= max.z;
    }
};

/** Row-major 3x4 affine transform (rotation/scale in the 3x3 part, translation in column 3). */
struct Affine3f {
    float m[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };

    static constexpr Affine3f identity() { return {}; }
    static constexpr Affine3f translation(Vec3f t)
    {
        Affine3f a;
        a.m[0][3] = t.x;
        a.m[1][3] = t.y;
        a.m[2][3] = t.z;
        return a;
    }
    static constexpr Affine3f scaling(float s)
    {
        Affine3f a;
        a.m[0][0] = a.m[1][1] = a.m[2][2] = s;
        return a;
    }
    /** Right-handed rotation about +Y (angle in radians). */
    static Affine3f rotationY(float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        Affine3f a;
        a.m[0][0] = c;
        a.m[0][2] = s;
        a.m[2][0] = -s;
        a.m[2][2] = c;
        return a;
    }

    constexpr Affine3f operator*(const Affine3f& o) const
    {
        Affine3f r;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 4; ++j) {
                float v = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
                if (j == 3) {
                    v += m[i][3];
                }
                r.m[i][j] = v;
            }
        }
        return r;
    }

    constexpr Vec3f transformPoint(Vec3f p) const
    {
        return { m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
            m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
            m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3] };
    }
    constexpr Vec3f transformVector(Vec3f v) const
    {
        return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
    }
};

} // namespace fluid
