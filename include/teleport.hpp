#pragma once
#include "memory.hpp"
#include "offsets.hpp"
#include "roblox.hpp"
#include "math.hpp"
#include <atomic>
#include <mutex>

namespace tp {

inline std::atomic<int>       g_hold{0};
inline std::atomic<bool>      g_spin{false};
inline std::atomic<uintptr_t> g_spin_target{0};

inline std::atomic<float>     g_spin_radius{8.f};
inline std::atomic<float>     g_spin_speed{5.f};
inline std::atomic<uintptr_t> g_cached_prim{0};
inline Vec3                   g_dest{};
inline std::mutex             g_mtx;

inline std::atomic<bool>      g_spectate{false};
inline std::atomic<uintptr_t> g_spectate_hum{0};
inline std::atomic<uintptr_t> g_spectate_cam{0};
inline std::atomic<uintptr_t> g_cam_subject_off{0};

inline std::atomic<bool>      g_lock{false};
inline std::atomic<uintptr_t> g_lock_target{0};
inline std::atomic<uintptr_t> g_lock_self{0};

inline uintptr_t local_hrp_primitive() {
    RbxDataModel dm = RbxDataModel::get();
    if (!dm.valid()) return 0;

    RbxInstance players_svc = dm.get_service("Players");
    if (!players_svc.valid()) return 0;

    uintptr_t local_player = rpm<uintptr_t>(players_svc.ptr + offsets::Players::LocalPlayer);
    if (!local_player) return 0;

    uintptr_t char_ptr = rpm<uintptr_t>(local_player + offsets::Player::Character);
    if (!char_ptr) return 0;

    RbxInstance character{char_ptr};
    uintptr_t hrp = character.find_child_by_name("HumanoidRootPart").ptr;
    if (!hrp) return 0;

    return rpm<uintptr_t>(hrp + offsets::BasePart::Primitive);
}

inline uintptr_t local_camera() {
    RbxDataModel dm = RbxDataModel::get();
    if (!dm.valid()) return 0;
    RbxInstance ws = dm.get_service("Workspace");
    if (!ws.valid()) return 0;
    return rpm<uintptr_t>(ws.ptr + offsets::Workspace::CurrentCamera);
}

inline uintptr_t local_humanoid() {
    RbxDataModel dm = RbxDataModel::get();
    if (!dm.valid()) return 0;
    RbxInstance psvc = dm.get_service("Players");
    if (!psvc.valid()) return 0;
    uintptr_t lp = rpm<uintptr_t>(psvc.ptr + offsets::Players::LocalPlayer);
    if (!lp) return 0;
    uintptr_t ch = rpm<uintptr_t>(lp + offsets::Player::Character);
    if (!ch) return 0;
    return RbxInstance{ch}.find_child_by_class("Humanoid").ptr;
}

inline uintptr_t find_cam_subject_off() {
    uintptr_t cam = local_camera();
    uintptr_t hum = local_humanoid();
    if (!cam || !hum) return 0;
    for (uintptr_t off = 0x40; off < 0x400; off += 8)
        if (rpm<uintptr_t>(cam + off) == hum) return off;
    return 0;
}

inline bool write_position(uintptr_t primitive, Vec3 dest) {
    if (!primitive) return false;
    bool ok = wpm<Vec3>(primitive + offsets::Primitive::Position, dest);
    const Vec3 zero{0.f, 0.f, 0.f};
    if (offsets::Primitive::AssemblyLinearVelocity)
        wpm<Vec3>(primitive + offsets::Primitive::AssemblyLinearVelocity, zero);
    if (offsets::Primitive::AssemblyAngularVelocity)
        wpm<Vec3>(primitive + offsets::Primitive::AssemblyAngularVelocity, zero);
    return ok;
}

inline void request_teleport(Vec3 dest, int frames = 60) {
    uintptr_t prim = local_hrp_primitive();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_dest = dest;
    }
    g_cached_prim.store(prim);
    g_hold.store(frames);
}

inline bool start_spectate(uintptr_t target_humanoid) {
    uintptr_t cam = local_camera();
    uintptr_t off = g_cam_subject_off.load();
    if (!off) { off = find_cam_subject_off(); g_cam_subject_off.store(off); }
    if (!target_humanoid || !cam || !off) return false;
    g_spectate_cam.store(cam);
    g_spectate_hum.store(target_humanoid);
    g_spectate.store(true);
    return true;
}

inline void stop_spectate() {
    g_spectate.store(false);
    uintptr_t cam = g_spectate_cam.load();
    uintptr_t off = g_cam_subject_off.load();
    uintptr_t lh  = local_humanoid();
    if (cam && off && lh) wpm<uintptr_t>(cam + off, lh);
}

inline void start_lock(uintptr_t target_hrp_primitive) {
    if (!target_hrp_primitive) return;
    g_lock_self.store(local_hrp_primitive());
    g_lock_target.store(target_hrp_primitive);
    g_lock.store(true);
}

inline void stop_lock() { g_lock.store(false); }

inline void start_spin(uintptr_t target_hrp_primitive) {
    if (!target_hrp_primitive) return;
    g_spin_target.store(target_hrp_primitive);
    g_spin.store(true);
}
inline void stop_spin() { g_spin.store(false); }

inline Vec3 cross(Vec3 a, Vec3 b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
inline Vec3 normalize(Vec3 v) {
    float l = v.length();
    return (l > 1e-6f) ? Vec3{v.x/l, v.y/l, v.z/l} : v;
}

inline void build_lookat(Vec3 eye, Vec3 target, float cf[12]) {
    Vec3 look  = normalize(target - eye);
    Vec3 back  = { -look.x, -look.y, -look.z };
    Vec3 right = normalize(cross(Vec3{0.f, 1.f, 0.f}, back));
    Vec3 up    = cross(back, right);

    cf[0]=right.x; cf[1]=up.x; cf[2]=back.x;
    cf[3]=right.y; cf[4]=up.y; cf[5]=back.y;
    cf[6]=right.z; cf[7]=up.z; cf[8]=back.z;
    cf[9]=eye.x;   cf[10]=eye.y; cf[11]=eye.z;
}

inline void service() {
    if (g_hold.load() > 0) {
        uintptr_t prim = g_cached_prim.load();
        if (!prim) prim = local_hrp_primitive();
        if (prim) {
            Vec3 dest;
            { std::lock_guard<std::mutex> lk(g_mtx); dest = g_dest; }
            write_position(prim, dest);
        }
        g_hold.fetch_sub(1);
    }

    if (g_lock.load()) {
        uintptr_t self = g_lock_self.load();
        if (!self) { self = local_hrp_primitive(); g_lock_self.store(self); }
        uintptr_t tgt = g_lock_target.load();
        if (self && tgt) {
            Vec3 tpos = rpm<Vec3>(tgt + offsets::Primitive::Position);
            write_position(self, Vec3{ tpos.x, tpos.y + 3.0f, tpos.z });
        }
    }

    if (g_spin.load()) {
        uintptr_t self = local_hrp_primitive();
        uintptr_t tgt  = g_spin_target.load();
        if (self && tgt) {
            static float angle = 0.f;
            static auto last = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            if (dt > 0.1f) dt = 0.1f;
            angle += dt * g_spin_speed.load();
            Vec3 tpos = rpm<Vec3>(tgt + offsets::Primitive::Position);
            float r = g_spin_radius.load();
            if (r < 0.5f) r = 8.f;
            write_position(self, Vec3{
                tpos.x + std::cos(angle) * r,
                tpos.y + 1.5f,
                tpos.z + std::sin(angle) * r });
        }
    }

    if (g_spectate.load()) {
        uintptr_t cam = g_spectate_cam.load();
        uintptr_t off = g_cam_subject_off.load();
        uintptr_t hum = g_spectate_hum.load();
        if (cam && off && hum)
            wpm<uintptr_t>(cam + off, hum);
    }
}

}
