#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <string>

namespace mem {
    inline HANDLE    g_proc = nullptr;
    inline DWORD     g_pid  = 0;
    inline uintptr_t g_base = 0;

    using NtReadVM_t  = LONG (__stdcall*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
    using NtWriteVM_t = LONG (__stdcall*)(HANDLE, PVOID, LPCVOID, SIZE_T, PSIZE_T);

    using VirtualAllocEx_t     = LPVOID (WINAPI*)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD);
    using VirtualFreeEx_t      = BOOL   (WINAPI*)(HANDLE, LPVOID, SIZE_T, DWORD);
    using CreateRemoteThread_t = HANDLE (WINAPI*)(HANDLE, LPSECURITY_ATTRIBUTES,
                                                  SIZE_T, LPTHREAD_START_ROUTINE,
                                                  LPVOID, DWORD, LPDWORD);

    inline NtReadVM_t          nt_read  = nullptr;
    inline NtWriteVM_t         nt_write = nullptr;
    inline VirtualAllocEx_t    vaex_fn  = nullptr;
    inline VirtualFreeEx_t     vfex_fn  = nullptr;
    inline CreateRemoteThread_t crt_fn  = nullptr;

    inline void init_syscalls() {
        if (nt_read && nt_write && vaex_fn && vfex_fn && crt_fn) return;
        HMODULE nt = GetModuleHandleA("ntdll.dll");
        if (!nt) nt = LoadLibraryA("ntdll.dll");
        if (nt) {
            if (!nt_read)  nt_read  = (NtReadVM_t) GetProcAddress(nt, "NtReadVirtualMemory");
            if (!nt_write) nt_write = (NtWriteVM_t)GetProcAddress(nt, "NtWriteVirtualMemory");
        }
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        if (!k32) k32 = LoadLibraryA("kernel32.dll");
        if (k32) {
            if (!vaex_fn) vaex_fn = (VirtualAllocEx_t)   GetProcAddress(k32, "VirtualAllocEx");
            if (!vfex_fn) vfex_fn = (VirtualFreeEx_t)    GetProcAddress(k32, "VirtualFreeEx");
            if (!crt_fn)  crt_fn  = (CreateRemoteThread_t)GetProcAddress(k32, "CreateRemoteThread");
        }
    }

    inline LPVOID vm_alloc(HANDLE h, LPVOID a, SIZE_T s, DWORD t, DWORD p) {
        if (!vaex_fn) init_syscalls();
        return vaex_fn ? vaex_fn(h, a, s, t, p) : nullptr;
    }
    inline BOOL vm_free(HANDLE h, LPVOID a, SIZE_T s, DWORD t) {
        if (!vfex_fn) init_syscalls();
        return vfex_fn ? vfex_fn(h, a, s, t) : FALSE;
    }
    inline HANDLE remote_thread(HANDLE h, LPSECURITY_ATTRIBUTES sa, SIZE_T st,
                                LPTHREAD_START_ROUTINE fn, LPVOID p,
                                DWORD flags, LPDWORD tid) {
        if (!crt_fn) init_syscalls();
        return crt_fn ? crt_fn(h, sa, st, fn, p, flags, tid) : nullptr;
    }

    inline BOOL vm_read(HANDLE h, LPCVOID addr, LPVOID buf,
                        SIZE_T size, SIZE_T* out) {
        if (!nt_read) init_syscalls();
        if (!nt_read) return FALSE;
        SIZE_T got = 0;
        LONG st = nt_read(h, (PVOID)addr, buf, size, &got);
        if (out) *out = got;
        return st >= 0;
    }

    inline BOOL vm_write(HANDLE h, LPVOID addr, LPCVOID buf,
                         SIZE_T size, SIZE_T* out) {
        if (!nt_write) init_syscalls();
        if (!nt_write) return FALSE;
        SIZE_T wrote = 0;
        LONG st = nt_write(h, addr, (LPCVOID)buf, size, &wrote);
        if (out) *out = wrote;
        return st >= 0;
    }

    inline bool has_visible_window(DWORD pid) {
        struct FindData { DWORD pid; bool found; };
        FindData fd{pid, false};
        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            auto* fd = (FindData*)lp;
            DWORD wpid = 0;
            GetWindowThreadProcessId(hwnd, &wpid);
            if (wpid == fd->pid && IsWindowVisible(hwnd)) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                if (rc.right > 200 && rc.bottom > 200) {
                    fd->found = true;
                    return FALSE;
                }
            }
            return TRUE;
        }, (LPARAM)&fd);
        return fd.found;
    }

    inline bool attach(const char* proc_name) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32 pe{ sizeof(pe) };
        DWORD target_pid = 0;
        if (Process32First(snap, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, proc_name) == 0) {
                    if (!target_pid) {
                        target_pid = pe.th32ProcessID;
                    }
                    if (has_visible_window(pe.th32ProcessID)) {
                        target_pid = pe.th32ProcessID;
                        break;
                    }
                }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
        if (!target_pid) return false;

        g_pid = target_pid;
        const DWORD RIGHTS =
            PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
            PROCESS_QUERY_LIMITED_INFORMATION;
        g_proc = OpenProcess(RIGHTS, FALSE, g_pid);
        if (g_proc) init_syscalls();
        return g_proc != nullptr;
    }

    inline uintptr_t get_module_base(const char* mod_name = nullptr) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, g_pid);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        MODULEENTRY32 me{ sizeof(me) };
        uintptr_t result = 0;
        if (Module32First(snap, &me)) {
            do {
                if (!mod_name || _stricmp(me.szModule, mod_name) == 0) {
                    result = (uintptr_t)me.modBaseAddr;
                    break;
                }
            } while (Module32Next(snap, &me));
        }
        CloseHandle(snap);
        return result;
    }

    inline bool alive() {
        if (!g_proc) return false;
        DWORD code = 0;
        return GetExitCodeProcess(g_proc, &code) && code == STILL_ACTIVE;
    }

    inline void detach() {
        if (g_proc) { CloseHandle(g_proc); g_proc = nullptr; }
        g_pid = g_base = 0;
    }

    inline void module_range(uintptr_t& start, uintptr_t& end) {
        start = end = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, g_pid);
        if (snap == INVALID_HANDLE_VALUE) return;
        MODULEENTRY32 me{ sizeof(me) };
        if (Module32First(snap, &me)) { start = (uintptr_t)me.modBaseAddr; end = start + me.modBaseSize; }
        CloseHandle(snap);
    }

    inline std::string module_path() {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, g_pid);
        if (snap == INVALID_HANDLE_VALUE) return {};
        MODULEENTRY32 me{ sizeof(me) };
        std::string path;
        if (Module32First(snap, &me)) path = me.szExePath;
        CloseHandle(snap);
        return path;
    }

    inline std::string exe_dir() {
        char buf[MAX_PATH]{};
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string p(buf);
        auto slash = p.find_last_of("\\/");
        return (slash == std::string::npos) ? std::string(".") : p.substr(0, slash);
    }
}

template<typename T>
__forceinline T rpm(uintptr_t addr) {
    T val{};
    mem::vm_read(mem::g_proc, (LPCVOID)addr, &val, sizeof(T), nullptr);
    return val;
}

template<typename T>
__forceinline bool wpm(uintptr_t addr, const T& val) {
    return mem::vm_write(mem::g_proc, (LPVOID)addr, &val, sizeof(T), nullptr) != 0;
}

template<typename T>
inline T read_chain(uintptr_t base, std::initializer_list<uintptr_t> offsets) {
    uintptr_t cur = base;
    const uintptr_t* it  = offsets.begin();
    const uintptr_t* end = offsets.end();
    while (it != end) {
        uintptr_t off = *it++;
        if (it == end) return rpm<T>(cur + off);
        cur = rpm<uintptr_t>(cur + off);
        if (!cur) return T{};
    }
    return T{};
}

inline std::string rpm_string(uintptr_t addr) {
    if (!addr) return {};
    size_t len = rpm<size_t>(addr + 0x10);
    if (!len || len > 256) return {};

    uintptr_t src = (len < 16) ? addr : rpm<uintptr_t>(addr);
    if (!src) return {};

    std::string out(len, '\0');
    mem::vm_read(mem::g_proc, (LPCVOID)src, out.data(), len, nullptr);
    return out;
}
