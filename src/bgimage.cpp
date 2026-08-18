#include "../include/bgimage.hpp"

#include <Windows.h>
#include <wincodec.h>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace bgimage {

    static ID3D11ShaderResourceView* g_srv = nullptr;
    static ID3D11Texture2D*          g_tex = nullptr;
    static int                       g_w = 0, g_h = 0;

    bool load(ID3D11Device* dev, const std::wstring& path) {
        if (!dev) return false;
        shutdown();


        HRESULT hr_co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        (void)hr_co;

        IWICImagingFactory* factory = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory) return false;

        IWICBitmapDecoder* decoder = nullptr;
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
                                                GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad,
                                                &decoder);
        if (FAILED(hr) || !decoder) { factory->Release(); return false; }

        IWICBitmapFrameDecode* frame = nullptr;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr) || !frame) { decoder->Release(); factory->Release(); return false; }

        IWICFormatConverter* conv = nullptr;
        hr = factory->CreateFormatConverter(&conv);
        if (FAILED(hr) || !conv) {
            frame->Release(); decoder->Release(); factory->Release();
            return false;
        }

        hr = conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                              WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            conv->Release(); frame->Release(); decoder->Release(); factory->Release();
            return false;
        }

        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        if (w == 0 || h == 0) {
            conv->Release(); frame->Release(); decoder->Release(); factory->Release();
            return false;
        }

        const UINT stride = w * 4;
        std::vector<BYTE> pixels(size_t(stride) * h);
        hr = conv->CopyPixels(nullptr, stride, (UINT)pixels.size(), pixels.data());

        conv->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        if (FAILED(hr)) return false;

        D3D11_TEXTURE2D_DESC td{};
        td.Width            = w;
        td.Height           = h;
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_IMMUTABLE;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem     = pixels.data();
        sd.SysMemPitch = stride;

        hr = dev->CreateTexture2D(&td, &sd, &g_tex);
        if (FAILED(hr) || !g_tex) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format              = td.Format;
        sv.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        hr = dev->CreateShaderResourceView(g_tex, &sv, &g_srv);
        if (FAILED(hr) || !g_srv) { shutdown(); return false; }

        g_w = (int)w;
        g_h = (int)h;
        return true;
    }

    void shutdown() {
        if (g_srv) { g_srv->Release(); g_srv = nullptr; }
        if (g_tex) { g_tex->Release(); g_tex = nullptr; }
        g_w = g_h = 0;
    }

    ImTextureID srv() { return (ImTextureID)(uintptr_t)g_srv; }
    int width()  { return g_w; }
    int height() { return g_h; }

    void cover_uv(float rect_w, float rect_h, ImVec2& uv0, ImVec2& uv1) {
        if (g_w <= 0 || g_h <= 0 || rect_w <= 0.f || rect_h <= 0.f) {
            uv0 = {0.f, 0.f};
            uv1 = {1.f, 1.f};
            return;
        }
        float img_ar = (float)g_w / (float)g_h;
        float rct_ar = rect_w / rect_h;
        if (img_ar > rct_ar) {

            float visible_w_units = rct_ar / img_ar;
            float pad = (1.f - visible_w_units) * 0.5f;
            uv0 = {pad, 0.f};
            uv1 = {1.f - pad, 1.f};
        } else {

            float visible_h_units = img_ar / rct_ar;
            float pad = (1.f - visible_h_units) * 0.5f;
            uv0 = {0.f, pad};
            uv1 = {1.f, 1.f - pad};
        }
    }

}
