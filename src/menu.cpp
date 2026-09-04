#define IMGUI_DEFINE_MATH_OPERATORS 1

#include "../include/menu.hpp"
#include "../include/bgimage.hpp"
#include "../include/dumper.hpp"
#include "../include/esp.hpp"
#include "../include/executor.hpp"
#include "../include/memory.hpp"
#include "../include/offsets.hpp"
#include "../include/teleport.hpp"
#include "../include/DebugTab.hpp"
#include "../include/updater.hpp"
#include "../include/kdrv_loader.hpp"
#include "../include/games.hpp"
#include "../include/log.hpp"
#include "../include/roblox.hpp"
#include "../include/overlay.hpp"
#include "../include/hitbox_expander.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>

#include <Windows.h>
#include <mmsystem.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace prof_fs = std::filesystem;

namespace { namespace font_blob {
#include "../Framwork/exterium_fonts.h"
} }

namespace {

struct Th {
    ImColor accent        {230, 134, 224, 255};
    ImColor accent_soft   {230, 134, 224, 100};
    ImColor accent_glow   {230, 134, 224,  70};
    ImColor accent_faint  {230, 134, 224,  30};

    ImColor winbg         { 15,  16,  18, 245};
    ImColor sidebar       { 13,  14,  16, 240};
    ImColor topbar        {  5,   5,   5, 120};
    ImColor child         { 19,  19,  19, 255};
    ImColor child_hover   { 26,  26,  30, 255};
    ImColor second        {255, 255, 255,  20};
    ImColor stroke        { 35,  35,  35, 255};
    ImColor stroke_soft   { 26,  26,  30, 255};
    ImColor scroll_bg     { 24,  24,  24, 255};

    ImColor text          {255, 255, 255, 255};
    ImColor text_dim      {200, 200, 200, 255};
    ImColor text_dimmer   {150, 150, 150, 255};
    ImColor text_ghost    {255, 255, 255,  80};
    ImColor foreground    {255, 255, 255,  38};

    ImColor ok            {126, 220, 156, 255};
    ImColor warn          {240, 200,  96, 255};
    ImColor bad           {240,  96, 108, 255};
    ImColor ink           { 10,  10,  12, 255};

    float win_w      = 1220.f;
    float win_h      = 740.f;
    float win_round  = 16.f;
    float topbar_h   = 58.f;
    float sidebar_w  = 224.f;
    float frame_w    = 476.f;
    float frame_h    = 656.f;
    float frame_gap  = 14.f;
    float item_h     = 38.f;
    float tab_h      = 36.f;
} g_th;

ImFont* g_font_regular = nullptr;
ImFont* g_font_medium  = nullptr;
ImFont* g_font_small   = nullptr;
ImFont* g_font_icon    = nullptr;
ImFont* g_font_icon_lg = nullptr;
ImFont* g_font_logo    = nullptr;
ImFont* g_font_arrow   = nullptr;

enum class Screen { ModePicker, BootTerminal, Launcher, Menu };
std::atomic<Screen> g_screen{Screen::ModePicker};
std::atomic<bool>   g_visible{true};
std::atomic<bool>   g_scan_ok{false};
std::atomic<bool>   g_injected{false};
std::atomic<int>    g_access_mode{0};
std::atomic<bool>   g_kernel_fallback{false};

int g_active_tab = 0;

struct AnimCache {
    struct E { float v; uint32_t last_frame; };
    std::unordered_map<ImGuiID, E> m;
    static constexpr size_t MAX = 1024;

    float step(ImGuiID id, float target, float speed) {
        auto& e = m[id];
        e.last_frame = (uint32_t)ImGui::GetFrameCount();
        float dt = ImGui::GetIO().DeltaTime;
        if (dt <= 0.f || dt > 0.25f) dt = 1.f / 60.f;
        float t = 1.f - std::exp(-speed * dt);
        e.v += (target - e.v) * t;
        if (std::fabs(e.v - target) < 0.001f) e.v = target;
        if (m.size() > MAX) evict();
        return e.v;
    }
    void evict() {
        uint32_t f = (uint32_t)ImGui::GetFrameCount();
        uint32_t cutoff = f > 240u ? f - 240u : 0u;
        for (auto it = m.begin(); it != m.end();) {
            if (it->second.last_frame < cutoff) it = m.erase(it);
            else ++it;
        }
    }
    void erase(ImGuiID id) { m.erase(id); }
};
AnimCache g_anim;

inline float anim(ImGuiID id, float target, float speed = 16.f) {
    return g_anim.step(id, target, speed);
}

inline ImU32 blend(ImU32 a, ImU32 b, float t) {
    if (t <= 0.f) return a;
    if (t >= 1.f) return b;
    ImVec4 av = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 bv = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32({
        av.x + (bv.x - av.x) * t,
        av.y + (bv.y - av.y) * t,
        av.z + (bv.z - av.z) * t,
        av.w + (bv.w - av.w) * t});
}
inline ImU32 with_alpha(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    if (v.w < 0.f) v.w = 0.f;
    if (v.w > 1.f) v.w = 1.f;
    return ImGui::ColorConvertFloat4ToU32(v);
}
inline ImU32 col(const ImColor& c) { return (ImU32)c; }
inline ImU32 col_a(const ImColor& c, float a) { return with_alpha((ImU32)c, a); }
inline bool  hit(const ImRect& r) { return ImGui::IsMouseHoveringRect(r.Min, r.Max); }
inline ImVec4 ImColorToImVec4(const ImColor& c) { return c.Value; }

inline ImVec2 center_text(ImVec2 mn, ImVec2 mx, const char* text) {
    return mn + (mx - mn) * 0.5f - ImGui::CalcTextSize(text) * 0.5f;
}

float uptime() {
    static auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
}

int g_rot_start = 0;
void rot_start(ImDrawList* dl) { g_rot_start = dl->VtxBuffer.Size; }
void rot_end(ImDrawList* dl, float rad) {
    ImVec2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
    auto& buf = dl->VtxBuffer;
    for (int i = g_rot_start; i < buf.Size; i++) {
        lo = ImMin(lo, buf[i].pos);
        hi = ImMax(hi, buf[i].pos);
    }
    ImVec2 center = (lo + hi) * 0.5f;
    float s = std::sin(rad), c = std::cos(rad);
    auto rotate = [&](ImVec2 p) {
        p = p - center;
        return ImVec2(p.x * c - p.y * s, p.x * s + p.y * c) + center;
    };
    for (int i = g_rot_start; i < buf.Size; i++) buf[i].pos = rotate(buf[i].pos);
}

void drop_shadow(ImDrawList* dl, ImVec2 tl, ImVec2 br,
                 float rounding, float radius, float strength) {
    for (int i = 0; i < 8; i++) {
        float s = (i + 1) / 8.f;
        float e = radius * s;
        ImU32 c = IM_COL32(0, 0, 0, (int)(strength * 255.f * (1.f - s) * (1.f - s) * 0.55f));
        dl->AddRectFilled({tl.x - e, tl.y - e * 0.4f + 4.f},
                          {br.x + e, br.y + e},
                          c, rounding + e);
    }
}

void faux_blur(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 tint, float rounding) {
    dl->AddRectFilled(mn, mx, tint, rounding);
    for (int y = 0; y < (int)(mx.y - mn.y); y += 3)
        dl->AddLine({mn.x, mn.y + (float)y}, {mx.x, mn.y + (float)y},
                    IM_COL32(255, 255, 255, 4), 1.f);
}

void particle_backdrop(ImVec2 wp, ImVec2 ws, float alpha_mult) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    static ImVec2 pos[64];
    static float  speed[64];
    static float  size[64];
    static float  rot[64];
    static bool   inited[64] = {};

    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.1f) dt = 0.1f;

    dl->PushClipRect(wp, wp + ws, true);
    for (int i = 0; i < 32; i++) {
        if (!inited[i] || pos[i].y < -60.f) {
            pos[i].x   = (float)(rand() % (int)ws.x);
            pos[i].y   = ws.y + 30.f + (rand() % 200);
            speed[i]   = 30.f + (rand() % 90);
            size[i]    = 10.f + (rand() % 24);
            rot[i]     = 0.f;
            inited[i]  = true;
        }
        pos[i].y  -= dt * speed[i];
        rot[i]    -= dt * 0.5f;

        rot_start(dl);
        float half = size[i];
        ImVec2 mn = wp + pos[i] - ImVec2(half, half);
        ImVec2 mx = wp + pos[i] + ImVec2(half, half);
        for (int g = 0; g < 4; g++) {
            float s = (g + 1) * 3.f;
            dl->AddRect({mn.x - s, mn.y - s}, {mx.x + s, mx.y + s},
                        col_a(g_th.accent, 0.06f * (1.f - g / 4.f) * alpha_mult),
                        6.f + s, 0, 1.f);
        }
        dl->AddRectFilled(mn, mx, col_a(g_th.accent, 0.25f * alpha_mult), 5.f);
        rot_end(dl, rot[i]);
    }
    dl->PopClipRect();
}

namespace ex {

bool Tab(const char* label, const char* icon_glyph, int* state, int idx) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGui::PushID(idx);
    const float W  = ImGui::GetContentRegionAvail().x;
    const float H  = g_th.tab_h;
    ImVec2 pos = win->DC.CursorPos;
    ImRect bb(pos, {pos.x + W, pos.y + H});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 4.f}}, 0.f);
    ImGuiID id = win->GetID("tab");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return false; }

    bool active  = (*state == idx);
    bool hovered = hit(bb);
    bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (pressed) *state = idx;

    ImDrawList* dl = win->DrawList;
    float thov = anim(id, hovered ? 1.f : 0.f, 20.f);
    float tact = anim(id ^ 0xACAC, active ? 1.f : 0.f, 22.f);

    if (tact > 0.02f) {
        dl->AddRectFilled(bb.Min, bb.Max, col_a(g_th.child, 0.85f * tact), 6.f);
    } else if (thov > 0.02f) {
        dl->AddRectFilled(bb.Min, bb.Max, col_a(g_th.child_hover, 0.55f * thov), 6.f);
    }

    if (tact > 0.02f) {
        dl->AddRectFilled({bb.Min.x + 2.f, bb.Min.y + 7.f},
                          {bb.Min.x + 4.f, bb.Max.y - 7.f},
                          col_a(g_th.accent, tact), 1.f);
        for (int i = 0; i < 5; i++) {
            float s = (i + 1) * 3.f;
            dl->AddRect({bb.Min.x + 2.f - s, bb.Min.y + 7.f - s * 0.5f},
                        {bb.Min.x + 4.f, bb.Max.y - 7.f + s * 0.5f},
                        col_a(g_th.accent, tact * (1.f - i / 5.f) * 0.10f), 3.f, 0, 1.f);
        }
    }

    if (icon_glyph && g_font_icon) {
        ImGui::PushFont(g_font_icon);
        ImVec2 iz = ImGui::CalcTextSize(icon_glyph);
        ImU32 icol = active ? col(g_th.accent)
                            : blend(col(g_th.text_dimmer), col(g_th.text), thov);
        dl->AddText({bb.Min.x + 14.f, bb.Min.y + (H - iz.y) * 0.5f + 1.f}, icol, icon_glyph);
        ImGui::PopFont();
    }

    if (g_font_medium) ImGui::PushFont(g_font_medium);
    ImU32 lcol = active ? col(g_th.text)
                        : blend(col(g_th.text_dimmer), col(g_th.text), thov);
    dl->AddText({bb.Min.x + 40.f, bb.Min.y + (H - ImGui::GetFontSize()) * 0.5f - 1.f},
                lcol, label);
    if (g_font_medium) ImGui::PopFont();

    ImGui::PopID();
    return pressed;
}

void GroupHeader(const char* label) {
    if (g_font_small) ImGui::PushFont(g_font_small);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText({pos.x + 10.f, pos.y + 4.f},
        col(g_th.text_ghost), label);
    ImGui::Dummy({0, ImGui::GetFontSize() + 6.f});
    if (g_font_small) ImGui::PopFont();
}

bool Checkbox(const char* label, bool* v) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGui::PushID(label);
    const float W = ImGui::GetContentRegionAvail().x;
    const float H = g_th.item_h;
    ImVec2 pos = win->DC.CursorPos;
    ImRect bb(pos, {pos.x + W, pos.y + H});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 2.f}}, 0.f);
    ImGuiID id = win->GetID("cb");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return false; }

    bool hovered = hit(bb);
    bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (pressed) *v = !*v;

    ImDrawList* dl = win->DrawList;
    float thov = anim(id, hovered ? 1.f : 0.f, 22.f);
    float ton  = anim(id ^ 0xFEED, *v ? 1.f : 0.f, 20.f);

    if (thov > 0.02f)
        dl->AddRectFilled(bb.Min, bb.Max, col_a(g_th.foreground, 0.4f * thov), 5.f);

    dl->AddText({bb.Min.x + 12.f, bb.Min.y + (H - ImGui::GetFontSize()) * 0.5f},
                *v ? col(g_th.text)
                   : blend(col(g_th.text_dimmer), col(g_th.text_dim), thov),
                label);

    const float pw = 32.f, ph = 18.f;
    ImVec2 pmn{bb.Max.x - pw - 10.f, bb.Min.y + (H - ph) * 0.5f};
    ImVec2 pmx{pmn.x + pw,           pmn.y + ph};
    ImU32 pill_bg = blend(col(g_th.stroke), col(g_th.accent), ton);
    dl->AddRectFilled(pmn, pmx, pill_bg, ph * 0.5f);
    if (ton > 0.02f) {
        for (int i = 0; i < 4; i++) {
            float s = (i + 1) * 1.5f;
            dl->AddRect({pmn.x - s, pmn.y - s}, {pmx.x + s, pmx.y + s},
                        col_a(g_th.accent, ton * (1.f - i / 4.f) * 0.25f),
                        (ph * 0.5f) + s, 0, 1.f);
        }
    }
    float pad = 2.f;
    float dia = ph - pad * 2.f;
    float x0  = pmn.x + pad;
    float x1  = pmx.x - pad - dia;
    float cx  = x0 + (x1 - x0) * ton;
    dl->AddRectFilled({cx, pmn.y + pad}, {cx + dia, pmx.y - pad},
                      blend(col(g_th.text_dim), col(g_th.text), ton), dia * 0.5f);

    ImGui::PopID();
    return pressed;
}

bool SliderFloat(const char* label, float* v, float mn, float mx,
                 const char* fmt = "%.2f") {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGui::PushID(label);
    const float W = ImGui::GetContentRegionAvail().x;
    const float H = g_th.item_h + 12.f;
    ImVec2 pos = win->DC.CursorPos;
    ImRect bb(pos, {pos.x + W, pos.y + H});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 2.f}}, 0.f);
    ImGuiID id = win->GetID("sf");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return false; }

    ImDrawList* dl = win->DrawList;

    dl->AddText({bb.Min.x + 12.f, bb.Min.y + 4.f}, col(g_th.text_dim), label);
    char buf[32]; std::snprintf(buf, sizeof(buf), fmt, *v);
    ImVec2 vsz = ImGui::CalcTextSize(buf);
    dl->AddText({bb.Max.x - vsz.x - 12.f, bb.Min.y + 4.f}, col(g_th.text), buf);

    float track_y = bb.Min.y + 28.f;
    ImRect track({bb.Min.x + 12.f, track_y}, {bb.Max.x - 12.f, track_y + 4.f});
    ImRect grab_zone({track.Min.x - 6.f, track.Min.y - 8.f},
                     {track.Max.x + 6.f, track.Max.y + 8.f});

    bool hovered = hit(grab_zone) || hit(bb);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        ImGui::SetActiveID(id, win);
    if (ImGui::GetActiveID() == id && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        ImGui::ClearActiveID();
    bool changed = false;
    if (ImGui::GetActiveID() == id) {
        float t = (ImGui::GetIO().MousePos.x - track.Min.x) / track.GetWidth();
        if (t < 0.f) t = 0.f;
        if (t > 1.f) t = 1.f;
        float nv = mn + t * (mx - mn);
        if (nv != *v) { *v = nv; changed = true; }
    }
    float pct = (mx > mn) ? ((*v - mn) / (mx - mn)) : 0.f;
    if (pct < 0.f) pct = 0.f;
    if (pct > 1.f) pct = 1.f;

    dl->AddRectFilled(track.Min, track.Max, col(g_th.scroll_bg), 2.f);
    if (pct > 0.001f)
        dl->AddRectFilled(track.Min,
                          {track.Min.x + track.GetWidth() * pct, track.Max.y},
                          col(g_th.accent), 2.f);

    ImVec2 knob{track.Min.x + track.GetWidth() * pct, track_y + 2.f};
    float knob_h = anim(id ^ 0xB0F1, hovered ? 1.f : 0.f, 16.f);
    if (pct > 0.001f)
        dl->AddCircleFilled(knob, 7.f + 2.f * knob_h, col_a(g_th.accent, 0.5f));
    dl->AddCircleFilled(knob, 5.f, col(g_th.accent));
    dl->AddCircle(knob, 5.f, col(g_th.ink), 0, 1.f);

    ImGui::PopID();
    return changed;
}
bool SliderInt(const char* label, int* v, int mn, int mx) {
    float fv = (float)*v;
    bool ch = SliderFloat(label, &fv, (float)mn, (float)mx, "%.0f");
    *v = (int)std::round(fv);
    return ch;
}

bool Combo(const char* label, int* v, const char* const* items, int n) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGui::PushID(label);
    const float W = ImGui::GetContentRegionAvail().x;
    const float H = g_th.item_h;
    ImVec2 pos = win->DC.CursorPos;
    ImRect bb(pos, {pos.x + W, pos.y + H});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 2.f}}, 0.f);
    ImGuiID id = win->GetID("cmb");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return false; }

    bool hovered = hit(bb);
    ImDrawList* dl = win->DrawList;
    float thov = anim(id, hovered ? 1.f : 0.f, 20.f);

    dl->AddText({bb.Min.x + 12.f, bb.Min.y + 4.f}, col(g_th.text_dim), label);

    const float pw = 100.f, ph = 20.f;
    ImVec2 pmn{bb.Max.x - pw - 10.f, bb.Min.y + (H - ph) * 0.5f + 2.f};
    ImVec2 pmx{pmn.x + pw, pmn.y + ph};
    ImU32 pbg = blend(col(g_th.stroke_soft), col(g_th.child_hover), thov);
    dl->AddRectFilled(pmn, pmx, pbg, 4.f);
    dl->AddRect(pmn, pmx, col_a(g_th.accent, 0.35f), 4.f, 0, 1.f);
    const char* cur = (*v >= 0 && *v < n) ? items[*v] : "-";
    ImVec2 csz = ImGui::CalcTextSize(cur);
    dl->AddText({pmn.x + 8.f, pmn.y + (ph - csz.y) * 0.5f}, col(g_th.text), cur);
    float cx = pmx.x - 12.f, cy = pmn.y + ph * 0.5f;
    dl->AddTriangleFilled({cx - 3.f, cy - 2.f}, {cx + 3.f, cy - 2.f}, {cx, cy + 3.f},
                          col(g_th.text_dim));

    bool changed = false;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        ImGui::OpenPopup("##cmb_pop");
    ImGui::SetNextWindowSize({pw + 16.f, 0.f});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, col(g_th.sidebar));
    ImGui::PushStyleColor(ImGuiCol_Border,  col(g_th.stroke));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6, 6});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {0, 2});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.f);
    if (ImGui::BeginPopup("##cmb_pop")) {
        for (int i = 0; i < n; i++) {
            ImGui::PushID(i);
            bool sel = (i == *v);
            ImVec2 ip = ImGui::GetCursorScreenPos();
            float iw = ImGui::GetContentRegionAvail().x;
            float ih = 24.f;
            ImRect ir(ip, {ip.x + iw, ip.y + ih});
            ImGui::Dummy({iw, ih});
            bool ih_hov = hit(ir);
            bool ih_pr  = ih_hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if (ih_pr) { *v = i; changed = true; ImGui::CloseCurrentPopup(); }
            ImDrawList* d2 = ImGui::GetWindowDrawList();
            if (sel)         d2->AddRectFilled(ir.Min, ir.Max, col_a(g_th.accent, 0.25f), 4.f);
            else if (ih_hov) d2->AddRectFilled(ir.Min, ir.Max, col(g_th.child_hover), 4.f);
            d2->AddText({ir.Min.x + 8.f, ir.Min.y + (ih - ImGui::GetFontSize()) * 0.5f},
                        sel ? col(g_th.accent) : col(g_th.text), items[i]);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    ImGui::PopID();
    return changed;
}

bool ColorEdit4Compact(const char* label, float col_rgba[4]) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGui::PushID(label);
    ImVec2 pos = win->DC.CursorPos;
    const float sw = 40.f, sh = 18.f;
    ImRect bb(pos, {pos.x + sw, pos.y + sh});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 2.f}}, 0.f);
    ImGuiID id = win->GetID("cp");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return false; }

    ImDrawList* dl = win->DrawList;
    ImU32 c = ImGui::ColorConvertFloat4ToU32({col_rgba[0], col_rgba[1], col_rgba[2], col_rgba[3]});
    for (int y = 0; y < (int)sh; y += 4) {
        for (int x = 0; x < (int)sw; x += 4) {
            bool o = ((x + y) / 4) % 2;
            dl->AddRectFilled({bb.Min.x + x, bb.Min.y + y},
                              {bb.Min.x + x + 4, bb.Min.y + y + 4},
                              o ? IM_COL32(60, 60, 60, 255) : IM_COL32(90, 90, 90, 255));
        }
    }
    dl->AddRectFilled(bb.Min, bb.Max, c, 3.f);
    dl->AddRect(bb.Min, bb.Max, col(g_th.stroke), 3.f, 0, 1.f);

    bool hovered = hit(bb);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        ImGui::OpenPopup("##cp_pop");

    bool changed = false;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, col(g_th.sidebar));
    ImGui::PushStyleColor(ImGuiCol_Border,  col(g_th.stroke));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 10});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.f);
    if (ImGui::BeginPopup("##cp_pop")) {
        ImGui::SetNextItemWidth(220.f);
        changed = ImGui::ColorPicker4("##pk", col_rgba,
            ImGuiColorEditFlags_PickerHueWheel |
            ImGuiColorEditFlags_NoSidePreview  |
            ImGuiColorEditFlags_NoLabel        |
            ImGuiColorEditFlags_AlphaBar);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    ImGui::PopID();
    return changed;
}
bool ColorEditImVec4(const char* label, ImVec4* v) {
    float c[4] = { v->x, v->y, v->z, v->w };
    bool ch = ColorEdit4Compact(label, c);
    if (ch) { v->x = c[0]; v->y = c[1]; v->z = c[2]; v->w = c[3]; }
    return ch;
}

bool ColorRow(const char* label, ImVec4* v) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGui::PushID(label);
    const float W = ImGui::GetContentRegionAvail().x;
    const float H = g_th.item_h;
    ImVec2 pos = win->DC.CursorPos;
    ImRect bb(pos, {pos.x + W, pos.y + H});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 2.f}}, 0.f);
    ImGuiID id = win->GetID("cr");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return false; }

    bool hovered = hit(bb);
    ImDrawList* dl = win->DrawList;
    float thov = anim(id, hovered ? 1.f : 0.f, 20.f);
    if (thov > 0.02f)
        dl->AddRectFilled(bb.Min, bb.Max, col_a(g_th.foreground, 0.4f * thov), 5.f);
    dl->AddText({bb.Min.x + 12.f, bb.Min.y + (H - ImGui::GetFontSize()) * 0.5f},
                blend(col(g_th.text_dimmer), col(g_th.text), thov), label);

    ImVec2 saved = ImGui::GetCursorPos();
    ImGui::SetCursorScreenPos({bb.Max.x - 50.f, bb.Min.y + (H - 18.f) * 0.5f});
    float c[4] = { v->x, v->y, v->z, v->w };
    bool ch = ColorEdit4Compact("##rc", c);
    if (ch) { v->x = c[0]; v->y = c[1]; v->z = c[2]; v->w = c[3]; }
    ImGui::SetCursorPos(saved);
    ImGui::Dummy({0, 0});

    ImGui::PopID();
    return ch;
}

extern int* g_listen_key;
extern bool g_listen_pre[256];
extern bool g_listen_last[256];
void _start_listen(int* pk);
const char* _vk_name(int vk);

bool Keybind(const char* id_str, int* pk) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;

    ImGui::PushID(id_str);
    ImVec2 pos = win->DC.CursorPos;
    const float kw = 70.f, kh = 20.f;
    ImRect bb(pos, {pos.x + kw, pos.y + kh});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 2.f}}, 0.f);
    ImGuiID id = win->GetID("kb");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return false; }

    bool listening = (g_listen_key == pk);
    bool hovered   = hit(bb);
    bool pr_l      = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool pr_r      = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (pr_l) { _start_listen(pk); listening = true; }
    if (pr_r && !listening) *pk = 0;

    ImDrawList* dl = win->DrawList;
    float thov = anim(id, (hovered || listening) ? 1.f : 0.f, 20.f);
    ImU32 bg = listening ? col(g_th.accent)
                         : blend(col(g_th.stroke_soft), col(g_th.child_hover), thov);
    dl->AddRectFilled(bb.Min, bb.Max, bg, 4.f);
    dl->AddRect(bb.Min, bb.Max, col_a(g_th.accent, listening ? 1.f : 0.35f), 4.f, 0, 1.f);

    const char* t = listening ? "[..]" : _vk_name(*pk);
    ImVec2 tz = ImGui::CalcTextSize(t);
    dl->AddText({bb.Min.x + (kw - tz.x) * 0.5f, bb.Min.y + (kh - tz.y) * 0.5f},
                listening ? col(g_th.ink) : col(g_th.text), t);

    ImGui::PopID();
    return pr_l;
}

void Pickerbox(const char* label, bool* v, float col_rgba[4]) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return;

    ImGui::PushID(label);
    const float W = ImGui::GetContentRegionAvail().x;
    const float H = g_th.item_h;
    ImVec2 pos = win->DC.CursorPos;
    ImRect bb(pos, {pos.x + W, pos.y + H});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 2.f}}, 0.f);
    ImGuiID id = win->GetID("pb");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return; }

    ImRect left(bb.Min, {bb.Max.x - 60.f, bb.Max.y});
    bool hovered = hit(left);
    bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (pressed) *v = !*v;

    ImDrawList* dl = win->DrawList;
    float thov = anim(id, hovered ? 1.f : 0.f, 22.f);
    float ton  = anim(id ^ 0xFEED, *v ? 1.f : 0.f, 20.f);
    if (thov > 0.02f)
        dl->AddRectFilled(bb.Min, bb.Max, col_a(g_th.foreground, 0.4f * thov), 5.f);

    dl->AddText({bb.Min.x + 12.f, bb.Min.y + (H - ImGui::GetFontSize()) * 0.5f},
                *v ? col(g_th.text) : blend(col(g_th.text_dimmer), col(g_th.text_dim), thov),
                label);

    const float pw = 32.f, ph = 18.f;
    ImVec2 pmn{bb.Max.x - pw - 60.f, bb.Min.y + (H - ph) * 0.5f};
    ImVec2 pmx{pmn.x + pw, pmn.y + ph};
    ImU32 pill = blend(col(g_th.stroke), col(g_th.accent), ton);
    dl->AddRectFilled(pmn, pmx, pill, ph * 0.5f);
    float pad = 2.f, dia = ph - pad * 2.f;
    float x0 = pmn.x + pad, x1 = pmx.x - pad - dia;
    float cxk = x0 + (x1 - x0) * ton;
    dl->AddRectFilled({cxk, pmn.y + pad}, {cxk + dia, pmx.y - pad},
                      blend(col(g_th.text_dim), col(g_th.text), ton), dia * 0.5f);

    ImVec2 saved = ImGui::GetCursorPos();
    ImGui::SetCursorScreenPos({bb.Max.x - 46.f, bb.Min.y + (H - 18.f) * 0.5f});
    ColorEdit4Compact("##swatch", col_rgba);
    ImGui::SetCursorPos(saved);
    ImGui::Dummy({0, 0});

    ImGui::PopID();
}

void Keybindbox(const char* label, bool* v, int* pk) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return;

    ImGui::PushID(label);
    const float W = ImGui::GetContentRegionAvail().x;
    const float H = g_th.item_h;
    ImVec2 pos = win->DC.CursorPos;
    ImRect bb(pos, {pos.x + W, pos.y + H});
    ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 2.f}}, 0.f);
    ImGuiID id = win->GetID("kbx");
    if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); return; }

    ImRect left(bb.Min, {bb.Max.x - 78.f, bb.Max.y});
    bool hovered = hit(left);
    bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (pressed) *v = !*v;

    ImDrawList* dl = win->DrawList;
    float thov = anim(id, hovered ? 1.f : 0.f, 22.f);
    float ton  = anim(id ^ 0xFACE, *v ? 1.f : 0.f, 20.f);
    if (thov > 0.02f)
        dl->AddRectFilled(bb.Min, bb.Max, col_a(g_th.foreground, 0.4f * thov), 5.f);

    dl->AddText({bb.Min.x + 12.f, bb.Min.y + (H - ImGui::GetFontSize()) * 0.5f},
                *v ? col(g_th.text) : blend(col(g_th.text_dimmer), col(g_th.text_dim), thov),
                label);

    const float pw = 32.f, ph = 18.f;
    ImVec2 pmn{bb.Max.x - pw - 78.f, bb.Min.y + (H - ph) * 0.5f};
    ImVec2 pmx{pmn.x + pw, pmn.y + ph};
    ImU32 pill = blend(col(g_th.stroke), col(g_th.accent), ton);
    dl->AddRectFilled(pmn, pmx, pill, ph * 0.5f);
    float pad = 2.f, dia = ph - pad * 2.f;
    float x0 = pmn.x + pad, x1 = pmx.x - pad - dia;
    float cxk = x0 + (x1 - x0) * ton;
    dl->AddRectFilled({cxk, pmn.y + pad}, {cxk + dia, pmx.y - pad},
                      blend(col(g_th.text_dim), col(g_th.text), ton), dia * 0.5f);

    ImVec2 saved = ImGui::GetCursorPos();
    ImGui::SetCursorScreenPos({bb.Max.x - 76.f, bb.Min.y + (H - 20.f) * 0.5f});
    Keybind(label, pk);
    ImGui::SetCursorPos(saved);
    ImGui::Dummy({0, 0});

    ImGui::PopID();
}

void Section(const char* label) {
    ImGui::Dummy({0, 4.f});
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (g_font_small) ImGui::PushFont(g_font_small);
    ImGui::GetWindowDrawList()->AddText({p.x + 12.f, p.y}, col(g_th.text_ghost), label);
    ImGui::Dummy({0, ImGui::GetFontSize() + 4.f});
    if (g_font_small) ImGui::PopFont();
}

bool BeginFrame(const char* id, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(g_th.child));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 10});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {0, 4});
    bool v = ImGui::BeginChild(id, size, true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    return v;
}
void EndFrame() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

}

int  g_listen_key_slot = 0;
int* ex::g_listen_key = nullptr;
bool ex::g_listen_pre[256] = {};
bool ex::g_listen_last[256] = {};

const char* ex::_vk_name(int vk) {
    static char buf[16];
    if (vk == 0) return "None";
    switch (vk) {
        case 0x01: return "LMB";    case 0x02: return "RMB";
        case 0x04: return "MMB";    case 0x05: return "MB4";  case 0x06: return "MB5";
        case 0x08: return "Back";   case 0x09: return "Tab";  case 0x0D: return "Enter";
        case 0x10: return "Shift";  case 0x11: return "Ctrl"; case 0x12: return "Alt";
        case 0x14: return "Caps";   case 0x1B: return "Esc";  case 0x20: return "Space";
        case 0x21: return "PgUp";   case 0x22: return "PgDn";
        case 0x23: return "End";    case 0x24: return "Home";
        case 0x25: return "Left";   case 0x26: return "Up";   case 0x27: return "Right"; case 0x28: return "Down";
        case 0x2C: return "PrtSc";  case 0x2D: return "Ins";  case 0x2E: return "Del";
        case 0xA0: return "LShift"; case 0xA1: return "RShift";
        case 0xA2: return "LCtrl";  case 0xA3: return "RCtrl";
        case 0xA4: return "LAlt";   case 0xA5: return "RAlt";
    }
    if (vk >= 0x30 && vk <= 0x39) { std::snprintf(buf, sizeof(buf), "%c", vk); return buf; }
    if (vk >= 0x41 && vk <= 0x5A) { std::snprintf(buf, sizeof(buf), "%c", vk); return buf; }
    if (vk >= 0x70 && vk <= 0x7B) { std::snprintf(buf, sizeof(buf), "F%d", vk - 0x6F); return buf; }
    if (vk >= 0x60 && vk <= 0x69) { std::snprintf(buf, sizeof(buf), "Num%d", vk - 0x60); return buf; }
    std::snprintf(buf, sizeof(buf), "%02X", vk);
    return buf;
}
void ex::_start_listen(int* pk) {
    ex::g_listen_key = pk;
    for (int i = 0; i < 256; i++) {
        bool d = (GetAsyncKeyState(i) & 0x8000) != 0;
        ex::g_listen_pre[i]  = d;
        ex::g_listen_last[i] = d;
    }
}
void poll_keybind_listen() {
    if (!ex::g_listen_key) return;
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) && !ex::g_listen_pre[VK_ESCAPE]) {
        ex::g_listen_key = nullptr;
        return;
    }
    for (int vk = 0x01; vk <= 0xFE; vk++) {
        if (vk == VK_ESCAPE || vk == VK_LBUTTON) continue;
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool prev = ex::g_listen_last[vk];
        ex::g_listen_last[vk] = down;
        if (ex::g_listen_pre[vk]) { if (!down) ex::g_listen_pre[vk] = false; continue; }
        if (!prev && down) { *ex::g_listen_key = vk; ex::g_listen_key = nullptr; return; }
    }
}

namespace tabs {

static const char* OPTS_BOX[]         = {"Off", "Corner", "Full"};
static const char* OPTS_SNAP[]        = {"Bottom", "Center", "Top"};
static const char* OPTS_BONE[]        = {"Head", "Chest"};
static const char* OPTS_PRI[]         = {"Nearest crosshair", "Lowest HP", "Role"};
static const char* OPTS_SILENT_PART[] = {"Head", "Torso", "Nearest to cursor"};
static const char* OPTS_RIVALS_F[]    = {"Delta", "Ratio"};
static const char* OPTS_CROSS[]       = {"Plus", "T", "Dot", "Circle", "X", "Gap"};

void aim_frames(ImVec2 fs) {
    if (ex::BeginFrame("##aim_a", fs)) {
        ex::Section("Activation");
        ex::Keybindbox ("Activation key",    &esp::cfg.aim_enabled, &esp::cfg.aim_key);
        ex::Checkbox("Toggle mode (latch on press)", &esp::cfg.aim_toggle_mode);
        ex::Section("General");
        ex::Checkbox("Enable aimbot",       &esp::cfg.aim_enabled);
        ex::Checkbox("Lock-on",             &esp::cfg.aim_lock);
        ex::Checkbox("Sticky",              &esp::cfg.aim_sticky);
        ex::Checkbox("Multi-bone fallback", &esp::cfg.aim_multibone);
        ex::Section("Target filtering");
        ex::Checkbox("Team check",          &esp::cfg.team_check);
        ex::Checkbox("Visibility check",    &esp::cfg.aim_visibility_check);
        ex::Checkbox("Wall check (raycast)",&esp::cfg.aim_wallcheck);
        ex::Checkbox("Skip knocked / K.O",  &esp::cfg.aim_knocked_check);
        ex::Section("Selection");
        ex::Combo("Priority", &esp::cfg.aim_priority, OPTS_PRI,  (int)IM_ARRAYSIZE(OPTS_PRI));
        ex::Combo("Bone",     &esp::cfg.aim_bone,    OPTS_BONE, (int)IM_ARRAYSIZE(OPTS_BONE));
        ex::SliderFloat("FOV radius",   &esp::cfg.fov_radius,   10.f, 1000.f, "%.0f");
        ex::SliderFloat("Max distance", &esp::cfg.aim_max_dist,  0.f, 5000.f, "%.0f");
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##aim_b", fs)) {
        ex::Section("Response");
        ex::SliderFloat("Strength",  &esp::cfg.aim_strength, 1.f, 100.f, "%.0f");
        ex::SliderFloat("Smoothing", &esp::cfg.aim_smooth,   0.1f, 1.f,  "%.2f");
        ex::SliderFloat("Deadzone",  &esp::cfg.aim_deadzone, 0.f, 30.f,  "%.0f");
        ex::Checkbox   ("Split X/Y smooth", &esp::cfg.aim_split_smooth);
        ex::SliderFloat("Smoothing Y",      &esp::cfg.aim_smooth_y, 0.1f, 1.f, "%.2f");
        ex::Section("Prediction");
        ex::Checkbox   ("Enable prediction", &esp::cfg.aim_predict);
        ex::SliderFloat("Lead (base)",       &esp::cfg.aim_prediction, 0.f, 0.5f, "%.2f");
        ex::SliderFloat("Lead X/Z mult",     &esp::cfg.aim_predict_xz, 0.f, 3.f,  "%.2f");
        ex::SliderFloat("Lead Y mult",       &esp::cfg.aim_predict_y,  0.f, 3.f,  "%.2f");
        ex::Section("Humanize");
        ex::Checkbox   ("Jitter",       &esp::cfg.aim_jitter);
        ex::SliderFloat("Jitter amount",&esp::cfg.aim_jitter_strength, 0.1f, 2.f, "%.2f");
        ex::Checkbox   ("Shake",        &esp::cfg.aim_shake);
        ex::SliderFloat("Shake X",      &esp::cfg.aim_shake_x, 0.f, 8.f, "%.1f");
        ex::SliderFloat("Shake Y",      &esp::cfg.aim_shake_y, 0.f, 8.f, "%.1f");
        ex::Section("Auto-fire");
        ex::Checkbox   ("Auto-fire on lock", &esp::cfg.aim_autofire);
        ex::SliderFloat("Auto-fire FOV",     &esp::cfg.aim_autofire_fov, 1.f, 20.f, "%.0f");
        ex::EndFrame();
    }
}

static const char* OPTS_HITBOX_TARGET[] = {"Head", "Torso", "HumanoidRootPart"};

void silent_frames(ImVec2 fs) {
    if (ex::BeginFrame("##silent_a", fs)) {
        ex::Section("Hitbox expander (silent)");
        ex::Checkbox   ("Enable hitbox expander", &esp::cfg.hitbox_enabled);
        ex::Combo      ("Target part", &esp::cfg.hitbox_target,
                        OPTS_HITBOX_TARGET, (int)IM_ARRAYSIZE(OPTS_HITBOX_TARGET));
        ex::SliderFloat("Hitbox size (studs)",    &esp::cfg.hitbox_size,     2.f, 60.f, "%.0f");
        ex::SliderFloat("Max distance",           &esp::cfg.aim_max_dist,    0.f, 5000.f, "%.0f");
        ex::Section("Filtering (shared)");
        ex::Checkbox   ("Team check",             &esp::cfg.team_check);
        ex::Checkbox   ("Skip knocked / K.O",     &esp::cfg.silent_knocked_check);
        ex::Section("Viewport-write silent (Da Hood / Rivals-style)");
        ex::Checkbox   ("Enable viewport silent", &esp::cfg.silent_aim);
        ex::Combo      ("Aim part", &esp::cfg.silent_aim_part, OPTS_SILENT_PART,
                        (int)IM_ARRAYSIZE(OPTS_SILENT_PART));
        ex::SliderFloat("Silent FOV",             &esp::cfg.silent_fov,      20.f, 500.f, "%.0f");
        ex::Checkbox   ("Visibility check",       &esp::cfg.aim_visibility_check);
        ex::Checkbox   ("Only fire while LMB",    &esp::cfg.silent_only_when_firing);
        ex::Section("Prediction");
        ex::Checkbox   ("Enable prediction",  &esp::cfg.aim_predict);
        ex::SliderFloat("Lead (base)",        &esp::cfg.aim_prediction, 0.f, 0.5f, "%.2f");
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##silent_b", fs)) {
        ex::Section("Live status");
        {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                esp::cfg.hitbox_enabled ? "expander ON  ·  %d target(s) grown"
                                        : "expander OFF",
                hitbox_expander::g_active_targets.load());
            ImGui::Dummy({0, 4.f});
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddText({p.x + 12.f, p.y},
                col(esp::cfg.hitbox_enabled ? g_th.accent : g_th.text_dimmer), buf);
            ImGui::Dummy({0, ImGui::GetFontSize() + 8.f});
        }
        ex::Section("Rivals viewport tuning");
        ex::Combo      ("Formula", &esp::cfg.rivals_formula, OPTS_RIVALS_F, (int)IM_ARRAYSIZE(OPTS_RIVALS_F));
        ex::Checkbox   ("Flip X",             &esp::cfg.rivals_flip_x);
        ex::Checkbox   ("Flip Y",             &esp::cfg.rivals_flip_y);
        ex::SliderFloat("X bias",             &esp::cfg.rivals_x_bias, -200.f, 200.f, "%.0f");
        ex::SliderFloat("Y bias",             &esp::cfg.rivals_y_bias, -200.f, 200.f, "%.0f");
        ex::Section("Key");
        ex::Keybindbox ("Viewport silent key", &esp::cfg.silent_aim, &esp::cfg.silent_key);
        ex::Section("How it works");
        {
            ImGui::Dummy({0, 2.f});
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            const char* lines[] = {
              "  1. Pick a part (Head / Torso / HRP) on each valid enemy.",
              "  2. Grow its Primitive.Size to {N,N,N}, transparent, no",
              "     collision - so the game's own raycast hits it.",
              "  3. Restore original size + transparency + flags on toggle",
              "     off, DM change, or when the player leaves.",
            };
            for (auto* l : lines) {
                d->AddText({p.x, p.y}, col(g_th.text_dimmer), l);
                p.y += ImGui::GetFontSize() + 2.f;
            }
            ImGui::Dummy({0, (float)IM_ARRAYSIZE(lines) * (ImGui::GetFontSize() + 2.f) + 6.f});
        }
        ex::EndFrame();
    }
}

void trig_frames(ImVec2 fs) {
    if (ex::BeginFrame("##trg_a", fs)) {
        ex::Section("Triggerbot");
        ex::Checkbox   ("Enable trigger",   &esp::cfg.trigger_enabled);
        ex::SliderFloat("Trigger FOV",      &esp::cfg.trigger_fov,    1.f, 50.f, "%.0f");
        ex::SliderFloat("Hitbox scale",     &esp::cfg.trig_box_scale, 0.5f, 4.f, "%.2f");
        ex::SliderInt  ("Fire delay ms",    &esp::cfg.trigger_delay,  30, 500);
        ex::SliderInt  ("Acquire ms",       &esp::cfg.trig_acquire_ms, 0, 500);
        ex::Section("Behaviour");
        ex::Checkbox   ("Hold LMB (auto weapons)", &esp::cfg.trigger_hold_mode);
        ex::Checkbox   ("Team check",              &esp::cfg.trigger_team_check);
        ex::Checkbox   ("Wall check",              &esp::cfg.trigger_wallcheck);
        ex::Checkbox   ("Show hitbox on screen",   &esp::cfg.trig_hitbox_viz);
        ex::Checkbox   ("Skip knife / melee",      &esp::cfg.trig_knife_check);
        ex::Section("Key");
        ex::Keybindbox ("Trigger key", &esp::cfg.trigger_enabled, &esp::cfg.trigger_key);
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##trg_b", fs)) {
        ex::Section("Freeze target");
        ex::Checkbox   ("Enable freeze",     &esp::cfg.freeze_player);
        ex::Keybindbox ("Freeze hold key",   &esp::cfg.freeze_player, &esp::cfg.freeze_player_key);
        ex::Section("Orbit lock (TP-spin)");
        ex::Checkbox   ("Enable TP-spin",    &esp::cfg.tp_spin_enabled);
        ex::SliderFloat("Radius",            &esp::cfg.tp_spin_radius, 2.f, 40.f, "%.1f");
        ex::SliderFloat("Angular speed",     &esp::cfg.tp_spin_speed,  0.5f, 30.f, "%.1f");
        ex::Keybindbox ("Hold key",          &esp::cfg.tp_spin_enabled, &esp::cfg.tp_spin_key);
        ex::Keybindbox ("Cancel key",        &esp::cfg.tp_spin_enabled, &esp::cfg.tp_spin_cancel_key);
        ex::Section("Hitmarker & killfeed");
        ex::Checkbox   ("Hitmarker",         &esp::cfg.hitmarker_enabled);
        ex::Checkbox   ("Beep on hit",       &esp::cfg.hitmarker_sound);
        ex::Checkbox   ("Killfeed",          &esp::cfg.killfeed_enabled);
        ex::EndFrame();
    }
}

void esp_frames(ImVec2 fs) {
    if (ex::BeginFrame("##esp_a", fs)) {
        ex::Section("Master");
        ex::Checkbox   ("Enable ESP",         &esp::cfg.enabled);
        ex::Combo      ("Box style", &esp::cfg.box_style, OPTS_BOX, (int)IM_ARRAYSIZE(OPTS_BOX));
        ex::Checkbox   ("Filled background",  &esp::cfg.draw_filled);
        ex::SliderFloat("Box thickness",      &esp::cfg.box_thickness, 0.5f, 5.f, "%.1f");
        ex::SliderFloat("Max distance",       &esp::cfg.max_dist, 50.f, 10000.f, "%.0f");
        ex::Checkbox   ("Team check",         &esp::cfg.esp_team_check);
        ex::Checkbox   ("HP-colored",         &esp::cfg.esp_box_health_color);
        ex::Checkbox   ("Wall visibility color split", &esp::cfg.esp_wall_color_split);
        ex::Section("Info");
        ex::Checkbox   ("Names",              &esp::cfg.draw_name);
        ex::Checkbox   ("Distance",           &esp::cfg.draw_distance);
        ex::Checkbox   ("Weapon",             &esp::cfg.draw_weapon);
        ex::Checkbox   ("Speed",              &esp::cfg.draw_speed);
        ex::Checkbox   ("User ID",            &esp::cfg.esp_show_id);
        ex::Checkbox   ("Text shadow",        &esp::cfg.text_shadow);
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##esp_b", fs)) {
        ex::Section("Health & indicators");
        ex::Checkbox   ("Health bar",     &esp::cfg.draw_health);
        ex::Checkbox   ("HP text",        &esp::cfg.draw_health_text);
        ex::Checkbox   ("Head dot",       &esp::cfg.draw_head_dot);
        ex::Checkbox   ("Off-screen arrows", &esp::cfg.draw_arrows);
        ex::Checkbox   ("Skeleton",       &esp::cfg.draw_skeleton);
        ex::Checkbox   ("Target line",    &esp::cfg.esp_target_line);
        ex::Section("Lines");
        ex::Checkbox   ("Snaplines",      &esp::cfg.draw_line);
        ex::Combo      ("Snap origin", &esp::cfg.snapline_pos, OPTS_SNAP, (int)IM_ARRAYSIZE(OPTS_SNAP));
        ex::Checkbox   ("Look-at rays",   &esp::cfg.draw_lookat);
        ex::SliderFloat("Look length",    &esp::cfg.lookat_length, 2.f, 40.f, "%.0f");
        ex::Checkbox   ("Velocity lines", &esp::cfg.draw_velocity);
        ex::SliderFloat("Predict seconds",&esp::cfg.velocity_seconds, 0.1f, 3.f, "%.1f");
        ex::Section("Overlays");
        ex::Checkbox   ("FOV circle",     &esp::cfg.draw_fov);
        ex::SliderFloat("FOV radius",     &esp::cfg.fov_radius, 10.f, 1000.f, "%.0f");
        ex::Checkbox   ("Crosshair",      &esp::cfg.draw_crosshair);
        ex::Combo      ("Crosshair style", &esp::cfg.crosshair_style, OPTS_CROSS, (int)IM_ARRAYSIZE(OPTS_CROSS));
        ex::SliderFloat("Crosshair size", &esp::cfg.crosshair_size, 2.f, 30.f, "%.0f");
        ex::Section("Colors - click a swatch for the wheel");
        ex::ColorRow   ("Box",            &esp::cfg.col_box);
        ex::ColorRow   ("Box fill",       &esp::cfg.col_box_fill);
        ex::ColorRow   ("Box (occluded)", &esp::cfg.col_box_occluded);
        ex::ColorRow   ("Visible split",  &esp::cfg.col_esp_visible);
        ex::ColorRow   ("Occluded split", &esp::cfg.col_esp_occluded_alt);
        ex::ColorRow   ("Name",           &esp::cfg.col_name);
        ex::ColorRow   ("Snapline",       &esp::cfg.col_line);
        ex::ColorRow   ("Head dot",       &esp::cfg.col_head);
        ex::ColorRow   ("Skeleton",       &esp::cfg.col_skeleton);
        ex::ColorRow   ("Look-at ray",    &esp::cfg.col_lookat);
        ex::ColorRow   ("Velocity line",  &esp::cfg.col_velocity);
        ex::ColorRow   ("FOV circle",     &esp::cfg.col_fov);
        ex::ColorRow   ("Crosshair",      &esp::cfg.col_crosshair);
        ex::ColorRow   ("Trigger hitbox", &esp::cfg.col_trig_hitbox);
        ex::EndFrame();
    }
}

void world_frames(ImVec2 fs) {
    if (ex::BeginFrame("##world_a", fs)) {
        ex::Section("Visuals");
        ex::Checkbox   ("Fullbright",         &esp::cfg.fullbright);
        ex::Checkbox   ("No skybox",          &esp::cfg.no_skybox);
        ex::Checkbox   ("No blur",            &esp::cfg.vis_no_blur);
        ex::Checkbox   ("No depth of field",  &esp::cfg.vis_no_dof);
        ex::Checkbox   ("No sun rays",        &esp::cfg.vis_no_sunrays);
        ex::Checkbox   ("No atmosphere",      &esp::cfg.vis_no_atmosphere);
        ex::Checkbox   ("No fog",             &esp::cfg.world_nofog);
        ex::Checkbox   ("Custom brightness",  &esp::cfg.world_bright_enabled);
        ex::SliderFloat("Brightness level",   &esp::cfg.world_bright, 0.f, 10.f, "%.1f");
        ex::Section("FPS & voxel");
        ex::Checkbox   ("FPS unlock",         &esp::cfg.fps_unlock);
        ex::SliderInt  ("FPS cap",            &esp::cfg.fps_cap, 30, 1000);
        ex::Checkbox   ("Fullbright (voxel)", &esp::cfg.ff_fullbright);
        ex::Checkbox   ("Grey sky",           &esp::cfg.ff_gray_sky);
        ex::Checkbox   ("Hide adorns",        &esp::cfg.ff_no_adorns);
        ex::Checkbox   ("No grass",           &esp::cfg.ff_no_grass);
        ex::Checkbox   ("Low textures",       &esp::cfg.ff_low_texture);
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##world_b", fs)) {
        ex::Section("Environment");
        ex::Checkbox   ("Clear vision",     &esp::cfg.clear_vision);
        ex::Checkbox   ("Anti-void",        &esp::cfg.anti_void);
        ex::Checkbox   ("Instant prompts",  &esp::cfg.prompts_instant);
        ex::Checkbox   ("Anim speed",       &esp::cfg.anim_speed_enabled);
        ex::SliderFloat("Multiplier",       &esp::cfg.anim_speed_mult, 0.1f, 10.f, "%.1f");
        ex::Checkbox   ("Time of day",      &esp::cfg.world_time_enabled);
        ex::SliderFloat("Clock",            &esp::cfg.world_time, 0.f, 24.f, "%.1f");
        ex::Checkbox   ("Gravity override", &esp::cfg.world_gravity_enabled);
        ex::SliderFloat("Gravity force",    &esp::cfg.world_gravity, 0.f, 400.f, "%.0f");
        ex::Checkbox   ("Tickrate hack",    &esp::cfg.tickrate_enabled);
        ex::SliderFloat("Steps / sec",      &esp::cfg.tickrate_amount, 1.f, 500.f, "%.0f");
        ex::Section("World chams");
        ex::Checkbox   ("Enable chams",     &esp::cfg.world_chams_enabled);
        ex::ColorEditImVec4("Chams color",  &esp::cfg.world_chams_color);
        ex::SliderFloat("Transparency",     &esp::cfg.world_chams_transparency, 0.f, 1.f, "%.2f");
        ex::SliderFloat("Reflectance",      &esp::cfg.world_chams_reflectance,  0.f, 1.f, "%.2f");
        ex::Section("Privacy");
        ex::Checkbox   ("Streamproof",      &esp::cfg.streamproof);
        ex::EndFrame();
    }
}

void cam_frames(ImVec2 fs) {
    if (ex::BeginFrame("##cam_a", fs)) {
        ex::Section("FOV & zoom");
        ex::Checkbox   ("FOV changer",       &esp::cfg.fov_changer_enabled);
        ex::SliderFloat("Field of view",     &esp::cfg.fov_value, 40.f, 120.f, "%.0f");
        ex::Checkbox   ("Zoom unlock",       &esp::cfg.zoom_unlock);
        ex::SliderFloat("Max zoom",          &esp::cfg.max_zoom, 100.f, 2000.f, "%.0f");
        ex::Checkbox   ("Lock first person", &esp::cfg.lock_first_person);
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##cam_b", fs)) {
        ex::Section("Freecam");
        ex::Checkbox   ("Enable freecam",  &esp::cfg.freecam);
        ex::SliderFloat("Freecam speed",   &esp::cfg.freecam_speed, 10.f, 300.f, "%.0f");
        ex::Section("Anti-aim / anti-cheat");
        ex::Checkbox   ("Spinbot",         &esp::cfg.antiaim_spin);
        ex::SliderFloat("Spin speed",      &esp::cfg.antiaim_spin_speed, 90.f, 1440.f, "%.0f");
        ex::Checkbox   ("Hitbox jitter",   &esp::cfg.antiaim_jitter);
        ex::SliderFloat("Jitter amount",   &esp::cfg.antiaim_jitter_amount, 1.f, 10.f, "%.1f");
        ex::EndFrame();
    }
}

void move_frames(ImVec2 fs) {
    if (ex::BeginFrame("##mv_a", fs)) {
        ex::Section("Fly");
        ex::Checkbox   ("Enable fly",   &esp::cfg.fly_enabled);
        ex::SliderFloat("Fly speed",    &esp::cfg.fly_speed, 10.f, 300.f, "%.0f");
        ex::Checkbox   ("Planar",       &esp::cfg.fly_planar);
        ex::Section("Speed / jump");
        ex::Checkbox   ("WalkSpeed",    &esp::cfg.speed_enabled);
        ex::SliderFloat("Speed value",  &esp::cfg.speed_value, 16.f, 200.f, "%.0f");
        ex::Checkbox   ("JumpPower",    &esp::cfg.jump_enabled);
        ex::SliderFloat("Power value",  &esp::cfg.jump_value, 50.f, 500.f, "%.0f");
        ex::Checkbox   ("Jump height",  &esp::cfg.jump_height_enabled);
        ex::SliderFloat("Height value", &esp::cfg.jump_height, 5.f, 100.f, "%.0f");
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##mv_b", fs)) {
        ex::Section("Miscellaneous");
        ex::Checkbox   ("Hover",              &esp::cfg.hover_enabled);
        ex::SliderFloat("Hover height",       &esp::cfg.hover_height, 2.f, 30.f, "%.1f");
        ex::Checkbox   ("No-clip",            &esp::cfg.noclip_enabled);
        ex::Checkbox   ("Infinite jump",      &esp::cfg.infinite_jump);
        ex::Checkbox   ("Auto bunny-hop",     &esp::cfg.bunny_hop);
        ex::Checkbox   ("Climb any slope",    &esp::cfg.no_slope_limit);
        ex::Checkbox   ("Anti-AFK",           &esp::cfg.antiafk);
        ex::Checkbox   ("Godmode",            &esp::cfg.godmode);
        ex::EndFrame();
    }
}

void hud_frames(ImVec2 fs) {
    if (ex::BeginFrame("##hud_a", fs)) {
        ex::Section("Radar");
        ex::Checkbox   ("Enable radar",   &esp::cfg.radar_enabled);
        ex::SliderFloat("Size",           &esp::cfg.radar_size,  70.f, 200.f, "%.0f");
        ex::SliderFloat("Range",          &esp::cfg.radar_range, 50.f, 500.f, "%.0f");
        ex::Checkbox   ("Names",          &esp::cfg.radar_names);
        ex::Section("Overlays");
        ex::Checkbox   ("Feature HUD",       &esp::cfg.draw_hud);
        ex::Checkbox   ("Watermark",         &esp::cfg.draw_watermark);
        ex::Checkbox   ("Keybind overlay",   &esp::cfg.draw_keybind_overlay);
        ex::Checkbox   ("Notifications",     &esp::cfg.notifications);
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##hud_b", fs)) {
        ex::Section("Proximity alerts");
        ex::Checkbox   ("Enable alerts",  &esp::cfg.sound_alerts);
        ex::SliderFloat("Range",          &esp::cfg.sound_alert_range, 20.f, 300.f, "%.0f");
        ex::SliderInt  ("Cooldown (ms)",  &esp::cfg.sound_alert_cd_ms, 200, 5000);
        ex::Section("Music HUD");
        ex::Checkbox   ("Spotify HUD",    &esp::cfg.spotify_hud_enabled);
        ex::Checkbox   ("Edit HUD",       &esp::cfg.spotify_hud_edit);
        ex::Section("Debug");
        ex::Checkbox   ("Debug console",  &esp::cfg.show_console);
        ex::EndFrame();
    }
}

void config_frames(ImVec2 fs) {
    if (ex::BeginFrame("##cfg_a", fs)) {
        ex::Section("Toggle keys");
        ex::Keybindbox ("Fly",         &esp::cfg.fly_enabled,     &esp::cfg.key_fly);
        ex::Keybindbox ("No-clip",     &esp::cfg.noclip_enabled,  &esp::cfg.key_noclip);
        ex::Keybindbox ("Lock-on",     &esp::cfg.aim_lock,        &esp::cfg.key_lock);
        ex::Keybindbox ("ESP",         &esp::cfg.enabled,         &esp::cfg.key_esp);
        ex::Keybindbox ("Aimbot",      &esp::cfg.aim_enabled,     &esp::cfg.key_aim);
        ex::Keybindbox ("Triggerbot",  &esp::cfg.trigger_enabled, &esp::cfg.key_trigger);
        ex::Keybindbox ("Radar",       &esp::cfg.radar_enabled,   &esp::cfg.key_radar);
        ex::Section("Utility keys");
        ex::Keybindbox ("TP under cursor", &esp::cfg.enabled,      &esp::cfg.key_tp_mouse);
        ex::SliderFloat("TP cursor distance", &esp::cfg.tp_mouse_dist, 10.f, 200.f, "%.0f");
        ex::Keybindbox ("Panic (kill all)",  &esp::cfg.enabled,     &esp::cfg.key_panic);
        ex::Checkbox   ("Per-game auto-load profiles", &esp::cfg.per_game_profiles);
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##cfg_b", fs)) {
        ex::Section("Weapon mods (Arsenal / BB / PF / BP / CB)");
        ex::Checkbox   ("No recoil",       &esp::cfg.arsenal_no_recoil);
        ex::Checkbox   ("No spread",       &esp::cfg.arsenal_no_spread);
        ex::Checkbox   ("Instant reload",  &esp::cfg.arsenal_auto_reload);
        ex::Checkbox   ("Rapid fire",      &esp::cfg.arsenal_rapid_fire);
        ex::SliderFloat("Rapid fire (rps)",&esp::cfg.arsenal_fire_rate, 1.f, 60.f, "%.0f");
        ex::Section("Knife Duels");
        ex::Checkbox   ("Knife curve",     &esp::cfg.kd_knife_curve);
        ex::Checkbox   ("Hitbox expander", &esp::cfg.kd_hitbox_expand);
        ex::SliderFloat("Hitbox size",     &esp::cfg.kd_hitbox_size, 2.f, 40.f, "%.0f");
        ex::Checkbox   ("Auto-throw",      &esp::cfg.kd_auto_throw);
        ex::SliderFloat("Throw cooldown",  &esp::cfg.kd_auto_throw_cooldown, 60.f, 1500.f, "%.0f");
        ex::EndFrame();
    }
}

void game_frames(ImVec2 fs) {
    games::Id g = games::active();
    if (ex::BeginFrame("##g_a", fs)) {
        ex::Section("Active game");
        ImGui::Dummy({0, 2.f});
        ImVec2 hp = ImGui::GetCursorScreenPos();
        if (g_font_medium) ImGui::PushFont(g_font_medium);
        ImGui::GetWindowDrawList()->AddText({hp.x + 12.f, hp.y}, col(g_th.accent),
            games::name_of(g));
        if (g_font_medium) ImGui::PopFont();
        ImGui::Dummy({0, ImGui::GetFontSize() + 4.f});
        if (g_font_small) ImGui::PushFont(g_font_small);
        ImVec2 tp2 = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText({tp2.x + 12.f, tp2.y}, col(g_th.text_dim),
            games::tagline_of(g));
        ImGui::Dummy({0, ImGui::GetFontSize() + 10.f});
        if (g_font_small) ImGui::PopFont();

        ex::Section("Common");
        ex::Checkbox   ("Enable ESP",     &esp::cfg.enabled);
        ex::Checkbox   ("Enable aimbot",  &esp::cfg.aim_enabled);
        ex::Checkbox   ("Silent aim",     &esp::cfg.silent_aim);
        ex::Checkbox   ("Team check",     &esp::cfg.team_check);
        ex::Checkbox   ("Visibility check",&esp::cfg.aim_visibility_check);
        ex::SliderFloat("FOV",            &esp::cfg.fov_radius, 10.f, 1000.f, "%.0f");
        ex::SliderFloat("Max distance",   &esp::cfg.aim_max_dist, 0.f, 5000.f, "%.0f");
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##g_b", fs)) {
        ex::Section("Game-specific");
        switch (g) {
            case games::Id::Arsenal:
            case games::Id::BadBusiness:
            case games::Id::PhantomForces:
            case games::Id::BigPaintball:
            case games::Id::CounterBlox:
                ex::Checkbox   ("No recoil",       &esp::cfg.arsenal_no_recoil);
                ex::Checkbox   ("No spread",       &esp::cfg.arsenal_no_spread);
                ex::Checkbox   ("Instant reload",  &esp::cfg.arsenal_auto_reload);
                ex::Checkbox   ("Rapid fire",      &esp::cfg.arsenal_rapid_fire);
                ex::SliderFloat("Rate (rps)",      &esp::cfg.arsenal_fire_rate, 1.f, 60.f, "%.0f");
                break;
            case games::Id::DaHood:
            case games::Id::KAT:
            case games::Id::Assassin:
                ex::Combo      ("Silent aim part", &esp::cfg.silent_aim_part, OPTS_SILENT_PART, (int)IM_ARRAYSIZE(OPTS_SILENT_PART));
                ex::SliderFloat("Silent FOV",      &esp::cfg.silent_fov, 20.f, 500.f, "%.0f");
                ex::Checkbox   ("Skip knocked",    &esp::cfg.silent_knocked_check);
                break;
            case games::Id::StrongestBG:
                ex::Checkbox   ("Walk speed",   &esp::cfg.speed_enabled);
                ex::SliderFloat("Speed value",  &esp::cfg.speed_value, 16.f, 200.f, "%.0f");
                ex::Checkbox   ("Jump power",   &esp::cfg.jump_enabled);
                ex::SliderFloat("Power value",  &esp::cfg.jump_value, 50.f, 500.f, "%.0f");
                break;
            case games::Id::BloxFruits:
                ex::SliderFloat("Max distance", &esp::cfg.max_dist, 500.f, 10000.f, "%.0f");
                ex::Checkbox   ("Walk speed",   &esp::cfg.speed_enabled);
                ex::SliderFloat("Speed value",  &esp::cfg.speed_value, 16.f, 200.f, "%.0f");
                break;
            case games::Id::Rivals:
                ex::Combo      ("Formula",  &esp::cfg.rivals_formula, OPTS_RIVALS_F, (int)IM_ARRAYSIZE(OPTS_RIVALS_F));
                ex::Checkbox   ("Only while firing", &esp::cfg.silent_only_when_firing);
                ex::Checkbox   ("Flip X",   &esp::cfg.rivals_flip_x);
                ex::Checkbox   ("Flip Y",   &esp::cfg.rivals_flip_y);
                ex::SliderFloat("X bias",   &esp::cfg.rivals_x_bias, -200.f, 200.f, "%.0f");
                ex::SliderFloat("Y bias",   &esp::cfg.rivals_y_bias, -200.f, 200.f, "%.0f");
                break;
            case games::Id::KnifeDuels:
                ex::Checkbox   ("Knife curve",     &esp::cfg.kd_knife_curve);
                ex::Checkbox   ("Hitbox expander", &esp::cfg.kd_hitbox_expand);
                ex::SliderFloat("Hitbox size",     &esp::cfg.kd_hitbox_size, 2.f, 40.f, "%.0f");
                ex::Checkbox   ("Auto-throw",      &esp::cfg.kd_auto_throw);
                ex::SliderFloat("Cooldown (ms)",   &esp::cfg.kd_auto_throw_cooldown, 60.f, 1500.f, "%.0f");
                break;
            case games::Id::Flick:
            case games::Id::Universal:
            default:
                ImGui::Dummy({0, 10.f});
                ImGui::GetWindowDrawList()->AddText(ImGui::GetCursorScreenPos(),
                    col(g_th.text_dim),
                    "  No game-specific panel - use the other tabs.");
                break;
        }
        ex::EndFrame();
    }
}

static std::string profiles_dir() { return mem::exe_dir() + "\\profiles"; }
static void ensure_profiles_dir() { std::error_code ec; prof_fs::create_directories(profiles_dir(), ec); }
static std::string profile_path(const std::string& name) { return profiles_dir() + "\\" + name + ".cfg"; }
static std::string sanitize(const std::string& in) {
    std::string out;
    for (char c : in) if (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ') out += c;
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back()  == ' ') out.pop_back();
    return out;
}
struct ProfileEntry { std::string name; uint64_t size = 0; std::time_t mtime = 0; };
static std::vector<ProfileEntry> g_prof_cache;
static double                    g_prof_cache_next = 0.0;
static std::string               g_prof_active;
static char                      g_prof_new[64] = {};
static std::string               g_prof_toast;
static float                     g_prof_toast_ttl = 0.f;
static std::string               g_prof_delete_confirm;

static void refresh_profiles() {
    double now = ImGui::GetTime();
    if (now < g_prof_cache_next) return;
    g_prof_cache_next = now + 0.5;
    ensure_profiles_dir();
    g_prof_cache.clear();
    std::error_code ec;
    for (auto& e : prof_fs::directory_iterator(profiles_dir(), ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        auto p = e.path();
        if (p.extension() != ".cfg") continue;
        ProfileEntry pe;
        pe.name = p.stem().string();
        pe.size = (uint64_t)e.file_size(ec);
        auto ft = e.last_write_time(ec);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ft - decltype(ft)::clock::now() + std::chrono::system_clock::now());
        pe.mtime = std::chrono::system_clock::to_time_t(sctp);
        g_prof_cache.push_back(pe);
    }
    std::sort(g_prof_cache.begin(), g_prof_cache.end(),
              [](const ProfileEntry& a, const ProfileEntry& b) { return a.mtime > b.mtime; });
}
static void toast(const std::string& s) { g_prof_toast = s; g_prof_toast_ttl = 2.5f; }
static std::string humanize_age(std::time_t t) {
    if (t == 0) return "unknown";
    std::time_t now = std::time(nullptr);
    long long dt = (long long)std::difftime(now, t);
    if (dt < 60)    return "just now";
    char b[32];
    if (dt < 3600)  { std::snprintf(b, sizeof(b), "%lldm ago", dt / 60);    return b; }
    if (dt < 86400) { std::snprintf(b, sizeof(b), "%lldh ago", dt / 3600);  return b; }
    std::snprintf(b, sizeof(b), "%lldd ago", dt / 86400);
    return b;
}

void profiles_frames(ImVec2 fs) {
    refresh_profiles();
    if (ex::BeginFrame("##pf_a", fs)) {
        ex::Section("Save new profile");
        ImGui::SetCursorPos({12.f, ImGui::GetCursorPosY()});
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        col(g_th.scroll_bg));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col(g_th.child_hover));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  col(g_th.child_hover));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 6});
        ImGui::PushItemWidth(fs.x - 24.f);
        bool enter = ImGui::InputTextWithHint("##newp", "profile name..", g_prof_new, IM_ARRAYSIZE(g_prof_new),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        std::string name = sanitize(g_prof_new);
        bool can = !name.empty();
        ImGui::Dummy({0, 6.f});
        ImGui::SetCursorPos({12.f, ImGui::GetCursorPosY()});
        ImVec2 bp = ImGui::GetCursorScreenPos();
        float bw = fs.x - 24.f, bh = 32.f;
        ImRect br(bp, {bp.x + bw, bp.y + bh});
        bool bh_hov = hit(br);
        bool bh_pr  = bh_hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        ImGui::Dummy({bw, bh + 4.f});
        ImDrawList* dl2 = ImGui::GetWindowDrawList();
        ImU32 bg = can ? blend(col(g_th.accent_soft), col(g_th.accent), anim(ImGui::GetCurrentWindow()->GetID("sbtn"), bh_hov ? 1.f : 0.f, 16.f))
                       : col(g_th.child_hover);
        ImU32 fg = can ? col(g_th.ink) : col(g_th.text_dimmer);
        dl2->AddRectFilled(br.Min, br.Max, bg, 6.f);
        const char* lbl = "Save current config as profile";
        ImVec2 ls = ImGui::CalcTextSize(lbl);
        dl2->AddText({br.Min.x + (bw - ls.x) * 0.5f, br.Min.y + (bh - ls.y) * 0.5f}, fg, lbl);
        if (bh_pr && can) {
            if (esp::save_config(profile_path(name).c_str())) {
                g_prof_active = name;
                toast("Saved \"" + name + "\"");
                g_prof_new[0] = 0;
                g_prof_cache_next = 0.0;
            } else toast("Failed to save profile");
        }
        if (enter && can) {
            if (esp::save_config(profile_path(name).c_str())) {
                g_prof_active = name;
                toast("Saved \"" + name + "\"");
                g_prof_new[0] = 0;
                g_prof_cache_next = 0.0;
            }
        }
        ex::Section("Status");
        char sb[128];
        std::snprintf(sb, sizeof(sb), "%zu profile(s) on disk", g_prof_cache.size());
        ImGui::Dummy({0, 4.f});
        ImVec2 tp = ImGui::GetCursorScreenPos();
        dl2->AddText({tp.x + 12.f, tp.y}, col(g_th.text_dim), sb);
        ImGui::Dummy({0, 18.f});
        if (!g_prof_active.empty()) {
            std::string a = "active: " + g_prof_active;
            ImVec2 tp2 = ImGui::GetCursorScreenPos();
            dl2->AddText({tp2.x + 12.f, tp2.y}, col(g_th.accent), a.c_str());
        }
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##pf_b", fs)) {
        if (g_prof_cache.empty()) {
            ex::Section("Nothing saved yet");
        } else {
            ex::Section("Saved profiles");
            for (const auto& pe : g_prof_cache) {
                ImGui::PushID(pe.name.c_str());
                ImGuiWindow* win = ImGui::GetCurrentWindow();
                const float W = ImGui::GetContentRegionAvail().x;
                const float H = 54.f;
                ImVec2 pos = win->DC.CursorPos;
                ImRect bb(pos, {pos.x + W, pos.y + H});
                ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 6.f}}, 0.f);
                ImGuiID id = win->GetID("prow");
                if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); continue; }

                bool hov = hit(bb);
                bool active = (g_prof_active == pe.name);
                ImDrawList* d = win->DrawList;
                float thov = anim(id, hov ? 1.f : 0.f, 18.f);
                float tac  = anim(id ^ 0xACAC, active ? 1.f : 0.f, 18.f);
                d->AddRectFilled(bb.Min, bb.Max,
                    blend(col(g_th.child), col(g_th.child_hover), std::max(thov, tac)), 6.f);
                if (tac > 0.02f) {
                    d->AddRectFilled({bb.Min.x + 2.f, bb.Min.y + 6.f},
                                     {bb.Min.x + 4.f, bb.Max.y - 6.f},
                                     col_a(g_th.accent, tac));
                }

                if (g_font_medium) ImGui::PushFont(g_font_medium);
                d->AddText({bb.Min.x + 12.f, bb.Min.y + 8.f},
                           blend(col(g_th.text_dim), col(g_th.text), std::max(thov, tac)),
                           pe.name.c_str());
                if (g_font_medium) ImGui::PopFont();
                char meta[96];
                std::snprintf(meta, sizeof(meta), "%llu bytes  ·  %s",
                              (unsigned long long)pe.size, humanize_age(pe.mtime).c_str());
                if (g_font_small) ImGui::PushFont(g_font_small);
                d->AddText({bb.Min.x + 12.f, bb.Min.y + 30.f}, col(g_th.text_dimmer), meta);
                if (g_font_small) ImGui::PopFont();

                auto btn = [&](const char* lbl, float right_x, bool danger) -> std::pair<bool, float> {
                    ImVec2 sz = ImGui::CalcTextSize(lbl);
                    float bw2 = sz.x + 20.f, bh2 = 22.f;
                    ImRect r({right_x - bw2, bb.Min.y + (H - bh2) * 0.5f},
                             {right_x,       bb.Min.y + (H + bh2) * 0.5f});
                    bool h2 = hit(r);
                    bool p2 = h2 && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                    ImGuiID bid = win->GetID(lbl);
                    float bt = anim(bid, h2 ? 1.f : 0.f, 18.f);
                    ImU32 acc = danger ? col(g_th.bad) : col(g_th.accent);
                    ImU32 bg  = blend(col(g_th.child_hover), acc, bt);
                    ImU32 fg  = bt > 0.5f ? col(g_th.ink) : col(g_th.text_dim);
                    d->AddRectFilled(r.Min, r.Max, bg, 4.f);
                    d->AddRect(r.Min, r.Max, col_a(danger ? g_th.bad : g_th.accent, 0.5f), 4.f, 0, 1.f);
                    d->AddText({r.Min.x + (bw2 - sz.x) * 0.5f, r.Min.y + (bh2 - sz.y) * 0.5f}, fg, lbl);
                    return {p2, bw2};
                };
                float rx = bb.Max.x - 10.f;
                bool pending = (g_prof_delete_confirm == pe.name);
                auto [dp, dw] = btn(pending ? "Confirm?" : "Del", rx, true);
                if (dp) {
                    if (pending) {
                        std::error_code ec;
                        prof_fs::remove(profile_path(pe.name), ec);
                        if (ec) toast("Failed to delete " + pe.name);
                        else    toast("Deleted \"" + pe.name + "\"");
                        if (g_prof_active == pe.name) g_prof_active.clear();
                        g_prof_delete_confirm.clear();
                        g_prof_cache_next = 0.0;
                    } else g_prof_delete_confirm = pe.name;
                }
                rx -= dw + 4.f;
                auto [op, ow] = btn("Save", rx, false);
                if (op) {
                    if (esp::save_config(profile_path(pe.name).c_str())) {
                        g_prof_active = pe.name;
                        toast("Overwrote \"" + pe.name + "\"");
                    } else toast("Failed");
                    g_prof_delete_confirm.clear();
                    g_prof_cache_next = 0.0;
                }
                rx -= ow + 4.f;
                auto [lp, lw] = btn("Load", rx, false);
                if (lp) {
                    if (esp::load_config(profile_path(pe.name).c_str())) {
                        g_prof_active = pe.name;
                        toast("Loaded \"" + pe.name + "\"");
                    } else toast("Failed");
                    g_prof_delete_confirm.clear();
                }
                (void)lw;

                ImGui::PopID();
            }
        }
        ex::EndFrame();
    }

    if (g_prof_toast_ttl > 0.f) {
        g_prof_toast_ttl -= ImGui::GetIO().DeltaTime;
        float a = g_prof_toast_ttl > 1.f ? 1.f : g_prof_toast_ttl;
        ImVec2 tsz = ImGui::CalcTextSize(g_prof_toast.c_str());
        float pw = tsz.x + 28.f, ph = 26.f;
        ImDrawList* fd = ImGui::GetForegroundDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        float tx = wp.x + ws.x - pw - 20.f;
        float ty = wp.y + ws.y - ph - 20.f;
        fd->AddRectFilled({tx, ty}, {tx + pw, ty + ph}, col_a(g_th.sidebar, a * 0.95f), 6.f);
        fd->AddRect({tx, ty}, {tx + pw, ty + ph}, col_a(g_th.accent, a * 0.6f), 6.f, 0, 1.f);
        fd->AddCircleFilled({tx + 10.f, ty + ph * 0.5f}, 3.f, col_a(g_th.accent, a));
        fd->AddText({tx + 20.f, ty + (ph - tsz.y) * 0.5f}, col_a(g_th.text, a), g_prof_toast.c_str());
    }
}

static char g_players_filter[64] = {};
void players_frames(ImVec2 fs, const std::vector<PlayerInfo>* players) {
    if (ex::BeginFrame("##pl_a", fs)) {
        ex::Section("Filter");
        ImGui::SetCursorPos({12.f, ImGui::GetCursorPosY()});
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        col(g_th.scroll_bg));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col(g_th.child_hover));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  col(g_th.child_hover));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 6});
        ImGui::PushItemWidth(fs.x - 24.f);
        ImGui::InputTextWithHint("##pfilt", "type a name..", g_players_filter, IM_ARRAYSIZE(g_players_filter));
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ex::Section("Stats");
        ImGui::Dummy({0, 4.f});
        ImVec2 tp = ImGui::GetCursorScreenPos();
        char sb[64];
        std::snprintf(sb, sizeof(sb), "%d players in game", players ? (int)players->size() : 0);
        ImGui::GetWindowDrawList()->AddText({tp.x + 12.f, tp.y}, col(g_th.text_dim), sb);
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##pl_b", fs)) {
        ex::Section("Players");
        if (!players || players->empty()) {
            ImGui::Dummy({0, 6.f});
            ImVec2 tp2 = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddText({tp2.x + 12.f, tp2.y}, col(g_th.text_dim),
                "  none - waiting on Roblox.");
        } else {
            std::string fs2 = g_players_filter;
            std::transform(fs2.begin(), fs2.end(), fs2.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });
            for (const auto& p : *players) {
                if (!fs2.empty()) {
                    std::string lo = p.name;
                    std::transform(lo.begin(), lo.end(), lo.begin(),
                        [](unsigned char c) { return (char)std::tolower(c); });
                    if (lo.find(fs2) == std::string::npos) continue;
                }
                ImGuiWindow* win = ImGui::GetCurrentWindow();
                uintptr_t ptr = p.player_ptr;
                ImGui::PushID((int)ImHashData(&ptr, sizeof(ptr)));
                const float W = ImGui::GetContentRegionAvail().x;
                const float H = 44.f;
                ImVec2 pos = win->DC.CursorPos;
                ImRect bb(pos, {pos.x + W, pos.y + H});
                ImGui::ItemSize({bb.Min, {bb.Max.x, bb.Max.y + 4.f}}, 0.f);
                ImGuiID id = win->GetID("prow");
                if (!ImGui::ItemAdd(bb, id)) { ImGui::PopID(); continue; }

                bool hov = hit(bb);
                ImDrawList* d = win->DrawList;
                float t = anim(id, hov ? 1.f : 0.f, 18.f);
                d->AddRectFilled(bb.Min, bb.Max,
                    blend(col(g_th.child), col(g_th.child_hover), t), 5.f);

                ImU32 dot = (p.health <= 0.f) ? col(g_th.text_dimmer) : col(g_th.ok);
                d->AddCircleFilled({bb.Min.x + 10.f, bb.Min.y + H * 0.5f}, 4.f, dot);
                d->AddText({bb.Min.x + 22.f, bb.Min.y + 6.f},
                    (p.health <= 0.f) ? col(g_th.text_dimmer) : col(g_th.text), p.name.c_str());
                char sub[64];
                if (p.health > 0.f)
                    std::snprintf(sub, sizeof(sub), "hp %.0f/%.0f", p.health, p.max_health);
                else
                    std::snprintf(sub, sizeof(sub), "dead");
                if (g_font_small) ImGui::PushFont(g_font_small);
                d->AddText({bb.Min.x + 22.f, bb.Min.y + 24.f}, col(g_th.text_dimmer), sub);
                if (g_font_small) ImGui::PopFont();

                auto chip = [&](const char* lbl, float rx) -> std::pair<bool, float> {
                    ImVec2 sz = ImGui::CalcTextSize(lbl);
                    float bw2 = sz.x + 18.f, bh2 = 22.f;
                    ImRect r({rx - bw2, bb.Min.y + (H - bh2) * 0.5f},
                             {rx,       bb.Min.y + (H + bh2) * 0.5f});
                    bool h2 = hit(r);
                    bool p2 = h2 && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                    ImGuiID bid = win->GetID(lbl);
                    float bt = anim(bid, h2 ? 1.f : 0.f, 18.f);
                    ImU32 bg = blend(col(g_th.child_hover), col(g_th.accent), bt);
                    ImU32 fg = bt > 0.5f ? col(g_th.ink) : col(g_th.text_dim);
                    d->AddRectFilled(r.Min, r.Max, bg, 4.f);
                    d->AddRect(r.Min, r.Max, col_a(g_th.accent, 0.4f), 4.f, 0, 1.f);
                    d->AddText({r.Min.x + (bw2 - sz.x) * 0.5f,
                                r.Min.y + (bh2 - sz.y) * 0.5f}, fg, lbl);
                    return {p2, bw2};
                };
                float rx = bb.Max.x - 8.f;
                auto [lp, lw] = chip("Lock", rx);
                if (lp && p.hrp_ptr) {
                    uintptr_t prim = rpm<uintptr_t>(p.hrp_ptr + offsets::BasePart::Primitive);
                    if (prim) {
                        if (tp::g_lock.load() && tp::g_lock_target.load() == prim)
                            tp::stop_lock();
                        else
                            tp::start_lock(prim);
                    }
                }
                rx -= lw + 4.f;
                auto [sp, sw] = chip("Spec", rx);
                if (sp && p.humanoid_ptr) {
                    if (tp::g_spectate.load() && tp::g_spectate_hum.load() == p.humanoid_ptr)
                        tp::stop_spectate();
                    else
                        tp::start_spectate(p.humanoid_ptr);
                }
                rx -= sw + 4.f;
                auto [tpp, tpw] = chip("TP", rx);
                if (tpp && p.hrp_ptr) {
                    Vec3 dest = p.position; dest.y += 3.f;
                    tp::request_teleport(dest, 45);
                }
                (void)tpw;

                ImGui::PopID();
            }
        }
        ex::EndFrame();
    }
}

static char g_expl_filter[64] = {};
static std::unordered_map<uintptr_t, uint8_t> g_expl_open;

void explorer_frames(ImVec2 fs) {
    if (ex::BeginFrame("##ex_a", fs)) {
        ex::Section("Filter");
        ImGui::SetCursorPos({12.f, ImGui::GetCursorPosY()});
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        col(g_th.scroll_bg));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col(g_th.child_hover));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  col(g_th.child_hover));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 6});
        ImGui::PushItemWidth(fs.x - 24.f);
        ImGui::InputTextWithHint("##efilt", "descendant name..", g_expl_filter, IM_ARRAYSIZE(g_expl_filter));
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        ex::Section("Actions");
        ImVec2 bp = ImGui::GetCursorScreenPos();
        float bw = fs.x - 24.f, bh = 28.f;
        ImRect br({bp.x + 12.f, bp.y}, {bp.x + 12.f + bw, bp.y + bh});
        bool hov = hit(br);
        bool pr  = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dumper::g_dumping.load();
        ImGui::Dummy({bw + 12.f, bh + 4.f});
        ImU32 bg = dumper::g_dumping.load()
            ? blend(col(g_th.warn), col(g_th.accent), 0.5f + 0.5f * std::sin(uptime() * 6.f))
            : blend(col(g_th.child_hover), col(g_th.accent), anim(ImGui::GetCurrentWindow()->GetID("dmp"), hov ? 1.f : 0.f, 16.f));
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(br.Min, br.Max, bg, 5.f);
        const char* lbl = dumper::g_dumping.load() ? "dumping.." : "Dump DataModel";
        ImVec2 ls = ImGui::CalcTextSize(lbl);
        d->AddText({br.Min.x + (bw - ls.x) * 0.5f, br.Min.y + (bh - ls.y) * 0.5f},
                   hov ? col(g_th.ink) : col(g_th.text), lbl);
        if (pr) dumper::dump_datamodel_async(mem::exe_dir() + "\\dumps");
        ex::EndFrame();
    }
    ImGui::SameLine(0.f, g_th.frame_gap);
    if (ex::BeginFrame("##ex_b", fs)) {
        ex::Section("DataModel");
        RbxDataModel dm = RbxDataModel::get();
        if (!dm.valid()) {
            ImGui::Dummy({0, 6.f});
            ImVec2 tp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddText({tp.x + 12.f, tp.y}, col(g_th.text_dim),
                "  DataModel not resolved yet.");
        } else {
            std::string fs2 = g_expl_filter;
            std::transform(fs2.begin(), fs2.end(), fs2.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });
            struct Frame { RbxInstance node; int depth; };
            std::vector<Frame> stack;
            stack.push_back({RbxInstance{dm.ptr}, 0});

            int rendered = 0;
            while (!stack.empty() && rendered < 256) {
                Frame f = stack.back(); stack.pop_back();
                if (!f.node.valid()) continue;
                std::string cls = f.node.get_class();
                if (cls.empty()) continue;
                std::string nm = f.node.get_name();
                if (nm.empty()) nm = cls;

                bool is_open = g_expl_open[f.node.ptr] != 0;
                ImGuiWindow* win = ImGui::GetCurrentWindow();
                float indent = (float)f.depth * 14.f;
                float rh = 20.f;
                float rw = ImGui::GetContentRegionAvail().x;
                ImVec2 pos = win->DC.CursorPos;
                ImRect bb({pos.x + indent + 8.f, pos.y}, {pos.x + rw, pos.y + rh});
                ImGui::Dummy({rw, rh + 2.f});
                ImGui::PushID((int)ImHashData(&f.node.ptr, sizeof(f.node.ptr)));
                ImGuiID id = win->GetID("er");
                bool hov = hit(bb);
                bool pr  = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                if (pr) { is_open = !is_open; g_expl_open[f.node.ptr] = is_open ? 1 : 0; }
                ImDrawList* d = win->DrawList;
                float t = anim(id, hov ? 1.f : 0.f, 18.f);
                if (t > 0.02f)
                    d->AddRectFilled(bb.Min, bb.Max, col_a(g_th.foreground, 0.5f * t), 3.f);

                float cx2 = bb.Min.x + 2.f, cy2 = bb.Min.y + rh * 0.5f;
                ImVec2 p0 = is_open ? ImVec2{cx2 - 3.f, cy2 - 2.f} : ImVec2{cx2 - 2.f, cy2 - 3.f};
                ImVec2 p1 = is_open ? ImVec2{cx2 + 3.f, cy2 - 2.f} : ImVec2{cx2 - 2.f, cy2 + 3.f};
                ImVec2 p2 = is_open ? ImVec2{cx2,       cy2 + 3.f} : ImVec2{cx2 + 3.f, cy2};
                d->AddTriangleFilled(p0, p1, p2, col(g_th.text_dim));

                char line[220];
                std::snprintf(line, sizeof(line), "%s   %s", nm.c_str(), cls.c_str());
                d->AddText({bb.Min.x + 14.f, bb.Min.y + (rh - ImGui::GetFontSize()) * 0.5f},
                           hov ? col(g_th.text) : col(g_th.text_dimmer), line);

                ImGui::PopID();
                rendered++;

                if (is_open && f.depth < 6) {
                    auto ch = f.node.get_children();
                    for (auto it = ch.rbegin(); it != ch.rend(); ++it)
                        stack.push_back({*it, f.depth + 1});
                }
            }
        }
        ex::EndFrame();
    }
}

}

struct TabDef {
    const char* group;
    const char* label;
    const char* icon;
    void (*draw)(ImVec2 fs);
};

void draw_players_wrapper(ImVec2 fs);
const std::vector<PlayerInfo>* g_players_snapshot = nullptr;

TabDef g_tabs[] = {
    {"Aimbot",   "Aim",       "9", tabs::aim_frames},
    {nullptr,    "Silent",    "0", tabs::silent_frames},
    {nullptr,    "Trigger",   "5", tabs::trig_frames},
    {"Visuals",  "ESP",       "8", tabs::esp_frames},
    {nullptr,    "World",     "3", tabs::world_frames},
    {nullptr,    "Camera",    "6", tabs::cam_frames},
    {"Others",   "Movement",  "4", tabs::move_frames},
    {nullptr,    "HUD",       "1", tabs::hud_frames},
    {nullptr,    "Config",    "2", tabs::config_frames},
    {"Game",     "Game",      "7", tabs::game_frames},
    {nullptr,    "Profiles",  "b", tabs::profiles_frames},
    {nullptr,    "Players",   "d", draw_players_wrapper},
    {nullptr,    "Explorer",  "c", tabs::explorer_frames},
};
constexpr int TAB_COUNT = (int)IM_ARRAYSIZE(g_tabs);

void draw_players_wrapper(ImVec2 fs) { tabs::players_frames(fs, g_players_snapshot); }

namespace screens {

std::atomic<bool> g_boot_sound_played{false};
std::wstring find_boot_mp3() {
    static const wchar_t* NAMES[] = { L"poppop.ai - uwu meme sound.mp3", L"poppop.mp3" };
    wchar_t exe_dir[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe_dir, MAX_PATH)) {
        wchar_t* slash = std::wcsrchr(exe_dir, L'\\');
        if (slash) *(slash + 1) = 0;
    }
    wchar_t appdata[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring bases[] = {
        exe_dir,
        std::wstring(appdata) + L"\\UwUClient\\",
        std::wstring(tmp)     + L"uwuclient\\",
        tmp };
    for (auto& base : bases)
        for (auto* name : NAMES) {
            std::wstring p = base + name;
            DWORD attr = GetFileAttributesW(p.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return p;
        }
    return {};
}
void play_boot_sound_once() {
    if (g_boot_sound_played.exchange(true)) return;
    std::wstring path = find_boot_mp3();
    if (path.empty()) return;
    std::thread([path]() {
        mciSendStringW(L"close uwuboot", nullptr, 0, nullptr);
        std::wstring cmd = L"open \"" + path + L"\" type mpegvideo alias uwuboot";
        if (mciSendStringW(cmd.c_str(), nullptr, 0, nullptr) != 0) return;
        mciSendStringW(L"play uwuboot", nullptr, 0, nullptr);
    }).detach();
}

void shell(ImVec2 wp, ImVec2 ws, ImDrawList* dl, const char* title, float title_h = 55.f) {
    drop_shadow(ImGui::GetBackgroundDrawList(), wp, wp + ws, g_th.win_round, 42.f, 0.85f);
    dl->AddRectFilled(wp, wp + ws, col(g_th.winbg), g_th.win_round);
    dl->AddRectFilled(wp, {wp.x + ws.x, wp.y + title_h}, col(g_th.topbar), g_th.win_round,
                      ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled({wp.x, wp.y + title_h - 3.f}, {wp.x + ws.x, wp.y + title_h}, col(g_th.accent));
    if (title && g_font_logo) {
        ImGui::PushFont(g_font_logo);
        ImVec2 tp = center_text(wp, {wp.x + ws.x, wp.y + title_h - 3.f}, title);
        dl->AddText(tp, col(g_th.text), title);
        ImGui::PopFont();
    }
}

void mode_picker() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = g_th.win_w, H = g_th.win_h;
    ImVec2 wp{(io.DisplaySize.x - W) * 0.5f, (io.DisplaySize.y - H) * 0.5f};
    ImGui::SetNextWindowPos(wp, ImGuiCond_Always);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    bool open = true;
    ImGui::Begin("##mp", &open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (ImTextureID bgtex = bgimage::srv()) {
        ImVec2 uv0, uv1;
        bgimage::cover_uv(ws.x, ws.y, uv0, uv1);
        dl->AddImageRounded(bgtex, wp, wp + ws, uv0, uv1, IM_COL32_WHITE, g_th.win_round);
        dl->AddRectFilled(wp, wp + ws, IM_COL32(0, 0, 0, 180), g_th.win_round);
    }
    shell(wp, ws, dl, "UwUClient", 55.f);
    particle_backdrop(wp, ws, 0.9f);

    if (g_font_medium) ImGui::PushFont(g_font_medium);
    const char* sub = "choose access mode";
    ImVec2 subz = ImGui::CalcTextSize(sub);
    dl->AddText({wp.x + (ws.x - subz.x) * 0.5f, wp.y + 90.f}, col(g_th.text_dim), sub);
    if (g_font_medium) ImGui::PopFont();

    bool upd_c, upd_a, upd_d;
    { std::string _l, _c; updater::snapshot(upd_c, upd_a, upd_d, _l, _c); }
    bool block = upd_c && upd_a && !upd_d;

    struct Card { const char* name; const char* tag; const char* desc; int mode; };
    Card CARDS[] = {
        {"Usermode",    "recommended", "standard access mode.\nworks on every supported game.", 0},
        {"Kernel Mode", "beta",        "driver-based access via \\\\.\\uwudrv.\nrequires admin.", 1},
    };
    const float CW = 300.f, CH = 220.f, GAP = 22.f;
    const float BASE_X = wp.x + (ws.x - 2 * CW - GAP) * 0.5f;
    const float BASE_Y = wp.y + 160.f;

    for (int i = 0; i < 2; i++) {
        ImVec2 mn{BASE_X + i * (CW + GAP), BASE_Y};
        ImVec2 mx{mn.x + CW, mn.y + CH};
        ImRect r(mn, mx);
        bool hov = !block && hit(r);
        bool pr  = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        ImGuiID id = ImGui::GetCurrentWindow()->GetID(CARDS[i].name);
        float t = anim(id, hov ? 1.f : 0.f, 18.f);
        dl->AddRectFilled(mn, mx, blend(col(g_th.child), col(g_th.child_hover), t), 12.f);
        dl->AddRect(mn, mx, col_a(g_th.accent, 0.3f + t * 0.5f), 12.f, 0, 1.5f);
        dl->AddRectFilled({mn.x, mn.y + 12.f}, {mn.x + 3.f, mx.y - 12.f}, col(g_th.accent));
        if (g_font_logo) {
            ImGui::PushFont(g_font_logo);
            dl->AddText({mn.x + 22.f, mn.y + 22.f}, col(g_th.text), CARDS[i].name);
            ImGui::PopFont();
        }
        if (g_font_small) ImGui::PushFont(g_font_small);
        ImVec2 tsz = ImGui::CalcTextSize(CARDS[i].tag);
        float tw = tsz.x + 14.f, thg = 18.f;
        ImVec2 tmn{mx.x - tw - 16.f, mn.y + 26.f};
        ImVec2 tmx{tmn.x + tw, tmn.y + thg};
        dl->AddRectFilled(tmn, tmx, col(g_th.scroll_bg), thg * 0.5f);
        dl->AddRect(tmn, tmx, col_a(g_th.accent, 0.5f), thg * 0.5f, 0, 1.f);
        dl->AddText({tmn.x + 7.f, tmn.y + (thg - tsz.y) * 0.5f}, col(g_th.accent), CARDS[i].tag);
        float dy = mn.y + 80.f;
        const char* p = CARDS[i].desc;
        while (*p) {
            const char* nl = std::strchr(p, '\n');
            const char* end = nl ? nl : (p + std::strlen(p));
            std::string line(p, end - p);
            dl->AddText({mn.x + 22.f, dy}, col(g_th.text_dim), line.c_str());
            dy += ImGui::GetFontSize() + 4.f;
            if (!nl) break;
            p = nl + 1;
        }
        const char* hint = "click to launch";
        ImVec2 hz = ImGui::CalcTextSize(hint);
        dl->AddText({mn.x + (CW - hz.x) * 0.5f, mx.y - 22.f}, col(g_th.accent), hint);
        if (g_font_small) ImGui::PopFont();

        if (pr) {
            g_access_mode.store(CARDS[i].mode);
            bool fallback = false;
            if (CARDS[i].mode == 1) {
                bool ok = kdrv_loader::try_load();
                if (!ok) { fallback = true; elog::warn("kernel: fallback to usermode"); }
            }
            g_kernel_fallback.store(fallback);
            g_screen.store(Screen::BootTerminal);
            play_boot_sound_once();
        }
    }

    if (g_font_small) ImGui::PushFont(g_font_small);
    const char* credit = "made by q3c on discord";
    ImVec2 cz = ImGui::CalcTextSize(credit);
    dl->AddText({wp.x + (ws.x - cz.x) * 0.5f, wp.y + ws.y - 28.f}, col(g_th.text_dimmer), credit);
    if (g_font_small) ImGui::PopFont();

    if (block) {
        std::string latest, current;
        bool a, b, c;
        updater::snapshot(a, b, c, latest, current);
        ImDrawList* fd = ImGui::GetForegroundDrawList();
        fd->AddRectFilled({0, 0}, io.DisplaySize, IM_COL32(0, 0, 0, 140));
        const float DW = 480.f, DH = 230.f;
        ImVec2 dmn{(io.DisplaySize.x - DW) * 0.5f, (io.DisplaySize.y - DH) * 0.5f};
        ImVec2 dmx{dmn.x + DW, dmn.y + DH};
        fd->AddRectFilled(dmn, dmx, col(g_th.winbg), 14.f);
        fd->AddRect(dmn, dmx, col_a(g_th.accent, 0.7f), 14.f, 0, 1.5f);
        fd->AddRectFilled({dmn.x, dmn.y}, {dmx.x, dmn.y + 2.f}, col(g_th.accent));
        if (g_font_logo) ImGui::PushFont(g_font_logo);
        const char* t = "update available";
        ImVec2 tz = ImGui::CalcTextSize(t);
        fd->AddText({dmn.x + (DW - tz.x) * 0.5f, dmn.y + 22.f}, col(g_th.accent), t);
        if (g_font_logo) ImGui::PopFont();
        if (g_font_medium) ImGui::PushFont(g_font_medium);
        char line[128];
        std::snprintf(line, sizeof(line), "v%s -> v%s", current.c_str(), latest.c_str());
        ImVec2 lz = ImGui::CalcTextSize(line);
        fd->AddText({dmn.x + (DW - lz.x) * 0.5f, dmn.y + 72.f}, col(g_th.text), line);
        if (g_font_medium) ImGui::PopFont();
        const float BW = 190.f, BH = 42.f, BG = 14.f;
        float bx = dmn.x + (DW - BW * 2 - BG) * 0.5f, by = dmx.y - BH - 22.f;
        ImRect bDL({bx, by}, {bx + BW, by + BH});
        bool dh = hit(bDL);
        fd->AddRectFilled(bDL.Min, bDL.Max,
            blend(col(g_th.accent_soft), col(g_th.accent),
                  anim(ImGui::GetCurrentWindow()->GetID("uld"), dh ? 1.f : 0.f, 16.f)), 8.f);
        const char* dl_l = "Download";
        ImVec2 dz = ImGui::CalcTextSize(dl_l);
        fd->AddText({bDL.Min.x + (BW - dz.x) * 0.5f, bDL.Min.y + (BH - dz.y) * 0.5f}, col(g_th.ink), dl_l);
        if (dh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) updater::open_releases_page();
        ImRect bSK({bx + BW + BG, by}, {bx + BW + BG + BW, by + BH});
        bool sh = hit(bSK);
        fd->AddRectFilled(bSK.Min, bSK.Max,
            blend(col(g_th.child), col(g_th.child_hover),
                  anim(ImGui::GetCurrentWindow()->GetID("uls"), sh ? 1.f : 0.f, 16.f)), 8.f);
        fd->AddRect(bSK.Min, bSK.Max, col_a(g_th.stroke, 0.7f), 8.f, 0, 1.f);
        const char* sk = "Later";
        ImVec2 sz2 = ImGui::CalcTextSize(sk);
        fd->AddText({bSK.Min.x + (BW - sz2.x) * 0.5f, bSK.Min.y + (BH - sz2.y) * 0.5f}, col(g_th.text_dim), sk);
        if (sh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) updater::dismiss();
    }
    ImGui::End();
}

void boot_terminal() {
    ImGuiIO& io = ImGui::GetIO();
    static float t_start = -1.f;
    if (t_start < 0.f) t_start = uptime();
    float t = uptime() - t_start;

    const float W = g_th.win_w, H = g_th.win_h;
    ImVec2 wp{(io.DisplaySize.x - W) * 0.5f, (io.DisplaySize.y - H) * 0.5f};
    ImGui::SetNextWindowPos(wp, ImGuiCond_Always);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    bool open = true;
    ImGui::Begin("##bt", &open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    shell(wp, ws, dl, "booting..", 55.f);
    particle_backdrop(wp, ws, 0.55f);

    for (int y = 0; y < (int)ws.y; y += 3)
        dl->AddLine({wp.x, wp.y + (float)y}, {wp.x + ws.x, wp.y + (float)y},
                    IM_COL32(255, 255, 255, 6), 1.f);

    if (g_font_logo) {
        ImGui::PushFont(g_font_logo);
        const char* w1 = "welcome to UwU client";
        ImVec2 sz = ImGui::CalcTextSize(w1);
        dl->AddText({wp.x + (ws.x - sz.x) * 0.5f, wp.y + 90.f}, col(g_th.accent), w1);
        ImGui::PopFont();
    }
    if (g_font_medium) ImGui::PushFont(g_font_medium);
    const char* credit = "made by q3c on discord";
    ImVec2 cz = ImGui::CalcTextSize(credit);
    dl->AddText({wp.x + (ws.x - cz.x) * 0.5f, wp.y + 130.f}, col(g_th.text_dim), credit);
    if (g_font_medium) ImGui::PopFont();

    bool kernel = g_access_mode.load() == 1;
    bool fell   = g_kernel_fallback.load();
    struct L { const char* s; ImU32 c; float t; };
    std::vector<L> lines;
    if (kernel && fell) {
        lines.push_back({"[  ..  ] locating vulnerable driver ..",     col(g_th.text_dim), 0.20f});
        lines.push_back({"[ FAIL ] driver bundle not present",         col(g_th.bad),      0.90f});
        lines.push_back({"[ warn ] falling back to usermode",          col(g_th.warn),     1.50f});
    } else if (kernel) {
        lines.push_back({"[  ..  ] locating vulnerable driver ..",     col(g_th.text_dim), 0.20f});
        lines.push_back({"[  ok  ] mapping driver",                    col(g_th.ok),       0.90f});
    } else {
        lines.push_back({"[  ..  ] resolving Roblox process ..",       col(g_th.text_dim), 0.20f});
        lines.push_back({"[  ok  ] usermode access initialized",       col(g_th.ok),       0.90f});
    }
    static char attach_line[128]  = "[  ..  ] attaching to pid ..";
    static char scan_line[128]    = "[  ..  ] scanning offsets ..";
    if (mem::alive())
        std::snprintf(attach_line, sizeof(attach_line), "[  ok  ] attached to pid %lu",
                      (unsigned long)mem::g_pid);
    if (g_scan_ok.load())
        std::snprintf(scan_line, sizeof(scan_line), "[  ok  ] offset table loaded");
    lines.push_back({attach_line, mem::alive()      ? col(g_th.ok) : col(g_th.text_dim), 1.90f});
    lines.push_back({scan_line,   g_scan_ok.load() ? col(g_th.ok) : col(g_th.text_dim), 2.60f});
    if (g_scan_ok.load()) lines.push_back({"[  ok  ] attached!", col(g_th.ok), 3.20f});

    if (g_font_medium) ImGui::PushFont(g_font_medium);
    float by = wp.y + 200.f, px = wp.x + 80.f;
    for (auto& L : lines) {
        if (t < L.t) break;
        float a = std::min(1.f, (t - L.t) * 4.f);
        dl->AddText({px, by}, with_alpha(L.c, a), L.s);
        by += ImGui::GetFontSize() + 8.f;
    }
    float blink = 0.5f + 0.5f * std::sin(uptime() * 5.f);
    dl->AddRectFilled({px - 14.f, by - ImGui::GetFontSize() - 4.f},
                      {px - 8.f,  by - 4.f}, col_a(g_th.accent, 0.4f + 0.6f * blink));
    if (g_font_medium) ImGui::PopFont();

    if (g_font_small) ImGui::PushFont(g_font_small);
    bool ready = g_scan_ok.load();
    const char* hint = ready ? "press SPACE or click to select a game"
                             : mem::alive() ? "scanning offsets .." : "waiting for Roblox ..";
    ImVec2 hz = ImGui::CalcTextSize(hint);
    dl->AddText({wp.x + (ws.x - hz.x) * 0.5f, wp.y + ws.y - 26.f},
                ready ? col(g_th.accent) : col(g_th.text_dimmer), hint);
    if (g_font_small) ImGui::PopFont();

    bool advance = ready && (
        ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hit(ImRect(wp, wp + ws))));
    if (ready && t > 4.0f) advance = true;
    if (advance) { t_start = -1.f; g_screen.store(Screen::Launcher); }

    ImGui::End();
}

void launcher() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = g_th.win_w, H = g_th.win_h;
    ImVec2 wp{(io.DisplaySize.x - W) * 0.5f, (io.DisplaySize.y - H) * 0.5f};
    ImGui::SetNextWindowPos(wp, ImGuiCond_Always);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    bool open = true;
    ImGui::Begin("##lu", &open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    shell(wp, ws, dl, "select a game", 55.f);
    particle_backdrop(wp, ws, 0.55f);

    bool alive = mem::alive();
    float pulse = 0.5f + 0.5f * std::sin(uptime() * 1.4f);

    if (g_font_small) ImGui::PushFont(g_font_small);
    char pill[64];
    std::snprintf(pill, sizeof(pill), alive ? "attached · pid %lu" : "waiting for Roblox",
                  (unsigned long)mem::g_pid);
    ImU32 dot_col = alive ? col(g_th.ok) : col(g_th.warn);
    ImVec2 pz = ImGui::CalcTextSize(pill);
    float pw = pz.x + 34.f, ph = 22.f;
    ImRect pr({wp.x + ws.x - pw - 18.f, wp.y + 68.f},
              {wp.x + ws.x - 18.f,      wp.y + 68.f + ph});
    dl->AddRectFilled(pr.Min, pr.Max, col(g_th.scroll_bg), ph * 0.5f);
    dl->AddRect(pr.Min, pr.Max, col_a(g_th.accent, 0.35f), ph * 0.5f, 0, 1.f);
    ImVec2 dcp{pr.Min.x + 12.f, pr.Min.y + ph * 0.5f};
    dl->AddCircleFilled(dcp, 3.5f, dot_col);
    dl->AddCircle(dcp, 5.5f, with_alpha(dot_col, 0.35f + 0.35f * pulse), 0, 1.f);
    dl->AddText({pr.Min.x + 22.f, pr.Min.y + (ph - pz.y) * 0.5f}, col(g_th.text_dim), pill);
    if (g_font_small) ImGui::PopFont();

    static bool detected_once = false;
    static games::Id detected = games::Id::Universal;
    if (!detected_once && alive) {
        detected = games::detect_from_place();
        detected_once = true;
        if (detected != games::Id::Universal &&
            esp::cfg.selected_game == (int)games::Id::Universal)
            esp::cfg.selected_game = (int)detected;
    }

    struct GameCard { const char* name; const char* tag; };
    static const GameCard CARDS[] = {
        {"Universal",     "any game"},
        {"Arsenal",       "aim + weapon mods"},
        {"Flick",         "aim / ESP"},
        {"Da Hood",       "silent aim"},
        {"Bad Business",  "weapon mods"},
        {"Phantom Forces","subtle mods"},
        {"BIG Paintball", "weapon mods"},
        {"Counter Blox",  "weapon mods"},
        {"KAT",           "silent knife"},
        {"Assassin!",     "silent knife"},
        {"Strongest BG",  "movement preset"},
        {"Blox Fruits",   "long-range preset"},
        {"Rivals",        "viewport silent aim"},
        {"Knife Duels",   "aim / ESP preset"},
    };
    constexpr int N = (int)IM_ARRAYSIZE(CARDS);
    const int COLS = 4;
    const float GAP = 12.f, SIDE = 30.f;
    const float CW = (ws.x - SIDE * 2 - (COLS - 1) * GAP) / (float)COLS;
    const float CH = 78.f;
    const float TOP = wp.y + 108.f;

    for (int i = 0; i < N; i++) {
        int c = i % COLS, r = i / COLS;
        ImVec2 mn{wp.x + SIDE + c * (CW + GAP), TOP + r * (CH + GAP)};
        ImVec2 mx{mn.x + CW, mn.y + CH};
        ImRect rc(mn, mx);
        bool hov = alive && hit(rc);
        bool sel = (esp::cfg.selected_game == i);
        bool pr2 = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        ImGuiID id = ImGui::GetCurrentWindow()->GetID(CARDS[i].name);
        float thov = anim(id, hov ? 1.f : 0.f, 18.f);
        float tsel = anim(id ^ 0xBEEB, sel ? 1.f : 0.f, 20.f);
        dl->AddRectFilled(mn, mx, blend(col(g_th.child), col(g_th.child_hover), thov), 8.f);
        ImU32 bd = sel ? col(g_th.accent) : col_a(g_th.accent, 0.15f + thov * 0.5f);
        dl->AddRect(mn, mx, bd, 8.f, 0, sel ? 1.5f : 1.f);
        if (tsel > 0.02f)
            dl->AddRectFilled({mn.x, mn.y + 10.f}, {mn.x + 3.f, mx.y - 10.f},
                              col_a(g_th.accent, tsel));
        if (g_font_medium) ImGui::PushFont(g_font_medium);
        dl->AddText({mn.x + 14.f, mn.y + 12.f},
                    sel ? col(g_th.text) : blend(col(g_th.text_dim), col(g_th.text), thov),
                    CARDS[i].name);
        if (g_font_medium) ImGui::PopFont();
        if (g_font_small) ImGui::PushFont(g_font_small);
        dl->AddText({mn.x + 14.f, mn.y + 36.f}, col(g_th.text_dimmer), CARDS[i].tag);
        if (g_font_small) ImGui::PopFont();
        if (sel) {
            ImVec2 kc{mx.x - 14.f, mx.y - 14.f};
            dl->AddCircleFilled(kc, 6.f, col(g_th.accent));
            dl->AddLine({kc.x - 2.5f, kc.y}, {kc.x - 0.5f, kc.y + 2.5f}, col(g_th.ink), 1.5f);
            dl->AddLine({kc.x - 0.5f, kc.y + 2.5f}, {kc.x + 3.f, kc.y - 2.f}, col(g_th.ink), 1.5f);
        }
        if (detected_once && detected != games::Id::Universal && i == (int)detected && !sel) {
            if (g_font_small) ImGui::PushFont(g_font_small);
            const char* det = "detected";
            ImVec2 dsz = ImGui::CalcTextSize(det);
            float dw = dsz.x + 12.f, dh = 14.f;
            ImVec2 dmin{mx.x - dw - 8.f, mn.y + 8.f};
            ImVec2 dmax{dmin.x + dw, dmin.y + dh};
            dl->AddRectFilled(dmin, dmax, col(g_th.scroll_bg), dh * 0.5f);
            dl->AddRect(dmin, dmax, col_a(g_th.accent, 0.5f), dh * 0.5f, 0, 1.f);
            dl->AddText({dmin.x + 6.f, dmin.y + (dh - dsz.y) * 0.5f}, col(g_th.accent), det);
            if (g_font_small) ImGui::PopFont();
        }
        if (pr2) {
            esp::cfg.selected_game = i;
            g_injected.store(true);
            g_visible.store(false);
            g_screen.store(Screen::Menu);
            games::lock_from_cfg();
            char msg[64];
            std::snprintf(msg, sizeof(msg), "UwUClient ready - %s - RShift to open",
                          games::name_of(games::active()));
            esp::notify(msg);
        }
    }

    if (g_font_small) ImGui::PushFont(g_font_small);
    const char* credit = "made by q3c on discord  ·  RShift to open once in-game";
    ImVec2 cz = ImGui::CalcTextSize(credit);
    dl->AddText({wp.x + (ws.x - cz.x) * 0.5f, wp.y + ws.y - 26.f}, col(g_th.text_dimmer), credit);
    if (g_font_small) ImGui::PopFont();

    ImGui::End();
}

}

}

namespace menu {

void init() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig cfg; cfg.FontDataOwnedByAtlas = false;
    ImFontConfig cfg_bold = cfg;

    const ImWchar* cyr = io.Fonts->GetGlyphRangesCyrillic();

    io.Fonts->AddFontFromMemoryTTF(
        (void*)font_blob::NunitoMedium, (int)sizeof(font_blob::NunitoMedium), 21.f, &cfg, cyr);
    g_font_medium = io.Fonts->AddFontFromMemoryTTF(
        (void*)font_blob::NunitoMedium, (int)sizeof(font_blob::NunitoMedium), 18.f, &cfg, cyr);
    g_font_regular = g_font_medium;
    g_font_small = io.Fonts->AddFontFromMemoryTTF(
        (void*)font_blob::NunitoMedium, (int)sizeof(font_blob::NunitoMedium), 15.f, &cfg, cyr);
    g_font_icon = io.Fonts->AddFontFromMemoryTTF(
        (void*)font_blob::icomoon, (int)sizeof(font_blob::icomoon), 18.f, &cfg, cyr);
    g_font_icon_lg = io.Fonts->AddFontFromMemoryTTF(
        (void*)font_blob::icomoon, (int)sizeof(font_blob::icomoon), 23.f, &cfg, cyr);
    g_font_logo = io.Fonts->AddFontFromMemoryTTF(
        (void*)font_blob::NunitoMedium, (int)sizeof(font_blob::NunitoMedium), 25.f, &cfg_bold, cyr);
    g_font_arrow = io.Fonts->AddFontFromMemoryTTF(
        (void*)font_blob::arrowicon, (int)sizeof(font_blob::arrowicon), 18.f, &cfg, cyr);

    if (!g_font_medium) io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui_ImplDX11_InvalidateDeviceObjects();

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.FramePadding      = ImVec2(10, 10);
    s.ItemSpacing       = ImVec2(10, 5);
    s.FrameRounding     = 5.f;
    s.WindowRounding    = g_th.win_round;
    s.WindowBorderSize  = 0.f;
    s.PopupBorderSize   = 1.3f;
    s.WindowPadding     = ImVec2(0, 0);
    s.ChildBorderSize   = 0.0001f;
    s.PopupRounding     = 5.f;
    s.ScrollbarSize     = 4.f;
    s.ScrollbarRounding = 10.f;
    s.Colors[ImGuiCol_WindowBg]              = ImColorToImVec4(g_th.winbg);
    s.Colors[ImGuiCol_ChildBg]               = ImColorToImVec4(g_th.child);
    s.Colors[ImGuiCol_PopupBg]               = ImColorToImVec4(g_th.sidebar);
    s.Colors[ImGuiCol_Border]                = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_Text]                  = ImColorToImVec4(g_th.text);
    s.Colors[ImGuiCol_TextDisabled]          = ImColorToImVec4(g_th.text_dimmer);
    s.Colors[ImGuiCol_FrameBg]               = ImColorToImVec4(g_th.scroll_bg);
    s.Colors[ImGuiCol_FrameBgHovered]        = ImColorToImVec4(g_th.child_hover);
    s.Colors[ImGuiCol_FrameBgActive]         = ImColorToImVec4(g_th.child_hover);
    s.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_ScrollbarGrab]         = ImColorToImVec4(g_th.stroke);
    s.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImColorToImVec4(g_th.accent_soft);
    s.Colors[ImGuiCol_ScrollbarGrabActive]   = ImColorToImVec4(g_th.accent);
    s.Colors[ImGuiCol_CheckMark]             = ImColorToImVec4(g_th.accent);
    s.Colors[ImGuiCol_SliderGrab]            = ImColorToImVec4(g_th.accent);
    s.Colors[ImGuiCol_SliderGrabActive]      = ImColorToImVec4(g_th.accent_soft);
    s.Colors[ImGuiCol_Button]                = ImColorToImVec4(g_th.child_hover);
    s.Colors[ImGuiCol_ButtonHovered]         = ImColorToImVec4(g_th.accent_soft);
    s.Colors[ImGuiCol_ButtonActive]          = ImColorToImVec4(g_th.accent);
    s.Colors[ImGuiCol_Header]                = ImColorToImVec4(g_th.child_hover);
    s.Colors[ImGuiCol_HeaderHovered]         = ImColorToImVec4(g_th.accent_soft);
    s.Colors[ImGuiCol_HeaderActive]          = ImColorToImVec4(g_th.accent);
    s.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_TextSelectedBg]        = ImColorToImVec4(g_th.accent_soft);
}

void set_visible(bool v) { if (!g_injected.load()) return; g_visible.store(v); }
bool is_visible()         { return g_visible.load(); }
bool is_injected()        { return g_injected.load(); }
void set_scan_ok(bool ok) { g_scan_ok.store(ok); }

void render(const std::vector<PlayerInfo>* players, uintptr_t render_view) {
    (void)render_view;

    Screen sc = g_screen.load();
    if (sc == Screen::ModePicker)   { screens::mode_picker();   return; }
    if (sc == Screen::BootTerminal) { screens::boot_terminal(); return; }
    if (sc == Screen::Launcher)     { screens::launcher();      return; }
    if (!g_visible.load()) return;

    g_players_snapshot = players;

    ImGuiIO& io = ImGui::GetIO();
    const float W = g_th.win_w, H = g_th.win_h;

    ImGui::SetNextWindowPos({(io.DisplaySize.x - W) * 0.5f, (io.DisplaySize.y - H) * 0.5f},
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    bool open = true;
    ImGui::Begin("UwUClient", &open,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    drop_shadow(ImGui::GetBackgroundDrawList(), wp, wp + ws, g_th.win_round, 40.f, 0.85f);
    dl->AddRectFilled(wp, wp + ws, col(g_th.winbg), g_th.win_round);

    particle_backdrop(wp, ws, 0.55f);

    ImVec2 sb_min{wp.x, wp.y + g_th.topbar_h};
    ImVec2 sb_max{wp.x + g_th.sidebar_w, wp.y + ws.y};
    dl->AddRectFilled(sb_min, sb_max, col(g_th.sidebar),
                      g_th.win_round, ImDrawFlags_RoundCornersBottomLeft);

    dl->AddRectFilled(wp, {wp.x + ws.x, wp.y + g_th.topbar_h - 3.f},
                      col(g_th.topbar), g_th.win_round, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled({wp.x, wp.y + g_th.topbar_h - 3.f}, {wp.x + ws.x, wp.y + g_th.topbar_h},
                      col(g_th.accent));

    if (g_font_logo) {
        ImGui::PushFont(g_font_logo);
        const char* title = "UwUClient";
        ImVec2 tp = center_text(wp, {wp.x + ws.x, wp.y + g_th.topbar_h - 3.f}, title);
        dl->AddText(tp, col(g_th.text), title);
        ImGui::PopFont();
    }
    if (g_font_small) ImGui::PushFont(g_font_small);
    char pill[64];
    if (mem::alive())
        std::snprintf(pill, sizeof(pill), "%s  ·  pid %lu  ·  %.0f fps",
                      games::name_of(games::active()),
                      (unsigned long)mem::g_pid, io.Framerate);
    else
        std::snprintf(pill, sizeof(pill), "detached  ·  %.0f fps", io.Framerate);
    ImVec2 psz = ImGui::CalcTextSize(pill);
    float pw = psz.x + 24.f, ph = 20.f;
    ImRect prc({wp.x + ws.x - pw - 12.f, wp.y + (g_th.topbar_h - 3.f - ph) * 0.5f},
               {wp.x + ws.x - 12.f,      wp.y + (g_th.topbar_h - 3.f + ph) * 0.5f});
    dl->AddRectFilled(prc.Min, prc.Max, col(g_th.scroll_bg), ph * 0.5f);
    dl->AddRect(prc.Min, prc.Max, col_a(g_th.accent, 0.35f), ph * 0.5f, 0, 1.f);
    dl->AddText({prc.Min.x + 12.f, prc.Min.y + (ph - psz.y) * 0.5f}, col(g_th.text_dim), pill);
    if (g_font_small) ImGui::PopFont();

    float drag_x = wp.x + 12.f;
    float drag_w = (prc.Min.x - 8.f) - drag_x;
    if (drag_w > 40.f) {
        ImGui::SetCursorScreenPos({drag_x, wp.y});
        ImGui::InvisibleButton("##tdrag", {drag_w, g_th.topbar_h - 3.f});
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 md = io.MouseDelta;
            ImGui::SetWindowPos({wp.x + md.x, wp.y + md.y});
        }
    }

    ImGui::SetCursorPos({0.f, g_th.topbar_h + 12.f});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {0, 2});
    ImGui::BeginChild("##rail", {g_th.sidebar_w, ws.y - g_th.topbar_h - 12.f}, false,
        ImGuiWindowFlags_NoBackground);

    for (int i = 0; i < TAB_COUNT; i++) {
        if (g_tabs[i].group) ex::GroupHeader(g_tabs[i].group);
        ex::Tab(g_tabs[i].label, g_tabs[i].icon, &g_active_tab, i);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::SetCursorPos({g_th.sidebar_w + 10.f, g_th.topbar_h + 12.f});
    ImGui::BeginGroup();
    ImVec2 fs{g_th.frame_w, g_th.frame_h};
    if (g_active_tab >= 0 && g_active_tab < TAB_COUNT && g_tabs[g_active_tab].draw)
        g_tabs[g_active_tab].draw(fs);
    ImGui::EndGroup();

    ImGui::End();

    poll_keybind_listen();

    if (!open) g_visible.store(false);
}

}

