#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <ctime>
#include <Windows.h>

namespace elog {

enum Level : int { L_INFO = 0, L_OK = 1, L_WARN = 2, L_ERR = 3, L_DBG = 4 };

struct Entry {
    std::string text;
    double      t;
    int         level;
};

inline std::vector<Entry>& buffer() { static std::vector<Entry> v; return v; }
inline std::mutex&         mutex()  { static std::mutex m; return m; }

inline double _now_s() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline FILE*& _logfile() { static FILE* f = nullptr; return f; }
inline void _open_logfile_once() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    char exe[512]{};
    GetModuleFileNameA(nullptr, exe, sizeof(exe));
    std::string path = exe;
    auto slash = path.find_last_of("\\/");
    if (slash != std::string::npos) path = path.substr(0, slash);
    path += "\\ext.log";
    _logfile() = fopen(path.c_str(), "w");
    if (_logfile()) fprintf(_logfile(), "--- ext.log start ---\n");
}
inline const char* _lvl_tag(int lvl) {
    switch (lvl) {
        case L_OK:   return "OK  ";
        case L_WARN: return "WARN";
        case L_ERR:  return "ERR ";
        case L_DBG:  return "DBG ";
        default:     return "INFO";
    }
}
inline void push(int lvl, const std::string& s) {
    std::lock_guard<std::mutex> l(mutex());
    buffer().push_back({s, _now_s(), lvl});
    if (buffer().size() > 800) buffer().erase(buffer().begin(), buffer().begin() + 200);
    _open_logfile_once();
    if (_logfile()) {
        std::time_t t = std::time(nullptr);
        std::tm lt{};
        localtime_s(&lt, &t);
        fprintf(_logfile(), "[%02d:%02d:%02d %s] %s\n",
                lt.tm_hour, lt.tm_min, lt.tm_sec, _lvl_tag(lvl), s.c_str());
        fflush(_logfile());
    }
}

inline void _pushf(int lvl, const char* fmt, va_list ap) {
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    push(lvl, buf);
}

inline void info(const char* fmt, ...) { va_list ap; va_start(ap, fmt); _pushf(L_INFO, fmt, ap); va_end(ap); }
inline void ok  (const char* fmt, ...) { va_list ap; va_start(ap, fmt); _pushf(L_OK,   fmt, ap); va_end(ap); }
inline void warn(const char* fmt, ...) { va_list ap; va_start(ap, fmt); _pushf(L_WARN, fmt, ap); va_end(ap); }
inline void err (const char* fmt, ...) { va_list ap; va_start(ap, fmt); _pushf(L_ERR,  fmt, ap); va_end(ap); }
inline void dbg (const char* fmt, ...) { va_list ap; va_start(ap, fmt); _pushf(L_DBG,  fmt, ap); va_end(ap); }

inline std::vector<Entry> snapshot() {
    std::lock_guard<std::mutex> l(mutex());
    return buffer();
}

inline void clear() {
    std::lock_guard<std::mutex> l(mutex());
    buffer().clear();
}

}
