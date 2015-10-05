/************************************************************************************
Filename    :   Win32_RoomTiny_Main.cpp
Content     :   First-person view test application for Oculus Rift
Created     :   11th May 2015
Authors     :   Tom Heath
Copyright   :   Copyright 2015 Oculus, Inc. All Rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*************************************************************************************/
/// This is an entry-level sample, showing a minimal VR sample,
/// in a simple environment.  Use WASD keys to move around, and cursor keys.
/// Dismiss the health and safety warning by tapping the headset,
/// or pressing any key.
/// It runs with DirectX11.

#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <vector>

#include <comdef.h>
#include <comip.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <OVR_CAPI_D3D.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

using namespace DirectX;

#ifndef VALIDATE
#define VALIDATE(x, msg)                                                  \
    if (!(x)) {                                                           \
        MessageBoxA(NULL, (msg), "OculusRoomTiny", MB_ICONERROR | MB_OK); \
        exit(-1);                                                         \
    }
#endif

#define COM_SMARTPTR_TYPEDEF(x) _COM_SMARTPTR_TYPEDEF(x, __uuidof(x))
COM_SMARTPTR_TYPEDEF(ID3D11BlendState);
COM_SMARTPTR_TYPEDEF(ID3D11Buffer);
COM_SMARTPTR_TYPEDEF(ID3D11DepthStencilState);
COM_SMARTPTR_TYPEDEF(ID3D11DepthStencilView);
COM_SMARTPTR_TYPEDEF(ID3D11Device);
COM_SMARTPTR_TYPEDEF(ID3D11DeviceContext);
COM_SMARTPTR_TYPEDEF(ID3D11InputLayout);
COM_SMARTPTR_TYPEDEF(ID3D11PixelShader);
COM_SMARTPTR_TYPEDEF(ID3D11RasterizerState);
COM_SMARTPTR_TYPEDEF(ID3D11RenderTargetView);
COM_SMARTPTR_TYPEDEF(ID3D11SamplerState);
COM_SMARTPTR_TYPEDEF(ID3D11ShaderResourceView);
COM_SMARTPTR_TYPEDEF(ID3D11Texture2D);
COM_SMARTPTR_TYPEDEF(ID3D11VertexShader);
COM_SMARTPTR_TYPEDEF(ID3DBlob);
COM_SMARTPTR_TYPEDEF(IDXGIAdapter);
COM_SMARTPTR_TYPEDEF(IDXGIDevice1);
COM_SMARTPTR_TYPEDEF(IDXGIFactory);
COM_SMARTPTR_TYPEDEF(IDXGISwapChain);

struct DepthBuffer {
    ID3D11DepthStencilViewPtr TexDsv;

    DepthBuffer(ID3D11Device* Device, ovrSizei size) {
        CD3D11_TEXTURE2D_DESC dsDesc(DXGI_FORMAT_D24_UNORM_S8_UINT, size.w, size.h);
        dsDesc.MipLevels = 1;
        dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ID3D11Texture2DPtr Tex;
        Device->CreateTexture2D(&dsDesc, NULL, &Tex);
        Device->CreateDepthStencilView(Tex, NULL, &TexDsv);
    }
};

struct Window {
    HWND Hwnd = nullptr;
    bool Running = false;
    bool Keys[256] = {};

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
        auto p = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, 0));
        switch (Msg) {
            case WM_KEYDOWN:
                p->Keys[wParam] = true;
                break;
            case WM_KEYUP:
                p->Keys[wParam] = false;
                break;
            case WM_DESTROY:
                p->Running = false;
                break;
            default:
                return DefWindowProcW(hWnd, Msg, wParam, lParam);
        }
        if ((p->Keys['Q'] && p->Keys[VK_CONTROL]) || p->Keys[VK_ESCAPE]) {
            p->Running = false;
        }
        return 0;
    }

    Window(HINSTANCE hinst, LPCWSTR title) : Running{true} {
        WNDCLASSW wc{};
        wc.lpszClassName = L"App";
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.cbWndExtra = sizeof(this);
        RegisterClassW(&wc);

        // adjust the window size and show at InitDevice time
        Hwnd =
            CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, 0, 0, hinst, 0);

        SetWindowLongPtr(Hwnd, 0, reinterpret_cast<LONG_PTR>(this));
    }

    auto HandleMessages() const {
        MSG msg{};
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return Running;
    }

    void Run(bool (*MainLoop)(const Window& window, bool retryCreate)) const {
        // false => just fail on any error
        VALIDATE(MainLoop(*this, false), "Oculus Rift not detected.");
        while (HandleMessages()) {
            // true => we'll attempt to retry for ovrError_DisplayLost
            if (!MainLoop(*this, true)) break;
            // Sleep a bit before retrying to reduce CPU load while the HMD is disconnected
            Sleep(10);
        }
    }
};

struct DirectX11 {
    int WinSizeW = 0;
    int WinSizeH = 0;
    ID3D11DevicePtr Device;
    ID3D11DeviceContextPtr Context;
    IDXGISwapChainPtr SwapChain;
    ID3D11Texture2DPtr BackBuffer;
    ID3D11VertexShaderPtr D3DVert;
    ID3D11PixelShaderPtr D3DPix;
    ID3D11InputLayoutPtr InputLayout;
    ID3D11SamplerStatePtr SamplerState;
    ID3D11BufferPtr ConstantBuffer;

    DirectX11(HWND window, int vpW, int vpH, const LUID* pLuid);

    void SetAndClearRenderTarget(ID3D11RenderTargetView* rendertarget,
                                 DepthBuffer* depthbuffer) const {
        Context->OMSetRenderTargets(1, &rendertarget, depthbuffer->TexDsv);
        Context->ClearRenderTargetView(rendertarget, std::begin({0.0f, 0.0f, 0.0f, 0.0f}));
        Context->ClearDepthStencilView(depthbuffer->TexDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                       1, 0);
    }

    void SetViewport(const ovrRecti& vp) const {
        D3D11_VIEWPORT D3Dvp{
            float(vp.Pos.x), float(vp.Pos.y), float(vp.Size.w), float(vp.Size.h), 0.0f, 1.0f};
        Context->RSSetViewports(1, &D3Dvp);
    }
};

enum class TextureFill { AUTO_WHITE, AUTO_WALL, AUTO_FLOOR, AUTO_CEILING };

auto createTexture(ID3D11Device* device, ID3D11DeviceContext* context, TextureFill texFill) {
    const int w{256}, h{256}, mips{8};
    CD3D11_TEXTURE2D_DESC dsDesc(DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, mips,
                                 D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    dsDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ID3D11Texture2DPtr tex;
    device->CreateTexture2D(&dsDesc, nullptr, &tex);
    ID3D11ShaderResourceViewPtr texSrv;
    device->CreateShaderResourceView(tex, nullptr, &texSrv);

    // Fill texture with requested pattern
    std::vector<DWORD> pix(w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto& curr = pix[y * w + x];
            switch (texFill) {
                case (TextureFill::AUTO_WALL):
                    curr =
                        (((y / 4 & 15) == 0) ||
                         (((x / 4 & 15) == 0) && ((((x / 4 & 31) == 0) ^ ((y / 4 >> 4) & 1)) == 0)))
                            ? 0xff3c3c3c
                            : 0xffb4b4b4;
                    break;
                case (TextureFill::AUTO_FLOOR):
                    curr = (((x >> 7) ^ (y >> 7)) & 1) ? 0xffb4b4b4 : 0xff505050;
                    break;
                case (TextureFill::AUTO_CEILING):
                    curr = (x / 4 == 0 || y / 4 == 0) ? 0xff505050 : 0xffb4b4b4;
                    break;
                case (TextureFill::AUTO_WHITE):
                    curr = 0xffffffff;
                    break;
                default:
                    curr = 0xffffffff;
                    break;
            }
        }
    context->UpdateSubresource(tex, 0, nullptr, pix.data(), w * 4, 0);
    context->GenerateMips(texSrv);

    return texSrv;
}

struct Vertex {
    XMFLOAT3 Pos;
    DWORD C;
    float U, V;
};

struct TriangleSet {
    std::vector<Vertex> Vertices;
    std::vector<short> Indices;

    void AddBox(float x1, float y1, float z1, float x2, float y2, float z2, DWORD c) {
        auto addQuad = [this](Vertex v0, Vertex v1, Vertex v2, Vertex v3) {
            auto addTriangle = [this](const std::initializer_list<Vertex>& vs) {
                for (const auto& v : vs) {
                    Indices.push_back(static_cast<short>(size(Vertices)));
                    Vertices.push_back(v);
                }
            };

            addTriangle({v0, v1, v2});
            addTriangle({v3, v2, v1});
        };

        auto modifyColor = [](DWORD c, XMFLOAT3 pos) {
            const auto v = XMLoadFloat3(&pos);
            auto length = [](const auto& v) { return XMVectorGetX(XMVector3Length(v)); };
            const auto dist1 = length(XMVectorAdd(v, XMVectorSet(2.0f, -4.0f, 2.0f, 0.0f)));
            const auto dist2 = length(XMVectorAdd(v, XMVectorSet(-3.0f, -4.0f, 3.0f, 0.0f)));
            const auto dist3 = length(XMVectorAdd(v, XMVectorSet(4.0f, -3.0f, -25.0f, 0.0f)));
            int bri = rand() % 160;
            float r = ((c >> 16) & 0xff) *
                      (bri + 192.0f * (0.65f + 8 / dist1 + 1 / dist2 + 4 / dist3)) / 255.0f;
            float g = ((c >> 8) & 0xff) *
                      (bri + 192.0f * (0.65f + 8 / dist1 + 1 / dist2 + 4 / dist3)) / 255.0f;
            float b = ((c >> 0) & 0xff) *
                      (bri + 192.0f * (0.65f + 8 / dist1 + 1 / dist2 + 4 / dist3)) / 255.0f;
            return ((c & 0xff000000) + ((r > 255 ? 255 : (DWORD)r) << 16) +
                    ((g > 255 ? 255 : (DWORD)g) << 8) + (b > 255 ? 255 : (DWORD)b));
        };

        addQuad({{x1, y2, z1}, modifyColor(c, {x1, y2, z1}), z1, x1},
                {{x2, y2, z1}, modifyColor(c, {x2, y2, z1}), z1, x2},
                {{x1, y2, z2}, modifyColor(c, {x1, y2, z2}), z2, x1},
                {{x2, y2, z2}, modifyColor(c, {x2, y2, z2}), z2, x2});
        addQuad({{x2, y1, z1}, modifyColor(c, {x2, y1, z1}), z1, x2},
                {{x1, y1, z1}, modifyColor(c, {x1, y1, z1}), z1, x1},
                {{x2, y1, z2}, modifyColor(c, {x2, y1, z2}), z2, x2},
                {{x1, y1, z2}, modifyColor(c, {x1, y1, z2}), z2, x1});
        addQuad({{x1, y1, z2}, modifyColor(c, {x1, y1, z2}), z2, y1},
                {{x1, y1, z1}, modifyColor(c, {x1, y1, z1}), z1, y1},
                {{x1, y2, z2}, modifyColor(c, {x1, y2, z2}), z2, y2},
                {{x1, y2, z1}, modifyColor(c, {x1, y2, z1}), z1, y2});
        addQuad({{x2, y1, z1}, modifyColor(c, {x2, y1, z1}), z1, y1},
                {{x2, y1, z2}, modifyColor(c, {x2, y1, z2}), z2, y1},
                {{x2, y2, z1}, modifyColor(c, {x2, y2, z1}), z1, y2},
                {{x2, y2, z2}, modifyColor(c, {x2, y2, z2}), z2, y2});
        addQuad({{x1, y1, z1}, modifyColor(c, {x1, y1, z1}), x1, y1},
                {{x2, y1, z1}, modifyColor(c, {x2, y1, z1}), x2, y1},
                {{x1, y2, z1}, modifyColor(c, {x1, y2, z1}), x1, y2},
                {{x2, y2, z1}, modifyColor(c, {x2, y2, z1}), x2, y2});
        addQuad({{x2, y1, z2}, modifyColor(c, {x2, y1, z2}), x2, y1},
                {{x1, y1, z2}, modifyColor(c, {x1, y1, z2}), x1, y1},
                {{x2, y2, z2}, modifyColor(c, {x2, y2, z2}), x2, y2},
                {{x1, y2, z2}, modifyColor(c, {x1, y2, z2}), x1, y2});
    }
};

struct Model {
    XMFLOAT3 Pos;
    XMFLOAT4 Rot;
    ID3D11ShaderResourceViewPtr Tex;
    ID3D11BufferPtr VertexBuffer;
    ID3D11BufferPtr IndexBuffer;
    std::size_t NumIndices;

    Model(ID3D11Device* device, const TriangleSet& t, XMFLOAT3 argPos, XMFLOAT4 argRot,
          ID3D11ShaderResourceView* tex)
        : Pos(argPos), Rot(argRot), Tex(tex), NumIndices{size(t.Indices)} {
        CD3D11_BUFFER_DESC vbDesc(size(t.Vertices) * sizeof(t.Vertices.back()),
                                  D3D11_BIND_VERTEX_BUFFER);
        D3D11_SUBRESOURCE_DATA vbData{t.Vertices.data(), 0, 0};
        device->CreateBuffer(&vbDesc, &vbData, &VertexBuffer);

        CD3D11_BUFFER_DESC ibDesc(size(t.Indices) * sizeof(t.Indices.back()),
                                  D3D11_BIND_INDEX_BUFFER);
        D3D11_SUBRESOURCE_DATA ibData{t.Indices.data(), 0, 0};
        device->CreateBuffer(&ibDesc, &ibData, &IndexBuffer);
    }

    void Render(DirectX11& directx, const XMMATRIX& projView) const {
        const auto modelMat = XMMatrixMultiply(XMMatrixRotationQuaternion(XMLoadFloat4(&Rot)),
                                               XMMatrixTranslationFromVector(XMLoadFloat3(&Pos)));
        const auto mat = XMMatrixMultiply(modelMat, projView);

        auto map = D3D11_MAPPED_SUBRESOURCE{};
        directx.Context->Map(directx.ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        memcpy(map.pData, &mat, sizeof(mat));
        directx.Context->Unmap(directx.ConstantBuffer, 0);

        directx.Context->IASetInputLayout(directx.InputLayout);
        directx.Context->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R16_UINT, 0);
        const auto vbs = {VertexBuffer.GetInterfacePtr()};
        directx.Context->IASetVertexBuffers(0, size(vbs), begin(vbs), std::begin({sizeof(Vertex)}),
                                            std::begin({UINT(0)}));
        directx.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        directx.Context->VSSetShader(directx.D3DVert, nullptr, 0);
        directx.Context->PSSetShader(directx.D3DPix, nullptr, 0);

        const auto samplerStates = {directx.SamplerState.GetInterfacePtr()};
        directx.Context->PSSetSamplers(0, size(samplerStates), begin(samplerStates));

        const auto texSrvs = {Tex.GetInterfacePtr()};
        directx.Context->PSSetShaderResources(0, size(texSrvs), begin(texSrvs));
        directx.Context->DrawIndexed(NumIndices, 0, 0);
    }
};

struct Scene {
    std::vector<std::unique_ptr<Model>> Models;

    void Render(DirectX11& directx, const XMMATRIX& projView) const {
        for (const auto& model : Models) model->Render(directx, projView);
    }

    Scene(ID3D11Device* device, ID3D11DeviceContext* context) {
        auto add = [this](Model* n) { Models.emplace_back(n); };

        TriangleSet cube;
        cube.AddBox(0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0xff404040);
        add(new Model(device, cube, {0, 0, 0}, {0, 0, 0, 1},
                      createTexture(device, context, TextureFill::AUTO_CEILING)));

        TriangleSet spareCube;
        spareCube.AddBox(0.1f, -0.1f, 0.1f, -0.1f, +0.1f, -0.1f, 0xffff0000);
        add(new Model(device, spareCube, {0, -10, 0}, {0, 0, 0, 1},
                      createTexture(device, context, TextureFill::AUTO_CEILING)));

        TriangleSet walls;
        walls.AddBox(10.1f, 0.0f, 20.0f, 10.0f, 4.0f, -20.0f, 0xff808080);     // Left Wall
        walls.AddBox(10.0f, -0.1f, 20.1f, -10.0f, 4.0f, 20.0f, 0xff808080);    // Back Wall
        walls.AddBox(-10.0f, -0.1f, 20.0f, -10.1f, 4.0f, -20.0f, 0xff808080);  // Right Wall
        add(new Model(device, walls, {0, 0, 0}, {0, 0, 0, 1},
                      createTexture(device, context, TextureFill::AUTO_WALL)));

        TriangleSet floors;
        floors.AddBox(10.0f, -0.1f, 20.0f, -10.0f, 0.0f, -20.1f, 0xff808080);    // Main floor
        floors.AddBox(15.0f, -6.1f, -18.0f, -15.0f, -6.0f, -30.0f, 0xff808080);  // Bottom floor
        add(new Model(device, floors, {0, 0, 0}, {0, 0, 0, 1},
                      createTexture(device, context, TextureFill::AUTO_FLOOR)));  // Floors

        TriangleSet ceiling;
        ceiling.AddBox(10.0f, 4.0f, 20.0f, -10.0f, 4.1f, -20.1f, 0xff808080);
        add(new Model(device, ceiling, {0, 0, 0}, {0, 0, 0, 1},
                      createTexture(device, context, TextureFill::AUTO_CEILING)));  // Ceiling

        TriangleSet furniture;
        furniture.AddBox(-9.5f, 0.75f, -3.0f, -10.1f, 2.5f, -3.1f,
                         0xff383838);  // Right side shelf// Verticals
        furniture.AddBox(-9.5f, 0.95f, -3.7f, -10.1f, 2.75f, -3.8f,
                         0xff383838);  // Right side shelf
        furniture.AddBox(-9.55f, 1.20f, -2.5f, -10.1f, 1.30f, -3.75f,
                         0xff383838);  // Right side shelf// Horizontals
        furniture.AddBox(-9.55f, 2.00f, -3.05f, -10.1f, 2.10f, -4.2f,
                         0xff383838);  // Right side shelf
        furniture.AddBox(-5.0f, 1.1f, -20.0f, -10.0f, 1.2f, -20.1f, 0xff383838);  // Right railing
        furniture.AddBox(10.0f, 1.1f, -20.0f, 5.0f, 1.2f, -20.1f, 0xff383838);    // Left railing
        for (float f = 5; f <= 9; f += 1)
            furniture.AddBox(-f, 0.0f, -20.0f, -f - 0.1f, 1.1f, -20.1f, 0xff505050);  // Left Bars
        for (float f = 5; f <= 9; f += 1)
            furniture.AddBox(f, 1.1f, -20.0f, f + 0.1f, 0.0f, -20.1f, 0xff505050);  // Right Bars
        furniture.AddBox(1.8f, 0.8f, -1.0f, 0.0f, 0.7f, 0.0f, 0xff505000);          // Table
        furniture.AddBox(1.8f, 0.0f, 0.0f, 1.7f, 0.7f, -0.1f, 0xff505000);          // Table Leg
        furniture.AddBox(1.8f, 0.7f, -1.0f, 1.7f, 0.0f, -0.9f, 0xff505000);         // Table Leg
        furniture.AddBox(0.0f, 0.0f, -1.0f, 0.1f, 0.7f, -0.9f, 0xff505000);         // Table Leg
        furniture.AddBox(0.0f, 0.7f, 0.0f, 0.1f, 0.0f, -0.1f, 0xff505000);          // Table Leg
        furniture.AddBox(1.4f, 0.5f, 1.1f, 0.8f, 0.55f, 0.5f, 0xff202050);          // Chair Set
        furniture.AddBox(1.401f, 0.0f, 1.101f, 1.339f, 1.0f, 1.039f, 0xff202050);   // Chair Leg 1
        furniture.AddBox(1.401f, 0.5f, 0.499f, 1.339f, 0.0f, 0.561f, 0xff202050);   // Chair Leg 2
        furniture.AddBox(0.799f, 0.0f, 0.499f, 0.861f, 0.5f, 0.561f, 0xff202050);   // Chair Leg 2
        furniture.AddBox(0.799f, 1.0f, 1.101f, 0.861f, 0.0f, 1.039f, 0xff202050);   // Chair Leg 2
        furniture.AddBox(1.4f, 0.97f, 1.05f, 0.8f, 0.92f, 1.10f,
                         0xff202050);  // Chair Back high bar
        for (float f = 3.0f; f <= 6.6f; f += 0.4f)
            furniture.AddBox(3, 0.0f, -f, 2.9f, 1.3f, -f - 0.1f, 0xff404040);  // Posts
        add(new Model(
            device, furniture, {0, 0, 0}, {0, 0, 0, 1},
            createTexture(device, context, TextureFill::AUTO_WHITE)));  // Fixtures & furniture
    }
};

struct Camera {
    XMVECTOR Pos;
    XMVECTOR Rot;
    auto GetViewMatrix() const {
        const auto forward = XMVector3Rotate(XMVectorSet(0, 0, -1, 0), Rot);
        return XMMatrixLookAtRH(Pos, XMVectorAdd(Pos, forward),
                                XMVector3Rotate(XMVectorSet(0, 1, 0, 0), Rot));
    }
};

// ovrSwapTextureSet wrapper class that also maintains the render target views needed for D3D11
// rendering.
struct OculusTexture {
    std::unique_ptr<ovrSwapTextureSet, std::function<void(ovrSwapTextureSet*)>> TextureSet;
    ID3D11RenderTargetViewPtr TexRtvs[2];

    OculusTexture(ID3D11Device* device, ovrHmd hmd, ovrSizei size)
        : TextureSet{[hmd, size, device, &texRtv = TexRtvs] {
                         // Create and validate the swap texture set and stash it in unique_ptr
                         CD3D11_TEXTURE2D_DESC dsDesc(
                             DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, size.w, size.h, 1, 1,
                             D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);

                         ovrSwapTextureSet* ts{};
                         auto result = ovr_CreateSwapTextureSetD3D11(
                             hmd, device, &dsDesc, ovrSwapTextureSetD3D11_Typeless, &ts);
                         VALIDATE(OVR_SUCCESS(result), "Failed to create SwapTextureSet.");
                         VALIDATE(ts->TextureCount == std::size(texRtv), "TextureCount mismatch.");
                         return ts;
                     }(),
                     // unique_ptr Deleter lambda to clean up the swap texture set
                     [hmd](ovrSwapTextureSet* ts) { ovr_DestroySwapTextureSet(hmd, ts); }} {
        // Create render target views for each of the textures in the swap texture set
        std::transform(TextureSet->Textures, TextureSet->Textures + TextureSet->TextureCount,
                       TexRtvs, [device](auto tex) {
                           CD3D11_RENDER_TARGET_VIEW_DESC rtvd(D3D11_RTV_DIMENSION_TEXTURE2D,
                                                               DXGI_FORMAT_R8G8B8A8_UNORM);
                           ID3D11RenderTargetViewPtr rtv;
                           device->CreateRenderTargetView(
                               reinterpret_cast<ovrD3D11Texture&>(tex).D3D11.pTexture, &rtvd, &rtv);
                           return rtv;
                       });
    }

    auto AdvanceToNextTexture() {
        return TextureSet->CurrentIndex = (TextureSet->CurrentIndex + 1) % TextureSet->TextureCount;
    }
};

DirectX11::DirectX11(HWND window, int vpW, int vpH, const LUID* pLuid)
    : WinSizeW{vpW}, WinSizeH{vpH} {
    auto windowSize = RECT{0, 0, WinSizeW, WinSizeH};
    AdjustWindowRect(&windowSize, WS_OVERLAPPEDWINDOW, false);
    const auto flags = SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW;
    if (!SetWindowPos(window, nullptr, 0, 0, windowSize.right - windowSize.left,
                      windowSize.bottom - windowSize.top, flags))
        VALIDATE(false, "SetWindowPos() failed.");

    IDXGIFactoryPtr dxgiFactory;
    auto hr = CreateDXGIFactory1(dxgiFactory.GetIID(), reinterpret_cast<void**>(&dxgiFactory));
    VALIDATE((hr == ERROR_SUCCESS), "CreateDXGIFactory1 failed");

    IDXGIAdapterPtr adapter;
    for (int iAdapter = 0; dxgiFactory->EnumAdapters(iAdapter, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++iAdapter) {
        DXGI_ADAPTER_DESC adapterDesc{};
        adapter->GetDesc(&adapterDesc);
        if ((pLuid == nullptr) || memcmp(&adapterDesc.AdapterLuid, pLuid, sizeof(LUID)) == 0) break;
    }

    auto DriverType = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
    const auto createFlags = [] {
#ifdef _DEBUG
        return D3D11_CREATE_DEVICE_DEBUG;
#else
        return D3D11_CREATE_DEVICE_FLAG(0);
#endif
    }();
    auto scDesc = DXGI_SWAP_CHAIN_DESC{};
    scDesc.BufferCount = 2;
    scDesc.BufferDesc.Width = WinSizeW;
    scDesc.BufferDesc.Height = WinSizeH;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferDesc.RefreshRate.Denominator = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = window;
    scDesc.SampleDesc.Count = 1;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    hr = D3D11CreateDeviceAndSwapChain(adapter, DriverType, nullptr, createFlags, nullptr, 0,
                                       D3D11_SDK_VERSION, &scDesc, &SwapChain, &Device, nullptr,
                                       &Context);
    VALIDATE((hr == ERROR_SUCCESS), "D3D11CreateDevice failed");

    // Create backbuffer
    hr = SwapChain->GetBuffer(0, BackBuffer.GetIID(), reinterpret_cast<void**>(&BackBuffer));
    VALIDATE((hr == ERROR_SUCCESS), "IDXGISwapChain::GetBuffer() failed");

    // Buffer for shader constants
    CD3D11_BUFFER_DESC uniformBufferDesc(sizeof(XMMATRIX), D3D11_BIND_CONSTANT_BUFFER,
                                         D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    Device->CreateBuffer(&uniformBufferDesc, nullptr, &ConstantBuffer);
    auto buffs = {ConstantBuffer.GetInterfacePtr()};
    Context->VSSetConstantBuffers(0, size(buffs), begin(buffs));

    // Set max frame latency to 1
    IDXGIDevice1Ptr DXGIDevice1;
    hr = Device.QueryInterface(DXGIDevice1.GetIID(), &DXGIDevice1);
    VALIDATE((hr == ERROR_SUCCESS), "QueryInterface failed");
    DXGIDevice1->SetMaximumFrameLatency(1);

    // Set up render states
    // Create and set rasterizer state
    auto rs = CD3D11_RASTERIZER_DESC{D3D11_DEFAULT};
    rs.AntialiasedLineEnable = rs.DepthClipEnable = TRUE;
    ID3D11RasterizerStatePtr rss;
    Device->CreateRasterizerState(&rs, &rss);
    Context->RSSetState(rss);

    // Create and set depth stencil state
    ID3D11DepthStencilStatePtr dss;
    Device->CreateDepthStencilState(&CD3D11_DEPTH_STENCIL_DESC{D3D11_DEFAULT}, &dss);
    Context->OMSetDepthStencilState(dss, 0);

    // Create and set blend state
    ID3D11BlendStatePtr bs;
    Device->CreateBlendState(&CD3D11_BLEND_DESC{D3D11_DEFAULT}, &bs);
    Context->OMSetBlendState(bs, nullptr, 0xffffffff);

    auto compileShader = [](const char* src, const char* target) {
        ID3DBlobPtr blob;
        D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr, "main", target, 0, 0, &blob,
                   nullptr);
        return blob;
    };

    // Create vertex shader and input layout
    auto defaultVertexShaderSrc = R"(float4x4 ProjView;
                                         void main(in float4 pos : POSITION,
                                                   in float4 col : COLOR0,
                                                   in float2 tex : TEXCOORD0,
                                                   out float4 oPos : SV_Position,
                                                   out float4 oCol : COLOR0,
                                                   out float2 oTex : TEXCOORD0) {
                                             oPos = mul(ProjView, pos);
                                             oTex = tex;
                                             oCol = col;
                                         })";
    ID3DBlobPtr vsBlob = compileShader(defaultVertexShaderSrc, "vs_4_0");
    Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
                               &D3DVert);
    D3D11_INPUT_ELEMENT_DESC defaultVertexDesc[] = {
        {"Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"Color", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TexCoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    Device->CreateInputLayout(defaultVertexDesc, std::size(defaultVertexDesc),
                              vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &InputLayout);

    // Create pixel shader
    auto defaultPixelShaderSrc = R"(Texture2D Texture : register(t0);
                                        SamplerState Linear : register(s0);
                                        float4 main(in float4 Position : SV_Position,
                                                    in float4 Color: COLOR0,
                                                    in float2  TexCoord : TEXCOORD0) : SV_Target {
                                            float4 TexCol = Texture.Sample(Linear, TexCoord);
                                            return(Color * TexCol);
                                        })";
    auto psBlob = compileShader(defaultPixelShaderSrc, "ps_4_0");
    Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
                              &D3DPix);

    // Create sampler state
    CD3D11_SAMPLER_DESC ss{D3D11_DEFAULT};
    ss.Filter = D3D11_FILTER_ANISOTROPIC;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    ss.MaxAnisotropy = 8;
    ss.MaxLOD = 15;
    Device->CreateSamplerState(&ss, &SamplerState);
}

// Helper to wrap ovr types like ovrHmd and ovrTexture* in a unique_ptr with custom create / destroy
template <typename CreateFunc, typename DestroyFunc>
auto create_unique(CreateFunc createFunc, DestroyFunc destroyFunc) {
    return std::unique_ptr<std::remove_pointer_t<std::result_of_t<CreateFunc()>>, DestroyFunc>{
        createFunc(), destroyFunc};
}

// return true to retry later (e.g. after display lost)
static bool MainLoop(const Window& window, bool retryCreate) {
    auto result = ovrResult{};
    auto luid = ovrGraphicsLuid{};
    // Initialize the HMD, stash it in a unique_ptr for automatic cleanup.
    auto HMD = create_unique(
        [&result, &luid] {
            ovrHmd HMD{};
            result = ovr_Create(&HMD, &luid);
            return HMD;
        },
        ovr_Destroy);
    if (!OVR_SUCCESS(result)) return retryCreate;

    auto hmdDesc = ovr_GetHmdDesc(HMD.get());

    // Setup Device and Graphics
    // Note: the mirror window can be any size, for this sample we use 1/2 the HMD resolution
    DirectX11 directx(window.Hwnd, hmdDesc.Resolution.w / 2, hmdDesc.Resolution.h / 2,
        reinterpret_cast<LUID*>(&luid));

    // Start the sensor which informs of the Rift's pose and motion
    result = ovr_ConfigureTracking(
        HMD.get(),
        ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection | ovrTrackingCap_Position, 0);
    VALIDATE(OVR_SUCCESS(result), "Failed to configure tracking.");

    // Make the eye render buffers (caution if actual size < requested due to HW limits).
    const ovrSizei idealSizes[] = {
        ovr_GetFovTextureSize(HMD.get(), ovrEye_Left, hmdDesc.DefaultEyeFov[ovrEye_Left], 1.0f),
        ovr_GetFovTextureSize(HMD.get(), ovrEye_Right, hmdDesc.DefaultEyeFov[ovrEye_Right], 1.0f)};
    OculusTexture eyeRenderTextures[] = {{directx.Device, HMD.get(), idealSizes[ovrEye_Left]},
                                         {directx.Device, HMD.get(), idealSizes[ovrEye_Right]}};
    DepthBuffer eyeDepthBuffers[] = {{directx.Device, idealSizes[ovrEye_Left]},
                                     {directx.Device, idealSizes[ovrEye_Right]}};
    const ovrRecti eyeRenderViewports[] = {{{0, 0}, idealSizes[ovrEye_Left]},
                                           {{0, 0}, idealSizes[ovrEye_Right]}};

    // Create a mirror to see on the monitor, stash it in a unique_ptr for automatic cleanup.
    auto mirrorTexture =
        create_unique([&result, hmd = HMD.get(), &directx] {
            CD3D11_TEXTURE2D_DESC td(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, directx.WinSizeW,
                                     directx.WinSizeH, 1, 1);
            ovrTexture* mirrorTexture{};
            result = ovr_CreateMirrorTextureD3D11(hmd, directx.Device, &td, 0, &mirrorTexture);
            return mirrorTexture;
        },
                      [hmd = HMD.get()](ovrTexture * mt) { ovr_DestroyMirrorTexture(hmd, mt); });
    VALIDATE(OVR_SUCCESS(result), "Failed to create mirror texture.");

    // Initialize the scene
    auto roomScene = Scene{directx.Device, directx.Context};
    auto mainCam = Camera{XMVectorSet(0.0f, 1.6f, 5.0f, 0), XMQuaternionIdentity()};
    auto isVisible = true;

    // Main loop
    while (window.HandleMessages()) {
        // Handle input
        [&mainCam, &window] {
            const auto forward = XMVector3Rotate(XMVectorSet(0, 0, -0.05f, 0), mainCam.Rot);
            const auto right = XMVector3Rotate(XMVectorSet(0.05f, 0, 0, 0), mainCam.Rot);
            if (window.Keys['W'] || window.Keys[VK_UP])
                mainCam.Pos = XMVectorAdd(mainCam.Pos, forward);
            if (window.Keys['S'] || window.Keys[VK_DOWN])
                mainCam.Pos = XMVectorSubtract(mainCam.Pos, forward);
            if (window.Keys['D']) mainCam.Pos = XMVectorAdd(mainCam.Pos, right);
            if (window.Keys['A']) mainCam.Pos = XMVectorSubtract(mainCam.Pos, right);
            static auto Yaw = 0.0f;
            if (window.Keys[VK_LEFT])
                mainCam.Rot = XMQuaternionRotationRollPitchYaw(0, Yaw += 0.02f, 0);
            if (window.Keys[VK_RIGHT])
                mainCam.Rot = XMQuaternionRotationRollPitchYaw(0, Yaw -= 0.02f, 0);
        }();

        // Animate the cube
        [& cube = roomScene.Models[0]] {
            static auto cubeClock = 0.0f;
            cube->Pos = XMFLOAT3(9 * sin(cubeClock), 3, 9 * cos(cubeClock += 0.015f));
        }();

        // Get both eye poses simultaneously, with IPD offset already included.
        const ovrEyeRenderDesc eyeRenderDesc[] = {
            ovr_GetRenderDesc(HMD.get(), ovrEye_Left, hmdDesc.DefaultEyeFov[ovrEye_Left]),
            ovr_GetRenderDesc(HMD.get(), ovrEye_Right, hmdDesc.DefaultEyeFov[ovrEye_Right])};
        const auto eyeRenderPoses = [hmd = HMD.get(), &eyeRenderDesc] {
            std::array<ovrPosef, 2> res;
            const auto ftiming = ovr_GetFrameTiming(hmd, 0);
            const auto hmdState = ovr_GetTrackingState(hmd, ftiming.DisplayMidpointSeconds);
            const ovrVector3f HmdToEyeViewOffset[] = {
                eyeRenderDesc[ovrEye_Left].HmdToEyeViewOffset,
                eyeRenderDesc[ovrEye_Right].HmdToEyeViewOffset};
            ovr_CalcEyePoses(hmdState.HeadPose.ThePose, HmdToEyeViewOffset, res.data());
            return res;
        }();

        // Render Scene to Eye Buffers
        if (isVisible) {
            for (auto eye : {ovrEye_Left, ovrEye_Right}) {
                // Increment to use next texture, just before rendering
                const auto texIndex = eyeRenderTextures[eye].AdvanceToNextTexture();
                directx.SetAndClearRenderTarget(eyeRenderTextures[eye].TexRtvs[texIndex],
                                                &eyeDepthBuffers[eye]);
                directx.SetViewport(eyeRenderViewports[eye]);

                // Get the pose information in XM format
                const auto eyeQuat = XMLoadFloat4(&XMFLOAT4{&eyeRenderPoses[eye].Orientation.x});
                const auto eyePos = XMLoadFloat3(&XMFLOAT3{&eyeRenderPoses[eye].Position.x});

                // Get view and projection matrices for the Rift camera
                const auto CombinedPos =
                    XMVectorAdd(mainCam.Pos, XMVector3Rotate(eyePos, mainCam.Rot));
                const auto finalCam =
                    Camera{CombinedPos, XMQuaternionMultiply(eyeQuat, mainCam.Rot)};
                const auto p = ovrMatrix4f_Projection(eyeRenderDesc[eye].Fov, 0.2f, 1000.0f,
                                                      ovrProjection_RightHanded);
                const auto proj = XMMatrixTranspose(XMLoadFloat4x4(&XMFLOAT4X4{&p.M[0][0]}));

                // Render the scene
                roomScene.Render(directx, XMMatrixMultiply(finalCam.GetViewMatrix(), proj));
            }
        }

        // Initialize our single full screen Fov layer.
        const auto ld = [&eyeRenderTextures, &eyeRenderViewports, &hmdDesc, &eyeRenderPoses] {
            auto res = ovrLayerEyeFov{{ovrLayerType_EyeFov, 0}};
            for (auto eye : {ovrEye_Left, ovrEye_Right}) {
                res.ColorTexture[eye] = eyeRenderTextures[eye].TextureSet.get();
                res.Viewport[eye] = eyeRenderViewports[eye];
                res.Fov[eye] = hmdDesc.DefaultEyeFov[eye];
                res.RenderPose[eye] = eyeRenderPoses[eye];
            }
            return res;
        }();
        const auto layers = &ld.Header;
        result = ovr_SubmitFrame(HMD.get(), 0, nullptr, &layers, 1);
        // exit the rendering loop if submit returns an error, will retry on ovrError_DisplayLost
        if (!OVR_SUCCESS(result)) return retryCreate;
        isVisible = result == ovrSuccess;

        // Render mirror
        directx.Context->CopyResource(
            directx.BackBuffer,
            reinterpret_cast<ovrD3D11Texture*>(mirrorTexture.get())->D3D11.pTexture);
        directx.SwapChain->Present(0, 0);
    }

    // Retry on ovrError_DisplayLost
    return retryCreate || !window.Running || result == ovrError_DisplayLost;
}

//-------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int) {
    // Initializes LibOVR, and the Rift
    auto result = ovr_Initialize(nullptr);
    VALIDATE(OVR_SUCCESS(result), "Failed to initialize libOVR.");

    Window window{hinst, L"Oculus Room Tiny (DX11)"};
    window.Run(MainLoop);

    ovr_Shutdown();

    return 0;
}
