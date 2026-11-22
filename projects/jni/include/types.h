#pragma once

#include <cmath>
#include <limits>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

namespace Angle {
    constexpr float DegToRad = PI / 180.0f;
    constexpr float RadToDeg = 180.0f / PI;

    inline float ToRadians(float deg) { return deg * DegToRad; }

    inline float ToDegrees(float rad) { return rad * RadToDeg; }

    inline float Normalize(float deg) {
        float v = std::fmod(deg, 360.0f);
        if (v < 0.0f) v += 360.0f;
        return v;
    }
}

struct Vec2 {
    float x, y;

    Vec2() : x(0.0f), y(0.0f) {}

    Vec2(float _x, float _y) : x(_x), y(_y) {}

    Vec2 operator+(const Vec2 &v) const { return {x + v.x, y + v.y}; }

    Vec2 operator-(const Vec2 &v) const { return {x - v.x, y - v.y}; }

    Vec2 operator*(float v) const { return {x * v, y * v}; }

    Vec2 operator/(float v) const { return {x / v, y / v}; }

    bool operator==(const Vec2 &v) const { return x == v.x && y == v.y; }

    bool operator!=(const Vec2 &v) const { return !(*this == v); }

    bool IsFinite() const { return std::isfinite(x) && std::isfinite(y); }

    static Vec2 Zero() { return {0.0f, 0.0f}; }

    static Vec2 Invalid() {
        return {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::quiet_NaN()};
    }

    static float Dot(const Vec2 &a, const Vec2 &b) { return a.x * b.x + a.y * b.y; }

    float LengthSquared() const { return x * x + y * y; }

    float Length() const { return std::sqrt(LengthSquared()); }
};

struct Vec3 {
    float x, y, z;

    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}

    Vec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    Vec3 operator+(const Vec3 &v) const { return {x + v.x, y + v.y, z + v.z}; }

    Vec3 operator-(const Vec3 &v) const { return {x - v.x, y - v.y, z - v.z}; }

    Vec3 operator*(float v) const { return {x * v, y * v, z * v}; }

    Vec3 operator/(float v) const { return {x / v, y / v, z / v}; }

    Vec3 operator-() const { return {-x, -y, -z}; }

    Vec3 &operator+=(const Vec3 &v) { x += v.x; y += v.y; z += v.z; return *this; }

    Vec3 &operator-=(const Vec3 &v) { x -= v.x; y -= v.y; z -= v.z; return *this; }

    bool operator==(const Vec3 &v) const { return x == v.x && y == v.y && z == v.z; }

    bool operator!=(const Vec3 &v) const { return !(*this == v); }

    bool IsFinite() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    bool IsZero() const { return x == 0.0f && y == 0.0f && z == 0.0f; }

    static Vec3 Zero() { return {0.0f, 0.0f, 0.0f}; }

    static float Dot(const Vec3 &a, const Vec3 &b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static Vec3 Cross(const Vec3 &a, const Vec3 &b) {
        return {a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
    }

    static float Distance(const Vec3 &a, const Vec3 &b) { return (a - b).Length(); }

    static float DistanceSquared(const Vec3 &a, const Vec3 &b) {
        return (a - b).LengthSquared();
    }

    float LengthSquared() const { return x * x + y * y + z * z; }

    float Length() const { return std::sqrt(LengthSquared()); }

    float HorizontalLength() const { return std::sqrt(x * x + y * y); }

    Vec3 Normalized() const {
        const float len = Length();
        if (len <= 0.0f) return Zero();
        return {x / len, y / len, z / len};
    }
};

struct Rotator {
    float Pitch = 0.0f;
    float Yaw = 0.0f;
    float Roll = 0.0f;

    Rotator() = default;

    Rotator(float pitch, float yaw, float roll) : Pitch(pitch), Yaw(yaw), Roll(roll) {}

    bool IsFinite() const {
        return std::isfinite(Pitch) && std::isfinite(Yaw) && std::isfinite(Roll);
    }

    static Rotator Zero() { return {}; }
};

struct Matrix {
    float M[4][4] = {};

    float *operator[](int index) { return M[index]; }

    const float *operator[](int index) const { return M[index]; }

    static Matrix Identity() {
        Matrix m;
        m.M[0][0] = m.M[1][1] = m.M[2][2] = m.M[3][3] = 1.0f;
        return m;
    }
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    float Magnitude() const { return std::sqrt(x * x + y * y + z * z + w * w); }
};

struct Transform {
    Quat Rotation;
    Vec3 Translation;
    float chunk = 0.0f;
    Vec3 Scale3D;
};
