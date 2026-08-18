#pragma once

#include <atomic>
#include <chrono>
#include <thread>

#include "memory.hpp"
#include "offsets.hpp"
#include "roblox.hpp"

namespace check {

inline std::atomic<bool> typing{false};

inline uintptr_t g_cached_cibc = 0;
inline uintptr_t g_cached_dm   = 0;

inline void run() {

    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));

        RbxDataModel dm = RbxDataModel::get();
        if (!dm.valid()) { typing.store(false); continue; }

        if (dm.ptr != g_cached_dm) {
            g_cached_dm  = dm.ptr;
            g_cached_cibc = 0;
        }

        static int spin = 0;
        if (!g_cached_cibc || (spin++ % 10) == 0) {
            RbxInstance tcs = dm.get_service("TextChatService");
            if (tcs.valid()) {
                RbxInstance cibc = tcs.find_child_by_class("ChatInputBarConfiguration");

                if (!cibc.valid())
                    cibc = tcs.find_child_by_name("ChatInputBarConfiguration");
                if (cibc.valid()) g_cached_cibc = cibc.ptr;
            }
        }

        if (!g_cached_cibc) { typing.store(false); continue; }

        uint8_t v = rpm<uint8_t>(g_cached_cibc + offsets::ChatInputBarConfiguration::IsOpen);
        typing.store(v == 1);
    }
}

inline bool is_typing() { return typing.load(); }

}
