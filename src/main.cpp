#include "../include/memory.hpp"
#include "../include/offsets.hpp"
#include "../include/roblox.hpp"
#include "../include/overlay.hpp"
#include "../include/esp.hpp"
#include "../include/math.hpp"
#include "../include/aimbot.hpp"
#include "../include/spotify.hpp"
#include "../include/updater.hpp"
#include "../include/teleport.hpp"
#include "../include/features.hpp"
#include "../include/scanner.hpp"
#include "../include/log.hpp"
#include "../include/luavm.hpp"
#include "../include/executor.hpp"
#include "../include/offset_updater.hpp"
#include "../include/fflags.hpp"
#include "../include/embedded_offsets.hpp"

#include "../include/menu.hpp"
#include "../include/check.hpp"
#include "../include/bgimage.hpp"
#include "../include/dumper.hpp"

#include <Windows.h>
#include <imgui.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>

#include <mutex>
#include <unordered_map>
#include <algorithm>

std::vector<PlayerInfo> g_players;
Matrix4                 g_view_matrix{};
Vec2                    g_screen{};
uintptr_t               g_render_view = 0;
std::atomic<bool>       g_menu_open{true};
std::atomic<uintptr_t>  g_local_team{0};
std::atomic<uintptr_t>  g_aim_target{0};
std::atomic<bool>       g_scan_done{false};
std::atomic<bool>       g_scan_ok{false};

std::vector<PlayerInfo> g_players_temp;
std::mutex             g_players_mtx;

static HWND find_roblox_window(DWORD pid) {
    struct FindData { DWORD pid; HWND hwnd; };
    FindData fd{pid, nullptr};
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* fd = (FindData*)lp;
        DWORD wpid = 0;
        GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid == fd->pid && IsWindowVisible(hwnd)) {
            char title[64]{};
            GetWindowTextA(hwnd, title, sizeof(title));
            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (rc.right > 200 && rc.bottom > 200) {
                fd->hwnd = hwnd;
                return FALSE;
            }
        }
        return TRUE;
    }, (LPARAM)&fd);
    return fd.hwnd;
}

static void list_reader_thread() {
    while (overlay::is_running()) {
        if (!mem::alive()) break;

        RbxDataModel dm = RbxDataModel::get();
        if (dm.valid()) {
            auto players = collect_player_instances(dm);

            uintptr_t lteam = 0;
            if (offsets::Player::Team) {
                RbxInstance psvc = dm.get_service("Players");
                if (psvc.valid()) {
                    uintptr_t lp = rpm<uintptr_t>(psvc.ptr + offsets::Players::LocalPlayer);
                    if (lp) lteam = rpm<uintptr_t>(lp + offsets::Player::Team);
                }
            }
            g_local_team = lteam;

            std::lock_guard<std::mutex> lock(g_players_mtx);

            std::unordered_map<uintptr_t, size_t> ix;
            ix.reserve(g_players_temp.size() * 2 + 1);
            for (size_t i = 0; i < g_players_temp.size(); i++)
                ix.emplace(g_players_temp[i].player_ptr, i);

            const size_t orig_end = g_players_temp.size();
            std::vector<uint8_t> keep(orig_end, 0);

            for (auto& fresh : players) {
                auto it = ix.find(fresh.player_ptr);
                if (it != ix.end()) {
                    g_players_temp[it->second].name = std::move(fresh.name);
                    keep[it->second] = 1;
                } else {
                    g_players_temp.push_back(std::move(fresh));
                }
            }

            size_t write = 0;
            for (size_t read = 0; read < orig_end; read++) {
                if (!keep[read]) continue;
                if (write != read)
                    g_players_temp[write] = std::move(g_players_temp[read]);
                write++;
            }
            if (write != orig_end) {
                const size_t appended = g_players_temp.size() - orig_end;
                for (size_t k = 0; k < appended; k++)
                    g_players_temp[write + k] = std::move(g_players_temp[orig_end + k]);
                g_players_temp.resize(write + appended);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

static void position_reader_thread() {
    while (overlay::is_running()) {
        if (!mem::alive()) break;

        RbxVisualEngine ve = RbxVisualEngine::get();
        if (ve.valid()) {
            ve.read_view_matrix(g_view_matrix);
            g_screen      = ve.dimensions();
            g_render_view = ve.render_view();
        }

        {
            std::lock_guard<std::mutex> lock(g_players_mtx);
            for (auto& p : g_players_temp) {
                if (p.player_ptr) {
                    p.team_ptr = offsets::Player::Team
                        ? rpm<uintptr_t>(p.player_ptr + offsets::Player::Team) : 0;
                    if (offsets::Player::UserId)     p.user_id = rpm<long long>(p.player_ptr + offsets::Player::UserId);
                    if (offsets::Player::AccountAge) p.account_age = rpm<int>(p.player_ptr + offsets::Player::AccountAge);
                    uintptr_t char_ptr = rpm<uintptr_t>(p.player_ptr + offsets::Player::Character);
                    if (!char_ptr) {
                        RbxDataModel dm = RbxDataModel::get();
                        if (dm.valid()) {
                            RbxInstance ws = dm.get_service("Workspace");
                            if (ws.valid()) {
                                RbxInstance char_root = ws.find_child_by_name("CharacterRoot");
                                RbxInstance candidate;
                                if (char_root.valid()) {
                                    candidate = char_root.find_child_by_name("Player");
                                    if (!candidate.valid()) {
                                        candidate = char_root.find_child_by_name(p.name);
                                    }
                                }
                                if (!candidate.valid()) {
                                    candidate = ws.find_child_by_name("Player");
                                }
                                if (!candidate.valid()) {
                                    candidate = ws.find_child_by_name(p.name);
                                }
                                if (candidate.valid() && candidate.get_class() == "Model") {
                                    char_ptr = candidate.ptr;
                                }
                            }
                        }
                    }

                    if (char_ptr) {
                        if (char_ptr != p.cached_char_ptr) {
                            p.cached_char_ptr = char_ptr;
                            p.hrp_ptr = 0;
                            p.head_ptr = 0;
                            p.humanoid_ptr = 0;
                            p.valid = false;
                            p.joint_valid_mask = 0;
                            for (int i = 0; i < J_COUNT; i++) p.joint_prim[i] = 0;
                            p.seen_dead = false;
                        }

                        if (!p.joint_valid_mask) {
                            RbxInstance character{char_ptr};
                            struct J { const char* name; int idx; };
                            static const J MAP[] = {
                                {"Head",           J_Head},
                                {"UpperTorso",     J_UpperTorso},
                                {"LowerTorso",     J_LowerTorso},
                                {"Torso",          J_UpperTorso},
                                {"HumanoidRootPart", J_HRP},
                                {"LeftUpperArm",   J_LUArm},
                                {"LeftLowerArm",   J_LLArm},
                                {"LeftHand",       J_LHand},
                                {"RightUpperArm",  J_RUArm},
                                {"RightLowerArm",  J_RLArm},
                                {"RightHand",      J_RHand},
                                {"LeftUpperLeg",   J_LULeg},
                                {"LeftLowerLeg",   J_LLLeg},
                                {"LeftFoot",       J_LFoot},
                                {"RightUpperLeg",  J_RULeg},
                                {"RightLowerLeg",  J_RLLeg},
                                {"RightFoot",      J_RFoot},
                                {"Left Arm",       J_LLArm},
                                {"Right Arm",      J_RLArm},
                                {"Left Leg",       J_LLLeg},
                                {"Right Leg",      J_RLLeg},
                            };
                            for (auto& c : character.get_children()) {
                                std::string nm = c.get_name();
                                for (auto& m : MAP) {
                                    if (nm == m.name) {
                                        uintptr_t prim = rpm<uintptr_t>(c.ptr + offsets::BasePart::Primitive);
                                        if (prim) {
                                            p.joint_prim[m.idx] = prim;
                                            p.joint_valid_mask |= (1u << m.idx);
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                        for (int i = 0; i < J_COUNT; i++) {
                            if (!p.joint_prim[i]) continue;
                            p.joint_pos[i] = rpm<Vec3>(p.joint_prim[i] + offsets::Primitive::Position);
                        }

                        if (!p.hrp_ptr) {
                            RbxInstance character{char_ptr};
                            p.hrp_ptr = character.find_child_by_name("HumanoidRootPart").ptr;
                        }
                        if (p.hrp_ptr) {
                            uintptr_t primitive = rpm<uintptr_t>(p.hrp_ptr + offsets::BasePart::Primitive);
                            if (primitive) {
                                p.position = rpm<Vec3>(primitive + offsets::Primitive::Position);
                                if (offsets::Primitive::AssemblyLinearVelocity)
                                    p.velocity = rpm<Vec3>(primitive + offsets::Primitive::AssemblyLinearVelocity);
                                p.valid = true;
                            } else {
                                p.valid = false;
                            }
                        } else {
                            p.valid = false;
                        }

                        if (!p.head_ptr) {
                            RbxInstance character{char_ptr};
                            p.head_ptr = character.find_child_by_name("Head").ptr;
                        }
                        if (p.head_ptr) {
                            uintptr_t hprim = rpm<uintptr_t>(p.head_ptr + offsets::BasePart::Primitive);
                            if (hprim) p.head_pos = rpm<Vec3>(hprim + offsets::Primitive::Position);
                            else p.head_ptr = 0;
                        }

                        if (!p.humanoid_ptr) {
                            RbxInstance character{char_ptr};
                            p.humanoid_ptr = character.find_child_by_class("Humanoid").ptr;
                        }
                        if (p.humanoid_ptr) {
                            float new_hp = rpm<float>(p.humanoid_ptr + offsets::Humanoid::Health);
                            float new_max = rpm<float>(p.humanoid_ptr + offsets::Humanoid::MaxHealth);
                            if (p.prev_health > 0.f && new_hp + 0.5f < p.prev_health)
                                p.last_hit_time = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now().time_since_epoch()).count();
                            p.prev_health = new_hp;
                            p.health = new_hp;
                            p.max_health = new_max;
                        }

                        {
                            uintptr_t head_or_hrp = p.head_ptr ? p.head_ptr : p.hrp_ptr;
                            if (head_or_hrp && offsets::Primitive::CFrame) {
                                uintptr_t prim = rpm<uintptr_t>(head_or_hrp + offsets::BasePart::Primitive);
                                if (prim) {
                                    float cf[9]{};
                                    mem::vm_read(mem::g_proc,
                                        (LPCVOID)(prim + offsets::Primitive::CFrame),
                                        cf, sizeof(cf), nullptr);
                                    Vec3 look = { -cf[2], -cf[5], -cf[8] };
                                    float l = look.length();
                                    if (l > 1e-4f) p.char_look = { look.x/l, look.y/l, look.z/l };
                                }
                            }
                        }

                        p.role = 0;

                        RbxInstance player{p.player_ptr};
                        RbxInstance backpack = player.find_child_by_class("Backpack");
                        if (backpack.valid()) {
                            for (auto& child : backpack.get_children()) {
                                if (child.get_class() == "Tool") {
                                    std::string t_name = child.get_name();
                                    if (t_name == "Knife") {
                                        p.role = 1;
                                        break;
                                    } else if (t_name == "Gun") {
                                        p.role = 2;
                                        break;
                                    }
                                }
                            }
                        }

                        p.weapon.clear();
                        {
                            RbxInstance character{char_ptr};
                            for (auto& child : character.get_children()) {
                                if (child.get_class() != "Tool") continue;
                                std::string t_name = child.get_name();
                                if (p.weapon.empty()) p.weapon = t_name;
                                if (p.role == 0) {
                                    if (t_name == "Knife")     p.role = 1;
                                    else if (t_name == "Gun")  p.role = 2;
                                }
                            }
                        }
                    } else {
                        p.valid = false;
                        p.cached_char_ptr = 0;
                        p.hrp_ptr = 0;
                        p.humanoid_ptr = 0;
                        p.role = 0;
                    }
                }
            }
            g_players = g_players_temp;
        }

        tp::g_spin_radius.store(esp::cfg.tp_spin_radius);
        tp::g_spin_speed.store(esp::cfg.tp_spin_speed);
        if (menu::is_injected() && esp::cfg.tp_spin_enabled) {

            if (esp::cfg.tp_spin_cancel_key &&
                (GetAsyncKeyState(esp::cfg.tp_spin_cancel_key) & 1)) {
                esp::cfg.tp_spin_enabled = false;
                tp::stop_spin();
            } else {

                bool armed = (esp::cfg.tp_spin_key == 0) ||
                             (GetAsyncKeyState(esp::cfg.tp_spin_key) & 0x8000);
                uintptr_t tgt = armed ? tp::g_lock_target.load() : 0;
                if (!tgt && armed) tgt = tp::g_spectate_hum.load();
                if (tgt) tp::start_spin(tgt);
                else     tp::stop_spin();
            }
        } else if (tp::g_spin.load()) {
            tp::stop_spin();
        }

        tp::service();
        if (menu::is_injected()) feat::apply_self_mods();

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {

    {
        char buf[MAX_PATH]{};
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string p(buf);
        auto slash = p.find_last_of("\\/");
        if (slash != std::string::npos) SetCurrentDirectoryA(p.substr(0, slash).c_str());
    }

    printf("[UwUClient] Waiting for RobloxPlayerBeta.exe...\n");

    while (!mem::attach("RobloxPlayerBeta.exe")) {
        Sleep(1000);
    }

    mem::g_base = mem::get_module_base();
    printf("[UwUClient] Attached. PID=%lu  Base=0x%llX\n",
           mem::g_pid, (unsigned long long)mem::g_base);
    elog::ok("attached to RobloxPlayerBeta.exe pid=%lu base=0x%llX",
             mem::g_pid, (unsigned long long)mem::g_base);

    HWND roblox_hwnd = nullptr;
    for (int i = 0; i < 20; i++) {
        roblox_hwnd = find_roblox_window(mem::g_pid);
        if (roblox_hwnd) break;
        Sleep(500);
    }
    if (!roblox_hwnd) {
        MessageBoxA(nullptr, "Could not find Roblox window.", "UwUClient", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!overlay::init(roblox_hwnd, hInst)) {
        MessageBoxA(nullptr, "Failed to create overlay.", "UwUClient", MB_OK | MB_ICONERROR);
        return 1;
    }


    {
        std::string dir = mem::exe_dir();
        std::wstring wdir(dir.begin(), dir.end());
        bgimage::load(overlay::g_device, wdir + L"\\bg.jpg");
    }

    std::string client_ver;
    {
        std::string cpath = mem::module_path();
        auto vp = cpath.find("version-");
        if (vp != std::string::npos) {
            auto e = cpath.find_first_of("\\/", vp);
            client_ver = cpath.substr(vp, (e == std::string::npos ? cpath.size() : e) - vp);
        }
    }

    std::string cache = offset_updater::cache_dir();
    std::string offsets_txt_path  = cache + "\\offsets.txt";
    std::string offsets_json_path = cache + "\\offsets.json";

    if (offsets::load_txt_content(embedded_offsets::OFFSETS_TXT))
        elog::ok("offsets: loaded embedded baseline");

    std::string fetched_ver = offset_updater::try_update(cache, client_ver);
    if (!fetched_ver.empty()) offsets::roblox_version = fetched_ver;

    bool have_txt = offsets::load_txt(offsets_txt_path);
    if (have_txt) {
        printf("[UwUClient] Loaded offsets.txt from cache (version %s)\n", offsets::roblox_version.c_str());
        elog::ok("loaded offsets.txt from cache (version %s)", offsets::roblox_version.c_str());
    }
    bool have_json = have_txt || offsets::load(offsets_json_path);

    if (fflags::load(cache + "\\fflags.json"))
        elog::ok("fflags.json loaded (TargetFps rva=0x%llX)",
                 (unsigned long long)fflags::rva::TaskSchedulerTargetFps);
    else
        elog::warn("fflags.json not cached yet — FFlag slots use defaults until next network fetch");

    bool json_matches = have_txt || fetched_ver == client_ver
                        ? true
                        : have_json && !client_ver.empty() && client_ver == offsets::roblox_version;

    if (have_json) elog::info("offsets.json cache present (version %s)", offsets::roblox_version.c_str());
    else            elog::info("no offsets.json cache — using embedded baseline");

    std::string luavm_cache = cache + "\\luavm_offsets.txt";
    if (std::filesystem::exists(luavm_cache)) {
        luavm::load(luavm_cache);
    } else {
        luavm::load_content(embedded_offsets::LUAVM_TXT, "embedded");
    }
    elog::info("client version detected: %s", client_ver.empty() ? "(none)" : client_ver.c_str());
    elog::info("json version matches client: %s", json_matches ? "yes" : "no");

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        printf("[UwUClient] SHIFT held - forcing rescan.\n");
        elog::warn("SHIFT held at launch - forcing rescan");
        json_matches = false;
    }

    if (json_matches) {
        RbxDataModel probe;
        for (int i = 0; i < 10; i++) {
            probe = RbxDataModel::get();
            if (probe.valid()) {

                RbxInstance root{probe.ptr};
                auto ch = root.get_children();
                if (!ch.empty()) break;
            }
            Sleep(300);
            probe = RbxDataModel{};
        }
        if (!probe.valid()) {
            printf("[UwUClient] Cached DataModel chain unreachable. Forcing rescan.\n");
            elog::warn("cached DataModel chain unreachable - forcing rescan");
            json_matches = false;
        } else {
            elog::ok("cached DataModel chain OK at 0x%llX", (unsigned long long)probe.ptr);
        }
    }

    if (!json_matches) {
        std::thread([roblox_hwnd]() {
            for (int i = 0; i < 30; i++) {
                if (scanner::run(roblox_hwnd, true)) { g_scan_ok = true; break; }
                Sleep(1000);
            }
            g_scan_done = true;
        }).detach();
    } else {
        std::thread([]() {

            scanner::feed_status("Cached offsets loaded");
            scanner::g_phase = 2; Sleep(220);
            scanner::feed_status("Verifying DataModel chain");
            scanner::g_phase = 4; Sleep(220);
            scanner::feed_status("Ready");
            scanner::g_phase = scanner::TOTAL_PHASES; Sleep(320);
            g_scan_ok   = true;
            g_scan_done = true;
        }).detach();
    }

    std::string config_path = mem::exe_dir() + "\\config.bin";
    if (esp::load_config(config_path.c_str()))
        printf("[UwUClient] Loaded saved config.\n");

    std::thread([config_path]() {
        auto next = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (overlay::is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            auto now = std::chrono::steady_clock::now();
            if (now >= next) {
                esp::save_config(config_path.c_str());
                next = now + std::chrono::seconds(5);
            }
        }
        esp::save_config(config_path.c_str());
    }).detach();

    auto sanitize = [](std::string s) {
        for (auto& ch : s) if (ch < 32 || ch == '\\' || ch == '/' || ch == ':' ||
                               ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
                               ch == '>' || ch == '|') ch = '_';
        return s;
    };
    if (esp::cfg.per_game_profiles) {
        RbxDataModel dm = RbxDataModel::get();
        if (dm.valid()) {
            std::string gname = RbxInstance{dm.ptr}.get_name();
            if (!gname.empty()) {
                std::string pg_path = mem::exe_dir() + "\\preset_" + sanitize(gname) + ".bin";
                if (esp::load_config(pg_path.c_str())) {
                    printf("[UwUClient] Loaded per-game preset: %s\n", pg_path.c_str());
                    esp::notify("Loaded per-game preset");
                }
            }
        }
    }

    overlay::set_passthrough(false);

    std::thread(list_reader_thread).detach();
    std::thread(position_reader_thread).detach();
    std::thread([]() {
        while (overlay::is_running()) {
            if (!mem::alive()) break;
            if (menu::is_injected()) feat::apply_movement();
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }).detach();
    aimbot::start_thread();
    spotify::start();
    updater::check_async();

    std::thread(check::run).detach();

    std::thread([]() {
        for (;;) {
            Sleep(750);
            if (!mem::alive()) {
                elog::info("watchdog: Roblox is gone, exiting.");
                ExitProcess(0);
            }
        }
    }).detach();

    std::thread([]() {
        for (;;) {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32 pe{}; pe.dwSize = sizeof(pe);
                DWORD me = GetCurrentProcessId();
                if (Process32First(snap, &pe)) {
                    do {
                        if (_stricmp("RobloxCrashHandler.exe", pe.szExeFile) == 0 &&
                            pe.th32ProcessID != me) {
                            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                            if (h) { TerminateProcess(h, 0); CloseHandle(h); }
                        }
                    } while (Process32Next(snap, &pe));
                }
                CloseHandle(snap);
            }
            Sleep(1000);
        }
    }).detach();

    std::thread([roblox_hwnd]() {

        int counter = 0;
        while (mem::alive()) {
            Sleep(1000);
            if (!esp::cfg.antiafk) { counter = 0; continue; }
            if (++counter < 60) continue;
            counter = 0;
            if (roblox_hwnd) {
                PostMessageA(roblox_hwnd, WM_KEYDOWN, VK_F15, 0);
                PostMessageA(roblox_hwnd, WM_KEYUP,   VK_F15, 0);
            }
        }
    }).detach();

    esp::notify("UwUClient ready");

    menu::init();

    while (overlay::is_running()) {
        overlay::poll_messages();

        overlay::sync_to_target(roblox_hwnd);

        {
            HWND fg_now = GetForegroundWindow();
            bool rob_focus = fg_now == roblox_hwnd || fg_now == overlay::g_hwnd;
            if (menu::is_injected() && rob_focus && (GetAsyncKeyState(VK_RSHIFT) & 1)) {
                bool open = !g_menu_open.load();
                g_menu_open.store(open);
                overlay::set_passthrough(!open);
            }
        }
        if (GetAsyncKeyState(VK_F10) & 1) {
            esp::cfg.show_console = !esp::cfg.show_console;
            esp::notify(esp::cfg.show_console ? "Console: ON (F10)" : "Console: OFF (F10)");
        }

        if (GetAsyncKeyState(VK_F9) & 1) {
            executor::ping("UwUClient: F9 ping");
            esp::notify("Executor ping fired");
        }

        if (GetAsyncKeyState(VK_F8) & 1) {
            if (!dumper::g_dumping.load()) {
                std::string dir = mem::exe_dir() + "\\dumps";
                dumper::dump_datamodel_async(dir);
                esp::notify("DataModel dump started (F8)");
            } else {
                esp::notify("Dump already running…");
            }
        }
        if (GetAsyncKeyState(VK_END) & 1) {
            esp::save_config((mem::exe_dir() + "\\config.bin").c_str());
            overlay::quit();
            break;
        }

        if (GetAsyncKeyState(esp::cfg.key_fly) & 1) {
            esp::cfg.fly_enabled = !esp::cfg.fly_enabled;
            esp::notify(esp::cfg.fly_enabled ? "Fly: ON" : "Fly: OFF");
        }
        if (GetAsyncKeyState(esp::cfg.key_noclip) & 1) {
            esp::cfg.noclip_enabled = !esp::cfg.noclip_enabled;
            esp::notify(esp::cfg.noclip_enabled ? "No-Clip: ON" : "No-Clip: OFF");
        }
        if (GetAsyncKeyState(esp::cfg.key_lock) & 1) {
            esp::cfg.aim_lock = !esp::cfg.aim_lock;
            esp::notify(esp::cfg.aim_lock ? "Lock-On: ON" : "Lock-On: OFF");
        }
        if (GetAsyncKeyState(esp::cfg.key_esp) & 1) {
            esp::cfg.enabled = !esp::cfg.enabled;
            esp::notify(esp::cfg.enabled ? "ESP: ON" : "ESP: OFF");
        }
        if (esp::cfg.key_aim && (GetAsyncKeyState(esp::cfg.key_aim) & 1)) {
            esp::cfg.aim_enabled = !esp::cfg.aim_enabled;
            esp::notify(esp::cfg.aim_enabled ? "Aimbot: ON" : "Aimbot: OFF");
        }
        if (esp::cfg.key_trigger && (GetAsyncKeyState(esp::cfg.key_trigger) & 1)) {
            esp::cfg.trigger_enabled = !esp::cfg.trigger_enabled;
            esp::notify(esp::cfg.trigger_enabled ? "Triggerbot: ON" : "Triggerbot: OFF");
        }
        if (esp::cfg.key_radar && (GetAsyncKeyState(esp::cfg.key_radar) & 1)) {
            esp::cfg.radar_enabled = !esp::cfg.radar_enabled;
            esp::notify(esp::cfg.radar_enabled ? "Radar: ON" : "Radar: OFF");
        }
        if (esp::cfg.key_panic && (GetAsyncKeyState(esp::cfg.key_panic) & 1)) {
            static bool panicked = false;
            static esp::Config backup;
            panicked = !panicked;
            if (panicked) {
                backup = esp::cfg;

                bool* all[] = {
                    &esp::cfg.enabled,               &esp::cfg.aim_enabled,
                    &esp::cfg.trigger_enabled,       &esp::cfg.silent_aim,
                    &esp::cfg.aim_lock,              &esp::cfg.aim_autofire,
                    &esp::cfg.hitmarker_enabled,     &esp::cfg.killfeed_enabled,
                    &esp::cfg.chams_enabled,         &esp::cfg.fly_enabled,
                    &esp::cfg.noclip_enabled,        &esp::cfg.speed_enabled,
                    &esp::cfg.jump_enabled,          &esp::cfg.jump_height_enabled,
                    &esp::cfg.hover_enabled,         &esp::cfg.no_slope_limit,
                    &esp::cfg.infinite_jump,         &esp::cfg.bunny_hop,
                    &esp::cfg.freecam,               &esp::cfg.godmode,
                    &esp::cfg.antiaim_spin,          &esp::cfg.antiaim_jitter,
                    &esp::cfg.antiafk,               &esp::cfg.streamproof,
                    &esp::cfg.radar_enabled,         &esp::cfg.draw_hud,
                    &esp::cfg.draw_watermark,        &esp::cfg.draw_keybind_overlay,
                    &esp::cfg.sound_alerts,          &esp::cfg.fov_changer_enabled,
                    &esp::cfg.zoom_unlock,           &esp::cfg.lock_first_person,
                    &esp::cfg.world_nofog,           &esp::cfg.world_time_enabled,
                    &esp::cfg.world_gravity_enabled, &esp::cfg.world_bright_enabled,
                    &esp::cfg.fullbright,            &esp::cfg.no_skybox,
                    &esp::cfg.vis_no_blur,           &esp::cfg.vis_no_dof,
                    &esp::cfg.vis_no_sunrays,        &esp::cfg.vis_no_atmosphere,
                    &esp::cfg.fps_unlock,            &esp::cfg.ff_fullbright,
                    &esp::cfg.ff_gray_sky,           &esp::cfg.ff_no_adorns,
                    &esp::cfg.ff_no_grass,           &esp::cfg.ff_low_texture,
                };
                for (bool* b : all) *b = false;

                if (tp::g_lock.load())     tp::stop_lock();
                if (tp::g_spectate.load()) tp::stop_spectate();
                if (g_render_view) {
                    wpm<uint8_t>(g_render_view + offsets::RenderView::LightingValid, 1);
                    wpm<uint8_t>(g_render_view + offsets::RenderView::SkyboxValid, 1);
                }
                esp::notify("PANIC - all off");
            } else {
                esp::cfg = backup;
                if (g_render_view) {
                    wpm<uint8_t>(g_render_view + offsets::RenderView::LightingValid, esp::cfg.fullbright ? 0 : 1);
                    wpm<uint8_t>(g_render_view + offsets::RenderView::SkyboxValid, esp::cfg.no_skybox ? 0 : 1);
                }
                esp::notify("Restored");
            }
        }
        if (esp::cfg.key_tp_mouse && (GetAsyncKeyState(esp::cfg.key_tp_mouse) & 1)) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(roblox_hwnd, &pt);
            float W = g_screen.x, H = g_screen.y;
            uintptr_t cam = tp::local_camera();
            if (W > 1.f && H > 1.f && cam && offsets::Camera::CFrame && offsets::Camera::FieldOfView) {
                float cf[9]{};
                mem::vm_read(mem::g_proc, (LPCVOID)(cam + offsets::Camera::CFrame), cf, sizeof(cf), nullptr);
                Vec3 right = {  cf[0],  cf[3],  cf[6] };
                Vec3 up    = {  cf[1],  cf[4],  cf[7] };
                Vec3 look  = { -cf[2], -cf[5], -cf[8] };
                Vec3 campos = rpm<Vec3>(cam + offsets::Camera::CFrame + 0x24);
                float fov = rpm<float>(cam + offsets::Camera::FieldOfView);
                if (fov < 1.f || fov > 170.f) fov = 70.f;
                float ndcx = 2.f * (float)pt.x / W - 1.f;
                float ndcy = 1.f - 2.f * (float)pt.y / H;
                float ty = tanf(fov * 0.5f * 3.14159265f / 180.f);
                float tx = ty * (W / H);
                Vec3 ray = tp::normalize(Vec3{
                    look.x + right.x*ndcx*tx + up.x*ndcy*ty,
                    look.y + right.y*ndcx*tx + up.y*ndcy*ty,
                    look.z + right.z*ndcx*tx + up.z*ndcy*ty });
                Vec3 dest = { campos.x + ray.x*esp::cfg.tp_mouse_dist,
                              campos.y + ray.y*esp::cfg.tp_mouse_dist,
                              campos.z + ray.z*esp::cfg.tp_mouse_dist };
                tp::request_teleport(dest, 60);
                esp::notify("TP to cursor");
            }
        }

        static bool prev_sp = false;
        if (esp::cfg.streamproof != prev_sp) {
            overlay::set_streamproof(esp::cfg.streamproof);
            prev_sp = esp::cfg.streamproof;
        }

        overlay::begin_frame();

        HWND fg = GetForegroundWindow();
        bool roblox_focused =
            !menu::is_injected() ||
            fg == roblox_hwnd ||
            fg == overlay::g_hwnd;
        if (!roblox_focused) {
            overlay::end_frame();
            continue;
        }

        if (menu::is_injected()) {
            ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowPos({0.f, 0.f});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::SetNextWindowBgAlpha(0.f);
            ImGui::Begin("##esp", nullptr,
                ImGuiWindowFlags_NoDecoration          |
                ImGuiWindowFlags_NoInputs              |
                ImGuiWindowFlags_NoMove                |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNav);

            ImDrawList* dl = ImGui::GetWindowDrawList();

            {
                RbxVisualEngine ve = RbxVisualEngine::get();
                RbxDataModel    dm = RbxDataModel::get();

                int valid = 0;
                Vec3 first_pos{};
                std::string first_name;
                for (auto& p : g_players) {
                    if (p.valid) {
                        if (!valid) { first_pos = p.position; first_name = p.name; }
                        valid++;
                    }
                }

                char line1[160];
                snprintf(line1, sizeof(line1),
                    "UwUClient | ve:%s dm:%s | total:%d valid:%d | RSHIFT=menu END=exit",
                    ve.valid() ? "OK" : "FAIL",
                    dm.valid() ? "OK" : "FAIL",
                    (int)g_players.size(), valid);

                dl->AddText({11.f, 11.f}, IM_COL32(0,0,0,220), line1);
                dl->AddText({10.f, 10.f}, IM_COL32(255,255,255,255), line1);
            }

            {
                RbxVisualEngine ve_now = RbxVisualEngine::get();
                if (ve_now.valid()) {
                    ve_now.read_view_matrix(g_view_matrix);
                    Vec2 dims_now = ve_now.dimensions();
                    if (dims_now.x > 0.f && dims_now.y > 0.f) g_screen = dims_now;
                }
            }

            {
                POINT client_origin{ 0, 0 };
                if (roblox_hwnd && ClientToScreen(roblox_hwnd, &client_origin)) {
                    RECT wr{};
                    if (GetWindowRect(roblox_hwnd, &wr)) {
                        esp::set_view_origin({ (float)(client_origin.x - wr.left),
                                               (float)(client_origin.y - wr.top) });
                    }
                }
            }

            if (g_screen.x > 0.f && g_screen.y > 0.f)
                esp::render(dl, g_players, g_view_matrix, g_screen);

            ImGui::End();
        }

        bool need_cursor = g_menu_open.load() || esp::cfg.show_console
                           || !menu::is_injected();
        ImGui::GetIO().MouseDrawCursor = need_cursor;

        menu::set_scan_ok(g_scan_ok.load());

        if (!menu::is_injected()) overlay::set_passthrough(false);

        menu::set_visible(g_menu_open.load());
        menu::render(&g_players, g_render_view);

        if (menu::is_injected() && !menu::is_visible() && g_menu_open.load()) {
            g_menu_open.store(false);
            overlay::set_passthrough(true);
        }

        if (esp::cfg.show_console) esp::render_console(g_players, g_screen);

        overlay::end_frame();
    }

    mem::detach();
    return 0;
}
