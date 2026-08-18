
#include "../include/menu.hpp"
#include "../include/bgimage.hpp"
#include "../include/dumper.hpp"
#include "../include/esp.hpp"
#include "../include/executor.hpp"
#include "../include/memory.hpp"
#include "../include/offsets.hpp"
#include "../include/teleport.hpp"
#include "../include/scanner.hpp"
#include "../include/DebugTab.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include "../include/memory.hpp"
#include "../include/games.hpp"
#include "../include/log.hpp"

#include <Windows.h>
#include <mmsystem.h>
#include <shlobj.h>

namespace {
#include "../Framwork/font.h"
}

namespace {

constexpr ImU32 C_BG_TOP     = IM_COL32(  6,   6,   6, 248);
constexpr ImU32 C_BG_BOT     = IM_COL32(  0,   0,   0, 248);
constexpr ImU32 C_BG_SIDE_T  = IM_COL32(  4,   4,   4, 252);
constexpr ImU32 C_BG_SIDE_B  = IM_COL32(  0,   0,   0, 252);
constexpr ImU32 C_BG_ELEV    = IM_COL32( 14,  14,  14, 255);
constexpr ImU32 C_BG_ELEV_H  = IM_COL32( 22,  22,  22, 255);
constexpr ImU32 C_BG_ELEV_A  = IM_COL32( 32,  32,  32, 255);

constexpr ImU32 C_LINE       = IM_COL32( 40,  40,  40, 255);
constexpr ImU32 C_LINE_SOFT  = IM_COL32( 22,  22,  22, 255);
constexpr ImU32 C_LINE_HAIR  = IM_COL32( 60,  60,  60, 255);

constexpr ImU32 C_TEXT       = IM_COL32(240, 240, 240, 255);
constexpr ImU32 C_TEXT_MUTE  = IM_COL32(150, 150, 150, 255);
constexpr ImU32 C_TEXT_DIM   = IM_COL32( 80,  80,  80, 255);

constexpr ImU32 C_ACC        = IM_COL32(255, 120, 190, 255);
constexpr ImU32 C_ACC_DEEP   = IM_COL32(210,  70, 150, 255);
constexpr ImU32 C_ACC_GLOW   = IM_COL32(255, 140, 200,  80);
constexpr ImU32 C_ACC_GLOW_S = IM_COL32(255, 140, 200,  32);
constexpr ImU32 C_ACC2       = IM_COL32(220, 190, 210, 255);
constexpr ImU32 C_ACC2_GLOW  = IM_COL32(240, 200, 220,  50);

constexpr ImU32 C_OK         = IM_COL32(190, 220, 195, 255);
constexpr ImU32 C_BAD        = IM_COL32(230, 170, 175, 255);
constexpr ImU32 C_INK        = IM_COL32(  8,   8,   8, 255);

constexpr ImU32 C_PINK       = IM_COL32(255, 120, 190, 255);
constexpr ImU32 C_PINK_DEEP  = IM_COL32(210,  70, 150, 255);
constexpr ImU32 C_PINK_GLOW  = IM_COL32(255, 140, 200,  70);
constexpr ImU32 C_TERM_GRN   = IM_COL32(150, 230, 170, 255);
constexpr ImU32 C_TERM_AMB   = IM_COL32(240, 200, 100, 255);

ImFont* g_font_body    = nullptr;
ImFont* g_font_small   = nullptr;
ImFont* g_font_medium  = nullptr;
ImFont* g_font_header  = nullptr;
ImFont* g_font_display = nullptr;

enum class Screen { ModePicker, BootTerminal, Launcher, Menu };
std::atomic<Screen> g_screen{Screen::ModePicker};
std::atomic<bool>   g_visible{true};
std::atomic<bool>   g_scan_ok{false};
std::atomic<bool>   g_inject_clicked{false};
std::atomic<bool>   g_injected{false};
int g_active_cat = 0;

std::atomic<int>    g_access_mode{0};
std::atomic<bool>   g_kernel_fallback{false};

static char g_filter_buf[16][64] = {};

enum class Fty { Toggle, SliderF, SliderI, Segment, Section, Key };
struct Field {
    Fty type; const char* label; void* addr;
    float fmin, fmax, step;
    const char* fmt;
    const char* const* opts; int opt_count;
};
struct Module {
    const char* name; bool* enabled;
    const Field* body; int body_count;
};
struct Category { const char* name; const Module* modules; int module_count; };

#define F_TOGGLE(lbl, ptr)                {Fty::Toggle,  lbl, (void*)&(ptr), 0, 0, 0, nullptr, nullptr, 0}
#define F_SLIDF(lbl, ptr, mn, mx, s, fmt) {Fty::SliderF, lbl, (void*)&(ptr), mn, mx, s, fmt, nullptr, 0}
#define F_SLIDI(lbl, ptr, mn, mx, s)      {Fty::SliderI, lbl, (void*)&(ptr), (float)(mn), (float)(mx), (float)(s), nullptr, nullptr, 0}
#define F_SEG(lbl, ptr, opts_arr)         {Fty::Segment, lbl, (void*)&(ptr), 0, 0, 0, nullptr, opts_arr, (int)(sizeof(opts_arr)/sizeof(*opts_arr))}
#define F_SEC(lbl)                        {Fty::Section, lbl, nullptr, 0, 0, 0, nullptr, nullptr, 0}
#define F_KEY(lbl, ptr)                   {Fty::Key,     lbl, (void*)&(ptr), 0, 0, 0, nullptr, nullptr, 0}

static const char* const OPTS_BOX[]   = {"Off", "Corner", "Full"};
static const char* const OPTS_SNAP[]  = {"Bottom", "Center", "Top"};
static const char* const OPTS_BONE[]  = {"Head", "Chest"};
static const char* const OPTS_PRI2[]  = {"Nearest crosshair", "Lowest HP", "Role"};
static const char* const OPTS_CROSS[] = {"Plus","T","Dot","Circle","X","Gap"};

static const Field FLD_AIM[] = {
    F_SEC("Trigger"),
    F_KEY("Activation key", esp::cfg.aim_key),
    F_TOGGLE("Toggle mode (press = latch)", esp::cfg.aim_toggle_mode),
    F_SEC("Target"),
    F_SEG("Priority", esp::cfg.aim_priority, OPTS_PRI2),
    F_SEG("Bone",     esp::cfg.aim_bone,     OPTS_BONE),
    F_TOGGLE("Team check",              esp::cfg.team_check),
    F_TOGGLE("Multi-bone fallback",     esp::cfg.aim_multibone),
    F_TOGGLE("Sticky aim",              esp::cfg.aim_sticky),
    F_TOGGLE("Occlusion check (players only)", esp::cfg.aim_visibility_check),
    F_TOGGLE("Skip knocked/KO targets", esp::cfg.aim_knocked_check),
    F_SLIDF("FOV radius",   esp::cfg.fov_radius,    10.f, 1000.f, 5.f,  "%.0f px"),
    F_SLIDF("Max distance", esp::cfg.aim_max_dist,   0.f, 5000.f, 10.f, "%.0f"),
    F_SEC("Prediction"),
    F_TOGGLE("Enable prediction",   esp::cfg.aim_predict),
    F_SLIDF("Lead (base)",          esp::cfg.aim_prediction, 0.f, 0.5f, 0.01f, "%.2f"),
    F_SLIDF("Lead X/Z mult",        esp::cfg.aim_predict_xz, 0.f, 3.f, 0.05f, "%.2f"),
    F_SLIDF("Lead Y mult",          esp::cfg.aim_predict_y,  0.f, 3.f, 0.05f, "%.2f"),
    F_SEC("Response"),
    F_TOGGLE("Lock-on (instant)",   esp::cfg.aim_lock),
    F_SLIDF("Strength",             esp::cfg.aim_strength, 1.f, 100.f, 1.f, "%.0f %%"),
    F_SLIDF("Smoothing",            esp::cfg.aim_smooth, 0.1f, 1.f, 0.01f, "%.2f"),
    F_SLIDF("Deadzone",             esp::cfg.aim_deadzone, 0.f, 30.f, 1.f, "%.0f px"),
    F_TOGGLE("Humanize (jitter)",   esp::cfg.aim_jitter),
    F_SLIDF("Jitter amount",        esp::cfg.aim_jitter_strength, 0.1f, 2.f, 0.05f, "%.2f"),
    F_TOGGLE("Split X/Y smoothing", esp::cfg.aim_split_smooth),
    F_SLIDF("Smoothing (Y)",        esp::cfg.aim_smooth_y, 0.1f, 1.f, 0.01f, "%.2f"),
    F_SEC("Shake"),
    F_TOGGLE("Aim shake (humanize)", esp::cfg.aim_shake),
    F_SLIDF("Shake X",              esp::cfg.aim_shake_x, 0.f, 8.f, 0.1f, "%.1f"),
    F_SLIDF("Shake Y",              esp::cfg.aim_shake_y, 0.f, 8.f, 0.1f, "%.1f"),
};

static const char* const OPTS_SILENT_PART[] = {"Head", "Torso", "Nearest to cursor"};
static const char* const OPTS_RIVALS_FORMULA[] = {"Delta (2×(scr−tgt))", "Ratio (mouse×scr/tgt)"};

static const Field FLD_FREEZE[] = {
    F_KEY("Hold key (0 = always on)", esp::cfg.freeze_player_key),
};

static const Field FLD_TPSPIN[] = {
    F_KEY("Hold key (0 = always on)", esp::cfg.tp_spin_key),
    F_KEY("Cancel key",               esp::cfg.tp_spin_cancel_key),
    F_SLIDF("Radius",                 esp::cfg.tp_spin_radius, 2.f, 40.f, 0.5f, "%.1f studs"),
    F_SLIDF("Angular speed",          esp::cfg.tp_spin_speed,  0.5f, 30.f, 0.1f, "%.1f rad/s"),
};
static const Field FLD_SILENT[] = {
    F_SEC("Range"),
    F_SLIDF("FOV radius",           esp::cfg.fov_radius, 10.f, 1000.f, 5.f, "%.0f px"),
    F_TOGGLE("Show FOV circle",     esp::cfg.draw_fov),
    F_SLIDF("Max distance",         esp::cfg.aim_max_dist, 0.f, 5000.f, 10.f, "%.0f"),
    F_SEC("Target"),
    F_SEG("Aim part", esp::cfg.silent_aim_part, OPTS_SILENT_PART),
    F_TOGGLE("Team check",          esp::cfg.team_check),
    F_TOGGLE("Skip knocked / KO",   esp::cfg.silent_knocked_check),
    F_TOGGLE("Visibility check",    esp::cfg.aim_visibility_check),
    F_SEC("Prediction"),
    F_TOGGLE("Enable prediction",   esp::cfg.aim_predict),
    F_SLIDF("Lead (base)",          esp::cfg.aim_prediction, 0.f, 0.5f, 0.01f, "%.2f"),
    F_SEC("Rivals viewport tuning"),
    F_SEG("Formula",                esp::cfg.rivals_formula, OPTS_RIVALS_FORMULA),
    F_TOGGLE("Only write while firing (LMB)", esp::cfg.silent_only_when_firing),
    F_TOGGLE("Flip X",              esp::cfg.rivals_flip_x),
    F_TOGGLE("Flip Y",              esp::cfg.rivals_flip_y),
    F_SLIDF("X bias (px)",          esp::cfg.rivals_x_bias, -200.f, 200.f, 1.f, "%.0f"),
    F_SLIDF("Y bias (px)",          esp::cfg.rivals_y_bias, -200.f, 200.f, 1.f, "%.0f"),
    F_SEC("Arsenal CFrame flick"),
    F_TOGGLE("Continuous flick (auto weapons — visible)", esp::cfg.arsenal_continuous_flick),
};
static const Field FLD_TRIG[] = {
    F_KEY("Trigger key",   esp::cfg.trigger_key),
    F_SLIDF("Trigger FOV", esp::cfg.trigger_fov, 1.f, 50.f, 1.f, "%.0f px"),
    F_SLIDF("Hitbox scale", esp::cfg.trig_box_scale, 0.5f, 4.f, 0.05f, "%.2f\xC3\x97"),
    F_SLIDI("Fire delay",  esp::cfg.trigger_delay, 0, 500, 5),
    F_SLIDI("Acquire (ms)",esp::cfg.trig_acquire_ms, 0, 500, 5),
    F_TOGGLE("Show hitbox on screen", esp::cfg.trig_hitbox_viz),
    F_TOGGLE("Skip knife / melee",    esp::cfg.trig_knife_check),
};
static const Field FLD_AUTOFIRE[] = {
    F_SLIDF("Auto-fire FOV", esp::cfg.aim_autofire_fov, 1.f, 20.f, 1.f, "%.0f px"),
};
static const Field FLD_HITMARK[] = { F_TOGGLE("Beep on hit", esp::cfg.hitmarker_sound) };
static const Field FLD_WEAPON_MODS[] = {
    F_SEC("Recoil / spread"),
    F_TOGGLE("No recoil",   esp::cfg.arsenal_no_recoil),
    F_TOGGLE("No spread",   esp::cfg.arsenal_no_spread),
    F_SEC("Reload / fire"),
    F_TOGGLE("Instant reload",       esp::cfg.arsenal_auto_reload),
    F_TOGGLE("Rapid fire",           esp::cfg.arsenal_rapid_fire),
    F_SLIDF("Rapid fire rate (rps)", esp::cfg.arsenal_fire_rate, 1.f, 60.f, 1.f, "%.0f"),
};
static const Module MODS_COMBAT[] = {
    {"Aimbot",             &esp::cfg.aim_enabled,       FLD_AIM,          IM_ARRAYSIZE(FLD_AIM)},
    {"Silent aim",         &esp::cfg.silent_aim,        FLD_SILENT,       IM_ARRAYSIZE(FLD_SILENT)},
    {"Weapon mods (Arsenal/BB/PF/BP/CB)", nullptr,      FLD_WEAPON_MODS,  IM_ARRAYSIZE(FLD_WEAPON_MODS)},
    {"Triggerbot",         &esp::cfg.trigger_enabled,   FLD_TRIG,         IM_ARRAYSIZE(FLD_TRIG)},
    {"Auto-fire on lock",  &esp::cfg.aim_autofire,      FLD_AUTOFIRE, IM_ARRAYSIZE(FLD_AUTOFIRE)},
    {"Freeze target",      &esp::cfg.freeze_player,     FLD_FREEZE,   IM_ARRAYSIZE(FLD_FREEZE)},
    {"TP-spin (orbit lock)",&esp::cfg.tp_spin_enabled,  FLD_TPSPIN,   IM_ARRAYSIZE(FLD_TPSPIN)},
    {"Hitmarker",          &esp::cfg.hitmarker_enabled, FLD_HITMARK,  IM_ARRAYSIZE(FLD_HITMARK)},
    {"Killfeed",           &esp::cfg.killfeed_enabled,  nullptr, 0},
};

static const Field FLD_BOX[] = {
    F_SEG("Style",              esp::cfg.box_style, OPTS_BOX),
    F_TOGGLE("Filled background", esp::cfg.draw_filled),
    F_SLIDF("Thickness",        esp::cfg.box_thickness, 0.5f, 5.f, 0.1f, "%.1f"),
    F_TOGGLE("Team check",      esp::cfg.esp_team_check),
    F_TOGGLE("HP-colored",      esp::cfg.esp_box_health_color),
    F_TOGGLE("Wall-visible color split", esp::cfg.esp_wall_color_split),
    F_SLIDF("Max distance",     esp::cfg.max_dist, 50.f, 10000.f, 50.f, "%.0f"),
};
static const Field FLD_HP[]     = { F_TOGGLE("Show HP text", esp::cfg.draw_health_text) };
static const Field FLD_SNAP[]   = { F_SEG("Origin", esp::cfg.snapline_pos, OPTS_SNAP) };
static const Field FLD_LOOK[]   = { F_SLIDF("Length", esp::cfg.lookat_length, 2.f, 40.f, 1.f, "%.0f") };
static const Field FLD_VEL[]    = { F_SLIDF("Predict sec", esp::cfg.velocity_seconds, 0.1f, 3.f, 0.1f, "%.1f") };
static const Field FLD_FOV[]    = { F_SLIDF("Radius", esp::cfg.fov_radius, 10.f, 1000.f, 5.f, "%.0f") };
static const Field FLD_CROSS[]  = {
    F_SEG("Style",  esp::cfg.crosshair_style, OPTS_CROSS),
    F_SLIDF("Size", esp::cfg.crosshair_size, 2.f, 30.f, 1.f, "%.0f"),
};
static const Module MODS_ESP[] = {
    {"ESP (master)",      &esp::cfg.enabled,          FLD_BOX,   IM_ARRAYSIZE(FLD_BOX)},
    {"Names",             &esp::cfg.draw_name,        nullptr, 0},
    {"Distance",          &esp::cfg.draw_distance,    nullptr, 0},
    {"Speed",             &esp::cfg.draw_speed,       nullptr, 0},
    {"Weapon",            &esp::cfg.draw_weapon,      nullptr, 0},
    {"User ID",           &esp::cfg.esp_show_id,      nullptr, 0},
    {"Health bar",        &esp::cfg.draw_health,      FLD_HP,   IM_ARRAYSIZE(FLD_HP)},
    {"Snaplines",         &esp::cfg.draw_line,        FLD_SNAP, IM_ARRAYSIZE(FLD_SNAP)},
    {"Head dot",          &esp::cfg.draw_head_dot,    nullptr, 0},
    {"Off-screen arrows", &esp::cfg.draw_arrows,      nullptr, 0},
    {"Skeleton",          &esp::cfg.draw_skeleton,    nullptr, 0},
    {"Target line",       &esp::cfg.esp_target_line,  nullptr, 0},
    {"Look-at rays",      &esp::cfg.draw_lookat,      FLD_LOOK, IM_ARRAYSIZE(FLD_LOOK)},
    {"Velocity lines",    &esp::cfg.draw_velocity,    FLD_VEL,  IM_ARRAYSIZE(FLD_VEL)},
    {"Chams",             &esp::cfg.chams_enabled,    nullptr, 0},
    {"FOV circle",        &esp::cfg.draw_fov,         FLD_FOV,  IM_ARRAYSIZE(FLD_FOV)},
    {"Crosshair",         &esp::cfg.draw_crosshair,   FLD_CROSS,IM_ARRAYSIZE(FLD_CROSS)},
    {"Text shadow",       &esp::cfg.text_shadow,      nullptr, 0},
};

static const Field FLD_BRIGHT[] = { F_SLIDF("Level", esp::cfg.world_bright, 0.f, 10.f, 0.1f, "%.1f") };
static const Field FLD_FPS[]    = { F_SLIDI("Cap",  esp::cfg.fps_cap, 30, 1000, 10) };
static const Module MODS_VISUALS[] = {
    {"Fullbright",          &esp::cfg.fullbright,           nullptr, 0},
    {"No skybox",           &esp::cfg.no_skybox,            nullptr, 0},
    {"No blur",             &esp::cfg.vis_no_blur,          nullptr, 0},
    {"No depth of field",   &esp::cfg.vis_no_dof,           nullptr, 0},
    {"No sun rays",         &esp::cfg.vis_no_sunrays,       nullptr, 0},
    {"No atmosphere",       &esp::cfg.vis_no_atmosphere,    nullptr, 0},
    {"No fog",              &esp::cfg.world_nofog,          nullptr, 0},
    {"Custom brightness",   &esp::cfg.world_bright_enabled, FLD_BRIGHT, IM_ARRAYSIZE(FLD_BRIGHT)},
    {"FPS unlock",          &esp::cfg.fps_unlock,           FLD_FPS,    IM_ARRAYSIZE(FLD_FPS)},
    {"Fullbright (voxel)",  &esp::cfg.ff_fullbright,        nullptr, 0},
    {"Grey sky",            &esp::cfg.ff_gray_sky,          nullptr, 0},
    {"Hide adorns",         &esp::cfg.ff_no_adorns,         nullptr, 0},
    {"No grass",            &esp::cfg.ff_no_grass,          nullptr, 0},
    {"Low textures",        &esp::cfg.ff_low_texture,       nullptr, 0},
};

static const Field FLD_TIME[] = { F_SLIDF("Clock", esp::cfg.world_time,    0.f,  24.f, 0.5f, "%.1f h") };
static const Field FLD_GRAV[] = { F_SLIDF("Force", esp::cfg.world_gravity, 0.f, 400.f, 5.f,  "%.0f") };
static const Field FLD_TICK[] = {
    F_SLIDF("Steps/sec", esp::cfg.tickrate_amount, 1.f, 500.f, 1.f, "%.0f"),
};
static const Module MODS_WORLD[] = {
    {"Time of day", &esp::cfg.world_time_enabled,    FLD_TIME, IM_ARRAYSIZE(FLD_TIME)},
    {"Gravity",     &esp::cfg.world_gravity_enabled, FLD_GRAV, IM_ARRAYSIZE(FLD_GRAV)},
    {"Tickrate hack", &esp::cfg.tickrate_enabled,    FLD_TICK, IM_ARRAYSIZE(FLD_TICK)},
    {"Streamproof", &esp::cfg.streamproof,           nullptr, 0},
};

static const Field FLD_FLY[] = {
    F_SLIDF("Speed", esp::cfg.fly_speed, 10.f, 300.f, 5.f, "%.0f"),
    F_TOGGLE("Planar (W = XZ forward)", esp::cfg.fly_planar),
};
static const Field FLD_WS[]  = { F_SLIDF("Speed",  esp::cfg.speed_value,  16.f, 200.f, 2.f,  "%.0f") };
static const Field FLD_JP[]  = { F_SLIDF("Power",  esp::cfg.jump_value,   50.f, 500.f, 10.f, "%.0f") };
static const Field FLD_JH[]  = { F_SLIDF("Height", esp::cfg.jump_height,   5.f, 100.f, 1.f,  "%.0f") };
static const Field FLD_HOV[] = { F_SLIDF("Height", esp::cfg.hover_height,  2.f,  30.f, 0.5f, "%.1f studs") };
static const Module MODS_MOVE[] = {
    {"Fly",            &esp::cfg.fly_enabled,         FLD_FLY, IM_ARRAYSIZE(FLD_FLY)},
    {"WalkSpeed",      &esp::cfg.speed_enabled,       FLD_WS,  IM_ARRAYSIZE(FLD_WS)},
    {"JumpPower",      &esp::cfg.jump_enabled,        FLD_JP,  IM_ARRAYSIZE(FLD_JP)},
    {"Jump height",    &esp::cfg.jump_height_enabled, FLD_JH,  IM_ARRAYSIZE(FLD_JH)},
    {"Hover",          &esp::cfg.hover_enabled,       FLD_HOV, IM_ARRAYSIZE(FLD_HOV)},
    {"No-clip",        &esp::cfg.noclip_enabled,      nullptr, 0},
    {"Infinite jump",  &esp::cfg.infinite_jump,       nullptr, 0},
    {"Auto bunny-hop", &esp::cfg.bunny_hop,           nullptr, 0},
    {"Climb any slope",&esp::cfg.no_slope_limit,      nullptr, 0},
    {"Anti-AFK",       &esp::cfg.antiafk,             nullptr, 0},
    {"Godmode",        &esp::cfg.godmode,             nullptr, 0},
};

static const Field FLD_FOV2[] = { F_SLIDF("Field of view", esp::cfg.fov_value,           40.f,  120.f, 1.f,  "%.0f") };
static const Field FLD_ZOOM[] = { F_SLIDF("Max zoom",      esp::cfg.max_zoom,           100.f, 2000.f, 20.f, "%.0f") };
static const Field FLD_FC[]   = { F_SLIDF("Speed",         esp::cfg.freecam_speed,       10.f,  300.f, 5.f,  "%.0f") };
static const Field FLD_SPIN[] = { F_SLIDF("Speed",         esp::cfg.antiaim_spin_speed,  90.f, 1440.f, 10.f, "%.0f °/s") };
static const Field FLD_HJ[]   = { F_SLIDF("Amount",        esp::cfg.antiaim_jitter_amount, 1.f, 10.f,  0.5f, "%.1f") };
static const Module MODS_CAM[] = {
    {"FOV changer",       &esp::cfg.fov_changer_enabled, FLD_FOV2, IM_ARRAYSIZE(FLD_FOV2)},
    {"Zoom unlock",       &esp::cfg.zoom_unlock,         FLD_ZOOM, IM_ARRAYSIZE(FLD_ZOOM)},
    {"Freecam",           &esp::cfg.freecam,             FLD_FC,   IM_ARRAYSIZE(FLD_FC)},
    {"Lock first person", &esp::cfg.lock_first_person,   nullptr, 0},
    {"Spinbot",           &esp::cfg.antiaim_spin,        FLD_SPIN, IM_ARRAYSIZE(FLD_SPIN)},
    {"Hitbox jitter",     &esp::cfg.antiaim_jitter,      FLD_HJ,   IM_ARRAYSIZE(FLD_HJ)},
};

static const Field FLD_RAD[] = {
    F_SLIDF("Size",  esp::cfg.radar_size,  70.f, 200.f, 5.f, "%.0f"),
    F_SLIDF("Range", esp::cfg.radar_range, 50.f, 500.f, 5.f, "%.0f"),
    F_TOGGLE("Names", esp::cfg.radar_names),
};
static const Field FLD_PRX[] = {
    F_SLIDF("Range",    esp::cfg.sound_alert_range, 20.f,  300.f, 5.f, "%.0f"),
    F_SLIDI("Cooldown", esp::cfg.sound_alert_cd_ms, 200, 5000, 50),
};
static const Module MODS_HUD[] = {
    {"Radar",            &esp::cfg.radar_enabled,        FLD_RAD, IM_ARRAYSIZE(FLD_RAD)},
    {"Proximity alerts", &esp::cfg.sound_alerts,         FLD_PRX, IM_ARRAYSIZE(FLD_PRX)},
    {"Feature HUD",      &esp::cfg.draw_hud,             nullptr, 0},
    {"Watermark",        &esp::cfg.draw_watermark,       nullptr, 0},
    {"Keybind overlay",  &esp::cfg.draw_keybind_overlay, nullptr, 0},
    {"Notifications",    &esp::cfg.notifications,        nullptr, 0},
    {"Debug console",    &esp::cfg.show_console,         nullptr, 0},
};

static const Field FLD_KEYS[] = {
    F_SEC("Toggle keys"),
    F_KEY("Fly",         esp::cfg.key_fly),
    F_KEY("No-clip",     esp::cfg.key_noclip),
    F_KEY("Lock-on",     esp::cfg.key_lock),
    F_KEY("ESP",         esp::cfg.key_esp),
    F_KEY("Aimbot",      esp::cfg.key_aim),
    F_KEY("Triggerbot",  esp::cfg.key_trigger),
    F_KEY("Radar",       esp::cfg.key_radar),
    F_SEC("Utility"),
    F_KEY("TP under cursor", esp::cfg.key_tp_mouse),
    F_KEY("Panic (kill all)", esp::cfg.key_panic),
    F_SLIDF("Cursor TP distance", esp::cfg.tp_mouse_dist, 10.f, 200.f, 5.f, "%.0f studs"),
    F_SEC("Profiles"),
    F_TOGGLE("Per-game auto-load", esp::cfg.per_game_profiles),
};
static const Module MODS_CONFIG[] = {
    {"Keybinds", nullptr, FLD_KEYS, IM_ARRAYSIZE(FLD_KEYS)},
};

static const Category CATS[] = {
    {"Combat",   MODS_COMBAT,   IM_ARRAYSIZE(MODS_COMBAT)},
    {"ESP",      MODS_ESP,      IM_ARRAYSIZE(MODS_ESP)},
    {"Visuals",  MODS_VISUALS,  IM_ARRAYSIZE(MODS_VISUALS)},
    {"World",    MODS_WORLD,    IM_ARRAYSIZE(MODS_WORLD)},
    {"Movement", MODS_MOVE,     IM_ARRAYSIZE(MODS_MOVE)},
    {"Camera",   MODS_CAM,      IM_ARRAYSIZE(MODS_CAM)},
    {"HUD",      MODS_HUD,      IM_ARRAYSIZE(MODS_HUD)},
    {"Config",   MODS_CONFIG,   IM_ARRAYSIZE(MODS_CONFIG)},
    {"Explorer", nullptr,       0},
    {"Profiles", nullptr,       0},
    {"Players",  nullptr,       0},
};
constexpr int CAT_COUNT       = IM_ARRAYSIZE(CATS);
constexpr int CAT_EXPLORER_IX = CAT_COUNT - 3;
constexpr int CAT_PROFILES_IX = CAT_COUNT - 2;
constexpr int CAT_PLAYERS_IX  = CAT_COUNT - 1;

static std::string g_players_filter;
static char        g_players_filter_buf[64] = {};

static std::unordered_map<ImGuiID, float> g_anim;
static float anim_to(ImGuiID id, float target, float speed = 16.f) {
    auto& v = g_anim[id];
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.f) dt = 1.f / 60.f;
    float t = 1.f - std::exp(-speed * dt);
    v += (target - v) * t;
    if (std::fabs(v - target) < 0.001f) v = target;
    return v;
}

static float uptime_seconds() {
    static auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
}

static ImU32 blend(ImU32 a, ImU32 b, float t) {
    if (t <= 0.f) return a; if (t >= 1.f) return b;
    ImVec4 av = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 bv = ImGui::ColorConvertU32ToFloat4(b);
    ImVec4 o{ av.x + (bv.x - av.x) * t,
              av.y + (bv.y - av.y) * t,
              av.z + (bv.z - av.z) * t,
              av.w + (bv.w - av.w) * t };
    return ImGui::ColorConvertFloat4ToU32(o);
}
static ImU32 with_alpha(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

static bool hit(ImRect r) { return ImGui::IsMouseHoveringRect(r.Min, r.Max); }

static void drop_shadow(ImDrawList* dl, ImVec2 tl, ImVec2 br, float rounding, float radius = 22.f, float strength = 0.55f) {
    for (int i = 0; i < 6; i++) {
        float s = (i + 1) / 6.f;
        float e = radius * s;
        ImU32 col = IM_COL32(0, 0, 0, (int)(strength * 255.f * (1.f - s) * (1.f - s) * 0.65f));
        dl->AddRectFilled({tl.x - e, tl.y - e * 0.4f + 4.f},
                          {br.x + e, br.y + e},
                          col, rounding + e);
    }
}

static void draw_icon(ImDrawList* dl, int cat, ImVec2 c, float r, ImU32 col, float thick = 1.5f) {

    switch (cat) {
        case 0: {
            dl->AddCircle(c, r * 0.72f, col, 20, thick);
            dl->AddLine({c.x - r * 1.05f, c.y}, {c.x - r * 0.42f, c.y}, col, thick);
            dl->AddLine({c.x + r * 0.42f, c.y}, {c.x + r * 1.05f, c.y}, col, thick);
            dl->AddLine({c.x, c.y - r * 1.05f}, {c.x, c.y - r * 0.42f}, col, thick);
            dl->AddLine({c.x, c.y + r * 0.42f}, {c.x, c.y + r * 1.05f}, col, thick);
            dl->AddCircleFilled(c, thick * 0.9f, col);
            break;
        }
        case 1: {
            const int seg = 16;
            const float w = r * 1.1f, h = r * 0.55f;
            dl->PathClear();
            for (int i = 0; i <= seg; i++) {
                float t = (float)i / seg;
                float x = -w + 2.f * w * t;
                float y = -h * std::sin(t * 3.14159f);
                dl->PathLineTo({c.x + x, c.y + y});
            }
            dl->PathStroke(col, 0, thick);
            dl->PathClear();
            for (int i = 0; i <= seg; i++) {
                float t = (float)i / seg;
                float x = -w + 2.f * w * t;
                float y =  h * std::sin(t * 3.14159f);
                dl->PathLineTo({c.x + x, c.y + y});
            }
            dl->PathStroke(col, 0, thick);
            dl->AddCircleFilled(c, r * 0.28f, col);
            break;
        }
        case 2: {
            dl->AddCircle(c, r * 0.42f, col, 16, thick);
            for (int i = 0; i < 8; i++) {
                float a = i * 3.14159f / 4.f;
                float cs = std::cos(a), sn = std::sin(a);
                dl->AddLine({c.x + cs * r * 0.62f, c.y + sn * r * 0.62f},
                            {c.x + cs * r * 0.95f, c.y + sn * r * 0.95f},
                            col, thick);
            }
            break;
        }
        case 3: {
            dl->AddCircle(c, r * 0.9f, col, 22, thick);
            dl->AddLine({c.x - r * 0.9f, c.y}, {c.x + r * 0.9f, c.y}, col, thick);

            dl->PathClear();
            const int seg = 20;
            for (int i = 0; i <= seg; i++) {
                float t = (float)i / seg;
                float ang = -1.5707963f + t * 3.14159f;
                dl->PathLineTo({c.x + std::cos(ang) * r * 0.35f, c.y + std::sin(ang) * r * 0.9f});
            }
            dl->PathStroke(col, 0, thick);
            break;
        }
        case 4: {
            dl->AddLine({c.x - r * 0.7f, c.y + r * 0.15f}, {c.x, c.y - r * 0.4f}, col, thick);
            dl->AddLine({c.x + r * 0.7f, c.y + r * 0.15f}, {c.x, c.y - r * 0.4f}, col, thick);
            dl->AddLine({c.x - r * 0.7f, c.y + r * 0.65f}, {c.x, c.y + r * 0.10f}, col, thick);
            dl->AddLine({c.x + r * 0.7f, c.y + r * 0.65f}, {c.x, c.y + r * 0.10f}, col, thick);
            break;
        }
        case 5: {
            ImVec2 tl{c.x - r * 0.95f, c.y - r * 0.55f};
            ImVec2 br{c.x + r * 0.95f, c.y + r * 0.70f};
            dl->AddRect(tl, br, col, 2.5f, 0, thick);

            dl->AddLine({c.x - r * 0.3f, tl.y}, {c.x - r * 0.15f, tl.y - r * 0.20f}, col, thick);
            dl->AddLine({c.x + r * 0.3f, tl.y}, {c.x + r * 0.15f, tl.y - r * 0.20f}, col, thick);
            dl->AddLine({c.x - r * 0.15f, tl.y - r * 0.20f},
                        {c.x + r * 0.15f, tl.y - r * 0.20f}, col, thick);
            dl->AddCircle({c.x, c.y + r * 0.10f}, r * 0.34f, col, 16, thick);
            break;
        }
        case 6: {
            float y = c.y - r * 0.55f;
            float gap = r * 0.35f;
            dl->AddLine({c.x - r * 0.85f, y},            {c.x + r * 0.85f, y},            col, thick * 1.4f);
            dl->AddLine({c.x - r * 0.85f, y + gap},      {c.x + r * 0.30f, y + gap},      col, thick * 1.4f);
            dl->AddLine({c.x - r * 0.85f, y + gap * 2},  {c.x + r * 0.60f, y + gap * 2},  col, thick * 1.4f);
            break;
        }
        case 7: {
            const int teeth = 8;
            dl->AddCircle(c, r * 0.55f, col, 22, thick);
            dl->AddCircleFilled(c, r * 0.16f, col);
            for (int i = 0; i < teeth; i++) {
                float a = i * 3.14159f * 2.f / teeth;
                float cs = std::cos(a), sn = std::sin(a);
                dl->AddLine({c.x + cs * r * 0.62f, c.y + sn * r * 0.62f},
                            {c.x + cs * r * 0.95f, c.y + sn * r * 0.95f},
                            col, thick * 1.15f);
            }
            break;
        }
        case 8: {
            ImVec2 tl{c.x - r * 1.0f,  c.y - r * 0.42f};
            ImVec2 br{c.x + r * 1.0f,  c.y + r * 0.42f};
            dl->AddRect(tl, br, col, r * 0.42f, 0, thick);

            dl->AddCircleFilled({c.x - r * 0.55f, c.y}, 1.6f, col);
            dl->AddCircleFilled({c.x + r * 0.55f, c.y - r * 0.15f}, 1.6f, col);
            dl->AddCircleFilled({c.x + r * 0.55f, c.y + r * 0.15f}, 1.6f, col);
            dl->AddCircleFilled({c.x + r * 0.75f, c.y}, 1.6f, col);
            dl->AddCircleFilled({c.x + r * 0.35f, c.y}, 1.6f, col);
            break;
        }
        case 9: {
            dl->AddCircleFilled({c.x - r * 0.55f, c.y - r * 0.55f}, 2.f, col);

            dl->AddLine({c.x - r * 0.55f, c.y - r * 0.55f + 3.f},
                        {c.x - r * 0.55f, c.y + r * 0.6f}, col, thick);

            for (int i = 0; i < 3; i++) {
                float y = c.y - r * 0.2f + i * (r * 0.4f);
                dl->AddLine({c.x - r * 0.55f, y}, {c.x + r * 0.3f, y}, col, thick);
                dl->AddCircleFilled({c.x + r * 0.4f, y}, 1.8f, col);
            }
            break;
        }
        case 10: {
            ImVec2 tl{c.x - r * 0.65f, c.y - r * 0.9f};
            ImVec2 br{c.x + r * 0.65f, c.y + r * 0.35f};
            dl->AddLine(tl,                   {br.x, tl.y}, col, thick);
            dl->AddLine(tl,                   {tl.x, br.y}, col, thick);
            dl->AddLine({br.x, tl.y},         {br.x, br.y}, col, thick);
            dl->AddLine({tl.x, br.y}, {c.x, c.y + r * 0.75f}, col, thick);
            dl->AddLine({br.x, br.y}, {c.x, c.y + r * 0.75f}, col, thick);
            dl->AddLine({c.x - r * 0.30f, c.y - r * 0.35f},
                        {c.x + r * 0.30f, c.y - r * 0.35f}, col, thick * 0.9f);
            break;
        }
        case 11: {
            dl->AddCircle({c.x - r * 0.30f, c.y - r * 0.15f}, r * 0.32f, col, 14, thick);
            dl->AddCircle({c.x + r * 0.42f, c.y - r * 0.05f}, r * 0.26f, col, 14, thick);
            dl->PathClear();
            dl->PathArcTo({c.x - r * 0.30f, c.y + r * 0.70f}, r * 0.55f, 3.14159f, 2.f * 3.14159f, 12);
            dl->PathStroke(col, 0, thick);
            dl->PathClear();
            dl->PathArcTo({c.x + r * 0.42f, c.y + r * 0.75f}, r * 0.45f, 3.14159f, 2.f * 3.14159f, 12);
            dl->PathStroke(col, 0, thick);
            break;
        }
        default: {
            dl->AddCircleFilled(c, r * 0.3f, col);
        }
    }
}

static void draw_switch(ImDrawList* dl, ImVec2 tl, bool on, bool hovered, ImGuiID id) {
    const float W = 34.f, H = 18.f;
    ImVec2 br(tl.x + W, tl.y + H);

    float t = anim_to(id, on ? 1.f : 0.f, 22.f);
    float th = anim_to(id ^ 0xA55A, hovered ? 1.f : 0.f, 18.f);

    ImU32 off_a = IM_COL32(40, 40, 40, 255);
    ImU32 off_b = IM_COL32(52, 52, 52, 255);
    ImU32 on_a  = C_ACC_DEEP;
    ImU32 on_b  = C_ACC;
    ImU32 col_a = blend(off_a, on_a, t);
    ImU32 col_b = blend(off_b, on_b, t);
    dl->AddRectFilledMultiColor(tl, br, col_a, col_b, col_b, col_a);
    dl->AddRect(tl, br, blend(C_LINE, C_ACC, t), H * 0.5f, 0, 1.f);

    if (t > 0.01f) {
        for (int i = 0; i < 4; i++) {
            float s = (i + 1) * 2.f;
            dl->AddRect({tl.x - s, tl.y - s}, {br.x + s, br.y + s},
                        with_alpha(C_ACC_GLOW_S, t * (1.f - i / 4.f)),
                        (H * 0.5f) + s, 0, 1.f);
        }
    }

    float pad = 2.f;
    float dia = H - pad * 2.f;
    float x0 = tl.x + pad;
    float x1 = br.x - pad - dia;
    float cx = x0 + (x1 - x0) * t;
    ImU32 knob = blend(IM_COL32(220, 220, 220, 255), C_INK, t);
    dl->AddRectFilled({cx, tl.y + pad}, {cx + dia, br.y - pad}, knob, dia * 0.5f);

    if (th > 0.02f) {
        dl->AddCircle({cx + dia * 0.5f, tl.y + H * 0.5f},
                      dia * 0.5f + 2.f, with_alpha(C_ACC, th * 0.6f), 16, 1.f);
    }

    if (t > 0.5f) {
        dl->AddCircleFilled({cx + dia * 0.5f, tl.y + H * 0.5f}, 2.2f * t, C_ACC);
    }
}

static void void_slider_f(const char* label, float* v, float mn, float mx, float step, const char* fmt) {
    (void)step;
    ImGui::PushID(label);
    ImGuiWindow* win = ImGui::GetCurrentWindow();

    float w;
    if (win->DC.ItemWidthStack.Size > 0) {
        w = ImGui::CalcItemWidth();
        float cra = ImGui::GetContentRegionAvail().x;
        if (w > cra) w = cra;
    } else {
        w = ImGui::GetContentRegionAvail().x;
    }
    if (w < 40.f) w = 40.f;
    float H = 40.f;
    ImVec2 pos = win->DC.CursorPos;
    ImRect bb(pos, {pos.x + w, pos.y + H});
    ImGui::ItemSize(bb, 0.f);
    ImGuiID id = win->GetID("sf");
    ImGui::ItemAdd(bb, id);

    ImDrawList* dl = win->DrawList;

    if (g_font_small) ImGui::PushFont(g_font_small);
    dl->AddText({pos.x, pos.y}, C_TEXT_MUTE, label);
    char buf[32]; std::snprintf(buf, sizeof(buf), fmt ? fmt : "%.2f", *v);
    ImVec2 sz = ImGui::CalcTextSize(buf);
    if (g_font_small) ImGui::PopFont();
    dl->AddText({bb.Max.x - sz.x, pos.y - 1.f}, C_TEXT, buf);

    float track_y = pos.y + 24.f;
    ImRect track({pos.x, track_y}, {pos.x + w, track_y + 4.f});

    bool hovered = hit(ImRect({track.Min.x, track.Min.y - 6.f}, {track.Max.x, track.Max.y + 6.f})) || hit(bb);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ImGui::SetActiveID(id, win);
    if (ImGui::GetActiveID() == id && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) ImGui::ClearActiveID();
    if (ImGui::GetActiveID() == id) {
        float t = (ImGui::GetIO().MousePos.x - track.Min.x) / track.GetWidth();
        if (t < 0) t = 0; if (t > 1) t = 1;
        float nv = mn + t * (mx - mn);
        if (step > 0.f) nv = std::round(nv / step) * step;
        *v = nv;
    }

    float pct = (mx > mn) ? ((*v - mn) / (mx - mn)) : 0.f;
    if (pct < 0) pct = 0; if (pct > 1) pct = 1;
    dl->AddRectFilled(track.Min, track.Max, IM_COL32(22, 22, 22, 255), 2.f);

    if (pct > 0.001f) {
        ImVec2 fmax{track.Min.x + track.GetWidth() * pct, track.Max.y};
        dl->AddRectFilledMultiColor(track.Min, fmax, C_ACC_DEEP, C_ACC, C_ACC, C_ACC_DEEP);
    }

    ImVec2 knob{track.Min.x + track.GetWidth() * pct, track_y + 2.f};
    float knob_hover = anim_to(id ^ 0xBEEF, hovered ? 1.f : 0.f, 16.f);
    if (pct > 0.001f) {
        dl->AddCircleFilled(knob, 8.f + 2.f * knob_hover, with_alpha(C_ACC_GLOW, 0.7f));
    }
    dl->AddCircleFilled(knob, 5.5f, C_ACC);
    dl->AddCircle(knob, 5.5f, IM_COL32(6, 6, 6, 220), 0, 1.f);

    ImGui::PopID();
}
static void void_slider_i(const char* label, int* v, int mn, int mx, int step) {
    float fv = (float)*v;
    void_slider_f(label, &fv, (float)mn, (float)mx, (float)step, "%.0f");
    *v = (int)std::round(fv);
}

static void void_segment(const char* label, int* v, const char* const* opts, int n) {
    ImGui::PushID(label);
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    float w = ImGui::GetContentRegionAvail().x;
    float H = 42.f;
    ImVec2 pos = win->DC.CursorPos;
    ImRect bb(pos, {pos.x + w, pos.y + H});
    ImGui::ItemSize(bb, 0.f);
    ImDrawList* dl = win->DrawList;

    if (g_font_small) ImGui::PushFont(g_font_small);
    dl->AddText(pos, C_TEXT_MUTE, label);
    if (g_font_small) ImGui::PopFont();

    float strip_y = pos.y + 19.f;
    ImRect strip({pos.x, strip_y}, {pos.x + w, strip_y + 22.f});
    dl->AddRectFilled(strip.Min, strip.Max, IM_COL32(14, 14, 14, 255), 6.f);
    dl->AddRect(strip.Min, strip.Max, C_LINE_SOFT, 6.f, 0, 1.f);

    float pad_in = 3.f;
    float seg_w = (strip.GetWidth() - pad_in * 2.f) / n;
    for (int i = 0; i < n; i++) {
        ImRect r({strip.Min.x + pad_in + i * seg_w, strip.Min.y + pad_in},
                 {strip.Min.x + pad_in + (i + 1) * seg_w, strip.Max.y - pad_in});
        bool hovered = hit(r);
        bool press   = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (press) *v = i;
        bool on = (*v == i);
        ImGuiID id = win->GetID(("seg" + std::to_string(i)).c_str());
        float t  = anim_to(id, on ? 1.f : 0.f, 22.f);
        float th = anim_to(id ^ 0x7C7C, hovered ? 1.f : 0.f, 18.f);

        if (t > 0.02f) {
            dl->AddRectFilledMultiColor(r.Min, r.Max,
                blend(C_BG_ELEV_H, C_ACC_DEEP, t),
                blend(C_BG_ELEV_H, C_ACC,      t),
                blend(C_BG_ELEV_H, C_ACC,      t),
                blend(C_BG_ELEV_H, C_ACC_DEEP, t));
            dl->AddRect(r.Min, r.Max, with_alpha(C_ACC, t * 0.8f), 5.f, 0, 1.f);
        } else if (th > 0.02f) {
            dl->AddRectFilled(r.Min, r.Max, with_alpha(C_BG_ELEV_H, th), 5.f);
        }
        ImU32 fg = on ? C_INK : blend(C_TEXT_MUTE, C_TEXT, th);
        ImVec2 ts = ImGui::CalcTextSize(opts[i]);
        dl->AddText({r.Min.x + (r.GetWidth() - ts.x) * 0.5f,
                     r.Min.y + (r.GetHeight() - ts.y) * 0.5f}, fg, opts[i]);
    }
    ImGui::PopID();
}

static const char* vk_name(int vk) {
    static char buf[16];
    if (vk == 0) return "None";
    switch (vk) {
        case 0x01: return "LMB";  case 0x02: return "RMB";
        case 0x04: return "MMB";  case 0x05: return "MB4"; case 0x06: return "MB5";
        case 0x08: return "Back"; case 0x09: return "Tab"; case 0x0D: return "Enter";
        case 0x10: return "Shift";case 0x11: return "Ctrl";case 0x12: return "Alt";
        case 0x14: return "Caps"; case 0x1B: return "Esc"; case 0x20: return "Space";
        case 0x21: return "PgUp"; case 0x22: return "PgDn";
        case 0x23: return "End";  case 0x24: return "Home";
        case 0x25: return "Left"; case 0x26: return "Up";  case 0x27: return "Right"; case 0x28: return "Down";
        case 0x2C: return "PrtSc";case 0x2D: return "Ins"; case 0x2E: return "Del";
        case 0x5B: return "LWin"; case 0x5C: return "RWin";
        case 0xA0: return "LShift"; case 0xA1: return "RShift";
        case 0xA2: return "LCtrl";  case 0xA3: return "RCtrl";
        case 0xA4: return "LAlt";   case 0xA5: return "RAlt";
    }
    if (vk >= 0x30 && vk <= 0x39) { std::snprintf(buf, sizeof(buf), "%c", vk); return buf; }
    if (vk >= 0x41 && vk <= 0x5A) { std::snprintf(buf, sizeof(buf), "%c", vk); return buf; }
    if (vk >= 0x70 && vk <= 0x7B) { std::snprintf(buf, sizeof(buf), "F%d", vk - 0x6F); return buf; }
    if (vk >= 0x60 && vk <= 0x69) { std::snprintf(buf, sizeof(buf), "Num%d", vk - 0x60); return buf; }
    std::snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

static int*  g_listen_key = nullptr;
static bool  g_listen_preheld[256]  = {};
static bool  g_listen_last_down[256] = {};

static void start_listen(int* pk) {
    g_listen_key = pk;
    for (int vk = 0; vk < 256; ++vk) {
        bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
        g_listen_preheld[vk]  = d;
        g_listen_last_down[vk] = d;
    }
}
static void poll_keybind_listen() {
    if (!g_listen_key) return;
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) && !g_listen_preheld[VK_ESCAPE]) {
        g_listen_key = nullptr;
        return;
    }
    for (int vk = 0x01; vk <= 0xFE; ++vk) {
        if (vk == VK_ESCAPE) continue;
        if (vk == VK_LBUTTON) continue;
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool prev = g_listen_last_down[vk];
        g_listen_last_down[vk] = down;
        if (g_listen_preheld[vk]) { if (!down) g_listen_preheld[vk] = false; continue; }
        if (!prev && down) { *g_listen_key = vk; g_listen_key = nullptr; return; }
    }
}

static void draw_field(const Field& f) {
    switch (f.type) {
        case Fty::Toggle: {
            ImGuiWindow* win = ImGui::GetCurrentWindow();
            float w = ImGui::GetContentRegionAvail().x;
            float H = 30.f;
            ImVec2 pos = win->DC.CursorPos;
            ImRect bb(pos, {pos.x + w, pos.y + H});
            ImGui::ItemSize(bb, 0.f);
            ImGui::PushID(f.label);
            ImGuiID id = win->GetID("tg");
            ImGui::ItemAdd(bb, id);
            bool hovered = hit(bb);
            bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            bool* b = (bool*)f.addr;
            if (pressed) *b = !*b;
            ImDrawList* dl = win->DrawList;
            float th = anim_to(id ^ 0xFF01, hovered ? 1.f : 0.f, 18.f);
            if (th > 0.01f) {
                dl->AddRectFilled({bb.Min.x - 4.f, bb.Min.y - 2.f}, {bb.Max.x + 4.f, bb.Max.y + 2.f},
                                  with_alpha(C_BG_ELEV_H, th * 0.7f), 4.f);
            }
            dl->AddText({pos.x, pos.y + (H - ImGui::GetFontSize()) * 0.5f},
                        *b ? C_TEXT : C_TEXT_MUTE, f.label);
            draw_switch(dl, {bb.Max.x - 34.f, bb.Min.y + (H - 18.f) * 0.5f},
                        *b, hovered, id);
            ImGui::PopID();
            break;
        }
        case Fty::SliderF:
            void_slider_f(f.label, (float*)f.addr, f.fmin, f.fmax, f.step, f.fmt);
            break;
        case Fty::SliderI:
            void_slider_i(f.label, (int*)f.addr, (int)f.fmin, (int)f.fmax, (int)f.step);
            break;
        case Fty::Segment:
            void_segment(f.label, (int*)f.addr, f.opts, f.opt_count);
            break;
        case Fty::Section: {
            ImGui::Dummy({0, 10});
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            dl->AddRectFilled({p.x, p.y + 2.f}, {p.x + 3.f, p.y + ImGui::GetFontSize() + 1.f}, C_ACC, 1.f);
            if (g_font_small) ImGui::PushFont(g_font_small);
            ImVec2 ts = ImGui::CalcTextSize(f.label);
            dl->AddText({p.x + 10.f, p.y + 2.f}, C_TEXT, f.label);
            if (g_font_small) ImGui::PopFont();
            dl->AddLine({p.x + 12.f + ts.x + 8.f, p.y + ImGui::GetFontSize() * 0.55f + 2.f},
                        {p.x + w,                 p.y + ImGui::GetFontSize() * 0.55f + 2.f},
                        C_LINE_HAIR, 1.f);
            ImGui::Dummy({0, ImGui::GetFontSize() + 8.f});
            break;
        }
        case Fty::Key: {
            ImGuiWindow* win = ImGui::GetCurrentWindow();
            float w = ImGui::GetContentRegionAvail().x;
            float H = 34.f;
            ImVec2 pos = win->DC.CursorPos;
            ImRect bb(pos, {pos.x + w, pos.y + H});
            ImGui::ItemSize(bb, 0.f);
            ImGui::PushID(f.label);
            ImGuiID id = win->GetID("kb");
            ImGui::ItemAdd(bb, id);
            int* pk = (int*)f.addr;
            bool listening = (g_listen_key == pk);
            bool hovered   = hit(bb);
            bool press_l   = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            bool press_r   = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            if (press_l) { start_listen(pk); listening = true; }
            if (press_r && !listening) *pk = 0;

            ImDrawList* dl = win->DrawList;
            dl->AddText({pos.x, pos.y + (H - ImGui::GetFontSize()) * 0.5f}, C_TEXT_MUTE, f.label);

            const char* txt = listening ? "…press key" : vk_name(*pk);
            ImVec2 tz = ImGui::CalcTextSize(txt);
            float cw = tz.x + 22.f, ch = 24.f;
            ImRect r({bb.Max.x - cw, bb.Min.y + (H - ch) * 0.5f},
                     {bb.Max.x,      bb.Min.y + (H + ch) * 0.5f});
            float th = anim_to(id ^ 0xB00B, (hovered || listening) ? 1.f : 0.f, 20.f);

            float pulse = listening ? (0.5f + 0.5f * std::sin(uptime_seconds() * 6.f)) : 0.f;
            if (listening) {
                dl->AddRect({r.Min.x - 2, r.Min.y - 2}, {r.Max.x + 2, r.Max.y + 2},
                            with_alpha(C_ACC, 0.35f + 0.35f * pulse), 6.f, 0, 1.f);
            }
            ImU32 bg = listening ? C_ACC : blend(IM_COL32(20, 20, 20, 255), C_BG_ELEV_A, th);
            ImU32 fg = listening ? C_INK : blend(C_TEXT_MUTE, C_TEXT, th);
            dl->AddRectFilled(r.Min, r.Max, bg, 5.f);
            if (!listening) dl->AddRect(r.Min, r.Max, C_LINE_HAIR, 5.f, 0, 1.f);
            dl->AddText({r.Min.x + (cw - tz.x) * 0.5f,
                         r.Min.y + (ch - tz.y) * 0.5f}, fg, txt);
            ImGui::PopID();
            break;
        }
    }
}

static bool module_row(const char* name, bool* enabled, bool has_body) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGui::PushID(name);
    const float H = 44.f;
    const float pad_x = 14.f;
    ImVec2 pos = win->DC.CursorPos;
    float  w   = ImGui::GetContentRegionAvail().x;
    ImRect bb(pos, {pos.x + w, pos.y + H});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 6.f}}, 0.f);
    ImGuiID id = win->GetID("row");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return false; }

    bool hovered = hit(bb);
    bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool right   = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    bool config_only = (enabled == nullptr);
    bool on = config_only ? false : *enabled;
    if (!config_only && pressed) { *enabled = !on; on = *enabled; }

    ImDrawList* dl = win->DrawList;
    float th   = anim_to(id ^ 0x1111, hovered ? 1.f : 0.f, 20.f);
    float ton  = anim_to(id ^ 0x2222, on ? 1.f : 0.f, 18.f);

    ImU32 base_l = C_BG_ELEV;
    ImU32 base_r = IM_COL32(22, 22, 22, 255);
    if (th > 0.02f || ton > 0.02f) {
        ImU32 hover_l = blend(base_l, C_BG_ELEV_H, th);
        ImU32 hover_r = blend(base_r, C_BG_ELEV_A, th);
        dl->AddRectFilledMultiColor(bb.Min, bb.Max, hover_l, hover_r, hover_r, hover_l);
    } else {
        dl->AddRectFilledMultiColor(bb.Min, bb.Max, base_l, base_r, base_r, base_l);
    }
    dl->AddRect(bb.Min, bb.Max, blend(C_LINE_SOFT, C_LINE, th), 8.f, 0, 1.f);

    if (ton > 0.02f) {
        dl->AddRectFilledMultiColor({bb.Min.x, bb.Min.y + 6.f},
                                    {bb.Min.x + 3.f, bb.Max.y - 6.f},
                                    C_ACC_DEEP, C_ACC_DEEP, C_ACC, C_ACC);

        for (int i = 0; i < 5; i++) {
            float s = (i + 1) * 3.f;
            dl->AddRectFilled({bb.Min.x - s, bb.Min.y + 6.f - s * 0.5f},
                              {bb.Min.x + 3.f, bb.Max.y - 6.f + s * 0.5f},
                              with_alpha(C_ACC_GLOW_S, ton * (1.f - i / 5.f)),
                              3.f);
        }
    }

    if (g_font_medium) ImGui::PushFont(g_font_medium);
    ImVec2 name_pos{bb.Min.x + pad_x, bb.Min.y + (H - ImGui::GetFontSize()) * 0.5f - 1.f};
    dl->AddText(name_pos, blend(C_TEXT_MUTE, C_TEXT, std::max(th, ton)), name);
    if (g_font_medium) ImGui::PopFont();

    if (config_only) {
        const char* label = "Open";
        ImVec2 sz = ImGui::CalcTextSize(label);
        float cw = sz.x + 22.f, ch = 24.f;
        ImRect r({bb.Max.x - cw - 12.f, bb.Min.y + (H - ch) * 0.5f},
                 {bb.Max.x - 12.f,      bb.Min.y + (H + ch) * 0.5f});
        ImU32 bg = hovered ? C_ACC : IM_COL32(24, 24, 24, 255);
        ImU32 fg = hovered ? C_INK : C_TEXT_MUTE;
        dl->AddRectFilled(r.Min, r.Max, bg, 5.f);
        if (!hovered) dl->AddRect(r.Min, r.Max, C_LINE_HAIR, 5.f, 0, 1.f);
        dl->AddText({r.Min.x + (cw - sz.x) * 0.5f, r.Min.y + (ch - sz.y) * 0.5f}, fg, label);
    } else {
        if (has_body) {

            float gx = bb.Max.x - 58.f;
            float gy = bb.Min.y + H * 0.5f;
            ImU32 dc = blend(C_TEXT_DIM, C_TEXT_MUTE, th);
            dl->AddCircleFilled({gx - 6.f, gy}, 1.6f, dc);
            dl->AddCircleFilled({gx,       gy}, 1.6f, dc);
            dl->AddCircleFilled({gx + 6.f, gy}, 1.6f, dc);
        }
        draw_switch(dl, {bb.Max.x - 34.f - 12.f, bb.Min.y + (H - 18.f) * 0.5f}, on, hovered, id);
    }

    ImGui::PopID();
    if (config_only) return (pressed || right) && has_body;
    return right && has_body;
}

static void player_row(const PlayerInfo& p, uintptr_t local_hrp_prim) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return;
    ImGui::PushID((int)(size_t)p.player_ptr);

    const float H     = 52.f;
    const float pad_x = 14.f;
    ImVec2 pos = win->DC.CursorPos;
    float  w   = ImGui::GetContentRegionAvail().x;
    ImRect bb(pos, {pos.x + w, pos.y + H});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 6.f}}, 0.f);
    ImGuiID id = win->GetID("prow");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return; }

    bool hovered = hit(bb);
    ImDrawList* dl = win->DrawList;
    float th = anim_to(id, hovered ? 1.f : 0.f, 20.f);

    ImU32 base_l = C_BG_ELEV;
    ImU32 base_r = IM_COL32(22, 22, 22, 255);
    ImU32 hover_l = blend(base_l, C_BG_ELEV_H, th);
    ImU32 hover_r = blend(base_r, C_BG_ELEV_A, th);
    dl->AddRectFilledMultiColor(bb.Min, bb.Max, hover_l, hover_r, hover_r, hover_l);
    dl->AddRect(bb.Min, bb.Max, blend(C_LINE_SOFT, C_LINE, th), 8.f, 0, 1.f);

    ImU32 dot = IM_COL32(100, 100, 100, 255);
    if      (p.role == 1) dot = C_BAD;
    else if (p.role == 2) dot = C_ACC;
    ImVec2 dot_c{bb.Min.x + pad_x, bb.Min.y + H * 0.5f};
    dl->AddCircleFilled(dot_c, 4.f, dot);
    dl->AddCircle(dot_c, 4.f, with_alpha(dot, 0.55f), 0, 1.f);

    if (p.role == 2) dl->AddCircle(dot_c, 7.f, with_alpha(C_ACC, 0.4f), 0, 1.f);

    if (g_font_medium) ImGui::PushFont(g_font_medium);
    ImU32 name_col = (p.health <= 0.f) ? C_TEXT_DIM : C_TEXT;
    dl->AddText({bb.Min.x + pad_x + 14.f, bb.Min.y + 8.f}, name_col, p.name.c_str());
    if (g_font_medium) ImGui::PopFont();

    char sub[64];
    Vec3 my{};
    if (local_hrp_prim) my = rpm<Vec3>(local_hrp_prim + offsets::Primitive::Position);
    float dist = (p.position - my).length();
    if (p.health <= 0.f) std::snprintf(sub, sizeof(sub), "dead   %.0f studs", dist);
    else                 std::snprintf(sub, sizeof(sub), "hp %.0f / %.0f   %.0f studs",
                                       p.health, p.max_health, dist);
    if (g_font_small) ImGui::PushFont(g_font_small);
    dl->AddText({bb.Min.x + pad_x + 14.f, bb.Min.y + 28.f}, C_TEXT_MUTE, sub);
    if (g_font_small) ImGui::PopFont();

    if (p.max_health > 0.f && p.health > 0.f) {
        float hp_pct = p.health / p.max_health;
        if (hp_pct < 0) hp_pct = 0; if (hp_pct > 1) hp_pct = 1;
        float bx = bb.Min.x + pad_x + 14.f;
        float by = bb.Max.y - 10.f;
        float bw2 = 180.f;
        ImU32 hpcol = hp_pct > 0.6f ? C_OK :
                      hp_pct > 0.3f ? IM_COL32(240, 200, 90, 255) : C_BAD;
        dl->AddRectFilled({bx, by}, {bx + bw2, by + 3.f}, IM_COL32(20, 20, 20, 255), 1.5f);
        dl->AddRectFilled({bx, by}, {bx + bw2 * hp_pct, by + 3.f}, hpcol, 1.5f);
    }

    auto chip = [&](const char* label, float right_x, bool active) -> bool {
        ImVec2 sz = ImGui::CalcTextSize(label);
        float bw = sz.x + 22.f, bh = 26.f;
        ImRect r({right_x - bw, bb.Min.y + (H - bh) * 0.5f},
                 {right_x,      bb.Min.y + (H + bh) * 0.5f});
        bool hov  = hit(r);
        bool pres = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        ImGuiID cid = win->GetID(label);
        float ct = anim_to(cid, (hov || active) ? 1.f : 0.f, 20.f);
        ImU32 bg = active ? C_ACC : blend(IM_COL32(24, 24, 24, 255), C_BG_ELEV_A, ct);
        ImU32 fg = active ? C_INK : blend(C_TEXT_MUTE, C_TEXT, ct);
        dl->AddRectFilled(r.Min, r.Max, bg, 5.f);
        if (!active) dl->AddRect(r.Min, r.Max, C_LINE_HAIR, 5.f, 0, 1.f);
        dl->AddText({r.Min.x + (r.GetWidth() - sz.x) * 0.5f,
                     r.Min.y + (r.GetHeight() - sz.y) * 0.5f}, fg, label);
        return pres;
    };

    bool locked_here   = tp::g_lock.load() &&
                         tp::g_lock_target.load() ==
                             (p.hrp_ptr ? rpm<uintptr_t>(p.hrp_ptr + offsets::BasePart::Primitive) : 0);
    bool spectate_here = tp::g_spectate.load() &&
                         tp::g_spectate_hum.load() == p.humanoid_ptr;

    float rx = bb.Max.x - 12.f;
    if (chip(locked_here ? "Unlock" : "Lock", rx, locked_here)) {
        if (locked_here) tp::stop_lock();
        else if (p.hrp_ptr) {
            uintptr_t prim = rpm<uintptr_t>(p.hrp_ptr + offsets::BasePart::Primitive);
            if (prim) { tp::start_lock(prim); esp::notify("Locked to " + p.name); }
        }
    }
    ImVec2 sz1 = ImGui::CalcTextSize(locked_here ? "Unlock" : "Lock");
    rx -= sz1.x + 22.f + 6.f;

    if (chip(spectate_here ? "Unview" : "View", rx, spectate_here)) {
        if (spectate_here) tp::stop_spectate();
        else if (p.humanoid_ptr) {
            if (tp::start_spectate(p.humanoid_ptr)) esp::notify("Viewing " + p.name);
        }
    }
    ImVec2 sz2 = ImGui::CalcTextSize(spectate_here ? "Unview" : "View");
    rx -= sz2.x + 22.f + 6.f;

    if (chip("TP", rx, false)) {
        if (p.valid && p.health > 0.f) {
            tp::request_teleport(Vec3{p.position.x, p.position.y + 3.f, p.position.z}, 60);
            esp::notify("TP to " + p.name);
        }
    }

    ImGui::PopID();
}

namespace prof_fs = std::filesystem;

static std::string profiles_dir() {
    return mem::exe_dir() + "\\profiles";
}
static void ensure_profiles_dir() {
    std::error_code ec;
    prof_fs::create_directories(profiles_dir(), ec);
}
static std::string profile_path(const std::string& name) {
    return profiles_dir() + "\\" + name + ".cfg";
}
static std::string sanitize_profile_name(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ')
            out += c;
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back()  == ' ') out.pop_back();
    return out;
}
struct ProfileEntry {
    std::string name;
    uint64_t    size = 0;
    std::time_t mtime = 0;
};
static std::vector<ProfileEntry> list_profiles() {
    ensure_profiles_dir();
    std::vector<ProfileEntry> out;
    std::error_code ec;
    for (auto& e : prof_fs::directory_iterator(profiles_dir(), ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        auto p = e.path();
        if (p.extension() != ".cfg") continue;
        ProfileEntry pe;
        pe.name  = p.stem().string();
        pe.size  = (uint64_t)e.file_size(ec);
        auto ftime = e.last_write_time(ec);
        auto sctp  = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
        pe.mtime = std::chrono::system_clock::to_time_t(sctp);
        out.push_back(pe);
    }
    std::sort(out.begin(), out.end(), [](const ProfileEntry& a, const ProfileEntry& b) {
        return a.mtime > b.mtime;
    });
    return out;
}
static std::string humanize_age(std::time_t t) {
    if (t == 0) return "unknown";
    std::time_t now = std::time(nullptr);
    long long dt = (long long)std::difftime(now, t);
    if (dt < 60)    return "just now";
    if (dt < 3600)  { char b[32]; std::snprintf(b, sizeof(b), "%lldm ago",  dt / 60);      return b; }
    if (dt < 86400) { char b[32]; std::snprintf(b, sizeof(b), "%lldh ago",  dt / 3600);    return b; }
    { char b[32]; std::snprintf(b, sizeof(b), "%lldd ago", dt / 86400); return b; }
}

static std::string g_profiles_active;
static char        g_prof_new_name_buf[64] = {};
static std::string g_prof_toast;
static float       g_prof_toast_ttl = 0.f;
static std::string g_prof_delete_confirm;

static void prof_toast(const std::string& s) {
    g_prof_toast = s;
    g_prof_toast_ttl = 2.4f;
}

static void draw_profiles_pane(ImVec2 cont_min, ImVec2 cont_max) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 body_min = cont_min;
    ImVec2 body_max = cont_max;

    ImVec2 sp = body_min;
    float  sw = body_max.x - body_min.x;
    float  sh = 46.f;
    dl->AddRectFilledMultiColor(sp, {sp.x + sw, sp.y + sh},
        C_BG_ELEV, IM_COL32(22, 22, 22, 255),
        IM_COL32(22, 22, 22, 255), C_BG_ELEV);
    dl->AddRect(sp, {sp.x + sw, sp.y + sh}, C_LINE, 8.f, 0, 1.f);

    float in_x = sp.x + 14.f;
    float in_y = sp.y + (sh - 22.f) * 0.5f;
    float in_w = sw - 140.f;
    dl->AddRectFilled({in_x, in_y}, {in_x + in_w, in_y + 22.f}, IM_COL32(10, 10, 10, 255), 6.f);
    dl->AddRect({in_x, in_y}, {in_x + in_w, in_y + 22.f}, C_LINE_HAIR, 6.f, 0, 1.f);

    ImGui::SetCursorScreenPos({in_x + 8.f, in_y + (22.f - ImGui::GetFontSize()) * 0.5f - 1.f});
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,         IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushItemWidth(in_w - 16.f);
    bool enter_save = ImGui::InputTextWithHint("##newprof", "new profile name…",
        g_prof_new_name_buf, IM_ARRAYSIZE(g_prof_new_name_buf),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    ImVec2 btn_min{sp.x + sw - 116.f, sp.y + (sh - 26.f) * 0.5f};
    ImVec2 btn_max{sp.x + sw - 14.f,  sp.y + (sh + 26.f) * 0.5f};
    ImRect btn_r(btn_min, btn_max);
    bool bhov = hit(btn_r);
    bool bpress = bhov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    std::string name = sanitize_profile_name(g_prof_new_name_buf);
    bool can_save = !name.empty();
    ImGuiID sid = ImGui::GetCurrentWindow()->GetID("savebtn");
    float st = anim_to(sid, (bhov && can_save) ? 1.f : 0.f, 18.f);
    ImU32 bg_l = can_save ? blend(C_BG_ELEV_A, C_ACC_DEEP, st) : IM_COL32(20, 20, 20, 255);
    ImU32 bg_r = can_save ? blend(C_BG_ELEV_A, C_ACC,      st) : IM_COL32(20, 20, 20, 255);
    ImU32 fg   = can_save ? blend(C_TEXT,      C_INK,      st) : C_TEXT_DIM;
    ImU32 bd   = can_save ? blend(C_ACC_DEEP,  C_ACC,      st) : C_LINE;
    dl->AddRectFilledMultiColor(btn_min, btn_max, bg_l, bg_r, bg_r, bg_l);
    dl->AddRect(btn_min, btn_max, bd, 6.f, 0, 1.f);

    float px = btn_min.x + 14.f, py = btn_min.y + 13.f;
    dl->AddLine({px - 4.f, py}, {px + 4.f, py}, fg, 1.6f);
    dl->AddLine({px, py - 4.f}, {px, py + 4.f}, fg, 1.6f);
    ImVec2 ts = ImGui::CalcTextSize("Save current");
    dl->AddText({btn_min.x + 26.f, btn_min.y + (26.f - ts.y) * 0.5f}, fg, "Save current");

    if ((bpress || enter_save) && can_save) {
        if (esp::save_config(profile_path(name).c_str())) {
            g_profiles_active = name;
            prof_toast("Saved profile \"" + name + "\"");
            g_prof_new_name_buf[0] = 0;
        } else {
            prof_toast("Failed to save profile");
        }
    }

    ImVec2 list_pos{body_min.x, sp.y + sh + 12.f};
    ImVec2 list_size(body_max.x - list_pos.x, body_max.y - list_pos.y);
    ImGui::SetCursorScreenPos(list_pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##proflist", list_size, false,
        ImGuiWindowFlags_NoBackground);

    auto profs = list_profiles();
    if (profs.empty()) {
        ImGui::Dummy({0, 32});
        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImDrawList* d2 = ImGui::GetWindowDrawList();
        d2->AddText({cp.x + 4.f, cp.y}, C_TEXT_DIM,
            "no profiles yet — type a name above and hit Save current.");
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 8});
        for (const auto& pe : profs) {
            ImGui::PushID(pe.name.c_str());
            ImGuiWindow* win = ImGui::GetCurrentWindow();
            const float H     = 62.f;
            const float pad_x = 14.f;
            ImVec2 pos = win->DC.CursorPos;
            float  w   = ImGui::GetContentRegionAvail().x;
            ImRect bb(pos, {pos.x + w, pos.y + H});
            ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 6.f}}, 0.f);
            ImGuiID id = win->GetID("prow");
            if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); continue; }

            bool hovered = hit(bb);
            bool active  = (g_profiles_active == pe.name);
            float th  = anim_to(id ^ 0xF00D, hovered ? 1.f : 0.f, 20.f);
            float tac = anim_to(id ^ 0xACAC, active  ? 1.f : 0.f, 18.f);

            ImDrawList* d2 = win->DrawList;
            ImU32 base_l = C_BG_ELEV;
            ImU32 base_r = IM_COL32(22, 22, 22, 255);
            ImU32 hover_l = blend(base_l, C_BG_ELEV_H, th);
            ImU32 hover_r = blend(base_r, C_BG_ELEV_A, th);
            d2->AddRectFilledMultiColor(bb.Min, bb.Max, hover_l, hover_r, hover_r, hover_l);
            d2->AddRect(bb.Min, bb.Max, blend(C_LINE_SOFT, C_LINE, std::max(th, tac)), 8.f, 0, 1.f);

            if (tac > 0.02f) {
                d2->AddRectFilledMultiColor({bb.Min.x, bb.Min.y + 8.f},
                                            {bb.Min.x + 3.f, bb.Max.y - 8.f},
                                            C_ACC_DEEP, C_ACC_DEEP, C_ACC, C_ACC);
                for (int i = 0; i < 5; i++) {
                    float s = (i + 1) * 3.f;
                    d2->AddRectFilled({bb.Min.x - s, bb.Min.y + 8.f - s * 0.5f},
                                      {bb.Min.x + 3.f, bb.Max.y - 8.f + s * 0.5f},
                                      with_alpha(C_ACC_GLOW_S, tac * (1.f - i / 5.f)),
                                      3.f);
                }
            }

            if (g_font_medium) ImGui::PushFont(g_font_medium);
            d2->AddText({bb.Min.x + pad_x, bb.Min.y + 10.f},
                        blend(C_TEXT_MUTE, C_TEXT, std::max(th, tac)), pe.name.c_str());
            if (g_font_medium) ImGui::PopFont();

            char meta[96];
            std::snprintf(meta, sizeof(meta), "%llu bytes  ·  %s",
                          (unsigned long long)pe.size, humanize_age(pe.mtime).c_str());
            if (g_font_small) ImGui::PushFont(g_font_small);
            d2->AddText({bb.Min.x + pad_x, bb.Min.y + 34.f}, C_TEXT_MUTE, meta);
            if (g_font_small) ImGui::PopFont();

            auto btn = [&](const char* label, float right_x, bool danger) -> std::pair<bool, float> {
                ImVec2 sz = ImGui::CalcTextSize(label);
                float bw = sz.x + 26.f, bh = 28.f;
                ImRect r({right_x - bw, bb.Min.y + (H - bh) * 0.5f},
                         {right_x,      bb.Min.y + (H + bh) * 0.5f});
                bool hov  = hit(r);
                bool pres = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                ImGuiID bid2 = win->GetID(label);
                float bt = anim_to(bid2, hov ? 1.f : 0.f, 20.f);
                ImU32 accent   = danger ? C_BAD : C_ACC;
                ImU32 accent_d = danger ? IM_COL32(200, 60, 72, 255) : C_ACC_DEEP;
                ImU32 bgl = blend(IM_COL32(22, 22, 22, 255), accent_d, bt);
                ImU32 bgr = blend(IM_COL32(22, 22, 22, 255), accent,   bt);
                ImU32 fgc = blend(C_TEXT_MUTE, C_INK, bt);
                ImU32 bdc = blend(C_LINE_HAIR, accent, bt);
                d2->AddRectFilledMultiColor(r.Min, r.Max, bgl, bgr, bgr, bgl);
                d2->AddRect(r.Min, r.Max, bdc, 6.f, 0, 1.f);
                d2->AddText({r.Min.x + (r.GetWidth() - sz.x) * 0.5f,
                             r.Min.y + (r.GetHeight() - sz.y) * 0.5f}, fgc, label);
                return {pres, bw};
            };

            float rx = bb.Max.x - 14.f;
            bool  pending_delete = (g_prof_delete_confirm == pe.name);
            auto [del_pressed, del_w] = btn(pending_delete ? "Confirm?" : "Delete", rx, true);
            if (del_pressed) {
                if (pending_delete) {
                    std::error_code ec;
                    prof_fs::remove(profile_path(pe.name), ec);
                    if (ec) prof_toast("Failed to delete " + pe.name);
                    else    prof_toast("Deleted \"" + pe.name + "\"");
                    if (g_profiles_active == pe.name) g_profiles_active.clear();
                    g_prof_delete_confirm.clear();
                } else {
                    g_prof_delete_confirm = pe.name;
                }
            }
            rx -= del_w + 6.f;
            auto [save_pressed, save_w] = btn("Overwrite", rx, false);
            if (save_pressed) {
                if (esp::save_config(profile_path(pe.name).c_str())) {
                    g_profiles_active = pe.name;
                    prof_toast("Overwrote \"" + pe.name + "\"");
                } else prof_toast("Failed to save " + pe.name);
                g_prof_delete_confirm.clear();
            }
            rx -= save_w + 6.f;
            auto [load_pressed, load_w] = btn("Load", rx, false);
            if (load_pressed) {
                if (esp::load_config(profile_path(pe.name).c_str())) {
                    g_profiles_active = pe.name;
                    prof_toast("Loaded \"" + pe.name + "\"");
                } else prof_toast("Failed to load " + pe.name);
                g_prof_delete_confirm.clear();
            }
            (void)load_w;

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !del_pressed && g_prof_delete_confirm == pe.name) {
                g_prof_delete_confirm.clear();
            }

            ImGui::PopID();
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (g_prof_toast_ttl > 0.f) {
        g_prof_toast_ttl -= ImGui::GetIO().DeltaTime;
        float a = g_prof_toast_ttl > 1.f ? 1.f : g_prof_toast_ttl;
        ImVec2 tsz = ImGui::CalcTextSize(g_prof_toast.c_str());
        float pw = tsz.x + 28.f, ph = 28.f;
        float tx = body_max.x - pw - 4.f;
        float ty = body_max.y - ph - 4.f;
        dl->AddRectFilled({tx, ty}, {tx + pw, ty + ph}, with_alpha(C_BG_ELEV_A, a * 0.92f), 8.f);
        dl->AddRect({tx, ty}, {tx + pw, ty + ph}, with_alpha(C_ACC, a * 0.6f), 8.f, 0, 1.f);
        dl->AddCircleFilled({tx + 12.f, ty + ph * 0.5f}, 3.f, with_alpha(C_ACC, a));
        dl->AddText({tx + 22.f, ty + (ph - tsz.y) * 0.5f}, with_alpha(C_TEXT, a),
                    g_prof_toast.c_str());
    }
}

static void gp_card(ImDrawList* dl, ImVec2 min, ImVec2 max) {
    dl->AddRectFilledMultiColor(min, max,
        C_BG_ELEV, IM_COL32(22, 22, 22, 255),
        IM_COL32(22, 22, 22, 255), C_BG_ELEV);
    dl->AddRect(min, max, C_LINE, 10.f, 0, 1.f);
}
static bool gp_chip_btn(ImDrawList* dl, ImVec2 min, ImVec2 max,
                        const char* label, bool on, bool accent) {
    ImRect r(min, max);
    bool hov  = hit(r);
    bool pres = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    ImGuiID id = ImGui::GetCurrentWindow()->GetID(label);
    float t   = anim_to(id, on ? 1.f : 0.f, 20.f);
    float th  = anim_to(id ^ 0xF00, hov ? 1.f : 0.f, 18.f);
    ImU32 bg_l, bg_r, fg;
    if (on) {
        bg_l = C_ACC_DEEP; bg_r = C_ACC; fg = C_INK;
    } else {
        bg_l = blend(IM_COL32(22, 22, 22, 255), C_BG_ELEV_A, th);
        bg_r = bg_l;
        fg   = blend(C_TEXT_MUTE, C_TEXT, th);
    }
    dl->AddRectFilledMultiColor(min, max, bg_l, bg_r, bg_r, bg_l);
    dl->AddRect(min, max, blend(C_LINE_HAIR, accent ? C_ACC : C_LINE, std::max(t, th)),
                6.f, 0, 1.f);
    ImVec2 sz = ImGui::CalcTextSize(label);
    dl->AddText({min.x + (r.GetWidth() - sz.x) * 0.5f,
                 min.y + (r.GetHeight() - sz.y) * 0.5f}, fg, label);
    return pres;
}
static void gp_toggle_row(ImDrawList* dl, ImVec2 pos, float w, float H,
                          const char* label, bool* on, const char* hint = nullptr) {
    ImRect bb(pos, {pos.x + w, pos.y + H});
    ImGuiID id = ImGui::GetCurrentWindow()->GetID(label);
    bool hovered = hit(bb);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) *on = !*on;
    float th = anim_to(id ^ 0x1234, hovered ? 1.f : 0.f, 20.f);
    if (th > 0.02f) {
        dl->AddRectFilled(bb.Min, bb.Max, with_alpha(C_BG_ELEV_H, th * 0.7f), 6.f);
    }
    if (g_font_medium) ImGui::PushFont(g_font_medium);
    dl->AddText({pos.x + 4.f, pos.y + 4.f},
                *on ? C_TEXT : C_TEXT_MUTE, label);
    if (g_font_medium) ImGui::PopFont();
    if (hint && g_font_small) {
        ImGui::PushFont(g_font_small);
        dl->AddText({pos.x + 4.f, pos.y + 26.f}, C_TEXT_DIM, hint);
        ImGui::PopFont();
    }
    draw_switch(dl, {bb.Max.x - 34.f - 4.f, pos.y + (H - 18.f) * 0.5f}, *on, hovered, id);
    ImGui::ItemSize(bb, 0.f);
    ImGui::ItemAdd(bb, id);
}

static void draw_game_pane(ImVec2 cont_min, ImVec2 cont_max) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    games::Id g = games::active();

    ImVec2 hp = cont_min;
    float  hw = cont_max.x - cont_min.x;
    float  hh = 78.f;
    gp_card(dl, hp, {hp.x + hw, hp.y + hh});

    ImVec2 ic{hp.x + 34.f, hp.y + hh * 0.5f};
    int icon_ix =
        (g == games::Id::Arsenal       ) ? 0 :
        (g == games::Id::Flick         ) ? 4 :
        (g == games::Id::DaHood        ) ? 6 :
        (g == games::Id::BadBusiness   ) ? 0 :
        (g == games::Id::PhantomForces ) ? 0 :
        (g == games::Id::BigPaintball  ) ? 0 :
        (g == games::Id::CounterBlox   ) ? 0 :
        (g == games::Id::KAT           ) ? 4 :
        (g == games::Id::Assassin      ) ? 4 :
        (g == games::Id::StrongestBG   ) ? 6 :
        (g == games::Id::BloxFruits    ) ? 8 :
        (g == games::Id::Rivals        ) ? 6 :
                                           8;
    draw_icon(dl, icon_ix, ic, 18.f, C_ACC, 1.9f);

    if (g_font_header) ImGui::PushFont(g_font_header);
    dl->AddText({hp.x + 68.f, hp.y + 14.f}, C_TEXT, games::name_of(g));
    if (g_font_header) ImGui::PopFont();
    if (g_font_small) ImGui::PushFont(g_font_small);
    dl->AddText({hp.x + 68.f, hp.y + 46.f}, C_TEXT_MUTE, games::tagline_of(g));
    if (g_font_small) ImGui::PopFont();

    if (g_font_small) ImGui::PushFont(g_font_small);
    const char* lockedTxt = "locked at inject";
    ImVec2 lz = ImGui::CalcTextSize(lockedTxt);
    float pw = lz.x + 34.f, ph = 22.f;
    ImVec2 pmin{hp.x + hw - pw - 14.f, hp.y + 14.f};
    ImVec2 pmax{pmin.x + pw, pmin.y + ph};
    dl->AddRectFilled(pmin, pmax, IM_COL32(16, 16, 16, 255), 11.f);
    dl->AddRect(pmin, pmax, C_LINE_HAIR, 11.f, 0, 1.f);
    dl->AddCircleFilled({pmin.x + 12.f, pmin.y + ph * 0.5f}, 3.f, C_ACC);
    dl->AddText({pmin.x + 22.f, pmin.y + (ph - lz.y) * 0.5f}, C_TEXT_MUTE, lockedTxt);
    if (g_font_small) ImGui::PopFont();

    ImVec2 body_min{cont_min.x, hp.y + hh + 14.f};
    ImVec2 body_max = cont_max;

    if (g == games::Id::Universal) {

        ImVec2 c1_min = body_min;
        ImVec2 c1_max = {body_max.x, body_min.y + 232.f};
        gp_card(dl, c1_min, c1_max);
        if (g_font_medium) ImGui::PushFont(g_font_medium);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 12.f}, C_TEXT, "Silent aim");
        if (g_font_medium) ImGui::PopFont();
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 34.f}, C_TEXT_MUTE,
                    "Writes GuiService.GuiInset (+0x330) so any ScreenPointToRay");
        dl->AddText({c1_min.x + 14.f, c1_min.y + 48.f}, C_TEXT_MUTE,
                    "the game fires from the cursor lands on the target in FOV.");
        if (g_font_small) ImGui::PopFont();

        float row_y = c1_min.y + 62.f;
        float row_h = 42.f;
        float row_w = (c1_max.x - c1_min.x) - 28.f;

        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Silent aim", &esp::cfg.silent_aim,
                      "Passive — no keybind. Shots land on target in FOV.");
        row_y += row_h;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Skip knocked (K.O) targets", &esp::cfg.silent_knocked_check,
                      "Skips Character.BodyEffects.K.O flagged players (Da Hood-like).");
        row_y += row_h;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Team check", &esp::cfg.team_check,
                      "Skip same-team players.");

        ImVec2 c2_min = {body_min.x, c1_max.y + 12.f};
        ImVec2 c2_max = {body_max.x, c2_min.y + 148.f};
        gp_card(dl, c2_min, c2_max);
        ImGui::SetCursorScreenPos({c2_min.x + 14.f, c2_min.y + 12.f});
        ImGui::PushItemWidth(c2_max.x - c2_min.x - 28.f);
        void_slider_f("Max distance", &esp::cfg.aim_max_dist, 0.f, 5000.f, 10.f, "%.0f");
        ImGui::PopItemWidth();
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c2_min.x + 14.f, c2_max.y - 28.f}, C_TEXT_MUTE,
                    "Visibility check:");
        if (g_font_small) ImGui::PopFont();
        gp_toggle_row(dl, {c2_min.x + 130.f, c2_max.y - 36.f}, row_w - 130.f, 26.f,
                      "", &esp::cfg.aim_visibility_check,
                      "Skip targets behind terrain.");

        ImVec2 c3_min = {body_min.x, c2_max.y + 12.f};
        ImVec2 c3_max = {body_max.x, c3_min.y + 74.f};
        gp_card(dl, c3_min, c3_max);
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c3_min.x + 14.f, c3_min.y + 10.f}, C_TEXT_MUTE, "Aim part");
        if (g_font_small) ImGui::PopFont();
        const char* PARTS[3] = {"Head", "Torso", "Nearest to cursor"};
        float seg_h = 32.f;
        float seg_y = c3_min.y + 32.f;
        float seg_w = ((c3_max.x - c3_min.x) - 28.f - 8.f) / 3.f;
        for (int i = 0; i < 3; i++) {
            ImVec2 mn{c3_min.x + 14.f + i * (seg_w + 4.f), seg_y};
            ImVec2 mx{mn.x + seg_w,                        seg_y + seg_h};
            if (gp_chip_btn(dl, mn, mx, PARTS[i],
                            esp::cfg.silent_aim_part == i, true)) {
                esp::cfg.silent_aim_part = i;
            }
        }

        ImVec2 c4_min = {body_min.x, c3_max.y + 12.f};
        ImVec2 c4_max = {body_max.x, c4_min.y + 74.f};
        gp_card(dl, c4_min, c4_max);
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c4_min.x + 14.f, c4_min.y + 10.f}, C_TEXT_MUTE,
            "GuiInset offset (auto-fetched from the offset feed if listed as");
        dl->AddText({c4_min.x + 14.f, c4_min.y + 26.f}, C_TEXT_MUTE,
            "GuiService::GuiInset). Hardcoded fallback: 0x330 (Vector4).");
        char cur[64];
        std::snprintf(cur, sizeof(cur), "current: 0x%llX",
                      (unsigned long long)offsets::GuiService::GuiInset);
        dl->AddText({c4_min.x + 14.f, c4_min.y + 46.f}, C_ACC, cur);
        if (g_font_small) ImGui::PopFont();

        return;
    }

    if (g == games::Id::Flick) {

        ImVec2 c1_min = body_min;
        ImVec2 c1_max = {body_max.x, body_min.y + 168.f};
        gp_card(dl, c1_min, c1_max);
        if (g_font_medium) ImGui::PushFont(g_font_medium);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 12.f}, C_TEXT, "What's live");
        if (g_font_medium) ImGui::PopFont();
        if (g_font_small) ImGui::PushFont(g_font_small);
        const char* live[] = {
            "\xE2\x80\xA2 FOV circle (Flick default: 120 px)",
            "\xE2\x80\xA2 ESP Box + Tracers + HP color",
            "\xE2\x80\xA2 Aim assist tuned to head bone, visibility check on",
            "\xE2\x80\xA2 All base movement / camera / world tabs work as usual",
        };
        for (int i = 0; i < 4; i++)
            dl->AddText({c1_min.x + 18.f, c1_min.y + 40.f + i * 18.f},
                        C_TEXT_MUTE, live[i]);
        if (g_font_small) ImGui::PopFont();

        ImVec2 btn_min{c1_min.x + 14.f,   c1_max.y - 42.f};
        ImVec2 btn_max{c1_max.x - 14.f,   c1_max.y - 12.f};
        if (gp_chip_btn(dl, btn_min, btn_max, "Apply Flick aim/ESP defaults", false, true)) {
            games::flick_apply_defaults();
            g_prof_toast    = "Applied Flick defaults";
            g_prof_toast_ttl = 2.2f;
        }

        ImVec2 c2_min = {body_min.x, c1_max.y + 12.f};
        ImVec2 c2_max = {body_max.x, c2_min.y + 152.f};
        gp_card(dl, c2_min, c2_max);
        if (g_font_medium) ImGui::PushFont(g_font_medium);
        dl->AddText({c2_min.x + 14.f, c2_min.y + 12.f}, C_TEXT,
                    "Not portable externally");
        if (g_font_medium) ImGui::PopFont();
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c2_min.x + 14.f, c2_min.y + 34.f}, C_TEXT_MUTE,
            "Flick's offensive mods all hinge on hooking BulletHandler.Fire");
        dl->AddText({c2_min.x + 14.f, c2_min.y + 50.f}, C_TEXT_MUTE,
            "which needs an in-process Lua executor — externally impossible.");
        const char* nope[] = {
            "\xE2\x80\xA2 Silent Aim  (per-shot p6.Direction rewrite)",
            "\xE2\x80\xA2 Fast Bullets  (p6.Force / p6.Gravity)",
            "\xE2\x80\xA2 Spoof anti-detect  (p6.Misc restore before ORIG)",
            "\xE2\x80\xA2 Full Auto / No Recoil  (Tool:SetAttribute)",
        };
        for (int i = 0; i < 4; i++)
            dl->AddText({c2_min.x + 18.f, c2_min.y + 74.f + i * 16.f},
                        C_TEXT_DIM, nope[i]);
        if (g_font_small) ImGui::PopFont();

        ImVec2 c3_min = {body_min.x, c2_max.y + 12.f};
        ImVec2 c3_max = {body_max.x, c3_min.y + 62.f};
        gp_card(dl, c3_min, c3_max);
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c3_min.x + 14.f, c3_min.y + 10.f}, C_TEXT_MUTE,
            "If auto-detect doesn't recognize your Flick variant, open the");
        dl->AddText({c3_min.x + 14.f, c3_min.y + 26.f}, C_TEXT_MUTE,
            "debug console (F10) — the PlaceId is logged on inject.");
        dl->AddText({c3_min.x + 14.f, c3_min.y + 42.f}, C_TEXT_DIM,
            "Send it and I'll wire the ID into detect_from_place().");
        if (g_font_small) ImGui::PopFont();

        return;
    }

    if (g == games::Id::DaHood) {

        ImVec2 c1_min = body_min;
        ImVec2 c1_max = {body_max.x, body_min.y + 232.f};
        gp_card(dl, c1_min, c1_max);
        if (g_font_medium) ImGui::PushFont(g_font_medium);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 12.f}, C_TEXT, "Silent aim");
        if (g_font_medium) ImGui::PopFont();
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 34.f}, C_TEXT_MUTE,
                    "Writes MouseService.MousePosition. Third-person only —");
        dl->AddText({c1_min.x + 14.f, c1_min.y + 48.f}, C_BAD,
                    "first-person shoots along Camera.LookVector, not mouse.");
        if (g_font_small) ImGui::PopFont();

        float row_y = c1_min.y + 62.f;
        float row_h = 42.f;
        float row_w = (c1_max.x - c1_min.x) - 28.f;

        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Silent aim", &esp::cfg.silent_aim,
                      "Passive — no keybind. Shoots go on target in FOV.");
        row_y += row_h;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Skip knocked (K.O) targets", &esp::cfg.silent_knocked_check,
                      "Walks Character.BodyEffects.K.O — skips downed players.");
        row_y += row_h;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Team check", &esp::cfg.team_check,
                      "Skip same-team players.");

        ImVec2 c2_min = {body_min.x, c1_max.y + 12.f};
        ImVec2 c2_max = {body_max.x, c2_min.y + 120.f};
        gp_card(dl, c2_min, c2_max);
        ImGui::SetCursorScreenPos({c2_min.x + 14.f, c2_min.y + 12.f});
        ImGui::PushItemWidth(c2_max.x - c2_min.x - 28.f);
        void_slider_f("Max distance", &esp::cfg.aim_max_dist, 0.f, 5000.f, 10.f, "%.0f");
        ImGui::PopItemWidth();

        ImVec2 c3_min = {body_min.x, c2_max.y + 12.f};
        ImVec2 c3_max = {body_max.x, c3_min.y + 74.f};
        gp_card(dl, c3_min, c3_max);
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c3_min.x + 14.f, c3_min.y + 10.f}, C_TEXT_MUTE, "Aim part");
        if (g_font_small) ImGui::PopFont();
        const char* PARTS[3] = {"Head", "Torso", "Nearest to cursor"};
        float seg_h = 32.f;
        float seg_y = c3_min.y + 32.f;
        float seg_w = ((c3_max.x - c3_min.x) - 28.f - 8.f) / 3.f;
        for (int i = 0; i < 3; i++) {
            ImVec2 mn{c3_min.x + 14.f + i * (seg_w + 4.f), seg_y};
            ImVec2 mx{mn.x + seg_w,                        seg_y + seg_h};
            if (gp_chip_btn(dl, mn, mx, PARTS[i],
                            esp::cfg.silent_aim_part == i, true)) {
                esp::cfg.silent_aim_part = i;
            }
        }

        ImVec2 c4_min = {body_min.x, c3_max.y + 12.f};
        ImVec2 c4_max = {body_max.x, c4_min.y + 44.f};
        if (gp_chip_btn(dl, c4_min, c4_max,
                        "Apply Da Hood defaults", false, true)) {
            games::dahood_apply_defaults();
            g_prof_toast    = "Applied Da Hood defaults";
            g_prof_toast_ttl = 2.2f;
        }
        return;
    }

    if (g == games::Id::Rivals) {
        ImVec2 c1_min = body_min;
        ImVec2 c1_max = {body_max.x, body_min.y + 232.f};
        gp_card(dl, c1_min, c1_max);
        if (g_font_medium) ImGui::PushFont(g_font_medium);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 12.f}, C_TEXT, "Silent aim");
        if (g_font_medium) ImGui::PopFont();
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 34.f}, C_TEXT_MUTE,
            "Rivals reads Camera.Viewport (Vector2int16) to build the fire ray.");
        dl->AddText({c1_min.x + 14.f, c1_min.y + 48.f}, C_TEXT_MUTE,
            "Writing 2*(screen - target) makes any shot land on the target.");
        if (g_font_small) ImGui::PopFont();

        float row_y = c1_min.y + 62.f;
        float row_h = 42.f;
        float row_w = (c1_max.x - c1_min.x) - 28.f;

        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Silent aim", &esp::cfg.silent_aim,
                      "Passive — no keybind. Shots land on target in FOV.");
        row_y += row_h;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Team check", &esp::cfg.team_check,
                      "Skip same-team players.");
        row_y += row_h;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Visibility check", &esp::cfg.aim_visibility_check,
                      "Skip targets behind terrain.");

        ImVec2 c2_min = {body_min.x, c1_max.y + 12.f};
        ImVec2 c2_max = {body_max.x, c2_min.y + 148.f};
        gp_card(dl, c2_min, c2_max);
        ImGui::SetCursorScreenPos({c2_min.x + 14.f, c2_min.y + 12.f});
        ImGui::PushItemWidth(c2_max.x - c2_min.x - 28.f);
        void_slider_f("Max distance", &esp::cfg.aim_max_dist, 0.f, 5000.f, 10.f, "%.0f");
        void_slider_f("X bias (px)",  &esp::cfg.rivals_x_bias, -200.f, 200.f, 1.f, "%.0f");
        void_slider_f("Y bias (px)",  &esp::cfg.rivals_y_bias, -200.f, 200.f, 1.f, "%.0f");
        ImGui::PopItemWidth();

        float fx_y = c2_max.y - 34.f;
        float half = (c2_max.x - c2_min.x) * 0.5f - 20.f;
        gp_toggle_row(dl, {c2_min.x + 14.f, fx_y}, half, 26.f,
                      "Flip X", &esp::cfg.rivals_flip_x,
                      "Negate viewport.x if shots consistently mirror horizontally.");
        gp_toggle_row(dl, {c2_min.x + half + 26.f, fx_y}, half, 26.f,
                      "Flip Y", &esp::cfg.rivals_flip_y,
                      "Negate viewport.y if shots consistently mirror vertically.");

        ImVec2 c3_min = {body_min.x, c2_max.y + 12.f};
        ImVec2 c3_max = {body_max.x, c3_min.y + 74.f};
        gp_card(dl, c3_min, c3_max);
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c3_min.x + 14.f, c3_min.y + 10.f}, C_TEXT_MUTE, "Aim part");
        if (g_font_small) ImGui::PopFont();
        const char* PARTS[3] = {"Head", "Torso", "Nearest to cursor"};
        float seg_h = 32.f;
        float seg_y = c3_min.y + 32.f;
        float seg_w = ((c3_max.x - c3_min.x) - 28.f - 8.f) / 3.f;
        for (int i = 0; i < 3; i++) {
            ImVec2 mn{c3_min.x + 14.f + i * (seg_w + 4.f), seg_y};
            ImVec2 mx{mn.x + seg_w,                        seg_y + seg_h};
            if (gp_chip_btn(dl, mn, mx, PARTS[i],
                            esp::cfg.silent_aim_part == i, true)) {
                esp::cfg.silent_aim_part = i;
            }
        }

        ImVec2 c4_min = {body_min.x, c3_max.y + 12.f};
        ImVec2 c4_max = {body_max.x, c4_min.y + 74.f};
        gp_card(dl, c4_min, c4_max);
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c4_min.x + 14.f, c4_min.y + 10.f}, C_TEXT_MUTE,
            "Camera.Viewport offset (auto-fetched from the offset feed —");
        dl->AddText({c4_min.x + 14.f, c4_min.y + 26.f}, C_TEXT_MUTE,
            "Camera::Viewport / Camera::ViewportSize).");
        char cur[64];
        std::snprintf(cur, sizeof(cur),
                      offsets::Camera::Viewport
                        ? "current: 0x%llX"
                        : "current: 0x%llX  (feed hasn't published this yet — silent aim will no-op)",
                      (unsigned long long)offsets::Camera::Viewport);
        dl->AddText({c4_min.x + 14.f, c4_min.y + 46.f},
                    offsets::Camera::Viewport ? C_ACC : C_BAD, cur);
        if (g_font_small) ImGui::PopFont();

        ImVec2 c5_min = {body_min.x, c4_max.y + 12.f};
        ImVec2 c5_max = {body_max.x, c5_min.y + 44.f};
        if (gp_chip_btn(dl, c5_min, c5_max,
                        "Apply Rivals defaults", false, true)) {
            games::rivals_apply_defaults();
            g_prof_toast    = "Applied Rivals defaults";
            g_prof_toast_ttl = 2.2f;
        }
        return;
    }

    if (g == games::Id::KAT || g == games::Id::Assassin) {
        const bool is_kat = (g == games::Id::KAT);
        ImVec2 c1_min = body_min;
        ImVec2 c1_max = {body_max.x, body_min.y + 232.f};
        gp_card(dl, c1_min, c1_max);
        if (g_font_medium) ImGui::PushFont(g_font_medium);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 12.f}, C_TEXT, "Silent throw");
        if (g_font_medium) ImGui::PopFont();
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 34.f}, C_TEXT_MUTE,
            is_kat
              ? "KAT throws cast from mouse.Hit — silent aim writes the target's"
              : "Assassin! throws cast from mouse.Hit — silent aim writes the target's");
        dl->AddText({c1_min.x + 14.f, c1_min.y + 48.f}, C_TEXT_MUTE,
            "MouseService.MousePosition so the knife lands on head every throw.");
        if (g_font_small) ImGui::PopFont();

        float row_y = c1_min.y + 62.f;
        float row_h = 42.f;
        float row_w = (c1_max.x - c1_min.x) - 28.f;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Silent aim", &esp::cfg.silent_aim,
                      "Passive — no keybind. Throws land in FOV.");
        row_y += row_h;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Team check", &esp::cfg.team_check,
                      "Skip same-team players (round-based modes).");
        row_y += row_h;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "Visibility check", &esp::cfg.aim_visibility_check,
                      "Skip targets behind terrain.");

        ImVec2 c2_min = {body_min.x, c1_max.y + 12.f};
        ImVec2 c2_max = {body_max.x, c2_min.y + 120.f};
        gp_card(dl, c2_min, c2_max);
        ImGui::SetCursorScreenPos({c2_min.x + 14.f, c2_min.y + 12.f});
        ImGui::PushItemWidth(c2_max.x - c2_min.x - 28.f);
        void_slider_f("Max distance", &esp::cfg.aim_max_dist, 0.f, 2000.f, 10.f, "%.0f");
        ImGui::PopItemWidth();

        ImVec2 c3_min = {body_min.x, c2_max.y + 12.f};
        ImVec2 c3_max = {body_max.x, c3_min.y + 74.f};
        gp_card(dl, c3_min, c3_max);
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c3_min.x + 14.f, c3_min.y + 10.f}, C_TEXT_MUTE, "Aim part");
        if (g_font_small) ImGui::PopFont();
        const char* PARTS[3] = {"Head", "Torso", "Nearest to cursor"};
        float seg_h = 32.f;
        float seg_y = c3_min.y + 32.f;
        float seg_w = ((c3_max.x - c3_min.x) - 28.f - 8.f) / 3.f;
        for (int i = 0; i < 3; i++) {
            ImVec2 mn{c3_min.x + 14.f + i * (seg_w + 4.f), seg_y};
            ImVec2 mx{mn.x + seg_w,                        seg_y + seg_h};
            if (gp_chip_btn(dl, mn, mx, PARTS[i],
                            esp::cfg.silent_aim_part == i, true)) {
                esp::cfg.silent_aim_part = i;
            }
        }

        ImVec2 c4_min = {body_min.x, c3_max.y + 12.f};
        ImVec2 c4_max = {body_max.x, c4_min.y + 44.f};
        const char* btn = is_kat ? "Apply KAT defaults" : "Apply Assassin! defaults";
        if (gp_chip_btn(dl, c4_min, c4_max, btn, false, true)) {
            if (is_kat) games::kat_apply_defaults();
            else        games::assassin_apply_defaults();
            g_prof_toast    = is_kat ? "Applied KAT defaults" : "Applied Assassin! defaults";
            g_prof_toast_ttl = 2.2f;
        }
        return;
    }

    if (g == games::Id::StrongestBG || g == games::Id::BloxFruits) {
        const bool is_tsb = (g == games::Id::StrongestBG);
        ImVec2 c1_min = body_min;
        ImVec2 c1_max = {body_max.x, body_min.y + 200.f};
        gp_card(dl, c1_min, c1_max);
        if (g_font_medium) ImGui::PushFont(g_font_medium);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 12.f}, C_TEXT, "Movement");
        if (g_font_medium) ImGui::PopFont();
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 34.f}, C_TEXT_MUTE,
            is_tsb
              ? "TSB validates jump-power server-side (rubber-bands). Walkspeed"
              : "Blox Fruits kicks at ~150+ WalkSpeed (anim watchdog). Preset"
        );
        dl->AddText({c1_min.x + 14.f, c1_min.y + 48.f}, C_TEXT_MUTE,
            is_tsb
              ? "under ~30 is transparent — preset is 24."
              : "sits at 90 and leaves jump alone."
        );
        if (g_font_small) ImGui::PopFont();

        float row_y = c1_min.y + 62.f;
        float row_h = 42.f;
        float row_w = (c1_max.x - c1_min.x) - 28.f;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "WalkSpeed", &esp::cfg.speed_enabled, nullptr);
        row_y += row_h;
        gp_toggle_row(dl, {c1_min.x + 14.f, row_y}, row_w, row_h,
                      "JumpPower", &esp::cfg.jump_enabled,
                      is_tsb ? "Server rubber-bands — leave off unless a round is dead."
                             : "Blox Fruits accepts small bumps. Careful over ~120.");

        ImVec2 c2_min = {body_min.x, c1_max.y + 12.f};
        ImVec2 c2_max = {body_max.x, c2_min.y + 120.f};
        gp_card(dl, c2_min, c2_max);
        ImGui::SetCursorScreenPos({c2_min.x + 14.f, c2_min.y + 12.f});
        ImGui::PushItemWidth(c2_max.x - c2_min.x - 28.f);
        void_slider_f("Walk speed",  &esp::cfg.speed_value, 16.f, is_tsb ? 60.f : 200.f, 1.f, "%.0f");
        void_slider_f("Jump power",  &esp::cfg.jump_value,  30.f, is_tsb ?  90.f : 200.f, 1.f, "%.0f");
        ImGui::PopItemWidth();

        ImVec2 c4_min = {body_min.x, c2_max.y + 12.f};
        ImVec2 c4_max = {body_max.x, c4_min.y + 44.f};
        const char* btn = is_tsb ? "Apply Strongest BG defaults" : "Apply Blox Fruits defaults";
        if (gp_chip_btn(dl, c4_min, c4_max, btn, false, true)) {
            if (is_tsb) games::strongestbg_apply_defaults();
            else        games::bloxfruits_apply_defaults();
            g_prof_toast    = is_tsb ? "Applied Strongest BG defaults" : "Applied Blox Fruits defaults";
            g_prof_toast_ttl = 2.2f;
        }
        return;
    }

    struct StompMeta {
        const char* subhint;
        const char* apply_label;
        const char* toast;
        void (*apply_defaults_fn)();
        bool  show_hip_preset;
        const char* pf_note;
    };
    StompMeta meta = {};
    switch (g) {
        case games::Id::Arsenal:
            meta = {
                "Patches ReplicatedStorage.Weapons.<gun>.<value> for every equipped weapon.",
                "Apply Arsenal aim/ESP defaults", "Applied Arsenal defaults",
                &games::arsenal_apply_defaults, true, nullptr,
            };
            break;
        case games::Id::BadBusiness:
            meta = {
                "Patches ReplicatedStorage.Weapons.<gun>.Stats — Recoil/Spread/ReloadTime/RateOfFire.",
                "Apply Bad Business defaults", "Applied Bad Business defaults",
                &games::badbusiness_apply_defaults, false, nullptr,
            };
            break;
        case games::Id::PhantomForces:
            meta = {
                "Patches PF weapon modules — CameraKick / SpreadRecover / RecoilMin-Max / FireRate.",
                "Apply Phantom Forces defaults", "Applied Phantom Forces defaults",
                &games::phantomforces_apply_defaults, false,
                "PF anti-cheat range-checks these values. Preset uses subtle non-zero writes.",
            };
            break;
        case games::Id::BigPaintball:
            meta = {
                "Patches ReplicatedStorage.Guns.<gun>.Stats — Recoil/Spread/ReloadTime/FireRate.",
                "Apply BIG Paintball defaults", "Applied BIG Paintball defaults",
                &games::bigpaintball_apply_defaults, false, nullptr,
            };
            break;
        case games::Id::CounterBlox:
            meta = {
                "Patches ReplicatedStorage.Weapons.<gun> — Vertical/Horizontal Recoil / Spread.",
                "Apply Counter Blox defaults", "Applied Counter Blox defaults",
                &games::counterblox_apply_defaults, false, nullptr,
            };
            break;
        default:

            return;
    }

    float y_cursor = body_min.y;
    if (meta.show_hip_preset) {
        ImVec2 c1_min = {body_min.x, y_cursor};
        ImVec2 c1_max = {body_max.x, y_cursor + 88.f};
        gp_card(dl, c1_min, c1_max);
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({c1_min.x + 14.f, c1_min.y + 12.f}, C_TEXT_MUTE, "Hip height preset");
        if (g_font_small) ImGui::PopFont();
        const char* PRESETS[3] = {"Normal (0)", "Tall (2)", "Float (4)"};
        float seg_h = 32.f;
        float seg_y = c1_min.y + 40.f;
        float seg_w = ((c1_max.x - c1_min.x) - 28.f - 8.f) / 3.f;
        for (int i = 0; i < 3; i++) {
            ImVec2 mn{c1_min.x + 14.f + i * (seg_w + 4.f), seg_y};
            ImVec2 mx{mn.x + seg_w,                        seg_y + seg_h};
            if (gp_chip_btn(dl, mn, mx, PRESETS[i], esp::cfg.arsenal_hip_preset == i, true)) {
                esp::cfg.arsenal_hip_preset = i;
            }
        }
        y_cursor = c1_max.y + 12.f;
    }

    float pf_extra = meta.pf_note ? 18.f : 0.f;
    ImVec2 c2_min = {body_min.x, y_cursor};
    ImVec2 c2_max = {body_max.x, y_cursor + 250.f + pf_extra};
    gp_card(dl, c2_min, c2_max);
    if (g_font_medium) ImGui::PushFont(g_font_medium);
    dl->AddText({c2_min.x + 14.f, c2_min.y + 12.f}, C_TEXT, "Weapon mods");
    if (g_font_medium) ImGui::PopFont();
    if (g_font_small) ImGui::PushFont(g_font_small);
    dl->AddText({c2_min.x + 14.f, c2_min.y + 34.f},
                offsets::ValueBase::Value ? C_TEXT_MUTE : C_BAD,
                offsets::ValueBase::Value ? meta.subhint
                    : "Disabled — dumper missing ValueBase::Value offset. See log.");
    if (meta.pf_note) {
        dl->AddText({c2_min.x + 14.f, c2_min.y + 48.f}, C_BAD, meta.pf_note);
    }
    if (g_font_small) ImGui::PopFont();

    float row_y = c2_min.y + 62.f + pf_extra;
    float row_h = 44.f;
    float row_w = (c2_max.x - c2_min.x) - 28.f;
    gp_toggle_row(dl, {c2_min.x + 14.f, row_y}, row_w, row_h,
                  "No Recoil",   &esp::cfg.arsenal_no_recoil,
                  "Neutralize recoil-family values.");
    row_y += row_h;
    gp_toggle_row(dl, {c2_min.x + 14.f, row_y}, row_w, row_h,
                  "No Spread",   &esp::cfg.arsenal_no_spread,
                  "Neutralize spread-family values.");
    row_y += row_h;
    gp_toggle_row(dl, {c2_min.x + 14.f, row_y}, row_w, row_h,
                  "Auto Reload", &esp::cfg.arsenal_auto_reload,
                  "ReloadTime → instant / near-instant (per-game safe value).");
    row_y += row_h;
    gp_toggle_row(dl, {c2_min.x + 14.f, row_y}, row_w, row_h,
                  "Rapid Fire",  &esp::cfg.arsenal_rapid_fire,
                  "FireRate → derived from slider below.");
    y_cursor = c2_max.y + 12.f;

    ImVec2 c3_min = {body_min.x, y_cursor};
    ImVec2 c3_max = {body_max.x, y_cursor + 68.f};
    gp_card(dl, c3_min, c3_max);
    ImGui::SetCursorScreenPos({c3_min.x + 14.f, c3_min.y + 10.f});
    ImGui::PushItemWidth(c3_max.x - c3_min.x - 28.f);
    void_slider_f("Fire rate ceiling", &esp::cfg.arsenal_fire_rate,
                  5.f, 60.f, 1.f, "%.0f shots/s");
    ImGui::PopItemWidth();
    y_cursor = c3_max.y + 12.f;

    ImVec2 c4_min = {body_min.x, y_cursor};
    ImVec2 c4_max = {body_max.x, y_cursor + 44.f};
    if (gp_chip_btn(dl, c4_min, c4_max, meta.apply_label, false, true)) {
        meta.apply_defaults_fn();
        g_prof_toast     = meta.toast;
        g_prof_toast_ttl = 2.2f;
    }
}

static std::unordered_map<uintptr_t, bool> g_expl_open;
static uintptr_t g_expl_selected = 0;
static char      g_expl_filter_buf[64] = {};
static std::string g_expl_selected_name;
static std::string g_expl_selected_class;

static void expl_draw_node(ImDrawList* dl, RbxInstance node,
                           float x, float y, float row_w, float row_h,
                           int depth, ImVec2 clip_min, ImVec2 clip_max,
                           float& out_next_y, const std::string& filter);

static void expl_draw_node(ImDrawList* dl, RbxInstance node,
                           float x, float y, float row_w, float row_h,
                           int depth, ImVec2 clip_min, ImVec2 clip_max,
                           float& out_next_y, const std::string& filter) {
    if (!node.valid()) { out_next_y = y; return; }

    std::string name = node.get_name();
    if (name.empty()) name = "<unnamed>";
    std::string cls  = node.get_class();

    bool matches = true;
    bool auto_expand = false;
    if (!filter.empty()) {
        std::string hay = name + " " + cls;
        std::string h2 = hay;
        for (auto& c : h2) c = (char)std::tolower((unsigned char)c);
        matches = h2.find(filter) != std::string::npos;
        auto_expand = matches;
    }

    bool visible = (y + row_h >= clip_min.y && y <= clip_max.y);

    auto children = node.get_children();
    bool has_kids = !children.empty();
    bool is_open  = g_expl_open[node.ptr] || auto_expand;

    if (visible) {
        float ind = (float)depth * 14.f;
        ImRect bb({x, y}, {x + row_w, y + row_h});
        bool hovered = hit(bb);
        bool selected = (g_expl_selected == node.ptr);

        if (selected)
            dl->AddRectFilled(bb.Min, bb.Max, IM_COL32(44, 44, 44, 255), 4.f);
        else if (hovered)
            dl->AddRectFilled(bb.Min, bb.Max, C_BG_ELEV_H, 4.f);

        float chev_x = x + 6.f + ind;
        float mid_y  = y + row_h * 0.5f;
        if (has_kids) {
            ImRect chev({chev_x - 6, mid_y - 6}, {chev_x + 6, mid_y + 6});
            ImU32 chev_col = selected ? C_ACC : C_TEXT_MUTE;
            if (is_open) {

                dl->AddTriangleFilled({chev_x - 4, mid_y - 2},
                                      {chev_x + 4, mid_y - 2},
                                      {chev_x,     mid_y + 3}, chev_col);
            } else {

                dl->AddTriangleFilled({chev_x - 2, mid_y - 4},
                                      {chev_x - 2, mid_y + 4},
                                      {chev_x + 3, mid_y},     chev_col);
            }
            if (hit(chev) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                g_expl_open[node.ptr] = !is_open;
                is_open = !is_open;
            }
        } else {
            dl->AddCircleFilled({chev_x, mid_y}, 1.5f, C_TEXT_DIM);
        }

        char label[192];
        if (!cls.empty())
            std::snprintf(label, sizeof(label), "%s  \xE2\x80\x94  %s",
                          name.c_str(), cls.c_str());
        else
            std::snprintf(label, sizeof(label), "%s", name.c_str());
        ImU32 fg = selected ? C_TEXT
                            : (matches ? C_TEXT : C_TEXT_DIM);
        dl->AddText({chev_x + 12.f, y + (row_h - ImGui::GetFontSize()) * 0.5f},
                    fg, label);

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !hit(ImRect({chev_x - 6, mid_y - 6}, {chev_x + 6, mid_y + 6}))) {
            g_expl_selected = node.ptr;
            g_expl_selected_name = name;
            g_expl_selected_class = cls;
        }
    }

    out_next_y = y + row_h;
    if (is_open) {

        int cap = (int)children.size();
        if (cap > 256) cap = 256;
        for (int i = 0; i < cap; i++) {
            float child_next_y = out_next_y;
            expl_draw_node(dl, children[i], x, out_next_y, row_w, row_h,
                           depth + 1, clip_min, clip_max, child_next_y, filter);
            out_next_y = child_next_y;
        }
        if ((int)children.size() > 256 && visible) {
            dl->AddText({x + 26.f + (float)(depth + 1) * 14.f, out_next_y + 2.f},
                        C_TEXT_DIM, "…truncated");
            out_next_y += row_h;
        }
    }
}

static void expl_prop_row(ImDrawList* dl, float x, float& y, float w,
                          const char* key, const char* val, ImU32 val_col = C_TEXT) {
    dl->AddText({x, y}, C_TEXT_MUTE, key);
    ImVec2 sz = ImGui::CalcTextSize(val);
    dl->AddText({x + w - sz.x, y}, val_col, val);
    y += 20.f;
}

static void set_clipboard_str(const std::string& s) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
    if (h) {
        void* p = GlobalLock(h);
        if (p) { std::memcpy(p, s.c_str(), s.size() + 1); GlobalUnlock(h); }
        SetClipboardData(CF_TEXT, h);
    }
    CloseClipboard();
}

static void draw_explorer_pane(ImVec2 cont_min, ImVec2 cont_max) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    RbxDataModel dm = RbxDataModel::get();

    ImVec2 sp = cont_min;
    float  sw = cont_max.x - cont_min.x;
    float  sh = 30.f;
    dl->AddRectFilled(sp, {sp.x + sw, sp.y + sh}, IM_COL32(14, 14, 14, 255), 8.f);
    dl->AddRect(sp, {sp.x + sw, sp.y + sh}, C_LINE_SOFT, 8.f, 0, 1.f);
    ImVec2 mc{sp.x + 12.f, sp.y + sh * 0.5f};
    dl->AddCircle(mc, 5.f, C_TEXT_MUTE, 0, 1.5f);
    dl->AddLine({mc.x + 3.5f, mc.y + 3.5f}, {mc.x + 8.f, mc.y + 8.f}, C_TEXT_MUTE, 1.5f);

    const float DUMP_BTN_W = 96.f;

    ImGui::SetCursorScreenPos({sp.x + 26.f, sp.y + (sh - ImGui::GetFontSize()) * 0.5f - 1.f});
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushItemWidth(sw - 36.f - DUMP_BTN_W - 10.f);
    ImGui::InputTextWithHint("##explfilter", "search by name / class…",
        g_expl_filter_buf, IM_ARRAYSIZE(g_expl_filter_buf));
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);


    {
        ImVec2 bmin{sp.x + sw - DUMP_BTN_W - 4.f, sp.y + 4.f};
        ImVec2 bmax{sp.x + sw - 4.f,              sp.y + sh - 4.f};
        bool   busy = dumper::g_dumping.load();
        bool   hov  = !busy && ImGui::IsMouseHoveringRect(bmin, bmax);
        ImU32  bg   = busy ? IM_COL32(24, 24, 24, 255)
                            : (hov ? blend(C_BG_ELEV_H, C_ACC_DEEP, 0.35f)
                                   : C_BG_ELEV);
        dl->AddRectFilled(bmin, bmax, bg, 6.f);
        dl->AddRect(bmin, bmax, busy ? C_LINE_HAIR : C_LINE, 6.f, 0, 1.f);
        char lbl[48];
        if (busy) std::snprintf(lbl, sizeof(lbl), "dumping %zu…",
                                dumper::g_progress_count.load());
        else      std::snprintf(lbl, sizeof(lbl), "Dump DM");
        ImVec2 lz = ImGui::CalcTextSize(lbl);
        dl->AddText({bmin.x + ((bmax.x - bmin.x) - lz.x) * 0.5f,
                     bmin.y + ((bmax.y - bmin.y) - lz.y) * 0.5f},
                    busy ? C_TEXT_MUTE : C_TEXT, lbl);
        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dumper::dump_datamodel_async(mem::exe_dir() + "\\dumps");
        }
    }

    std::string filter = g_expl_filter_buf;
    for (auto& c : filter) c = (char)std::tolower((unsigned char)c);

    float body_y = sp.y + sh + 10.f;
    float tree_w = (cont_max.x - cont_min.x) * 0.60f - 6.f;
    float prop_x = cont_min.x + tree_w + 12.f;
    float prop_w = cont_max.x - prop_x;

    dl->AddRectFilled({cont_min.x, body_y},
                      {cont_min.x + tree_w, cont_max.y},
                      IM_COL32(10, 10, 10, 255), 8.f);
    dl->AddRect({cont_min.x, body_y},
                {cont_min.x + tree_w, cont_max.y},
                C_LINE_SOFT, 8.f, 0, 1.f);

    ImGui::SetCursorScreenPos({cont_min.x + 6.f, body_y + 6.f});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##expltree",
        {tree_w - 12.f, cont_max.y - body_y - 12.f}, false,
        ImGuiWindowFlags_NoBackground);
    ImVec2 tree_top = ImGui::GetCursorScreenPos();
    ImVec2 tree_bot = {tree_top.x + tree_w - 12.f,
                       tree_top.y + (cont_max.y - body_y - 12.f)};

    if (!dm.valid()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_TEXT_DIM),
            "  DataModel not resolved.");
    } else {
        float row_h = 22.f;
        float y = tree_top.y;
        float next_y = y;
        expl_draw_node(dl, dm.as_instance(), tree_top.x, y,
                       tree_w - 12.f, row_h, 0,
                       tree_top, tree_bot, next_y, filter);

        ImGui::Dummy({0, next_y - tree_top.y});
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    dl->AddRectFilled({prop_x, body_y},
                      {prop_x + prop_w, cont_max.y},
                      IM_COL32(10, 10, 10, 255), 8.f);
    dl->AddRect({prop_x, body_y},
                {prop_x + prop_w, cont_max.y},
                C_LINE_SOFT, 8.f, 0, 1.f);

    float px = prop_x + 14.f;
    float py = body_y + 14.f;
    float pw = prop_w - 28.f;

    if (!g_expl_selected) {
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({px, py}, C_TEXT_DIM, "Click any node in the tree to inspect it.");
        if (g_font_small) ImGui::PopFont();
        return;
    }

    RbxInstance sel{g_expl_selected};

    std::string live_name = sel.get_name();
    std::string live_cls  = sel.get_class();
    if (!live_name.empty()) g_expl_selected_name = live_name;
    if (!live_cls.empty())  g_expl_selected_class = live_cls;

    if (g_font_header) ImGui::PushFont(g_font_header);
    dl->AddText({px, py}, C_TEXT, g_expl_selected_name.c_str());
    py += 26.f;
    if (g_font_header) ImGui::PopFont();
    if (g_font_small) ImGui::PushFont(g_font_small);
    dl->AddText({px, py}, C_ACC, g_expl_selected_class.c_str());
    py += 20.f;
    if (g_font_small) ImGui::PopFont();

    dl->AddLine({px, py + 4.f}, {px + pw, py + 4.f}, C_LINE_HAIR);
    py += 14.f;

    char buf[96];

    std::snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)sel.ptr);
    expl_prop_row(dl, px, py, pw, "Address", buf);

    uintptr_t parent = offsets::Instance::Parent
        ? rpm<uintptr_t>(sel.ptr + offsets::Instance::Parent) : 0;
    std::snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)parent);
    expl_prop_row(dl, px, py, pw, "Parent", buf);

    auto kids = sel.get_children();
    std::snprintf(buf, sizeof(buf), "%d", (int)kids.size());
    expl_prop_row(dl, px, py, pw, "Children", buf);

    if (live_cls == "Humanoid") {
        if (offsets::Humanoid::Health) {
            float h = rpm<float>(sel.ptr + offsets::Humanoid::Health);
            float mh = offsets::Humanoid::MaxHealth
                ? rpm<float>(sel.ptr + offsets::Humanoid::MaxHealth) : 0.f;
            std::snprintf(buf, sizeof(buf), "%.1f / %.1f", h, mh);
            expl_prop_row(dl, px, py, pw, "Health", buf,
                          h > 0.f ? C_OK : C_BAD);
        }
        if (offsets::Humanoid::WalkSpeed) {
            std::snprintf(buf, sizeof(buf), "%.1f",
                rpm<float>(sel.ptr + offsets::Humanoid::WalkSpeed));
            expl_prop_row(dl, px, py, pw, "WalkSpeed", buf);
        }
        if (offsets::Humanoid::JumpPower) {
            std::snprintf(buf, sizeof(buf), "%.1f",
                rpm<float>(sel.ptr + offsets::Humanoid::JumpPower));
            expl_prop_row(dl, px, py, pw, "JumpPower", buf);
        }
        if (offsets::Humanoid::HipHeight) {
            std::snprintf(buf, sizeof(buf), "%.2f",
                rpm<float>(sel.ptr + offsets::Humanoid::HipHeight));
            expl_prop_row(dl, px, py, pw, "HipHeight", buf);
        }
    } else if (live_cls == "Camera") {
        if (offsets::Camera::FieldOfView) {
            std::snprintf(buf, sizeof(buf), "%.1f",
                rpm<float>(sel.ptr + offsets::Camera::FieldOfView));
            expl_prop_row(dl, px, py, pw, "FieldOfView", buf);
        }
    } else if (live_cls == "NumberValue" || live_cls == "IntValue" ||
               live_cls == "BoolValue" || live_cls == "Misc") {
        if (offsets::ValueBase::Value) {
            if (live_cls == "BoolValue") {
                bool v = rpm<uint8_t>(sel.ptr + offsets::ValueBase::Value) != 0;
                expl_prop_row(dl, px, py, pw, "Value", v ? "true" : "false",
                              v ? C_OK : C_TEXT_MUTE);
            } else if (live_cls == "IntValue") {
                std::snprintf(buf, sizeof(buf), "%d",
                    (int)rpm<int32_t>(sel.ptr + offsets::ValueBase::Value));
                expl_prop_row(dl, px, py, pw, "Value", buf);
            } else {
                std::snprintf(buf, sizeof(buf), "%.4f",
                    rpm<float>(sel.ptr + offsets::ValueBase::Value));
                expl_prop_row(dl, px, py, pw, "Value", buf);
            }
        }
    } else if (offsets::BasePart::Primitive) {

        uintptr_t prim = rpm<uintptr_t>(sel.ptr + offsets::BasePart::Primitive);
        if (prim && offsets::Primitive::Position) {
            Vec3 p = rpm<Vec3>(prim + offsets::Primitive::Position);
            std::snprintf(buf, sizeof(buf), "%.1f, %.1f, %.1f", p.x, p.y, p.z);
            expl_prop_row(dl, px, py, pw, "Position", buf);
        }
    }

    py += 12.f;
    dl->AddLine({px, py}, {px + pw, py}, C_LINE_HAIR);
    py += 14.f;

    auto action_btn = [&](const char* label, float bw) -> bool {
        ImRect r({px, py}, {px + bw, py + 26.f});
        return gp_chip_btn(dl, r.Min, r.Max, label, false, true);
    };
    float bw = (pw - 12.f) * 0.5f;
    if (action_btn("Copy address", bw)) {
        char cb[32]; std::snprintf(cb, sizeof(cb), "0x%llX", (unsigned long long)sel.ptr);
        set_clipboard_str(cb);
        g_prof_toast = "Address copied"; g_prof_toast_ttl = 1.5f;
    }
    {
        ImRect r({px + bw + 12.f, py}, {px + pw, py + 26.f});
        if (gp_chip_btn(dl, r.Min, r.Max, "Copy name", false, true)) {
            set_clipboard_str(g_expl_selected_name);
            g_prof_toast = "Name copied"; g_prof_toast_ttl = 1.5f;
        }
    }
    py += 34.f;

    if (offsets::BasePart::Primitive && offsets::Primitive::Position) {
        uintptr_t prim = rpm<uintptr_t>(sel.ptr + offsets::BasePart::Primitive);
        if (prim) {
            ImRect r({px, py}, {px + pw, py + 30.f});
            if (gp_chip_btn(dl, r.Min, r.Max,
                            "Teleport me to this part", false, true)) {
                Vec3 target = rpm<Vec3>(prim + offsets::Primitive::Position);
                target.y += 3.f;
                tp::request_teleport(target, 60);
                g_prof_toast = "Teleport requested"; g_prof_toast_ttl = 1.6f;
            }
            py += 38.f;
        }
    }
}

static void draw_players_pane(const std::vector<PlayerInfo>* players,
                              ImVec2 cont_min, ImVec2 cont_max) {
    ImVec2 body_min(cont_min.x, cont_min.y + 44.f);
    ImVec2 body_max = cont_max;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sp = body_min;
    float sw = body_max.x - body_min.x;
    float sh = 34.f;
    dl->AddRectFilled(sp, {sp.x + sw, sp.y + sh}, IM_COL32(14, 14, 14, 255), 8.f);
    dl->AddRect(sp, {sp.x + sw, sp.y + sh}, C_LINE_SOFT, 8.f, 0, 1.f);

    ImVec2 mc{sp.x + 14.f, sp.y + sh * 0.5f};
    dl->AddCircle(mc, 5.f, C_TEXT_MUTE, 0, 1.5f);
    dl->AddLine({mc.x + 3.5f, mc.y + 3.5f}, {mc.x + 8.f, mc.y + 8.f}, C_TEXT_MUTE, 1.5f);

    ImGui::SetCursorScreenPos({sp.x + 28.f, sp.y + (sh - ImGui::GetFontSize()) * 0.5f - 1.f});
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,         IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGui::PushItemWidth(sw - 40.f);
    if (ImGui::InputTextWithHint("##filter", "Filter players…",
        g_players_filter_buf, IM_ARRAYSIZE(g_players_filter_buf))) {
        g_players_filter = g_players_filter_buf;
        for (auto& c : g_players_filter) c = (char)std::tolower((unsigned char)c);
    }
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    ImVec2 list_pos{sp.x, sp.y + sh + 10.f};
    ImVec2 list_size(body_max.x - list_pos.x, body_max.y - list_pos.y);
    ImGui::SetCursorScreenPos(list_pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##players_list", list_size, false,
        ImGuiWindowFlags_NoBackground);

    uintptr_t local_hrp_prim = tp::local_hrp_primitive();

    if (!players || players->empty()) {
        ImGui::Dummy({0, 30});
        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText({cp.x + 4.f, cp.y}, C_TEXT_DIM,
            "no players in snapshot");
    } else {
        int drawn = 0;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 6});
        for (const auto& p : *players) {
            if (!g_players_filter.empty()) {
                std::string lower = p.name;
                for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
                if (lower.find(g_players_filter) == std::string::npos) continue;
            }
            player_row(p, local_hrp_prim);
            drawn++;
        }
        ImGui::PopStyleVar();
        if (!drawn) {
            ImGui::Dummy({0, 20});
            ImVec2 cp = ImGui::GetCursorScreenPos();
            char msg[96];
            std::snprintf(msg, sizeof(msg), "nothing matches '%s'", g_players_filter.c_str());
            ImGui::GetWindowDrawList()->AddText({cp.x + 4.f, cp.y}, C_TEXT_DIM, msg);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

struct PopupState { const Module* mod = nullptr; ImVec2 anchor{}; bool just_opened = false; float anim = 0.f; int cat_ix = 0; };
static PopupState g_popup;

static ImVec2 g_main_pos{};
static ImVec2 g_main_size{};

static void draw_popup() {
    if (!g_popup.mod) { g_popup.anim = 0.f; return; }
    const Module& m = *g_popup.mod;

    constexpr float POP_GAP     = 14.f;
    constexpr float POP_W_1COL  = 480.f;
    constexpr float POP_W_2COL  = 740.f;
    constexpr float COL_GAP     = 20.f;
    ImVec2 io_size = ImGui::GetIO().DisplaySize;

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float dim = anim_to(0xDEADBEEF, 1.f, 12.f);
    fg->AddRectFilled({0, 0}, io_size, IM_COL32(0, 0, 0, (int)(50 * dim)));

    const float HDR_H    = 54.f;
    const float BODY_PAD = 28.f;
    const float ROW_GAP  = 12.f;
    auto field_h = [](const Field& f) -> float {
        switch (f.type) {
            case Fty::Toggle:  return 30.f;
            case Fty::Key:     return 34.f;
            case Fty::SliderF:
            case Fty::SliderI: return 40.f;
            case Fty::Segment: return 42.f;
            case Fty::Section: return ImGui::GetFontSize() + 16.f;
        }
        return 30.f;
    };

    float content_h = 0.f;
    for (int i = 0; i < m.body_count; i++) {
        if (i) content_h += ROW_GAP;
        content_h += field_h(m.body[i]);
    }

    float body_budget = io_size.y * 0.85f - HDR_H - BODY_PAD - 12.f;
    if (body_budget < 200.f) body_budget = 200.f;

    bool two_col = (content_h > body_budget) && (m.body_count >= 4);

    int split_at = m.body_count;
    float col_a_h = content_h;
    float col_b_h = 0.f;
    if (two_col) {
        float half = 0.f;
        for (int i = 0; i < m.body_count; i++) {
            float h = field_h(m.body[i]) + (i ? ROW_GAP : 0.f);
            half += h;
        }
        half *= 0.5f;
        float acc = 0.f;
        split_at = m.body_count;
        for (int i = 0; i < m.body_count; i++) {
            float h = field_h(m.body[i]) + (i ? ROW_GAP : 0.f);
            if (acc + h > half && i > 0) { split_at = i; break; }
            acc += h;
        }

        col_a_h = 0.f;
        for (int i = 0; i < split_at; i++) {
            if (i) col_a_h += ROW_GAP;
            col_a_h += field_h(m.body[i]);
        }
        col_b_h = 0.f;
        for (int i = split_at; i < m.body_count; i++) {
            if (i > split_at) col_b_h += ROW_GAP;
            col_b_h += field_h(m.body[i]);
        }
    }

    const float POP_W = two_col ? POP_W_2COL : POP_W_1COL;
    float pop_max_h = io_size.y * 0.90f;
    float taller_col = col_a_h > col_b_h ? col_a_h : col_b_h;
    float wanted_h = HDR_H + BODY_PAD + taller_col + 16.f;
    if (wanted_h > pop_max_h) wanted_h = pop_max_h;
    if (wanted_h < 120.f)     wanted_h = 120.f;

    if (g_popup.just_opened) {
        ImVec2 pop_pos;
        if (g_main_pos.x + g_main_size.x + POP_GAP + POP_W <= io_size.x) {
            pop_pos = {g_main_pos.x + g_main_size.x + POP_GAP, g_main_pos.y};
        } else {
            pop_pos = {g_main_pos.x - POP_W - POP_GAP, g_main_pos.y};
            if (pop_pos.x < 4.f) pop_pos.x = 4.f;
        }

        if (pop_pos.y + wanted_h > io_size.y - 4.f)
            pop_pos.y = io_size.y - wanted_h - 4.f;
        if (pop_pos.y < 4.f) pop_pos.y = 4.f;
        ImGui::SetNextWindowPos(pop_pos, ImGuiCond_Always);
        g_popup.just_opened = false;
        g_popup.anim = 0.f;
        g_anim.erase((ImGuiID)0xDEADBEEF);
    }

    g_popup.anim = anim_to((ImGuiID)0xCA11AB1E, 1.f, 18.f);
    float ease = g_popup.anim;
    float scale = 0.94f + 0.06f * ease;
    float alpha = ease;

    ImGui::SetNextWindowSize({POP_W, wanted_h}, ImGuiCond_Always);
    ImGui::SetNextWindowFocus();
    (void)g_popup.anchor;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, with_alpha(C_BG_TOP, alpha));
    ImGui::PushStyleColor(ImGuiCol_Border,   with_alpha(C_LINE, alpha));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,  IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    bool open = true;
    ImGui::Begin("##configpop", &open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoScrollbar| ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();

    drop_shadow(ImGui::GetBackgroundDrawList(),
                wp, {wp.x + ws.x, wp.y + ws.y}, 12.f, 28.f, 0.7f * alpha);

    dl->AddRectFilledMultiColor(wp, {wp.x + ws.x, wp.y + ws.y},
        with_alpha(C_BG_TOP, alpha), with_alpha(C_BG_TOP, alpha),
        with_alpha(C_BG_BOT, alpha), with_alpha(C_BG_BOT, alpha));

    const float HDR = HDR_H;
    dl->AddRectFilledMultiColor(wp, {wp.x + ws.x, wp.y + HDR},
        with_alpha(IM_COL32(16, 16, 16, 255), alpha), with_alpha(IM_COL32(16, 16, 16, 255), alpha),
        with_alpha(IM_COL32( 8,  8,  8, 255), alpha), with_alpha(IM_COL32( 8,  8,  8, 255), alpha));
    dl->AddLine({wp.x, wp.y + HDR}, {wp.x + ws.x, wp.y + HDR}, with_alpha(C_LINE, alpha));

    dl->AddRectFilledMultiColor({wp.x, wp.y + 6.f}, {wp.x + 3.f, wp.y + HDR - 6.f},
        with_alpha(C_ACC_DEEP, alpha), with_alpha(C_ACC_DEEP, alpha),
        with_alpha(C_ACC, alpha),      with_alpha(C_ACC, alpha));

    ImVec2 icon_c{wp.x + 26.f, wp.y + HDR * 0.5f};
    dl->AddCircleFilled(icon_c, 13.f, with_alpha(IM_COL32(24, 24, 24, 255), alpha));
    dl->AddCircle(icon_c, 13.f, with_alpha(C_LINE_HAIR, alpha), 0, 1.f);
    draw_icon(dl, g_popup.cat_ix, icon_c, 8.f, with_alpha(C_ACC, alpha), 1.6f);

    if (g_font_header) ImGui::PushFont(g_font_header);
    float title_fs = ImGui::GetFontSize();
    dl->AddText({wp.x + 50.f, wp.y + (HDR - title_fs) * 0.5f - 8.f},
                with_alpha(C_TEXT, alpha), m.name);
    ImVec2 title_sz = ImGui::CalcTextSize(m.name);
    if (g_font_header) ImGui::PopFont();

    if (g_font_small) ImGui::PushFont(g_font_small);
    const char* sub = two_col ? "right-click a module · Esc to close · drag header to move"
                              : "Esc to close · drag header to move";
    dl->AddText({wp.x + 50.f, wp.y + (HDR - title_fs) * 0.5f + title_sz.y - 6.f},
                with_alpha(C_TEXT_DIM, alpha), sub);
    if (g_font_small) ImGui::PopFont();

    ImVec2 xc{wp.x + ws.x - 24.f, wp.y + HDR * 0.5f};
    ImRect xr({xc.x - 12, xc.y - 12}, {xc.x + 12, xc.y + 12});
    bool xh = hit(xr);
    ImGuiID xid = ImGui::GetCurrentWindow()->GetID("popx");
    float xt = anim_to(xid, xh ? 1.f : 0.f, 18.f);
    if (xt > 0.02f) {
        dl->AddCircleFilled(xc, 12.f, with_alpha(blend(IM_COL32(30,30,30,255), C_BAD, xt * 0.4f), alpha));
    }
    ImU32 xcol = blend(C_TEXT_MUTE, C_BAD, xt);
    dl->AddLine({xc.x - 4.5f, xc.y - 4.5f}, {xc.x + 4.5f, xc.y + 4.5f}, with_alpha(xcol, alpha), 1.7f);
    dl->AddLine({xc.x - 4.5f, xc.y + 4.5f}, {xc.x + 4.5f, xc.y - 4.5f}, with_alpha(xcol, alpha), 1.7f);
    if (xh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) open = false;

    ImGui::SetCursorScreenPos(wp);
    ImGui::InvisibleButton("##drag", {ws.x - 30.f, HDR});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 md = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos({wp.x + md.x, wp.y + md.y});
    }

    float body_h = ws.y - HDR - 12.f;
    if (body_h < 40.f) body_h = 40.f;
    ImGui::SetCursorScreenPos({wp.x, wp.y + HDR});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22.f, 18.f});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));

    if (!two_col) {
        ImGui::BeginChild("##popbody", {ws.x, body_h}, false,
            ImGuiWindowFlags_NoBackground);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, ROW_GAP});
        for (int i = 0; i < m.body_count; i++) draw_field(m.body[i]);
        ImGui::PopStyleVar();
        ImGui::Dummy({0, 6});
        ImGui::EndChild();
    } else {

        float col_w = (ws.x - 44.f - COL_GAP) * 0.5f;

        ImGui::BeginChild("##popbody", {ws.x, body_h}, false,
            ImGuiWindowFlags_NoBackground);

        ImGui::BeginChild("##popcolA", {col_w, body_h - 36.f}, false,
            ImGuiWindowFlags_NoBackground);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, ROW_GAP});
        for (int i = 0; i < split_at; i++) draw_field(m.body[i]);
        ImGui::PopStyleVar();
        ImGui::EndChild();

        ImGui::SameLine(0.f, COL_GAP);

        ImGui::BeginChild("##popcolB", {col_w, body_h - 36.f}, false,
            ImGuiWindowFlags_NoBackground);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, ROW_GAP});
        for (int i = split_at; i < m.body_count; i++) draw_field(m.body[i]);
        ImGui::PopStyleVar();
        ImGui::EndChild();

        ImGui::EndChild();

        float divider_x = wp.x + 22.f + col_w + COL_GAP * 0.5f;
        float dy0 = wp.y + HDR + 18.f;
        float dy1 = wp.y + ws.y - 18.f;
        dl->AddRectFilledMultiColor(
            {divider_x - 0.5f, dy0}, {divider_x + 0.5f, dy1},
            with_alpha(C_LINE_HAIR, 0.f),
            with_alpha(C_LINE_HAIR, 0.f),
            with_alpha(C_LINE_HAIR, alpha),
            with_alpha(C_LINE_HAIR, alpha));
        dl->AddRectFilledMultiColor(
            {divider_x - 0.5f, (dy0 + dy1) * 0.5f},
            {divider_x + 0.5f, dy1},
            with_alpha(C_LINE_HAIR, alpha),
            with_alpha(C_LINE_HAIR, alpha),
            with_alpha(C_LINE_HAIR, 0.f),
            with_alpha(C_LINE_HAIR, 0.f));
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    (void)scale;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;

    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);

    if (!open) g_popup.mod = nullptr;
}

static std::atomic<bool> g_boot_sound_played{false};

static std::wstring find_boot_mp3() {
    static const wchar_t* NAMES[] = {
        L"poppop.ai - uwu meme sound.mp3",
        L"poppop.mp3",
    };

    wchar_t exe_dir[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe_dir, MAX_PATH)) {
        wchar_t* slash = std::wcsrchr(exe_dir, L'\\');
        if (slash) *(slash + 1) = 0;
    }

    wchar_t appdata[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);

    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);

    std::wstring bases[] = {
        exe_dir,
        std::wstring(appdata) + L"\\UwUClient\\",
        std::wstring(tmp)     + L"uwuclient\\",
        tmp,
    };

    for (auto& base : bases) {
        for (auto* name : NAMES) {
            std::wstring p = base + name;
            DWORD attr = GetFileAttributesW(p.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
                return p;
        }
    }
    return {};
}

static void play_boot_sound_once() {
    if (g_boot_sound_played.exchange(true)) return;
    std::wstring path = find_boot_mp3();
    if (path.empty()) {
        elog::warn("boot: no MP3 found (looked in exe dir, %%TEMP%%\\uwuclient, %%APPDATA%%\\UwUClient)");
        return;
    }
    std::thread([path]() {
        mciSendStringW(L"close uwuboot", nullptr, 0, nullptr);
        std::wstring open_cmd = L"open \"" + path + L"\" type mpegvideo alias uwuboot";
        MCIERROR err = mciSendStringW(open_cmd.c_str(), nullptr, 0, nullptr);
        if (err != 0) {
            elog::warn("boot: mciSendString open failed (err=%lu)", (unsigned long)err);
            return;
        }
        mciSendStringW(L"play uwuboot", nullptr, 0, nullptr);
    }).detach();
}

static void draw_mode_picker() {
    ImGuiIO& io = ImGui::GetIO();

    const float W = 720.f, H = 460.f;
    ImVec2 wp{(io.DisplaySize.x - W) * 0.5f, (io.DisplaySize.y - H) * 0.5f};
    ImGui::SetNextWindowPos(wp, ImGuiCond_Always);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    bool open = true;
    ImGui::Begin("##modepicker", &open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    drop_shadow(ImGui::GetBackgroundDrawList(),
                wp, {wp.x + ws.x, wp.y + ws.y}, 16.f, 40.f, 0.75f);

    if (ImTextureID bgtex = bgimage::srv()) {
        ImVec2 uv0, uv1;
        bgimage::cover_uv(ws.x, ws.y, uv0, uv1);
        dl->AddImageRounded(bgtex, wp, {wp.x + ws.x, wp.y + ws.y},
                            uv0, uv1, IM_COL32_WHITE, 16.f);
        dl->AddRectFilled(wp, {wp.x + ws.x, wp.y + ws.y},
                          IM_COL32(0, 0, 0, 170), 16.f);
    } else {
        dl->AddRectFilledMultiColor(wp, {wp.x + ws.x, wp.y + ws.y},
            C_BG_TOP, C_BG_TOP, C_BG_BOT, C_BG_BOT);
    }
    dl->AddRect(wp, {wp.x + ws.x, wp.y + ws.y}, C_LINE, 16.f, 0, 1.f);

    float pulse = 0.5f + 0.5f * std::sin(uptime_seconds() * 1.4f);
    dl->AddRectFilledMultiColor(wp, {wp.x + ws.x, wp.y + 2.f},
        with_alpha(C_PINK_DEEP, 0.7f), with_alpha(C_PINK, 1.f),
        with_alpha(C_PINK, 1.f),       with_alpha(C_PINK_DEEP, 0.7f));

    if (g_font_display) {
        ImGui::PushFont(g_font_display);
        const char* title = "UwUClient";
        ImVec2 sz = ImGui::CalcTextSize(title);
        ImVec2 tp{wp.x + (ws.x - sz.x) * 0.5f, wp.y + 46.f};
        dl->AddText({tp.x + 2, tp.y + 2}, IM_COL32(0, 0, 0, 180), title);
        dl->AddText(tp, C_PINK, title);
        ImGui::PopFont();
    }

    if (g_font_medium) ImGui::PushFont(g_font_medium);
    const char* sub = "choose access mode";
    ImVec2 subz = ImGui::CalcTextSize(sub);
    dl->AddText({wp.x + (ws.x - subz.x) * 0.5f, wp.y + 130.f}, C_TEXT_MUTE, sub);
    if (g_font_medium) ImGui::PopFont();

    struct Card { const char* name; const char* tag; const char* desc; int mode; bool disabled; };
    Card CARDS[] = {
        {"Usermode",    "recommended",  "standard access mode.\nworks on every game we support.", 0, false},
        {"Kernel Mode", "coming soon",  "driver-based access.\nnot shipped yet - will fall back to usermode.", 1, true},
    };

    const float CW = 300.f, CH = 190.f, GAP = 20.f;
    const float TOTAL = 2 * CW + GAP;
    const float BASE_X = wp.x + (ws.x - TOTAL) * 0.5f;
    const float BASE_Y = wp.y + 180.f;

    for (int i = 0; i < 2; i++) {
        ImVec2 mn{BASE_X + i * (CW + GAP), BASE_Y};
        ImVec2 mx{mn.x + CW, mn.y + CH};
        ImRect r(mn, mx);
        bool hov = hit(r);
        bool press = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        ImGuiID cid = ImGui::GetCurrentWindow()->GetID(CARDS[i].name);
        float th = anim_to(cid, hov ? 1.f : 0.f, 18.f);

        ImU32 bg_l = blend(C_BG_ELEV,   C_BG_ELEV_H, th);
        ImU32 bg_r = blend(IM_COL32(24,24,24,255), C_BG_ELEV_A, th);
        dl->AddRectFilledMultiColor(mn, mx, bg_l, bg_r, bg_r, bg_l);
        ImU32 bd = blend(C_LINE_SOFT, CARDS[i].mode == 1 ? C_PINK : C_ACC, th);
        dl->AddRect(mn, mx, bd, 12.f, 0, 1.5f);

        if (th > 0.02f) {
            for (int g2 = 0; g2 < 6; g2++) {
                float s = (g2 + 1) * 3.f;
                dl->AddRect({mn.x - s, mn.y - s}, {mx.x + s, mx.y + s},
                    with_alpha(CARDS[i].mode == 1 ? C_PINK_GLOW : C_ACC_GLOW_S,
                               th * (1.f - g2 / 6.f) * 0.7f),
                    12.f + s, 0, 1.f);
            }
        }

        dl->AddRectFilledMultiColor({mn.x, mn.y + 14.f}, {mn.x + 3.f, mx.y - 14.f},
            CARDS[i].mode == 1 ? C_PINK_DEEP : C_ACC_DEEP,
            CARDS[i].mode == 1 ? C_PINK_DEEP : C_ACC_DEEP,
            CARDS[i].mode == 1 ? C_PINK      : C_ACC,
            CARDS[i].mode == 1 ? C_PINK      : C_ACC);

        if (g_font_header) ImGui::PushFont(g_font_header);
        dl->AddText({mn.x + 22.f, mn.y + 22.f},
                    CARDS[i].mode == 1 ? C_PINK : C_TEXT, CARDS[i].name);
        if (g_font_header) ImGui::PopFont();

        if (g_font_small) ImGui::PushFont(g_font_small);
        ImVec2 tz = ImGui::CalcTextSize(CARDS[i].tag);
        float tw = tz.x + 16.f, thg = 18.f;
        ImVec2 tmin{mx.x - tw - 16.f, mn.y + 26.f};
        ImVec2 tmax{tmin.x + tw, tmin.y + thg};
        dl->AddRectFilled(tmin, tmax, IM_COL32(24, 24, 24, 255), 9.f);
        dl->AddRect(tmin, tmax, with_alpha(CARDS[i].mode == 1 ? C_PINK : C_ACC, 0.5f), 9.f, 0, 1.f);
        dl->AddText({tmin.x + 8.f, tmin.y + (thg - tz.y) * 0.5f},
                    CARDS[i].mode == 1 ? C_PINK : C_TEXT_MUTE, CARDS[i].tag);
        if (g_font_small) ImGui::PopFont();

        if (g_font_small) ImGui::PushFont(g_font_small);
        float dy = mn.y + 72.f;
        const char* p = CARDS[i].desc;
        while (*p) {
            const char* nl = std::strchr(p, '\n');
            const char* end = nl ? nl : (p + std::strlen(p));
            std::string line(p, end - p);
            dl->AddText({mn.x + 22.f, dy}, C_TEXT_MUTE, line.c_str());
            dy += ImGui::GetFontSize() + 4.f;
            if (!nl) break;
            p = nl + 1;
        }
        if (g_font_small) ImGui::PopFont();

        const char* hint = CARDS[i].disabled ? "click to try  ·  will auto-fallback" : "click to launch";
        if (g_font_small) ImGui::PushFont(g_font_small);
        ImVec2 hz = ImGui::CalcTextSize(hint);
        dl->AddText({mn.x + (CW - hz.x) * 0.5f, mx.y - 22.f},
                    CARDS[i].mode == 1 ? C_PINK_DEEP : C_TEXT_DIM, hint);
        if (g_font_small) ImGui::PopFont();

        if (press) {
            g_access_mode.store(CARDS[i].mode);
            g_kernel_fallback.store(CARDS[i].disabled);
            g_screen.store(Screen::BootTerminal);
            play_boot_sound_once();
        }
    }
    (void)pulse;

    if (g_font_small) ImGui::PushFont(g_font_small);
    const char* credit = "made by q3c on discord";
    ImVec2 cz = ImGui::CalcTextSize(credit);
    dl->AddText({wp.x + (ws.x - cz.x) * 0.5f, wp.y + ws.y - 34.f},
                C_TEXT_DIM, credit);
    if (g_font_small) ImGui::PopFont();

    ImGui::End();
}


struct BootLine {
    const char* text;
    ImU32       col;
    float       delay;
    bool        is_pink_big;
};

static void draw_boot_terminal() {
    ImGuiIO& io = ImGui::GetIO();

    static float t_start = -1.f;
    if (t_start < 0.f) t_start = uptime_seconds();
    float t = uptime_seconds() - t_start;

    const float W = 900.f, H = 620.f;
    ImVec2 wp{(io.DisplaySize.x - W) * 0.5f, (io.DisplaySize.y - H) * 0.5f};
    ImGui::SetNextWindowPos(wp, ImGuiCond_Always);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    bool open = true;
    ImGui::Begin("##bootterm", &open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    drop_shadow(ImGui::GetBackgroundDrawList(),
                wp, {wp.x + ws.x, wp.y + ws.y}, 12.f, 36.f, 0.8f);


    dl->AddRectFilled(wp, {wp.x + ws.x, wp.y + ws.y},
                      IM_COL32(4, 4, 6, 250), 14.f);
    dl->AddRect(wp, {wp.x + ws.x, wp.y + ws.y},
                with_alpha(C_PINK, 0.5f), 14.f, 0, 1.5f);

    for (int y = 0; y < (int)ws.y; y += 3) {
        dl->AddLine({wp.x, wp.y + (float)y},
                    {wp.x + ws.x, wp.y + (float)y},
                    IM_COL32(255, 255, 255, 8), 1.f);
    }

    const float TB = 28.f;
    dl->AddRectFilled(wp, {wp.x + ws.x, wp.y + TB}, IM_COL32(12, 12, 14, 255),
                      14.f, ImDrawFlags_RoundCornersTop);
    dl->AddLine({wp.x, wp.y + TB}, {wp.x + ws.x, wp.y + TB},
                with_alpha(C_PINK, 0.4f));
    dl->AddCircleFilled({wp.x + 16.f, wp.y + TB * 0.5f}, 5.f, IM_COL32(255, 90, 90, 255));
    dl->AddCircleFilled({wp.x + 32.f, wp.y + TB * 0.5f}, 5.f, IM_COL32(240, 200, 90, 255));
    dl->AddCircleFilled({wp.x + 48.f, wp.y + TB * 0.5f}, 5.f, IM_COL32(120, 220, 130, 255));
    if (g_font_small) ImGui::PushFont(g_font_small);
    const char* wintitle = g_access_mode.load() == 1
        ? "uwuclient  —  kernel loader"
        : "uwuclient  —  usermode loader";
    ImVec2 wtz = ImGui::CalcTextSize(wintitle);
    dl->AddText({wp.x + (ws.x - wtz.x) * 0.5f, wp.y + (TB - wtz.y) * 0.5f},
                C_TEXT_MUTE, wintitle);
    if (g_font_small) ImGui::PopFont();

    static const char* BANNER[] = {
        "  _   _        _   _  ____ _ _            _   ",
        " | | | |_ __ _| | | |/ ___| (_) ___ _ __ | |_ ",
        " | | | \\ V  V / |_| | |   | | |/ _ \\ '_ \\| __|",
        " | |_| |\\  ^  /\\___/| |___| | |  __/ | | | |_ ",
        "  \\___/  \\_/\\_/     \\____|_|_|\\___|_| |_|\\__|",
    };
    float by = wp.y + TB + 20.f;
    if (g_font_small) ImGui::PushFont(g_font_small);
    for (auto* line : BANNER) {
        ImVec2 sz = ImGui::CalcTextSize(line);
        dl->AddText({wp.x + (ws.x - sz.x) * 0.5f, by}, C_PINK, line);
        by += ImGui::GetFontSize() + 2.f;
    }
    if (g_font_small) ImGui::PopFont();
    by += 12.f;

    if (g_font_display) {
        ImGui::PushFont(g_font_display);
        const char* w1 = "welcome to uwu client";
        ImVec2 sz = ImGui::CalcTextSize(w1);
        float scale = 1.f;
        if (sz.x > ws.x - 80.f) scale = (ws.x - 80.f) / sz.x;
        float fsz = ImGui::GetFontSize() * scale;
        dl->AddText(ImGui::GetFont(), fsz,
                    {wp.x + (ws.x - sz.x * scale) * 0.5f + 2.f, by + 2.f},
                    IM_COL32(0, 0, 0, 200), w1);
        dl->AddText(ImGui::GetFont(), fsz,
                    {wp.x + (ws.x - sz.x * scale) * 0.5f, by},
                    C_PINK, w1);
        by += fsz + 6.f;
        ImGui::PopFont();
    }

    if (g_font_medium) ImGui::PushFont(g_font_medium);
    const char* credit = "made by q3c on discord";
    ImVec2 cz = ImGui::CalcTextSize(credit);
    dl->AddText({wp.x + (ws.x - cz.x) * 0.5f, by}, C_TEXT_MUTE, credit);
    by += ImGui::GetFontSize() + 22.f;
    if (g_font_medium) ImGui::PopFont();

    dl->AddLine({wp.x + 40.f, by}, {wp.x + ws.x - 40.f, by},
                with_alpha(C_PINK, 0.35f), 1.f);
    by += 18.f;

    bool kernel = g_access_mode.load() == 1;
    bool fell_back = g_kernel_fallback.load();

    struct L { const char* s; ImU32 c; float t; };
    std::vector<L> lines;
    if (kernel && fell_back) {
        lines.push_back({"[  ..  ] locating vulnerable driver ...",       C_TEXT_MUTE, 0.20f});
        lines.push_back({"[ FAIL ] driver bundle not present in build",   C_BAD,       0.90f});
        lines.push_back({"[ warn ] falling back to usermode access",      C_TERM_AMB,  1.50f});
    } else if (kernel) {
        lines.push_back({"[  ..  ] locating vulnerable driver ...",       C_TEXT_MUTE, 0.20f});
        lines.push_back({"[  ok  ] mapping driver",                       C_TERM_GRN,  0.90f});
    } else {
        lines.push_back({"[  ..  ] resolving Roblox process ...",         C_TEXT_MUTE, 0.20f});
        lines.push_back({"[  ok  ] usermode access initialized",          C_TERM_GRN,  0.90f});
    }

    static char attach_line[128]   = "[  ..  ] attaching to pid ...";
    static char scan_line[128]     = "[  ..  ] scanning offsets ...";
    static char attached_line[128] = "[  ok  ] attached!";
    if (mem::alive()) {
        std::snprintf(attach_line, sizeof(attach_line),
                      "[  ok  ] attached to pid %lu", (unsigned long)mem::g_pid);
    }
    if (g_scan_ok.load()) {
        std::snprintf(scan_line, sizeof(scan_line),
                      "[  ok  ] offset table loaded");
    }
    lines.push_back({attach_line, mem::alive() ? C_TERM_GRN : C_TEXT_MUTE, 1.90f});
    lines.push_back({scan_line,   g_scan_ok.load() ? C_TERM_GRN : C_TEXT_MUTE, 2.60f});
    if (g_scan_ok.load())
        lines.push_back({attached_line, C_TERM_GRN, 3.20f});

    if (g_font_medium) ImGui::PushFont(g_font_medium);
    float px = wp.x + 60.f;
    for (auto& L : lines) {
        if (t < L.t) break;
        float a = std::min(1.f, (t - L.t) * 4.f);
        dl->AddText({px, by}, with_alpha(L.c, a), L.s);
        by += ImGui::GetFontSize() + 8.f;
    }

    float blink = 0.5f + 0.5f * std::sin(uptime_seconds() * 5.f);
    dl->AddRectFilled({px - 14.f, by - ImGui::GetFontSize() - 4.f},
                      {px - 8.f,  by - 4.f},
                      with_alpha(C_PINK, 0.4f + 0.6f * blink));
    if (g_font_medium) ImGui::PopFont();

    if (g_font_small) ImGui::PushFont(g_font_small);
    bool ready = g_scan_ok.load();
    const char* hint =
        ready       ? "press SPACE or click to select a game"
      : mem::alive()? "scanning offsets ..."
                    : "waiting for Roblox ...";
    ImVec2 hz = ImGui::CalcTextSize(hint);
    dl->AddText({wp.x + (ws.x - hz.x) * 0.5f, wp.y + ws.y - 26.f},
                ready ? C_PINK : C_TEXT_DIM, hint);
    if (g_font_small) ImGui::PopFont();

    bool wants_advance =
        ready && (
            ImGui::IsKeyPressed(ImGuiKey_Space) ||
            ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
             hit(ImRect(wp, {wp.x + ws.x, wp.y + ws.y})))
        );
    if (ready && t > 4.0f) wants_advance = true;
    if (wants_advance) {
        t_start = -1.f;
        g_screen.store(Screen::Launcher);
    }

    ImGui::End();
}

static void draw_launcher() {
    ImGuiIO& io = ImGui::GetIO();

    const float W = 900.f, H = 620.f;
    ImVec2 wp{(io.DisplaySize.x - W) * 0.5f, (io.DisplaySize.y - H) * 0.5f};
    ImGui::SetNextWindowPos(wp, ImGuiCond_Always);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    bool open = true;
    ImGui::Begin("##launcher", &open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    drop_shadow(ImGui::GetBackgroundDrawList(),
                wp, {wp.x + ws.x, wp.y + ws.y}, 14.f, 40.f, 0.85f);

    dl->AddRectFilled(wp, {wp.x + ws.x, wp.y + ws.y},
                      IM_COL32(4, 4, 6, 250), 14.f);
    dl->AddRect(wp, {wp.x + ws.x, wp.y + ws.y},
                with_alpha(C_PINK, 0.5f), 14.f, 0, 1.5f);

    for (int y = 0; y < (int)ws.y; y += 3) {
        dl->AddLine({wp.x, wp.y + (float)y},
                    {wp.x + ws.x, wp.y + (float)y},
                    IM_COL32(255, 255, 255, 8), 1.f);
    }

    const float TB = 30.f;
    dl->AddRectFilled(wp, {wp.x + ws.x, wp.y + TB},
                      IM_COL32(12, 12, 14, 255),
                      14.f, ImDrawFlags_RoundCornersTop);
    dl->AddLine({wp.x, wp.y + TB}, {wp.x + ws.x, wp.y + TB},
                with_alpha(C_PINK, 0.4f));

    ImVec2 red_c{wp.x + 18.f, wp.y + TB * 0.5f};
    ImVec2 yel_c{wp.x + 36.f, wp.y + TB * 0.5f};
    ImVec2 grn_c{wp.x + 54.f, wp.y + TB * 0.5f};
    bool red_hov = hit(ImRect({red_c.x - 6, red_c.y - 6}, {red_c.x + 6, red_c.y + 6}));
    dl->AddCircleFilled(red_c, 5.5f,
        red_hov ? IM_COL32(255, 130, 130, 255) : IM_COL32(255, 90, 90, 255));
    dl->AddCircleFilled(yel_c, 5.5f, IM_COL32(240, 200, 90, 255));
    dl->AddCircleFilled(grn_c, 5.5f, IM_COL32(120, 220, 130, 255));
    if (red_hov) {
        dl->AddLine({red_c.x - 2.5f, red_c.y - 2.5f}, {red_c.x + 2.5f, red_c.y + 2.5f},
                    IM_COL32(80, 20, 20, 255), 1.f);
        dl->AddLine({red_c.x - 2.5f, red_c.y + 2.5f}, {red_c.x + 2.5f, red_c.y - 2.5f},
                    IM_COL32(80, 20, 20, 255), 1.f);
    }
    if (red_hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        PostQuitMessage(0);
    }

    if (g_font_small) ImGui::PushFont(g_font_small);
    const char* wintitle = "uwuclient  —  select game";
    ImVec2 wtz = ImGui::CalcTextSize(wintitle);
    dl->AddText({wp.x + (ws.x - wtz.x) * 0.5f, wp.y + (TB - wtz.y) * 0.5f},
                C_TEXT_MUTE, wintitle);
    if (g_font_small) ImGui::PopFont();

    ImGui::SetCursorScreenPos(wp);
    ImGui::InvisibleButton("##ldrag", {ws.x - 80.f, TB});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 md = io.MouseDelta;
        ImGui::SetWindowPos({wp.x + md.x, wp.y + md.y});
    }

    dl->AddRectFilledMultiColor({wp.x, wp.y + TB - 2.f}, {wp.x + ws.x, wp.y + TB},
        with_alpha(C_PINK_DEEP, 0.5f), with_alpha(C_PINK, 0.9f),
        with_alpha(C_PINK, 0.9f),      with_alpha(C_PINK_DEEP, 0.5f));

    bool alive = mem::alive();
    float pulse = 0.5f + 0.5f * std::sin(uptime_seconds() * 1.4f);

    if (g_font_header) ImGui::PushFont(g_font_header);
    const char* h1 = "select a game";
    ImVec2 h1z = ImGui::CalcTextSize(h1);
    float head_y = wp.y + TB + 24.f;
    dl->AddText({wp.x + (ws.x - h1z.x) * 0.5f + 1.f, head_y + 1.f},
                IM_COL32(0, 0, 0, 200), h1);
    dl->AddText({wp.x + (ws.x - h1z.x) * 0.5f, head_y}, C_PINK, h1);
    if (g_font_header) ImGui::PopFont();

    if (g_font_small) ImGui::PushFont(g_font_small);
    const char* h2 = "click a card — you'll drop straight into the menu";
    ImVec2 h2z = ImGui::CalcTextSize(h2);
    dl->AddText({wp.x + (ws.x - h2z.x) * 0.5f, head_y + h1z.y + 4.f},
                C_TEXT_DIM, h2);
    if (g_font_small) ImGui::PopFont();

    if (g_font_small) ImGui::PushFont(g_font_small);
    char pill[64];
    std::snprintf(pill, sizeof(pill),
                  alive ? "attached · pid %lu" : "waiting for Roblox",
                  (unsigned long)mem::g_pid);
    ImU32 dot_col = alive ? C_TERM_GRN : IM_COL32(240, 200, 90, 255);
    ImVec2 pz = ImGui::CalcTextSize(pill);
    float pill_w = pz.x + 34.f, pill_h = 22.f;
    ImRect pr({wp.x + ws.x - pill_w - 20.f, wp.y + TB + 12.f},
              {wp.x + ws.x - 20.f,          wp.y + TB + 12.f + pill_h});
    dl->AddRectFilled(pr.Min, pr.Max, IM_COL32(16, 16, 18, 255), 11.f);
    dl->AddRect(pr.Min, pr.Max, with_alpha(C_PINK, 0.35f), 11.f, 0, 1.f);
    ImVec2 dc{pr.Min.x + 12.f, pr.Min.y + pill_h * 0.5f};
    dl->AddCircleFilled(dc, 3.5f, dot_col);
    dl->AddCircle(dc, 5.5f, with_alpha(dot_col, 0.35f + 0.35f * pulse), 0, 1.f);
    dl->AddText({pr.Min.x + 22.f, pr.Min.y + (pill_h - pz.y) * 0.5f},
                C_TEXT_MUTE, pill);
    if (g_font_small) ImGui::PopFont();

    static bool detected_once = false;
    static games::Id detected = games::Id::Universal;
    if (!detected_once && alive) {
        detected = games::detect_from_place();
        detected_once = true;
        if (detected != games::Id::Universal &&
            esp::cfg.selected_game == (int)games::Id::Universal) {
            esp::cfg.selected_game = (int)detected;
        }
    }


    struct GameCard { const char* name; const char* tag; int icon; };
    static const GameCard CARDS[] = {
        {"Universal",     "any game",              8},
        {"Arsenal",       "aim + weapon mods",     0},
        {"Flick",         "aim/ESP only",          4},
        {"Da Hood",       "silent aim",            6},
        {"Bad Business",  "weapon mods",           0},
        {"Phantom Forces","subtle mods",           0},
        {"BIG Paintball", "weapon mods",           0},
        {"Counter Blox",  "weapon mods",           0},
        {"KAT",           "silent knife",          4},
        {"Assassin!",     "silent knife",          4},
        {"Strongest BG",  "movement preset",       6},
        {"Blox Fruits",   "long-range preset",     8},
        {"Rivals",        "viewport silent aim",   6},
    };
    constexpr int N_CARDS = (int)(sizeof(CARDS) / sizeof(CARDS[0]));
    const int   COLS = 4;
    const float GAP  = 12.f;
    const float SIDE_PAD = 32.f;
    const float CW   = (ws.x - SIDE_PAD * 2.f - (COLS - 1) * GAP) / (float)COLS;
    const float CH   = 82.f;
    const float TOP  = head_y + h1z.y + 44.f;

    const int LAST_ROW_N   = ((N_CARDS - 1) % COLS) + 1;
    const int LAST_ROW_TOP = N_CARDS - LAST_ROW_N;
    const float ROW_W_FULL = COLS * CW + (COLS - 1) * GAP;
    const float ROW_W_LAST = LAST_ROW_N * CW + (LAST_ROW_N - 1) * GAP;
    const float LAST_OFFSET = (ROW_W_FULL - ROW_W_LAST) * 0.5f;

    for (int i = 0; i < N_CARDS; i++) {
        int col = i % COLS;
        int row = i / COLS;
        float x_off = (i >= LAST_ROW_TOP) ? LAST_OFFSET : 0.f;
        ImVec2 mn{wp.x + SIDE_PAD + x_off + col * (CW + GAP),
                  TOP              + row  * (CH + GAP)};
        ImVec2 mx{mn.x + CW, mn.y + CH};
        ImRect r(mn, mx);
        bool hov = alive && hit(r);
        bool sel = (esp::cfg.selected_game == i);
        ImGuiID cid = ImGui::GetCurrentWindow()->GetID(CARDS[i].name);
        float thov = anim_to(cid, hov ? 1.f : 0.f, 18.f);
        float tsel = anim_to(cid ^ 0xBEEB, sel ? 1.f : 0.f, 20.f);

        ImU32 bg_l = blend(IM_COL32(14, 14, 18, 230), IM_COL32(28, 28, 34, 250), thov);
        ImU32 bg_r = blend(IM_COL32(20, 20, 24, 230), IM_COL32(38, 38, 46, 250), thov);
        dl->AddRectFilledMultiColor(mn, mx, bg_l, bg_r, bg_r, bg_l);
        ImU32 bd = sel ? blend(C_LINE, C_PINK, tsel)
                       : blend(C_LINE_SOFT, with_alpha(C_PINK, 0.5f), thov);
        dl->AddRect(mn, mx, bd, 10.f, 0, 1.f);

        if (tsel > 0.02f) {
            for (int g2 = 0; g2 < 5; g2++) {
                float s = (g2 + 1) * 3.f;
                dl->AddRect({mn.x - s, mn.y - s}, {mx.x + s, mx.y + s},
                            with_alpha(C_PINK_GLOW, tsel * (1.f - g2 / 5.f) * 0.85f),
                            10.f + s, 0, 1.f);
            }
            dl->AddRectFilledMultiColor({mn.x, mn.y + 10.f},
                                        {mn.x + 3.f, mx.y - 10.f},
                                        C_PINK_DEEP, C_PINK_DEEP, C_PINK, C_PINK);
        }

        draw_icon(dl, CARDS[i].icon, {mn.x + 22.f, mn.y + 24.f}, 11.f,
                  sel ? C_PINK : blend(C_TEXT_MUTE, C_TEXT, std::max(thov, tsel)),
                  sel ? 1.9f : 1.5f);

        if (g_font_medium) ImGui::PushFont(g_font_medium);
        dl->AddText({mn.x + 44.f, mn.y + 14.f},
                    sel ? C_TEXT : blend(C_TEXT_MUTE, C_TEXT, thov), CARDS[i].name);
        if (g_font_medium) ImGui::PopFont();

        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({mn.x + 44.f, mn.y + 36.f}, C_TEXT_DIM, CARDS[i].tag);
        if (g_font_small) ImGui::PopFont();

        if (sel) {
            ImVec2 kc{mx.x - 16.f, mx.y - 16.f};
            dl->AddCircleFilled(kc, 7.f, C_PINK);
            dl->AddLine({kc.x - 3.f, kc.y}, {kc.x - 0.5f, kc.y + 3.f}, C_INK, 1.6f);
            dl->AddLine({kc.x - 0.5f, kc.y + 3.f}, {kc.x + 4.f, kc.y - 2.5f}, C_INK, 1.6f);
        }

        if (detected_once && detected != games::Id::Universal &&
            i == (int)detected && !sel) {
            if (g_font_small) ImGui::PushFont(g_font_small);
            const char* det = "detected";
            ImVec2 dsz = ImGui::CalcTextSize(det);
            float dw = dsz.x + 14.f, dh = 15.f;
            ImVec2 dmin{mx.x - dw - 8.f, mn.y + 8.f};
            ImVec2 dmax{dmin.x + dw, dmin.y + dh};
            dl->AddRectFilled(dmin, dmax, IM_COL32(22, 22, 26, 255), 7.f);
            dl->AddRect(dmin, dmax, with_alpha(C_PINK, 0.6f), 7.f, 0, 1.f);
            dl->AddText({dmin.x + 7.f, dmin.y + (dh - dsz.y) * 0.5f},
                        C_PINK, det);
            if (g_font_small) ImGui::PopFont();
        }

        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            esp::cfg.selected_game = i;
            g_injected.store(true);
            g_visible.store(false);
            g_screen.store(Screen::Menu);
            games::lock_from_cfg();
            char msg[64];
            std::snprintf(msg, sizeof(msg),
                          "UwUClient ready — %s — RShift to open",
                          games::name_of(games::active()));
            esp::notify(msg);
        }
    }

    if (g_font_small) ImGui::PushFont(g_font_small);
    const char* credit = "made by q3c on discord  ·  RShift to open once in-game";
    ImVec2 cz = ImGui::CalcTextSize(credit);
    dl->AddText({wp.x + (ws.x - cz.x) * 0.5f, wp.y + ws.y - 26.f},
                C_TEXT_DIM, credit);
    if (g_font_small) ImGui::PopFont();

    ImGui::End();
}
}

namespace menu {

void init() {
    ImGuiIO& io = ImGui::GetIO();

    io.Fonts->Clear();
    ImFontConfig cfg; cfg.FontDataOwnedByAtlas = false;

    g_font_body    = io.Fonts->AddFontFromMemoryTTF((void*)rawData, (int)sizeof(rawData), 15.f, &cfg);
    g_font_small   = io.Fonts->AddFontFromMemoryTTF((void*)rawData, (int)sizeof(rawData), 12.f, &cfg);
    g_font_medium  = io.Fonts->AddFontFromMemoryTTF((void*)rawData, (int)sizeof(rawData), 17.f, &cfg);
    g_font_header  = io.Fonts->AddFontFromMemoryTTF((void*)rawData, (int)sizeof(rawData), 24.f, &cfg);
    g_font_display = io.Fonts->AddFontFromMemoryTTF((void*)rawData, (int)sizeof(rawData), 56.f, &cfg);
    if (!g_font_body) io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui_ImplDX11_InvalidateDeviceObjects();

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 12.f;
    s.ChildRounding     = 8.f;
    s.FrameRounding     = 6.f;
    s.GrabRounding      = 6.f;
    s.WindowBorderSize  = 1.f;
    s.FrameBorderSize   = 0.f;
    s.WindowPadding     = {0, 0};
    s.ItemSpacing       = {0, 0};
    s.ScrollbarRounding = 6.f;
    s.ScrollbarSize     = 9.f;

    auto& c = s.Colors;
    c[ImGuiCol_Text]           = ImGui::ColorConvertU32ToFloat4(C_TEXT);
    c[ImGuiCol_TextDisabled]   = ImGui::ColorConvertU32ToFloat4(C_TEXT_DIM);
    c[ImGuiCol_WindowBg]       = ImGui::ColorConvertU32ToFloat4(C_BG_TOP);
    c[ImGuiCol_ChildBg]        = {0, 0, 0, 0};
    c[ImGuiCol_Border]         = ImGui::ColorConvertU32ToFloat4(C_LINE);
    c[ImGuiCol_ScrollbarBg]    = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab]  = ImGui::ColorConvertU32ToFloat4(IM_COL32(52, 52, 52, 255));
    c[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(80, 80, 80, 255));
    c[ImGuiCol_ScrollbarGrabActive]  = ImGui::ColorConvertU32ToFloat4(C_ACC);
}

void set_visible(bool v) {
    if (!g_injected.load()) return;
    g_visible.store(v);
}
bool is_visible()         { return g_visible.load(); }
bool is_injected()        { return g_injected.load(); }
void set_scan_ok(bool ok) { g_scan_ok.store(ok); }

void render(const std::vector<PlayerInfo>* players, uintptr_t render_view) {
    (void)render_view;

    Screen s = g_screen.load();
    if (s == Screen::ModePicker) {
        draw_mode_picker();
        return;
    }
    if (s == Screen::BootTerminal) {
        draw_boot_terminal();
        return;
    }
    if (s == Screen::Launcher) {
        draw_launcher();
        return;
    }

    if (!g_visible.load()) return;

    ImGuiIO& io = ImGui::GetIO();

    const float W = 1000.f, H = 640.f;
    ImGui::SetNextWindowPos({(io.DisplaySize.x - W) * 0.5f, (io.DisplaySize.y - H) * 0.5f},
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    bool open = true;
    ImGui::Begin("UwUClient", &open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    g_main_pos  = ImGui::GetWindowPos();
    g_main_size = ImGui::GetWindowSize();

    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    drop_shadow(ImGui::GetBackgroundDrawList(),
                wp, {wp.x + ws.x, wp.y + ws.y}, 14.f, 40.f, 0.85f);

    dl->AddRectFilled(wp, {wp.x + ws.x, wp.y + ws.y},
                      IM_COL32(4, 4, 6, 250), 14.f);
    dl->AddRect(wp, {wp.x + ws.x, wp.y + ws.y},
                with_alpha(C_PINK, 0.5f), 14.f, 0, 1.5f);

    for (int y = 0; y < (int)ws.y; y += 3) {
        dl->AddLine({wp.x, wp.y + (float)y},
                    {wp.x + ws.x, wp.y + (float)y},
                    IM_COL32(255, 255, 255, 8), 1.f);
    }

    const float TB = 46.f;
    dl->AddRectFilled(wp, {wp.x + ws.x, wp.y + TB},
                      IM_COL32(12, 12, 14, 255),
                      14.f, ImDrawFlags_RoundCornersTop);
    dl->AddLine({wp.x, wp.y + TB}, {wp.x + ws.x, wp.y + TB},
                with_alpha(C_PINK, 0.4f));

    dl->AddRectFilledMultiColor({wp.x, wp.y + TB - 2.f}, {wp.x + ws.x, wp.y + TB},
        with_alpha(C_PINK_DEEP, 0.5f), with_alpha(C_PINK, 0.9f),
        with_alpha(C_PINK, 0.9f),      with_alpha(C_PINK_DEEP, 0.5f));

    ImVec2 red_c{wp.x + 20.f, wp.y + TB * 0.5f};
    ImVec2 yel_c{wp.x + 40.f, wp.y + TB * 0.5f};
    ImVec2 grn_c{wp.x + 60.f, wp.y + TB * 0.5f};
    bool red_hov = hit(ImRect({red_c.x - 7, red_c.y - 7}, {red_c.x + 7, red_c.y + 7}));
    dl->AddCircleFilled(red_c, 6.f,
        red_hov ? IM_COL32(255, 130, 130, 255) : IM_COL32(255, 90, 90, 255));
    dl->AddCircleFilled(yel_c, 6.f, IM_COL32(240, 200, 90, 255));
    dl->AddCircleFilled(grn_c, 6.f, IM_COL32(120, 220, 130, 255));
    if (red_hov) {
        dl->AddLine({red_c.x - 3, red_c.y - 3}, {red_c.x + 3, red_c.y + 3},
                    IM_COL32(80, 20, 20, 255), 1.2f);
        dl->AddLine({red_c.x - 3, red_c.y + 3}, {red_c.x + 3, red_c.y - 3},
                    IM_COL32(80, 20, 20, 255), 1.2f);
    }
    if (red_hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        g_visible.store(false);
    }

    games::Id gid = games::active();
    char titlebuf[128];
    std::snprintf(titlebuf, sizeof(titlebuf),
                  "uwuclient  —  %s", games::name_of(gid));
    if (g_font_medium) ImGui::PushFont(g_font_medium);
    ImVec2 tsz = ImGui::CalcTextSize(titlebuf);
    dl->AddText({wp.x + (ws.x - tsz.x) * 0.5f,
                 wp.y + (TB - ImGui::GetFontSize()) * 0.5f - 1.f},
                C_TEXT, titlebuf);
    if (g_font_medium) ImGui::PopFont();

    char status[96];
    std::snprintf(status, sizeof(status),
                  mem::alive() ? "pid %lu  ·  %.0f fps"
                               : "detached  ·  %.0f fps",
                  mem::alive() ? (unsigned long)mem::g_pid : (unsigned long)io.Framerate,
                  io.Framerate);
    if (g_font_small) ImGui::PushFont(g_font_small);
    ImVec2 stz = ImGui::CalcTextSize(status);
    float sp_w = stz.x + 20.f, sp_h = 20.f;
    ImRect sr({wp.x + ws.x - sp_w - 20.f, wp.y + (TB - sp_h) * 0.5f},
              {wp.x + ws.x - 20.f,        wp.y + (TB + sp_h) * 0.5f});
    dl->AddRectFilled(sr.Min, sr.Max, IM_COL32(18, 18, 20, 255), 10.f);
    dl->AddRect(sr.Min, sr.Max, with_alpha(C_PINK, 0.4f), 10.f, 0, 1.f);
    dl->AddText({sr.Min.x + 10.f, sr.Min.y + (sp_h - stz.y) * 0.5f},
                C_TEXT_MUTE, status);
    if (g_font_small) ImGui::PopFont();

    ImGui::SetCursorScreenPos({wp.x + 80.f, wp.y});
    ImGui::InvisibleButton("##titlebar", {ws.x - 80.f - sp_w - 30.f, TB});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 md = io.MouseDelta;
        ImGui::SetWindowPos({wp.x + md.x, wp.y + md.y});
    }

    const float SB_W = 224.f;

    dl->AddRectFilledMultiColor({wp.x, wp.y + TB},
                                {wp.x + SB_W, wp.y + ws.y},
        C_BG_SIDE_T, C_BG_SIDE_T, C_BG_SIDE_B, C_BG_SIDE_B);
    dl->AddLine({wp.x + SB_W, wp.y + TB}, {wp.x + SB_W, wp.y + ws.y}, C_LINE_HAIR);

    const float NAV_X   = wp.x + 12.f;
    const float NAV_W   = SB_W - 24.f;
    const float NAV_H   = 40.f;
    const float NAV_GAP = 5.f;

    float active_target_y = wp.y + TB + 14.f + g_active_cat * (NAV_H + NAV_GAP);
    ImGuiID ind_id = ImGui::GetCurrentWindow()->GetID("nav_ind");
    float active_y = anim_to(ind_id, active_target_y, 22.f);

    dl->AddRectFilled({NAV_X, active_y}, {NAV_X + NAV_W, active_y + NAV_H}, C_BG_ELEV_H, 8.f);
    dl->AddRect({NAV_X, active_y}, {NAV_X + NAV_W, active_y + NAV_H}, with_alpha(C_ACC, 0.25f), 8.f, 0, 1.f);

    dl->AddRectFilledMultiColor(
        {NAV_X, active_y + 8.f}, {NAV_X + 3.f, active_y + NAV_H - 8.f},
        C_ACC_DEEP, C_ACC_DEEP, C_ACC, C_ACC);
    for (int i = 0; i < 4; i++) {
        float s = (i + 1) * 3.f;
        dl->AddRectFilled({NAV_X - s, active_y + 8.f - s * 0.4f},
                          {NAV_X + 3.f, active_y + NAV_H - 8.f + s * 0.4f},
                          with_alpha(C_ACC_GLOW_S, (1.f - i / 4.f) * 0.55f), 3.f);
    }

    for (int i = 0; i < CAT_COUNT; i++) {
        float ny = wp.y + TB + 14.f + i * (NAV_H + NAV_GAP);
        ImRect r({NAV_X, ny}, {NAV_X + NAV_W, ny + NAV_H});
        bool hovered = hit(r);
        bool press   = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool active  = (g_active_cat == i);
        if (press) g_active_cat = i;

        ImGuiID nid = ImGui::GetCurrentWindow()->GetID(("nav" + std::to_string(i)).c_str());
        float hth = anim_to(nid, (hovered && !active) ? 1.f : 0.f, 18.f);
        if (hth > 0.02f) {
            dl->AddRectFilled(r.Min, r.Max, with_alpha(C_BG_ELEV, hth * 0.85f), 8.f);
        }

        ImVec2 ic{r.Min.x + 20.f, r.Min.y + NAV_H * 0.5f};
        ImU32 icol = active ? C_ACC : blend(C_TEXT_DIM, C_TEXT_MUTE, hth);
        draw_icon(dl, i, ic, 9.f, icol, active ? 1.7f : 1.4f);

        if (g_font_medium) ImGui::PushFont(g_font_medium);
        ImU32 tcol = active ? C_TEXT : blend(C_TEXT_MUTE, C_TEXT, hth);
        dl->AddText({r.Min.x + 38.f, r.Min.y + (NAV_H - ImGui::GetFontSize()) * 0.5f - 1.f},
                    tcol, CATS[i].name);
        if (g_font_medium) ImGui::PopFont();

        int oncount = 0;
        for (int m = 0; m < CATS[i].module_count; m++)
            if (CATS[i].modules[m].enabled && *CATS[i].modules[m].enabled) oncount++;
        if (oncount > 0) {
            char cb[8]; std::snprintf(cb, sizeof(cb), "%d", oncount);
            ImVec2 sz;
            if (g_font_small) ImGui::PushFont(g_font_small);
            sz = ImGui::CalcTextSize(cb);
            float bw = sz.x + 10.f;
            ImRect bpx({r.Max.x - bw - 8.f, r.Min.y + (NAV_H - 16.f) * 0.5f},
                       {r.Max.x - 8.f,      r.Min.y + (NAV_H + 16.f) * 0.5f});
            ImU32 bgc = active ? C_ACC : IM_COL32(24, 24, 24, 255);
            ImU32 fgc = active ? C_INK : C_TEXT_MUTE;
            dl->AddRectFilled(bpx.Min, bpx.Max, bgc, 8.f);
            if (!active) dl->AddRect(bpx.Min, bpx.Max, C_LINE_HAIR, 8.f, 0, 1.f);
            dl->AddText({bpx.Min.x + (bw - sz.x) * 0.5f, bpx.Min.y + (16.f - sz.y) * 0.5f + 1.f},
                        fgc, cb);
            if (g_font_small) ImGui::PopFont();
        }
    }

    float footer_y = wp.y + ws.y - 32.f;
    if (g_font_small) ImGui::PushFont(g_font_small);
    dl->AddText({wp.x + 14.f, footer_y}, C_TEXT_DIM, "RShift  toggle");
    dl->AddText({wp.x + 14.f, footer_y + 12.f}, C_TEXT_DIM, "END     save + quit");
    if (g_font_small) ImGui::PopFont();

    ImVec2 cont_min{wp.x + SB_W + 20.f, wp.y + TB + 18.f};
    ImVec2 cont_max{wp.x + ws.x - 20.f, wp.y + ws.y - 20.f};

    if (g_font_header) ImGui::PushFont(g_font_header);
    dl->AddText(cont_min, C_TEXT, CATS[g_active_cat].name);
    ImVec2 hsz = ImGui::CalcTextSize(CATS[g_active_cat].name);
    if (g_font_header) ImGui::PopFont();

    dl->AddRectFilledMultiColor(
        {cont_min.x, cont_min.y + hsz.y + 3.f},
        {cont_min.x + 24.f, cont_min.y + hsz.y + 5.5f},
        C_ACC_DEEP, C_ACC, C_ACC, C_ACC_DEEP);

    char sub[96];
    if (g_active_cat == CAT_PLAYERS_IX) {
        int n = players ? (int)players->size() : 0;
        std::snprintf(sub, sizeof(sub), "%d in game", n);
    } else if (g_active_cat == CAT_PROFILES_IX) {
        int n = 0;
        std::error_code ec;
        for (auto& e : prof_fs::directory_iterator(profiles_dir(), ec)) {
            if (ec) break;
            if (e.is_regular_file(ec) && e.path().extension() == ".cfg") n++;
        }
        if (!g_profiles_active.empty())
            std::snprintf(sub, sizeof(sub), "%d saved  ·  active: %s", n, g_profiles_active.c_str());
        else
            std::snprintf(sub, sizeof(sub), "%d saved", n);
    } else if (g_active_cat == CAT_EXPLORER_IX) {
        std::snprintf(sub, sizeof(sub), "live DataModel");
    } else {
        std::snprintf(sub, sizeof(sub), "%d modules", CATS[g_active_cat].module_count);
    }
    if (g_font_small) ImGui::PushFont(g_font_small);
    ImVec2 subz = ImGui::CalcTextSize(sub);
    dl->AddText({cont_max.x - subz.x, cont_min.y + 8.f}, C_TEXT_DIM, sub);
    if (g_font_small) ImGui::PopFont();

    if (g_active_cat == CAT_PLAYERS_IX) {
        ImVec2 pmin{cont_min.x, cont_min.y + hsz.y + 12.f};
        draw_players_pane(players, pmin, cont_max);
    } else if (g_active_cat == CAT_PROFILES_IX) {
        ImVec2 pmin{cont_min.x, cont_min.y + hsz.y + 12.f};
        draw_profiles_pane(pmin, cont_max);
    } else if (g_active_cat == CAT_EXPLORER_IX) {
        ImVec2 pmin{cont_min.x, cont_min.y + hsz.y + 12.f};
        draw_explorer_pane(pmin, cont_max);
    } else {

        ImVec2 sbp{cont_min.x, cont_min.y + hsz.y + 14.f};
        float sbw = cont_max.x - cont_min.x;
        float sbh = 30.f;
        dl->AddRectFilled(sbp, {sbp.x + sbw, sbp.y + sbh}, IM_COL32(14, 14, 14, 255), 8.f);
        dl->AddRect(sbp, {sbp.x + sbw, sbp.y + sbh}, C_LINE_SOFT, 8.f, 0, 1.f);
        ImVec2 mc{sbp.x + 12.f, sbp.y + sbh * 0.5f};
        dl->AddCircle(mc, 5.f, C_TEXT_MUTE, 0, 1.5f);
        dl->AddLine({mc.x + 3.5f, mc.y + 3.5f}, {mc.x + 8.f, mc.y + 8.f}, C_TEXT_MUTE, 1.5f);

        ImGui::SetCursorScreenPos({sbp.x + 26.f, sbp.y + (sbh - ImGui::GetFontSize()) * 0.5f - 1.f});
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushItemWidth(sbw - 36.f);
        ImGui::PushID(g_active_cat);
        ImGui::InputTextWithHint("##catfilter", "Search this tab…",
            g_filter_buf[g_active_cat], IM_ARRAYSIZE(g_filter_buf[g_active_cat]));
        ImGui::PopID();
        ImGui::PopItemWidth();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        ImVec2 list_pos{cont_min.x, sbp.y + sbh + 10.f};
        ImVec2 list_size(cont_max.x - list_pos.x, cont_max.y - list_pos.y);
        ImGui::SetCursorScreenPos(list_pos);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##mods", list_size, false,
            ImGuiWindowFlags_NoBackground);

        std::string fs = g_filter_buf[g_active_cat];
        for (auto& c : fs) c = (char)std::tolower((unsigned char)c);

        const Category& cat = CATS[g_active_cat];
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 8});
        for (int i = 0; i < cat.module_count; i++) {
            const Module& m = cat.modules[i];
            if (!fs.empty()) {
                std::string lower = m.name;
                for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
                if (lower.find(fs) == std::string::npos) continue;
            }
            if (module_row(m.name, m.enabled, m.body_count > 0)) {
                g_popup.mod = &m;
                g_popup.anchor = ImGui::GetMousePos();
                g_popup.just_opened = true;
                g_popup.cat_ix = g_active_cat;
            }
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::End();

    draw_popup();

    poll_keybind_listen();

    (void)players;
    if (!open) g_visible.store(false);
}

}
