#pragma once
#include <cstdint>
#include <string>
#include <fstream>
#include <sstream>
#include "memory.hpp"

namespace fflags {

    inline uintptr_t GetSet_StoragePtr = 0xC0;
    inline uintptr_t GetSetOffset      = 0x30;

    namespace rva {
        inline uintptr_t TaskSchedulerTargetFps       = 0x8041C28;
        inline uintptr_t DebugPauseVoxelizer          = 0x824D268;
        inline uintptr_t DebugSkyGray                 = 0x82150A8;
        inline uintptr_t DebugAdornsDisabled          = 0x820FEF8;
        inline uintptr_t FRMMinGrassDistance          = 0x79941B0;
        inline uintptr_t FRMMaxGrassDistance          = 0x7994198;
        inline uintptr_t TextureQualityOverride       = 0x7978798;
        inline uintptr_t TextureQualityOverrideEnabled= 0x8244FC8;
    }

    inline uintptr_t _find_num(const std::string& s, size_t start, size_t end,
                               const std::string& key) {
        std::string pat = "\"" + key + "\"";
        size_t p = s.find(pat, start);
        if (p == std::string::npos || p >= end) return 0;
        p = s.find(':', p);
        if (p == std::string::npos || p >= end) return 0;
        p++;
        while (p < end && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) p++;
        size_t e = p;
        while (e < end && ((s[e] >= '0' && s[e] <= '9') ||
                           (s[e] >= 'a' && s[e] <= 'f') ||
                           (s[e] >= 'A' && s[e] <= 'F') ||
                           s[e] == 'x' || s[e] == 'X')) e++;
        if (e == p) return 0;
        try { return std::stoull(s.substr(p, e - p), nullptr, 0); }
        catch (...) { return 0; }
    }

    inline bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        std::stringstream ss; ss << f.rdbuf();
        std::string s = ss.str();

        {
            size_t p = s.find("\"FFlagList\"");
            if (p != std::string::npos) {
                size_t brace = s.find('{', p);
                if (brace != std::string::npos) {
                    size_t close = s.find('}', brace);
                    if (close != std::string::npos) {
                        if (auto v = _find_num(s, brace, close, "ToValue")) GetSet_StoragePtr = v;
                        if (auto v = _find_num(s, brace, close, "ToFlag"))  GetSetOffset      = v;
                    }
                }
            }
        }

        size_t fbegin = s.find("\"FFlags\"");
        if (fbegin == std::string::npos) return false;
        size_t brace = s.find('{', fbegin);
        if (brace == std::string::npos) return false;

        size_t depth = 1, i = brace + 1;
        while (i < s.size() && depth > 0) {
            if (s[i] == '{') depth++;
            else if (s[i] == '}') depth--;
            if (depth == 0) break;
            i++;
        }
        size_t fend = i;

        auto grab = [&](const char* name, uintptr_t& dst) {
            if (auto v = _find_num(s, brace, fend, name)) dst = v;
        };
        grab("TaskSchedulerTargetFps",        rva::TaskSchedulerTargetFps);
        grab("DebugPauseVoxelizer",           rva::DebugPauseVoxelizer);
        grab("DebugSkyGray",                  rva::DebugSkyGray);
        grab("DebugAdornsDisabled",           rva::DebugAdornsDisabled);
        grab("FRMMinGrassDistance",           rva::FRMMinGrassDistance);
        grab("FRMMaxGrassDistance",           rva::FRMMaxGrassDistance);
        grab("TextureQualityOverride",        rva::TextureQualityOverride);
        grab("TextureQualityOverrideEnabled", rva::TextureQualityOverrideEnabled);
        return true;
    }

    inline uintptr_t value_addr(uintptr_t flag_rva) {
        if (!flag_rva || !mem::g_base) return 0;
        uintptr_t storage = rpm<uintptr_t>(mem::g_base + flag_rva + GetSet_StoragePtr);
        if (storage < 0x10000 || storage > 0x7FFFFFFFFFFFULL) return 0;
        return storage + GetSetOffset;
    }

    inline int  read_int(uintptr_t rva)         { uintptr_t a = value_addr(rva); return a ? rpm<int>(a) : 0; }
    inline void write_int(uintptr_t rva, int v) { uintptr_t a = value_addr(rva); if (a) wpm<int>(a, v); }

    inline bool read_bool(uintptr_t rva) { uintptr_t a = value_addr(rva); return a && rpm<uint8_t>(a) != 0; }
    inline void write_bool(uintptr_t rva, bool v) {
        uintptr_t a = value_addr(rva);
        if (!a) return;
        uint8_t cur = rpm<uint8_t>(a);
        if (cur <= 1) wpm<uint8_t>(a, v ? 1 : 0);
    }
    inline void write_int_guarded(uintptr_t rva, int v, int lo, int hi) {
        int cur = read_int(rva);
        if (cur >= lo && cur <= hi && cur != v) write_int(rva, v);
    }
}
