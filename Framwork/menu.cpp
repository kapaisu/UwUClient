#define _CRT_SECURE_NO_WARNINGS
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <vector>
#include <string>
#include <map>          
#include <stdio.h>
#include <cmath>        

namespace Style {
    const float topbar_height = 24.f;
    const float tab_height = 25.f;
    const float tab_padding = 10.f;
    const float outline_thickness = 1.f;
    const float text_outline_offset = 1.f;

    const float tab_anim_speed = 2.0f;
    const float collapse_anim_speed = 10.0f;

    const ImU32 col_outline = IM_COL32(10, 10, 10, 255);
    const ImU32 col_bg = IM_COL32(15, 12, 17, 255);
    const ImU32 col_text = IM_COL32(230, 230, 230, 255);
    const ImU32 col_widget_bg = IM_COL32(40, 35, 45, 255);


    const ImU32 col_tab_active_top = IM_COL32(95, 25, 125, 255);
    const ImU32 col_tab_active_bottom = IM_COL32(50, 10, 70, 255);

    const ImU32 col_anim_hover = IM_COL32(130, 50, 180, 255);
    const ImU32 col_pulse_bright = IM_COL32(210, 100, 255, 255);

    const ImU32 col_tab_inactive_top = IM_COL32(25, 22, 30, 255);
    const ImU32 col_tab_inactive_bottom = IM_COL32(15, 12, 18, 255);

    const ImU32 col_topbar_top = IM_COL32(75, 20, 100, 255);
    const ImU32 col_topbar_bottom = IM_COL32(35, 10, 50, 255);


    const ImU32 col_popup_bg = IM_COL32(25, 22, 28, 255);
}


static const char* active_picker_label = nullptr;
static float* active_picker_col = nullptr;

void AddTriangleFilledMultiColor(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec2 c, ImU32 col_a, ImU32 col_b, ImU32 col_c) {
    if (!dl) return;
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve(3, 3);
    dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx));
    dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 1));
    dl->PrimWriteIdx((ImDrawIdx)(dl->_VtxCurrentIdx + 2));
    dl->PrimWriteVtx(a, uv, col_a);
    dl->PrimWriteVtx(b, uv, col_b);
    dl->PrimWriteVtx(c, uv, col_c);
}

ImU32 ColorLerp(ImU32 col_a, ImU32 col_b, float t) {
    int r1 = (col_a >> 0) & 0xFF; int g1 = (col_a >> 8) & 0xFF; int b1 = (col_a >> 16) & 0xFF; int a1 = (col_a >> 24) & 0xFF;
    int r2 = (col_b >> 0) & 0xFF; int g2 = (col_b >> 8) & 0xFF; int b2 = (col_b >> 16) & 0xFF; int a2 = (col_b >> 24) & 0xFF;
    int r = r1 + (int)((r2 - r1) * t);
    int g = g1 + (int)((g2 - g1) * t);
    int b = b1 + (int)((b2 - b1) * t);
    int a = a1 + (int)((a2 - a1) * t);
    return IM_COL32(r, g, b, a);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

ImVec2 RotateVertex(ImVec2 p, ImVec2 c, float s, float c_cos) {
    return ImVec2(c.x + (p.x - c.x) * c_cos - (p.y - c.y) * s, c.y + (p.x - c.x) * s + (p.y - c.y) * c_cos);
}

void DrawShadedArrow(ImDrawList* dl, ImVec2 center, float size, float angle_rad, bool hovered) {
    ImVec2 p1 = ImVec2(-size, -size);
    ImVec2 p2 = ImVec2(-size, size);
    ImVec2 p3 = ImVec2(size, 0);

    float s_sin = sinf(angle_rad);
    float c_cos = cosf(angle_rad);

    ImVec2 r1 = RotateVertex(p1, ImVec2(0, 0), s_sin, c_cos);
    ImVec2 r2 = RotateVertex(p2, ImVec2(0, 0), s_sin, c_cos);
    ImVec2 r3 = RotateVertex(p3, ImVec2(0, 0), s_sin, c_cos);

    ImU32 col_shadow = IM_COL32(0, 0, 0, 150);
    dl->AddTriangleFilled(ImVec2(center.x + r1.x + 1, center.y + r1.y + 1), ImVec2(center.x + r2.x + 1, center.y + r2.y + 1), ImVec2(center.x + r3.x + 1, center.y + r3.y + 1), col_shadow);

    ImU32 col_tip = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(240, 240, 240, 255);
    ImU32 col_base = hovered ? IM_COL32(220, 220, 240, 255) : IM_COL32(160, 160, 160, 255);

    AddTriangleFilledMultiColor(dl, ImVec2(center.x + r3.x, center.y + r3.y), ImVec2(center.x + r1.x, center.y + r1.y), ImVec2(center.x + r2.x, center.y + r2.y), col_tip, col_base, col_base);
    dl->AddTriangle(ImVec2(center.x + r1.x, center.y + r1.y), ImVec2(center.x + r2.x, center.y + r2.y), ImVec2(center.x + r3.x, center.y + r3.y), IM_COL32(50, 50, 60, 255), 1.0f);
}

void AddTextWithOutline(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text) {
    ImU32 outline_col = Style::col_outline;
    float o = Style::text_outline_offset;
    dl->AddText(ImVec2(pos.x - o, pos.y), outline_col, text);
    dl->AddText(ImVec2(pos.x + o, pos.y), outline_col, text);
    dl->AddText(ImVec2(pos.x, pos.y - o), outline_col, text);
    dl->AddText(ImVec2(pos.x, pos.y + o), outline_col, text);
    dl->AddText(ImVec2(pos.x - o, pos.y - o), outline_col, text);
    dl->AddText(ImVec2(pos.x + o, pos.y - o), outline_col, text);
    dl->AddText(ImVec2(pos.x - o, pos.y + o), outline_col, text);
    dl->AddText(ImVec2(pos.x + o, pos.y + o), outline_col, text);
    dl->AddText(pos, col, text);
}

std::map<ImGuiID, float> anim_state;
std::map<ImGuiID, float> slider_visuals;

struct ChildState {
    bool is_collapsed = false;
    float current_height = -1.0f;
};
std::map<ImGuiID, ChildState> child_states;


namespace BackgroundFX {
    struct Particle { ImVec2 Pos; ImVec2 Vel; float Size; float Alpha; };
    std::vector<Particle> particles;
    bool initialized = false;
    const int PARTICLE_COUNT = 100;
    const float CONNECTION_DISTANCE = 150.0f;

    const ImU32 COL_BG_TOP = IM_COL32(25, 10, 35, 255);
    const ImU32 COL_BG_BOT = IM_COL32(15, 5, 20, 255);
    const ImU32 COL_PARTICLE = IM_COL32(160, 60, 220, 200);
    const ImU32 COL_LINE = IM_COL32(130, 40, 190, 255);

    float GetRandomFloat(float min, float max) {
        return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (max - min)));
    }

    void UpdateAndDraw() {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImVec2 screen_size = ImGui::GetIO().DisplaySize;
        dl->AddRectFilledMultiColor(ImVec2(0, 0), screen_size, COL_BG_TOP, COL_BG_TOP, COL_BG_BOT, COL_BG_BOT);

        if (!initialized) {
            particles.clear();
            for (int i = 0; i < PARTICLE_COUNT; i++) {
                particles.push_back({
                    ImVec2(GetRandomFloat(0, screen_size.x), GetRandomFloat(0, screen_size.y)),
                    ImVec2(GetRandomFloat(-0.5f, 0.5f), GetRandomFloat(-0.5f, 0.5f)),
                    GetRandomFloat(1.5f, 3.0f), GetRandomFloat(0.1f, 1.0f) });
            }
            initialized = true;
        }

        for (int i = 0; i < particles.size(); i++) {
            Particle& p = particles[i];
            p.Pos.x += p.Vel.x; p.Pos.y += p.Vel.y;
            if (p.Pos.x < 0) p.Pos.x = screen_size.x; if (p.Pos.x > screen_size.x) p.Pos.x = 0;
            if (p.Pos.y < 0) p.Pos.y = screen_size.y; if (p.Pos.y > screen_size.y) p.Pos.y = 0;
            ImU32 p_col = (COL_PARTICLE & 0x00FFFFFF) | ((int)(p.Alpha * 255.f) << 24);
            dl->AddCircleFilled(p.Pos, p.Size, p_col);
            for (int j = i + 1; j < particles.size(); j++) {
                Particle& p2 = particles[j];
                float dx = p.Pos.x - p2.Pos.x; float dy = p.Pos.y - p2.Pos.y;
                float dist_sq = dx * dx + dy * dy;
                if (dist_sq < CONNECTION_DISTANCE * CONNECTION_DISTANCE) {
                    float dist = sqrtf(dist_sq);
                    float alpha = 1.0f - (dist / CONNECTION_DISTANCE);
                    ImU32 line_col = (COL_LINE & 0x00FFFFFF) | ((int)(alpha * 150.f) << 24);
                    dl->AddLine(p.Pos, p2.Pos, line_col, 1.0f);
                }
            }
        }
    }
}



bool CustomCheckbox(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float size = 14.0f;
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total_bb(pos, ImVec2(pos.x + size + 8 + text_size.x, pos.y + size));
    const ImRect check_bb(pos, ImVec2(pos.x + size, pos.y + size));

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) *v = !(*v);

    float t = anim_state[id];
    float target = *v ? 1.0f : 0.0f;
    float anim_speed = 5.0f * ImGui::GetIO().DeltaTime;

    if (t < target) { t += anim_speed; if (t > target) t = target; }
    else if (t > target) { t -= anim_speed; if (t < target) t = target; }
    anim_state[id] = t;

    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(check_bb.Min, check_bb.Max, Style::col_widget_bg);
    dl->AddRect(check_bb.Min, check_bb.Max, Style::col_outline);

    if (t > 0.01f) {
        float ease_t = 1.0f - powf(1.0f - t, 3.0f);
        float pad = 7.0f * (1.0f - ease_t) + 2.0f;
        ImU32 fill_col_top = Style::col_tab_active_top;
        ImU32 fill_col_bot = Style::col_tab_active_bottom;

        if (*v && t > 0.9f) {
            float time = (float)ImGui::GetTime();
            float pulse = (sinf(time * 3.5f) + 1.0f) * 0.5f;
            fill_col_top = ColorLerp(Style::col_tab_active_top, Style::col_pulse_bright, pulse * 0.20f);
            fill_col_bot = ColorLerp(Style::col_tab_active_bottom, Style::col_pulse_bright, pulse * 0.10f);
        }
        else {
            fill_col_top = ColorLerp(Style::col_widget_bg, fill_col_top, t);
            fill_col_bot = ColorLerp(Style::col_widget_bg, fill_col_bot, t);
        }

        dl->AddRectFilledMultiColor(ImVec2(check_bb.Min.x + pad, check_bb.Min.y + pad), ImVec2(check_bb.Max.x - pad, check_bb.Max.y - pad), fill_col_top, fill_col_top, fill_col_bot, fill_col_bot);
    }

    ImU32 text_col = (hovered || *v) ? IM_COL32(255, 255, 255, 255) : Style::col_text;
    ImVec2 text_pos = ImVec2(check_bb.Max.x + 8, check_bb.Min.y - 1);
    AddTextWithOutline(dl, text_pos, text_col, label);
    return pressed;
}

bool CustomSlider(const char* label, float* v, float v_min, float v_max, const char* format = "%.1f") {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float w = ImGui::GetContentRegionAvail().x - 4.0f;
    const float h = 16.0f;

    ImVec2 label_pos = window->DC.CursorPos;
    ImGui::ItemSize(ImVec2(w, ImGui::CalcTextSize(label).y + 4));
    ImDrawList* dl = window->DrawList;
    AddTextWithOutline(dl, label_pos, Style::col_text, label);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect frame_bb(pos, ImVec2(pos.x + w, pos.y + h));

    ImGui::ItemSize(frame_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(frame_bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(frame_bb, id, &hovered, &held);

    if (held || pressed) {
        float mouse_pos_x = g.IO.MousePos.x;
        float ratio = (mouse_pos_x - frame_bb.Min.x) / (frame_bb.Max.x - frame_bb.Min.x);
        if (ratio < 0.0f) ratio = 0.0f; if (ratio > 1.0f) ratio = 1.0f;
        *v = v_min + (v_max - v_min) * ratio;
    }

    float t_hover = anim_state[id];
    float target_hover = (hovered || held) ? 1.0f : 0.0f;
    float anim_speed_hover = 10.0f * ImGui::GetIO().DeltaTime;
    if (t_hover < target_hover) t_hover += anim_speed_hover; else t_hover -= anim_speed_hover;
    if (t_hover < 0.0f) t_hover = 0.0f; if (t_hover > 1.0f) t_hover = 1.0f;
    anim_state[id] = t_hover;

    if (slider_visuals.find(id) == slider_visuals.end()) slider_visuals[id] = *v;
    float& visual_val = slider_visuals[id];
    float glide_speed = 7.0f * ImGui::GetIO().DeltaTime;
    visual_val = Lerp(visual_val, *v, glide_speed);

    dl->AddRectFilled(frame_bb.Min, frame_bb.Max, Style::col_widget_bg);
    float ratio_vis = (visual_val - v_min) / (v_max - v_min);
    if (ratio_vis < 0.0f) ratio_vis = 0.0f; if (ratio_vis > 1.0f) ratio_vis = 1.0f;
    ImVec2 fill_max = ImVec2(frame_bb.Min.x + (frame_bb.Max.x - frame_bb.Min.x) * ratio_vis, frame_bb.Max.y);

    if (ratio_vis > 0.001f) {
        ImU32 col_top = Style::col_tab_active_top;
        ImU32 col_bot = Style::col_tab_active_bottom;
        if (t_hover > 0.0f) {
            col_top = ColorLerp(col_top, Style::col_anim_hover, t_hover * 0.5f);
            col_bot = ColorLerp(col_bot, Style::col_anim_hover, t_hover * 0.2f);
        }
        dl->AddRectFilledMultiColor(frame_bb.Min, fill_max, col_top, col_top, col_bot, col_bot);
        ImU32 glow_col = IM_COL32(255, 255, 255, 150);
        dl->AddRectFilled(ImVec2(fill_max.x - 1, frame_bb.Min.y), fill_max, glow_col);
    }
    dl->AddRect(frame_bb.Min, frame_bb.Max, Style::col_outline);

    char value_buf[64]; sprintf(value_buf, format, *v);
    ImVec2 val_size = ImGui::CalcTextSize(value_buf);
    ImVec2 val_pos = ImVec2(frame_bb.Min.x + (frame_bb.GetWidth() - val_size.x) * 0.5f, frame_bb.Min.y + (frame_bb.GetHeight() - val_size.y) * 0.5f);
    AddTextWithOutline(dl, val_pos, IM_COL32(255, 255, 255, 255), value_buf);
    ImGui::Dummy(ImVec2(0, 4));
    return pressed || held;
}


bool CustomCombo(const char* label, int* current_item, const std::vector<const char*>& items) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float w = ImGui::GetContentRegionAvail().x - 4.0f;
    const float h = 20.0f;

    ImVec2 label_pos = window->DC.CursorPos;
    ImGui::ItemSize(ImVec2(w, ImGui::CalcTextSize(label).y + 4));
    ImDrawList* dl = window->DrawList;
    AddTextWithOutline(dl, label_pos, Style::col_text, label);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect frame_bb(pos, ImVec2(pos.x + w, pos.y + h));

    ImGui::ItemSize(frame_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(frame_bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(frame_bb, id, &hovered, &held);
    bool popup_open = ImGui::IsPopupOpen(id, ImGuiPopupFlags_None);

    if (pressed) {
        ImGui::OpenPopup(id, ImGuiPopupFlags_None);
        popup_open = true;
    }

    dl->AddRectFilled(frame_bb.Min, frame_bb.Max, Style::col_widget_bg);
    if (hovered || popup_open) dl->AddRectFilled(frame_bb.Min, frame_bb.Max, IM_COL32(255, 255, 255, 10));
    dl->AddRect(frame_bb.Min, frame_bb.Max, Style::col_outline);

    const char* preview = (items.size() > 0 && *current_item >= 0 && *current_item < items.size()) ? items[*current_item] : "";
    ImVec2 text_size = ImGui::CalcTextSize(preview);
    ImVec2 text_pos = ImVec2(frame_bb.Min.x + 5, frame_bb.Min.y + (h - text_size.y) * 0.5f);
    AddTextWithOutline(dl, text_pos, IM_COL32(255, 255, 255, 255), preview);

    float t_open = anim_state[id];
    float target_open = popup_open ? 1.0f : 0.0f;
    float anim_speed = 10.0f * ImGui::GetIO().DeltaTime;
    if (t_open < target_open) t_open += anim_speed; else t_open -= anim_speed;
    if (t_open < 0.0f) t_open = 0.0f; if (t_open > 1.0f) t_open = 1.0f;
    anim_state[id] = t_open;

    ImVec2 arrow_center = ImVec2(frame_bb.Max.x - 12, frame_bb.Min.y + h * 0.5f);
    float arrow_angle = 3.14159f * 0.5f + (t_open * 3.14159f);
    DrawShadedArrow(dl, arrow_center, 3.0f, arrow_angle, hovered || popup_open);

    bool value_changed = false;
    if (popup_open) {
        ImGui::SetNextWindowPos(ImVec2(frame_bb.Min.x, frame_bb.Max.y + 2));
        ImGui::SetNextWindowSize(ImVec2(frame_bb.GetWidth(), -1));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Style::col_popup_bg);
        ImGui::PushStyleColor(ImGuiCol_Border, Style::col_outline);

        if (ImGui::BeginPopupEx(id, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {
            for (int i = 0; i < items.size(); i++) {
                bool is_selected = (i == *current_item);

                ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 15));

                if (is_selected) {
                    ImVec2 cur = ImGui::GetCursorScreenPos();
                    float dot_y = cur.y + ImGui::GetTextLineHeight() * 0.5f;
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cur.x + 3, dot_y), 2.5f, Style::col_pulse_bright);
                }

                ImGui::Indent(10.0f);
                if (ImGui::Selectable(items[i], is_selected)) {
                    *current_item = i;
                    value_changed = true;
                }
                ImGui::Unindent(10.0f);

                if (is_selected) ImGui::SetItemDefaultFocus();
                ImGui::PopStyleColor(2);
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    ImGui::Dummy(ImVec2(0, 4));
    return value_changed;
}


bool CustomMultiCombo(const char* label, std::vector<bool>& selected, const std::vector<const char*>& items) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);

    const float w = ImGui::GetContentRegionAvail().x - 4.0f;
    const float h = 20.0f;

    ImVec2 label_pos = window->DC.CursorPos;
    ImGui::ItemSize(ImVec2(w, ImGui::CalcTextSize(label).y + 4));
    ImDrawList* dl = window->DrawList;
    AddTextWithOutline(dl, label_pos, Style::col_text, label);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect frame_bb(pos, ImVec2(pos.x + w, pos.y + h));

    ImGui::ItemSize(frame_bb, Style::tab_padding);
    if (!ImGui::ItemAdd(frame_bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(frame_bb, id, &hovered, &held);
    bool popup_open = ImGui::IsPopupOpen(id, ImGuiPopupFlags_None);

    if (pressed) {
        ImGui::OpenPopup(id, ImGuiPopupFlags_None);
        popup_open = true;
    }

    dl->AddRectFilled(frame_bb.Min, frame_bb.Max, Style::col_widget_bg);
    if (hovered || popup_open) dl->AddRectFilled(frame_bb.Min, frame_bb.Max, IM_COL32(255, 255, 255, 10));
    dl->AddRect(frame_bb.Min, frame_bb.Max, Style::col_outline);

    std::string preview = "";
    int active_count = 0;
    for (size_t i = 0; i < items.size(); i++) {
        if (i < selected.size() && selected[i]) {
            if (active_count > 0) preview += ", ";
            preview += items[i];
            active_count++;
        }
    }
    if (active_count == 0) preview = "None";
    if (active_count == items.size() && items.size() > 1) preview = "All";

    ImVec2 text_size = ImGui::CalcTextSize(preview.c_str());
    dl->PushClipRect(frame_bb.Min, ImVec2(frame_bb.Max.x - 20, frame_bb.Max.y));
    ImVec2 text_pos = ImVec2(frame_bb.Min.x + 5, frame_bb.Min.y + (h - text_size.y) * 0.5f);
    AddTextWithOutline(dl, text_pos, IM_COL32(255, 255, 255, 255), preview.c_str());
    dl->PopClipRect();

    float t_open = anim_state[id];
    float target_open = popup_open ? 1.0f : 0.0f;
    float anim_speed = 10.0f * ImGui::GetIO().DeltaTime;
    if (t_open < target_open) t_open += anim_speed; else t_open -= anim_speed;
    if (t_open < 0.0f) t_open = 0.0f; if (t_open > 1.0f) t_open = 1.0f;
    anim_state[id] = t_open;

    ImVec2 arrow_center = ImVec2(frame_bb.Max.x - 12, frame_bb.Min.y + h * 0.5f);
    float arrow_angle = 3.14159f * 0.5f + (t_open * 3.14159f);
    DrawShadedArrow(dl, arrow_center, 3.0f, arrow_angle, hovered || popup_open);

    bool value_changed = false;
    if (popup_open) {
        ImGui::SetNextWindowPos(ImVec2(frame_bb.Min.x, frame_bb.Max.y + 2));
        ImGui::SetNextWindowSize(ImVec2(frame_bb.GetWidth(), -1));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Style::col_popup_bg);
        ImGui::PushStyleColor(ImGuiCol_Border, Style::col_outline);

        if (ImGui::BeginPopupEx(id, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {
            for (size_t i = 0; i < items.size(); i++) {
                if (i >= selected.size()) break;

                bool is_selected = selected[i];
                ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 15));

                if (is_selected) {
                    ImVec2 cur = ImGui::GetCursorScreenPos();
                    float dot_y = cur.y + ImGui::GetTextLineHeight() * 0.5f;
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cur.x + 3, dot_y), 2.5f, Style::col_pulse_bright);
                }

                ImGui::Indent(10.0f);
                if (ImGui::Selectable(items[i], is_selected, ImGuiSelectableFlags_DontClosePopups)) {
                    selected[i] = !selected[i];
                    value_changed = true;
                }
                ImGui::Unindent(10.0f);

                ImGui::PopStyleColor(2);
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    ImGui::Dummy(ImVec2(0, 4));
    return value_changed;
}


bool CustomColorPicker(const char* label, float col[4]) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float w = ImGui::GetContentRegionAvail().x - 4.0f;
    const float h = 20.0f;

    ImDrawList* dl = window->DrawList;

    // Layout Setup
    float box_w = 25.0f;
    float box_h_inner = 10.0f;
    float gap = 10.0f;

    const ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = ImGui::CalcTextSize(label);

    // Box Positionierung
    float box_start_x = pos.x + label_size.x + gap;
    float box_y_offset = (h - box_h_inner) * 0.5f;

    const ImRect box_bb(
        ImVec2(box_start_x, pos.y + box_y_offset),
        ImVec2(box_start_x + box_w, pos.y + box_y_offset + box_h_inner)
    );

    const ImRect total_bb(pos, ImVec2(pos.x + w, pos.y + h));

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) return false;


    AddTextWithOutline(dl, ImVec2(pos.x, pos.y + (h - ImGui::GetFontSize()) * 0.5f), Style::col_text, label);

    bool is_active = (active_picker_label != nullptr && strcmp(active_picker_label, label) == 0);

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(box_bb, id, &hovered, &held);

    if (pressed) {
        if (is_active) {
            active_picker_label = nullptr;
            active_picker_col = nullptr;
        }
        else {
            active_picker_label = label;
            active_picker_col = col;
        }
    }


    float t_hover = anim_state[id];
   
    float target_hover = (is_active || hovered || held) ? 1.0f : 0.0f;
    float anim_speed = 15.0f * ImGui::GetIO().DeltaTime;

    if (t_hover < target_hover) t_hover += anim_speed;
    else t_hover -= anim_speed;


    if (t_hover < 0.0f) t_hover = 0.0f;
    if (t_hover > 1.0f) t_hover = 1.0f;

    anim_state[id] = t_hover;


    dl->AddRectFilled(box_bb.Min, box_bb.Max, IM_COL32(80, 80, 80, 255));

  
    ImU32 converted_col = ImGui::ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], col[3]));
    dl->AddRectFilled(box_bb.Min, box_bb.Max, converted_col);

    if (is_active) {
       
        dl->AddRect(box_bb.Min, box_bb.Max, Style::col_pulse_bright, 0.0f, 0, 1.5f);
    }
    else if (t_hover > 0.01f) {
     
        ImU32 base_col = Style::col_pulse_bright;
        int alpha = (int)(t_hover * 255.0f);
        ImU32 glow_col = (base_col & 0x00FFFFFF) | (alpha << 24);
        dl->AddRect(box_bb.Min, box_bb.Max, glow_col, 0.0f, 0, 1.5f);
    }
    else {
       
        dl->AddRect(box_bb.Min, box_bb.Max, Style::col_outline);
    }

    return false;
}


void BeginCustomChild(const char* label, ImVec2 size) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiID id = window->GetID(label);

    if (child_states.find(id) == child_states.end()) {
        child_states[id].current_height = size.y;
        child_states[id].is_collapsed = false;
    }
    ChildState& state = child_states[id];

    const float header_h = 20.0f;
    float target_height = state.is_collapsed ? header_h : size.y;
    float dt = ImGui::GetIO().DeltaTime;
    float lerp_speed = Style::collapse_anim_speed;
    state.current_height = Lerp(state.current_height, target_height, dt * lerp_speed);
    if (fabs(state.current_height - target_height) < 0.5f) state.current_height = target_height;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild(label, ImVec2(size.x, state.current_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 s = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilledMultiColor(
        p, ImVec2(p.x + s.x, p.y + header_h),
        Style::col_tab_active_top, Style::col_tab_active_top,
        Style::col_tab_active_bottom, Style::col_tab_active_bottom
    );
    dl->AddRect(p, ImVec2(p.x + s.x, p.y + s.y), Style::col_outline);

    ImVec2 text_size = ImGui::CalcTextSize(label);
    ImVec2 text_pos = ImVec2(p.x + (s.x - text_size.x) * 0.5f, p.y + (header_h - text_size.y) * 0.5f);
    AddTextWithOutline(dl, text_pos, IM_COL32(255, 255, 255, 255), label);

    
    float open_factor = (state.current_height - header_h) / (size.y - header_h);
    if (open_factor < 0.0f) open_factor = 0.0f; if (open_factor > 1.0f) open_factor = 1.0f;

    ImVec2 tri_center = ImVec2(p.x + s.x - 15.0f, p.y + header_h * 0.5f);

   
    float angle = open_factor * (3.14159f * 0.5f);
    bool hovered = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + s.x, p.y + header_h));

    DrawShadedArrow(dl, tri_center, 4.0f, angle, hovered);

 
    ImGui::SetCursorPos(ImVec2(0, 0));
    if (ImGui::InvisibleButton("##header_btn", ImVec2(size.x, header_h))) {
        state.is_collapsed = !state.is_collapsed;
    }

    ImGui::SetCursorPosY(header_h + 8.0f);
    ImGui::Indent(8.0f);
    ImGui::PushItemWidth(s.x - 16.0f);
}

void EndCustomChild() {
    ImGui::PopItemWidth();
    ImGui::Unindent(8.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}


static int current_tab = 0;
static int prev_tab = -1;
static float fade_alpha = 1.0f;

void RenderMenu() {
    BackgroundFX::UpdateAndDraw();

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Once);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Style::col_bg);

    ImGui::Begin("CheatMenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 win_size = ImGui::GetWindowSize();

   
    ImVec2 topbar_min = win_pos;
    ImVec2 topbar_max = ImVec2(win_pos.x + win_size.x, win_pos.y + Style::topbar_height);
    dl->AddRectFilledMultiColor(topbar_min, topbar_max, Style::col_topbar_top, Style::col_topbar_top, Style::col_topbar_bottom, Style::col_topbar_bottom);
    dl->AddRect(topbar_min, topbar_max, Style::col_outline);
    AddTextWithOutline(dl, ImVec2(win_pos.x + 10, win_pos.y + 4), IM_COL32(255, 255, 255, 255), "LUNO Menu");

    // Tabs
    const int tab_count = 5;
    const char* tab_names[] = { "Aimbot", "Player ESP", "Entity ESP", "Miscellaneous", "Settings" };
    float tab_y_min = win_pos.y + Style::topbar_height;
    float tab_y_max = tab_y_min + Style::tab_height;
    float tab_width = win_size.x / (float)tab_count;

    if (current_tab != prev_tab) {
        prev_tab = current_tab;
        fade_alpha = 0.f; 
    }

    if (fade_alpha < 1.0f) {
        fade_alpha += ImGui::GetIO().DeltaTime * Style::tab_anim_speed;
        if (fade_alpha > 1.0f) fade_alpha = 1.0f;
    }

    for (int i = 0; i < tab_count; i++) {
        float tx0 = win_pos.x + (float)i * tab_width;
        float tx1 = tx0 + tab_width;
        ImVec2 tab_min(tx0, tab_y_min); ImVec2 tab_max(tx1, tab_y_max);
        bool is_active = (current_tab == i);

        if (is_active) dl->AddRectFilledMultiColor(tab_min, tab_max, Style::col_tab_active_top, Style::col_tab_active_top, Style::col_tab_active_bottom, Style::col_tab_active_bottom);
        else dl->AddRectFilledMultiColor(tab_min, tab_max, Style::col_tab_inactive_top, Style::col_tab_inactive_top, Style::col_tab_inactive_bottom, Style::col_tab_inactive_bottom);

        const char* label = tab_names[i];
        ImVec2 label_size = ImGui::CalcTextSize(label);
        float label_x = tx0 + Style::tab_padding;
        float label_y = tab_y_min + (Style::tab_height - label_size.y) * 0.5f;
        ImU32 text_col = is_active ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 160, 255);
        AddTextWithOutline(dl, ImVec2(label_x, label_y), text_col, label);
    }

    dl->AddLine(ImVec2(win_pos.x, tab_y_min), ImVec2(win_pos.x + win_size.x, tab_y_min), Style::col_outline);
    dl->AddLine(ImVec2(win_pos.x, tab_y_max), ImVec2(win_pos.x + win_size.x, tab_y_max), Style::col_outline);
    for (int i = 1; i < tab_count; i++) dl->AddLine(ImVec2(win_pos.x + (float)i * tab_width, tab_y_min), ImVec2(win_pos.x + (float)i * tab_width, tab_y_max), Style::col_outline);

    ImGui::SetCursorPos(ImVec2(0, Style::topbar_height));
    for (int i = 0; i < tab_count; i++) {
        char tab_id[16]; sprintf(tab_id, "##tab%d", i);
        if (ImGui::InvisibleButton(tab_id, ImVec2(tab_width, Style::tab_height))) current_tab = i;
        if (i < tab_count - 1) ImGui::SameLine();
    }


    float base_content_y = Style::topbar_height + Style::tab_height + 10;
    float ease_progress = 1.0f - powf(1.0f - fade_alpha, 4.0f);
    float anim_offset_y = 30.0f * (1.0f - ease_progress);

    ImGui::SetCursorPos(ImVec2(10, base_content_y + anim_offset_y));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade_alpha);

    ImGui::BeginGroup();

    if (current_tab == 0) {
        float total_w = 600.0f - 20.0f;
        float total_h = 400.0f - (Style::topbar_height + Style::tab_height + 20.0f);
        float col_w = (total_w - 10.0f) * 0.5f;
        float half_h = (total_h - 10.0f) * 0.5f;

        ImGui::BeginGroup();
        BeginCustomChild("Aimbot", ImVec2(col_w, half_h));
        {
            static bool b_enable = false; static bool b_silent = false; static bool b_pred = true; static bool b_fov = true; static bool b_smooth = true;
            static int hitscan_mode = 0;
            static std::vector<bool> bones_selected = { false, true, false, true };
            static float fov_color[4] = { 1.f, 0.f, 1.f, 1.f };

            ImGui::Spacing();
            CustomCheckbox("Enable", &b_enable);
            CustomCheckbox("Silent Aim", &b_silent);
            CustomCheckbox("Prediction", &b_pred);

            std::vector<const char*> hitscan_items = { "Nearest", "Head", "Chest", "Pelvis" };
            CustomCombo("Target Priority", &hitscan_mode, hitscan_items);

            std::vector<const char*> bone_names = { "Head", "Neck", "Chest", "Pelvis" };
            CustomMultiCombo("Hitscan Bones", bones_selected, bone_names);

            CustomColorPicker("FOV Color", fov_color);
        }
        EndCustomChild();
        ImGui::Dummy(ImVec2(0, 10.0f));
        BeginCustomChild("Triggerbot", ImVec2(col_w, half_h));
        {
            static bool b_trig = false; static bool b_flash = false; static float f_delay = 0.0f;
            static int trig_mode = 0;
            ImGui::Spacing();
            CustomCheckbox("Enable Trigger", &b_trig);
            CustomCheckbox("Check Flash", &b_flash);
            CustomSlider("Reaction Delay", &f_delay, 0.f, 200.f, "%.0f ms");

            std::vector<const char*> trig_items = { "Hold", "Toggle", "Always On" };
            CustomCombo("Trigger Mode", &trig_mode, trig_items);
        }
        EndCustomChild();
        ImGui::EndGroup();

        ImGui::SameLine(0, 10.0f);

        ImGui::BeginGroup();
        BeginCustomChild("Aimbot configurations", ImVec2(col_w, half_h));
        {
            static float fov = 4.5f; static float smooth = 5.0f;
            ImGui::Spacing();
            CustomSlider("FOV Radius", &fov, 0.f, 20.f);
            CustomSlider("Smoothness", &smooth, 1.f, 20.f);
        }
        EndCustomChild();
        ImGui::Dummy(ImVec2(0, 10.0f));
        BeginCustomChild("Extra Aimbot configurations", ImVec2(col_w, half_h));
        {
            static float dist = 300.f; static bool b_team = false;
            ImGui::Spacing();
            CustomSlider("Max Distance", &dist, 0.f, 1000.f, "%.0f m");
            CustomCheckbox("Ignore Team", &b_team);
        }
        EndCustomChild();
        ImGui::EndGroup();
    }
    else if (current_tab == 1) {
        static bool b_box = true;
        static int box_type = 0;
        static float esp_col[4] = { 0.f, 1.f, 0.f, 1.f };
        static std::vector<bool> esp_flags = { true, false, true };
        std::vector<const char*> flag_names = { "Name", "Health", "Weapon" };

        CustomCheckbox("ESP Box", &b_box);
        std::vector<const char*> box_items = { "2D Box", "2D Corner", "3D Box" };
        CustomCombo("Box Type", &box_type, box_items);

        CustomMultiCombo("ESP Flags", esp_flags, flag_names);
        CustomColorPicker("ESP Color", esp_col);
    }

    ImGui::EndGroup();
    ImGui::PopStyleVar();

    dl->AddRect(win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y), Style::col_outline);
    ImGui::End();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(2);

  
    if (active_picker_label && active_picker_col) {
        ImGui::SetNextWindowSize(ImVec2(250, 275), ImGuiCond_Once);

    
        if (ImGui::IsMouseReleased(0)) ImGui::SetNextWindowFocus();

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Style::col_bg);
        ImGui::PushStyleColor(ImGuiCol_Border, Style::col_outline);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

      
        if (ImGui::Begin(active_picker_label, nullptr, ImGuiWindowFlags_NoDecoration)) {

            ImDrawList* dl_picker = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            ImVec2 s = ImGui::GetWindowSize();
            const float header_h = 24.0f;

         
            ImU32 glow_col = IM_COL32(140, 60, 220, 50);
            dl_picker->AddRectFilled(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + s.x + 2, p.y + s.y + 2), glow_col, 5.0f);

         
            dl_picker->AddRectFilled(ImVec2(p.x, p.y + header_h), ImVec2(p.x + s.x, p.y + s.y), Style::col_bg);

            dl_picker->AddRectFilledMultiColor(
                p, ImVec2(p.x + s.x, p.y + header_h),
                Style::col_topbar_top, Style::col_topbar_top,
                Style::col_topbar_bottom, Style::col_topbar_bottom
            );

      
            dl_picker->AddRect(p, ImVec2(p.x + s.x, p.y + s.y), Style::col_outline);
            dl_picker->AddLine(ImVec2(p.x, p.y + header_h), ImVec2(p.x + s.x, p.y + header_h), Style::col_outline);

      
            AddTextWithOutline(dl_picker, ImVec2(p.x + 10, p.y + 4), IM_COL32(255, 255, 255, 255), active_picker_label);

       
            ImGui::SetCursorPos(ImVec2(10, header_h + 10));
            ImGui::PushItemWidth(s.x - 20);

            ImGui::ColorPicker4("##picker_content", active_picker_col,
                ImGuiColorEditFlags_NoSidePreview |
                ImGuiColorEditFlags_NoSmallPreview |
                ImGuiColorEditFlags_AlphaBar |
                ImGuiColorEditFlags_DisplayRGB |
                ImGuiColorEditFlags_DisplayHex
            );

            ImGui::PopItemWidth();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }
}