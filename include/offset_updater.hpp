#pragma once
#include <Windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdio>

#include "log.hpp"

#pragma comment(lib, "shell32.lib")

#pragma comment(lib, "winhttp.lib")

namespace offset_updater {

    inline constexpr const wchar_t* HOST      = L"offsets.imtheo.lol";
    inline constexpr INTERNET_PORT  PORT      = INTERNET_DEFAULT_HTTPS_PORT;
    inline constexpr const wchar_t* USER_AGENT = L"UwUClient-offsets/1.0";

    inline std::wstring widen(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    inline int http_get(const std::wstring& path, std::string& out) {
        out.clear();
        HINTERNET hSes = WinHttpOpen(USER_AGENT,
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSes) return 0;

        WinHttpSetTimeouts(hSes, 10000, 10000, 15000, 15000);

        HINTERNET hCon = WinHttpConnect(hSes, HOST, PORT, 0);
        if (!hCon) { WinHttpCloseHandle(hSes); return 0; }

        HINTERNET hReq = WinHttpOpenRequest(hCon, L"GET", path.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
        if (!hReq) { WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSes); return 0; }

        BOOL ok = WinHttpSendRequest(hReq,
                                     L"Accept-Encoding: identity\r\n", (DWORD)-1L,
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                  && WinHttpReceiveResponse(hReq, nullptr);

        int status = 0;
        if (ok) {
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
            } while (avail > 0);
        }

        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hCon);
        WinHttpCloseHandle(hSes);
        return status;
    }

    inline std::string latest_version_from_index(const std::string& json) {

        const std::string key = "\"version\"";
        size_t p = json.find(key);
        if (p == std::string::npos) return {};
        p = json.find(':', p);
        if (p == std::string::npos) return {};
        p = json.find('"', p);
        if (p == std::string::npos) return {};
        p++;
        size_t e = json.find('"', p);
        if (e == std::string::npos) return {};
        return json.substr(p, e - p);
    }

    inline std::string cache_dir() {
        char buf[MAX_PATH]{};
        if (FAILED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf))) {

            char tmp[MAX_PATH]{};
            GetTempPathA(MAX_PATH, tmp);
            std::string p = std::string(tmp) + "UwUClient";
            CreateDirectoryA(p.c_str(), nullptr);
            return p;
        }
        std::string p = std::string(buf) + "\\ext";
        CreateDirectoryA(p.c_str(), nullptr);
        return p;
    }

    inline std::string sidecar_path(const std::string& dir) {
        return dir + "\\offsets.version";
    }

    inline std::string cached_version(const std::string& dir) {
        std::ifstream f(sidecar_path(dir), std::ios::binary);
        if (!f.is_open()) return {};
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        return s;
    }

    inline bool write_atomic(const std::string& path, const std::string& body) {
        std::string tmp = path + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return false;
            out.write(body.data(), (std::streamsize)body.size());
            if (!out) return false;
        }

        if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileA(tmp.c_str());
            return false;
        }
        return true;
    }

    inline bool fetch_and_write(const std::string& dir, const std::string& version) {
        std::wstring path = L"/" + widen(version) + L"/offsets.txt";
        std::string body;
        int st = http_get(path, body);
        if (st != 200 || body.size() < 128) {
            elog::warn("offset_updater: GET %s%ls -> %d, %zu bytes",
                       "offsets.imtheo.lol", path.c_str(), st, body.size());
            return false;
        }

        std::string dst = dir + "\\offsets.txt";
        if (!write_atomic(dst, body)) {
            elog::warn("offset_updater: failed to write %s", dst.c_str());
            return false;
        }

        write_atomic(sidecar_path(dir), version);
        elog::ok("offset_updater: wrote offsets.txt (%zu bytes) for %s",
                 body.size(), version.c_str());

        std::wstring jpath = L"/" + widen(version) + L"/offsets.json";
        std::string jbody;
        if (http_get(jpath, jbody) == 200 && jbody.size() > 64) {
            write_atomic(dir + "\\offsets.json", jbody);
        }

        std::wstring fpath = L"/" + widen(version) + L"/fflags.json";
        std::string fbody;
        if (http_get(fpath, fbody) == 200 && fbody.size() > 1024) {
            write_atomic(dir + "\\fflags.json", fbody);
            elog::ok("offset_updater: wrote fflags.json (%zu bytes)", fbody.size());
        }
        return true;
    }

    inline std::string try_update(const std::string& dir, const std::string& client_ver) {

        if (!client_ver.empty()) {
            std::string cached = cached_version(dir);
            bool have_offs  = GetFileAttributesA((dir + "\\offsets.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
            bool have_flags = GetFileAttributesA((dir + "\\fflags.json").c_str()) != INVALID_FILE_ATTRIBUTES;
            if (cached == client_ver && have_offs && have_flags) {
                elog::info("offset_updater: cache hit for %s", client_ver.c_str());
                return client_ver;
            }
        }

        if (!client_ver.empty()) {
            elog::info("offset_updater: fetching %s", client_ver.c_str());
            if (fetch_and_write(dir, client_ver)) return client_ver;
            elog::warn("offset_updater: exact-version fetch failed; falling back to latest");
        }

        std::string index;
        if (http_get(L"/versions", index) != 200 || index.empty()) {
            elog::warn("offset_updater: /versions unreachable");
            return {};
        }
        std::string latest = latest_version_from_index(index);
        if (latest.empty()) {
            elog::warn("offset_updater: /versions parse failed");
            return {};
        }
        if (!client_ver.empty() && latest != client_ver) {
            elog::warn("offset_updater: client=%s but latest=%s (mismatch — chain may fail)",
                       client_ver.c_str(), latest.c_str());
        }
        if (fetch_and_write(dir, latest)) return latest;
        return {};
    }
}
