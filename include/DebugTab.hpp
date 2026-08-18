#pragma once
#include "roblox.hpp"
#include <vector>
#include <string>
#include <chrono>

class DebugTab {
public:
    void Render(const std::vector<PlayerInfo>& players);
private:
    uintptr_t selected_entity_address = 0;
    char search_filter[256] = "";
    bool show_deactivated = true;
    std::string status_message = "";
    std::chrono::time_point<std::chrono::steady_clock> status_clear_time;

    static float CalculateDistance(const Vec3& a, const Vec3& b);
    static const PlayerInfo* GetLocalPlayer(const std::vector<PlayerInfo>& players);
};
