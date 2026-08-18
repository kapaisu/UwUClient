#pragma once
#include <d3d11.h>
#include <imgui.h>
#include <string>

namespace bgimage {

    bool load(ID3D11Device* dev, const std::wstring& path);
    void shutdown();

    ImTextureID srv();
    int         width();
    int         height();


    void cover_uv(float rect_w, float rect_h, ImVec2& uv0, ImVec2& uv1);

}
