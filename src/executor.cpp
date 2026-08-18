
#include "../include/executor.hpp"
#include "../include/luavm.hpp"
#include "../include/memory.hpp"
#include "../include/offsets.hpp"
#include "../include/roblox.hpp"
#include "../include/log.hpp"
#include <Windows.h>
#include <vector>
#include <string>
#include <mutex>

namespace executor {

static std::mutex        g_mtx;
static uintptr_t         g_lua_state = 0;
static bool              g_tried_resolve = false;

struct Blob {
    std::vector<uint8_t> bytes;
    void u8 (uint8_t v)  { bytes.push_back(v); }
    void u16(uint16_t v) { bytes.push_back(v & 0xFF); bytes.push_back(v >> 8); }
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) bytes.push_back((v >> (i*8)) & 0xFF); }
    void u64(uint64_t v) { for (int i = 0; i < 8; i++) bytes.push_back((v >> (i*8)) & 0xFF); }

    void mov_rax(uint64_t v) { u8(0x48); u8(0xB8); u64(v); }

    void mov_rcx(uint64_t v) { u8(0x48); u8(0xB9); u64(v); }

    void mov_rdx(uint64_t v) { u8(0x48); u8(0xBA); u64(v); }

    void mov_r8 (uint64_t v) { u8(0x49); u8(0xB8); u64(v); }

    void mov_r9 (uint64_t v) { u8(0x49); u8(0xB9); u64(v); }

    void call_rax()          { u8(0xFF); u8(0xD0); }

    void sub_rsp(uint8_t v)  { u8(0x48); u8(0x83); u8(0xEC); u8(v); }

    void add_rsp(uint8_t v)  { u8(0x48); u8(0x83); u8(0xC4); u8(v); }

    void ret()               { u8(0xC3); }

    void mov_mem_rax(uint64_t addr) { u8(0x48); u8(0xA3); u64(addr); }
};

static DWORD fire_shellcode(const std::vector<uint8_t>& code) {
    LPVOID rem = mem::vm_alloc(mem::g_proc, nullptr, code.size(),
                                MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!rem) { elog::err("executor: alloc shellcode failed err=%lu", GetLastError()); return 0; }

    if (!mem::vm_write(mem::g_proc, rem, code.data(), code.size(), nullptr)) {
        elog::err("executor: write shellcode failed err=%lu", GetLastError());
        mem::vm_free(mem::g_proc, rem, 0, MEM_RELEASE);
        return 0;
    }

    HANDLE th = mem::remote_thread(mem::g_proc, nullptr, 0,
                                   (LPTHREAD_START_ROUTINE)rem, nullptr, 0, nullptr);
    if (!th) {
        elog::err("executor: CreateRemoteThread failed err=%lu", GetLastError());
        mem::vm_free(mem::g_proc, rem, 0, MEM_RELEASE);
        return 0;
    }
    WaitForSingleObject(th, 5000);
    DWORD ec = 0;
    GetExitCodeThread(th, &ec);
    CloseHandle(th);
    mem::vm_free(mem::g_proc, rem, 0, MEM_RELEASE);
    return ec;
}

enum class GgsAbi { PointerArgs, ImmediateArgs, ScOnly };
static uintptr_t try_ggs(RbxInstance sc_inst, GgsAbi abi) {
    LPVOID scratch = mem::vm_alloc(mem::g_proc, nullptr, 64,
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!scratch) return 0;
    uint64_t init[3] = { 8, 0, 0 };
    mem::vm_write(mem::g_proc, scratch, init, sizeof(init), nullptr);

    Blob sc;
    sc.sub_rsp(0x28);
    sc.mov_rcx((uint64_t)sc_inst.ptr);
    switch (abi) {
        case GgsAbi::PointerArgs:
            sc.mov_rdx((uint64_t)scratch);
            sc.mov_r8 ((uint64_t)scratch + 8);
            break;
        case GgsAbi::ImmediateArgs:
            sc.mov_rdx(8);
            sc.mov_r8 (0);
            break;
        case GgsAbi::ScOnly:

            break;
    }
    sc.mov_rax(luavm::abs(luavm::GetGlobalState));
    sc.call_rax();
    sc.mov_mem_rax((uint64_t)scratch + 16);
    sc.add_rsp(0x28);
    sc.u8(0x33); sc.u8(0xC0);
    sc.ret();

    DWORD rc = fire_shellcode(sc.bytes);
    (void)rc;

    uint64_t result = 0;
    mem::vm_read(mem::g_proc, (LPBYTE)scratch + 16, &result, sizeof(result), nullptr);
    mem::vm_free(mem::g_proc, scratch, 0, MEM_RELEASE);

    if (result > 0x10000 && result < 0x7FFFFFFFFFFFULL) return (uintptr_t)result;
    return 0;
}

static uintptr_t resolve_lua_state() {
    if (!luavm::GetGlobalState) { elog::err("executor: GetGlobalState offset not loaded"); return 0; }

    RbxDataModel dm = RbxDataModel::get();
    if (!dm.valid()) { elog::err("executor: DataModel unreachable"); return 0; }

    RbxInstance sc_inst = dm.get_service("ScriptContext");
    if (!sc_inst.valid()) {
        elog::err("executor: ScriptContext service not found under DataModel");
        return 0;
    }
    elog::info("executor: ScriptContext @ 0x%llX", (unsigned long long)sc_inst.ptr);

    struct Try { GgsAbi abi; const char* name; };
    Try tries[] = {
        { GgsAbi::ImmediateArgs, "immediate (rcx=sc, rdx=8, r8=0)" },
        { GgsAbi::PointerArgs,   "pointer (rcx=sc, rdx=&id, r8=&script)" },
        { GgsAbi::ScOnly,        "sc-only (rcx=sc)" },
    };
    for (auto& t : tries) {
        uintptr_t result = try_ggs(sc_inst, t.abi);
        if (result) {
            elog::ok("executor: lua_state @ 0x%llX via %s", (unsigned long long)result, t.name);
            return result;
        }
        elog::warn("executor: GetGlobalState via %s returned 0/invalid", t.name);
    }

    elog::err("executor: no GetGlobalState ABI worked. Offset likely stale.");
    return 0;
}

bool ready() {
    return g_lua_state != 0;
}
uintptr_t lua_state() { return g_lua_state; }

void refresh_state() {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_tried_resolve = false;
    g_lua_state = 0;
}

static uintptr_t ensure_state() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_lua_state) return g_lua_state;
    if (g_tried_resolve) return 0;
    g_tried_resolve = true;
    if (!luavm::loaded()) { elog::err("executor: luavm offsets missing"); return 0; }
    g_lua_state = resolve_lua_state();
    return g_lua_state;
}

bool ping(const std::string& msg) {
    uintptr_t L = ensure_state();
    if (!L) return false;

    LPVOID msg_rem = mem::vm_alloc(mem::g_proc, nullptr, msg.size() + 1,
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!msg_rem) { elog::err("ping: msg alloc failed"); return false; }
    mem::vm_write(mem::g_proc, msg_rem, msg.c_str(), msg.size() + 1, nullptr);

    Blob sc;
    sc.sub_rsp(0x28);
    sc.mov_rcx((uint64_t)L);
    sc.mov_rdx((uint64_t)msg_rem);
    sc.mov_rax(luavm::abs(luavm::Print));
    sc.call_rax();
    sc.add_rsp(0x28);
    sc.u8(0x33); sc.u8(0xC0);
    sc.ret();

    DWORD rc = fire_shellcode(sc.bytes);
    mem::vm_free(mem::g_proc, msg_rem, 0, MEM_RELEASE);
    elog::ok("ping: sent (thread exit=0x%lX) — check Roblox F9 console", rc);
    return true;
}

bool execute_source(const std::string& source) {
    uintptr_t L = ensure_state();
    if (!L) return false;
    if (!luavm::LuaVM_Load) { elog::err("execute: LuaVM_Load offset missing"); return false; }
    if (source.empty()) return false;

    elog::info("execute: %zu-byte payload", source.size());

    static const char CHUNK[] = "=ext_exec";
    size_t chunk_len = sizeof(CHUNK);
    size_t src_len   = source.size() + 1;
    size_t total     = chunk_len + src_len + 16;

    LPVOID rem = mem::vm_alloc(mem::g_proc, nullptr, total,
                                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!rem) { elog::err("execute: alloc failed"); return false; }
    uint8_t* base = (uint8_t*)rem;
    uint8_t* chunk_r  = base;
    uint8_t* source_r = base + chunk_len;
    uint8_t* scratch  = source_r + src_len;

    mem::vm_write(mem::g_proc, chunk_r,  CHUNK,          chunk_len, nullptr);
    mem::vm_write(mem::g_proc, source_r, source.c_str(), src_len,   nullptr);

    Blob sc;
    sc.sub_rsp(0x38);
    sc.mov_rcx((uint64_t)L);
    sc.mov_rdx((uint64_t)source_r);
    sc.mov_r8 ((uint64_t)chunk_r);
    sc.mov_r9 (0);
    sc.mov_rax(luavm::abs(luavm::LuaVM_Load));
    sc.call_rax();
    sc.mov_mem_rax((uint64_t)scratch);

    sc.mov_rcx((uint64_t)L);
    sc.mov_rax(luavm::abs(luavm::luau_execute));
    sc.call_rax();

    sc.add_rsp(0x38);
    sc.u8(0x33); sc.u8(0xC0);
    sc.ret();

    DWORD rc = fire_shellcode(sc.bytes);

    uint64_t load_result = 0;
    mem::vm_read(mem::g_proc, scratch, &load_result, sizeof(load_result), nullptr);
    mem::vm_free(mem::g_proc, rem, 0, MEM_RELEASE);

    elog::ok("execute: thread=0x%lX  LuaVM_Load=0x%llX (nonzero == failure)",
             rc, (unsigned long long)load_result);
    return true;
}

}
