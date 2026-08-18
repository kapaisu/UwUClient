#include "../include/dumper.hpp"
#include "../include/roblox.hpp"
#include "../include/log.hpp"

#include <Windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <unordered_set>

namespace dumper {

    std::atomic<bool>   g_dumping{false};
    std::atomic<size_t> g_progress_count{0};
    std::string         g_last_dump_dir;


    static constexpr size_t MAX_NODES = 500000;
    static constexpr int    MAX_DEPTH = 40;

    static std::string timestamp() {
        SYSTEMTIME t;
        GetLocalTime(&t);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d",
                      t.wYear, t.wMonth, t.wDay,
                      t.wHour, t.wMinute, t.wSecond);
        return buf;
    }

    static void escape_json(FILE* f, const std::string& s) {
        fputc('"', f);
        for (char c : s) {
            if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
            else if ((unsigned char)c < 0x20) fprintf(f, "\\u%04x", (int)(unsigned char)c);
            else fputc(c, f);
        }
        fputc('"', f);
    }

    std::string dump_datamodel(const std::string& base_dir) {
        RbxDataModel dm = RbxDataModel::get();
        if (!dm.valid()) {
            elog::warn("dumper: DataModel not resolved");
            return "";
        }

        std::string out_dir = base_dir + "\\dm_" + timestamp();
        std::error_code ec;
        std::filesystem::create_directories(out_dir, ec);

        std::string tree_path = out_dir + "\\tree.txt";
        std::string json_path = out_dir + "\\flat.json";
        std::string svc_path  = out_dir + "\\services.txt";
        std::string sum_path  = out_dir + "\\summary.txt";

        FILE* tf = fopen(tree_path.c_str(), "wb");
        FILE* jf = fopen(json_path.c_str(), "wb");
        FILE* sf = fopen(svc_path.c_str(), "wb");
        FILE* mf = fopen(sum_path.c_str(),  "wb");
        if (!tf || !jf || !sf || !mf) {
            elog::warn("dumper: could not open output files in %s", out_dir.c_str());
            if (tf) fclose(tf);
            if (jf) fclose(jf);
            if (sf) fclose(sf);
            if (mf) fclose(mf);
            return "";
        }

        fprintf(jf, "[\n");
        fprintf(sf, "# Top-level DataModel children (services)\n");
        fprintf(tf, "# DataModel dump  root=0x%llX  %s\n",
                (unsigned long long)dm.ptr, timestamp().c_str());

        struct Frame { RbxInstance node; int depth; uintptr_t parent; };
        std::vector<Frame> stack;
        stack.reserve(4096);
        stack.push_back({dm.as_instance(), 0, 0});

        std::unordered_set<uintptr_t> seen;
        bool   first_json = true;
        size_t count = 0;
        bool   truncated = false;

        while (!stack.empty()) {
            Frame f = stack.back();
            stack.pop_back();

            if (!f.node.valid()) continue;
            if (!seen.insert(f.node.ptr).second) continue;
            if (f.depth > MAX_DEPTH) continue;
            if (++count > MAX_NODES) { truncated = true; break; }
            g_progress_count.store(count);

            std::string cls  = f.node.get_class();
            std::string name = f.node.get_name();


            for (int i = 0; i < f.depth; i++) fputs("  ", tf);
            fprintf(tf, "[%s] \"%s\" @ 0x%llX  parent=0x%llX\n",
                    cls.c_str(), name.c_str(),
                    (unsigned long long)f.node.ptr,
                    (unsigned long long)f.parent);


            if (f.depth == 1) {
                fprintf(sf, "[%s] %s @ 0x%llX\n",
                        cls.c_str(), name.c_str(),
                        (unsigned long long)f.node.ptr);
            }


            if (!first_json) fputs(",\n", jf);
            first_json = false;
            fputs("  {", jf);
            fprintf(jf, "\"addr\":\"0x%llX\",", (unsigned long long)f.node.ptr);
            fprintf(jf, "\"parent\":\"0x%llX\",", (unsigned long long)f.parent);
            fprintf(jf, "\"depth\":%d,", f.depth);
            fputs("\"class\":", jf); escape_json(jf, cls); fputs(",", jf);
            fputs("\"name\":",  jf); escape_json(jf, name);
            fputs("}", jf);


            auto kids = f.node.get_children();
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                stack.push_back({*it, f.depth + 1, f.node.ptr});
            }
        }

        fprintf(jf, "\n]\n");

        fprintf(mf, "DataModel dump\n");
        fprintf(mf, "  timestamp:  %s\n", timestamp().c_str());
        fprintf(mf, "  dm addr:    0x%llX\n", (unsigned long long)dm.ptr);
        fprintf(mf, "  instances:  %zu%s\n", count, truncated ? "  (truncated)" : "");
        fprintf(mf, "  max depth:  %d\n", MAX_DEPTH);
        fprintf(mf, "  files:      tree.txt, flat.json, services.txt\n");

        fclose(tf);
        fclose(jf);
        fclose(sf);
        fclose(mf);

        elog::ok("dumper: wrote %zu instances%s to %s",
                 count, truncated ? " (truncated)" : "",
                 out_dir.c_str());
        return out_dir;
    }

    void dump_datamodel_async(const std::string& base_dir) {
        if (g_dumping.exchange(true)) return;
        g_progress_count.store(0);
        std::thread([base_dir]() {
            std::string dir = dump_datamodel(base_dir);
            g_last_dump_dir = dir;
            g_dumping.store(false);
        }).detach();
    }

}
