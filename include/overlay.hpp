#pragma once
#include <Windows.h>
#include <d3d11.h>

namespace overlay {

    bool init(HWND target_hwnd, HINSTANCE hInst);

    void sync_to_target(HWND target_hwnd);

    bool is_running();

    void quit();

    void poll_messages();

    void begin_frame();

    void end_frame();

    void set_passthrough(bool on);

    void set_streamproof(bool on);

    extern HWND                  g_hwnd;
    extern ID3D11Device*         g_device;
    extern ID3D11DeviceContext*  g_context;
    extern IDXGISwapChain*       g_swap_chain;
    extern ID3D11RenderTargetView* g_rtv;

}
