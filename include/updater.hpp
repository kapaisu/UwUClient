#pragma once
#include <Windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "version.hpp"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

namespace updater {

    inline std::atomic<bool> g_checked{false};
    inline std::atomic<bool> g_running{false};
    inline std::atomic<bool> g_update_available{false};
    inline std::atomic<bool> g_dismissed{false};
    inline std::atomic<bool> g_fetch_failed{false};

    inline std::mutex g_mtx;
    inline std::string g_latest;

    inline constexpr const wchar_t* HOST = L"raw.githubusercontent.com";
    inline constexpr const wchar_t* PATH = L"/kapaisu/UwUClient/refs/heads/main/v.txt";
    inline constexpr const wchar_t* UA   = L"UwUClient-Updater/1.0";
    inline constexpr const char* RELEASES_URL = "https://github.com/kapaisu/UwUClient/releases/";

    inline std::string trim(std::string s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                              s.back() == ' '  || s.back() == '\t')) s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        return s.substr(i);
    }

    inline int http_get(const wchar_t* host, const wchar_t* path, std::string& out) {
        out.clear();
        HINTERNET hSes = WinHttpOpen(UA, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSes) return 0;
        WinHttpSetTimeouts(hSes, 8000, 8000, 12000, 12000);

        HINTERNET hCon = WinHttpConnect(hSes, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hCon) { WinHttpCloseHandle(hSes); return 0; }

        HINTERNET hReq = WinHttpOpenRequest(hCon, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hReq) { WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSes); return 0; }

        DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirect_policy, sizeof(redirect_policy));

        int status = 0;
        if (WinHttpSendRequest(hReq, L"Accept: text/plain\r\n", (DWORD)-1L,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD status_dw = 0, sz = sizeof(status_dw);
            WinHttpQueryHeaders(hReq,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                &status_dw, &sz, WINHTTP_NO_HEADER_INDEX);
            status = (int)status_dw;

            DWORD avail = 0;
            do {
                avail = 0;
                if (!WinHttpQueryDataAvailable(hReq, &avail)) break;
                if (!avail) break;
                std::string chunk(avail, 0);
                DWORD read = 0;
                if (!WinHttpReadData(hReq, chunk.data(), avail, &read)) break;
                out.append(chunk.data(), read);
                if (out.size() > 8192) break;
            } while (avail > 0);
        }

        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hCon);
        WinHttpCloseHandle(hSes);
        return status;
    }

    inline void check_async() {
        if (g_running.exchange(true)) return;
        std::thread([]() {
            std::string body;
            int st = http_get(HOST, PATH, body);
            body = trim(body);
            bool avail = false;
            if (st == 200 && !body.empty()) {
                avail = body != std::string(uwu::CURRENT_VERSION);
                {
                    std::lock_guard<std::mutex> lk(g_mtx);
                    g_latest = body;
                }
                g_fetch_failed.store(false);
            } else {
                g_fetch_failed.store(true);
            }
            g_update_available.store(avail);
            g_checked.store(true);
            g_running.store(false);
        }).detach();
    }

    inline void snapshot(bool& checked, bool& available, bool& dismissed,
                         std::string& latest, std::string& current) {
        checked   = g_checked.load();
        available = g_update_available.load();
        dismissed = g_dismissed.load();
        current   = uwu::CURRENT_VERSION;
        std::lock_guard<std::mutex> lk(g_mtx);
        latest = g_latest;
    }

    inline void dismiss() { g_dismissed.store(true); }

    inline void open_releases_page() {
        ShellExecuteA(nullptr, "open", RELEASES_URL, nullptr, nullptr, SW_SHOWNORMAL);
        g_dismissed.store(true);
    }
}
