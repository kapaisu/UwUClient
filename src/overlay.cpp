#include "../include/overlay.hpp"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dwmapi.h>
#include <stdexcept>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace overlay {

HWND                   g_hwnd       = nullptr;
ID3D11Device*          g_device     = nullptr;
ID3D11DeviceContext*   g_context    = nullptr;
IDXGISwapChain*        g_swap_chain = nullptr;
ID3D11RenderTargetView* g_rtv       = nullptr;

static bool            g_running    = true;
static HINSTANCE       g_inst       = nullptr;
static const char*     CLASS_NAME   = "UwUClient_overlay";

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    if (msg == WM_DESTROY) { g_running = false; return 0; }
    if (msg == WM_SIZE && g_swap_chain) {
        if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
        g_swap_chain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        ID3D11Texture2D* buf = nullptr;
        g_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&buf);
        if (buf) {
            g_device->CreateRenderTargetView(buf, nullptr, &g_rtv);
            buf->Release();
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static bool create_dx11(int w, int h) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount        = 2;
    sd.BufferDesc.Width   = w;
    sd.BufferDesc.Height  = h;
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = g_hwnd;
    sd.SampleDesc.Count   = 1;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &g_swap_chain, &g_device, &fl, &g_context);
    if (FAILED(hr)) return false;

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable           = TRUE;
    bd.RenderTarget[0].SrcBlend             = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend            = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp              = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha        = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha       = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha         = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask= D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11BlendState* bs = nullptr;
    g_device->CreateBlendState(&bd, &bs);
    if (bs) { g_context->OMSetBlendState(bs, nullptr, 0xffffffff); bs->Release(); }

    ID3D11Texture2D* buf = nullptr;
    g_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&buf);
    if (!buf) return false;
    g_device->CreateRenderTargetView(buf, nullptr, &g_rtv);
    buf->Release();

    return g_rtv != nullptr;
}

bool init(HWND target_hwnd, HINSTANCE hInst) {
    g_inst = hInst;

    RECT rc{};
    GetWindowRect(target_hwnd, &rc);
    int x = rc.left, y = rc.top;
    int w = rc.right  - rc.left;
    int h = rc.bottom - rc.top;

    WNDCLASSEXA wc{sizeof(wc)};
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursorA(nullptr, IDC_ARROW);
    RegisterClassExA(&wc);

    g_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        CLASS_NAME, "UwUClient",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return false;

    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(g_hwnd, &margins);
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);

    if (!create_dx11(w, h)) return false;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f))
        io.Fonts->AddFontDefault();

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding     = 7.f;
    s.FrameRounding      = 4.f;
    s.GrabRounding       = 4.f;
    s.WindowBorderSize   = 1.f;
    s.Colors[ImGuiCol_WindowBg]       = {0.07f, 0.07f, 0.09f, 0.92f};
    s.Colors[ImGuiCol_TitleBg]        = {0.04f, 0.04f, 0.06f, 1.00f};
    s.Colors[ImGuiCol_TitleBgActive]  = {0.05f, 0.38f, 0.22f, 1.00f};
    s.Colors[ImGuiCol_CheckMark]      = {0.0f,  1.0f,  0.4f,  1.0f};
    s.Colors[ImGuiCol_SliderGrab]     = {0.0f,  0.8f,  0.4f,  1.0f};
    s.Colors[ImGuiCol_Button]         = {0.08f, 0.33f, 0.18f, 1.0f};
    s.Colors[ImGuiCol_ButtonHovered]  = {0.10f, 0.50f, 0.28f, 1.0f};
    s.Colors[ImGuiCol_FrameBg]        = {0.12f, 0.12f, 0.16f, 1.0f};
    s.Colors[ImGuiCol_Header]         = {0.05f, 0.38f, 0.22f, 0.6f};
    s.Colors[ImGuiCol_HeaderHovered]  = {0.05f, 0.38f, 0.22f, 0.9f};
    s.Colors[ImGuiCol_SeparatorActive]= {0.0f,  0.9f,  0.4f,  1.0f};

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    return true;
}

void sync_to_target(HWND target_hwnd) {
    if (!target_hwnd || !g_hwnd) return;
    RECT rc{};
    GetWindowRect(target_hwnd, &rc);
    int x = rc.left, y = rc.top;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

bool is_running() { return g_running; }
void quit()       { g_running = false; }

void poll_messages() {
    MSG msg{};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
        if (msg.message == WM_QUIT) g_running = false;
    }
}

void begin_frame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const float clear[4] = {0.f, 0.f, 0.f, 0.f};
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);
}

void end_frame() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swap_chain->Present(1, 0);
}

void set_passthrough(bool on) {
    if (!g_hwnd) return;
    LONG ex = GetWindowLongA(g_hwnd, GWL_EXSTYLE);
    if (on) {

        ex |=  WS_EX_TRANSPARENT;
        ex |=  WS_EX_NOACTIVATE;
    } else {

        ex &= ~WS_EX_TRANSPARENT;
        ex &= ~WS_EX_NOACTIVATE;
    }
    SetWindowLongA(g_hwnd, GWL_EXSTYLE, ex);
    if (!on) {

        SetForegroundWindow(g_hwnd);
        SetFocus(g_hwnd);
    }
}

void set_streamproof(bool on) {
    if (g_hwnd) SetWindowDisplayAffinity(g_hwnd, on ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}

}
