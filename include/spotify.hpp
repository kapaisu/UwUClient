#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <Windows.h>

namespace spotify {

    inline std::mutex        g_mtx;
    inline std::string       g_title;
    inline std::string       g_artist;
    inline std::atomic<bool> g_playing{false};
    inline std::atomic<bool> g_running{false};
    inline std::chrono::steady_clock::time_point g_song_start{};

    inline std::string wstr_to_utf8(const std::wstring& w) {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                    nullptr, 0, nullptr, nullptr);
        std::string out(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                            out.data(), n, nullptr, nullptr);
        return out;
    }

    inline HWND find_spotify_window() {
        struct S { HWND hwnd; DWORD pid; };
        S s{nullptr, 0};
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            S* s = (S*)lp;
            if (!IsWindowVisible(h)) return TRUE;
            wchar_t title[512]{};
            if (!GetWindowTextW(h, title, 512) || !title[0]) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (!pid) return TRUE;
            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!proc) return TRUE;
            wchar_t path[MAX_PATH]{};
            DWORD sz = MAX_PATH;
            QueryFullProcessImageNameW(proc, 0, path, &sz);
            CloseHandle(proc);
            std::wstring p(path);
            for (auto& c : p) c = (wchar_t)towlower(c);
            if (p.find(L"\\spotify.exe") != std::wstring::npos) {
                s->hwnd = h; s->pid = pid;
                return FALSE;
            }
            return TRUE;
        }, (LPARAM)&s);
        return s.hwnd;
    }

    inline void poll_thread() {
        std::string last_title, last_artist;
        while (g_running.load()) {
            HWND hwnd = find_spotify_window();
            std::string title, artist;
            bool playing = false;
            if (hwnd) {
                wchar_t buf[512]{};
                GetWindowTextW(hwnd, buf, 512);
                std::string t = wstr_to_utf8(buf);
                size_t dash = t.find(" - ");
                if (dash == std::string::npos) dash = t.find(" \xe2\x80\x93 ");
                if (dash != std::string::npos) {
                    artist  = t.substr(0, dash);
                    title   = t.substr(dash + 3);
                    playing = true;
                } else if (t != "Spotify" && t != "Spotify Free" && t != "Spotify Premium" && !t.empty()) {
                    title = t;
                    playing = true;
                }
            }
            {
                std::lock_guard<std::mutex> lock(g_mtx);
                if (title != last_title || artist != last_artist) {
                    g_song_start = std::chrono::steady_clock::now();
                    last_title = title;
                    last_artist = artist;
                }
                g_title  = title;
                g_artist = artist;
            }
            g_playing.store(playing);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    inline void start() {
        if (g_running.exchange(true)) return;
        std::thread(poll_thread).detach();
    }

    inline void snapshot(std::string& title, std::string& artist, int& elapsed_sec, bool& playing) {
        std::lock_guard<std::mutex> lock(g_mtx);
        title = g_title; artist = g_artist;
        playing = g_playing.load();
        if (playing) {
            elapsed_sec = (int)std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - g_song_start).count();
        } else {
            elapsed_sec = 0;
        }
    }

    inline void send_media_key(WORD vk) {
        INPUT in[2]{};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = vk;
        in[1].type = INPUT_KEYBOARD;
        in[1].ki.wVk = vk;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
    }

    inline void toggle_play()  { send_media_key(VK_MEDIA_PLAY_PAUSE); }
    inline void skip_next()    { send_media_key(VK_MEDIA_NEXT_TRACK); }
    inline void skip_prev()    { send_media_key(VK_MEDIA_PREV_TRACK); }
}
