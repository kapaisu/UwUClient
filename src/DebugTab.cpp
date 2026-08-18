#include "../include/DebugTab.hpp"
#include "../include/teleport.hpp"
#include "../include/esp.hpp"
#include "../include/fflags.hpp"
#include <imgui.h>
#include <Windows.h>
#include <format>
#include <cmath>
#include <algorithm>
#include <cctype>

float DebugTab::CalculateDistance(const Vec3& a, const Vec3& b) {
    return std::sqrt(
        (a.x - b.x) * (a.x - b.x) +
        (a.y - b.y) * (a.y - b.y) +
        (a.z - b.z) * (a.z - b.z)
    );
}

const PlayerInfo* DebugTab::GetLocalPlayer(const std::vector<PlayerInfo>& players) {
    RbxDataModel dm = RbxDataModel::get();
    if (dm.valid()) {
        RbxInstance players_svc = dm.get_service("Players");
        if (players_svc.valid()) {
            uintptr_t local_player_ptr = rpm<uintptr_t>(players_svc.ptr + offsets::Players::LocalPlayer);
            for (const auto& p : players) {
                if (p.player_ptr == local_player_ptr) {
                    return &p;
                }
            }
        }
    }
    return nullptr;
}

void DebugTab::Render(const std::vector<PlayerInfo>& players) {
    if (!status_message.empty() && std::chrono::steady_clock::now() > status_clear_time) {
        status_message.clear();
    }

    {
        uintptr_t va = fflags::value_addr(fflags::rva::TaskSchedulerTargetFps);
        int fps = va ? rpm<int>(va) : 0;
        ImGui::TextDisabled("FastFlags:");
        ImGui::SameLine();
        if (va)
            ImGui::Text("TaskSchedulerTargetFps = %d  (slot 0x%llX)", fps, (unsigned long long)va);
        else
            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "TargetFps slot unresolved (layout mismatch)");
        ImGui::Separator();
    }

    static float left_pane_width = 250.0f;
    ImGui::Columns(2, "DebugTabColumns", true);
    if (ImGui::GetColumnOffset(1) == 0.0f) ImGui::SetColumnWidth(0, left_pane_width);

    ImGui::BeginChild("##EntityListPanel", ImVec2(0, 0), true);

    ImGui::InputTextWithHint("##Filter", "Filter by name...", search_filter, IM_ARRAYSIZE(search_filter));
    ImGui::Checkbox("Show deactivated", &show_deactivated);
    ImGui::Separator();

    std::string filter_str = search_filter;
    std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

    if (players.empty()) {
        ImGui::TextDisabled("No entities found\nVerify DataModel scan is running.");
    } else {
        ImGui::BeginChild("##ListScrollable");
        for (const auto& ent : players) {
            bool is_deactivated = (ent.health <= 0.0f);
            if (!show_deactivated && is_deactivated) continue;

            if (!filter_str.empty()) {
                std::string lower_name = ent.name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                if (lower_name.find(filter_str) == std::string::npos) continue;
            }

            ImGui::PushID(static_cast<int>(ent.player_ptr));

            if (is_deactivated) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            }

            ImVec4 role_color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            if (ent.role == 1) role_color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
            else if (ent.role == 2) role_color = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);

            ImGui::ColorButton("##RoleDot", role_color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(10, 10));
            ImGui::SameLine();

            std::string row_label = std::format("{} [HP: {:.0f}]", ent.name, ent.health);

            bool is_selected = (selected_entity_address == ent.player_ptr);
            if (ImGui::Selectable(row_label.c_str(), is_selected)) {
                selected_entity_address = ent.player_ptr;
            }

            if (is_deactivated) {
                ImGui::PopStyleVar();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    ImGui::BeginChild("##InspectorPanel", ImVec2(0, 0), true);

    const PlayerInfo* selected = nullptr;
    for (const auto& ent : players) {
        if (ent.player_ptr == selected_entity_address) {
            selected = &ent;
            break;
        }
    }

    if (!selected) {
        ImGui::TextDisabled("Select an entity to inspect.");
        ImGui::EndChild();
        ImGui::Columns(1);
        return;
    }

    bool is_deactivated = (selected->health <= 0.0f);
    const PlayerInfo* local_player = GetLocalPlayer(players);
    bool is_local = (local_player && local_player->player_ptr == selected->player_ptr);

    ImGui::TextUnformatted("Identity");
    ImGui::Separator();

    ImGui::Text("%s", selected->name.c_str());
    ImGui::Text("Player: 0x%llX", selected->player_ptr);
    ImGui::Text("Char:   0x%llX", selected->cached_char_ptr);
    ImGui::Text("HRP:    0x%llX", selected->hrp_ptr);
    ImGui::Spacing();

    ImGui::TextUnformatted("State");
    ImGui::Separator();

    float health_frac = (selected->max_health > 0.0f) ? (selected->health / selected->max_health) : 0.0f;
    std::string hp_overlay = std::format("{:.1f} / {:.1f}", selected->health, selected->max_health);
    ImGui::ProgressBar(health_frac, ImVec2(-FLT_MIN, 0), hp_overlay.c_str());

    ImGui::Text("Status: %s", is_deactivated ? "Deactivated" : (is_local ? "Alive (Local)" : "Alive"));
    ImGui::Spacing();

    ImGui::TextUnformatted("Transform (Debug Coordinates)");
    ImGui::Separator();

    ImGui::Text("X: %10.2f  Y: %10.2f  Z: %10.2f", selected->position.x, selected->position.y, selected->position.z);

    if (local_player && !is_local) {
        float dist = CalculateDistance(local_player->position, selected->position);
        ImGui::Text("Distance from local: %.2f studs", dist);
    }
    ImGui::Spacing();

    ImGui::TextUnformatted("Debug Navigation");
    ImGui::Separator();

    float pos_arr[3] = { selected->position.x, selected->position.y, selected->position.z };
    ImGui::InputFloat3("Coordinates", pos_arr, "%.2f", ImGuiInputTextFlags_ReadOnly);

    bool btn_disabled = false;
    std::string tooltip = "";

    if (is_deactivated) {
        btn_disabled = true;
        tooltip = "Entity is deactivated";
    } else if (is_local) {
        btn_disabled = true;
        tooltip = "Cannot jump to self — use for cross-referencing only";
    } else if (!local_player || local_player->hrp_ptr == 0) {
        btn_disabled = true;
        tooltip = "Local player HRP not cached";
    }

    (void)pos_arr;

    if (btn_disabled) ImGui::BeginDisabled();

    if (ImGui::Button("Teleport To Player", ImVec2(-FLT_MIN, 30))) {
        Vec3 dest = { selected->position.x, selected->position.y + 3.0f, selected->position.z };
        tp::request_teleport(dest, 60);
        status_message = std::format("Teleporting to {}", selected->name);
        esp::notify(std::format("Teleported to {}", selected->name));
        status_clear_time = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    }

    uintptr_t target_prim = selected->hrp_ptr
        ? rpm<uintptr_t>(selected->hrp_ptr + offsets::BasePart::Primitive) : 0;

    if (!tp::g_lock.load()) {
        if (ImGui::Button("Lock To Player", ImVec2(-FLT_MIN, 30))) {
            if (target_prim) {
                tp::start_lock(target_prim);
                status_message = std::format("Locked to {}", selected->name);
                esp::notify(std::format("Locked to {}", selected->name));
            } else {
                status_message = "Failed — target HRP not resolved";
            }
            status_clear_time = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        }
    }

    if (!tp::g_spectate.load()) {
        if (ImGui::Button("View Player", ImVec2(-FLT_MIN, 30))) {
            if (selected->humanoid_ptr && tp::start_spectate(selected->humanoid_ptr)) {
                status_message = std::format("Viewing {}", selected->name);
                esp::notify(std::format("Viewing {}", selected->name));
            } else {
                status_message = "Failed — could not resolve camera subject";
            }
            status_clear_time = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        }
    }

    if (btn_disabled) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }

    if (tp::g_lock.load()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Unlock", ImVec2(-FLT_MIN, 30))) {
            tp::stop_lock();
            status_message = "Unlocked";
            status_clear_time = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        }
        ImGui::PopStyleColor();
    }
    if (tp::g_spectate.load()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Stop Viewing", ImVec2(-FLT_MIN, 30))) {
            tp::stop_spectate();
            status_message = "Stopped viewing";
            status_clear_time = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        }
        ImGui::PopStyleColor();
    }

    if (!status_message.empty()) {
        if (status_message.starts_with("Failed")) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", status_message.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", status_message.c_str());
        }
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "⚠ Intended for local/private server development use only");

    ImGui::EndChild();
    ImGui::Columns(1);
}
