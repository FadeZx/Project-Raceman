#pragma once

#include <cmath>
#include <array>
#include <ostream>

namespace raceman::physics
{

struct Vector3
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    Vector3() = default;
    Vector3(float px, float py, float pz) : x(px), y(py), z(pz) {}

    Vector3 operator+(const Vector3 &rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    Vector3 operator-(const Vector3 &rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    Vector3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    Vector3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }

    Vector3 &operator+=(const Vector3 &rhs)
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }

    Vector3 &operator-=(const Vector3 &rhs)
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }

    Vector3 &operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    Vector3 &operator/=(float scalar)
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }
};

inline Vector3 operator*(float scalar, const Vector3 &v)
{
    return {v.x * scalar, v.y * scalar, v.z * scalar};
}

inline float dot(const Vector3 &a, const Vector3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vector3 cross(const Vector3 &a, const Vector3 &b)
{
    return Vector3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

inline float length(const Vector3 &v)
{
    return std::sqrt(dot(v, v));
}

inline Vector3 normalize(const Vector3 &v)
{
    float len = length(v);
    if (len < 1e-5f)
    {
        return {0.0f, 0.0f, 0.0f};
    }
    return v / len;
}

struct Quaternion
{
    float w{1.0f};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    Quaternion() = default;
    Quaternion(float pw, float px, float py, float pz) : w(pw), x(px), y(py), z(pz) {}

    static Quaternion identity() { return {1.0f, 0.0f, 0.0f, 0.0f}; }

    static Quaternion fromAxisAngle(const Vector3 &axis, float angle)
    {
        float halfAngle = angle * 0.5f;
        float s = std::sin(halfAngle);
        return {std::cos(halfAngle), axis.x * s, axis.y * s, axis.z * s};
    }

    Quaternion operator*(const Quaternion &rhs) const
    {
        return {
            w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
            w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
            w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
            w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w};
    }

    Vector3 rotate(const Vector3 &v) const
    {
        Vector3 qv{x, y, z};
        Vector3 t = 2.0f * cross(qv, v);
        return v + w * t + cross(qv, t);
    }

    Quaternion normalized() const
    {
        float len = std::sqrt(w * w + x * x + y * y + z * z);
        if (len < 1e-6f)
        {
            return Quaternion::identity();
        }
        float inv = 1.0f / len;
        return {w * inv, x * inv, y * inv, z * inv};
    }
};

struct Transform
{
    Vector3 position{};
    Quaternion rotation{};
};

inline std::ostream &operator<<(std::ostream &os, const Vector3 &v)
{
    os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    return os;
}

} // namespace raceman::physics

