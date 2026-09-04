#pragma once

#include "esp.hpp"
#include "memory.hpp"
#include "offsets.hpp"
#include "roblox.hpp"
#include "log.hpp"
#include "features.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern std::vector<PlayerInfo> g_players;
extern std::mutex              g_players_mtx;

namespace hitbox_expander {

inline constexpr uintptr_t BASEPART_TRANSPARENCY_OFFSET = 0x130;

struct Cache {
    uintptr_t part = 0;
    uintptr_t prim = 0;
    Vec3      orig_size{};
    float     orig_trans = 0.f;
    uint8_t   orig_flags = 0;
    bool      valid = false;
};

inline std::unordered_map<uintptr_t, Cache> g_cache;
inline uintptr_t                            g_last_dm = 0;
inline std::atomic<int>                     g_active_targets{0};
inline std::atomic<bool>                    g_running{false};

inline const char* part_name_for(int target, int idx) {
    switch (target) {
        case 0:
            switch (idx) {
                case 0: return "HeadHB";
                case 1: return "Head";
                default: return nullptr;
            }
        case 1:
            switch (idx) {
                case 0: return "Hitbox";
                case 1: return "UpperTorso";
                case 2: return "LowerTorso";
                case 3: return "Torso";
                case 4: return "HumanoidRootPart";
                default: return nullptr;
            }
        case 2:
            switch (idx) {
                case 0: return "HumanoidRootPart";
                case 1: return "Hitbox";
                default: return nullptr;
            }
        default:
            return idx == 0 ? "Head" : nullptr;
    }
}

inline Cache* resolve_hitbox(uintptr_t char_ptr, int target) {
    auto it = g_cache.find(char_ptr);
    if (it != g_cache.end() && it->second.valid) return &it->second;
    if (!offsets::BasePart::Primitive || !offsets::Primitive::Size) return nullptr;

    RbxInstance ch{char_ptr};
    for (int i = 0; ; i++) {
        const char* name = part_name_for(target, i);
        if (!name) break;
        RbxInstance part = ch.find_child_by_name(name);
        if (!part.valid()) continue;
        uintptr_t prim = rpm<uintptr_t>(part.ptr + offsets::BasePart::Primitive);
        if (!prim) continue;
        Cache c;
        c.part       = part.ptr;
        c.prim       = prim;
        c.orig_size  = rpm<Vec3>(prim + offsets::Primitive::Size);
        c.orig_trans = rpm<float>(part.ptr + BASEPART_TRANSPARENCY_OFFSET);
        c.orig_flags = offsets::Primitive::PrimitiveFlags
                     ? rpm<uint8_t>(prim + offsets::Primitive::PrimitiveFlags) : 0;
        c.valid      = true;
        auto [ins, _] = g_cache.insert_or_assign(char_ptr, c);
        return &ins->second;
    }
    return nullptr;
}

inline void restore_entry(const Cache& c) {
    if (c.prim && offsets::Primitive::Size)
        wpm<Vec3>(c.prim + offsets::Primitive::Size, c.orig_size);
    if (c.part)
        wpm<float>(c.part + BASEPART_TRANSPARENCY_OFFSET, c.orig_trans);
    if (c.prim && offsets::Primitive::PrimitiveFlags)
        wpm<uint8_t>(c.prim + offsets::Primitive::PrimitiveFlags, c.orig_flags);
}

inline void restore_all() {
    for (auto& kv : g_cache) restore_entry(kv.second);
    g_cache.clear();
    g_active_targets.store(0);
}

inline void tick() {
    if (!mem::alive()) { if (!g_cache.empty()) g_cache.clear(); g_active_targets.store(0); return; }

    RbxDataModel dm = RbxDataModel::get();
    uintptr_t dm_ptr = dm.valid() ? dm.ptr : 0;
    if (dm_ptr != g_last_dm) {
        g_cache.clear();
        g_last_dm = dm_ptr;
    }

    std::vector<PlayerInfo> players;
    {
        std::lock_guard<std::mutex> lk(g_players_mtx);
        players = g_players;
    }
    std::unordered_set<uintptr_t> live;
    live.reserve(players.size());
    for (auto& p : players) if (p.cached_char_ptr) live.insert(p.cached_char_ptr);

    if (!esp::cfg.hitbox_enabled) {
        for (auto it = g_cache.begin(); it != g_cache.end();) {
            if (live.count(it->first)) restore_entry(it->second);
            it = g_cache.erase(it);
        }
        g_active_targets.store(0);
        return;
    }
    if (!offsets::BasePart::Primitive || !offsets::Primitive::Size) return;

    uintptr_t lp = feat::local_player_ptr();
    uintptr_t local_team = 0, local_char = 0;
    if (lp) {
        if (offsets::Player::Team)      local_team = rpm<uintptr_t>(lp + offsets::Player::Team);
        if (offsets::Player::Character) local_char = rpm<uintptr_t>(lp + offsets::Player::Character);
    }

    float sz = esp::cfg.hitbox_size;
    if (sz < 2.f)  sz = 2.f;
    if (sz > 60.f) sz = 60.f;
    Vec3 size{ sz, sz, sz };
    int  target = esp::cfg.hitbox_target;
    if (target < 0 || target > 2) target = 0;
    const uint8_t cc = offsets::PrimitiveFlags::CanCollide
                     ? offsets::PrimitiveFlags::CanCollide : 0x8;

    std::unordered_set<uintptr_t> active;
    active.reserve(players.size());

    for (auto& p : players) {
        if (!p.valid || p.health <= 0.f) continue;
        if (!p.cached_char_ptr) continue;
        if (local_char && p.cached_char_ptr == local_char) continue;
        if (esp::cfg.team_check && local_team && p.team_ptr && p.team_ptr == local_team) continue;
        if (esp::cfg.aim_max_dist > 0.f) {
        }
        Cache* c = resolve_hitbox(p.cached_char_ptr, target);
        if (!c) continue;

        active.insert(p.cached_char_ptr);

        wpm<Vec3>(c->prim + offsets::Primitive::Size, size);
        wpm<float>(c->part + BASEPART_TRANSPARENCY_OFFSET, 1.0f);
        if (offsets::Primitive::PrimitiveFlags) {
            uint8_t f = rpm<uint8_t>(c->prim + offsets::Primitive::PrimitiveFlags);
            if (f & cc) wpm<uint8_t>(c->prim + offsets::Primitive::PrimitiveFlags,
                                     (uint8_t)(f & ~cc));
        }
    }

    for (auto it = g_cache.begin(); it != g_cache.end();) {
        if (active.count(it->first)) { ++it; continue; }
        if (live.count(it->first)) restore_entry(it->second);
        it = g_cache.erase(it);
    }
    g_active_targets.store((int)active.size());
}

inline void thread_body() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        if (!mem::alive()) break;
        try { tick(); } catch (...) {}
    }
    restore_all();
}

inline void start_thread() {
    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true)) return;
    std::thread(thread_body).detach();
}

}
