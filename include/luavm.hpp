#pragma once
#include <cstdint>
#include <string>
#include <fstream>
#include <sstream>
#include "memory.hpp"
#include "log.hpp"

namespace luavm {

    inline uintptr_t luau_execute            = 0;
    inline uintptr_t luaO_nilobject          = 0;
    inline uintptr_t luaH_dummynode          = 0;
    inline uintptr_t luaD_throw              = 0;
    inline uintptr_t luaC_step               = 0;
    inline uintptr_t OpCodeLookupTable       = 0;
    inline uintptr_t luau_load               = 0;
    inline uintptr_t LuaVM_Load              = 0;
    inline uintptr_t Print                   = 0;
    inline uintptr_t ScriptContextResume     = 0;
    inline uintptr_t ResumeFacet             = 0;
    inline uintptr_t GetGlobalState          = 0;
    inline uintptr_t GetLuaStateForInstance  = 0;

    inline uintptr_t abs(uintptr_t rva) { return rva ? (mem::g_base + rva) : 0; }

    inline bool loaded() {

        return luau_execute != 0 && Print != 0 && GetGlobalState != 0;
    }

    inline int load_stream(std::istream& f, const char* origin) {
        auto put = [](const std::string& key, uintptr_t v) -> bool {
            #define M(x) if (key == #x) { x = v; return true; }
            M(luau_execute) M(luaO_nilobject) M(luaH_dummynode) M(luaD_throw)
            M(luaC_step)   M(OpCodeLookupTable) M(luau_load) M(LuaVM_Load)
            M(Print) M(ScriptContextResume) M(ResumeFacet)
            M(GetGlobalState) M(GetLuaStateForInstance)
            #undef M
            return false;
        };

        int hits = 0;
        std::string line;
        while (std::getline(f, line)) {

            auto sl = line.find("//");
            if (sl != std::string::npos) line.erase(sl);
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string lhs = line.substr(0, eq);
            std::string rhs = line.substr(eq + 1);
            auto trim = [](std::string& s) {
                while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                                       s.back() == '\r' || s.back() == ';')) s.pop_back();
                while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
            };
            trim(rhs);

            std::string name;
            for (int i = (int)lhs.size() - 1; i >= 0; i--) {
                char c = lhs[i];
                if (c == ' ' || c == '\t') { if (!name.empty()) break; continue; }
                name.insert(name.begin(), c);
            }
            if (name.empty()) continue;

            uintptr_t v = 0;
            try { v = std::stoull(rhs, nullptr, 0); } catch (...) { continue; }
            if (put(name, v)) hits++;
        }
        elog::info("luavm: %d fields resolved from %s", hits, origin);
        return hits;
    }

    inline int load(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) { elog::warn("luavm: could not open %s", path.c_str()); return 0; }
        return load_stream(f, path.c_str());
    }

    inline int load_content(const std::string& content, const char* origin = "embedded") {
        std::istringstream ss(content);
        return load_stream(ss, origin);
    }
}
