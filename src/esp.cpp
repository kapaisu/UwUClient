#include "../include/esp.hpp"
#include "../include/memory.hpp"
#include "../include/offsets.hpp"
#include "../include/aimbot.hpp"
#include "../include/log.hpp"
#include "../include/executor.hpp"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>
#include <Windows.h>
#include "../include/DebugTab.hpp"

extern std::atomic<uintptr_t> g_local_team;
extern std::atomic<uintptr_t> g_aim_target;

namespace esp {

Config cfg;

static ImU32 acc(float a = 1.f) {
    return IM_COL32((int)(cfg.accent[0] * 255), (int)(cfg.accent[1] * 255),
                    (int)(cfg.accent[2] * 255), (int)(a * 255));
}

static bool g_flat_mode = false;
void set_flat_mode(bool on) { g_flat_mode = on; }

struct Notif { std::string text; double t; };
static std::vector<Notif> g_notifs;
void notify(const std::string& msg) {
    g_notifs.push_back({msg, ImGui::GetTime()});
    if (g_notifs.size() > 6) g_notifs.erase(g_notifs.begin());
}

struct KillEntry { std::string text; double t; bool is_kill; };
static std::vector<KillEntry> g_killfeed;
static double g_last_hit_flash = -100.0;
static void push_killfeed(const std::string& s, bool is_kill) {
    g_killfeed.push_back({s, ImGui::GetTime(), is_kill});
    if (g_killfeed.size() > 8) g_killfeed.erase(g_killfeed.begin());
}
static double steady_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void async_beep(int freq, int dur) {
    std::thread([freq, dur]() { Beep((DWORD)freq, (DWORD)dur); }).detach();
}

static constexpr uint32_t CFG_MAGIC = 0x54455845;

bool save_config(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    uint32_t magic = CFG_MAGIC, sz = (uint32_t)sizeof(Config);
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&sz, sizeof(sz), 1, f);
    fwrite(&cfg, sizeof(Config), 1, f);
    fclose(f);
    return true;
}

bool load_config(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint32_t magic = 0, sz = 0;
    bool ok = false;
    if (fread(&magic, sizeof(magic), 1, f) == 1 &&
        fread(&sz, sizeof(sz), 1, f) == 1 &&
        magic == CFG_MAGIC && sz == sizeof(Config)) {
        ok = (fread(&cfg, sizeof(Config), 1, f) == 1);
    }
    fclose(f);
    return ok;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string b64_encode(const uint8_t* d, size_t n) {
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = d[i] << 16;
        if (i + 1 < n) v |= d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        o += B64[(v >> 18) & 63];
        o += B64[(v >> 12) & 63];
        o += (i + 1 < n) ? B64[(v >> 6) & 63] : '=';
        o += (i + 2 < n) ? B64[v & 63] : '=';
    }
    return o;
}
static bool b64_decode(const std::string& s, std::vector<uint8_t>& out) {
    int rev[256]; for (int i = 0; i < 256; i++) rev[i] = -1;
    for (int i = 0; i < 64; i++) rev[(uint8_t)B64[i]] = i;
    uint32_t v = 0; int bits = 0;
    for (char c : s) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int d = rev[(uint8_t)c];
        if (d < 0) return false;
        v = (v << 6) | d; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((v >> bits) & 0xFF)); }
    }
    return !out.empty();
}

std::string export_config_string() {
    std::vector<uint8_t> buf;
    uint32_t magic = CFG_MAGIC, sz = (uint32_t)sizeof(Config);
    auto app = [&](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p; buf.insert(buf.end(), b, b + n); };
    app(&magic, sizeof(magic)); app(&sz, sizeof(sz)); app(&cfg, sizeof(Config));
    return b64_encode(buf.data(), buf.size());
}
bool import_config_string(const std::string& s) {
    std::vector<uint8_t> buf;
    if (!b64_decode(s, buf)) return false;
    if (buf.size() != 8 + sizeof(Config)) return false;
    uint32_t magic, sz;
    memcpy(&magic, buf.data(), 4); memcpy(&sz, buf.data() + 4, 4);
    if (magic != CFG_MAGIC || sz != sizeof(Config)) return false;
    memcpy(&cfg, buf.data() + 8, sizeof(Config));
    return true;
}

static void set_clipboard(const std::string& s) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
    if (h) {
        void* p = GlobalLock(h);
        if (p) { memcpy(p, s.c_str(), s.size() + 1); GlobalUnlock(h); SetClipboardData(CF_TEXT, h); }
    }
    CloseClipboard();
}
static std::string get_clipboard() {
    std::string r;
    if (!OpenClipboard(nullptr)) return r;
    HANDLE h = GetClipboardData(CF_TEXT);
    if (h) { char* p = (char*)GlobalLock(h); if (p) { r = p; GlobalUnlock(h); } }
    CloseClipboard();
    return r;
}

static void DrawTextWithShadow(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text) {
    if (cfg.text_shadow) {
        dl->AddText({pos.x + 1.f, pos.y + 1.f}, IM_COL32(0, 0, 0, 255), text);
    }
    dl->AddText(pos, col, text);
}

static ImU32 to_u32(ImVec4 c) {
    return IM_COL32((int)(c.x*255),(int)(c.y*255),(int)(c.z*255),(int)(c.w*255));
}

static Vec2 g_view_origin{0.f, 0.f};

void set_view_origin(Vec2 origin) { g_view_origin = origin; }

static bool project(const Matrix4& vm, Vec3 world, Vec2 screen, ImVec2& out) {
    Vec2 p = world_to_screen(vm, world, screen.x, screen.y);
    if (p.x < 0.f && p.y < 0.f) return false;
    out = {p.x + g_view_origin.x, p.y + g_view_origin.y};
    return true;
}

static bool get_box(const Matrix4& vm, Vec3 head_world, Vec3 feet_world, Vec2 screen,
                    ImVec2& tl, ImVec2& br) {
    ImVec2 sf, sh;
    if (!project(vm, feet_world, screen, sf)) return false;
    if (!project(vm, head_world, screen, sh)) return false;

    float h = sf.y - sh.y;
    float w = h * 0.38f;

    tl = {sh.x - w, sh.y};
    br = {sh.x + w, sf.y};
    return (br.x - tl.x > 2.f) && (br.y - tl.y > 2.f);
}

static ImU32 hp_color(float hp, float max_hp) {
    float t = (max_hp > 0.f) ? (hp / max_hp) : 0.f;
    if (t > 0.5f) {
        float r = (1.f - t) * 2.f;
        return IM_COL32((int)(r*220), 200, 0, 230);
    }
    float g = t * 2.f;
    return IM_COL32(220, (int)(g*200), 0, 230);
}

static bool is_on_screen(const Matrix4& vm, Vec3 w, Vec2 screen) {

    float cw = vm[12]*w.x + vm[13]*w.y + vm[14]*w.z + vm[15];
    if (cw <= 0.f) return false;
    Vec2 s = world_to_screen(vm, w, screen.x, screen.y);
    return s.x >= 0.f && s.x <= screen.x && s.y >= 0.f && s.y <= screen.y;
}

static void draw_offscreen_arrow(ImDrawList* dl, const Matrix4& vm, Vec3 w, Vec2 screen, ImU32 col) {
    float cx = vm[0]*w.x + vm[1]*w.y + vm[2]*w.z + vm[3];
    float cy = vm[4]*w.x + vm[5]*w.y + vm[6]*w.z + vm[7];
    float cw = vm[12]*w.x + vm[13]*w.y + vm[14]*w.z + vm[15];
    if (fabsf(cw) < 1e-4f) return;
    float nx = cx / cw, ny = cy / cw;
    if (cw < 0.f) { nx = -nx; ny = -ny; }

    ImVec2 c = { g_view_origin.x + screen.x * 0.5f,
                 g_view_origin.y + screen.y * 0.5f };
    float dx = nx, dy = -ny;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1e-4f) return;
    dx /= len; dy /= len;

    float radius = (screen.x < screen.y ? screen.x : screen.y) * 0.34f;
    ImVec2 p = { c.x + dx*radius, c.y + dy*radius };
    float a = atan2f(dy, dx), s = 13.f;
    ImVec2 t1 = { p.x + cosf(a)*s,        p.y + sinf(a)*s };
    ImVec2 t2 = { p.x + cosf(a+2.5f)*s,   p.y + sinf(a+2.5f)*s };
    ImVec2 t3 = { p.x + cosf(a-2.5f)*s,   p.y + sinf(a-2.5f)*s };
    dl->AddTriangleFilled(t1, t2, t3, col);
    dl->AddTriangle(t1, t2, t3, IM_COL32(0,0,0,255), 1.5f);
}

void render(ImDrawList* dl, const std::vector<PlayerInfo>& players,
            const Matrix4& vm, Vec2 screen) {
    if (!cfg.enabled || !dl) return;

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

    ImVec2 screen_center = { g_view_origin.x + screen.x * 0.5f,
                             g_view_origin.y + screen.y * 0.5f };

    uintptr_t aim_tgt = g_aim_target.load();
    ImVec2    tgt_screen{};
    bool      tgt_found = false;

    for (const auto& p : players) {
        if (!p.valid) continue;
        if (cfg.esp_team_check && p.team_ptr && p.team_ptr == g_local_team.load()) continue;

        Vec3 delta = p.position - cam;
        float dist = delta.length();
        if (dist > cfg.max_dist) continue;

        if (!is_on_screen(vm, p.position, screen)) {
            if (cfg.draw_arrows) {
                ImU32 acol = to_u32(cfg.col_box);
                if (p.role == 1)      acol = IM_COL32(255, 30, 30, 255);
                else if (p.role == 2) acol = IM_COL32(30, 120, 255, 255);
                draw_offscreen_arrow(dl, vm, p.position, screen, acol);
            }
            continue;
        }

        Vec3 head_world = p.position;
        Vec3 feet_world = p.position;
        {
            uint32_t m = p.joint_valid_mask;
            bool have_head = (m & (1u << J_Head)) != 0;
            bool have_lf   = (m & (1u << J_LFoot)) != 0;
            bool have_rf   = (m & (1u << J_RFoot)) != 0;

            if (have_head) {

                head_world = p.joint_pos[J_Head];
                head_world.y += 0.6f;
            } else {

                head_world.y = p.position.y + 1.6f;
            }

            if (have_lf || have_rf) {
                Vec3 f = have_lf ? p.joint_pos[J_LFoot] : p.joint_pos[J_RFoot];
                if (have_lf && have_rf) {
                    Vec3 g = p.joint_pos[J_RFoot];
                    f = { (f.x + g.x) * 0.5f, (f.y + g.y) * 0.5f, (f.z + g.z) * 0.5f };
                }
                feet_world = f;
                feet_world.y -= 0.4f;
            } else {

                feet_world.y = p.position.y - 2.5f;
            }

            head_world.x = p.position.x; head_world.z = p.position.z;
            feet_world.x = p.position.x; feet_world.z = p.position.z;

            float span = head_world.y - feet_world.y;
            if (span < 0.5f || span > 30.f) {
                head_world = { p.position.x, p.position.y + 1.6f, p.position.z };
                feet_world = { p.position.x, p.position.y - 2.5f, p.position.z };
            }
        }

        ImVec2 tl, br;
        if (!get_box(vm, head_world, feet_world, screen, tl, br)) continue;

        float bw = br.x - tl.x;
        float bh = br.y - tl.y;

        ImU32 box_col  = to_u32(cfg.col_box);
        ImU32 name_col = to_u32(cfg.col_name);
        ImU32 line_col = to_u32(cfg.col_line);

        if (p.role == 1) {
            box_col  = IM_COL32(255, 30, 30, 255);
            name_col = IM_COL32(255, 30, 30, 255);
            line_col = IM_COL32(255, 30, 30, 255);
        } else if (p.role == 2) {
            box_col  = IM_COL32(30, 120, 255, 255);
            name_col = IM_COL32(30, 120, 255, 255);
            line_col = IM_COL32(30, 120, 255, 255);
        }

        if (cfg.esp_box_health_color) {
            box_col = hp_color(p.health, p.max_health);
        }

        bool need_occlusion = cfg.chams_enabled || cfg.esp_wall_color_split;
        bool occluded = need_occlusion &&
            !aimbot::visible_heuristic(cam, p.head_ptr ? p.head_pos : p.position,
                                       players, vm, screen);
        if (cfg.esp_wall_color_split) {
            box_col  = to_u32(occluded ? cfg.col_esp_occluded_alt
                                        : cfg.col_esp_visible);
            line_col = box_col;
        } else if (occluded) {
            box_col  = to_u32(cfg.col_box_occluded);
            line_col = box_col;
        }

        ImU32 fill_col = to_u32(cfg.col_box_fill);
        ImU32 head_col = to_u32(cfg.col_head);

        bool is_target = cfg.esp_target_line && aim_tgt && p.player_ptr == aim_tgt;
        if (is_target) {
            tgt_screen = { (tl.x + br.x) * 0.5f, (tl.y + br.y) * 0.5f };
            tgt_found  = true;
            dl->AddRect({tl.x - 2.f, tl.y - 2.f}, {br.x + 2.f, br.y + 2.f}, acc(1.f), 0.f, 0, 2.f);
            dl->AddRectFilled(tl, br, acc(0.10f));
        }

        if (cfg.draw_head_dot) {
            ImVec2 head_s;
            Vec3 head_pos = p.head_ptr ? p.head_pos
                          : Vec3{p.position.x, p.position.y + 1.6f, p.position.z};
            if (project(vm, head_pos, screen, head_s)) {

                dl->AddCircleFilled(head_s, 5.f, (head_col & 0x00FFFFFF) | ((ImU32)55 << 24));
                dl->AddCircleFilled(head_s, 3.2f, IM_COL32(0, 0, 0, 200));
                dl->AddCircleFilled(head_s, 2.5f, head_col);
            }
        }

        if (cfg.draw_filled && cfg.box_style > 0) {

            ImU32 top = (fill_col & 0x00FFFFFF) | ((ImU32)((((fill_col >> 24) & 0xFF) * 40) / 255) << 24);
            dl->AddRectFilledMultiColor(tl, br, top, top, fill_col, fill_col);
        }

        auto box_glow = [&]() {
            ImU32 g1 = (box_col & 0x00FFFFFF) | ((ImU32)60 << 24);
            ImU32 g2 = (box_col & 0x00FFFFFF) | ((ImU32)28 << 24);
            dl->AddRect({tl.x - 1.f, tl.y - 1.f}, {br.x + 1.f, br.y + 1.f}, g1, 0.f, 0, 1.f);
            dl->AddRect({tl.x - 2.f, tl.y - 2.f}, {br.x + 2.f, br.y + 2.f}, g2, 0.f, 0, 1.f);

            dl->AddRect({tl.x + 1.f, tl.y + 1.f}, {br.x + 1.f, br.y + 1.f},
                        IM_COL32(0, 0, 0, 90), 0.f, 0, 1.f);
        };
        if (cfg.box_style != 0) box_glow();

        if (cfg.box_style == 1) {
            float cx = bw * 0.28f;
            float cy = bh * 0.28f;
            float t  = cfg.box_thickness;

            ImU32 sh = IM_COL32(0, 0, 0, 200);
            auto seg = [&](ImVec2 a, ImVec2 b) {
                dl->AddLine({a.x + 1, a.y + 1}, {b.x + 1, b.y + 1}, sh, t);
                dl->AddLine(a, b, box_col, t);
            };
            seg({tl.x, tl.y}, {tl.x + cx, tl.y});
            seg({tl.x, tl.y}, {tl.x,      tl.y + cy});
            seg({br.x, tl.y}, {br.x - cx, tl.y});
            seg({br.x, tl.y}, {br.x,      tl.y + cy});
            seg({tl.x, br.y}, {tl.x + cx, br.y});
            seg({tl.x, br.y}, {tl.x,      br.y - cy});
            seg({br.x, br.y}, {br.x - cx, br.y});
            seg({br.x, br.y}, {br.x,      br.y - cy});
        } else if (cfg.box_style == 2) {
            dl->AddRect(tl, br, box_col, 0.f, 0, cfg.box_thickness);
        }

        if (cfg.draw_health && p.max_health > 0.f) {
            float t = p.health / p.max_health;
            if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
            float filled = bh * t;
            float bx    = tl.x - 6.f;

            ImU32 track_bg = IM_COL32(10, 12, 15, 220);
            dl->AddRectFilled({bx - 4.f, tl.y - 1.f}, {bx + 1.f, br.y + 1.f}, track_bg, 2.f);
            dl->AddRect      ({bx - 4.f, tl.y - 1.f}, {bx + 1.f, br.y + 1.f}, IM_COL32(0,0,0,220), 2.f, 0, 1.f);

            ImU32 hp_c = hp_color(p.health, p.max_health);
            ImU32 hp_c2 = (hp_c & 0x00FFFFFF) | ((ImU32)200 << 24);
            dl->AddRectFilledMultiColor({bx - 3.f, br.y - filled}, {bx, br.y},
                hp_c2, hp_c2, hp_c, hp_c);

            dl->AddRectFilled({bx - 3.f, br.y - filled}, {bx, br.y - filled + 1.f},
                              IM_COL32(255, 255, 255, 200), 0.f);

            if (cfg.draw_health_text) {
                char hp_buf[16];
                snprintf(hp_buf, sizeof(hp_buf), "%.0f", p.health);
                ImVec2 hp_size = ImGui::CalcTextSize(hp_buf);
                DrawTextWithShadow(dl, {bx - hp_size.x - 6.f, br.y - filled - (hp_size.y/2.f)}, IM_COL32(255,255,255,255), hp_buf);
            }
        }

        if (cfg.draw_name && !p.name.empty()) {
            float tx = tl.x + bw * 0.5f - ImGui::CalcTextSize(p.name.c_str()).x * 0.5f;
            DrawTextWithShadow(dl, {tx, tl.y - 14.f}, name_col, p.name.c_str());
        }

        {
            float below_y = br.y + 2.f;
            auto centered = [&](const char* s, ImU32 col) {
                float tx = tl.x + bw * 0.5f - ImGui::CalcTextSize(s).x * 0.5f;
                DrawTextWithShadow(dl, {tx, below_y}, col, s);
                below_y += 13.f;
            };
            char buf[32];
            if (cfg.draw_distance) {

                snprintf(buf, sizeof(buf), "%.0f studs", dist);
                centered(buf, IM_COL32(170,170,170,255));
            }
            if (cfg.draw_speed) {
                float spd = sqrtf(p.velocity.x*p.velocity.x + p.velocity.z*p.velocity.z);
                snprintf(buf, sizeof(buf), "%.0f spd", spd);
                centered(buf, IM_COL32(120,200,255,255));
            }
            if (cfg.draw_weapon && !p.weapon.empty())
                centered(p.weapon.c_str(), IM_COL32(255,210,120,255));
            if (cfg.esp_show_id && p.user_id) {
                snprintf(buf, sizeof(buf), "id %lld", p.user_id);
                centered(buf, IM_COL32(150,150,160,255));
            }
        }

        if (cfg.draw_skeleton && p.cached_char_ptr) {
            static const int BONES[][2] = {
                {J_Head, J_UpperTorso}, {J_UpperTorso, J_LowerTorso}, {J_LowerTorso, J_HRP},
                {J_UpperTorso, J_LUArm}, {J_LUArm, J_LLArm}, {J_LLArm, J_LHand},
                {J_UpperTorso, J_RUArm}, {J_RUArm, J_RLArm}, {J_RLArm, J_RHand},
                {J_LowerTorso, J_LULeg}, {J_LULeg, J_LLLeg}, {J_LLLeg, J_LFoot},
                {J_LowerTorso, J_RULeg}, {J_RULeg, J_RLLeg}, {J_RLLeg, J_RFoot},
                {J_UpperTorso, J_LLArm}, {J_UpperTorso, J_RLArm},
                {J_UpperTorso, J_LLLeg}, {J_UpperTorso, J_RLLeg},
            };
            ImU32 sk_col = to_u32(cfg.col_skeleton);
            for (auto& b : BONES) {
                if (!(p.joint_valid_mask & (1u << b[0]))) continue;
                if (!(p.joint_valid_mask & (1u << b[1]))) continue;
                ImVec2 a, c;
                if (!project(vm, p.joint_pos[b[0]], screen, a)) continue;
                if (!project(vm, p.joint_pos[b[1]], screen, c)) continue;
                dl->AddLine(a, c, sk_col, 1.5f);
            }
            for (int i = 0; i < J_COUNT; i++) {
                if (!(p.joint_valid_mask & (1u << i))) continue;
                ImVec2 s;
                if (project(vm, p.joint_pos[i], screen, s))
                    dl->AddCircleFilled(s, 1.6f, sk_col);
            }
        }

        if (cfg.draw_lookat) {
            Vec3 origin = p.head_ptr ? p.head_pos : p.position;
            Vec3 tip = { origin.x + p.char_look.x * cfg.lookat_length,
                         origin.y + p.char_look.y * cfg.lookat_length,
                         origin.z + p.char_look.z * cfg.lookat_length };
            ImVec2 a, b;
            if (project(vm, origin, screen, a) && project(vm, tip, screen, b)) {
                ImU32 lc = to_u32(cfg.col_lookat);
                dl->AddLine(a, b, lc, 1.8f);
                dl->AddCircleFilled(b, 2.5f, lc);
            }
        }

        if (cfg.draw_velocity) {
            Vec3 tip = { p.position.x + p.velocity.x * cfg.velocity_seconds,
                         p.position.y + p.velocity.y * cfg.velocity_seconds,
                         p.position.z + p.velocity.z * cfg.velocity_seconds };
            ImVec2 a, b;
            if (project(vm, p.position, screen, a) && project(vm, tip, screen, b)) {
                float dx = b.x - a.x, dy = b.y - a.y;
                float l = sqrtf(dx*dx + dy*dy);
                if (l > 6.f) {
                    ImU32 vc = to_u32(cfg.col_velocity);
                    dl->AddLine(a, b, vc, 1.6f);
                    dx /= l; dy /= l;
                    ImVec2 arr1 = { b.x - dx*7.f - dy*4.f, b.y - dy*7.f + dx*4.f };
                    ImVec2 arr2 = { b.x - dx*7.f + dy*4.f, b.y - dy*7.f - dx*4.f };
                    dl->AddLine(b, arr1, vc, 1.6f);
                    dl->AddLine(b, arr2, vc, 1.6f);
                }
            }
        }

        if (cfg.sound_alerts && dist < cfg.sound_alert_range) {
            double now_s = steady_seconds();
            PlayerInfo& mp = const_cast<PlayerInfo&>(p);
            if (now_s - mp.last_alert_time > (cfg.sound_alert_cd_ms / 1000.0)) {
                mp.last_alert_time = now_s;
                async_beep(880, 40);
            }
        }

        if ((cfg.hitmarker_enabled || cfg.killfeed_enabled) && p.last_hit_time > 0.0) {
            double now_s = steady_seconds();
            PlayerInfo& mp = const_cast<PlayerInfo&>(p);
            if (now_s - p.last_hit_time < 0.15) {
                g_last_hit_flash = ImGui::GetTime();
                if (cfg.hitmarker_sound) async_beep(1400, 30);
                if (cfg.killfeed_enabled)
                    push_killfeed("hit  " + p.name, false);
                mp.last_hit_time = 0.0;
            }
        }
        if (cfg.killfeed_enabled && p.health <= 0.f && !p.seen_dead && p.max_health > 0.f) {
            PlayerInfo& mp = const_cast<PlayerInfo&>(p);
            mp.seen_dead = true;
            push_killfeed("KILL  " + p.name, true);
        }
        if (p.health > 0.f && p.seen_dead) {
            PlayerInfo& mp = const_cast<PlayerInfo&>(p);
            mp.seen_dead = false;
        }

        if (cfg.draw_line) {
            ImVec2 feet_s;
            if (project(vm, p.position, screen, feet_s)) {
                ImVec2 origin = screen_center;
                if (cfg.snapline_pos == 0)      origin = {g_view_origin.x + screen.x * 0.5f, g_view_origin.y + screen.y};
                else if (cfg.snapline_pos == 2) origin = {g_view_origin.x + screen.x * 0.5f, g_view_origin.y};

                const int SEG = 12;
                for (int i = 0; i < SEG; i++) {
                    float t0 = (float)i / SEG;
                    float t1 = (float)(i + 1) / SEG;
                    ImVec2 p0{origin.x + (feet_s.x - origin.x) * t0, origin.y + (feet_s.y - origin.y) * t0};
                    ImVec2 p1{origin.x + (feet_s.x - origin.x) * t1, origin.y + (feet_s.y - origin.y) * t1};
                    int a0 = (int)(((line_col >> 24) & 0xFF) * (0.10f + 0.90f * t0));
                    ImU32 c = (line_col & 0x00FFFFFF) | ((ImU32)a0 << 24);
                    dl->AddLine(p0, p1, c, 1.3f);
                }
            }
        }
    }

    if (cfg.esp_target_line && tgt_found) {

        ImU32 glow = acc(0.30f);
        dl->AddLine(screen_center, tgt_screen, glow, 4.f);
        dl->AddLine(screen_center, tgt_screen, acc(0.95f), 1.6f);
    }

    if (cfg.trigger_enabled && cfg.trig_hitbox_viz) {
        float r = cfg.trigger_fov * cfg.trig_box_scale;
        if (r < 2.f) r = 2.f;
        ImU32 hc = to_u32(cfg.col_trig_hitbox);

        const int SEG = 40;
        const float TAU = 6.28318530718f;
        for (int i = 0; i < SEG; i++) {
            if (i & 1) continue;
            float a0 = (float)i / SEG * TAU;
            float a1 = (float)(i + 1) / SEG * TAU;
            ImVec2 p0{screen_center.x + std::cos(a0) * r, screen_center.y + std::sin(a0) * r};
            ImVec2 p1{screen_center.x + std::cos(a1) * r, screen_center.y + std::sin(a1) * r};
            dl->AddLine(p0, p1, hc, 1.3f);
        }
    }

    if (cfg.draw_fov) {
        ImU32 fov_col   = to_u32(cfg.col_fov);
        ImU32 fov_solid = (fov_col & 0x00FFFFFF) | 0xFF000000;

        for (int i = 0; i < 5; i++) {
            float ro    = cfg.fov_radius + (i + 1) * 1.4f;
            float atten = (1.f - i / 5.f);
            int a       = (int)(28.f * atten);
            ImU32 g     = (fov_solid & 0x00FFFFFF) | ((ImU32)a << 24);
            dl->AddCircle(screen_center, ro, g, 96, 1.f);
        }

        dl->AddCircle(screen_center, cfg.fov_radius, fov_col, 96, 1.4f);

        auto tick = [&](float ang) {
            float ci = cosf(ang), si = sinf(ang);
            ImVec2 a{screen_center.x + ci * (cfg.fov_radius - 3.f),
                     screen_center.y + si * (cfg.fov_radius - 3.f)};
            ImVec2 b{screen_center.x + ci * (cfg.fov_radius + 3.f),
                     screen_center.y + si * (cfg.fov_radius + 3.f)};
            dl->AddLine(a, b, fov_col, 1.4f);
        };
        tick(-1.5707963f); tick(0.f); tick(1.5707963f); tick(3.14159265f);

        if (cfg.aim_enabled) {
            float t = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 3.2f);
            ImU32 pcol = (0x00FFFFFF & IM_COL32(80, 200, 255, 255)) |
                         ((ImU32)(int)(120.f * t) << 24);
            dl->AddCircle(screen_center, cfg.fov_radius + 6.f * t, pcol, 96, 1.2f);
        }
    }

    if (cfg.draw_crosshair) {
        ImVec2 c = screen_center;
        float  s = cfg.crosshair_size;
        ImU32 col = to_u32(cfg.col_crosshair);
        ImU32 sh  = IM_COL32(0, 0, 0, 230);

        ImU32 glow = (col & 0x00FFFFFF) | ((ImU32)70 << 24);

        auto ln = [&](float ax, float ay, float bx, float by) {

            dl->AddLine({c.x + ax + 1.f, c.y + ay + 1.f}, {c.x + bx + 1.f, c.y + by + 1.f}, sh,   2.0f);

            dl->AddLine({c.x + ax,        c.y + ay       }, {c.x + bx,        c.y + by       }, glow, 3.4f);

            dl->AddLine({c.x + ax,        c.y + ay       }, {c.x + bx,        c.y + by       }, col,  1.6f);
        };
        switch (cfg.crosshair_style) {
            case 0:
                ln(-s, 0, s, 0);
                ln(0, -s, 0, s);
                dl->AddCircleFilled(c, 1.5f, col);
                break;
            case 1:
                ln(-s, 0, s, 0);
                ln(0, 0, 0, s);
                break;
            case 2:
                dl->AddCircleFilled(c, 4.f, glow);
                dl->AddCircleFilled(c, 2.2f, sh);
                dl->AddCircleFilled(c, 1.6f, col);
                break;
            case 3:
                dl->AddCircle(c, s + 1.f, glow, 32, 3.f);
                dl->AddCircle(c, s,       sh,   32, 2.4f);
                dl->AddCircle(c, s,       col,  32, 1.4f);
                dl->AddCircleFilled(c, 1.2f, col);
                break;
            case 4:
                ln(-s, -s, s, s);
                ln(-s, s, s, -s);
                break;
            case 5:
                ln(-s, 0, -s * 0.35f, 0);
                ln(s * 0.35f, 0, s, 0);
                ln(0, -s, 0, -s * 0.35f);
                ln(0, s * 0.35f, 0, s);
                dl->AddCircleFilled(c, 1.0f, col);
                break;
        }
    }

    if (cfg.hitmarker_enabled) {
        double age = ImGui::GetTime() - g_last_hit_flash;
        if (age >= 0.0 && age < 0.28) {
            float a = 1.f - (float)(age / 0.28);
            float s = 10.f + (float)age * 20.f;
            ImU32 hc = IM_COL32(255, 255, 255, (int)(230 * a));
            ImU32 sh = IM_COL32(0, 0, 0, (int)(180 * a));
            ImVec2 c = screen_center;
            dl->AddLine({c.x - s - 1, c.y - s - 1}, {c.x - 3, c.y - 3}, sh, 3.f);
            dl->AddLine({c.x + s + 1, c.y - s - 1}, {c.x + 3, c.y - 3}, sh, 3.f);
            dl->AddLine({c.x - s - 1, c.y + s + 1}, {c.x - 3, c.y + 3}, sh, 3.f);
            dl->AddLine({c.x + s + 1, c.y + s + 1}, {c.x + 3, c.y + 3}, sh, 3.f);
            dl->AddLine({c.x - s, c.y - s}, {c.x - 3, c.y - 3}, hc, 1.8f);
            dl->AddLine({c.x + s, c.y - s}, {c.x + 3, c.y - 3}, hc, 1.8f);
            dl->AddLine({c.x - s, c.y + s}, {c.x - 3, c.y + 3}, hc, 1.8f);
            dl->AddLine({c.x + s, c.y + s}, {c.x + 3, c.y + 3}, hc, 1.8f);
        }
    }

    if (cfg.killfeed_enabled) {
        double now = ImGui::GetTime();
        float ky = 40.f;
        for (int i = (int)g_killfeed.size() - 1; i >= 0; i--) {
            double age = now - g_killfeed[i].t;
            if (age > 4.5) { g_killfeed.erase(g_killfeed.begin() + i); continue; }
            float al = 1.f;
            if (age < 0.15) al = (float)(age / 0.15);
            else if (age > 4.0) al = 1.f - (float)((age - 4.0) / 0.5);
            if (al < 0.f) al = 0.f;
            const auto& e = g_killfeed[i];
            float tw = ImGui::CalcTextSize(e.text.c_str()).x;
            float pw = tw + 24.f, ph = 22.f;
            float px = screen.x - 14.f - pw, py = ky;
            ImU32 bg = IM_COL32(12, 13, 16, (int)(210 * al));
            ImU32 accent_col = e.is_kill ? IM_COL32(255, 80, 80, (int)(255 * al))
                                         : IM_COL32(240, 210, 90, (int)(255 * al));
            dl->AddRectFilled({px, py}, {px + pw, py + ph}, bg, 4.f);
            dl->AddRectFilled({px, py}, {px + 3.f, py + ph}, accent_col, 4.f);
            dl->AddText({px + 12.f, py + 3.f}, IM_COL32(232, 236, 242, (int)(255 * al)), e.text.c_str());
            ky += ph + 5.f;
        }
    }

    if (cfg.draw_keybind_overlay) {
        struct KB { int vk; const char* label; bool on; };
        auto keyname = [](int vk) -> const char* {
            static char buf[16];
            switch (vk) {
                case 0x01: return "LMB"; case 0x02: return "RMB"; case 0x04: return "MMB";
                case 0x05: return "MB4"; case 0x06: return "MB5";
                case VK_SHIFT: return "Shift"; case VK_CONTROL: return "Ctrl"; case VK_MENU: return "Alt";
                case VK_INSERT: return "Ins"; case VK_DELETE: return "Del";
                case VK_HOME: return "Home"; case VK_END: return "End";
                case 0: return "-";
            }
            if (vk >= 'A' && vk <= 'Z') { buf[0] = (char)vk; buf[1] = 0; return buf; }
            if (vk >= VK_F1 && vk <= VK_F12) { snprintf(buf, sizeof(buf), "F%d", vk - VK_F1 + 1); return buf; }
            snprintf(buf, sizeof(buf), "0x%02X", vk); return buf;
        };
        KB kbs[] = {
            {cfg.key_esp,        "ESP",        cfg.enabled},
            {cfg.key_aim,        "Aim",        cfg.aim_enabled},
            {cfg.key_trigger,    "Trigger",    cfg.trigger_enabled},
            {cfg.key_fly,        "Fly",        cfg.fly_enabled},
            {cfg.key_noclip,     "NoClip",     cfg.noclip_enabled},
            {cfg.key_lock,       "Lock",       cfg.aim_lock},
            {cfg.key_radar,      "Radar",      cfg.radar_enabled},
            {cfg.key_tp_mouse,   "TP Cursor",  false},
            {cfg.key_panic,      "PANIC",      false},
        };
        float rowh = 18.f, pad = 8.f;
        float wdt = 148.f;
        int n = sizeof(kbs)/sizeof(kbs[0]);
        float h = pad*2 + n * rowh;
        float x = screen.x - 14.f - wdt;
        float y = screen.y - 14.f - h;
        dl->AddRectFilled({x, y}, {x + wdt, y + h}, IM_COL32(12, 13, 16, 210), 6.f);
        dl->AddRectFilled({x, y}, {x + 3.f, y + h}, acc(1.f), 6.f);
        float ly = y + pad;
        for (auto& k : kbs) {
            ImU32 col_lbl = k.on ? acc(1.f) : IM_COL32(180, 184, 190, 240);
            ImU32 col_key = IM_COL32(150, 154, 162, 220);
            DrawTextWithShadow(dl, {x + 12.f, ly}, col_lbl, k.label);
            const char* kn = keyname(k.vk);
            float kw = ImGui::CalcTextSize(kn).x;
            DrawTextWithShadow(dl, {x + wdt - kw - 12.f, ly}, col_key, kn);
            ly += rowh;
        }
    }

    if (cfg.draw_watermark) {
        char wm[40];
        snprintf(wm, sizeof(wm), "UwUClient | %.0f fps", ImGui::GetIO().Framerate);
        float w = ImGui::CalcTextSize(wm).x;
        DrawTextWithShadow(dl, {screen.x - w - 12.f, 10.f}, acc(1.f), wm);
    }

    if (cfg.radar_enabled) {
        float R = cfg.radar_size;
        ImVec2 rc = {14.f + R, screen.y - 14.f - R};
        dl->AddCircleFilled(rc, R, IM_COL32(10, 12, 15, 190), 56);
        dl->AddCircle(rc, R, acc(0.85f), 56, 1.5f);
        dl->AddLine({rc.x - R, rc.y}, {rc.x + R, rc.y}, IM_COL32(255, 255, 255, 22));
        dl->AddLine({rc.x, rc.y - R}, {rc.x, rc.y + R}, IM_COL32(255, 255, 255, 22));

        float fx = vm[12], fz = vm[14];
        float fl = sqrtf(fx * fx + fz * fz);
        if (fl > 1e-4f) { fx /= fl; fz /= fl; }
        float scl = R / (cfg.radar_range > 1.f ? cfg.radar_range : 1.f);
        for (const auto& p : players) {
            if (!p.valid) continue;
            if (cfg.esp_team_check && p.team_ptr && p.team_ptr == g_local_team.load()) continue;
            float dx = p.position.x - cam.x, dz = p.position.z - cam.z;
            float fwd = dx * fx + dz * fz;
            float rgt = dx * fz - dz * fx;
            float px = rc.x + rgt * scl, py = rc.y - fwd * scl;
            float ox = px - rc.x, oy = py - rc.y, d = sqrtf(ox * ox + oy * oy);
            if (d > R) { px = rc.x + ox / d * R; py = rc.y + oy / d * R; }
            ImU32 col = (p.role == 1) ? IM_COL32(255, 45, 45, 255)
                      : (p.role == 2) ? IM_COL32(45, 120, 255, 255) : acc(1.f);
            dl->AddCircleFilled({px, py}, 3.f, col);
            if (cfg.radar_names && !p.name.empty())
                dl->AddText({px + 5.f, py - 6.f}, IM_COL32(210, 214, 220, 220), p.name.c_str());
        }
        dl->AddCircleFilled(rc, 3.f, IM_COL32(255, 255, 255, 255));
    }

    if (cfg.draw_hud) {
        struct FE { bool on; const char* n; };
        FE feats[] = {
            {cfg.aim_enabled, "Aimbot"}, {cfg.trigger_enabled, "Triggerbot"},
            {cfg.enabled, "ESP"}, {cfg.radar_enabled, "Radar"},
            {cfg.fly_enabled, "Fly"}, {cfg.noclip_enabled, "No-Clip"},
            {cfg.speed_enabled, "Speed"}, {cfg.bunny_hop, "Bunny-Hop"},
            {cfg.antiaim_spin || cfg.antiaim_jitter, "Anti-Aim"},
        };
        float x = 14.f, y = 40.f, wdt = 128.f;
        int count = 0; for (auto& f : feats) if (f.on) count++;
        float ph = 24.f + count * 18.f;
        dl->AddRectFilled({x, y}, {x + wdt, y + ph}, IM_COL32(12, 13, 16, 200), 6.f);
        dl->AddRectFilled({x, y}, {x + 3.f, y + ph}, acc(1.f), 6.f);
        DrawTextWithShadow(dl, {x + 10.f, y + 5.f}, acc(1.f), "UwUClient");
        float ly = y + 24.f;
        for (auto& f : feats) if (f.on) {
            dl->AddCircleFilled({x + 14.f, ly + 7.f}, 2.5f, acc(1.f));
            DrawTextWithShadow(dl, {x + 22.f, ly}, IM_COL32(220, 224, 230, 255), f.n);
            ly += 18.f;
        }
    }

    if (cfg.notifications) {
        double now = ImGui::GetTime();
        float ny = screen.y - 60.f;
        for (int i = (int)g_notifs.size() - 1; i >= 0; i--) {
            double age = now - g_notifs[i].t;
            if (age > 3.5) { g_notifs.erase(g_notifs.begin() + i); continue; }
            float al = 1.f;
            if (age < 0.18) al = (float)(age / 0.18);
            else if (age > 3.0) al = 1.f - (float)((age - 3.0) / 0.5);
            if (al < 0.f) al = 0.f;
            const std::string& s = g_notifs[i].text;
            float tw = ImGui::CalcTextSize(s.c_str()).x;
            float pw = tw + 26.f, ph = 28.f;
            float px = screen.x - 14.f - pw, py = ny;
            dl->AddRectFilled({px, py}, {px + pw, py + ph}, IM_COL32(14, 15, 19, (int)(220 * al)), 6.f);
            dl->AddRectFilled({px, py}, {px + 3.f, py + ph}, acc(al), 6.f);
            dl->AddText({px + 14.f, py + 6.f}, IM_COL32(230, 234, 240, (int)(255 * al)), s.c_str());
            ny -= (ph + 6.f);
        }
    }
}

static void apply_theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 8.f;
    s.ChildRounding     = 6.f;
    s.FrameRounding     = 5.f;
    s.PopupRounding     = 5.f;
    s.GrabRounding      = 5.f;
    s.TabRounding       = 6.f;
    s.ScrollbarRounding = 6.f;
    s.WindowBorderSize  = 1.f;
    s.FrameBorderSize   = 0.f;
    s.WindowPadding     = ImVec2(14, 12);
    s.FramePadding      = ImVec2(9, 5);
    s.ItemSpacing       = ImVec2(9, 8);
    s.ItemInnerSpacing  = ImVec2(7, 5);
    s.ScrollbarSize     = 12.f;
    s.GrabMinSize       = 10.f;
    s.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    s.PopupBorderSize        = 1.f;
    s.SeparatorTextBorderSize = 2.f;
    s.SeparatorTextPadding    = ImVec2(0.f, 6.f);
    s.TabBarBorderSize       = 2.f;
    s.CellPadding            = ImVec2(8.f, 6.f);

    const ImVec4 white   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    const ImVec4 whiteLo = ImVec4(1.00f, 1.00f, 1.00f, 0.35f);
    const ImVec4 whiteMd = ImVec4(1.00f, 1.00f, 1.00f, 0.75f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.05f, 0.05f, 0.05f, 0.98f);
    c[ImGuiCol_Border]               = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.66f, 0.66f, 0.66f, 1.00f);
    c[ImGuiCol_CheckMark]            = white;
    c[ImGuiCol_SliderGrab]           = whiteMd;
    c[ImGuiCol_SliderGrabActive]     = white;
    c[ImGuiCol_Button]               = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_SeparatorHovered]     = whiteLo;
    c[ImGuiCol_SeparatorActive]      = white;
    c[ImGuiCol_Tab]                  = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]  = white;
    c[ImGuiCol_TabDimmed]            = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    c[ImGuiCol_TextSelectedBg]       = whiteLo;

    cfg.accent[0] = cfg.accent[1] = cfg.accent[2] = 1.f;
}

static float ui_anim(ImGuiID id, float target, float rate = 14.f) {
    static std::unordered_map<ImGuiID, float> m;
    float& v = m[id];
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.f || dt > 0.1f) dt = 1.f / 60.f;
    v += (target - v) * (1.f - expf(-rate * dt));
    return v;
}
static ImU32 lerp_col(ImU32 a, ImU32 b, float t) {
    ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a), cb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
        ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
}

static bool toggle(const char* label, bool* v) {
    ImGui::PushID(label);
    float h = ImGui::GetFrameHeight() * 1.05f;
    float w = h * 2.05f, r = h * 0.5f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool changed = ImGui::InvisibleButton("sw", ImVec2(w, h));
    if (changed) *v = !*v;
    float t  = ui_anim(ImGui::GetID("sw"), *v ? 1.f : 0.f, 18.f);
    float hv = ui_anim(ImGui::GetID("hv"), ImGui::IsItemHovered() ? 1.f : 0.f, 20.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (t > 0.02f)
        dl->AddRectFilled({p.x - 3, p.y - 3}, {p.x + w + 3, p.y + h + 3}, acc(t * 0.20f), r + 3);

    ImU32 off_col = IM_COL32(42, 46, 54, 255);
    dl->AddRectFilled(p, {p.x + w, p.y + h}, lerp_col(off_col, acc(1.f), t), r);

    dl->AddRect(p, {p.x + w, p.y + h}, IM_COL32(0, 0, 0, 60), r, 0, 1.f);

    float kx = p.x + r + t * (w - r * 2);
    dl->AddCircleFilled({kx + 0.5f, p.y + r + 1.f}, r - 3.f, IM_COL32(0, 0, 0, 80));
    dl->AddCircleFilled({kx, p.y + r}, r - 3.f, IM_COL32(252, 252, 254, 255));
    ImGui::SameLine(0, 12);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text,
        lerp_col(IM_COL32(200, 205, 213, 255), IM_COL32(255, 255, 255, 255), hv));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopID();
    return changed;
}

static bool sidebar_tab(const char* icon, const char* label, bool active) {
    ImGui::PushID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = 38.f;
    bool clicked = ImGui::InvisibleButton("t", {w, h});
    float a = ui_anim(ImGui::GetID("t"),
                      active ? 1.f : (ImGui::IsItemHovered() ? 0.45f : 0.f), 16.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (a > 0.01f) dl->AddRectFilled({p.x + 6, p.y}, {p.x + w - 6, p.y + h}, acc(a * 0.14f), 8.f);
    ImU32 col = lerp_col(IM_COL32(150, 155, 163, 255), IM_COL32(255, 255, 255, 255), a);
    ImU32 icol = lerp_col(IM_COL32(120, 126, 135, 255), acc(1.f), a);
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImGui::GetFont(), 15.f, {p.x + 18, p.y + (h - ts.y) * 0.5f - 1}, icol, icon);
    dl->AddText({p.x + 42, p.y + (h - ts.y) * 0.5f}, col, label);
    ImGui::PopID();
    return clicked;
}

static void card_begin(const char* title, ImVec2 size = {0, 0}) {
    if (g_flat_mode) {

        if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x + 1.f)
            ImGui::NewLine();

        ImGui::Dummy({0, 4.f});
        ImVec2 wp = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 ac = acc(1.f);
        dl->AddRectFilled({wp.x, wp.y + 4}, {wp.x + 4.f, wp.y + 18}, ac, 1.f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.f);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 234, 240, 255));
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();

        ImVec2 sp = ImGui::GetCursorScreenPos();
        float aw = ImGui::GetContentRegionAvail().x;
        dl->AddLine({sp.x, sp.y + 2}, {sp.x + aw, sp.y + 2},
                    IM_COL32(70, 74, 82, 200), 1.f);
        ImGui::Dummy({0, 8.f});
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(15, 17, 20, 220));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(34, 39, 46, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 15));
    ImGui::BeginChild(title, size, true);

    ImVec2 wp = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 ac = acc(1.f);
    dl->AddRectFilled({wp.x - 6, wp.y + 3}, {wp.x - 2, wp.y + 18}, ac, 2.f);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 244, 250, 255));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();

    ImVec2 sp = ImGui::GetCursorScreenPos();
    float aw = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilledMultiColor(
        {sp.x, sp.y + 3}, {sp.x + aw * 0.6f, sp.y + 4},
        acc(0.9f), acc(0.f), acc(0.f), acc(0.9f));
    dl->AddLine({sp.x, sp.y + 4}, {sp.x + aw, sp.y + 4}, IM_COL32(28, 32, 38, 255), 1.f);
    ImGui::Dummy({0, 12});
}
static void card_end() {
    if (g_flat_mode) {
        ImGui::Dummy({0, 10.f});
        return;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

static bool slider(const char* label, float* v, float mn, float mx, const char* fmt = "%.1f") {
    ImGui::PushID(label);
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::TextUnformatted(label);
    char val[40]; snprintf(val, sizeof(val), fmt, *v);
    ImVec2 vs = ImGui::CalcTextSize(val);
    ImGui::SameLine();
    float aw = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (aw - vs.x));
    ImGui::PushStyleColor(ImGuiCol_Text, acc(1.f));
    ImGui::TextUnformatted(val);
    ImGui::PopStyleColor();

    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float rowh = 16.f;
    ImGui::InvisibleButton("s", {w, rowh});
    bool activ = ImGui::IsItemActive(), hov = ImGui::IsItemHovered();
    if (activ && w > 0.f) {
        float t = (io.MousePos.x - p.x) / w;
        if (t < 0.f) t = 0.f;
        if (t > 1.f) t = 1.f;
        *v = mn + t * (mx - mn);
    }
    float frac = (mx > mn) ? (*v - mn) / (mx - mn) : 0.f;
    float fa = ui_anim(ImGui::GetID("s"), frac, 22.f);
    float th = 6.f, cy = p.y + rowh * 0.5f;
    dl->AddRectFilled({p.x, cy - th * 0.5f}, {p.x + w, cy + th * 0.5f}, IM_COL32(46, 50, 58, 255), th * 0.5f);
    dl->AddRectFilled({p.x, cy - th * 0.5f}, {p.x + w * fa, cy + th * 0.5f}, acc(1.f), th * 0.5f);
    float kx = p.x + w * fa;
    if (hov || activ) dl->AddCircleFilled({kx, cy}, 9.f, acc(0.22f));
    dl->AddCircleFilled({kx, cy}, 6.f, IM_COL32(255, 255, 255, 255));
    ImGui::PopID();
    return activ;
}
static bool slideri(const char* label, int* v, int mn, int mx) {
    float f = (float)*v;
    bool r = slider(label, &f, (float)mn, (float)mx, "%.0f");
    *v = (int)(f + 0.5f);
    return r;
}

static bool segmented(const char* label, int* v, const char* const items[], int n) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight();
    float seg = w / (n > 0 ? n : 1);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p0, {p0.x + w, p0.y + h}, IM_COL32(28, 31, 37, 255), 6.f);
    bool changed = false;
    for (int i = 0; i < n; i++) {
        if (i > 0) ImGui::SameLine(0, 0);
        ImGui::PushID(i);
        ImVec2 sp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("seg", {seg, h})) { *v = i; changed = true; }
        float a = ui_anim(ImGui::GetID("seg"), (*v == i) ? 1.f : (ImGui::IsItemHovered() ? 0.45f : 0.f), 16.f);
        if (*v == i)          dl->AddRectFilled(sp, {sp.x + seg, sp.y + h}, acc(1.f), 6.f);
        else if (a > 0.01f)   dl->AddRectFilled(sp, {sp.x + seg, sp.y + h}, acc(a * 0.18f), 6.f);
        ImVec2 ts = ImGui::CalcTextSize(items[i]);
        ImU32 tc = (*v == i) ? IM_COL32(16, 18, 21, 255) : IM_COL32(185, 190, 197, 255);
        dl->AddText({sp.x + (seg - ts.x) * 0.5f, sp.y + (h - ts.y) * 0.5f}, tc, items[i]);
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

static const char* vk_name(int vk) {
    static char buf[16];
    switch (vk) {
        case 0x01: return "LMB"; case 0x02: return "RMB"; case 0x04: return "MMB";
        case 0x05: return "MB4"; case 0x06: return "MB5";
        case VK_SHIFT: return "Shift"; case VK_CONTROL: return "Ctrl"; case VK_MENU: return "Alt";
        case VK_SPACE: return "Space"; case VK_TAB: return "Tab"; case VK_RETURN: return "Enter";
        case VK_INSERT: return "Insert"; case VK_DELETE: return "Del";
        case VK_HOME: return "Home"; case VK_END: return "End";
        case VK_PRIOR: return "PgUp"; case VK_NEXT: return "PgDn"; case VK_OEM_3: return "`";
    }
    if (vk >= 'A' && vk <= 'Z') { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= '0' && vk <= '9') { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= VK_F1 && vk <= VK_F12) { snprintf(buf, sizeof(buf), "F%d", vk - VK_F1 + 1); return buf; }
    snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}
static void keybind(const char* label, int* vk) {
    static int* listening = nullptr;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(150);
    ImGui::PushID(label);
    if (ImGui::Button((listening == vk) ? "[ press ]" : vk_name(*vk), ImVec2(-FLT_MIN, 0)))
        listening = (listening == vk) ? nullptr : vk;
    if (listening == vk) {
        for (int k = 1; k <= 0xFE; k++) {
            if (GetAsyncKeyState(k) & 0x8000) {
                if (k != VK_ESCAPE) *vk = k;
                listening = nullptr;
                break;
            }
        }
    }
    ImGui::PopID();
}

void render_console(const std::vector<PlayerInfo>& players, Vec2 screen) {
    if (!cfg.show_console) return;

    ImGui::SetNextWindowSize({560, 480}, ImGuiCond_Once);
    ImGui::SetNextWindowPos({screen.x - 580.f, 60.f}, ImGuiCond_Once);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(8, 9, 11, 235));
    ImGui::PushStyleColor(ImGuiCol_Border,   IM_COL32(30, 34, 40, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));

    ImGui::Begin("UwUClient ~ console", &cfg.show_console);

    RbxVisualEngine ve = RbxVisualEngine::get();
    RbxDataModel    dm = RbxDataModel::get();
    uintptr_t lp = 0, ch = 0, hrp = 0, hrp_prim = 0;
    if (dm.valid()) {
        RbxInstance psvc = dm.get_service("Players");
        if (psvc.valid() && offsets::Players::LocalPlayer) {
            lp = rpm<uintptr_t>(psvc.ptr + offsets::Players::LocalPlayer);
            if (lp && offsets::Player::Character) {
                ch = rpm<uintptr_t>(lp + offsets::Player::Character);
                if (ch) {
                    hrp = RbxInstance{ch}.find_child_by_name("HumanoidRootPart").ptr;
                    if (hrp && offsets::BasePart::Primitive)
                        hrp_prim = rpm<uintptr_t>(hrp + offsets::BasePart::Primitive);
                }
            }
        }
    }
    int valid_count = 0;
    for (auto& p : players) if (p.valid) valid_count++;

    auto dot = [&](bool ok) {
        ImU32 c = ok ? IM_COL32(90, 220, 120, 255) : IM_COL32(240, 90, 90, 255);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled({p.x + 6.f, p.y + 8.f}, 5.f, c);
        ImGui::GetWindowDrawList()->AddCircle({p.x + 6.f, p.y + 8.f}, 5.f, IM_COL32(0,0,0,180));
        ImGui::Dummy({14, 14});
        ImGui::SameLine();
    };
    auto row = [&](const char* label, bool ok, const char* val) {
        dot(ok);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(150);
        ImGui::TextColored(ok ? ImVec4(0.65f, 0.95f, 0.7f, 1.f) : ImVec4(0.95f, 0.5f, 0.5f, 1.f), "%s", val);
    };

    char buf[64];
    ImGui::TextDisabled("SUBSYSTEMS");
    ImGui::Separator();

    snprintf(buf, sizeof(buf), ve.valid() ? "OK 0x%llX" : "FAIL", (unsigned long long)ve.ptr);
    row("VisualEngine", ve.valid(), buf);

    snprintf(buf, sizeof(buf), dm.valid() ? "OK 0x%llX" : "FAIL", (unsigned long long)dm.ptr);
    row("DataModel",    dm.valid(), buf);

    snprintf(buf, sizeof(buf), lp ? "OK 0x%llX" : "N/A", (unsigned long long)lp);
    row("LocalPlayer",  lp != 0, buf);

    snprintf(buf, sizeof(buf), ch ? "OK 0x%llX" : "N/A", (unsigned long long)ch);
    row("Character",    ch != 0, buf);

    snprintf(buf, sizeof(buf), hrp ? "OK 0x%llX" : "N/A", (unsigned long long)hrp);
    row("HumanoidRootPart", hrp != 0, buf);

    snprintf(buf, sizeof(buf), hrp_prim ? "OK 0x%llX" : "MISSING", (unsigned long long)hrp_prim);
    row("HRP Primitive", hrp_prim != 0, buf);

    snprintf(buf, sizeof(buf), "%d / %d", valid_count, (int)players.size());
    row("Player list (valid/total)", valid_count > 0, buf);

    ImGui::Spacing();
    ImGui::TextDisabled("KEY OFFSETS");
    ImGui::Separator();
    if (ImGui::BeginTable("offsets", 2, ImGuiTableFlags_SizingStretchProp)) {
        struct O { const char* n; uintptr_t v; };
        O offs[] = {
            {"VE::Pointer",           offsets::VisualEngine::Pointer},
            {"VE::ViewMatrix",        offsets::VisualEngine::ViewMatrix},
            {"VE::Dimensions",        offsets::VisualEngine::Dimensions},
            {"VE::FakeDataModel",     offsets::VisualEngine::FakeDataModel},
            {"FakeDM::Pointer",       offsets::FakeDataModel::Pointer},
            {"FakeDM::RealDM",        offsets::FakeDataModel::RealDataModel},
            {"Instance::Name",        offsets::Instance::Name},
            {"Instance::Parent",      offsets::Instance::Parent},
            {"Instance::Children",    offsets::Instance::Children},
            {"Player::Character",     offsets::Player::Character},
            {"Player::Team",          offsets::Player::Team},
            {"Players::LocalPlayer",  offsets::Players::LocalPlayer},
            {"BasePart::Primitive",   offsets::BasePart::Primitive},
            {"Primitive::CFrame",     offsets::Primitive::CFrame},
            {"Primitive::Position",   offsets::Primitive::Position},
            {"Humanoid::Health",      offsets::Humanoid::Health},
            {"Humanoid::WalkSpeed",   offsets::Humanoid::WalkSpeed},
            {"Camera::CFrame",        offsets::Camera::CFrame},
            {"Workspace::CurrentCamera", offsets::Workspace::CurrentCamera},
        };
        for (auto& o : offs) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(o.n);
            ImGui::TableSetColumnIndex(1);
            if (o.v)
                ImGui::TextColored(ImVec4(0.65f, 0.95f, 0.7f, 1.f), "0x%llX", (unsigned long long)o.v);
            else
                ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1.f), "unresolved");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("LOG");
    ImGui::SameLine();
    const char* lvls[] = { "all", "ok+", "warn+", "err" };
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::Combo("##lvl", &cfg.console_lvl_filter, lvls, 4);
    ImGui::SameLine();
    if (ImGui::SmallButton("clear")) elog::clear();
    ImGui::SameLine();
    ImGui::Checkbox("wrap", &cfg.console_wrap);
    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(4, 5, 7, 255));
    ImGui::BeginChild("log", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    auto snap = elog::snapshot();
    double now = elog::_now_s();
    for (const auto& e : snap) {
        int min_lvl = (cfg.console_lvl_filter == 0) ? -1 :
                      (cfg.console_lvl_filter == 1) ? elog::L_OK :
                      (cfg.console_lvl_filter == 2) ? elog::L_WARN :
                                                     elog::L_ERR;
        if (min_lvl >= 0 && e.level != elog::L_OK && e.level != elog::L_WARN && e.level != elog::L_ERR) continue;
        if (min_lvl == elog::L_WARN && e.level < elog::L_WARN) continue;
        if (min_lvl == elog::L_ERR  && e.level < elog::L_ERR)  continue;

        ImVec4 col;
        const char* tag;
        switch (e.level) {
            case elog::L_OK:   col = ImVec4(0.42f, 0.90f, 0.55f, 1.f); tag = "OK  "; break;
            case elog::L_WARN: col = ImVec4(0.95f, 0.82f, 0.35f, 1.f); tag = "WARN"; break;
            case elog::L_ERR:  col = ImVec4(0.95f, 0.45f, 0.45f, 1.f); tag = "ERR "; break;
            case elog::L_DBG:  col = ImVec4(0.55f, 0.60f, 0.68f, 1.f); tag = "DBG "; break;
            default:           col = ImVec4(0.82f, 0.86f, 0.90f, 1.f); tag = "INFO"; break;
        }
        double age = now - e.t;
        ImGui::TextColored(ImVec4(0.45f, 0.48f, 0.52f, 1.f), "[%7.2fs]", age);
        ImGui::SameLine();
        ImGui::TextColored(col, "%s", tag);
        ImGui::SameLine();
        if (cfg.console_wrap) ImGui::TextWrapped("%s", e.text.c_str());
        else                  ImGui::TextUnformatted(e.text.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

static char g_script_buf[1 << 16] = "print(\"hello from UwUClient\")\n";
static char g_script_status[128]  = "";

const char* esp_current_script_buffer() { return g_script_buf; }

static void render_tab_body(int active, uintptr_t render_view_ptr,
                            const std::vector<PlayerInfo>& players);

void draw_active_tab(int active, uintptr_t render_view_ptr,
                     const std::vector<PlayerInfo>& players) {
    static bool themed = false;
    if (!themed) { apply_theme(); themed = true; }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
    render_tab_body(active, render_view_ptr, players);
    ImGui::PopStyleVar(2);
}

static void render_tab_body(int active, uintptr_t render_view_ptr,
                            const std::vector<PlayerInfo>& players) {
        float FULL = ImGui::GetContentRegionAvail().x;
        float HALF = (FULL - 10.f) * 0.5f;

        if (active == 0) {
            card_begin("Silent Aim", {HALF, 108});
                toggle("Enable (passive, no keybind)", &cfg.silent_aim);
                ImGui::TextDisabled("Runs on any LMB click. No aim key required.");
                toggle("Team Check##sa", &cfg.team_check);
            card_end();
            ImGui::SameLine(0, 10);
            card_begin("Aimbot", {HALF, 108});
                toggle("Enable Aimbot", &cfg.aim_enabled);
                const char* keys[] = { "LMB", "RMB", "MB4", "MB5", "SHIFT", "ALT" };
                int key_idx = (cfg.aim_key == 0x01) ? 0 : (cfg.aim_key == 0x02) ? 1 : (cfg.aim_key == 0x05) ? 2 : (cfg.aim_key == 0x06) ? 3 : (cfg.aim_key == 0xA0) ? 4 : (cfg.aim_key == 0x12) ? 5 : 1;
                if (segmented("Aim Key", &key_idx, keys, 6)) {
                    static const int VK[6] = {0x01, 0x02, 0x05, 0x06, 0xA0, 0x12};
                    cfg.aim_key = VK[key_idx];
                }
            card_end();

            card_begin("Target Selection", {HALF, 234});
                const char* priorities[] = { "Distance", "Health", "Role" };
                segmented("Priority", &cfg.aim_priority, priorities, 3);
                const char* bones[] = { "Head", "Chest" };
                if (!cfg.aim_multibone) segmented("Bone", &cfg.aim_bone, bones, 2);
                toggle("Multi-Bone Fallback", &cfg.aim_multibone);
                toggle("Sticky Aim", &cfg.aim_sticky);
                toggle("Visibility Check", &cfg.aim_visibility_check);
                toggle("Prediction", &cfg.aim_predict);
                if (cfg.aim_predict) slider("Lead", &cfg.aim_prediction, 0.f, 0.5f, "%.2f");
                slider("Max Distance", &cfg.aim_max_dist, 0.f, 5000.f, "%.0f");
            card_end();
            ImGui::SameLine(0, 10);
            card_begin("Aim Response", {HALF, 234});
                toggle("Lock-On (instant)", &cfg.aim_lock);
                if (!cfg.aim_lock) {
                    slider("Strength", &cfg.aim_strength, 1.f, 100.f, "%.0f%%");
                    slider("Smoothing", &cfg.aim_smooth, 0.1f, 1.0f, "%.2f");
                    slider("Deadzone", &cfg.aim_deadzone, 0.f, 30.f, "%.0f px");
                    toggle("Split X/Y Smooth", &cfg.aim_split_smooth);
                    if (cfg.aim_split_smooth) slider("Smooth Y", &cfg.aim_smooth_y, 0.1f, 1.0f, "%.2f");
                } else {
                    slider("Lock Sens", &cfg.aim_lock_div, 0.5f, 5.f, "%.1f");
                    ImGui::TextDisabled("1.0 = glued. Raise for jitter/overshoot.");
                }
                toggle("Humanize (jitter)", &cfg.aim_jitter);
                if (cfg.aim_jitter) slider("Jitter Amt", &cfg.aim_jitter_strength, 0.1f, 2.f, "%.2f");
                toggle("Toggle Mode", &cfg.aim_toggle_mode);
            card_end();

            card_begin("Triggerbot", {HALF, 168});
                toggle("Enable Triggerbot", &cfg.trigger_enabled);
                const char* tkeys[] = { "MB4", "MB5", "RMB", "ALT", "C" };
                int tk = (cfg.trigger_key == 0x05) ? 0 : (cfg.trigger_key == 0x06) ? 1 : (cfg.trigger_key == 0x02) ? 2 : (cfg.trigger_key == 0x12) ? 3 : 4;
                if (segmented("Trigger Key", &tk, tkeys, 5)) {
                    static const int TK[5] = {0x05, 0x06, 0x02, 0x12, 0x43};
                    cfg.trigger_key = TK[tk];
                }
                slider("Trigger FOV", &cfg.trigger_fov, 1.f, 50.f, "%.0f px");
                slideri("Delay (ms)", &cfg.trigger_delay, 0, 500);
                toggle("Auto-Fire on Lock", &cfg.aim_autofire);
                if (cfg.aim_autofire) slider("Auto-Fire FOV", &cfg.aim_autofire_fov, 1.f, 20.f, "%.0f px");
            card_end();
            ImGui::SameLine(0, 10);
            card_begin("FOV & Range", {HALF, 168});
                toggle("Draw FOV Circle", &cfg.draw_fov);
                if (cfg.draw_fov) slider("FOV Radius", &cfg.fov_radius, 10.f, 1000.f, "%.0f");
                slider("Max ESP dist", &cfg.max_dist, 50.f, 10000.f, "%.0f");
            card_end();
        }
        else if (active == 1) {
            card_begin("Boxes", {HALF, 180});
                toggle("Enable ESP", &cfg.enabled);
                const char* box_styles[] = { "Off", "Corner", "Full" };
                segmented("Box Style", &cfg.box_style, box_styles, 3);
                toggle("Filled Background", &cfg.draw_filled);
                slider("Thickness", &cfg.box_thickness, 0.5f, 5.f, "%.1f");
                toggle("Team Check", &cfg.esp_team_check);
                toggle("HP-colored Box", &cfg.esp_box_health_color);
            card_end();
            ImGui::SameLine(0, 10);
            card_begin("Info Tags", {HALF, 180});
                toggle("Names",      &cfg.draw_name);
                toggle("Distance",   &cfg.draw_distance);
                toggle("Speed",      &cfg.draw_speed);
                toggle("Weapon",     &cfg.draw_weapon);
                toggle("User ID",    &cfg.esp_show_id);
                toggle("Health Bar", &cfg.draw_health);
                if (cfg.draw_health) toggle("Show HP Text", &cfg.draw_health_text);
            card_end();

            card_begin("Lines & Markers", {HALF, 232});
                toggle("Snaplines",  &cfg.draw_line);
                if (cfg.draw_line) {
                    const char* snap_pos[] = { "Bottom", "Center", "Top" };
                    segmented("Origin", &cfg.snapline_pos, snap_pos, 3);
                }
                toggle("Head Dot",   &cfg.draw_head_dot);
                toggle("Off-screen Arrows", &cfg.draw_arrows);
                toggle("Skeleton",   &cfg.draw_skeleton);
                toggle("Aim Target Line", &cfg.esp_target_line);
                toggle("Look-At Rays", &cfg.draw_lookat);
                if (cfg.draw_lookat) slider("Ray Length", &cfg.lookat_length, 2.f, 40.f, "%.0f");
                toggle("Velocity Lines", &cfg.draw_velocity);
                if (cfg.draw_velocity) slider("Predict Sec", &cfg.velocity_seconds, 0.1f, 3.f, "%.2f");
                toggle("Chams (recolor)", &cfg.chams_enabled);
            card_end();
            ImGui::SameLine(0, 10);
            card_begin("Combat Feedback", {HALF, 232});
                toggle("Hitmarker", &cfg.hitmarker_enabled);
                if (cfg.hitmarker_enabled) toggle("Beep on Hit", &cfg.hitmarker_sound);
                toggle("Kill/Hit Feed", &cfg.killfeed_enabled);
                toggle("Proximity Sound Alert", &cfg.sound_alerts);
                if (cfg.sound_alerts) {
                    slider("Alert Range", &cfg.sound_alert_range, 20.f, 300.f, "%.0f");
                    slideri("Cooldown (ms)", &cfg.sound_alert_cd_ms, 200, 5000);
                }
                toggle("Keybind Overlay", &cfg.draw_keybind_overlay);
                toggle("Text Shadow", &cfg.text_shadow);
            card_end();

            card_begin("Radar / HUD", {HALF, 180});
                toggle("Radar", &cfg.radar_enabled);
                if (cfg.radar_enabled) {
                    slider("Radar Size", &cfg.radar_size, 70.f, 200.f, "%.0f");
                    slider("Radar Range", &cfg.radar_range, 50.f, 500.f, "%.0f");
                    toggle("Radar Names", &cfg.radar_names);
                }
                toggle("Feature HUD", &cfg.draw_hud);
                toggle("Notifications", &cfg.notifications);
                toggle("Watermark",  &cfg.draw_watermark);
            card_end();
            ImGui::SameLine(0, 10);
            card_begin("Crosshair", {HALF, 180});
                toggle("Draw Crosshair", &cfg.draw_crosshair);
                if (cfg.draw_crosshair) {
                    const char* cx_styles[] = { "Plus", "T", "Dot", "Circle", "X", "Gap" };
                    segmented("Style", &cfg.crosshair_style, cx_styles, 6);
                    slider("Size", &cfg.crosshair_size, 2.f, 30.f, "%.0f");
                }
            card_end();
        }
        else if (active == 2) {
            card_begin("World", {HALF, 210});
                if (toggle("Fullbright", &cfg.fullbright) && render_view_ptr)
                    wpm<uint8_t>(render_view_ptr + offsets::RenderView::LightingValid, cfg.fullbright ? 0 : 1);
                if (toggle("No Skybox", &cfg.no_skybox) && render_view_ptr)
                    wpm<uint8_t>(render_view_ptr + offsets::RenderView::SkyboxValid, cfg.no_skybox ? 0 : 1);
                toggle("No Fog", &cfg.world_nofog);
                toggle("Time of Day", &cfg.world_time_enabled);
                if (cfg.world_time_enabled) slider("Clock", &cfg.world_time, 0.f, 24.f, "%.1f");
                toggle("Gravity", &cfg.world_gravity_enabled);
                if (cfg.world_gravity_enabled) slider("Force##grav", &cfg.world_gravity, 0.f, 400.f, "%.0f");
                toggle("Brightness", &cfg.world_bright_enabled);
                if (cfg.world_bright_enabled) slider("Level##bright", &cfg.world_bright, 0.f, 10.f, "%.1f");
            card_end();
            ImGui::SameLine(0, 10);
            card_begin("Post-FX", {HALF, 210});
                toggle("No Blur", &cfg.vis_no_blur);
                toggle("No Depth of Field", &cfg.vis_no_dof);
                toggle("No Sun Rays", &cfg.vis_no_sunrays);
                toggle("No Atmosphere", &cfg.vis_no_atmosphere);
            card_end();

            card_begin("Performance (FastFlags)", {FULL, 168});
                ImGui::TextDisabled("Experimental - confirm Debug tab shows FPS ~60 first.");
                toggle("FPS Unlock", &cfg.fps_unlock);
                if (cfg.fps_unlock) slideri("FPS Cap", &cfg.fps_cap, 30, 1000);
                toggle("Fullbright (voxel)", &cfg.ff_fullbright);
                ImGui::SameLine(190);
                toggle("Grey Sky",     &cfg.ff_gray_sky);
                toggle("Hide Adorns",  &cfg.ff_no_adorns);
                ImGui::SameLine(190);
                toggle("No Grass",     &cfg.ff_no_grass);
                toggle("Low Textures", &cfg.ff_low_texture);
            card_end();

            card_begin("ESP Colors", {FULL, 100});
                ImGui::ColorEdit4("Box##c",  (float*)&cfg.col_box,  ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine(); if (cfg.draw_filled) ImGui::ColorEdit4("Fill##c", (float*)&cfg.col_box_fill, ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine(); ImGui::ColorEdit4("Name##c", (float*)&cfg.col_name, ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine(); ImGui::ColorEdit4("Line##c", (float*)&cfg.col_line, ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine(); if (cfg.draw_head_dot) ImGui::ColorEdit4("Head##c", (float*)&cfg.col_head, ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine(); if (cfg.draw_fov) ImGui::ColorEdit4("FOV##c", (float*)&cfg.col_fov, ImGuiColorEditFlags_NoInputs);
                if (cfg.draw_skeleton) ImGui::ColorEdit4("Skeleton##c", (float*)&cfg.col_skeleton, ImGuiColorEditFlags_NoInputs);
                if (cfg.draw_lookat)   { ImGui::SameLine(); ImGui::ColorEdit4("Look-At##c",  (float*)&cfg.col_lookat,   ImGuiColorEditFlags_NoInputs); }
                if (cfg.draw_velocity) { ImGui::SameLine(); ImGui::ColorEdit4("Velocity##c", (float*)&cfg.col_velocity, ImGuiColorEditFlags_NoInputs); }
                if (cfg.chams_enabled) { ImGui::SameLine(); ImGui::ColorEdit4("Occluded##c", (float*)&cfg.col_box_occluded, ImGuiColorEditFlags_NoInputs); }
                if (cfg.draw_crosshair){ ImGui::SameLine(); ImGui::ColorEdit4("Crosshair##c", (float*)&cfg.col_crosshair, ImGuiColorEditFlags_NoInputs); }
            card_end();
        }
        else if (active == 3) {
            card_begin("Movement", {HALF, 296});
                toggle("WalkSpeed", &cfg.speed_enabled);
                if (cfg.speed_enabled) slider("Speed", &cfg.speed_value, 16.f, 200.f, "%.0f");
                toggle("JumpPower", &cfg.jump_enabled);
                if (cfg.jump_enabled) slider("Jump", &cfg.jump_value, 50.f, 500.f, "%.0f");
                toggle("Fly", &cfg.fly_enabled);
                if (cfg.fly_enabled) {
                    slider("Fly Speed", &cfg.fly_speed, 10.f, 300.f, "%.0f");
                    ImGui::TextDisabled("WASD move, Space up, Ctrl down.");
                }
                toggle("No-Clip", &cfg.noclip_enabled);
                toggle("Infinite Jump", &cfg.infinite_jump);
                toggle("Auto Bunny-Hop", &cfg.bunny_hop);
                toggle("Anti-AFK", &cfg.antiafk);
                if (ImGui::Button("Fly + No-Clip##combo", ImVec2(-FLT_MIN, 0))) {
                    cfg.fly_enabled = true; cfg.noclip_enabled = true;
                }
            card_end();
            ImGui::SameLine(0, 10);
            card_begin("Survival", {HALF, 296});
                toggle("Godmode (max HP)", &cfg.godmode);
                toggle("Hover", &cfg.hover_enabled);
                if (cfg.hover_enabled) slider("Hover Height", &cfg.hover_height, 0.f, 30.f, "%.1f");
                toggle("Jump Height", &cfg.jump_height_enabled);
                if (cfg.jump_height_enabled) slider("Height", &cfg.jump_height, 5.f, 100.f, "%.0f");
                toggle("Climb Any Slope", &cfg.no_slope_limit);
            card_end();

            card_begin("Anti-Aim", {FULL, 152});
                toggle("Spinbot", &cfg.antiaim_spin);
                if (cfg.antiaim_spin) slider("Spin Speed", &cfg.antiaim_spin_speed, 90.f, 1440.f, "%.0f/s");
                toggle("Hitbox Jitter", &cfg.antiaim_jitter);
                if (cfg.antiaim_jitter) slider("Jitter", &cfg.antiaim_jitter_amount, 1.f, 10.f, "%.1f studs");
            card_end();
        }
        else if (active == 4) {
            card_begin("Field of View", {HALF, 148});
                toggle("FOV Changer", &cfg.fov_changer_enabled);
                if (cfg.fov_changer_enabled) slider("Field of View", &cfg.fov_value, 40.f, 120.f, "%.0f");
            card_end();
            ImGui::SameLine(0, 10);
            card_begin("Zoom", {HALF, 148});
                toggle("Zoom Unlock", &cfg.zoom_unlock);
                if (cfg.zoom_unlock) slider("Max Zoom", &cfg.max_zoom, 100.f, 2000.f, "%.0f");
                toggle("Lock First Person", &cfg.lock_first_person);
            card_end();

            card_begin("Freecam", {FULL, 148});
                toggle("Freecam", &cfg.freecam);
                if (cfg.freecam) {
                    slider("Freecam Speed", &cfg.freecam_speed, 10.f, 300.f, "%.0f");
                    ImGui::TextDisabled("WASD move, arrows look, Space/Ctrl up/down.");
                }
            card_end();
        }
        else if (active == 5) {
            card_begin("General", {HALF, 168});
                toggle("Streamproof (hide from capture)", &cfg.streamproof);
                toggle("Per-Game Auto-Load Profile", &cfg.per_game_profiles);
                if (cfg.per_game_profiles)
                    ImGui::TextDisabled("Loads preset_<place>.bin over base config on launch.");
                toggle("Debug Console (F10)", &cfg.show_console);
                slider("TP Cursor Distance", &cfg.tp_mouse_dist, 10.f, 200.f, "%.0f");
            card_end();
            ImGui::SameLine(0, 10);
            card_begin("Preset Slots", {HALF, 168});
                for (int i = 1; i <= 3; i++) {
                    ImGui::PushID(i);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Slot %d", i);
                    ImGui::SameLine(74);
                    std::string pp = mem::exe_dir() + "\\preset_" + std::to_string(i) + ".bin";
                    if (ImGui::Button("Save")) { save_config(pp.c_str()); notify("Saved slot " + std::to_string(i)); }
                    ImGui::SameLine();
                    if (ImGui::Button("Load")) { if (load_config(pp.c_str())) notify("Loaded slot " + std::to_string(i)); }
                    ImGui::PopID();
                }
            card_end();

            card_begin("Keybinds", {FULL, 232});
                ImGui::TextDisabled("Click a bind, then press any key (Esc cancels).");
                ImGui::Columns(2, "kbcols", false);
                keybind("Fly",        &cfg.key_fly);
                keybind("No-Clip",    &cfg.key_noclip);
                keybind("Lock-On",    &cfg.key_lock);
                keybind("ESP",        &cfg.key_esp);
                keybind("Aimbot",     &cfg.key_aim);
                ImGui::NextColumn();
                keybind("Triggerbot", &cfg.key_trigger);
                keybind("Radar",      &cfg.key_radar);
                keybind("TP Cursor",  &cfg.key_tp_mouse);
                keybind("Panic",      &cfg.key_panic);
                ImGui::Columns(1);
            card_end();

            card_begin("Theme & Share", {FULL, 116});
                ImGui::ColorEdit3("Accent Color", cfg.accent, ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine(); ImGui::TextDisabled("- recolors the whole menu");
                if (ImGui::Button("Copy Config to Clipboard")) { set_clipboard(export_config_string()); notify("Config copied to clipboard"); }
                ImGui::SameLine();
                if (ImGui::Button("Load Config from Clipboard")) {
                    if (import_config_string(get_clipboard())) notify("Config loaded from clipboard");
                    else notify("Clipboard has no valid config");
                }
            card_end();
        }
        else if (active == 6) {

            char* script_buf = g_script_buf;
            char* status_line = g_script_status;
            const size_t script_buf_sz = sizeof(g_script_buf);
            const size_t status_line_sz = sizeof(g_script_status);

            card_begin("Lua Executor", {FULL, 0});
                bool have_state = executor::ready();
                if (have_state) {
                    ImGui::TextColored({0.55f, 0.9f, 0.55f, 1.f}, "\xe2\x97\x89 ready");
                    ImGui::SameLine();
                    ImGui::TextDisabled("lua_state = 0x%llX", (unsigned long long)executor::lua_state());
                } else {
                    ImGui::TextColored({0.95f, 0.55f, 0.4f, 1.f}, "\xe2\x97\x89 not ready");
                    ImGui::SameLine();
                    ImGui::TextDisabled("- press Refresh, or check offsets/luavm offets.txt is loaded");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Refresh state")) executor::refresh_state();

                float sy = ImGui::GetContentRegionAvail().y - 88.f;
                if (sy < 200.f) sy = 200.f;
                ImGui::InputTextMultiline("##editor", script_buf, script_buf_sz,
                                          {-1, sy}, ImGuiInputTextFlags_AllowTabInput);

                ImGui::BeginDisabled(!have_state);
                if (ImGui::Button("Execute (F5)", ImVec2(160, 30))) {
                    bool ok = executor::execute_source(script_buf);
                    snprintf(status_line, status_line_sz,
                             ok ? "dispatched" : "failed");
                }
                ImGui::SameLine();
                if (ImGui::Button("Ping Print", ImVec2(120, 30))) {
                    bool ok = executor::ping("UwUClient external: hello from ping");
                    snprintf(status_line, status_line_sz, ok ? "ping sent" : "ping failed");
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Clear", ImVec2(80, 30))) script_buf[0] = 0;
                ImGui::SameLine();
                ImGui::TextDisabled("F10 opens the log console — watch it for [OK]/[ERR]");
                if (status_line[0]) {
                    ImGui::SameLine();
                    ImGui::TextColored({0.65f, 0.85f, 1.f, 1.f}, "- %s", status_line);
                }
            card_end();
        }
        else if (active == 7) {
            static DebugTab debug_tab;
            debug_tab.Render(players);
        }
}

void draw_menu(uintptr_t render_view_ptr, const std::vector<PlayerInfo>& players) {
    static bool themed = false;
    if (!themed) { apply_theme(); themed = true; }
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowSize({1020, 700}, ImGuiCond_Once);
    ImGui::SetNextWindowPos({40, 40}, ImGuiCond_Once);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
    ImGui::Begin("UwUClient", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse);

    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 ACC = acc(1.f);
    const float header_h = 56.f, sidebar_w = 178.f, footer_h = 44.f;

    ImGui::SetCursorPos({0, 0});
    ImGui::InvisibleButton("##hdr", {ws.x, header_h});
    if (ImGui::IsItemActive())
        ImGui::SetWindowPos({wp.x + io.MouseDelta.x, wp.y + io.MouseDelta.y});
    dl->AddRectFilled(wp, {wp.x + ws.x, wp.y + header_h}, IM_COL32(11, 12, 15, 255),
                      10.f, ImDrawFlags_RoundCornersTop);
    dl->AddText(ImGui::GetFont(), 30.f, {wp.x + 22, wp.y + 12}, ACC, "UwUClient");
    dl->AddText({wp.x + 76, wp.y + 24}, IM_COL32(120, 125, 133, 255), "HvH");
    {
        char st[80];
        snprintf(st, sizeof(st), "%.0f fps  |  %d players", ImGui::GetIO().Framerate, (int)players.size());
        float sw = ImGui::CalcTextSize(st).x;
        dl->AddText({wp.x + ws.x - sw - 22, wp.y + 24}, IM_COL32(160, 165, 173, 255), st);
    }
    ImU32 a1 = acc(1.f), a0 = acc(0.f);
    float my = wp.x + ws.x * 0.5f;
    dl->AddRectFilledMultiColor({wp.x, wp.y + header_h - 2}, {my, wp.y + header_h}, a0, a1, a1, a0);
    dl->AddRectFilledMultiColor({my, wp.y + header_h - 2}, {wp.x + ws.x, wp.y + header_h}, a1, a0, a0, a1);

    struct Tab { const char* icon; const char* label; };
    static const Tab tabs[] = {
        {"\xe2\x9a\x94", "Aimbot"},
        {"\xe2\x97\x89", "ESP"},
        {"\xe2\x9c\xa6", "Visuals"},
        {"\xe2\x96\xb2", "Player"},
        {"\xe2\x96\xa1", "Camera"},
        {"\xe2\x9a\x99", "Config"},
        {"\xe2\x9a\xa1", "Execute"},
        {"\xe2\x87\x84", "Debug"},
    };
    constexpr int TAB_COUNT = 8;
    static int active = 0;
    ImGui::SetCursorPos({0, header_h});
    dl->AddRectFilled({wp.x, wp.y + header_h}, {wp.x + sidebar_w, wp.y + ws.y - footer_h},
                      IM_COL32(10, 11, 14, 255));
    ImGui::BeginChild("sidebar", {sidebar_w, ws.y - header_h - footer_h}, false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Dummy({0, 12});
        ImVec2 sp = ImGui::GetCursorScreenPos();
        const float th = 38.f, gap = 4.f;
        for (int i = 0; i < TAB_COUNT; i++) {
            ImGui::SetCursorPosX(6);
            if (sidebar_tab(tabs[i].icon, tabs[i].label, active == i)) active = i;
            ImGui::Dummy({0, gap});
        }
        static float ind = 0.f;
        float dt = (io.DeltaTime > 0.f && io.DeltaTime < 0.1f) ? io.DeltaTime : 1.f / 60.f;
        float ty = sp.y + active * (th + gap) + 10.f;
        ind += (ty - ind) * (1.f - expf(-16.f * dt));
        ImGui::GetWindowDrawList()->AddRectFilled({sp.x + 2, ind}, {sp.x + 5.f, ind + th - 20.f}, ACC, 2.f);
    }
    ImGui::EndChild();

    ImGui::SetCursorPos({sidebar_w, header_h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
    ImGui::BeginChild("content", {ws.x - sidebar_w, ws.y - header_h - footer_h}, false);
        render_tab_body(active, render_view_ptr, players);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    dl->AddRectFilled({wp.x, wp.y + ws.y - footer_h}, {wp.x + ws.x, wp.y + ws.y},
                      IM_COL32(13, 14, 17, 255), 10.f, ImDrawFlags_RoundCornersBottom);
    ImGui::SetCursorPos({16, ws.y - footer_h + 8});
    static std::string cfg_path = mem::exe_dir() + "\\config.bin";
    if (ImGui::Button("Save")) save_config(cfg_path.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Load")) load_config(cfg_path.c_str());
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("RSHIFT menu  |  END exit");

    ImGui::End();
    ImGui::PopStyleVar(2);
}

}
