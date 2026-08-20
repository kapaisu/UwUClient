#include "../include/aimbot.hpp"
#include "../include/esp.hpp"
#include "../include/memory.hpp"
#include "../include/offsets.hpp"
#include "../include/teleport.hpp"
#include "../include/log.hpp"
#include "../include/check.hpp"
#include "../include/menu.hpp"
#include "../include/roblox.hpp"
#include "../include/games.hpp"
#include <Windows.h>
#include <thread>
#include <chrono>
#include <cmath>
#include <random>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cfloat>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <vector>

extern std::vector<PlayerInfo> g_players;
extern Matrix4 g_view_matrix;
extern Vec2 g_screen;
extern std::mutex g_players_mtx;
extern std::atomic<uintptr_t> g_local_team;
extern std::atomic<uintptr_t> g_aim_target;

namespace aimbot {

    static std::mt19937 gen(std::random_device{}());

    struct AimPoint { Vec3 world; Vec2 screen; float dist; bool valid; };

    static void move_mouse(float dx, float dy) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = (LONG)std::round(dx);
        input.mi.dy = (LONG)std::round(dy);
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        if (input.mi.dx != 0 || input.mi.dy != 0) {
            SendInput(1, &input, sizeof(INPUT));
        }
    }

    static void send_click() {
        INPUT in[2]{};
        in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, in, sizeof(INPUT));
    }

    static Vec3 get_camera_pos(const Matrix4& vm) {
        Vec3 cam{};
        Vec3 r0 = { vm[0], vm[1], vm[2] };
        Vec3 r1 = { vm[4], vm[5], vm[6] };
        Vec3 r2 = { vm[12], vm[13], vm[14] };
        float tx = vm[3];
        float ty = vm[7];
        float tz = vm[15];
        float sx = r0.length();
        float sy = r1.length();
        if (sx > 0.0001f && sy > 0.0001f) {
            Vec3 Rx = r0 * (1.f / sx);
            Vec3 Ry = r1 * (1.f / sy);
            Vec3 Rz = r2;
            float tx_s = tx / sx;
            float ty_s = ty / sy;
            cam.x = -(Rx.x * tx_s + Ry.x * ty_s + Rz.x * tz);
            cam.y = -(Rx.y * tx_s + Ry.y * ty_s + Rz.y * tz);
            cam.z = -(Rx.z * tx_s + Ry.z * ty_s + Rz.z * tz);
        }
        return cam;
    }

    static bool is_visible(Vec3 cam_pos, Vec3 target_pos,
                           const std::vector<PlayerInfo>& players,
                           const Matrix4& vm, Vec2 screen) {
        Vec2 s = world_to_screen(vm, target_pos, screen.x, screen.y);
        if (s.x < 0.f && s.y < 0.f) return false;
        if (s.x < 0.f || s.x > screen.x || s.y < 0.f || s.y > screen.y) return false;

        float tdist = (target_pos - cam_pos).length();
        for (const auto& op : players) {
            if (!op.valid) continue;
            float odist = (op.position - cam_pos).length();
            if (odist + 3.f >= tdist) continue;

            Vec2 os = world_to_screen(vm, op.position, screen.x, screen.y);
            if (os.x < 0.f && os.y < 0.f) continue;
            float bh = 60.f, bw = 25.f;
            if (fabsf(s.x - os.x) < bw && fabsf(s.y - os.y) < bh) return false;
        }
        return true;
    }

    static bool is_knocked(const PlayerInfo& p) {
        if (!p.cached_char_ptr || !offsets::ValueBase::Value) return false;
        RbxInstance ch{p.cached_char_ptr};
        RbxInstance bfx = ch.find_child_by_name("BodyEffects");
        if (!bfx.valid()) return false;
        RbxInstance ko = bfx.find_child_by_name("K.O");
        if (!ko.valid()) return false;

        return rpm<uint8_t>(ko.ptr + offsets::ValueBase::Value) != 0;
    }

    struct WallPart { Vec3 pos; Vec3 half; };
    static std::vector<WallPart> g_wall_cache;
    static std::chrono::steady_clock::time_point g_wall_cache_at{};
    static std::mutex g_wall_cache_mtx;

    static bool model_has_humanoid(RbxInstance model) {
        for (auto& c : model.get_children()) {
            if (c.get_class() == "Humanoid") return true;
        }
        return false;
    }

    static void collect_parts_rec(RbxInstance root, std::vector<WallPart>& out, int depth) {
        if (depth < 0 || !root.valid()) return;
        if (out.size() > 2048) return;
        for (auto& c : root.get_children()) {
            if (out.size() > 2048) return;
            std::string cls = c.get_class();
            if (cls == "Model") {
                if (model_has_humanoid(c)) continue;
                collect_parts_rec(c, out, depth - 1);
                continue;
            }
            if (cls == "Folder") {
                collect_parts_rec(c, out, depth - 1);
                continue;
            }
            if (cls != "Part" && cls != "MeshPart" && cls != "UnionOperation" &&
                cls != "TrussPart" && cls != "WedgePart" && cls != "CornerWedgePart") continue;

            uintptr_t prim = rpm<uintptr_t>(c.ptr + offsets::BasePart::Primitive);
            if (!prim) continue;
            Vec3 sz = rpm<Vec3>(prim + offsets::Primitive::Size);
            if (sz.x < 2.0f && sz.y < 2.0f && sz.z < 2.0f) continue;
            Vec3 pos = rpm<Vec3>(prim + offsets::Primitive::Position);
            out.push_back({pos, {sz.x * 0.5f, sz.y * 0.5f, sz.z * 0.5f}});
        }
    }

    static void refresh_wall_cache_if_stale() {
        auto now = std::chrono::steady_clock::now();
        if (!g_wall_cache.empty() &&
            now - g_wall_cache_at < std::chrono::milliseconds(2000)) return;
        g_wall_cache_at = now;
        RbxDataModel dm = RbxDataModel::get();
        if (!dm.valid()) return;
        RbxInstance ws = dm.get_service("Workspace");
        if (!ws.valid()) return;
        std::vector<WallPart> next;
        next.reserve(g_wall_cache.capacity() ? g_wall_cache.capacity() : 256);
        collect_parts_rec(ws, next, 4);
        std::lock_guard<std::mutex> lk(g_wall_cache_mtx);
        g_wall_cache.swap(next);
    }

    static bool wall_los_clear(Vec3 cam, Vec3 target) {
        refresh_wall_cache_if_stale();
        Vec3 d = target - cam;
        float dist = d.length();
        if (dist < 1.f) return true;
        Vec3 dir = { d.x / dist, d.y / dist, d.z / dist };

        std::lock_guard<std::mutex> lk(g_wall_cache_mtx);
        for (auto& wp : g_wall_cache) {
            Vec3 mn = { wp.pos.x - wp.half.x, wp.pos.y - wp.half.y, wp.pos.z - wp.half.z };
            Vec3 mx = { wp.pos.x + wp.half.x, wp.pos.y + wp.half.y, wp.pos.z + wp.half.z };

            float t_near = 0.f, t_far = dist;
            bool miss = false;
            for (int a = 0; a < 3; a++) {
                float o = (&cam.x)[a];
                float dv = (&dir.x)[a];
                float mn_a = (&mn.x)[a];
                float mx_a = (&mx.x)[a];
                if (fabsf(dv) < 1e-6f) {
                    if (o < mn_a || o > mx_a) { miss = true; break; }
                    continue;
                }
                float t1 = (mn_a - o) / dv;
                float t2 = (mx_a - o) / dv;
                if (t1 > t2) std::swap(t1, t2);
                if (t1 > t_near) t_near = t1;
                if (t2 < t_far)  t_far  = t2;
                if (t_near > t_far) { miss = true; break; }
            }
            if (miss) continue;
            if (t_near > 2.5f && t_near < dist - 5.0f) return false;
        }
        return true;
    }

    struct Vec4f     { float x, y, z, w; };
    struct Vec2i16   { int16_t x, y; };
    struct Vec2f     { float x, y; };


    struct RbxCFrame {
        float px, py, pz;
        float r00, r01, r02;
        float r10, r11, r12;
        float r20, r21, r22;
    };

    static inline Vec3 vcross(Vec3 a, Vec3 b) {
        return { a.y*b.z - a.z*b.y,
                 a.z*b.x - a.x*b.z,
                 a.x*b.y - a.y*b.x };
    }
    static inline Vec3 vnorm(Vec3 v) {
        float L = v.length();
        if (L < 1e-6f) return {0.f, 0.f, 1.f};
        float inv = 1.f / L;
        return { v.x*inv, v.y*inv, v.z*inv };
    }

    static inline RbxCFrame make_lookat_cf(Vec3 eye, Vec3 target) {
        Vec3 forward = vnorm(target - eye);
        Vec3 world_up{0.f, 1.f, 0.f};
        Vec3 rx = vcross(forward, world_up);
        if (rx.length() < 1e-4f) rx = {1.f, 0.f, 0.f};
        Vec3 right = vnorm(rx);
        Vec3 up    = vcross(right, forward);
        RbxCFrame cf{};
        cf.px = eye.x; cf.py = eye.y; cf.pz = eye.z;
        cf.r00 = right.x; cf.r01 = up.x; cf.r02 = -forward.x;
        cf.r10 = right.y; cf.r11 = up.y; cf.r12 = -forward.y;
        cf.r20 = right.z; cf.r21 = up.z; cf.r22 = -forward.z;
        return cf;
    }

    static uintptr_t g_silent_mouse_service = 0;
    static uintptr_t g_silent_gui_service   = 0;
    static uintptr_t g_silent_camera        = 0;
    static uintptr_t g_silent_last_dm       = 0;
    static Vec4f     g_silent_inset_orig    = {0.f, 0.f, 0.f, 0.f};
    static bool      g_silent_inset_saved   = false;
    static bool      g_silent_inset_dirty   = false;
    static bool      g_silent_viewport_dirty = false;


    struct VpCandidate { unsigned off; bool is_float; };
    static std::vector<VpCandidate> g_viewport_candidates;
    static bool                     g_viewport_scanned = false;
    static std::chrono::steady_clock::time_point g_viewport_last_scan{};

    static void silent_refresh_caches() {
        RbxDataModel dm = RbxDataModel::get();
        if (!dm.valid()) return;
        if (dm.ptr != g_silent_last_dm) {
            g_silent_last_dm       = dm.ptr;
            g_silent_mouse_service = 0;
            g_silent_gui_service   = 0;
            g_silent_camera        = 0;
            g_silent_inset_saved   = false;
            g_silent_inset_dirty   = false;
            g_silent_viewport_dirty = false;
            g_viewport_candidates.clear();
            g_viewport_scanned      = false;
        }
        if (!g_silent_mouse_service)
            g_silent_mouse_service = dm.get_service("MouseService").ptr;
        if (!g_silent_gui_service)
            g_silent_gui_service   = dm.get_service("GuiService").ptr;

        if (g_silent_gui_service && !g_silent_inset_saved &&
            offsets::GuiService::GuiInset) {
            g_silent_inset_orig = rpm<Vec4f>(
                g_silent_gui_service + offsets::GuiService::GuiInset);
            g_silent_inset_saved = true;
        }


        if (!g_silent_camera && offsets::Workspace::CurrentCamera) {
            RbxInstance ws = dm.get_service("Workspace");
            if (ws.valid())
                g_silent_camera = rpm<uintptr_t>(ws.ptr + offsets::Workspace::CurrentCamera);
        }


        auto now = std::chrono::steady_clock::now();
        bool need_scan = g_silent_camera &&
                         (!g_viewport_scanned ||
                          (g_viewport_candidates.empty() &&
                           now - g_viewport_last_scan > std::chrono::seconds(2)));
        if (need_scan) {
            g_viewport_last_scan = now;
            g_viewport_scanned   = true;
            Vec2 s = g_screen;
            if (s.x > 1.f && s.y > 1.f) {
                constexpr size_t SCAN_BYTES = 0x2000;
                std::vector<uint8_t> buf(SCAN_BYTES);
                SIZE_T got = 0;
                mem::vm_read(mem::g_proc, (LPCVOID)g_silent_camera,
                                  buf.data(), SCAN_BYTES, &got);

                g_viewport_candidates.clear();


                int16_t wx16 = (int16_t)s.x, wy16 = (int16_t)s.y;
                for (size_t off = 0; off + 4 <= got; off += 2) {
                    int16_t x, y;
                    std::memcpy(&x, buf.data() + off,     2);
                    std::memcpy(&y, buf.data() + off + 2, 2);
                    if (std::abs((int)x - (int)wx16) <= 4 &&
                        std::abs((int)y - (int)wy16) <= 4) {
                        g_viewport_candidates.push_back({(unsigned)off, false});
                    }
                }


                for (size_t off = 0; off + 8 <= got; off += 4) {
                    float fx, fy;
                    std::memcpy(&fx, buf.data() + off,     4);
                    std::memcpy(&fy, buf.data() + off + 4, 4);
                    if (std::fabs(fx - s.x) <= 2.f &&
                        std::fabs(fy - s.y) <= 2.f) {
                        g_viewport_candidates.push_back({(unsigned)off, true});
                    }
                }

                if (g_viewport_candidates.empty()) {
                    elog::warn("silent(Rivals): scan found NO viewport-matching fields in cam[0..0x%X]. Camera may not be fully initialized yet — will retry in 2s.",
                               (unsigned)SCAN_BYTES);
                } else {
                    elog::ok("silent(Rivals): scan found %zu viewport candidate(s). Writing to all per tick.",
                             g_viewport_candidates.size());
                    for (auto& c : g_viewport_candidates)
                        elog::info("  candidate: cam+0x%X (%s)",
                                   c.off, c.is_float ? "float32x2" : "int16x2");
                }
            }
        }
    }

    static uintptr_t silent_current_input_object() {
        if (!g_silent_mouse_service || !offsets::MouseService::InputObject) return 0;

        return rpm<uintptr_t>(g_silent_mouse_service
                              + offsets::MouseService::InputObject + 0x10);
    }


    static void silent_restore_inset() {
        if (g_silent_inset_dirty && g_silent_gui_service &&
            g_silent_inset_saved  && offsets::GuiService::GuiInset) {
            wpm<Vec4f>(g_silent_gui_service + offsets::GuiService::GuiInset,
                       g_silent_inset_orig);
            g_silent_inset_dirty = false;
        }
        if (g_silent_viewport_dirty && g_silent_camera) {
            Vec2 s = g_screen;
            if (s.x > 0.f && s.y > 0.f) {
                Vec2i16 vp16{ (int16_t)s.x, (int16_t)s.y };
                Vec2f   vpf { s.x, s.y };
                for (auto& c : g_viewport_candidates) {
                    uintptr_t addr = g_silent_camera + c.off;
                    if (c.is_float) wpm<Vec2f>(addr, vpf);
                    else            wpm<Vec2i16>(addr, vp16);
                }
            }
            g_silent_viewport_dirty = false;
        }
    }

    static void silent_aim_thread() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        uintptr_t sticky_ptr = 0;

        auto once_per_sec = [](auto& tp) {
            auto now = std::chrono::steady_clock::now();
            if (now < tp) return false;
            tp = now + std::chrono::seconds(1);
            return true;
        };
        static auto t_no_svc = std::chrono::steady_clock::now();
        static auto t_no_io  = std::chrono::steady_clock::now();
        static auto t_no_tgt = std::chrono::steady_clock::now();
        static auto t_ok     = std::chrono::steady_clock::now();

        bool prev_silent = false;
        for (;;) {

            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            if (!mem::alive()) break;
            if (!menu::is_injected()) { silent_restore_inset(); prev_silent = false; continue; }
            if (!esp::cfg.silent_aim) { silent_restore_inset(); prev_silent = false; continue; }
            if (check::is_typing())   { silent_restore_inset(); continue; }


            if (!prev_silent) {
                esp::cfg.draw_fov = true;
                prev_silent = true;
            }

            const bool is_rivals_mode  = (games::active() == games::Id::Rivals);
            const bool is_arsenal_mode = (games::active() == games::Id::Arsenal);
            const bool needs_mouse     = !is_rivals_mode && !is_arsenal_mode;


            if (needs_mouse) {
                if (!offsets::MouseService::InputObject ||
                    !offsets::MouseService::MousePosition) {
                    if (once_per_sec(t_no_svc))
                        elog::warn("silent: MouseService offsets missing (InputObject=0x%llX, MousePosition=0x%llX)",
                                   (unsigned long long)offsets::MouseService::InputObject,
                                   (unsigned long long)offsets::MouseService::MousePosition);
                    continue;
                }
            }

            silent_refresh_caches();

            uintptr_t io = 0;
            if (needs_mouse) {
                if (!g_silent_mouse_service) {
                    if (once_per_sec(t_no_svc))
                        elog::warn("silent: MouseService instance not found in DataModel");
                    continue;
                }
                io = silent_current_input_object();
                if (!io) {
                    if (once_per_sec(t_no_io))
                        elog::warn("silent: InputObject pointer is 0 (svc=0x%llX)",
                                   (unsigned long long)g_silent_mouse_service);
                    continue;
                }
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
            if (screen.x < 1.f || screen.y < 1.f) continue;

            Vec2 center = {screen.x * 0.5f, screen.y * 0.5f};
            Vec3 cam_pos = get_camera_pos(vm);
            uintptr_t lteam = g_local_team.load();
            float pred = esp::cfg.aim_predict ? esp::cfg.aim_prediction : 0.f;


            float fov  = esp::cfg.fov_radius > 0.f ? esp::cfg.fov_radius : 200.f;

            const PlayerInfo* best = nullptr;
            Vec2  best_screen{-1.f, -1.f};
            Vec3  best_world{0.f, 0.f, 0.f};
            float best_d = FLT_MAX;

            bool sticky_alive = false;
            for (const auto& p : players) {
                if (p.player_ptr != sticky_ptr) continue;
                if (!p.valid || p.health <= 0.f) break;
                if (esp::cfg.team_check && p.team_ptr && p.team_ptr == lteam) break;
                if (esp::cfg.silent_knocked_check && is_knocked(p)) break;

                Vec3 tw;
                if (esp::cfg.silent_aim_part == 0) {
                    if (p.joint_valid_mask & (1u << J_Head)) tw = p.joint_pos[J_Head];
                    else tw = {p.position.x, p.position.y + 1.5f, p.position.z};
                } else if (esp::cfg.silent_aim_part == 1) {
                    if (p.joint_valid_mask & (1u << J_UpperTorso)) tw = p.joint_pos[J_UpperTorso];
                    else tw = p.position;
                } else {
                    tw = p.position;
                }

                tw = {tw.x + p.velocity.x * pred * esp::cfg.aim_predict_xz,
                      tw.y + p.velocity.y * pred * esp::cfg.aim_predict_y,
                      tw.z + p.velocity.z * pred * esp::cfg.aim_predict_xz};

                Vec2 s = world_to_screen(vm, tw, screen.x, screen.y);
                if (s.x < 0.f && s.y < 0.f) break;
                float dx = s.x - center.x, dy = s.y - center.y;
                float d  = std::sqrt(dx * dx + dy * dy);
                if (d > fov) break;
                if (esp::cfg.aim_max_dist > 0.f &&
                    (tw - cam_pos).length() > esp::cfg.aim_max_dist) break;
                if (esp::cfg.aim_visibility_check &&
                    !is_visible(cam_pos, tw, players, vm, screen)) break;

                best = &p; best_screen = s; best_world = tw; best_d = d;
                sticky_alive = true;
                break;
            }
            if (sticky_alive) goto have_target;

            for (const auto& p : players) {
                if (!p.valid || p.health <= 0.f) continue;
                if (esp::cfg.team_check && p.team_ptr && p.team_ptr == lteam) continue;
                if (esp::cfg.silent_knocked_check && is_knocked(p)) continue;

                Vec3 tw;
                bool have = false;
                if (esp::cfg.silent_aim_part == 0) {
                    if (p.joint_valid_mask & (1u << J_Head)) { tw = p.joint_pos[J_Head]; have = true; }
                    else { tw = {p.position.x, p.position.y + 1.5f, p.position.z}; have = true; }
                } else if (esp::cfg.silent_aim_part == 1) {
                    if (p.joint_valid_mask & (1u << J_UpperTorso)) { tw = p.joint_pos[J_UpperTorso]; have = true; }
                    else                                            { tw = p.position; have = true; }
                } else {

                    float mind = FLT_MAX;
                    for (int j = 0; j < J_COUNT; j++) {
                        if (!(p.joint_valid_mask & (1u << j))) continue;
                        Vec2 s = world_to_screen(vm, p.joint_pos[j], screen.x, screen.y);
                        if (s.x < 0.f && s.y < 0.f) continue;
                        float dx = s.x - center.x, dy = s.y - center.y;
                        float dd = dx * dx + dy * dy;
                        if (dd < mind) { mind = dd; tw = p.joint_pos[j]; have = true; }
                    }
                }
                if (!have) continue;

                tw = {tw.x + p.velocity.x * pred * esp::cfg.aim_predict_xz,
                      tw.y + p.velocity.y * pred * esp::cfg.aim_predict_y,
                      tw.z + p.velocity.z * pred * esp::cfg.aim_predict_xz};

                if (esp::cfg.aim_max_dist > 0.f &&
                    (tw - cam_pos).length() > esp::cfg.aim_max_dist) continue;

                Vec2 s = world_to_screen(vm, tw, screen.x, screen.y);
                if (s.x < 0.f && s.y < 0.f) continue;

                float dx = s.x - center.x, dy = s.y - center.y;
                float d  = std::sqrt(dx * dx + dy * dy);
                if (d > fov) continue;

                if (esp::cfg.aim_visibility_check &&
                    !is_visible(cam_pos, tw, players, vm, screen)) continue;

                if (d < best_d) { best_d = d; best = &p; best_screen = s; best_world = tw; }
            }

            if (!best) {
                sticky_ptr = 0;
                silent_restore_inset();
                if (once_per_sec(t_no_tgt))
                    elog::info("silent: no target in FOV (players=%d)", (int)players.size());
                continue;
            }
            sticky_ptr = best->player_ptr;
        have_target:;

            const bool is_rivals = (games::active() == games::Id::Rivals);

            if (is_rivals) {


                if (!g_silent_camera) {
                    if (once_per_sec(t_no_svc))
                        elog::warn("silent(Rivals): camera not cached yet");
                    continue;
                }
                if (g_viewport_candidates.empty()) {
                    if (once_per_sec(t_no_svc))
                        elog::warn("silent(Rivals): no viewport candidates from struct scan yet — will keep retrying");
                    continue;
                }


                float tx = best_screen.x + esp::cfg.rivals_x_bias;
                float ty = best_screen.y + esp::cfg.rivals_y_bias;

                float safe_tx = std::fabs(tx) < 1.f ? (tx < 0.f ? -1.f : 1.f) : tx;
                float safe_ty = std::fabs(ty) < 1.f ? (ty < 0.f ? -1.f : 1.f) : ty;


                float mx = screen.x * 0.5f, my = screen.y * 0.5f;
                if (g_silent_mouse_service && offsets::MouseService::InputObject &&
                    offsets::MouseService::MousePosition) {
                    uintptr_t io = rpm<uintptr_t>(g_silent_mouse_service
                                    + offsets::MouseService::InputObject + 0x10);
                    if (io) {
                        Vec2 mp = rpm<Vec2>(io + offsets::MouseService::MousePosition);
                        if (mp.x > 1.f && mp.y > 1.f) { mx = mp.x; my = mp.y; }
                    }
                }

                float dx, dy;
                if (esp::cfg.rivals_formula == 1) {

                    dx = mx * screen.x / safe_tx;
                    dy = my * screen.y / safe_ty;
                } else {

                    dx = 2.f * (screen.x - tx);
                    dy = 2.f * (screen.y - ty);
                }
                if (esp::cfg.rivals_flip_x) dx = -dx;
                if (esp::cfg.rivals_flip_y) dy = -dy;

                Vec2i16 vp16{ (int16_t)dx, (int16_t)dy };
                Vec2f   vpf { dx, dy };


                bool firing_ok = !esp::cfg.silent_only_when_firing ||
                                 (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
                if (!firing_ok) {


                    silent_restore_inset();
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }


                Vec2f   dim_f  { screen.x, screen.y };
                Vec2i16 dim_i16{ (int16_t)screen.x, (int16_t)screen.y };
                for (auto& c : g_viewport_candidates) {
                    uintptr_t addr = g_silent_camera + c.off;
                    if (c.is_float) {
                        for (int i = 0; i < 20; i++) wpm<Vec2f>(addr, vpf);
                        wpm<Vec2f>(addr, dim_f);
                    } else {
                        for (int i = 0; i < 20; i++) wpm<Vec2i16>(addr, vp16);
                        wpm<Vec2i16>(addr, dim_i16);
                    }
                }
                g_silent_viewport_dirty = true;

                if (once_per_sec(t_ok)) {


                    Vec2f back{0.f, 0.f};
                    bool have_back = false;
                    for (auto& c : g_viewport_candidates) if (c.is_float) {
                        mem::vm_read(mem::g_proc,
                            (LPCVOID)(g_silent_camera + c.off),
                            &back, sizeof(back), nullptr);
                        have_back = true;
                        break;
                    }
                    elog::ok("silent(Rivals): tgt='%s' target=(%.0f,%.0f) vp_f=(%.1f,%.1f) back=(%.1f,%.1f) candidates=%zu%s",
                        best->name.c_str(), best_screen.x, best_screen.y,
                        vpf.x, vpf.y,
                        have_back ? back.x : 0.f, have_back ? back.y : 0.f,
                        g_viewport_candidates.size(),
                        (have_back && std::fabs(back.x - vpf.x) < 1.f &&
                         std::fabs(back.y - vpf.y) < 1.f) ? " OK" :
                        (have_back ? " OVERWRITTEN" : ""));
                }


                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else if (games::active() == games::Id::Arsenal) {


                if (!g_silent_camera || !offsets::Camera::CFrame ||
                    !offsets::BasePart::Primitive ||
                    !offsets::Primitive::CFrame) {
                    if (once_per_sec(t_no_svc))
                        elog::warn("silent(Arsenal): missing offset — cam=0x%llX cf=0x%llX prim=0x%llX pcf=0x%llX",
                                   (unsigned long long)g_silent_camera,
                                   (unsigned long long)offsets::Camera::CFrame,
                                   (unsigned long long)offsets::BasePart::Primitive,
                                   (unsigned long long)offsets::Primitive::CFrame);
                    continue;
                }

                bool lmb_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;


                static bool prev_lmb = false;
                bool edge_fire  = lmb_down && !prev_lmb;
                bool cont_fire  = esp::cfg.arsenal_continuous_flick && lmb_down;
                prev_lmb = lmb_down;

                bool should_fire =
                    (!esp::cfg.silent_only_when_firing) ||
                    edge_fire ||
                    cont_fire;

                if (!should_fire) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                if (!best->hrp_ptr) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                uintptr_t target_prim =
                    rpm<uintptr_t>(best->hrp_ptr + offsets::BasePart::Primitive);
                if (!target_prim) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                const uintptr_t tgt_cf_addr = target_prim + offsets::Primitive::CFrame;


                RbxCFrame ucam{};
                mem::vm_read(mem::g_proc,
                    (LPCVOID)(g_silent_camera + offsets::Camera::CFrame),
                    &ucam, sizeof(ucam), nullptr);
                if (ucam.px == 0.f && ucam.py == 0.f && ucam.pz == 0.f) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }

                Vec3 fwd{ -ucam.r02, -ucam.r12, -ucam.r22 };
                Vec3 eye{ ucam.px, ucam.py, ucam.pz };
                Vec3 hit_pos{
                    eye.x + fwd.x * 10.f,
                    eye.y + fwd.y * 10.f,
                    eye.z + fwd.z * 10.f
                };


                RbxCFrame torig{};
                mem::vm_read(mem::g_proc, (LPCVOID)tgt_cf_addr,
                                  &torig, sizeof(torig), nullptr);


                RbxCFrame warp = torig;
                warp.px = hit_pos.x;
                warp.py = hit_pos.y;
                warp.pz = hit_pos.z;


                for (int i = 0; i < 8; i++)
                    wpm<RbxCFrame>(tgt_cf_addr, warp);
                wpm<RbxCFrame>(tgt_cf_addr, torig);

                if (once_per_sec(t_ok)) {
                    elog::ok("silent(Arsenal HRP warp %s): tgt='%s' hit_pos=(%.1f,%.1f,%.1f)",
                        cont_fire ? "continuous" : "edge",
                        best->name.c_str(), hit_pos.x, hit_pos.y, hit_pos.z);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(
                    cont_fire ? 2 : 8));
            } else {


                if (!g_silent_gui_service || !offsets::GuiService::GuiInset) {
                    if (once_per_sec(t_no_svc))
                        elog::warn("silent: GuiService offset missing (svc=0x%llX inset=0x%llX)",
                                   (unsigned long long)g_silent_gui_service,
                                   (unsigned long long)offsets::GuiService::GuiInset);
                    continue;
                }

                Vec2 mouse{0.f, 0.f};
                mem::vm_read(mem::g_proc,
                    (LPCVOID)(io + offsets::MouseService::MousePosition),
                    &mouse, sizeof(mouse), nullptr);

                Vec4f new_inset{
                    best_screen.x - mouse.x,
                    58.f + best_screen.y - mouse.y,
                    g_silent_inset_orig.z,
                    g_silent_inset_orig.w
                };
                wpm<Vec4f>(g_silent_gui_service + offsets::GuiService::GuiInset,
                           new_inset);
                g_silent_inset_dirty = true;

                if (once_per_sec(t_ok)) {
                    elog::ok("silent(GuiInset): tgt='%s' target=(%.0f,%.0f) mouse=(%.0f,%.0f) inset=(%.1f,%.1f)",
                        best->name.c_str(), best_screen.x, best_screen.y,
                        mouse.x, mouse.y, new_inset.x, new_inset.y);
                }
            }
        }

        silent_restore_inset();
    }

    static void freeze_player_thread() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            if (!mem::alive()) break;
            if (!menu::is_injected()) continue;
            if (!esp::cfg.freeze_player) continue;
            if (check::is_typing()) continue;

            if (esp::cfg.freeze_player_key &&
                !(GetAsyncKeyState(esp::cfg.freeze_player_key) & 0x8000)) continue;

            uintptr_t tgt = g_aim_target.load();
            if (!tgt) continue;

            std::vector<PlayerInfo> players;
            {
                std::lock_guard<std::mutex> lock(g_players_mtx);
                players = g_players;
            }
            for (const auto& p : players) {
                if (p.player_ptr != tgt || !p.hrp_ptr) continue;
                if (!offsets::BasePart::Primitive ||
                    !offsets::Primitive::AssemblyLinearVelocity) break;
                uintptr_t prim = rpm<uintptr_t>(p.hrp_ptr + offsets::BasePart::Primitive);
                if (!prim) break;
                Vec3 zero{0.f, 0.f, 0.f};
                wpm<Vec3>(prim + offsets::Primitive::AssemblyLinearVelocity, zero);
                if (offsets::Primitive::AssemblyAngularVelocity)
                    wpm<Vec3>(prim + offsets::Primitive::AssemblyAngularVelocity, zero);
                break;
            }
        }
    }

    static void tickrate_thread() {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!mem::alive()) break;
            if (!menu::is_injected()) continue;

            RbxDataModel dm = RbxDataModel::get();
            if (!dm.valid() || !offsets::Workspace::World ||
                !offsets::World::WorldStepsPerSec) continue;
            RbxInstance ws = dm.get_service("Workspace");
            if (!ws.valid()) continue;
            uintptr_t world = rpm<uintptr_t>(ws.ptr + offsets::Workspace::World);
            if (!world) continue;

            if (esp::cfg.tickrate_enabled) {
                float amt = esp::cfg.tickrate_amount > 1.f ? esp::cfg.tickrate_amount : 60.f;

                wpm<float>(world + offsets::World::WorldStepsPerSec, amt * 4.f);
            }
        }
    }

    void start_thread() {
        std::thread(silent_aim_thread).detach();
        std::thread(freeze_player_thread).detach();
        std::thread(tickrate_thread).detach();
        std::thread([]() {

            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
            float smoothed_dx = 0.f;
            float smoothed_dy = 0.f;

            float carry_x = 0.f;
            float carry_y = 0.f;

            auto last_time    = std::chrono::high_resolution_clock::now();
            auto next_trigger = std::chrono::steady_clock::now();
            auto next_autofire= std::chrono::steady_clock::now();

            while (true) {
                if (!mem::alive()) break;

                auto now = std::chrono::high_resolution_clock::now();
                float delta_time = std::chrono::duration<float>(now - last_time).count();
                last_time = now;

                std::vector<PlayerInfo> players;
                Matrix4 vm;
                Vec2 screen;
                {
                    std::lock_guard<std::mutex> lock(g_players_mtx);
                    players = g_players;
                    std::copy(std::begin(g_view_matrix), std::end(g_view_matrix), std::begin(vm));
                    screen = g_screen;
                }

                if (screen.x <= 0 || screen.y <= 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }

                Vec2 center = { screen.x * 0.5f, screen.y * 0.5f };
                Vec3 cam_pos = get_camera_pos(vm);
                float pred = esp::cfg.aim_predict ? esp::cfg.aim_prediction : 0.f;
                uintptr_t local_team = g_local_team.load();

                auto pick_point = [&](const PlayerInfo& pl) -> AimPoint {

                    Vec3 head;
                    if (pl.joint_valid_mask & (1u << J_Head)) {
                        head = pl.joint_pos[J_Head];
                    } else {
                        head = { pl.position.x, pl.position.y + 1.5f, pl.position.z };
                    }

                    Vec3 chest = { pl.position.x, pl.position.y + 0.0f, pl.position.z };
                    Vec3 belly = { pl.position.x, pl.position.y - 1.0f, pl.position.z };
                    Vec3 cands[3]; int nc = 0;
                    if (esp::cfg.aim_lock) {
                        cands[nc++] = head;
                    } else if (esp::cfg.aim_multibone) {
                        cands[nc++] = head;
                        cands[nc++] = chest;
                        cands[nc++] = belly;
                    } else {
                        cands[nc++] = (esp::cfg.aim_bone == 0) ? head : chest;
                    }
                    AimPoint best{}; best.dist = FLT_MAX; best.valid = false;
                    for (int i = 0; i < nc; i++) {
                        Vec3 tp = { cands[i].x + pl.velocity.x * pred * esp::cfg.aim_predict_xz,
                                    cands[i].y + pl.velocity.y * pred * esp::cfg.aim_predict_y,
                                    cands[i].z + pl.velocity.z * pred * esp::cfg.aim_predict_xz };
                        Vec2 s = world_to_screen(vm, tp, screen.x, screen.y);
                        if (s.x < 0.f && s.y < 0.f) continue;
                        float dx = s.x - center.x, dy = s.y - center.y;
                        float d = std::sqrt(dx * dx + dy * dy);
                        if (d < best.dist) best = { tp, s, d, true };
                    }
                    return best;
                };

                {
                    static uintptr_t candidate = 0;
                    static std::chrono::steady_clock::time_point candidate_since{};
                    static bool trigger_lmb_down = false;

                    bool key_down_trig = (GetAsyncKeyState(esp::cfg.trigger_key) & 0x8000) != 0;
                    bool trigger_active = esp::cfg.trigger_enabled && key_down_trig;

                    float scaled_fov = esp::cfg.trigger_fov * esp::cfg.trig_box_scale;
                    if (scaled_fov < 1.f) scaled_fov = 1.f;
                    uintptr_t in_fov = 0;

                    if (trigger_active) {
                        for (auto& p : players) {
                            if (!p.valid || p.health <= 0) continue;
                            if ((esp::cfg.team_check || esp::cfg.trigger_team_check) &&
                                p.team_ptr && p.team_ptr == local_team) continue;
                            if (esp::cfg.aim_knocked_check && is_knocked(p)) continue;

                            if (esp::cfg.trig_knife_check && !p.weapon.empty()) {
                                std::string w = p.weapon;
                                for (auto& ch : w) ch = (char)std::tolower((unsigned char)ch);
                                if (w.find("knife")  != std::string::npos ||
                                    w.find("bat")    != std::string::npos ||
                                    w.find("hammer") != std::string::npos ||
                                    w.find("melee")  != std::string::npos) continue;
                            }
                            Vec3 tpos = { p.position.x + p.velocity.x * pred * esp::cfg.aim_predict_xz,
                                          p.position.y + 0.5f + p.velocity.y * pred * esp::cfg.aim_predict_y,
                                          p.position.z + p.velocity.z * pred * esp::cfg.aim_predict_xz };
                            Vec2 s = world_to_screen(vm, tpos, screen.x, screen.y);
                            if (s.x < 0.f && s.y < 0.f) continue;
                            float dx = s.x - center.x, dy = s.y - center.y;
                            if (std::sqrt(dx * dx + dy * dy) > scaled_fov) continue;

                            Vec3 head_pos = (p.joint_valid_mask & (1u << J_Head))
                                          ? p.joint_pos[J_Head] : tpos;
                            if (esp::cfg.trigger_wallcheck &&
                                !wall_los_clear(cam_pos, head_pos)) continue;

                            in_fov = p.player_ptr;
                            break;
                        }
                    }

                    auto now2 = std::chrono::steady_clock::now();

                    if (esp::cfg.trigger_hold_mode) {
                        if (in_fov && in_fov != candidate) {
                            candidate = in_fov;
                            candidate_since = now2;
                        }
                        if (!in_fov) candidate = 0;

                        bool acquired = candidate != 0 &&
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                now2 - candidate_since).count() >= esp::cfg.trig_acquire_ms;
                        bool should_hold = trigger_active && acquired && in_fov;

                        if (should_hold && !trigger_lmb_down) {
                            INPUT in{}; in.type = INPUT_MOUSE;
                            in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                            SendInput(1, &in, sizeof(INPUT));
                            trigger_lmb_down = true;
                        } else if (!should_hold && trigger_lmb_down) {
                            INPUT in{}; in.type = INPUT_MOUSE;
                            in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                            SendInput(1, &in, sizeof(INPUT));
                            trigger_lmb_down = false;
                        }
                    } else {
                        if (trigger_lmb_down) {
                            INPUT in{}; in.type = INPUT_MOUSE;
                            in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                            SendInput(1, &in, sizeof(INPUT));
                            trigger_lmb_down = false;
                        }
                        if (trigger_active && in_fov) {
                            if (in_fov != candidate) {
                                candidate = in_fov;
                                candidate_since = now2;
                            }
                            long long held = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now2 - candidate_since).count();
                            if (held >= esp::cfg.trig_acquire_ms && now2 >= next_trigger) {
                                send_click();
                                next_trigger = now2 + std::chrono::milliseconds(esp::cfg.trigger_delay);
                            }
                        } else if (!in_fov) {
                            candidate = 0;
                        }
                    }
                }

                bool key_down = (GetAsyncKeyState(esp::cfg.aim_key) & 0x8000) != 0;
                static bool toggle_active = false, prev_key = false;
                if (esp::cfg.aim_toggle_mode && key_down && !prev_key) toggle_active = !toggle_active;
                prev_key = key_down;
                bool aiming = esp::cfg.aim_enabled &&
                              (esp::cfg.aim_toggle_mode ? toggle_active : key_down);

                if (check::is_typing()) aiming = false;

                static uintptr_t locked_ptr = 0;
                static std::chrono::steady_clock::time_point locked_lost_at{};

                if (!aiming) {
                    smoothed_dx = 0.f;
                    smoothed_dy = 0.f;
                    carry_x = 0.f;
                    carry_y = 0.f;
                    g_aim_target = 0;
                    locked_ptr = 0;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }

                PlayerInfo* best_target = nullptr;
                float best_score = FLT_MAX;

                bool hold_mode = (esp::cfg.aim_lock || esp::cfg.aim_sticky) && locked_ptr != 0;
                if (hold_mode) {
                    PlayerInfo* held = nullptr;

                    bool dead_drop = false;
                    bool found_ptr = false;
                    for (auto& p : players) {
                        if (p.player_ptr != locked_ptr) continue;
                        found_ptr = true;

                        if (p.health <= 0) { dead_drop = true; break; }
                        if (esp::cfg.aim_knocked_check && is_knocked(p)) { dead_drop = true; break; }

                        if (!p.valid) break;
                        if (esp::cfg.team_check && p.team_ptr && p.team_ptr == local_team) break;
                        if (esp::cfg.aim_max_dist > 0.f &&
                            (p.position - cam_pos).length() > esp::cfg.aim_max_dist) break;
                        AimPoint ap = pick_point(p);
                        if (!ap.valid) break;
                        if (esp::cfg.aim_wallcheck &&
                            !wall_los_clear(cam_pos, ap.world)) break;

                        float slack = esp::cfg.aim_lock ? 3.f : 1.6f;
                        if (ap.dist > esp::cfg.fov_radius * slack) break;
                        held = &p;
                        break;
                    }

                    if (!found_ptr) dead_drop = true;

                    if (held) {
                        best_target = held;
                        locked_lost_at = std::chrono::steady_clock::time_point{};
                    } else if (dead_drop) {

                        elog::info("aim: target dropped (dead/left) — unlocked");
                        locked_ptr = 0;
                        locked_lost_at = std::chrono::steady_clock::time_point{};
                        smoothed_dx = 0.f;
                        smoothed_dy = 0.f;
                    } else {

                        auto now2 = std::chrono::steady_clock::now();
                        if (locked_lost_at == std::chrono::steady_clock::time_point{})
                            locked_lost_at = now2;
                        auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        now2 - locked_lost_at).count();
                        if (since > 350) {
                            locked_ptr = 0;
                            locked_lost_at = std::chrono::steady_clock::time_point{};
                        }
                    }
                }

                if (!best_target) {
                    for (auto& p : players) {
                        if (!p.valid || p.health <= 0) continue;
                        if (esp::cfg.team_check && p.team_ptr && p.team_ptr == local_team) continue;
                        if (esp::cfg.aim_knocked_check && is_knocked(p)) continue;
                        if (esp::cfg.aim_max_dist > 0.f &&
                            (p.position - cam_pos).length() > esp::cfg.aim_max_dist) continue;

                        AimPoint ap = pick_point(p);
                        if (!ap.valid) continue;
                        if (esp::cfg.aim_visibility_check &&
                            !is_visible(cam_pos, ap.world, players, vm, screen)) continue;
                        if (esp::cfg.aim_wallcheck &&
                            !wall_los_clear(cam_pos, ap.world)) continue;
                        float dist = ap.dist;
                        if (dist > esp::cfg.fov_radius) continue;

                        float score = 0.f;
                        if (esp::cfg.aim_priority == 0) {
                            score = dist;
                        } else if (esp::cfg.aim_priority == 1) {
                            score = p.health;
                        } else if (esp::cfg.aim_priority == 2) {
                            if (p.role == 1) score = 1.f;
                            else if (p.role == 2) score = 2.f;
                            else score = 3.f;
                            score += (dist / 10000.f);
                        }

                        if (p.player_ptr == locked_ptr) score *= 0.6f;

                        if (score < best_score) {
                            best_score = score;
                            best_target = &p;
                        }
                    }
                    if (best_target && (esp::cfg.aim_lock || esp::cfg.aim_sticky)) {
                        locked_ptr = best_target->player_ptr;
                        locked_lost_at = std::chrono::steady_clock::time_point{};
                    }
                }

                {
                    static uintptr_t prev_lock = 0;
                    uintptr_t now_lock = best_target ? best_target->player_ptr : 0;
                    if (now_lock != prev_lock) {
                        if (now_lock)
                            elog::info("aim: locked '%s' (score=%.1f)",
                                       best_target->name.c_str(), best_score);
                        else if (prev_lock)
                            elog::info("aim: target lost");
                        prev_lock = now_lock;
                    }
                }
                g_aim_target = best_target ? best_target->player_ptr : 0;

                if (best_target) {
                    AimPoint ap = pick_point(*best_target);
                    Vec2 p_screen = ap.valid ? ap.screen
                        : world_to_screen(vm, best_target->position, screen.x, screen.y);

                    float raw_dx = p_screen.x - center.x;
                    float raw_dy = p_screen.y - center.y;

                    if (esp::cfg.aim_lock) {

                        float mx = raw_dx;
                        float my = raw_dy;
                        constexpr float LOCK_CAP = 1500.f;
                        if (mx >  LOCK_CAP) mx =  LOCK_CAP;
                        if (mx < -LOCK_CAP) mx = -LOCK_CAP;
                        if (my >  LOCK_CAP) my =  LOCK_CAP;
                        if (my < -LOCK_CAP) my = -LOCK_CAP;

                        if (esp::cfg.aim_shake) {
                            std::normal_distribution<float> sx(0.f, esp::cfg.aim_shake_x);
                            std::normal_distribution<float> sy2(0.f, esp::cfg.aim_shake_y);
                            mx += sx(gen);
                            my += sy2(gen);
                        }
                        mx += carry_x;
                        my += carry_y;
                        float emit_x = std::round(mx);
                        float emit_y = std::round(my);
                        carry_x = mx - emit_x;
                        carry_y = my - emit_y;
                        if (emit_x != 0.f || emit_y != 0.f)
                            move_mouse(emit_x, emit_y);
                        smoothed_dx = 0.f;
                        smoothed_dy = 0.f;
                    } else if (esp::cfg.aim_deadzone > 0.f &&
                               fabsf(raw_dx) < esp::cfg.aim_deadzone &&
                               fabsf(raw_dy) < esp::cfg.aim_deadzone) {
                        smoothed_dx = 0.f;
                        smoothed_dy = 0.f;
                    } else {
                        float strength_mult = esp::cfg.aim_strength / 100.f;

                        float sy = esp::cfg.aim_split_smooth ? esp::cfg.aim_smooth_y : esp::cfg.aim_smooth;
                        float tx = std::clamp(esp::cfg.aim_smooth * (delta_time * 200.f), 0.01f, 1.0f);
                        float ty = std::clamp(sy * (delta_time * 200.f), 0.01f, 1.0f);

                        smoothed_dx = smoothed_dx + (raw_dx * strength_mult - smoothed_dx) * tx;
                        smoothed_dy = smoothed_dy + (raw_dy * strength_mult - smoothed_dy) * ty;

                        float move_x = smoothed_dx / 1.5f;
                        float move_y = smoothed_dy / 1.5f;

                        if (esp::cfg.aim_jitter) {
                            std::normal_distribution<float> d(0.0f, esp::cfg.aim_jitter_strength);
                            move_x += d(gen);
                            move_y += d(gen);
                        }
                        if (esp::cfg.aim_shake) {
                            std::normal_distribution<float> sx(0.f, esp::cfg.aim_shake_x);
                            std::normal_distribution<float> sy2(0.f, esp::cfg.aim_shake_y);
                            move_x += sx(gen);
                            move_y += sy2(gen);
                        }

                        move_x = std::clamp(move_x, -250.f, 250.f);
                        move_y = std::clamp(move_y, -250.f, 250.f);
                        move_mouse(move_x, move_y);
                    }

                    if (esp::cfg.aim_autofire &&
                        fabsf(raw_dx) < esp::cfg.aim_autofire_fov &&
                        fabsf(raw_dy) < esp::cfg.aim_autofire_fov) {
                        auto n2 = std::chrono::steady_clock::now();
                        if (n2 >= next_autofire) {
                            send_click();
                            next_autofire = n2 + std::chrono::milliseconds(esp::cfg.trigger_delay);
                        }
                    }
                } else {
                    smoothed_dx = 0.f;
                    smoothed_dy = 0.f;
                    carry_x = 0.f;
                    carry_y = 0.f;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }).detach();
    }
}
