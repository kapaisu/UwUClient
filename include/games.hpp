#pragma once

#include "esp.hpp"
#include "memory.hpp"
#include "offsets.hpp"
#include "roblox.hpp"
#include "log.hpp"

#include <chrono>
#include <string>
#include <cstdint>

namespace games {

enum class Id : int {
    Universal      = 0,
    Arsenal        = 1,
    Flick          = 2,
    DaHood         = 3,

    BadBusiness    = 4,
    PhantomForces  = 5,
    BigPaintball   = 6,
    CounterBlox    = 7,

    KAT            = 8,
    Assassin       = 9,

    StrongestBG    = 10,
    BloxFruits     = 11,
    Rivals         = 12,
    Count
};

inline Id id_from_int(int i) {
    if (i < 0 || i >= (int)Id::Count) return Id::Universal;
    return (Id)i;
}

inline Id detect_from_place() {
    RbxDataModel dm = RbxDataModel::get();
    if (!dm.valid() || !offsets::DataModel::PlaceId) return Id::Universal;
    int64_t place = rpm<int64_t>(dm.ptr + offsets::DataModel::PlaceId);

    if (place == 286090429LL)   return Id::Arsenal;

    if (place == 2788229376LL)  return Id::DaHood;

    if (place == 3233893879LL)  return Id::BadBusiness;

    if (place == 292439477LL)   return Id::PhantomForces;

    if (place == 4076430840LL)  return Id::BigPaintball;

    if (place == 301549746LL)   return Id::CounterBlox;

    if (place == 1156569102LL)  return Id::KAT;

    if (place == 20279777LL)    return Id::Assassin;

    if (place == 15532962292LL) return Id::StrongestBG;

    if (place == 2753915549LL)  return Id::BloxFruits;

    return Id::Universal;
}
inline const char* name_of(Id g) {
    switch (g) {
        case Id::Universal:     return "Universal";
        case Id::Arsenal:       return "Arsenal";
        case Id::Flick:         return "Flick";
        case Id::DaHood:        return "Da Hood";
        case Id::BadBusiness:   return "Bad Business";
        case Id::PhantomForces: return "Phantom Forces";
        case Id::BigPaintball:  return "BIG Paintball";
        case Id::CounterBlox:   return "Counter Blox";
        case Id::KAT:           return "KAT";
        case Id::Assassin:      return "Assassin!";
        case Id::StrongestBG:   return "Strongest Battlegrounds";
        case Id::BloxFruits:    return "Blox Fruits";
        case Id::Rivals:        return "Rivals";
        default:                return "?";
    }
}
inline const char* tagline_of(Id g) {
    switch (g) {
        case Id::Universal:     return "generic — works on any game";
        case Id::Arsenal:       return "aim tuning + weapon mods for Arsenal";
        case Id::Flick:         return "Flick support (drop flick.txt to enable)";
        case Id::DaHood:        return "silent aim + KO check for Da Hood";
        case Id::BadBusiness:   return "weapon mods + silent aim for Bad Business";
        case Id::PhantomForces: return "subtle weapon mods for Phantom Forces";
        case Id::BigPaintball:  return "weapon mods + full-auto for BIG Paintball";
        case Id::CounterBlox:   return "weapon mods for Counter Blox";
        case Id::KAT:           return "silent knife throws for KAT";
        case Id::Assassin:      return "silent knife throws for Assassin!";
        case Id::StrongestBG:   return "movement preset for Strongest BG";
        case Id::BloxFruits:    return "long-range ESP preset for Blox Fruits";
        case Id::Rivals:        return "silent aim via Camera.Viewport for Rivals";
        default:                return "";
    }
}

inline Id g_locked{Id::Universal};
inline bool g_did_inject_setup = false;

inline uintptr_t g_cached_repstore = 0;
inline uintptr_t g_cached_weapons  = 0;

inline uintptr_t ensure_replicated_storage() {
    if (g_cached_repstore) return g_cached_repstore;
    RbxDataModel dm = RbxDataModel::get();
    if (!dm.valid()) return 0;
    g_cached_repstore = dm.get_service("ReplicatedStorage").ptr;
    return g_cached_repstore;
}

inline uintptr_t ensure_rs_child(uintptr_t& slot, const char* child) {
    if (slot) return slot;
    uintptr_t rs = ensure_replicated_storage();
    if (!rs) return 0;
    slot = RbxInstance{rs}.find_child_by_name(child).ptr;
    return slot;
}
inline uintptr_t ensure_arsenal_weapons() {
    return ensure_rs_child(g_cached_weapons, "Weapons");
}

inline uintptr_t g_cached_bb_weapons = 0;
inline uintptr_t g_cached_pf_weapons = 0;
inline uintptr_t g_cached_bp_weapons = 0;
inline uintptr_t g_cached_cb_weapons = 0;

inline void invalidate_caches() {

    g_cached_repstore = 0;
    g_cached_weapons  = 0;
    g_cached_bb_weapons = 0;
    g_cached_pf_weapons = 0;
    g_cached_bp_weapons = 0;
    g_cached_cb_weapons = 0;
}

template <class F>
inline void for_each_named_descendant(RbxInstance root, F&& pred_cb) {
    if (!root.valid()) return;
    std::vector<RbxInstance> stack{root};
    while (!stack.empty()) {
        RbxInstance cur = stack.back(); stack.pop_back();
        for (auto& c : cur.get_children()) {
            pred_cb(c);
            stack.push_back(c);
        }
    }
}

struct Snapshot { uintptr_t addr; float orig; };
inline std::vector<Snapshot> g_snap_recoil;
inline std::vector<Snapshot> g_snap_spread;
inline std::vector<Snapshot> g_snap_reload;
inline std::vector<Snapshot> g_snap_ereload;
inline std::vector<Snapshot> g_snap_firerate;

inline void snapshot_and_write(std::vector<Snapshot>& store,
                               uintptr_t addr, float new_val) {
    for (auto& s : store) if (s.addr == addr) { wpm<float>(addr, new_val); return; }
    float orig = rpm<float>(addr);
    store.push_back({addr, orig});
    wpm<float>(addr, new_val);
}
inline void restore_all(std::vector<Snapshot>& store) {
    for (auto& s : store) wpm<float>(s.addr, s.orig);
    store.clear();
}

inline void arsenal_apply_defaults() {

    esp::cfg.team_check                = true;
    esp::cfg.esp_team_check            = true;
    esp::cfg.aim_visibility_check      = true;
    esp::cfg.aim_bone                  = 0;
    esp::cfg.aim_priority              = 0;
    esp::cfg.aim_key                   = 0x02;
    esp::cfg.fov_radius                = 120.f;
    esp::cfg.aim_smooth                = 0.65f;
    esp::cfg.aim_max_dist              = 500.f;
    esp::cfg.max_dist                  = 1500.f;

    esp::cfg.arsenal_hip_preset        = 1;

    esp::cfg.antiafk                   = true;
    esp::cfg.key_fly                   = 'X';

    esp::cfg.enabled                   = true;
    esp::cfg.draw_name                 = true;
    esp::cfg.draw_distance             = true;
    esp::cfg.draw_health               = true;
    esp::cfg.draw_line                 = false;
    esp::cfg.box_style                 = 2;
}

inline float hip_preset_value(int preset) {
    switch (preset) {
        case 0:  return 0.f;
        case 1:  return 2.f;
        case 2:  return 4.f;
        default: return 2.f;
    }
}

struct WeaponModSpec {
    const char* game_name;
    const char* const* recoil_names;
    const char* const* spread_names;
    const char* const* reload_names;
    const char* const* ereload_names;
    const char* const* firerate_names;
    float       recoil_val;
    float       spread_val;
    float       reload_val;
    float       ereload_val;

    float       firerate_base_interval;
    float       firerate_base_rate;
};

inline bool name_matches(const std::string& n, const char* const* list) {
    if (!list) return false;
    for (int i = 0; list[i]; i++) if (n == list[i]) return true;
    return false;
}

inline void weapon_mods_apply_with(uintptr_t weapons_root, const WeaponModSpec& s) {
    if (!offsets::ValueBase::Value) {
        static auto next = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        bool need_any = esp::cfg.arsenal_no_recoil || esp::cfg.arsenal_no_spread ||
                        esp::cfg.arsenal_auto_reload || esp::cfg.arsenal_rapid_fire;
        if (need_any && now >= next) {
            elog::warn("%s weapon mods disabled — dumper missing ValueBase::Value", s.game_name);
            next = now + std::chrono::seconds(2);
        }
        return;
    }
    if (!weapons_root) return;

    const uintptr_t VOFF = offsets::ValueBase::Value;

    if (esp::cfg.arsenal_no_recoil) {
        for_each_named_descendant(RbxInstance{weapons_root}, [&](RbxInstance c) {
            if (name_matches(c.get_name(), s.recoil_names))
                snapshot_and_write(g_snap_recoil, c.ptr + VOFF, s.recoil_val);
        });
    } else if (!g_snap_recoil.empty()) restore_all(g_snap_recoil);

    if (esp::cfg.arsenal_no_spread) {
        for_each_named_descendant(RbxInstance{weapons_root}, [&](RbxInstance c) {
            if (name_matches(c.get_name(), s.spread_names))
                snapshot_and_write(g_snap_spread, c.ptr + VOFF, s.spread_val);
        });
    } else if (!g_snap_spread.empty()) restore_all(g_snap_spread);

    if (esp::cfg.arsenal_auto_reload) {
        for_each_named_descendant(RbxInstance{weapons_root}, [&](RbxInstance c) {
            const std::string n = c.get_name();
            if (name_matches(n, s.reload_names))  snapshot_and_write(g_snap_reload,  c.ptr + VOFF, s.reload_val);
            if (name_matches(n, s.ereload_names)) snapshot_and_write(g_snap_ereload, c.ptr + VOFF, s.ereload_val);
        });
    } else {
        if (!g_snap_reload.empty())  restore_all(g_snap_reload);
        if (!g_snap_ereload.empty()) restore_all(g_snap_ereload);
    }

    if (esp::cfg.arsenal_rapid_fire) {
        float fr = esp::cfg.arsenal_fire_rate > 0.5f ? esp::cfg.arsenal_fire_rate : 20.f;
        float interval = s.firerate_base_interval * (s.firerate_base_rate / fr);
        for_each_named_descendant(RbxInstance{weapons_root}, [&](RbxInstance c) {
            if (name_matches(c.get_name(), s.firerate_names))
                snapshot_and_write(g_snap_firerate, c.ptr + VOFF, interval);
        });
    } else if (!g_snap_firerate.empty()) restore_all(g_snap_firerate);
}

inline void arsenal_apply_weapon_mods() {
    static const char* R[]  = {"RecoilControl", "Recoil", nullptr};
    static const char* S[]  = {"MaxSpread", "Spread", "SpreadControl", nullptr};
    static const char* RL[] = {"ReloadTime", nullptr};
    static const char* ER[] = {"EReloadTime", nullptr};
    static const char* FR[] = {"FireRate", "BFireRate", nullptr};
    static const WeaponModSpec spec {
        "Arsenal", R, S, RL, ER, FR,
         0.f,  0.f,
         0.05f,  0.05f,
         0.02f,  20.f,
    };
    weapon_mods_apply_with(ensure_arsenal_weapons(), spec);
}

inline void arsenal_apply_hip_preset(uintptr_t hum) {
    if (!hum || !offsets::Humanoid::HipHeight) return;
    wpm<float>(hum + offsets::Humanoid::HipHeight,
               hip_preset_value(esp::cfg.arsenal_hip_preset));
}

inline void dahood_apply_defaults() {
    esp::cfg.silent_aim            = true;
    esp::cfg.silent_aim_part       = 1;
    esp::cfg.silent_fov            = 200.f;
    esp::cfg.silent_knocked_check  = true;
    esp::cfg.team_check            = false;
    esp::cfg.aim_visibility_check  = true;
    esp::cfg.aim_max_dist          = 500.f;

    esp::cfg.aim_bone              = 1;
    esp::cfg.fov_radius            = 200.f;
    esp::cfg.aim_key               = 0x02;

    esp::cfg.enabled               = true;
    esp::cfg.draw_name             = true;
    esp::cfg.draw_distance         = true;
    esp::cfg.draw_health           = true;
    esp::cfg.box_style             = 2;
    esp::cfg.esp_box_health_color  = true;
    esp::cfg.max_dist              = 1500.f;
}
inline void dahood_apply_tick(uintptr_t ) {

}

inline void flick_apply_defaults() {
    esp::cfg.fov_radius            = 120.f;
    esp::cfg.aim_bone              = 0;
    esp::cfg.aim_priority          = 0;
    esp::cfg.aim_visibility_check  = true;
    esp::cfg.aim_key               = 0x02;
    esp::cfg.aim_max_dist          = 500.f;
    esp::cfg.max_dist              = 1500.f;

    esp::cfg.enabled               = true;
    esp::cfg.draw_name             = true;
    esp::cfg.draw_distance         = true;
    esp::cfg.draw_health           = true;
    esp::cfg.draw_line             = true;
    esp::cfg.snapline_pos          = 0;
    esp::cfg.box_style             = 2;
    esp::cfg.esp_box_health_color  = true;

}
inline void flick_apply_tick(uintptr_t ) {

}

inline void badbusiness_apply_defaults() {
    esp::cfg.enabled                = true;
    esp::cfg.team_check             = true;
    esp::cfg.esp_team_check         = true;
    esp::cfg.aim_visibility_check   = true;
    esp::cfg.aim_bone               = 0;
    esp::cfg.aim_priority           = 0;
    esp::cfg.aim_key                = 0x02;
    esp::cfg.fov_radius             = 140.f;
    esp::cfg.aim_smooth             = 0.60f;
    esp::cfg.aim_max_dist           = 600.f;
    esp::cfg.max_dist               = 1500.f;
    esp::cfg.draw_name              = true;
    esp::cfg.draw_distance          = true;
    esp::cfg.draw_health            = true;
    esp::cfg.box_style              = 2;
    esp::cfg.esp_box_health_color   = true;
    esp::cfg.arsenal_fire_rate      = 20.f;
}
inline void badbusiness_apply_weapon_mods() {
    static const char* R[]  = {"RecoilY", "RecoilX", "Recoil", nullptr};
    static const char* S[]  = {"Spread", "MinSpread", "MaxSpread", nullptr};
    static const char* RL[] = {"ReloadTime", nullptr};
    static const char* ER[] = {"EmptyReloadTime", "EReloadTime", nullptr};
    static const char* FR[] = {"RateOfFire", "FireRate", nullptr};
    static const WeaponModSpec spec {
        "BadBusiness", R, S, RL, ER, FR,
         0.f,  0.f,
         0.05f,  0.05f,
         0.02f,  20.f,
    };
    weapon_mods_apply_with(ensure_rs_child(g_cached_bb_weapons, "Weapons"), spec);
}

inline void phantomforces_apply_defaults() {
    esp::cfg.enabled                = true;
    esp::cfg.team_check             = true;
    esp::cfg.esp_team_check         = true;
    esp::cfg.aim_visibility_check   = true;
    esp::cfg.aim_bone               = 0;
    esp::cfg.aim_priority           = 0;
    esp::cfg.aim_key                = 0x02;
    esp::cfg.fov_radius             = 100.f;
    esp::cfg.aim_smooth             = 0.55f;
    esp::cfg.aim_max_dist           = 800.f;
    esp::cfg.max_dist               = 2000.f;
    esp::cfg.draw_name              = true;
    esp::cfg.draw_distance          = true;
    esp::cfg.draw_health            = true;
    esp::cfg.draw_line              = false;
    esp::cfg.box_style              = 2;

    esp::cfg.arsenal_fire_rate      = 12.f;
}
inline void phantomforces_apply_weapon_mods() {
    static const char* R[]  = {"CameraKick", "CameraKickMin", "CameraKickMax",
                               "RecoilXMin", "RecoilXMax", "RecoilYMin", "RecoilYMax",
                               "Recoil", nullptr};
    static const char* S[]  = {"HipfireSpreadRecover", "SpreadRecover",
                               "MinSpread", "MaxSpread", "Spread", nullptr};
    static const char* RL[] = {"ReloadTime", nullptr};
    static const char* ER[] = {"EquipTime", "EReloadTime", nullptr};
    static const char* FR[] = {"FireRate", "FiringRate", nullptr};
    static const WeaponModSpec spec {
        "PhantomForces", R, S, RL, ER, FR,

         0.05f,  0.02f,
         0.75f,  0.75f,
         0.04f,  12.f,
    };
    weapon_mods_apply_with(ensure_rs_child(g_cached_pf_weapons, "Weapons"), spec);
}

inline void bigpaintball_apply_defaults() {
    esp::cfg.enabled                = true;
    esp::cfg.team_check             = true;
    esp::cfg.esp_team_check         = true;
    esp::cfg.aim_visibility_check   = true;
    esp::cfg.aim_bone               = 0;
    esp::cfg.aim_priority           = 0;
    esp::cfg.aim_key                = 0x02;
    esp::cfg.fov_radius             = 130.f;
    esp::cfg.aim_smooth             = 0.65f;
    esp::cfg.aim_max_dist           = 500.f;
    esp::cfg.max_dist               = 1500.f;
    esp::cfg.draw_name              = true;
    esp::cfg.draw_distance          = true;
    esp::cfg.draw_health            = true;
    esp::cfg.box_style              = 2;
    esp::cfg.esp_box_health_color   = true;
    esp::cfg.arsenal_fire_rate      = 20.f;
}
inline void bigpaintball_apply_weapon_mods() {
    static const char* R[]  = {"Recoil", "RecoilY", "RecoilX", nullptr};
    static const char* S[]  = {"Spread", "MaxSpread", "MinSpread", nullptr};
    static const char* RL[] = {"ReloadTime", nullptr};
    static const char* ER[] = {"EReloadTime", nullptr};
    static const char* FR[] = {"FireRate", "RateOfFire", nullptr};
    static const WeaponModSpec spec {
        "BigPaintball", R, S, RL, ER, FR,
         0.f,  0.f,
         0.05f,  0.05f,
         0.02f,  20.f,
    };
    weapon_mods_apply_with(ensure_rs_child(g_cached_bp_weapons, "Guns"), spec);
}

inline void counterblox_apply_defaults() {
    esp::cfg.enabled                = true;
    esp::cfg.team_check             = true;
    esp::cfg.esp_team_check         = true;
    esp::cfg.aim_visibility_check   = true;
    esp::cfg.aim_bone               = 0;
    esp::cfg.aim_priority           = 0;
    esp::cfg.aim_key                = 0x02;
    esp::cfg.fov_radius             = 100.f;
    esp::cfg.aim_smooth             = 0.55f;
    esp::cfg.aim_max_dist           = 800.f;
    esp::cfg.max_dist               = 2500.f;
    esp::cfg.draw_name              = true;
    esp::cfg.draw_distance          = true;
    esp::cfg.draw_health            = true;
    esp::cfg.box_style              = 2;
    esp::cfg.arsenal_fire_rate      = 15.f;
}
inline void counterblox_apply_weapon_mods() {
    static const char* R[]  = {"Recoil", "VerticalRecoil", "HorizontalRecoil", nullptr};
    static const char* S[]  = {"Spread", "MaxSpread", "MinSpread", nullptr};
    static const char* RL[] = {"ReloadTime", nullptr};
    static const char* ER[] = {"EReloadTime", nullptr};
    static const char* FR[] = {"FireRate", "RateOfFire", nullptr};
    static const WeaponModSpec spec {
        "CounterBlox", R, S, RL, ER, FR,
         0.f,  0.f,
         0.1f,  0.1f,
         0.02f,  15.f,
    };
    weapon_mods_apply_with(ensure_rs_child(g_cached_cb_weapons, "Weapons"), spec);
}

inline void kat_apply_defaults() {
    esp::cfg.silent_aim            = true;
    esp::cfg.silent_aim_part       = 0;
    esp::cfg.silent_fov            = 250.f;
    esp::cfg.silent_knocked_check  = false;
    esp::cfg.team_check            = false;
    esp::cfg.aim_visibility_check  = true;
    esp::cfg.aim_max_dist          = 300.f;
    esp::cfg.aim_bone              = 0;
    esp::cfg.fov_radius            = 200.f;
    esp::cfg.aim_key               = 0x02;
    esp::cfg.enabled               = true;
    esp::cfg.draw_name             = true;
    esp::cfg.draw_distance         = true;
    esp::cfg.draw_health           = false;
    esp::cfg.box_style             = 2;
    esp::cfg.max_dist              = 1000.f;
}

inline void assassin_apply_defaults() {
    esp::cfg.silent_aim            = true;
    esp::cfg.silent_aim_part       = 0;
    esp::cfg.silent_fov            = 300.f;
    esp::cfg.silent_knocked_check  = false;
    esp::cfg.team_check            = false;
    esp::cfg.aim_visibility_check  = true;
    esp::cfg.aim_max_dist          = 400.f;
    esp::cfg.aim_bone              = 0;
    esp::cfg.fov_radius            = 220.f;
    esp::cfg.aim_key               = 0x02;
    esp::cfg.enabled               = true;
    esp::cfg.draw_name             = true;
    esp::cfg.draw_distance         = true;
    esp::cfg.draw_health           = false;
    esp::cfg.box_style             = 2;
    esp::cfg.max_dist              = 1200.f;
}

inline void strongestbg_apply_defaults() {
    esp::cfg.enabled                = true;
    esp::cfg.team_check             = false;
    esp::cfg.esp_team_check         = false;
    esp::cfg.draw_name              = true;
    esp::cfg.draw_distance          = true;
    esp::cfg.draw_health            = true;
    esp::cfg.esp_box_health_color   = true;
    esp::cfg.box_style              = 2;
    esp::cfg.max_dist               = 800.f;
    esp::cfg.fov_radius             = 220.f;
    esp::cfg.aim_bone               = 1;
    esp::cfg.aim_smooth             = 0.70f;
    esp::cfg.aim_visibility_check   = false;
    esp::cfg.aim_max_dist           = 200.f;
    esp::cfg.aim_key                = 0x02;
    esp::cfg.speed_enabled          = true;
    esp::cfg.speed_value            = 24.f;
    esp::cfg.jump_enabled           = false;
}

inline void rivals_apply_defaults() {
    esp::cfg.silent_aim            = true;
    esp::cfg.silent_aim_part       = 0;
    esp::cfg.silent_knocked_check  = false;
    esp::cfg.team_check             = true;
    esp::cfg.esp_team_check         = true;
    esp::cfg.aim_visibility_check   = true;
    esp::cfg.aim_max_dist           = 800.f;

    esp::cfg.aim_bone              = 0;
    esp::cfg.fov_radius            = 150.f;
    esp::cfg.aim_key               = 0x02;
    esp::cfg.enabled               = true;
    esp::cfg.draw_name             = true;
    esp::cfg.draw_distance         = true;
    esp::cfg.draw_health           = true;
    esp::cfg.box_style             = 2;
    esp::cfg.esp_box_health_color  = true;
    esp::cfg.max_dist              = 2000.f;
}

inline void bloxfruits_apply_defaults() {
    esp::cfg.enabled                = true;
    esp::cfg.team_check             = false;
    esp::cfg.esp_team_check         = false;
    esp::cfg.draw_name              = true;
    esp::cfg.draw_distance          = true;
    esp::cfg.draw_health            = true;
    esp::cfg.box_style              = 2;
    esp::cfg.esp_box_health_color   = false;
    esp::cfg.max_dist               = 5000.f;
    esp::cfg.fov_radius             = 150.f;
    esp::cfg.aim_bone               = 1;
    esp::cfg.aim_smooth             = 0.60f;
    esp::cfg.aim_visibility_check   = false;
    esp::cfg.aim_max_dist           = 1500.f;
    esp::cfg.aim_key                = 0x02;
    esp::cfg.speed_enabled          = true;
    esp::cfg.speed_value            = 90.f;
    esp::cfg.jump_enabled           = false;
}

inline void lock_from_cfg() {
    g_locked = id_from_int(esp::cfg.selected_game);
    g_did_inject_setup = false;
}
inline Id active() { return g_locked; }

inline bool needs_character() {

    return g_locked == Id::Arsenal
        || g_locked == Id::StrongestBG
        || g_locked == Id::BloxFruits;
}

inline void on_inject() {
    if (g_did_inject_setup) return;
    g_did_inject_setup = true;
    elog::ok("games: locked to %s", name_of(g_locked));

    RbxDataModel dm = RbxDataModel::get();
    if (dm.valid() && offsets::DataModel::PlaceId) {
        int64_t place = rpm<int64_t>(dm.ptr + offsets::DataModel::PlaceId);
        elog::info("games: PlaceId = %lld", (long long)place);
    }
}

inline void on_tick(uintptr_t hum) {
    switch (g_locked) {
        case Id::Arsenal:
            arsenal_apply_weapon_mods();
            if (!esp::cfg.hover_enabled) arsenal_apply_hip_preset(hum);
            break;
        case Id::BadBusiness:
            badbusiness_apply_weapon_mods();
            break;
        case Id::PhantomForces:
            phantomforces_apply_weapon_mods();
            break;
        case Id::BigPaintball:
            bigpaintball_apply_weapon_mods();
            break;
        case Id::CounterBlox:
            counterblox_apply_weapon_mods();
            break;
        case Id::Flick:
            flick_apply_tick(hum);
            break;

        case Id::KAT:
        case Id::Assassin:
        case Id::StrongestBG:
        case Id::BloxFruits:
        case Id::DaHood:
        case Id::Universal:
        default: break;
    }
}

inline void on_dm_changed() {
    invalidate_caches();

    g_snap_recoil.clear();
    g_snap_spread.clear();
    g_snap_reload.clear();
    g_snap_ereload.clear();
    g_snap_firerate.clear();
}

}
