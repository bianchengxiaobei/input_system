#pragma once

#include <cmath>

namespace insys {

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vector2() = default;
    constexpr Vector2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vector2 operator+(const Vector2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vector2 operator-(const Vector2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vector2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vector2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vector2& operator+=(const Vector2& o) { x += o.x; y += o.y; return *this; }
    constexpr Vector2& operator-=(const Vector2& o) { x -= o.x; y -= o.y; return *this; }

    constexpr float dot(const Vector2& o) const { return x * o.x + y * o.y; }
    float length() const { return std::sqrt(x * x + y * y); }
    constexpr bool is_zero() const { return x == 0.0f && y == 0.0f; }

    constexpr bool operator==(const Vector2& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Vector2& o) const { return !(*this == o); }
};

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vector3() = default;
    constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vector3 operator+(const Vector3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vector3 operator-(const Vector3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vector3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr float dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vector3 cross(const Vector3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    constexpr bool is_zero() const { return x == 0.0f && y == 0.0f && z == 0.0f; }
    constexpr bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vector3& o) const { return !(*this == o); }
};

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quaternion() = default;
    constexpr Quaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quaternion from_axis_angle(Vector3 axis, float radians) {
        const float length = axis.length();
        if (length <= 1e-6f)
            return {};
        const float half = radians * 0.5f;
        const float scale = std::sin(half) / length;
        return {axis.x * scale, axis.y * scale, axis.z * scale, std::cos(half)};
    }

    constexpr Quaternion operator*(const Quaternion& o) const {
        return {
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y + y * o.w + z * o.x - x * o.z,
            w * o.z + z * o.w + x * o.y - y * o.x,
            w * o.w - x * o.x - y * o.y - z * o.z,
        };
    }

    Vector3 rotate(const Vector3& value) const {
        const Vector3 axis{x, y, z};
        const Vector3 twiceCross = axis.cross(value) * 2.0f;
        return value + twiceCross * w + axis.cross(twiceCross);
    }
};

struct Vector2i {
    int x = 0;
    int y = 0;

    constexpr Vector2i() = default;
    constexpr Vector2i(int x_, int y_) : x(x_), y(y_) {}
};

} // namespace insys
