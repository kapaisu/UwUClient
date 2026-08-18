#pragma once

#include <string>
#include <atomic>

namespace dumper {

    // Walks the entire DataModel and writes a snapshot into a new timestamped
    // subdirectory under `base_dir`. Returns the full dump-dir path on
    // success, empty string on failure.
    std::string dump_datamodel(const std::string& base_dir);

    // Spawn dump_datamodel on a detached thread so the UI/render thread does
    // not stall. Progress is exposed via the atomics below.
    void dump_datamodel_async(const std::string& base_dir);

    // Shared status the UI can poll.
    extern std::atomic<bool>   g_dumping;
    extern std::atomic<size_t> g_progress_count;
    extern std::string         g_last_dump_dir;   // updated only when !g_dumping

}
