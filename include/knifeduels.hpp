#pragma once

#include "esp.hpp"
#include "roblox.hpp"
#include "memory.hpp"
#include "offsets.hpp"
#include "math.hpp"
#include "features.hpp"
#include "games.hpp"
#include "menu.hpp"
#include "log.hpp"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cfloat>

extern Matrix4 g_view_matrix;
extern Vec2    g_screen;

namespace knifeduels {

inline std::atomic<int> g_curve_knives{0};
inline std::atomic<int> g_expanded_hitboxes{0};
inline std::atomic<int> g_auto_throws{0};

struct KnifeState {
    uintptr_t prim = 0;
};

inline std::unordered_map<uintptr_t, KnifeState> g_knives;
inline std::unordered_map<uintptr_t, uintptr_t> g_hitbox_cache;
inline std::unordered_map<uintptr_t, Vec3>      g_orig_sizes;

inline uintptr_t g_ws_cache = 0;
inline uintptr_t ensure_workspace() {
    if (g_ws_cache) return g_ws_cache;
    RbxDataModel dm = RbxDataModel::get();
    if (!dm.valid()) return 0;
    g_ws_cache = dm.get_service("Workspace").ptr;
    return g_ws_cache;
}

inline void reset_caches() {
    g_ws_cache = 0;
    g_knives.clear();
    g_hitbox_cache.clear();
    g_orig_sizes.clear();
    g_curve_knives.store(0);
    g_expanded_hitboxes.store(0);
}

inline uintptr_t resolve_model_prim(uintptr_t model_ptr) {
    if (!model_ptr || !offsets::BasePart::Primitive) return 0;
    RbxInstance m{model_ptr};
    for (auto& c : m.get_children()) {
        std::string cls = c.get_class();
        if (cls != "Part" && cls != "MeshPart") continue;
        uintptr_t prim = rpm<uintptr_t>(c.ptr + offsets::BasePart::Primitive);
        if (prim) return prim;
    }
    return 0;
}

inline void steer_knives(const std::vector<PlayerInfo>& players,
                         uintptr_t local_hrp, uintptr_t local_team) {
    if (!offsets::Primitive::AssemblyLinearVelocity ||
        !offsets::Primitive::Position) return;
    uintptr_t ws = ensure_workspace();
    if (!ws) return;

    std::unordered_set<uintptr_t> live;
    for (auto& c : RbxInstance{ws}.get_children()) {
        if (c.get_name() != "ThrownKnife") continue;
        if (c.get_class() != "Model") continue;
        live.insert(c.ptr);
    }
    for (auto it = g_knives.begin(); it != g_knives.end();) {
        if (!live.count(it->first)) it = g_knives.erase(it);
        else ++it;
    }

    int hooked = 0;
    for (uintptr_t model_ptr : live) {
        auto& st = g_knives[model_ptr];
        if (!st.prim) st.prim = resolve_model_prim(model_ptr);
        if (!st.prim) continue;

        Vec3 kpos = rpm<Vec3>(st.prim + offsets::Primitive::Position);
        Vec3 kvel = rpm<Vec3>(st.prim + offsets::Primitive::AssemblyLinearVelocity);
        float speed = kvel.length();
        if (speed < 5.f) continue;

        const PlayerInfo* best = nullptr;
        float best_d = FLT_MAX;
        for (const auto& p : players) {
            if (!p.valid || p.health <= 0.f) continue;
            if (esp::cfg.team_check && p.team_ptr && local_team &&
                p.team_ptr == local_team) continue;
            if (p.hrp_ptr && local_hrp && p.hrp_ptr == local_hrp) continue;
            float d = (p.head_pos - kpos).length();
            if (d < best_d) { best_d = d; best = &p; }
        }
        if (!best) continue;

        Vec3 to = best->head_pos - kpos;
        float dist = to.length();
        if (dist < 0.01f) continue;
        Vec3 dir{ to.x / dist, to.y / dist, to.z / dist };
        Vec3 nvel{ dir.x * speed, dir.y * speed, dir.z * speed };
        wpm<Vec3>(st.prim + offsets::Primitive::AssemblyLinearVelocity, nvel);
        hooked++;
    }
    g_curve_knives.store(hooked);
}

inline void expand_hitboxes(const std::vector<PlayerInfo>& players,
                            uintptr_t local_char_ptr, uintptr_t local_team) {
    if (!offsets::BasePart::Primitive || !offsets::Primitive::Size) return;

    float sz = esp::cfg.kd_hitbox_size > 1.f ? esp::cfg.kd_hitbox_size : 15.f;
    Vec3 huge{ sz, sz, sz };

    int count = 0;
    for (const auto& p : players) {
        if (!p.valid || p.health <= 0.f) continue;
        if (!p.cached_char_ptr) continue;
        if (local_char_ptr && p.cached_char_ptr == local_char_ptr) continue;
        if (esp::cfg.team_check && p.team_ptr && local_team &&
            p.team_ptr == local_team) continue;

        uintptr_t hb_part = 0;
        auto it = g_hitbox_cache.find(p.cached_char_ptr);
        if (it != g_hitbox_cache.end()) {
            hb_part = it->second;
        } else {
            RbxInstance ch{p.cached_char_ptr};
            RbxInstance hb = ch.find_child_by_name("Hitbox");
            if (hb.valid()) hb_part = hb.ptr;
            g_hitbox_cache[p.cached_char_ptr] = hb_part;
        }
        uintptr_t target_part = hb_part ? hb_part : p.hrp_ptr;
        if (!target_part) continue;
        uintptr_t prim = rpm<uintptr_t>(target_part + offsets::BasePart::Primitive);
        if (!prim) continue;
        uintptr_t sz_addr = prim + offsets::Primitive::Size;

        if (!g_orig_sizes.count(sz_addr)) {
            g_orig_sizes[sz_addr] = rpm<Vec3>(sz_addr);
        }
        wpm<Vec3>(sz_addr, huge);
        count++;
    }
    g_expanded_hitboxes.store(count);
}

inline void restore_hitboxes() {
    for (auto& kv : g_orig_sizes) wpm<Vec3>(kv.first, kv.second);
    g_orig_sizes.clear();
    g_hitbox_cache.clear();
    g_expanded_hitboxes.store(0);
}

inline bool head_in_fov_visible(const std::vector<PlayerInfo>& players,
                                const Matrix4& vm, Vec2 screen,
                                uintptr_t local_char_ptr,
                                uintptr_t local_team,
                                Vec3 cam_pos, float fov_px, float max_dist) {
    if (screen.x < 2.f || screen.y < 2.f) return false;
    Vec2 center{ screen.x * 0.5f, screen.y * 0.5f };
    const PlayerInfo* best = nullptr;
    float best_d = FLT_MAX;
    for (const auto& p : players) {
        if (!p.valid || p.health <= 0.f) continue;
        if (local_char_ptr && p.cached_char_ptr == local_char_ptr) continue;
        if (esp::cfg.team_check && p.team_ptr && local_team &&
            p.team_ptr == local_team) continue;
        float wd = (p.head_pos - cam_pos).length();
        if (wd > max_dist) continue;
        Vec2 s = world_to_screen(vm, p.head_pos, screen.x, screen.y);
        if (s.x < 0.f && s.y < 0.f) continue;
        Vec2 d = s - center;
        float dd = d.length();
        if (dd > fov_px) continue;
        if (dd < best_d) { best_d = dd; best = &p; }
    }
    return best != nullptr;
}

inline void send_click_once() {
    INPUT in[2]{};
    in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
}

inline void thread_body() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    auto next_throw = std::chrono::steady_clock::now();
    bool  prev_expand_on = false;
    bool  prev_curve_on  = false;
    games::Id prev_game = games::Id::Universal;

    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        if (!mem::alive()) break;

        games::Id g = games::active();
        if (g != prev_game) {
            if (prev_expand_on) restore_hitboxes();
            reset_caches();
            prev_expand_on = false;
            prev_curve_on  = false;
            prev_game = g;
        }
        if (g != games::Id::KnifeDuels) {
            if (prev_expand_on) { restore_hitboxes(); prev_expand_on = false; }
            continue;
        }
        if (!menu::is_injected()) {
            if (prev_expand_on) { restore_hitboxes(); prev_expand_on = false; }
            continue;
        }

        std::vector<PlayerInfo> players;
        Matrix4 vm;
        Vec2 screen;
        {
            std::lock_guard<std::mutex> lock(g_players_mtx);
            players = g_players;
            std::copy(std::begin(g_view_matrix), std::end(g_view_matrix), std::begin(vm));
            screen = g_screen;
        }

        uintptr_t lp = feat::local_player_ptr();
        uintptr_t local_char = 0, local_hrp = 0, local_team = 0;
        if (lp && offsets::Player::Character) {
            local_char = rpm<uintptr_t>(lp + offsets::Player::Character);
            if (local_char) {
                RbxInstance ch{local_char};
                local_hrp = ch.find_child_by_name("HumanoidRootPart").ptr;
            }
            if (offsets::Player::Team)
                local_team = rpm<uintptr_t>(lp + offsets::Player::Team);
        }

        if (esp::cfg.kd_knife_curve) {
            steer_knives(players, local_hrp, local_team);
            prev_curve_on = true;
        } else if (prev_curve_on) {
            g_knives.clear();
            g_curve_knives.store(0);
            prev_curve_on = false;
        }

        if (esp::cfg.kd_hitbox_expand) {
            expand_hitboxes(players, local_char, local_team);
            prev_expand_on = true;
        } else if (prev_expand_on) {
            restore_hitboxes();
            prev_expand_on = false;
        }

        if (esp::cfg.kd_auto_throw) {
            auto now = std::chrono::steady_clock::now();
            if (now >= next_throw) {
                Vec3 cam_pos{};
                {
                    Vec3 r0{ vm[0], vm[1], vm[2] };
                    Vec3 r1{ vm[4], vm[5], vm[6] };
                    Vec3 r2{ vm[12], vm[13], vm[14] };
                    float tx = vm[3], ty = vm[7], tz = vm[15];
                    float sx = r0.length(), sy = r1.length();
                    if (sx > 0.0001f && sy > 0.0001f) {
                        Vec3 Rx{ r0.x/sx, r0.y/sx, r0.z/sx };
                        Vec3 Ry{ r1.x/sy, r1.y/sy, r1.z/sy };
                        float tx_s = tx / sx, ty_s = ty / sy;
                        cam_pos.x = -(Rx.x*tx_s + Ry.x*ty_s + r2.x*tz);
                        cam_pos.y = -(Rx.y*tx_s + Ry.y*ty_s + r2.y*tz);
                        cam_pos.z = -(Rx.z*tx_s + Ry.z*ty_s + r2.z*tz);
                    }
                }
                float fov = esp::cfg.fov_radius > 0.f ? esp::cfg.fov_radius : 160.f;
                float md  = esp::cfg.aim_max_dist > 0.f ? esp::cfg.aim_max_dist : 300.f;
                if (head_in_fov_visible(players, vm, screen, local_char,
                                        local_team, cam_pos, fov, md)) {
                    send_click_once();
                    g_auto_throws.fetch_add(1);
                    float cd = esp::cfg.kd_auto_throw_cooldown > 40.f
                             ? esp::cfg.kd_auto_throw_cooldown : 350.f;
                    next_throw = now + std::chrono::milliseconds((int)cd);
                }
            }
        }
    }
}

inline void start_thread() {
    std::thread(thread_body).detach();
}

}
