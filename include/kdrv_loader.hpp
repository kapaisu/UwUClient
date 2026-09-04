#pragma once
#include <Windows.h>
#include <string>
#include <fstream>
#include "log.hpp"
#include "memory.hpp"

namespace kdrv_loader {

inline const char* SERVICE_NAME = "uwudrv";

inline bool is_elevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD sz = 0;
    bool ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &sz) && el.TokenIsElevated;
    CloseHandle(tok);
    return ok;
}

inline std::string tmp_path() {
    char buf[MAX_PATH]{};
    GetTempPathA(MAX_PATH, buf);
    return std::string(buf) + "uwudrv.sys";
}

inline std::string beside_exe(const char* name) {
    char buf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    auto slash = p.find_last_of("\\/");
    return (slash == std::string::npos ? std::string(".") : p.substr(0, slash)) + "\\" + name;
}

inline bool file_exists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool extract_embedded_sys(const std::string& dst) {
    HRSRC h = FindResourceA(nullptr, "UWUDRV_SYS", MAKEINTRESOURCEA(RT_RCDATA));
    if (!h) return false;
    HGLOBAL hg = LoadResource(nullptr, h);
    if (!hg) return false;
    DWORD size = SizeofResource(nullptr, h);
    if (size < 512) return false;
    void* data = LockResource(hg);
    if (!data) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write((const char*)data, size);
    return out.good();
}

inline bool service_running(SC_HANDLE svc) {
    SERVICE_STATUS st{};
    if (!QueryServiceStatus(svc, &st)) return false;
    return st.dwCurrentState == SERVICE_RUNNING || st.dwCurrentState == SERVICE_START_PENDING;
}

inline bool sc_load(const std::string& sys_path) {
    SC_HANDLE mgr = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!mgr) {
        elog::warn("kdrv_loader: OpenSCManager failed err=%lu (need admin)", GetLastError());
        return false;
    }
    SC_HANDLE svc = CreateServiceA(
        mgr, SERVICE_NAME, SERVICE_NAME,
        SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        sys_path.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!svc) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            svc = OpenServiceA(mgr, SERVICE_NAME, SERVICE_ALL_ACCESS);
        }
        if (!svc) {
            elog::warn("kdrv_loader: CreateService failed err=%lu", err);
            CloseServiceHandle(mgr);
            return false;
        }
    }
    bool ok = false;
    if (service_running(svc)) {
        ok = true;
    } else if (StartServiceA(svc, 0, nullptr)) {
        ok = true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            ok = true;
        } else {
            elog::warn("kdrv_loader: StartService failed err=%lu (0x%lX)", err, err);
            if (err == 577) {
                elog::warn("kdrv_loader: driver rejected as unsigned. Enable test-signing (bcdedit /set testsigning on + reboot) or map with kdmapper.");
            }
        }
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(mgr);
    return ok;
}

inline void sc_unload() {
    SC_HANDLE mgr = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!mgr) return;
    SC_HANDLE svc = OpenServiceA(mgr, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (svc) {
        SERVICE_STATUS st{};
        ControlService(svc, SERVICE_CONTROL_STOP, &st);
        DeleteService(svc);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(mgr);
}

inline bool try_load() {
    if (mem::kdrv_ping()) {
        elog::ok("kdrv_loader: driver already loaded");
        return true;
    }
    if (!is_elevated()) {
        elog::warn("kdrv_loader: not elevated. Right-click UwUClient.exe -> Run as administrator.");
        return false;
    }
    std::string sys;
    if (extract_embedded_sys(tmp_path())) {
        sys = tmp_path();
        elog::info("kdrv_loader: extracted embedded uwudrv.sys -> %s", sys.c_str());
    } else {
        std::string adj = beside_exe("uwudrv.sys");
        if (file_exists(adj)) {
            sys = adj;
            elog::info("kdrv_loader: using adjacent %s", sys.c_str());
        } else {
            elog::warn("kdrv_loader: no embedded .sys and no uwudrv.sys next to exe. Compile driver/uwudrv.vcxproj (needs WDK) and drop the .sys next to UwUClient.exe.");
            return false;
        }
    }
    if (!sc_load(sys)) return false;
    for (int i = 0; i < 40; i++) {
        if (mem::kdrv_ping()) {
            elog::ok("kdrv_loader: driver up and responding to ping");
            return true;
        }
        Sleep(50);
    }
    elog::warn("kdrv_loader: SCM says started but ping never came back");
    return false;
}

}
