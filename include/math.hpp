#pragma once
#include <cmath>
#include <cstdint>

struct Vec2 {
    float x = 0.f, y = 0.f;
    Vec2 operator+(Vec2 o) const { return {x+o.x, y+o.y}; }
    Vec2 operator-(Vec2 o) const { return {x-o.x, y-o.y}; }
    Vec2 operator*(float s) const { return {x*s, y*s}; }
    float length() const { return sqrtf(x*x + y*y); }
};

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
    Vec3 operator+(Vec3 o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator-(Vec3 o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator*(float s) const { return {x*s,   y*s,   z*s  }; }
    float length() const { return sqrtf(x*x + y*y + z*z); }
    float dot(Vec3 o) const { return x*o.x + y*o.y + z*o.z; }
};

using Matrix4 = float[16];

inline Vec2 world_to_screen(const Matrix4& vm, Vec3 w, float sw, float sh) {
    float x = vm[0]*w.x + vm[1]*w.y + vm[2] *w.z + vm[3];
    float y = vm[4]*w.x + vm[5]*w.y + vm[6] *w.z + vm[7];
    float ww= vm[12]*w.x + vm[13]*w.y + vm[14]*w.z + vm[15];
    if (ww < 0.01f) return {-1.f, -1.f};
    float inv = 1.f / ww;
    return {
        (1.f + x * inv) * sw * 0.5f,
        (1.f - y * inv) * sh * 0.5f
    };
}

inline bool on_screen(Vec2 p, float sw, float sh) {
    return p.x >= 0.f && p.x <= sw && p.y >= 0.f && p.y <= sh;
}
