#pragma once
#include "memory.hpp"
#include "offsets.hpp"
#include "roblox.hpp"
#include "esp.hpp"
#include "teleport.hpp"
#include "fflags.hpp"
#include "log.hpp"
#include "games.hpp"
#include <vector>
#include <chrono>

namespace feat {

inline uintptr_t g_local_player{0};
inline uintptr_t g_cached_char{0};
inline uintptr_t g_cached_hum{0};
inline uintptr_t g_cached_hrp_prim{0};
inline uintptr_t g_cached_cam{0};
inline std::vector<uintptr_t> g_cached_part_prims;

inline uintptr_t local_player_ptr() {
    if (g_local_player) return g_local_player;
    RbxDataModel dm = RbxDataModel::get();
    if (!dm.valid()) return 0;
    RbxInstance psvc = dm.get_service("Players");
    if (!psvc.valid()) return 0;
    g_local_player = rpm<uintptr_t>(psvc.ptr + offsets::Players::LocalPlayer);
    return g_local_player;
}

inline uintptr_t ensure_cam() {
    if (!g_cached_cam) g_cached_cam = tp::local_camera();
    return g_cached_cam;
}

inline uintptr_t g_cached_lighting{0};
inline uintptr_t g_cached_world{0};

inline uintptr_t ensure_lighting() {
    if (!g_cached_lighting) {
        RbxDataModel dm = RbxDataModel::get();
        if (dm.valid()) g_cached_lighting = dm.get_service("Lighting").ptr;
    }
    return g_cached_lighting;
}
inline uintptr_t ensure_world() {
    if (!g_cached_world && offsets::Workspace::World) {
        RbxDataModel dm = RbxDataModel::get();
        if (dm.valid()) {
            uintptr_t ws = dm.get_service("Workspace").ptr;
            if (ws) g_cached_world = rpm<uintptr_t>(ws + offsets::Workspace::World);
        }
    }
    return g_cached_world;
}

inline uintptr_t g_cached_blur{0};
inline uintptr_t g_cached_dof{0};
inline uintptr_t g_cached_sunrays{0};
inline uintptr_t g_cached_atmo{0};

inline bool g_ff_prev_fps_unlock = false;
inline int  g_ff_prev_fps_cap    = -1;
inline bool g_ff_p_fb = false, g_ff_p_gs = false, g_ff_p_na = false,
            g_ff_p_ng = false, g_ff_p_lt = false;

inline float    g_zoom_orig_min       = -1.f;
inline float    g_zoom_orig_max       = -1.f;
inline uint32_t g_camera_mode_orig    = 0xFFFFFFFF;
inline float    g_atmo_orig_density   = -1.f;

inline uintptr_t ensure_effect(uintptr_t& cache, const char* cls) {
    if (cache) return cache;
    if (uintptr_t l = ensure_lighting()) cache = RbxInstance{l}.find_child_by_class(cls).ptr;
    if (!cache) { if (uintptr_t c = ensure_cam()) cache = RbxInstance{c}.find_child_by_class(cls).ptr; }
    return cache;
}

inline bool is_base_part(const std::string& cls) {
    return cls == "Part" || cls == "MeshPart" || cls == "UnionOperation" ||
           cls == "WedgePart" || cls == "TrussPart" || cls == "CornerWedgePart" ||
           cls == "SpawnLocation" || cls == "Seat" || cls == "VehicleSeat";
}

inline void refresh_char_cache(uintptr_t ch) {
    g_cached_char = ch;
    g_cached_hum = 0;
    g_cached_hrp_prim = 0;
    g_cached_cam = 0;
    g_cached_part_prims.clear();
    if (!ch) return;

    RbxInstance character{ch};
    g_cached_hum = character.find_child_by_class("Humanoid").ptr;

    uintptr_t hrp = character.find_child_by_name("HumanoidRootPart").ptr;
    if (hrp) g_cached_hrp_prim = rpm<uintptr_t>(hrp + offsets::BasePart::Primitive);

    for (auto& c : character.get_children()) {
        if (!is_base_part(c.get_class())) continue;
        uintptr_t prim = rpm<uintptr_t>(c.ptr + offsets::BasePart::Primitive);
        if (prim) g_cached_part_prims.push_back(prim);
    }
}

inline void set_collision(bool enabled) {
    if (!offsets::Primitive::PrimitiveFlags) return;
    const uint8_t mask = offsets::PrimitiveFlags::CanCollide;
    for (uintptr_t prim : g_cached_part_prims) {
        uint8_t flags = rpm<uint8_t>(prim + offsets::Primitive::PrimitiveFlags);
        uint8_t next = enabled ? (flags | mask) : (flags & ~mask);
        if (next != flags)
            wpm<uint8_t>(prim + offsets::Primitive::PrimitiveFlags, next);
    }
}

inline void set_anchor(uintptr_t prim, bool on) {
    if (!prim || !offsets::Primitive::PrimitiveFlags) return;
    const uint8_t mask = offsets::PrimitiveFlags::Anchored;
    uint8_t flags = rpm<uint8_t>(prim + offsets::Primitive::PrimitiveFlags);
    uint8_t next = on ? (flags | mask) : (flags & ~mask);
    if (next != flags)
        wpm<uint8_t>(prim + offsets::Primitive::PrimitiveFlags, next);
}

inline void invalidate_caches_if_dm_changed() {
    static uintptr_t last_dm = 0;
    RbxDataModel dm = RbxDataModel::get();
    if (!dm.valid()) return;
    if (dm.ptr != last_dm) {
        last_dm = dm.ptr;
        g_cached_lighting = 0;
        g_cached_world    = 0;
        g_cached_blur     = 0;
        g_cached_dof      = 0;
        g_cached_sunrays  = 0;
        g_cached_atmo     = 0;
        g_local_player    = 0;

        g_ff_prev_fps_unlock = false;
        g_ff_prev_fps_cap    = -1;
        g_ff_p_fb = g_ff_p_gs = g_ff_p_na = g_ff_p_ng = g_ff_p_lt = false;
        g_zoom_orig_min = -1.f;
        g_zoom_orig_max = -1.f;
        g_camera_mode_orig = 0xFFFFFFFF;
        g_atmo_orig_density = -1.f;
        games::on_dm_changed();
    }
}

inline void apply_self_mods() {
    invalidate_caches_if_dm_changed();
    games::on_inject();

    {
        if (esp::cfg.fps_unlock &&
            (!g_ff_prev_fps_unlock || esp::cfg.fps_cap != g_ff_prev_fps_cap)) {
            fflags::write_int_guarded(fflags::rva::TaskSchedulerTargetFps,
                                      esp::cfg.fps_cap, 1, 100000);
            g_ff_prev_fps_cap = esp::cfg.fps_cap;
        }
        g_ff_prev_fps_unlock = esp::cfg.fps_unlock;

        if (esp::cfg.ff_fullbright != g_ff_p_fb) {
            fflags::write_bool(fflags::rva::DebugPauseVoxelizer, esp::cfg.ff_fullbright);
            g_ff_p_fb = esp::cfg.ff_fullbright;
        }
        if (esp::cfg.ff_gray_sky != g_ff_p_gs) {
            fflags::write_bool(fflags::rva::DebugSkyGray, esp::cfg.ff_gray_sky);
            g_ff_p_gs = esp::cfg.ff_gray_sky;
        }
        if (esp::cfg.ff_no_adorns != g_ff_p_na) {
            fflags::write_bool(fflags::rva::DebugAdornsDisabled, esp::cfg.ff_no_adorns);
            g_ff_p_na = esp::cfg.ff_no_adorns;
        }
        if (esp::cfg.ff_no_grass != g_ff_p_ng) {
            int d = esp::cfg.ff_no_grass ? 0 : 128;
            fflags::write_int_guarded(fflags::rva::FRMMinGrassDistance, d, 0, 100000);
            fflags::write_int_guarded(fflags::rva::FRMMaxGrassDistance, d, 0, 100000);
            g_ff_p_ng = esp::cfg.ff_no_grass;
        }
        if (esp::cfg.ff_low_texture != g_ff_p_lt) {
            fflags::write_bool(fflags::rva::TextureQualityOverrideEnabled, esp::cfg.ff_low_texture);
            if (esp::cfg.ff_low_texture)
                fflags::write_int_guarded(fflags::rva::TextureQualityOverride, 0, 0, 100);
            g_ff_p_lt = esp::cfg.ff_low_texture;
        }
    }

    bool need_char = esp::cfg.speed_enabled || esp::cfg.jump_enabled ||
                     esp::cfg.noclip_enabled || esp::cfg.infinite_jump ||
                     esp::cfg.fly_enabled || esp::cfg.bunny_hop || esp::cfg.freecam ||
                     esp::cfg.antiaim_spin || esp::cfg.antiaim_jitter ||
                     esp::cfg.godmode || esp::cfg.hover_enabled ||
                     esp::cfg.jump_height_enabled || esp::cfg.no_slope_limit ||
                     games::needs_character();
    if (need_char) {
        uintptr_t lp = local_player_ptr();
        uintptr_t ch = lp ? rpm<uintptr_t>(lp + offsets::Player::Character) : 0;
        if (ch != g_cached_char) refresh_char_cache(ch);
    }

    struct HumOrig {
        uintptr_t hum = 0;
        float walkspeed = -1.f, jumppower = -1.f;
        float hipheight = -1.f, jumpheight = -1.f, maxslope = -1.f;
    };
    static HumOrig hum_orig;
    if (g_cached_hum && hum_orig.hum != g_cached_hum) {

        hum_orig = HumOrig{ g_cached_hum, -1.f, -1.f, -1.f, -1.f, -1.f };
    }
    auto ensure_hum_cache = [&](float& slot, uintptr_t off) {
        if (!g_cached_hum || !off) return;
        if (slot < 0.f) slot = rpm<float>(g_cached_hum + off);
    };

    constexpr int RESTORE_TICKS = 12;
    {
        static int ticks = 0;
        if (g_cached_hum && offsets::Humanoid::WalkSpeed) {
            if (esp::cfg.speed_enabled) {
                ensure_hum_cache(hum_orig.walkspeed, offsets::Humanoid::WalkSpeed);
                wpm<float>(g_cached_hum + offsets::Humanoid::WalkSpeed, esp::cfg.speed_value);
                if (offsets::Humanoid::WalkSpeedCheck)
                    wpm<float>(g_cached_hum + offsets::Humanoid::WalkSpeedCheck, esp::cfg.speed_value);
                ticks = RESTORE_TICKS;
            } else if (ticks > 0 && hum_orig.walkspeed >= 0.f) {
                wpm<float>(g_cached_hum + offsets::Humanoid::WalkSpeed, hum_orig.walkspeed);
                if (offsets::Humanoid::WalkSpeedCheck)
                    wpm<float>(g_cached_hum + offsets::Humanoid::WalkSpeedCheck, hum_orig.walkspeed);
                ticks--;
            }
        }
    }
    {
        static int ticks = 0;
        if (g_cached_hum && offsets::Humanoid::JumpPower) {
            if (esp::cfg.jump_enabled) {
                ensure_hum_cache(hum_orig.jumppower, offsets::Humanoid::JumpPower);
                wpm<float>(g_cached_hum + offsets::Humanoid::JumpPower, esp::cfg.jump_value);
                ticks = RESTORE_TICKS;
            } else if (ticks > 0 && hum_orig.jumppower >= 0.f) {
                wpm<float>(g_cached_hum + offsets::Humanoid::JumpPower, hum_orig.jumppower);
                ticks--;
            }
        }
    }

    {
        static bool prev_noclip = false;
        if (esp::cfg.noclip_enabled) {
            set_collision(false);
        } else if (prev_noclip) {
            set_collision(true);
        }
        prev_noclip = esp::cfg.noclip_enabled;
    }

    {
        static bool prev_space = false;
        bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        if (esp::cfg.infinite_jump && g_cached_hum && offsets::Humanoid::Jump &&
            space && !prev_space) {
            wpm<uint8_t>(g_cached_hum + offsets::Humanoid::Jump, 1);
        }
        prev_space = space;
    }

    if (esp::cfg.bunny_hop && g_cached_hum && offsets::Humanoid::Jump &&
        (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
        bool grounded = true;
        if (offsets::Humanoid::FloorMaterial) {
            uint32_t fm = rpm<uint32_t>(g_cached_hum + offsets::Humanoid::FloorMaterial);
            grounded = (fm != 1792 && fm != 0);
        }
        static auto last_bhop = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (grounded &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_bhop).count() > 40) {
            wpm<uint8_t>(g_cached_hum + offsets::Humanoid::Jump, 1);
            last_bhop = now;
        }
    }

    if ((esp::cfg.antiaim_spin || esp::cfg.antiaim_jitter) && g_cached_hrp_prim &&
        offsets::Primitive::CFrame && offsets::Primitive::Position) {
        static auto last = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        Vec3 pos = rpm<Vec3>(g_cached_hrp_prim + offsets::Primitive::Position);
        if (esp::cfg.antiaim_jitter) {
            static float jt = 0.f; jt += dt * 45.f;
            pos.x += sinf(jt) * esp::cfg.antiaim_jitter_amount;
            pos.z += cosf(jt) * esp::cfg.antiaim_jitter_amount;
        }

        float cf[12];
        if (esp::cfg.antiaim_spin) {
            static float ang = 0.f;
            ang += (esp::cfg.antiaim_spin_speed * 3.14159265f / 180.f) * dt;
            float c = cosf(ang), s = sinf(ang);
            cf[0]=c; cf[1]=0; cf[2]=s;
            cf[3]=0; cf[4]=1; cf[5]=0;
            cf[6]=-s;cf[7]=0; cf[8]=c;
        } else {
            mem::vm_read(mem::g_proc, (LPCVOID)(g_cached_hrp_prim + offsets::Primitive::CFrame),
                              cf, 9 * sizeof(float), nullptr);
        }
        cf[9]=pos.x; cf[10]=pos.y; cf[11]=pos.z;
        mem::vm_write(mem::g_proc, (LPVOID)(g_cached_hrp_prim + offsets::Primitive::CFrame),
                           cf, sizeof(cf), nullptr);
    }

    if (esp::cfg.fov_changer_enabled && offsets::Camera::FieldOfView) {
        uintptr_t cam = ensure_cam();
        if (cam) wpm<float>(cam + offsets::Camera::FieldOfView, esp::cfg.fov_value);
    }

    {

        if (esp::cfg.godmode && g_cached_hum &&
            offsets::Humanoid::MaxHealth && offsets::Humanoid::Health) {
            float mh = rpm<float>(g_cached_hum + offsets::Humanoid::MaxHealth);
            if (mh > 0.f) wpm<float>(g_cached_hum + offsets::Humanoid::Health, mh);
        }

        static int hover_ticks = 0, jh_ticks = 0, slope_ticks = 0;
        if (g_cached_hum && offsets::Humanoid::HipHeight) {
            if (esp::cfg.hover_enabled) {
                ensure_hum_cache(hum_orig.hipheight, offsets::Humanoid::HipHeight);
                wpm<float>(g_cached_hum + offsets::Humanoid::HipHeight, esp::cfg.hover_height);
                hover_ticks = RESTORE_TICKS;
            } else if (hover_ticks > 0 && hum_orig.hipheight >= 0.f) {
                wpm<float>(g_cached_hum + offsets::Humanoid::HipHeight, hum_orig.hipheight);
                hover_ticks--;
            }
        }
        if (g_cached_hum && offsets::Humanoid::JumpHeight) {
            if (esp::cfg.jump_height_enabled) {
                ensure_hum_cache(hum_orig.jumpheight, offsets::Humanoid::JumpHeight);
                wpm<float>(g_cached_hum + offsets::Humanoid::JumpHeight, esp::cfg.jump_height);
                jh_ticks = RESTORE_TICKS;
            } else if (jh_ticks > 0 && hum_orig.jumpheight >= 0.f) {
                wpm<float>(g_cached_hum + offsets::Humanoid::JumpHeight, hum_orig.jumpheight);
                jh_ticks--;
            }
        }
        if (g_cached_hum && offsets::Humanoid::MaxSlopeAngle) {
            if (esp::cfg.no_slope_limit) {
                ensure_hum_cache(hum_orig.maxslope, offsets::Humanoid::MaxSlopeAngle);
                wpm<float>(g_cached_hum + offsets::Humanoid::MaxSlopeAngle, 89.f);
                slope_ticks = RESTORE_TICKS;
            } else if (slope_ticks > 0 && hum_orig.maxslope >= 0.f) {
                wpm<float>(g_cached_hum + offsets::Humanoid::MaxSlopeAngle, hum_orig.maxslope);
                slope_ticks--;
            }
        }

        if (esp::cfg.no_slope_limit && !offsets::Humanoid::MaxSlopeAngle) {
            static auto next = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now >= next) {
                elog::warn("no_slope_limit: Humanoid::MaxSlopeAngle offset not resolved by dumper");
                next = now + std::chrono::seconds(1);
            }
        }
    }

    {

        static int zoom_ticks = 0, fp_ticks = 0;
        uintptr_t lp = (esp::cfg.zoom_unlock || esp::cfg.lock_first_person ||
                        zoom_ticks > 0 || fp_ticks > 0)
                       ? local_player_ptr() : 0;
        if (esp::cfg.zoom_unlock && lp) {
            if (offsets::Player::MaxZoomDistance) {
                if (g_zoom_orig_max < 0.f) g_zoom_orig_max = rpm<float>(lp + offsets::Player::MaxZoomDistance);
                wpm<float>(lp + offsets::Player::MaxZoomDistance, esp::cfg.max_zoom);
            }
            if (offsets::Player::MinZoomDistance) {
                if (g_zoom_orig_min < 0.f) g_zoom_orig_min = rpm<float>(lp + offsets::Player::MinZoomDistance);
                wpm<float>(lp + offsets::Player::MinZoomDistance, 0.5f);
            }
            zoom_ticks = RESTORE_TICKS;
        } else if (zoom_ticks > 0 && lp) {
            if (offsets::Player::MaxZoomDistance && g_zoom_orig_max >= 0.f)
                wpm<float>(lp + offsets::Player::MaxZoomDistance, g_zoom_orig_max);
            if (offsets::Player::MinZoomDistance && g_zoom_orig_min >= 0.f)
                wpm<float>(lp + offsets::Player::MinZoomDistance, g_zoom_orig_min);
            zoom_ticks--;
        }

        if (offsets::Player::CameraMode && lp) {
            if (esp::cfg.lock_first_person) {
                if (g_camera_mode_orig == 0xFFFFFFFF)
                    g_camera_mode_orig = rpm<uint32_t>(lp + offsets::Player::CameraMode);
                wpm<uint32_t>(lp + offsets::Player::CameraMode, 1);
                fp_ticks = RESTORE_TICKS;
            } else if (fp_ticks > 0 && g_camera_mode_orig != 0xFFFFFFFF) {
                wpm<uint32_t>(lp + offsets::Player::CameraMode, g_camera_mode_orig);
                fp_ticks--;
            }
        }
    }

    {

        static int t_blur = 0, t_dof = 0, t_sr = 0, t_atmo = 0;

        if (esp::cfg.vis_no_blur) {
            if (uintptr_t b = ensure_effect(g_cached_blur, "BlurEffect")) {
                wpm<uint8_t>(b + 0xB0, 0); t_blur = RESTORE_TICKS;
            }
        } else if (t_blur > 0 && g_cached_blur) {
            wpm<uint8_t>(g_cached_blur + 0xB0, 1); t_blur--;
        }

        if (esp::cfg.vis_no_dof) {
            if (uintptr_t d = ensure_effect(g_cached_dof, "DepthOfFieldEffect")) {
                wpm<uint8_t>(d + 0xB0, 0); t_dof = RESTORE_TICKS;
            }
        } else if (t_dof > 0 && g_cached_dof) {
            wpm<uint8_t>(g_cached_dof + 0xB0, 1); t_dof--;
        }

        if (esp::cfg.vis_no_sunrays) {
            if (uintptr_t s = ensure_effect(g_cached_sunrays, "SunRaysEffect")) {
                wpm<uint8_t>(s + 0xB0, 0); t_sr = RESTORE_TICKS;
            }
        } else if (t_sr > 0 && g_cached_sunrays) {
            wpm<uint8_t>(g_cached_sunrays + 0xB0, 1); t_sr--;
        }

        if (esp::cfg.vis_no_atmosphere) {
            if (uintptr_t a = ensure_effect(g_cached_atmo, "Atmosphere")) {
                if (g_atmo_orig_density < 0.f)
                    g_atmo_orig_density = rpm<float>(a + 0xD0);
                wpm<float>(a + 0xD0, 0.f);
                t_atmo = RESTORE_TICKS;
            }
        } else if (t_atmo > 0 && g_cached_atmo && g_atmo_orig_density >= 0.f) {
            wpm<float>(g_cached_atmo + 0xD0, g_atmo_orig_density); t_atmo--;
        }
    }

    if (esp::cfg.world_nofog && offsets::Lighting::FogEnd) {
        if (uintptr_t l = ensure_lighting()) wpm<float>(l + offsets::Lighting::FogEnd, 100000.f);
    }
    if (esp::cfg.world_time_enabled && offsets::Lighting::ClockTime) {
        if (uintptr_t l = ensure_lighting()) {
            double mins = (double)esp::cfg.world_time * 60.0;
            wpm<double>(l + offsets::Lighting::ClockTime, mins);
        }
    }
    if (esp::cfg.world_bright_enabled && offsets::Lighting::Brightness) {
        if (uintptr_t l = ensure_lighting()) wpm<float>(l + offsets::Lighting::Brightness, esp::cfg.world_bright);
    }
    {

        static float    orig_grav = -1.f;
        static uintptr_t grav_w   = 0;
        static int       ticks    = 0;
        if (esp::cfg.world_gravity_enabled && offsets::World::Gravity) {
            if (uintptr_t w = ensure_world()) {
                if (grav_w != w) { orig_grav = -1.f; grav_w = w; }
                if (orig_grav < 0.f) orig_grav = rpm<float>(w + offsets::World::Gravity);
                wpm<float>(w + offsets::World::Gravity, esp::cfg.world_gravity);
                ticks = RESTORE_TICKS;
            }
        } else if (ticks > 0 && orig_grav >= 0.f) {
            if (uintptr_t w = ensure_world()) {
                wpm<float>(w + offsets::World::Gravity, orig_grav);
                ticks--;
            }
        }
    }

    games::on_tick(g_cached_hum);
}

inline void apply_movement() {
    if (!esp::cfg.fly_enabled && !esp::cfg.freecam) return;

    uintptr_t lp = local_player_ptr();
    uintptr_t ch = lp ? rpm<uintptr_t>(lp + offsets::Player::Character) : 0;
    if (ch != g_cached_char) refresh_char_cache(ch);

    {
        static bool prev_fly = false;
        static bool idle_locked   = false;
        static Vec3 idle_pos      { };

        if (esp::cfg.fly_enabled) {

            static auto next_diag = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now >= next_diag) {
                if (!g_cached_hrp_prim ||
                    !offsets::Primitive::AssemblyLinearVelocity ||
                    !offsets::Camera::CFrame) {
                    elog::warn("fly: pre-req missing (hrp_prim=0x%llX vel_off=0x%llX cam_off=0x%llX lp=0x%llX ch=0x%llX)",
                        (unsigned long long)g_cached_hrp_prim,
                        (unsigned long long)offsets::Primitive::AssemblyLinearVelocity,
                        (unsigned long long)offsets::Camera::CFrame,
                        (unsigned long long)local_player_ptr(),
                        (unsigned long long)g_cached_char);
                    next_diag = now + std::chrono::seconds(1);
                }
            }
        }

        if (esp::cfg.fly_enabled && g_cached_hrp_prim &&
            offsets::Primitive::AssemblyLinearVelocity && offsets::Camera::CFrame) {
            uintptr_t cam = ensure_cam();
            if (cam) {
                if (!prev_fly) {
                    elog::ok("fly: enabled (hrp_prim=0x%llX cam=0x%llX)",
                             (unsigned long long)g_cached_hrp_prim,
                             (unsigned long long)cam);
                    idle_locked = false;
                }

                float cf[9]{};
                mem::vm_read(mem::g_proc, (LPCVOID)(cam + offsets::Camera::CFrame),
                                  cf, sizeof(cf), nullptr);
                Vec3 look  = { -cf[2], -cf[5], -cf[8] };
                Vec3 right = {  cf[0],  cf[3],  cf[6] };

                if (esp::cfg.fly_planar) {
                    look.y = 0.f;
                    float ll = look.length();
                    if (ll > 1e-4f) { look.x /= ll; look.z /= ll; }
                    right.y = 0.f;
                    float rl = right.length();
                    if (rl > 1e-4f) { right.x /= rl; right.z /= rl; }
                }

                Vec3 dir{0.f, 0.f, 0.f};
                if (GetAsyncKeyState('W') & 0x8000) dir = dir + look;
                if (GetAsyncKeyState('S') & 0x8000) dir = dir - look;
                if (GetAsyncKeyState('D') & 0x8000) dir = dir + right;
                if (GetAsyncKeyState('A') & 0x8000) dir = dir - right;
                if (GetAsyncKeyState(VK_SPACE)   & 0x8000) dir.y += 1.f;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) dir.y -= 1.f;

                float len = dir.length();
                if (len > 1e-3f) {

                    float inv = esp::cfg.fly_speed / len;
                    Vec3 vel  = { dir.x * inv, dir.y * inv, dir.z * inv };
                    wpm<Vec3>(g_cached_hrp_prim + offsets::Primitive::AssemblyLinearVelocity, vel);
                    idle_locked = false;
                } else {

                    if (!idle_locked && offsets::Primitive::Position) {
                        idle_pos = rpm<Vec3>(g_cached_hrp_prim + offsets::Primitive::Position);
                        idle_locked = true;
                    }
                    wpm<Vec3>(g_cached_hrp_prim + offsets::Primitive::AssemblyLinearVelocity,
                              Vec3{0.f, 0.f, 0.f});
                    if (idle_locked && offsets::Primitive::Position) {
                        wpm<Vec3>(g_cached_hrp_prim + offsets::Primitive::Position, idle_pos);
                    }
                }
                if (offsets::Primitive::AssemblyAngularVelocity)
                    wpm<Vec3>(g_cached_hrp_prim + offsets::Primitive::AssemblyAngularVelocity,
                              Vec3{0.f, 0.f, 0.f});
            }
        } else if (prev_fly) {
            elog::info("fly: disabled");
            idle_locked = false;
        }
        prev_fly = esp::cfg.fly_enabled;
    }

    {
        static bool prev_fc = false;
        static Vec3 fc_pos{};
        static float yaw = 0.f, pitch = 0.f;
        static auto last = std::chrono::steady_clock::now();

        static uintptr_t saved_subject = 0;
        if (esp::cfg.freecam && offsets::Camera::CFrame && offsets::Camera::CameraType) {
            uintptr_t cam = ensure_cam();
            if (cam) {
                wpm<uint32_t>(cam + offsets::Camera::CameraType, 6);

                if (offsets::Camera::CameraSubject) {
                    if (!prev_fc) saved_subject = rpm<uintptr_t>(cam + offsets::Camera::CameraSubject);
                    wpm<uintptr_t>(cam + offsets::Camera::CameraSubject, 0);
                }
                if (!prev_fc) {
                    elog::ok("freecam: enabled (cam=0x%llX, subj_saved=0x%llX)",
                             (unsigned long long)cam, (unsigned long long)saved_subject);
                    fc_pos = rpm<Vec3>(cam + offsets::Camera::CFrame + 0x24);
                    float cf0[9]{};
                    mem::vm_read(mem::g_proc, (LPCVOID)(cam + offsets::Camera::CFrame), cf0, sizeof(cf0), nullptr);
                    Vec3 lk = { -cf0[2], -cf0[5], -cf0[8] };
                    yaw = atan2f(lk.x, lk.z);
                    float ly = lk.y; if (ly > 1.f) ly = 1.f; if (ly < -1.f) ly = -1.f;
                    pitch = asinf(ly);
                    last = std::chrono::steady_clock::now();
                }
                auto now = std::chrono::steady_clock::now();
                float dt = std::chrono::duration<float>(now - last).count();
                last = now;
                if (dt > 0.05f) dt = 0.05f;

                float ls = 2.0f * dt;
                if (GetAsyncKeyState(VK_LEFT)  & 0x8000) yaw   -= ls;
                if (GetAsyncKeyState(VK_RIGHT) & 0x8000) yaw   += ls;
                if (GetAsyncKeyState(VK_UP)    & 0x8000) pitch += ls;
                if (GetAsyncKeyState(VK_DOWN)  & 0x8000) pitch -= ls;
                if (pitch >  1.5f) pitch =  1.5f;
                if (pitch < -1.5f) pitch = -1.5f;

                Vec3 look  = { cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
                Vec3 back  = { -look.x, -look.y, -look.z };
                Vec3 right = tp::normalize(tp::cross(Vec3{0.f, 1.f, 0.f}, back));
                Vec3 up    = tp::cross(back, right);

                Vec3 dir{0.f, 0.f, 0.f};
                if (GetAsyncKeyState('W') & 0x8000) dir = dir + look;
                if (GetAsyncKeyState('S') & 0x8000) dir = dir - look;
                if (GetAsyncKeyState('D') & 0x8000) dir = dir + right;
                if (GetAsyncKeyState('A') & 0x8000) dir = dir - right;
                if (GetAsyncKeyState(VK_SPACE)   & 0x8000) dir.y += 1.f;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) dir.y -= 1.f;
                float len = dir.length();
                if (len > 0.001f) fc_pos = fc_pos + dir * (esp::cfg.freecam_speed * dt / len);

                float cf[12];
                cf[0]=right.x; cf[1]=up.x; cf[2]=back.x;
                cf[3]=right.y; cf[4]=up.y; cf[5]=back.y;
                cf[6]=right.z; cf[7]=up.z; cf[8]=back.z;
                cf[9]=fc_pos.x; cf[10]=fc_pos.y; cf[11]=fc_pos.z;
                mem::vm_write(mem::g_proc, (LPVOID)(cam + offsets::Camera::CFrame), cf, sizeof(cf), nullptr);
            }
        } else if (prev_fc) {
            elog::info("freecam: disabled");
            uintptr_t cam = ensure_cam();
            if (cam && offsets::Camera::CameraType) wpm<uint32_t>(cam + offsets::Camera::CameraType, 5);
            if (cam && offsets::Camera::CameraSubject && saved_subject)
                wpm<uintptr_t>(cam + offsets::Camera::CameraSubject, saved_subject);
        }
        prev_fc = esp::cfg.freecam;
    }
}

}
