#pragma once
#include <vector>
#include <cmath>
#include "roblox.hpp"
#include "math.hpp"

namespace aimbot {
    void start_thread();

    inline bool visible_heuristic(Vec3 cam_pos, Vec3 target_pos,
                                  const std::vector<PlayerInfo>& players,
                                  const Matrix4& vm, Vec2 screen) {
        Vec2 s = world_to_screen(vm, target_pos, screen.x, screen.y);
        if (s.x < 0.f && s.y < 0.f) return false;
        if (s.x < 0.f || s.x > screen.x || s.y < 0.f || s.y > screen.y) return false;
        float tdist = (target_pos - cam_pos).length();
        for (const auto& op : players) {
            if (!op.valid) continue;
            if ((op.position - target_pos).length() < 0.5f) continue;
            float odist = (op.position - cam_pos).length();
            if (odist + 3.f >= tdist) continue;
            Vec2 os = world_to_screen(vm, op.position, screen.x, screen.y);
            if (os.x < 0.f && os.y < 0.f) continue;
            if (fabsf(s.x - os.x) < 25.f && fabsf(s.y - os.y) < 60.f) return false;
        }
        return true;
    }
}
