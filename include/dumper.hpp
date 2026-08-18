#pragma once

#include <string>
#include <atomic>

namespace dumper {


    std::string dump_datamodel(const std::string& base_dir);


    void dump_datamodel_async(const std::string& base_dir);


    extern std::atomic<bool>   g_dumping;
    extern std::atomic<size_t> g_progress_count;
    extern std::string         g_last_dump_dir;

}
