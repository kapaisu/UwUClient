#pragma once
#include "memory.hpp"
#include "offsets.hpp"
#include "log.hpp"
#include <Windows.h>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <unordered_set>

namespace scanner {

inline std::atomic<int> g_phase{0};
constexpr int TOTAL_PHASES = 6;
inline const char* phase_name(int p) {
    switch (p) {
        case 0: return "Starting...";
        case 1: return "Reading game module";
        case 2: return "Scanning VisualEngine";
        case 3: return "Scanning instance tree";
        case 4: return "Resolving DataModel";
        case 5: return "Resolving properties";
        default: return "Done";
    }
}

inline bool valid(uintptr_t a) { return a > 0x10000 && a < 0x7FFFFFFFFFFFULL; }

struct FeedLine { std::string text; bool hit; };
inline std::mutex g_feed_mtx;
inline std::vector<FeedLine> g_feed;

inline void feed_status(const std::string& s) {
    {
        std::lock_guard<std::mutex> lk(g_feed_mtx);
        g_feed.push_back({ s, false });
        if (g_feed.size() > 256) g_feed.erase(g_feed.begin());
    }
    elog::info("scan: %s", s.c_str());
}
inline void feed(const char* name, uintptr_t val) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%-26s 0x%llX", name, (unsigned long long)val);
    {
        std::lock_guard<std::mutex> lk(g_feed_mtx);
        g_feed.push_back({ buf, val != 0 });
        if (g_feed.size() > 256) g_feed.erase(g_feed.begin());
    }
    if (val) elog::ok("scan: %s = 0x%llX", name, (unsigned long long)val);
    else     elog::warn("scan: %s unresolved", name);
}
inline std::vector<FeedLine> feed_snapshot() {
    std::lock_guard<std::mutex> lk(g_feed_mtx);
    return g_feed;
}
inline void feed_clear() {
    std::lock_guard<std::mutex> lk(g_feed_mtx);
    g_feed.clear();
}

template <typename F>
void each_region(F f) {
    uintptr_t addr = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    while (VirtualQueryEx(mem::g_proc, (LPCVOID)addr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT)
            f((uintptr_t)mbi.BaseAddress, (size_t)mbi.RegionSize, mbi.Protect);
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }
}

inline std::vector<uint8_t> read_region(uintptr_t base, size_t size) {
    if (size == 0 || size > 0x20000000) return {};
    std::vector<uint8_t> buf(size);
    SIZE_T got = 0;
    if (!mem::vm_read(mem::g_proc, (LPCVOID)base, buf.data(), size, &got)) return {};
    buf.resize(got);
    return buf;
}

inline void find_all(const std::vector<uint8_t>& data, const void* pat, size_t len,
                     std::vector<size_t>& out) {
    if (data.size() < len) return;
    for (size_t i = 0; i + len <= data.size(); i++)
        if (memcmp(data.data() + i, pat, len) == 0) out.push_back(i);
}

struct Blob { uintptr_t base; std::vector<uint8_t> data; bool writable = false; };

inline bool prot_readable(DWORD prot) {
    if (prot & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    DWORD p = prot & 0xFF;
    return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
           p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

inline std::vector<Blob> read_module() {
    std::vector<Blob> out;
    size_t total = 0;
    uintptr_t bs, be; mem::module_range(bs, be);
    each_region([&](uintptr_t base, size_t size, DWORD prot) {
        if (base < bs || base >= be) return;
        if (!prot_readable(prot)) return;
        auto d = read_region(base, size);
        if (!d.empty()) {
            total += d.size();
            DWORD p = prot & 0xFF;
            bool w = (p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
                      p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY);
            out.push_back({ base, std::move(d), w });
        }
    });
    { char b[80]; snprintf(b, sizeof(b), "module cached: %zu regions, %zu KB",
                           out.size(), total / 1024); feed_status(b); }
    return out;
}

inline std::vector<uintptr_t> find_qword_all(const std::vector<Blob>& mod, uint64_t val) {
    std::vector<uintptr_t> writable_hits, readonly_hits;
    for (auto& b : mod) {
        std::vector<size_t> hits;
        find_all(b.data, &val, 8, hits);
        for (size_t idx : hits) {
            if (b.writable) writable_hits.push_back(b.base + idx);
            else            readonly_hits.push_back(b.base + idx);
        }
    }
    writable_hits.insert(writable_hits.end(), readonly_hits.begin(), readonly_hits.end());
    return writable_hits;
}

inline uintptr_t find_qword_verified(const std::vector<Blob>& mod, uintptr_t val,
                                     uintptr_t base_start) {
    auto hits = find_qword_all(mod, (uint64_t)val);
    for (uintptr_t h : hits) {
        uintptr_t off = h - base_start;
        uintptr_t got = rpm<uintptr_t>(base_start + off);
        if (got == val) return h;
    }
    return 0;
}

inline uintptr_t find_qword(const std::vector<Blob>& mod, uint64_t val) {
    auto hits = find_qword_all(mod, val);
    return hits.empty() ? 0 : hits.front();
}

inline std::string read_rbx_str(uintptr_t p) {
    if (!valid(p)) return {};
    uint32_t len = rpm<uint32_t>(p + 16);
    if (len == 0 || len > 128) return {};
    uintptr_t src = (len > 15) ? rpm<uintptr_t>(p) : p;
    if (!valid(src)) return {};
    char buf[129]{};
    mem::vm_read(mem::g_proc, (LPCVOID)src, buf, len, nullptr);
    return std::string(buf, len);
}
inline std::string get_class(uintptr_t inst) {
    if (!valid(inst)) return {};
    uintptr_t desc = rpm<uintptr_t>(inst + offsets::Instance::ClassDescriptor);
    if (!valid(desc)) return {};
    uintptr_t cn = rpm<uintptr_t>(desc + offsets::ClassDescriptor::Name);
    return read_rbx_str(cn);
}
inline std::string get_name(uintptr_t inst) {
    if (!valid(inst) || !offsets::Instance::Name) return {};
    return read_rbx_str(inst + offsets::Instance::Name);
}
inline std::vector<uintptr_t> get_children(uintptr_t inst) {
    std::vector<uintptr_t> out;
    if (!valid(inst) || !offsets::Instance::Children) return out;
    uintptr_t cp = rpm<uintptr_t>(inst + offsets::Instance::Children);
    if (!valid(cp)) return out;
    uintptr_t top = rpm<uintptr_t>(cp);
    uintptr_t end = rpm<uintptr_t>(cp + 8);
    if (!valid(top) || end <= top || (end - top) > 0x100000) return out;
    for (uintptr_t c = top; c < end; c += 16) {
        uintptr_t child = rpm<uintptr_t>(c);
        if (valid(child)) out.push_back(child);
    }
    return out;
}
inline uintptr_t find_child_class(uintptr_t inst, const char* cls) {
    for (auto c : get_children(inst)) if (get_class(c) == cls) return c;
    return 0;
}
inline uintptr_t find_child_name(uintptr_t inst, const char* nm) {
    for (auto c : get_children(inst)) if (get_name(c) == nm) return c;
    return 0;
}

inline bool is_view_matrix(uintptr_t addr) {
    float vm[16];
    if (!mem::vm_read(mem::g_proc, (LPCVOID)addr, vm, sizeof(vm), nullptr)) return false;
    float r2 = std::sqrt(vm[12]*vm[12] + vm[13]*vm[13] + vm[14]*vm[14]);
    if (!(r2 > 0.95f && r2 < 1.05f)) return false;
    float d02 = vm[0]*vm[12] + vm[1]*vm[13] + vm[2]*vm[14];
    float d12 = vm[4]*vm[12] + vm[5]*vm[13] + vm[6]*vm[14];
    return std::fabs(d02) < 0.05f && std::fabs(d12) < 0.05f;
}

inline bool is_cframe(uintptr_t addr) {
    float m[9];
    if (!mem::vm_read(mem::g_proc, (LPCVOID)addr, m, sizeof(m), nullptr)) return false;
    float c0 = std::sqrt(m[0]*m[0] + m[3]*m[3] + m[6]*m[6]);
    float c2 = std::sqrt(m[2]*m[2] + m[5]*m[5] + m[8]*m[8]);
    return c0 > 0.95f && c0 < 1.05f && c2 > 0.95f && c2 < 1.05f;
}

inline bool looks_like_res(float fw, float fh) {
    if (!(fw >= 640.f && fw <= 7680.f && fh >= 360.f && fh <= 4320.f)) return false;
    if (fw != std::floor(fw) || fh != std::floor(fh)) return false;
    float ar = fw / fh;
    return ar >= 1.15f && ar <= 2.60f;
}

inline uintptr_t scan_visual_engine(HWND hwnd, const std::vector<Blob>& mod) {
    RECT rc{}; if (hwnd) GetClientRect(hwnd, &rc);
    float cw = (float)rc.right, ch = (float)rc.bottom;

    struct Res { float w, h; };
    std::vector<Res> res;
    auto add = [&](float w, float h) {
        if (w < 320.f || h < 240.f) return;
        for (auto& r : res) if (r.w == w && r.h == h) return;
        res.push_back({ w, h });
    };
    add(cw, ch);
    if (hwnd) { RECT wr{}; GetWindowRect(hwnd, &wr);
                add((float)(wr.right - wr.left), (float)(wr.bottom - wr.top)); }
    add((float)GetSystemMetrics(SM_CXSCREEN), (float)GetSystemMetrics(SM_CYSCREEN));
    const Res common[] = { {1920,1080},{1280,720},{1600,900},{2560,1440},
                           {1366,768},{1440,900},{1536,864},{3840,2160},{1024,768} };
    for (auto& r : common) add(r.w, r.h);

    { char b[72]; snprintf(b, sizeof(b), "client %gx%g, %zu res candidates", cw, ch, res.size());
      feed_status(b); }

    uintptr_t base_start, base_end;
    mem::module_range(base_start, base_end);

    std::vector<std::tuple<uintptr_t, uintptr_t, uintptr_t>> cands;
    const uintptr_t dim_offs[] = { 0xAB0, 0xAB8, 0xAA0, 0xAC0, 0x720, 0x718, 0x728, 0x2F0, 0x2E8, 0x2F8 };
    size_t dim_hits = 0;

    auto test_dim = [&](uintptr_t dim_addr) {
        for (uintptr_t d : dim_offs) {
            uintptr_t ve = dim_addr - d;
            if (is_view_matrix(ve + 0x150)) cands.emplace_back(ve, d, 0x150);
        }
    };

    each_region([&](uintptr_t base, size_t size, DWORD prot) {
        if (prot != PAGE_READWRITE) return;
        auto data = read_region(base, size);
        if (data.empty()) return;
        for (auto& r : res) {
            uint8_t pat[8]; memcpy(pat, &r.w, 4); memcpy(pat + 4, &r.h, 4);
            std::vector<size_t> hits;
            find_all(data, pat, 8, hits);
            for (size_t idx : hits) { dim_hits++; test_dim(base + idx); }
        }
    });

    { char b[80]; snprintf(b, sizeof(b), "%zu dim hits, %zu VE candidates", dim_hits, cands.size());
      feed_status(b); }

    if (cands.empty()) {
        feed_status("no exact match - loose scanning for render size...");
        int probes = 0;
        each_region([&](uintptr_t base, size_t size, DWORD prot) {
            if (!cands.empty() || probes > 4000) return;
            if (prot != PAGE_READWRITE) return;
            auto data = read_region(base, size);
            if (data.size() < 8) return;
            for (size_t i = 0; i + 8 <= data.size(); i += 4) {
                float fw, fh;
                memcpy(&fw, data.data() + i, 4); memcpy(&fh, data.data() + i + 4, 4);
                if (!looks_like_res(fw, fh)) continue;
                if (++probes > 4000) break;
                test_dim(base + i);
                if (!cands.empty()) { char b[48];
                    snprintf(b, sizeof(b), "loose hit %gx%g", fw, fh); feed_status(b); break; }
            }
        });
    }

    for (auto& [ve, dimOff, vmOff] : cands) {
        uintptr_t found = find_qword_verified(mod, ve, base_start);
        if (found) {
            offsets::VisualEngine::Pointer    = found - base_start;
            offsets::VisualEngine::Dimensions = dimOff;
            offsets::VisualEngine::ViewMatrix = vmOff;
            feed("VisualEngine::Pointer",    offsets::VisualEngine::Pointer);
            feed("VisualEngine::Dimensions", offsets::VisualEngine::Dimensions);
            feed("VisualEngine::ViewMatrix", offsets::VisualEngine::ViewMatrix);
            return ve;
        }
    }
    if (!cands.empty()) {

        auto& [ve, dimOff, vmOff] = cands.front();
        offsets::VisualEngine::Dimensions = dimOff;
        offsets::VisualEngine::ViewMatrix = vmOff;
        feed("VisualEngine::Dimensions", offsets::VisualEngine::Dimensions);
        feed("VisualEngine::ViewMatrix", offsets::VisualEngine::ViewMatrix);
        feed_status("VE found in heap - trying FakeDataModel fallback");
        return ve;
    }
    if (dim_hits == 0)
        feed_status("no render-size match in heap (wrong window?)");
    else
        feed_status("dims matched but no valid view matrix nearby");
    return 0;
}

inline std::vector<uint8_t> build_sso_pattern(const char* s) {
    std::vector<uint8_t> pat;
    size_t n = strlen(s);
    if (n > 15) return pat;
    pat.reserve(32);
    for (size_t i = 0; i < 16; i++) pat.push_back(i < n ? (uint8_t)s[i] : 0);
    for (int i = 0; i < 8; i++) pat.push_back(i == 0 ? (uint8_t)n : 0);
    for (int i = 0; i < 8; i++) pat.push_back(i == 0 ? 0x0f : 0);
    return pat;
}

struct StrHit { uintptr_t addr; const char* expected_class; };

inline std::vector<StrHit> find_class_name_strings(size_t max_hits_per_name = 32) {
    static const char* candidates[] = {
        "DataModel",
        "Workspace",
        "Players",
        "Lighting",
        "ReplicatedStorage",
        "ScriptContext",
        "Script Context",
        "CoreGui",
        "StarterGui",
        "SoundService",
        "TeleportService",
    };
    std::vector<StrHit> out;
    for (const char* name : candidates) {
        size_t before = out.size();

        auto pat = build_sso_pattern(name);
        if (!pat.empty()) {
            each_region([&](uintptr_t base, size_t size, DWORD prot) {
                if (prot != PAGE_READWRITE) return;
                auto data = read_region(base, size);
                std::vector<size_t> hits;
                find_all(data, pat.data(), pat.size(), hits);
                for (size_t idx : hits) {
                    if (out.size() - before >= max_hits_per_name) break;
                    out.push_back({ base + idx, name });
                }
            });
        }

        {
            std::vector<uint8_t> raw(strlen(name) + 1);
            memcpy(raw.data(), name, strlen(name));
            raw.back() = 0;
            each_region([&](uintptr_t base, size_t size, DWORD prot) {
                if (prot != PAGE_READWRITE) return;
                auto data = read_region(base, size);
                std::vector<size_t> hits;
                find_all(data, raw.data(), raw.size(), hits);
                for (size_t idx : hits) {
                    if (out.size() - before >= max_hits_per_name) break;

                    uintptr_t addr = base + idx;
                    bool dup = false;
                    for (size_t k = before; k < out.size(); k++) {
                        if (out[k].addr == addr) { dup = true; break; }
                    }
                    if (!dup) out.push_back({ addr, name });
                }
            });
        }

        if (out.size() > before) {
            char b[80];
            snprintf(b, sizeof(b), "string '%s': %zu hit(s)", name, out.size() - before);
            feed_status(b);
        }
    }
    return out;
}

inline uintptr_t scan_instances() {
    auto hits = find_class_name_strings();
    if (hits.empty()) { feed_status("no class-name string found in heap"); return 0; }
    { char b[64]; snprintf(b, sizeof(b), "total string candidates: %zu", hits.size()); feed_status(b); }

    uintptr_t root_inst = 0;
    const char* root_expected = nullptr;
    uintptr_t root_name_off = 0;

    for (const auto& sh : hits) {
        if (root_inst) break;
        uint64_t addr64 = (uint64_t)sh.addr;
        each_region([&](uintptr_t base, size_t size, DWORD prot) {
            if (root_inst || prot != PAGE_READWRITE) return;
            auto data = read_region(base, size);
            std::vector<size_t> ptr_hits;
            find_all(data, &addr64, 8, ptr_hits);
            for (size_t idx : ptr_hits) {
                uintptr_t ptr_addr = base + idx;
                for (uintptr_t name_off = 0x40; name_off < 0x200; name_off += 8) {
                    uintptr_t inst = ptr_addr - name_off;
                    std::string cls = get_class(inst);
                    if (cls.empty()) continue;

                    if (cls == sh.expected_class || cls == "ScriptContext" || cls == "DataModel" ||
                        cls == "Workspace" || cls == "Players" || cls == "Lighting" ||
                        cls == "ReplicatedStorage" || cls == "CoreGui" || cls == "StarterGui" ||
                        cls == "SoundService" || cls == "TeleportService") {
                        root_inst = inst;
                        root_name_off = name_off;
                        root_expected = sh.expected_class;
                        { char b[80];
                          snprintf(b, sizeof(b), "root inst class '%s' name_off=0x%llX",
                                   cls.c_str(), (unsigned long long)name_off);
                          feed_status(b); }
                        return;
                    }
                }
            }
        });
    }
    if (!root_inst) { feed_status("no valid class descriptor found via name pointer walk"); return 0; }
    offsets::Instance::Name = root_name_off;
    feed("Instance::Name", offsets::Instance::Name);

    uintptr_t dm = 0;
    uintptr_t cur = root_inst;
    for (int depth = 0; depth < 10 && !dm; depth++) {
        std::string cls = get_class(cur);
        if (cls == "DataModel") { dm = cur; break; }
        uintptr_t next = 0;
        uintptr_t next_poff = 0;
        for (uintptr_t poff = 0x20; poff < 0x200; poff += 8) {
            uintptr_t p = rpm<uintptr_t>(cur + poff);
            if (!valid(p)) continue;
            std::string pc = get_class(p);
            if (pc == "DataModel") { offsets::Instance::Parent = poff; dm = p; break; }
            if (!pc.empty() && next == 0 && p != cur) {
                if (pc == "Workspace" || pc == "Players" || pc == "Lighting" ||
                    pc == "ReplicatedStorage" || pc == "ScriptContext" ||
                    pc == "CoreGui" || pc == "StarterGui" || pc == "SoundService" ||
                    pc == "TeleportService") {
                    next = p; next_poff = poff;
                }
            }
        }
        if (dm) break;
        if (!next) break;
        offsets::Instance::Parent = next_poff;
        cur = next;
    }
    if (!dm) { feed_status("DataModel not found via parent chain"); return 0; }
    feed("Instance::Parent", offsets::Instance::Parent);

    for (uintptr_t coff = 0x40; coff < 0x200; coff += 8) {
        uintptr_t cp = rpm<uintptr_t>(dm + coff);
        if (!valid(cp)) continue;
        uintptr_t top = rpm<uintptr_t>(cp), end = rpm<uintptr_t>(cp + 8);
        if (valid(top) && valid(end) && end > top) {
            uintptr_t count = (end - top) / 16;
            if (count > 50 && count < 250) { offsets::Instance::Children = coff; break; }
        }
    }
    if (offsets::Instance::Children) feed("Instance::Children", offsets::Instance::Children);
    return dm;
}

inline void scan_fake_datamodel(uintptr_t ve, uintptr_t dm, const std::vector<Blob>& mod) {
    uintptr_t base_start, base_end;
    mem::module_range(base_start, base_end);
    if (!ve) return;

    for (uintptr_t voff = 0x80; voff < 0x2000; voff += 8) {
        uintptr_t F = rpm<uintptr_t>(ve + voff);
        if (!valid(F)) continue;
        uintptr_t vt = rpm<uintptr_t>(F);
        if (vt < base_start || vt >= base_end) continue;
        for (uintptr_t K = 0x10; K < 0x400; K += 8) {
            if (rpm<uintptr_t>(F + K) != dm) continue;
            offsets::VisualEngine::FakeDataModel = voff;
            offsets::FakeDataModel::RealDataModel = K;
            feed("VE::FakeDataModel", offsets::VisualEngine::FakeDataModel);
            feed("FakeDataModel::RealDM", offsets::FakeDataModel::RealDataModel);
            uintptr_t loc = find_qword_verified(mod, F, base_start);
            if (loc) { offsets::FakeDataModel::Pointer = loc - base_start;
                       feed("FakeDataModel::Pointer", offsets::FakeDataModel::Pointer); }
            return;
        }
    }
}

inline void scan_fake_datamodel_by_heap(uintptr_t dm, const std::vector<Blob>& mod) {
    if (!dm) return;
    if (offsets::FakeDataModel::Pointer) return;
    uintptr_t base_start, base_end;
    mem::module_range(base_start, base_end);
    uint64_t v = (uint64_t)dm;
    feed_status("searching heap for FakeDataModel(dm)...");
    each_region([&](uintptr_t base, size_t size, DWORD prot) {
        if (offsets::FakeDataModel::Pointer) return;
        if (prot != PAGE_READWRITE) return;
        auto data = read_region(base, size);
        if (data.size() < 8) return;
        std::vector<size_t> hits;
        find_all(data, &v, 8, hits);
        for (size_t idx : hits) {
            if (offsets::FakeDataModel::Pointer) return;
            for (uintptr_t K = 0x10; K < 0x400; K += 8) {
                if (K > idx) break;
                uintptr_t F = base + idx - K;
                uintptr_t vt = rpm<uintptr_t>(F);
                if (vt < base_start || vt >= base_end) continue;

                if (rpm<uintptr_t>(F + K) != (uintptr_t)dm) continue;
                uintptr_t loc = find_qword_verified(mod, F, base_start);
                if (!loc) continue;
                offsets::FakeDataModel::Pointer = loc - base_start;
                offsets::FakeDataModel::RealDataModel = K;
                feed("FakeDataModel::Pointer(heap)", offsets::FakeDataModel::Pointer);
                feed("FakeDataModel::RealDM(heap)", offsets::FakeDataModel::RealDataModel);
                return;
            }
        }
    });
}

inline uintptr_t find_workspace_model_for(uintptr_t workspace, const std::string& pname) {

    uintptr_t model = find_child_name(workspace, pname.c_str());
    if (model && get_class(model) == "Model") return model;

    uintptr_t char_root = find_child_name(workspace, "CharacterRoot");
    if (char_root) {
        model = find_child_name(char_root, pname.c_str());
        if (model && get_class(model) == "Model") return model;

        model = find_child_name(char_root, "Player");
        if (model && get_class(model) == "Model") return model;
    }

    for (auto c : get_children(workspace)) {
        if (get_class(c) != "Model") continue;
        for (auto grand : get_children(c)) {
            if (get_class(grand) == "Humanoid") return c;
        }
    }
    return 0;
}

inline void scan_character(uintptr_t dm) {
    uintptr_t players = find_child_class(dm, "Players");
    uintptr_t workspace = find_child_class(dm, "Workspace");
    if (!players || !workspace) return;

    for (auto p : get_children(players)) {
        if (get_class(p) != "Player") continue;
        std::string pname = get_name(p);
        uintptr_t model = find_workspace_model_for(workspace, pname);
        if (!model) continue;
        for (uintptr_t off = 0x150; off < 0x350; off += 8) {
            if (rpm<uintptr_t>(p + off) == model) {
                offsets::Player::Character = off;
                feed("Player::Character", off);
                return;
            }
        }
    }
    feed_status("scan_character: no player/model match found (waiting for spawn)");
}

inline void scan_properties(uintptr_t dm) {
    uintptr_t players = find_child_class(dm, "Players");
    uintptr_t workspace = find_child_class(dm, "Workspace");

    if (players) {
        for (uintptr_t off = 0x100; off < 0x400; off += 8) {
            uintptr_t v = rpm<uintptr_t>(players + off);
            if (valid(v) && get_class(v) == "Player") { offsets::Players::LocalPlayer = off; feed("Players::LocalPlayer", off); break; }
        }
    }

    if (workspace) {
        for (uintptr_t off = 0x400; off < 0x1400; off += 8) {
            uintptr_t cam = rpm<uintptr_t>(workspace + off);
            if (valid(cam) && get_class(cam) == "Camera") {
                offsets::Workspace::CurrentCamera = off;
                feed("Workspace::CurrentCamera", off);
                break;
            }
        }
    }

    uintptr_t sample_char = 0;
    if (players)
        for (auto p : get_children(players)) {
            uintptr_t ch = rpm<uintptr_t>(p + offsets::Player::Character);
            if (valid(ch) && get_class(ch) == "Model") { sample_char = ch; break; }
        }
    if (sample_char) {
        uintptr_t hrp = find_child_name(sample_char, "HumanoidRootPart");
        if (hrp) {
            for (uintptr_t bo = 0x80; bo < 0x300; bo += 8) {
                uintptr_t prim = rpm<uintptr_t>(hrp + bo);
                if (!valid(prim)) continue;
                for (uintptr_t po = 0x80; po < 0x300; po += 4) {
                    if (is_cframe(prim + po)) {
                        offsets::BasePart::Primitive = bo;
                        offsets::Primitive::CFrame = po;
                        offsets::Primitive::Position = po + 0x24;
                        feed("BasePart::Primitive", bo);
                        feed("Primitive::CFrame", po);
                        break;
                    }
                }
                if (offsets::BasePart::Primitive) break;
            }
        }
        uintptr_t hum = find_child_class(sample_char, "Humanoid");
        if (hum) {
            float h  = rpm<float>(hum + offsets::Humanoid::Health);
            float mh = rpm<float>(hum + offsets::Humanoid::MaxHealth);
            bool ok = (h > 0.f && h < 100000.f && mh >= h && mh < 100000.f);
            if (!ok) {
                for (uintptr_t ho = 0x100; ho < 0x600 && !ok; ho += 4) {
                    float health = rpm<float>(hum + ho);
                    if (!(health > 0.f && health < 100000.f)) continue;
                    for (uintptr_t mo = ho + 4; mo <= ho + 0x40; mo += 4) {
                        float maxh = rpm<float>(hum + mo);
                        if (maxh >= health && maxh > 0.f && maxh < 100000.f) {
                            offsets::Humanoid::Health = ho;
                            offsets::Humanoid::MaxHealth = mo;
                            ok = true; break;
                        }
                    }
                }
                feed_status(ok ? "Humanoid::Health repaired" : "Humanoid::Health repair FAILED");
            }
            feed("Humanoid::Health", offsets::Humanoid::Health);
            feed("Humanoid::MaxHealth", offsets::Humanoid::MaxHealth);
        }
    }

    if (players) {
        bool team_found = false;
        for (auto p : get_children(players)) {
            if (team_found) break;
            if (get_class(p) != "Player") continue;
            for (uintptr_t off = 0x280; off < 0x380; off += 8) {
                uintptr_t t = rpm<uintptr_t>(p + off);
                if (valid(t) && get_class(t) == "Team") {
                    offsets::Player::Team = off; team_found = true;
                    feed("Player::Team", off);
                    break;
                }
            }
        }
        if (!team_found) feed_status("Player::Team not found (nobody on a team) - using default");
    }
}

inline void write_offsets_json(const std::string& roblox_version) {
    FILE* f = fopen((mem::exe_dir() + "\\offsets.json").c_str(), "w");
    if (!f) return;
    auto K = [&](const char* k, uintptr_t v) {
        fprintf(f, "      \"%s\": %llu,\n", k, (unsigned long long)v);
    };
    auto Klast = [&](const char* k, uintptr_t v) {
        fprintf(f, "      \"%s\": %llu\n", k, (unsigned long long)v);
    };
    fprintf(f, "{\n");
    fprintf(f, "  \"metadata\": {\n");
    fprintf(f, "    \"dumper\": \"UwUClient-runtime-scanner\",\n");
    fprintf(f, "    \"roblox_version\": \"%s\"\n", roblox_version.c_str());
    fprintf(f, "  },\n");

    fprintf(f, "  \"FakeDataModel\": {\n");
    K("Pointer",        offsets::FakeDataModel::Pointer);
    Klast("RealDataModel",  offsets::FakeDataModel::RealDataModel);
    fprintf(f, "  },\n");

    fprintf(f, "  \"VisualEngine\": {\n");
    K("Pointer",       offsets::VisualEngine::Pointer);
    K("Dimensions",    offsets::VisualEngine::Dimensions);
    K("ViewMatrix",    offsets::VisualEngine::ViewMatrix);
    K("RenderView",    offsets::VisualEngine::RenderView);
    Klast("FakeDataModel", offsets::VisualEngine::FakeDataModel);
    fprintf(f, "  },\n");

    fprintf(f, "  \"Instance\": {\n");
    K("ClassDescriptor", offsets::Instance::ClassDescriptor);
    K("ChildrenStart",   offsets::Instance::Children);
    K("Name",            offsets::Instance::Name);
    Klast("Parent",      offsets::Instance::Parent);
    fprintf(f, "  },\n");

    fprintf(f, "  \"Player\": {\n");
    K("Character",       offsets::Player::Character);
    K("Team",            offsets::Player::Team);
    K("UserId",          offsets::Player::UserId);
    K("AccountAge",      offsets::Player::AccountAge);
    K("MinZoomDistance", offsets::Player::MinZoomDistance);
    K("MaxZoomDistance", offsets::Player::MaxZoomDistance);
    Klast("CameraMode",  offsets::Player::CameraMode);
    fprintf(f, "  },\n");

    fprintf(f, "  \"Players\": {\n");
    Klast("LocalPlayer", offsets::Players::LocalPlayer);
    fprintf(f, "  },\n");

    fprintf(f, "  \"BasePart\": {\n");
    Klast("Primitive",   offsets::BasePart::Primitive);
    fprintf(f, "  },\n");

    fprintf(f, "  \"Primitive\": {\n");
    K("CFrame",                  offsets::Primitive::CFrame);
    K("Position",                offsets::Primitive::Position);
    K("AssemblyLinearVelocity",  offsets::Primitive::AssemblyLinearVelocity);
    K("AssemblyAngularVelocity", offsets::Primitive::AssemblyAngularVelocity);
    Klast("PrimitiveFlags",      offsets::Primitive::PrimitiveFlags);
    fprintf(f, "  },\n");

    fprintf(f, "  \"Humanoid\": {\n");
    K("Health",         offsets::Humanoid::Health);
    K("MaxHealth",      offsets::Humanoid::MaxHealth);
    K("WalkSpeed",      offsets::Humanoid::WalkSpeed);
    K("WalkSpeedCheck", offsets::Humanoid::WalkSpeedCheck);
    Klast("JumpPower",  offsets::Humanoid::JumpPower);
    fprintf(f, "  },\n");

    fprintf(f, "  \"Workspace\": {\n");
    K("CurrentCamera", offsets::Workspace::CurrentCamera);
    Klast("World",     offsets::Workspace::World);
    fprintf(f, "  },\n");

    fprintf(f, "  \"World\": {\n");
    Klast("Gravity", offsets::World::Gravity);
    fprintf(f, "  },\n");

    fprintf(f, "  \"Lighting\": {\n");
    K("ClockTime",      offsets::Lighting::ClockTime);
    K("Brightness",     offsets::Lighting::Brightness);
    K("FogEnd",         offsets::Lighting::FogEnd);
    Klast("OutdoorAmbient", offsets::Lighting::OutdoorAmbient);
    fprintf(f, "  },\n");

    fprintf(f, "  \"Camera\": {\n");
    K("CFrame",      offsets::Camera::CFrame);
    Klast("FieldOfView", offsets::Camera::FieldOfView);
    fprintf(f, "  }\n");

    fprintf(f, "}\n");
    fclose(f);
}

inline void write_scan_log(uintptr_t dm, bool ok) {
    FILE* f = fopen((mem::exe_dir() + "\\scan.log").c_str(), "w");
    if (!f) return;
    auto L = [&](const char* n, uintptr_t v) { fprintf(f, "%-26s 0x%llX\n", n, (unsigned long long)v); };
    fprintf(f, "UwUClient runtime scan  [%s]\n----------------\n", ok ? "OK" : "FAILED");
    L("DataModel (runtime)",     dm);
    L("VisualEngine::Pointer",   offsets::VisualEngine::Pointer);
    L("VisualEngine::ViewMatrix",offsets::VisualEngine::ViewMatrix);
    L("VisualEngine::Dimensions",offsets::VisualEngine::Dimensions);
    L("VE::FakeDataModel",       offsets::VisualEngine::FakeDataModel);
    L("FakeDataModel::Pointer",  offsets::FakeDataModel::Pointer);
    L("FakeDataModel::RealDM",   offsets::FakeDataModel::RealDataModel);
    {
      uintptr_t ve = rpm<uintptr_t>(mem::g_base + offsets::VisualEngine::Pointer);
      uintptr_t fake = (ve && offsets::VisualEngine::FakeDataModel)
                       ? rpm<uintptr_t>(ve + offsets::VisualEngine::FakeDataModel) : 0;
      uintptr_t chain_dm = fake ? rpm<uintptr_t>(fake + offsets::FakeDataModel::RealDataModel) : 0;
      L("chain-resolved DM",     chain_dm);
    }
    L("Instance::Name",          offsets::Instance::Name);
    L("Instance::Parent",        offsets::Instance::Parent);
    L("Instance::Children",      offsets::Instance::Children);
    L("Player::Character",       offsets::Player::Character);
    L("Players::LocalPlayer",    offsets::Players::LocalPlayer);
    L("Workspace::CurrentCamera",offsets::Workspace::CurrentCamera);
    L("Camera::CFrame",          offsets::Camera::CFrame);
    L("Camera::FieldOfView",     offsets::Camera::FieldOfView);
    L("BasePart::Primitive",     offsets::BasePart::Primitive);
    L("Primitive::CFrame",       offsets::Primitive::CFrame);
    L("Primitive::Position",     offsets::Primitive::Position);
    L("Humanoid::Health",        offsets::Humanoid::Health);
    L("Humanoid::MaxHealth",     offsets::Humanoid::MaxHealth);
    fprintf(f, "-- properties (default/scanned) --\n");
    L("Player::Team",            offsets::Player::Team);
    L("Player::UserId",          offsets::Player::UserId);
    L("Camera::CameraType",      offsets::Camera::CameraType);
    L("Humanoid::WalkSpeed",     offsets::Humanoid::WalkSpeed);
    L("Humanoid::WalkSpeedCheck",offsets::Humanoid::WalkSpeedCheck);
    L("Humanoid::JumpPower",     offsets::Humanoid::JumpPower);
    L("Humanoid::HipHeight",     offsets::Humanoid::HipHeight);
    L("Humanoid::JumpHeight",    offsets::Humanoid::JumpHeight);
    L("Humanoid::MaxSlopeAngle", offsets::Humanoid::MaxSlopeAngle);
    L("Humanoid::PlatformStand", offsets::Humanoid::PlatformStand);
    L("Humanoid::FloorMaterial", offsets::Humanoid::FloorMaterial);
    L("Workspace::World",        offsets::Workspace::World);
    L("World::Gravity",          offsets::World::Gravity);
    L("Lighting::ClockTime",     offsets::Lighting::ClockTime);
    L("Lighting::Brightness",    offsets::Lighting::Brightness);
    L("Lighting::FogEnd",        offsets::Lighting::FogEnd);
    fclose(f);
}

inline std::vector<Blob>& cached_module() {
    static std::vector<Blob> mod;
    return mod;
}

inline uintptr_t& cached_ve_addr()   { static uintptr_t v = 0; return v; }
inline uintptr_t& cached_dm_addr()   { static uintptr_t v = 0; return v; }
inline int&        scan_attempt()    { static int v = 0; return v; }

inline bool run(HWND hwnd, bool scan_props) {
    feed_clear();
    int attempt = ++scan_attempt();
    { char b[48]; snprintf(b, sizeof(b), "scan attempt #%d", attempt); feed_status(b); }
    g_phase = 1;
    auto& mod_cache = cached_module();
    if (mod_cache.empty()) {
        feed_status("Reading game module...");
        mod_cache = read_module();
    } else {
        feed_status("Using cached game module");
    }
    std::vector<Blob>& mod = mod_cache;

    uintptr_t& cached_dm = cached_dm_addr();

    bool chain_via_ve   = offsets::VisualEngine::Pointer != 0 &&
                          offsets::VisualEngine::FakeDataModel != 0 &&
                          offsets::FakeDataModel::RealDataModel != 0;
    bool chain_via_fake = offsets::FakeDataModel::Pointer != 0 &&
                          offsets::FakeDataModel::RealDataModel != 0;
    bool chain_via_rt   = runtime::dm_addr != 0 ||
                         (runtime::ve_addr != 0 && offsets::VisualEngine::FakeDataModel != 0 &&
                          offsets::FakeDataModel::RealDataModel != 0);
    if ((chain_via_ve || chain_via_fake || chain_via_rt) &&
        offsets::Instance::Children != 0 && cached_dm) {
        feed_status("Chain cached - resolving physics/properties only...");
        g_phase = 5;
        scan_character(cached_dm);
        if (scan_props) scan_properties(cached_dm);
        g_phase = TOTAL_PHASES;

        bool phys_ok = offsets::BasePart::Primitive != 0 && offsets::Primitive::Position != 0;
        if (!phys_ok) feed_status("BasePart still unresolved - character not ready yet");
        bool ok = phys_ok;
        write_scan_log(cached_dm, ok);
        if (ok) {
            std::string ver;
            std::string cpath = mem::module_path();
            auto vp = cpath.find("version-");
            if (vp != std::string::npos) {
                auto e = cpath.find_first_of("\\/", vp);
                ver = cpath.substr(vp, (e == std::string::npos ? cpath.size() : e) - vp);
            }
            offsets::roblox_version = ver;
            write_offsets_json(ver);
            feed_status("cached offsets to offsets.json");
        }
        return ok;
    }
    g_phase = 2;
    uintptr_t ve = 0;
    static int ve_last_try = -100;
    if (offsets::VisualEngine::Pointer != 0 && offsets::VisualEngine::ViewMatrix != 0) {
        feed_status("VE already resolved - skipping heap sweep");
        ve = cached_ve_addr();
    } else if (cached_ve_addr()) {
        feed_status("Using cached VE heap candidate");
        ve = cached_ve_addr();
    } else if (attempt - ve_last_try < 4 && attempt > 1) {
        feed_status("VE scan cooling down (waiting a few attempts)");
    } else {
        feed_status("Scanning heap for VisualEngine...");
        ve_last_try = attempt;
        ve = scan_visual_engine(hwnd, mod);
        if (ve) cached_ve_addr() = ve;
    }
    if (ve) runtime::ve_addr = ve;

    g_phase = 3;
    uintptr_t dm = 0;
    static int dm_last_try = -100;
    if (cached_dm && offsets::Instance::Name != 0 && offsets::Instance::Children != 0) {
        feed_status("DataModel already resolved - skipping instance tree scan");
        dm = cached_dm;
    } else if (attempt - dm_last_try < 3 && attempt > 1) {
        feed_status("DM scan cooling down");
    } else {
        feed_status("Scanning instance tree...");
        dm_last_try = attempt;
        dm = scan_instances();
        if (dm) cached_dm = dm;
    }
    if (dm) runtime::dm_addr = dm;
    if (!dm || !offsets::Instance::Children) {
        feed_status("SCAN FAILED - DataModel or Instance::Children missing");
        write_scan_log(dm, false);
        return false;
    }
    g_phase = 4;
    feed_status("Resolving DataModel chain...");
    if (!offsets::FakeDataModel::Pointer)
        scan_fake_datamodel(ve, dm, mod);
    static int fake_heap_last_try = -100;
    if (!offsets::FakeDataModel::Pointer && (attempt - fake_heap_last_try >= 5 || attempt == 1)) {
        fake_heap_last_try = attempt;
        scan_fake_datamodel_by_heap(dm, mod);
    } else if (!offsets::FakeDataModel::Pointer) {
        feed_status("FakeDM heap sweep cooling down");
    }
    if (!offsets::VisualEngine::Pointer && !offsets::FakeDataModel::Pointer &&
        !runtime::ve_addr && !runtime::dm_addr) {
        feed_status("SCAN FAILED - no module pointer AND no runtime heap fallback");
        write_scan_log(dm, false);
        return false;
    }
    if (!offsets::VisualEngine::Pointer && !offsets::FakeDataModel::Pointer) {
        feed_status("Note: no module pointer to VE/FakeDM; using runtime heap fallback (rescan needed each launch)");
    }
    scan_character(dm);
    if (scan_props) { g_phase = 5; feed_status("Resolving properties..."); scan_properties(dm); }
    g_phase = TOTAL_PHASES;
    feed_status(offsets::FakeDataModel::Pointer ? "Done - all core offsets resolved" : "Done (with warnings)");

    bool chain_ok = offsets::FakeDataModel::Pointer != 0 || offsets::VisualEngine::Pointer != 0;
    bool phys_ok  = offsets::BasePart::Primitive != 0 && offsets::Primitive::Position != 0;
    if (!phys_ok) feed_status("BasePart/Primitive unresolved - waiting for character to spawn...");
    bool ok = chain_ok && phys_ok;
    write_scan_log(dm, ok);
    if (ok) {
        std::string ver;
        std::string cpath = mem::module_path();
        auto vp = cpath.find("version-");
        if (vp != std::string::npos) {
            auto e = cpath.find_first_of("\\/", vp);
            ver = cpath.substr(vp, (e == std::string::npos ? cpath.size() : e) - vp);
        }
        offsets::roblox_version = ver;
        write_offsets_json(ver);
        feed_status("cached offsets to offsets.json");
    }
    return ok;
}

}
