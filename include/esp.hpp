#pragma once
#include <d3d11.h>
#include <imgui.h>
#include "math.hpp"
#include "roblox.hpp"

namespace esp {

    struct Config {
        bool  enabled       = true;
        int   box_style     = 1;
        bool  draw_filled   = false;
        bool  draw_name     = true;
        bool  draw_health   = true;
        bool  draw_health_text = false;
        bool  draw_distance = true;
        bool  draw_line     = false;
        int   snapline_pos  = 0;
        bool  draw_head_dot = false;
        bool  text_shadow   = true;
        bool  fullbright    = false;
        bool  no_skybox     = false;
        float box_thickness = 1.5f;
        float max_dist      = 10000.f;
        float accent[3]     = {1.0f, 1.0f, 1.0f};
        ImVec4 col_box      = {0.00f, 1.00f, 0.35f, 1.f};
        ImVec4 col_box_fill = {0.00f, 1.00f, 0.35f, 0.2f};
        ImVec4 col_name     = {1.00f, 1.00f, 1.00f, 1.f};
        ImVec4 col_line     = {1.00f, 0.25f, 0.25f, 1.f};
        ImVec4 col_head     = {1.00f, 1.00f, 1.00f, 1.f};

        bool  draw_fov      = false;
        float fov_radius    = 100.f;
        ImVec4 col_fov      = {1.00f, 1.00f, 1.00f, 0.30f};

        bool  draw_skeleton = false;
        ImVec4 col_skeleton = {1.00f, 1.00f, 1.00f, 1.00f};

        bool  aim_enabled   = false;
        int   aim_key       = 0x02;
        float aim_strength  = 100.f;
        float aim_smooth    = 1.0f;
        bool  aim_lock      = true;
        float aim_lock_div  = 3.0f;
        int   aim_bone      = 0;
        bool  aim_sticky    = false;
        bool  team_check    = false;
        bool  aim_visibility_check = false;
        bool  aim_jitter    = false;
        float aim_jitter_strength = 0.5f;
        int   aim_priority  = 0;
        bool  aim_predict   = true;
        float aim_prediction = 0.10f;
        bool  aim_multibone = false;
        bool  aim_autofire  = false;
        float aim_autofire_fov = 4.f;

        bool  trigger_enabled = false;
        int   trigger_key   = 0x06;
        float trigger_fov   = 6.f;
        int   trigger_delay = 40;

        bool  speed_enabled = false;
        float speed_value   = 16.f;
        bool  jump_enabled  = false;
        float jump_value    = 50.f;
        bool  noclip_enabled = false;
        bool  infinite_jump  = false;
        bool  fly_enabled    = false;
        float fly_speed      = 60.f;
        bool  fly_planar     = true;
        bool  fov_changer_enabled = false;
        float fov_value     = 70.f;

        bool  antiaim_spin   = false;
        float antiaim_spin_speed = 720.f;
        bool  antiaim_jitter = false;
        float antiaim_jitter_amount = 3.f;

        bool  antiafk = false;

        bool  fps_unlock   = false;
        int   fps_cap      = 240;
        bool  ff_fullbright = false;
        bool  ff_gray_sky   = false;
        bool  ff_no_adorns  = false;
        bool  ff_no_grass   = false;
        bool  ff_low_texture= false;

        bool  draw_arrows   = false;

        bool  esp_team_check = false;

        bool  radar_enabled = false;
        float radar_size    = 110.f;
        float radar_range   = 150.f;
        bool  draw_hud      = false;
        bool  notifications = true;
        bool  radar_names   = false;

        bool  esp_target_line = false;
        bool  esp_show_id     = false;
        bool  esp_box_health_color = false;
        float crosshair_size  = 7.f;

        bool  vis_no_sunrays    = false;
        bool  vis_no_atmosphere = false;

        bool  streamproof   = false;

        int   key_fly    = 0x70;
        int   key_noclip = 0x71;
        int   key_lock   = 0x72;
        int   key_esp    = 0x73;
        int   key_aim        = 0;
        int   key_trigger    = 0;
        int   key_radar      = 0;
        int   key_tp_nearest = 0;
        int   key_panic      = 0x2E;
        int   key_tp_mouse   = 0;

        bool  world_nofog        = false;
        bool  world_time_enabled = false;
        float world_time         = 14.f;
        bool  world_gravity_enabled = false;
        float world_gravity      = 196.2f;
        bool  world_bright_enabled  = false;
        float world_bright       = 2.f;

        bool  draw_weapon    = false;
        bool  draw_speed     = false;
        bool  draw_crosshair = false;
        bool  draw_watermark = false;

        float aim_max_dist    = 0.f;
        bool  aim_toggle_mode = false;
        float aim_deadzone    = 0.f;
        bool  aim_split_smooth = false;
        float aim_smooth_y    = 1.0f;

        bool  bunny_hop     = false;
        bool  freecam       = false;
        float freecam_speed = 60.f;

        bool  zoom_unlock       = false;
        float max_zoom          = 400.f;
        bool  lock_first_person = false;

        bool  vis_no_blur = false;
        bool  vis_no_dof  = false;

        float tp_mouse_dist = 40.f;

        bool  godmode           = false;
        bool  hover_enabled     = false;
        float hover_height      = 5.f;
        bool  jump_height_enabled = false;
        float jump_height       = 15.f;
        bool  no_slope_limit    = false;

        bool  draw_lookat       = false;
        float lookat_length     = 8.f;
        ImVec4 col_lookat       = {1.00f, 0.35f, 0.35f, 1.00f};

        bool  draw_velocity     = false;
        float velocity_seconds  = 0.75f;
        ImVec4 col_velocity     = {0.35f, 0.85f, 1.00f, 1.00f};

        bool  chams_enabled     = false;
        ImVec4 col_box_occluded = {1.00f, 0.20f, 0.20f, 1.00f};

        bool  hitmarker_enabled = false;
        bool  hitmarker_sound   = false;
        bool  killfeed_enabled  = false;

        bool  silent_aim        = false;

        bool  hitbox_enabled = false;
        float hitbox_size    = 10.f;
        int   hitbox_target  = 0;

        bool  per_game_profiles = false;

        int   crosshair_style   = 0;
        ImVec4 col_crosshair    = {0.00f, 1.00f, 0.47f, 0.90f};

        bool  sound_alerts      = false;
        float sound_alert_range = 80.f;
        int   sound_alert_cd_ms = 1500;

        bool  draw_keybind_overlay = false;

        float vis_cone_deg      = 45.f;

        bool  show_console       = false;
        bool  console_wrap       = false;
        int   console_lvl_filter = 0;

        int   selected_game = 0;

        int   silent_key           = 0x02;
        bool  silent_knocked_check = false;
        int   silent_aim_part      = 0;
        float silent_fov           = 200.f;

        // Rivals viewport-writer tuneables. If the game consistently shoots
        // the wrong side of the target, flip the axis; if it lands beside
        // the target because your crosshair isn't at exact screen center,
        // dial in the bias (pixels — positive = shoot lower / more right).
        bool  rivals_flip_x        = false;
        bool  rivals_flip_y        = false;
        float rivals_x_bias        = 0.f;
        float rivals_y_bias        = 0.f;

        // Write the viewport override ONLY while the fire button is held.
        // Cures the flashing double-crosshair (writes only during shots) and
        // concentrates the burst around the exact moment the raycast happens,
        // which beats Rivals's per-frame viewport restore in new rounds.
        bool  silent_only_when_firing = true;

        // 0 = Delta:  vp = 2 * (screen - target)         (proven working — default)
        // 1 = Ratio:  vp = mouse * screen / target       (alternate; may work
        //                                                 better off-center in
        //                                                 some Rivals builds).
        int   rivals_formula        = 0;

        // Arsenal-only. Silent aim rotates Camera.CFrame to point at target
        // so the client raycast hits it. When continuous_flick is OFF, we
        // only flick on the exact LMB-down edge — one clean shot per click,
        // no camera jerk while holding fire. Turn ON for auto weapons at the
        // cost of visible camera jitter.
        bool  arsenal_continuous_flick = false;

        bool  aim_shake            = false;
        float aim_shake_x          = 1.0f;
        float aim_shake_y          = 1.0f;

        bool  aim_knocked_check    = false;

        bool  freeze_player        = false;
        int   freeze_player_key    = 0;

        bool  tickrate_enabled     = false;
        float tickrate_amount      = 60.f;

        float aim_predict_xz       = 1.0f;
        float aim_predict_y        = 1.0f;

        bool   esp_wall_color_split = false;
        ImVec4 col_esp_visible      = {0.30f, 1.00f, 0.40f, 1.00f};
        ImVec4 col_esp_occluded_alt = {1.00f, 0.30f, 0.30f, 1.00f};

        bool  tp_spin_enabled      = false;
        int   tp_spin_key          = 0;
        int   tp_spin_cancel_key   = 'X';
        float tp_spin_radius       = 8.f;
        float tp_spin_speed        = 5.f;

        bool  aim_easing_enabled   = false;
        int   aim_ease_style       = 1;
        int   aim_ease_dur_ms      = 250;
        float aim_ease_amp         = 1.0f;

        bool  trig_hitbox_viz      = false;
        float trig_box_scale       = 1.0f;
        bool  trig_knife_check     = false;
        int   trig_acquire_ms      = 100;
        ImVec4 col_trig_hitbox     = {1.00f, 0.85f, 0.30f, 0.85f};

        int   esp_font_pick        = 1;

        int   arsenal_hip_preset = 1;

        bool  arsenal_no_recoil   = false;
        bool  arsenal_no_spread   = false;
        bool  arsenal_auto_reload = false;
        bool  arsenal_rapid_fire  = false;
        bool  arsenal_inf_ammo    = false;
        float arsenal_fire_rate   = 20.f;
    };

    extern Config cfg;

    bool save_config(const char* path);
    bool load_config(const char* path);

    std::string export_config_string();
    bool        import_config_string(const std::string& s);

    void notify(const std::string& msg);

    void set_view_origin(Vec2 origin);

    void render(ImDrawList* dl,
                const std::vector<PlayerInfo>& players,
                const Matrix4& vm,
                Vec2 screen);

    void render_console(const std::vector<PlayerInfo>& players, Vec2 screen);

    void draw_menu(uintptr_t render_view_ptr, const std::vector<PlayerInfo>& players);

    void draw_active_tab(int active, uintptr_t render_view_ptr,
                         const std::vector<PlayerInfo>& players);

    const char* esp_current_script_buffer();

    void set_flat_mode(bool on);

}
