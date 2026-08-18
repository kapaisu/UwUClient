#include <windows.h>
#include <d3d11.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "menu.h"
#include "font_awesome.h"
#include <string>
#include <iostream>
#include "font.h"
#include "defenitions.h"

#pragma comment(lib, "d3d11.lib")

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct DX11Objects {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* main_render_target_view = nullptr;
};

void CreateRenderTarget(DX11Objects* dx) {
    ID3D11Texture2D* pBackBuffer = nullptr;
    dx->swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    dx->device->CreateRenderTargetView(pBackBuffer, NULL, &dx->main_render_target_view);
    pBackBuffer->Release();
}

void CleanupRenderTarget(DX11Objects* dx) {
    if (dx->main_render_target_view) {
        dx->main_render_target_view->Release();
        dx->main_render_target_view = nullptr;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    if (msg == WM_SIZE) {
        if (wParam != SIZE_MINIMIZED) {
            DX11Objects* dx = (DX11Objects*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
            if (dx && dx->swap_chain) {
                CleanupRenderTarget(dx);
                dx->swap_chain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget(dx);
            }
        }
        return 0;
    }
    if (msg == WM_SYSCOMMAND) {
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int main()
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"ImGui Example", NULL };
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindow(wc.lpszClassName, L"ImGui DX11 Menu", WS_OVERLAPPEDWINDOW, 100, 100, 1920, 1080, NULL, NULL, wc.hInstance, NULL);

    D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    DX11Objects* dx = new DX11Objects();
    D3D_FEATURE_LEVEL createdFeatureLevel;
    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &dx->swap_chain, &dx->device, &createdFeatureLevel, &dx->device_context);
    CreateRenderTarget(dx);

    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)dx);

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.FrameBorderSize = 0.0f;
    style.ScrollbarRounding = 2.0f;

    ImVec4* colors = style.Colors;

    // Text & backgrounds
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.97f, 1.0f);              // near-white text
    colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.06f, 1.0f);          // very dark grey
  

    colors[ImGuiCol_ChildBg] = ImVec4(0.04f, 0.04f, 0.05f, 0.6f);


    colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.08f, 0.95f);          // slightly lighter for popups

    // Frames
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);

    // Checkmarks & sliders (blue accent)
    colors[ImGuiCol_CheckMark] = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);

    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);        // blue on hover
    colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.5f, 0.8f, 1.0f);        // darker blue when active

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.25f, 0.5f, 0.8f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.08f, 0.09f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.18f, 0.2f, 1.0f);

    // Separators & borders
    colors[ImGuiCol_Separator] = ImVec4(0.4f, 0.8f, 1.0f, 0.8f); 
    colors[ImGuiCol_Border] = ImVec4(0.4f, 0.8f, 1.0f, 0.4f); 


    // Title bars
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 0.5f, 0.8f, 1.0f);       // blue accent








    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(dx->device, dx->device_context);




    // FONT
    ImFontConfig font;
    font.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)rawData, sizeof(rawData), 14.5f, &font);





    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    iconConfig.OversampleH = 3;
    iconConfig.OversampleV = 3;
    iconConfig.FontDataOwnedByAtlas = true;
    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(fa6_solid_compressed_data, fa6_solid_compressed_size, 15.f, &iconConfig, icon_ranges);



   
   // fontBold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", 26.0f);
    // Fonts
    io.Fonts->Build();




    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();


        /////1font

       // ImFontConfig font;
      //  font.FontDataOwnedByAtlas = false;

    //    io.Fonts->AddFontFromMemoryTTF((void*)rawData, sizeof(rawData), 14.5f, &font);


    

        ///2 font icons
     //   ImFontConfig iconConfig;
     //   iconConfig.MergeMode = true;
    //    iconConfig.PixelSnapH = true;
    ///    iconConfig.OversampleH = 3;
    //    iconConfig.OversampleV = 3;
    //    iconConfig.FontDataOwnedByAtlas = true;


    //    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(fa6_solid_compressed_data, fa6_solid_compressed_size, 15.f, &iconConfig, icon_ranges);

   



        RenderMenu();

        ImGui::Render();
        const float clear_color[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        dx->device_context->OMSetRenderTargets(1, &dx->main_render_target_view, NULL);
        dx->device_context->ClearRenderTargetView(dx->main_render_target_view, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        dx->swap_chain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupRenderTarget(dx);
    if (dx->swap_chain) dx->swap_chain->Release();
    if (dx->device_context) dx->device_context->Release();
    if (dx->device) dx->device->Release();
    delete dx;

    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}

