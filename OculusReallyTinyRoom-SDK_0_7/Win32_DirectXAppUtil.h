
/************************************************************************************
Filename    :   Win32_DirectXAppUtil.h
Content     :   D3D11 application/Window setup functionality for RoomTiny
Created     :   October 20th, 2014
Author      :   Tom Heath
Copyright   :   Copyright 2014 Oculus, Inc. All Rights reserved.
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
#ifndef OVR_Win32_DirectXAppUtil_h
#define OVR_Win32_DirectXAppUtil_h

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <vector>

#include <comdef.h>
#include <comip.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

using namespace DirectX;

#ifndef VALIDATE
    #define VALIDATE(x, msg) if (!(x)) { MessageBoxA(NULL, (msg), "OculusRoomTiny", MB_ICONERROR | MB_OK); exit(-1); }
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

    DepthBuffer(ID3D11Device* Device, int sizeW, int sizeH, int sampleCount = 1) {
        CD3D11_TEXTURE2D_DESC dsDesc(DXGI_FORMAT_D24_UNORM_S8_UINT, sizeW, sizeH);
        dsDesc.MipLevels = 1;
        dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        dsDesc.SampleDesc.Count = sampleCount;
        ID3D11Texture2DPtr Tex;
        Device->CreateTexture2D(&dsDesc, NULL, &Tex);
        Device->CreateDepthStencilView(Tex, NULL, &TexDsv);
    }
};

// clean up member COM pointers
template<typename T> void Release(T *&obj)
{
    if (!obj) return;
    obj->Release();
    obj = nullptr;
}

struct DataBuffer {
    ID3D11BufferPtr D3DBuffer;
    size_t Size;

    DataBuffer(ID3D11Device* Device, D3D11_BIND_FLAG use, const void* buffer, size_t size)
        : Size(size) {
        CD3D11_BUFFER_DESC desc(size, use, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
        D3D11_SUBRESOURCE_DATA sr{buffer, 0, 0};
        Device->CreateBuffer(&desc, buffer ? &sr : nullptr, &D3DBuffer);
    }
};

struct DirectX11 {
    HWND Window = nullptr;
    bool Running = false;
    bool Key[256];
    int WinSizeW = 0;
    int WinSizeH = 0;
    ID3D11DevicePtr Device;
    ID3D11DeviceContextPtr Context;
    IDXGISwapChainPtr SwapChain;
    std::unique_ptr<DepthBuffer> MainDepthBuffer;
    ID3D11Texture2DPtr BackBuffer;
    ID3D11RenderTargetViewPtr BackBufferRT;
    // Fixed size buffer for shader constants, before copied into buffer
    static const int UNIFORM_DATA_SIZE = 2000;
    unsigned char UniformData[UNIFORM_DATA_SIZE];
    std::unique_ptr<DataBuffer> UniformBufferGen;
    HINSTANCE hInstance = nullptr;

    static LRESULT CALLBACK WindowProc(_In_ HWND hWnd, _In_ UINT Msg, _In_ WPARAM wParam,
                                       _In_ LPARAM lParam) {
        auto p = reinterpret_cast<DirectX11*>(GetWindowLongPtr(hWnd, 0));
        switch (Msg) {
            case WM_KEYDOWN:
                p->Key[wParam] = true;
                break;
            case WM_KEYUP:
                p->Key[wParam] = false;
                break;
            case WM_DESTROY:
                p->Running = false;
                break;
            default:
                return DefWindowProcW(hWnd, Msg, wParam, lParam);
        }
        if ((p->Key['Q'] && p->Key[VK_CONTROL]) || p->Key[VK_ESCAPE]) {
            p->Running = false;
        }
        return 0;
    }

    DirectX11() {
        // Clear input
        std::fill(std::begin(Key), std::end(Key), false);
    }

    ~DirectX11() {
        ReleaseDevice();
        CloseWindow();
    }

    bool InitWindow(HINSTANCE hinst, LPCWSTR title) {
        hInstance = hinst;
        Running = true;

        WNDCLASSW wc{};
        wc.lpszClassName = L"App";
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.cbWndExtra = sizeof(this);
        RegisterClassW(&wc);

        // adjust the window size and show at InitDevice time
        Window =
            CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, 0, 0, hinst, 0);
        if (!Window) return false;

        SetWindowLongPtr(Window, 0, LONG_PTR(this));

        return true;
    }

    void CloseWindow() {
        if (Window) {
            DestroyWindow(Window);
            Window = nullptr;
            UnregisterClassW(L"App", hInstance);
        }
    }

    bool InitDevice(int vpW, int vpH, const LUID* pLuid, bool windowed = true) {
        WinSizeW = vpW;
        WinSizeH = vpH;

        RECT size = {0, 0, vpW, vpH};
        AdjustWindowRect(&size, WS_OVERLAPPEDWINDOW, false);
        const UINT flags = SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW;
        if (!SetWindowPos(Window, nullptr, 0, 0, size.right - size.left, size.bottom - size.top,
                          flags))
            return false;

        IDXGIFactoryPtr DXGIFactory;
        HRESULT hr =
            CreateDXGIFactory1(DXGIFactory.GetIID(), reinterpret_cast<void**>(&DXGIFactory));
        VALIDATE((hr == ERROR_SUCCESS), "CreateDXGIFactory1 failed");

        IDXGIAdapterPtr Adapter;
        for (UINT iAdapter = 0;
             DXGIFactory->EnumAdapters(iAdapter, &Adapter) != DXGI_ERROR_NOT_FOUND; ++iAdapter) {
            DXGI_ADAPTER_DESC adapterDesc;
            Adapter->GetDesc(&adapterDesc);
            if ((pLuid == nullptr) || memcmp(&adapterDesc.AdapterLuid, pLuid, sizeof(LUID)) == 0)
                break;
        }

        const D3D11_CREATE_DEVICE_FLAG createFlags = [] {
#ifdef _DEBUG
            return D3D11_CREATE_DEVICE_DEBUG;
#else
            return 0;
#endif
        }();
        auto DriverType = Adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
        hr = D3D11CreateDevice(Adapter, DriverType, 0, createFlags, nullptr, 0, D3D11_SDK_VERSION,
                               &Device, nullptr, &Context);
        VALIDATE((hr == ERROR_SUCCESS), "D3D11CreateDevice failed");

        // Create swap chain
        DXGI_SWAP_CHAIN_DESC scDesc{};
        scDesc.BufferCount = 2;
        scDesc.BufferDesc.Width = WinSizeW;
        scDesc.BufferDesc.Height = WinSizeH;
        scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferDesc.RefreshRate.Denominator = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.OutputWindow = Window;
        scDesc.SampleDesc.Count = 1;
        scDesc.Windowed = windowed;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
        hr = DXGIFactory->CreateSwapChain(Device, &scDesc, &SwapChain);
        VALIDATE((hr == ERROR_SUCCESS), "CreateSwapChain failed");

        // Create backbuffer
        SwapChain->GetBuffer(0, BackBuffer.GetIID(), reinterpret_cast<void**>(&BackBuffer));
        hr = Device->CreateRenderTargetView(BackBuffer, nullptr, &BackBufferRT);
        VALIDATE((hr == ERROR_SUCCESS), "CreateRenderTargetView failed");

        // Main depth buffer
        MainDepthBuffer = std::make_unique<DepthBuffer>(Device, WinSizeW, WinSizeH);
        ID3D11RenderTargetView* rts[] = {BackBufferRT};
        Context->OMSetRenderTargets(std::distance(std::begin(rts), std::end(rts)), rts,
                                    MainDepthBuffer->TexDsv);

        // Buffer for shader constants
        UniformBufferGen = std::make_unique<DataBuffer>(Device, D3D11_BIND_CONSTANT_BUFFER, nullptr,
                                                        UNIFORM_DATA_SIZE);
        ID3D11Buffer* buffs[] = {UniformBufferGen->D3DBuffer};
        Context->VSSetConstantBuffers(0, std::distance(std::begin(buffs), std::end(buffs)), buffs);

        // Set max frame latency to 1
        IDXGIDevice1Ptr DXGIDevice1;
        hr = Device.QueryInterface(DXGIDevice1.GetIID(), &DXGIDevice1);
        VALIDATE((hr == ERROR_SUCCESS), "QueryInterface failed");
        DXGIDevice1->SetMaximumFrameLatency(1);

        return true;
    }

    void SetAndClearRenderTarget(ID3D11RenderTargetView* rendertarget, DepthBuffer* depthbuffer) {
        float black[] = {0.0f, 0.0f, 0.0f, 0.0f};
        Context->OMSetRenderTargets(1, &rendertarget, depthbuffer->TexDsv);
        Context->ClearRenderTargetView(rendertarget, black);
        Context->ClearDepthStencilView(depthbuffer->TexDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                       1, 0);
    }

    void SetViewport(float vpX, float vpY, float vpW, float vpH) {
        D3D11_VIEWPORT D3Dvp{vpX, vpY, vpW, vpH, 0.0f, 1.0f};
        Context->RSSetViewports(1, &D3Dvp);
    }

    bool HandleMessages() {
        MSG msg{};
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return Running;
    }

    void Run(bool (*MainLoop)(bool retryCreate)) {
        // false => just fail on any error
        VALIDATE(MainLoop(false), "Oculus Rift not detected.");
        while (HandleMessages()) {
            // true => we'll attempt to retry for ovrError_DisplayLost
            if (!MainLoop(true)) break;
            // Sleep a bit before retrying to reduce CPU load while the HMD is disconnected
            Sleep(10);
        }
    }

    void ReleaseDevice() {
        if (SwapChain) {
            SwapChain->SetFullscreenState(FALSE, NULL);
        }
    }
};

// global DX11 state
static struct DirectX11 DIRECTX;

struct Texture
{
    ID3D11Texture2DPtr Tex;
    ID3D11ShaderResourceViewPtr TexSv;
    ID3D11RenderTargetViewPtr TexRtv;
    int SizeW, SizeH, MipLevels;

    enum { AUTO_WHITE = 1, AUTO_WALL, AUTO_FLOOR, AUTO_CEILING, AUTO_GRID, AUTO_GRADE_256 };
    Texture(int sizeW, int sizeH, bool rendertarget, int mipLevels = 1, int sampleCount = 1)
        : SizeW{sizeW}, SizeH{sizeH}, MipLevels{mipLevels} {
        CD3D11_TEXTURE2D_DESC dsDesc(DXGI_FORMAT_R8G8B8A8_UNORM, SizeW, SizeH);
        dsDesc.MipLevels = MipLevels;
        dsDesc.SampleDesc.Count = sampleCount;
        if (rendertarget) dsDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;

        DIRECTX.Device->CreateTexture2D(&dsDesc, nullptr, &Tex);
        DIRECTX.Device->CreateShaderResourceView(Tex, nullptr, &TexSv);
        if (rendertarget) DIRECTX.Device->CreateRenderTargetView(Tex, nullptr, &TexRtv);
    }

    Texture(bool rendertarget, int sizeW, int sizeH, int autoFillData = 0, int sampleCount = 1)
        : Texture{sizeW, sizeH, rendertarget, autoFillData ? 8 : 1, sampleCount} {
        if (autoFillData) AutoFillTexture(autoFillData);
    }

    void FillTexture(DWORD* pix) {
        // Make local ones, because will be reducing them
        int sizeW = SizeW;
        int sizeH = SizeH;
        for (int level = 0; level < MipLevels; ++level) {
            DIRECTX.Context->UpdateSubresource(Tex, level, nullptr, pix, sizeW * 4, sizeH * 4);

            for (int j = 0; j < (sizeH & ~1); j += 2) {
                uint8_t* psrc = reinterpret_cast<uint8_t*>(pix) + (sizeW * j * 4);
                uint8_t* pdest = reinterpret_cast<uint8_t*>(pix) + (sizeW * j);
                for (int i = 0; i<sizeW>> 1; i++, psrc += 8, pdest += 4) {
                    pdest[0] =
                        (((int)psrc[0]) + psrc[4] + psrc[sizeW * 4 + 0] + psrc[sizeW * 4 + 4]) >> 2;
                    pdest[1] =
                        (((int)psrc[1]) + psrc[5] + psrc[sizeW * 4 + 1] + psrc[sizeW * 4 + 5]) >> 2;
                    pdest[2] =
                        (((int)psrc[2]) + psrc[6] + psrc[sizeW * 4 + 2] + psrc[sizeW * 4 + 6]) >> 2;
                    pdest[3] =
                        (((int)psrc[3]) + psrc[7] + psrc[sizeW * 4 + 3] + psrc[sizeW * 4 + 7]) >> 2;
                }
            }
            sizeW >>= 1;
            sizeH >>= 1;
        }
    }

    static void ConvertToSRGB(DWORD* linear) {
        DWORD drgb[3];
        for (int k = 0; k < 3; k++) {
            float rgb = ((float)((*linear >> (k * 8)) & 0xff)) / 255.0f;
            rgb = pow(rgb, 2.2f);
            drgb[k] = (DWORD)(rgb * 255.0f);
        }
        *linear = (*linear & 0xff000000) + (drgb[2] << 16) + (drgb[1] << 8) + (drgb[0] << 0);
    }

    void AutoFillTexture(int autoFillData) {
        std::vector<DWORD> pix(SizeW * SizeH);
        for (int j = 0; j < SizeH; j++)
            for (int i = 0; i < SizeW; i++) {
                DWORD* curr = &pix[j * SizeW + i];
                switch (autoFillData) {
                    case (AUTO_WALL):
                        *curr = (((j / 4 & 15) == 0) ||
                                 (((i / 4 & 15) == 0) &&
                                  ((((i / 4 & 31) == 0) ^ ((j / 4 >> 4) & 1)) == 0)))
                                    ? 0xff3c3c3c
                                    : 0xffb4b4b4;
                        break;
                    case (AUTO_FLOOR):
                        *curr = (((i >> 7) ^ (j >> 7)) & 1) ? 0xffb4b4b4 : 0xff505050;
                        break;
                    case (AUTO_CEILING):
                        *curr = (i / 4 == 0 || j / 4 == 0) ? 0xff505050 : 0xffb4b4b4;
                        break;
                    case (AUTO_WHITE):
                        *curr = 0xffffffff;
                        break;
                    case (AUTO_GRADE_256):
                        *curr = 0xff000000 + i * 0x010101;
                        break;
                    case (AUTO_GRID):
                        *curr = (i < 4) || (i > (SizeW - 5)) || (j < 4) || (j > (SizeH - 5))
                                    ? 0xffffffff
                                    : 0xff000000;
                        break;
                    default:
                        *curr = 0xffffffff;
                        break;
                }
                /// ConvertToSRGB(curr); //Require format for SDK - I've been recommended to remove
                /// for now.
            }
        FillTexture(pix.data());
    }
};

struct Material {
    ID3D11VertexShaderPtr D3DVert;
    ID3D11PixelShaderPtr D3DPix;
    std::unique_ptr<Texture> Tex;
    ID3D11InputLayoutPtr InputLayout;
    UINT VertexSize;
    ID3D11SamplerStatePtr SamplerState;
    ID3D11RasterizerStatePtr Rasterizer;
    ID3D11DepthStencilStatePtr DepthState;
    ID3D11BlendStatePtr BlendState;

    enum { MAT_WRAP = 1, MAT_WIRE = 2, MAT_ZALWAYS = 4, MAT_NOCULL = 8, MAT_TRANS = 16 };

    Material(Texture* t)
        : Tex(t), VertexSize(24) {
        D3D11_INPUT_ELEMENT_DESC defaultVertexDesc[] = {
            {"Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"Color", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TexCoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        const DWORD flags = MAT_WRAP | MAT_TRANS;

        // Use defaults if no shaders specified
        const char* defaultVertexShaderSrc =
            "float4x4 ProjView;  float4 MasterCol;"
            "void main(in  float4 Position  : POSITION,    in  float4 Color : COLOR0, in  float2 "
            "TexCoord  : TEXCOORD0,"
            "          out float4 oPosition : SV_Position, out float4 oColor: COLOR0, out float2 "
            "oTexCoord : TEXCOORD0)"
            "{   oPosition = mul(ProjView, Position); oTexCoord = TexCoord; "
            "    oColor = MasterCol * Color; }";
        const char* defaultPixelShaderSrc =
            "Texture2D Texture   : register(t0); SamplerState Linear : register(s0); "
            "float4 main(in float4 Position : SV_Position, in float4 Color: COLOR0, in float2 "
            "TexCoord : TEXCOORD0) : SV_Target"
            "{   float4 TexCol = Texture.Sample(Linear, TexCoord); "
            "    if (TexCol.a==0) clip(-1); "  // If alpha = 0, don't draw
            "    return(Color * TexCol); }";

        // Create vertex shader
        ID3DBlobPtr blobData;
        D3DCompile(defaultVertexShaderSrc, strlen(defaultVertexShaderSrc), nullptr, nullptr,
                   nullptr, "main", "vs_4_0", 0, 0, &blobData, nullptr);
        DIRECTX.Device->CreateVertexShader(blobData->GetBufferPointer(), blobData->GetBufferSize(),
                                           nullptr, &D3DVert);

        // Create input layout
        DIRECTX.Device->CreateInputLayout(
            defaultVertexDesc,
            std::distance(std::begin(defaultVertexDesc), std::end(defaultVertexDesc)),
            blobData->GetBufferPointer(), blobData->GetBufferSize(), &InputLayout);

        // Create pixel shader
        D3DCompile(defaultPixelShaderSrc, strlen(defaultPixelShaderSrc), nullptr, nullptr, nullptr,
                   "main", "ps_4_0", 0, 0, &blobData, nullptr);
        DIRECTX.Device->CreatePixelShader(blobData->GetBufferPointer(), blobData->GetBufferSize(),
                                          nullptr, &D3DPix);

        // Create sampler state
        D3D11_SAMPLER_DESC ss;
        memset(&ss, 0, sizeof(ss));
        ss.AddressU = ss.AddressV = ss.AddressW =
            flags & MAT_WRAP ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_BORDER;
        ss.Filter = D3D11_FILTER_ANISOTROPIC;
        ss.MaxAnisotropy = 8;
        ss.MaxLOD = 15;
        DIRECTX.Device->CreateSamplerState(&ss, &SamplerState);

        // Create rasterizer
        D3D11_RASTERIZER_DESC rs;
        memset(&rs, 0, sizeof(rs));
        rs.AntialiasedLineEnable = rs.DepthClipEnable = true;
        rs.CullMode = flags & MAT_NOCULL ? D3D11_CULL_NONE : D3D11_CULL_BACK;
        rs.FillMode = flags & MAT_WIRE ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        DIRECTX.Device->CreateRasterizerState(&rs, &Rasterizer);

        // Create depth state
        D3D11_DEPTH_STENCIL_DESC dss;
        memset(&dss, 0, sizeof(dss));
        dss.DepthEnable = true;
        dss.DepthFunc = flags & MAT_ZALWAYS ? D3D11_COMPARISON_ALWAYS : D3D11_COMPARISON_LESS;
        dss.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        DIRECTX.Device->CreateDepthStencilState(&dss, &DepthState);

        // Create blend state - trans or otherwise
        D3D11_BLEND_DESC bm;
        memset(&bm, 0, sizeof(bm));
        bm.RenderTarget[0].BlendEnable = flags & MAT_TRANS ? true : false;
        bm.RenderTarget[0].BlendOp = bm.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bm.RenderTarget[0].SrcBlend = bm.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        bm.RenderTarget[0].DestBlend = bm.RenderTarget[0].DestBlendAlpha =
            D3D11_BLEND_INV_SRC_ALPHA;
        bm.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        DIRECTX.Device->CreateBlendState(&bm, &BlendState);
    }
};

//----------------------------------------------------------------------
struct Vertex
{
	XMFLOAT3  Pos;
	DWORD     C;
	float     U, V;
	Vertex() {};
	Vertex(XMFLOAT3 pos, DWORD c, float u, float v) : Pos(pos), C(c), U(u), V(v) {};
};

//-----------------------------------------------------------------------
struct TriangleSet
{
	int       numVertices, numIndices, maxBuffer;
	Vertex    * Vertices;
	short     * Indices;
	TriangleSet(int maxTriangles = 2000) : maxBuffer(3 * maxTriangles)
	{
		numVertices = numIndices = 0;
		Vertices = (Vertex *)_aligned_malloc(maxBuffer *sizeof(Vertex), 16);
		Indices = (short *)  _aligned_malloc(maxBuffer *sizeof(short), 16);
	}
    ~TriangleSet()
    {
        _aligned_free(Vertices);
        _aligned_free(Indices);
    }
	void AddQuad(Vertex v0, Vertex v1, Vertex v2, Vertex v3) { AddTriangle(v0, v1, v2);	AddTriangle(v3, v2, v1); }
	void AddTriangle(Vertex v0, Vertex v1, Vertex v2)
	{
		VALIDATE(numVertices <= (maxBuffer - 3), "Insufficient triangle set");
		for (int i = 0; i < 3; i++) Indices[numIndices++] = numVertices + i;
		Vertices[numVertices++] = v0;
		Vertices[numVertices++] = v1;
		Vertices[numVertices++] = v2;
	}

	DWORD ModifyColor(DWORD c, XMFLOAT3 pos)
	{
		#define GetLengthLocal(v)  (sqrt(v.x*v.x + v.y*v.y + v.z*v.z))
		float dist1 = GetLengthLocal(XMFLOAT3(pos.x - (-2), pos.y - (4), pos.z - (-2)));
		float dist2 = GetLengthLocal(XMFLOAT3(pos.x - (3),  pos.y - (4), pos.z - (-3)));
		float dist3 = GetLengthLocal(XMFLOAT3(pos.x - (-4), pos.y - (3), pos.z - (25)));
		int   bri = rand() % 160;
		float R = ((c >> 16) & 0xff) * (bri + 192.0f*(0.65f + 8 / dist1 + 1 / dist2 + 4 / dist3)) / 255.0f;
		float G = ((c >> 8) & 0xff) * (bri + 192.0f*(0.65f + 8 / dist1 + 1 / dist2 + 4 / dist3)) / 255.0f;
		float B = ((c >> 0) & 0xff) * (bri + 192.0f*(0.65f + 8 / dist1 + 1 / dist2 + 4 / dist3)) / 255.0f;
		return( (c & 0xff000000) + ((R>255 ? 255 : (DWORD)R) << 16) + ((G>255 ? 255 : (DWORD)G) << 8) + (B>255 ? 255 : (DWORD)B));
	}

	void AddSolidColorBox(float x1, float y1, float z1, float x2, float y2, float z2, DWORD c)
	{
		AddQuad(Vertex(XMFLOAT3(x1, y2, z1), ModifyColor(c, XMFLOAT3(x1, y2, z1)), z1, x1),
			    Vertex(XMFLOAT3(x2, y2, z1), ModifyColor(c, XMFLOAT3(x2, y2, z1)), z1, x2),
			    Vertex(XMFLOAT3(x1, y2, z2), ModifyColor(c, XMFLOAT3(x1, y2, z2)), z2, x1),
			    Vertex(XMFLOAT3(x2, y2, z2), ModifyColor(c, XMFLOAT3(x2, y2, z2)), z2, x2));
		AddQuad(Vertex(XMFLOAT3(x2, y1, z1), ModifyColor(c, XMFLOAT3(x2, y1, z1)), z1, x2),
			    Vertex(XMFLOAT3(x1, y1, z1), ModifyColor(c, XMFLOAT3(x1, y1, z1)), z1, x1),
			    Vertex(XMFLOAT3(x2, y1, z2), ModifyColor(c, XMFLOAT3(x2, y1, z2)), z2, x2),
			    Vertex(XMFLOAT3(x1, y1, z2), ModifyColor(c, XMFLOAT3(x1, y1, z2)), z2, x1));
		AddQuad(Vertex(XMFLOAT3(x1, y1, z2), ModifyColor(c, XMFLOAT3(x1, y1, z2)), z2, y1),
			    Vertex(XMFLOAT3(x1, y1, z1), ModifyColor(c, XMFLOAT3(x1, y1, z1)), z1, y1),
			    Vertex(XMFLOAT3(x1, y2, z2), ModifyColor(c, XMFLOAT3(x1, y2, z2)), z2, y2),
			    Vertex(XMFLOAT3(x1, y2, z1), ModifyColor(c, XMFLOAT3(x1, y2, z1)), z1, y2));
		AddQuad(Vertex(XMFLOAT3(x2, y1, z1), ModifyColor(c, XMFLOAT3(x2, y1, z1)), z1, y1),
			    Vertex(XMFLOAT3(x2, y1, z2), ModifyColor(c, XMFLOAT3(x2, y1, z2)), z2, y1),
			    Vertex(XMFLOAT3(x2, y2, z1), ModifyColor(c, XMFLOAT3(x2, y2, z1)), z1, y2),
			    Vertex(XMFLOAT3(x2, y2, z2), ModifyColor(c, XMFLOAT3(x2, y2, z2)), z2, y2));
		AddQuad(Vertex(XMFLOAT3(x1, y1, z1), ModifyColor(c, XMFLOAT3(x1, y1, z1)), x1, y1),
			    Vertex(XMFLOAT3(x2, y1, z1), ModifyColor(c, XMFLOAT3(x2, y1, z1)), x2, y1),
			    Vertex(XMFLOAT3(x1, y2, z1), ModifyColor(c, XMFLOAT3(x1, y2, z1)), x1, y2),
			    Vertex(XMFLOAT3(x2, y2, z1), ModifyColor(c, XMFLOAT3(x2, y2, z1)), x2, y2));
		AddQuad(Vertex(XMFLOAT3(x2, y1, z2), ModifyColor(c, XMFLOAT3(x2, y1, z2)), x2, y1),
			    Vertex(XMFLOAT3(x1, y1, z2), ModifyColor(c, XMFLOAT3(x1, y1, z2)), x1, y1),
			    Vertex(XMFLOAT3(x2, y2, z2), ModifyColor(c, XMFLOAT3(x2, y2, z2)), x2, y2),
			    Vertex(XMFLOAT3(x1, y2, z2), ModifyColor(c, XMFLOAT3(x1, y2, z2)), x1, y2));
	}
};

//----------------------------------------------------------------------
struct Model
{
	XMFLOAT3     Pos; 
	XMFLOAT4     Rot; 
	Material   * Fill;
	DataBuffer * VertexBuffer;
	DataBuffer * IndexBuffer;
	int          NumIndices;

	Model() : Fill(nullptr), VertexBuffer(nullptr), IndexBuffer(nullptr) {};
    void Init(TriangleSet * t)
    {
		NumIndices = t->numIndices;
		VertexBuffer = new DataBuffer(DIRECTX.Device, D3D11_BIND_VERTEX_BUFFER, &t->Vertices[0], t->numVertices * sizeof(Vertex));
		IndexBuffer = new DataBuffer(DIRECTX.Device, D3D11_BIND_INDEX_BUFFER, &t->Indices[0], t->numIndices * sizeof(short));
    }
	Model(TriangleSet * t, XMFLOAT3 argPos, XMFLOAT4 argRot, Material * argFill) :
        Pos(argPos),
        Rot(argRot),
        Fill(argFill)
	{
        Init(t);
	}
    // 2D scenes, for latency tester and full screen copies, etc
	Model(Material * mat, float minx, float miny, float maxx, float maxy,  float zDepth = 0) :
        Pos(XMFLOAT3(0, 0, 0)),
        Rot(XMFLOAT4(0, 0, 0, 1)),
        Fill(mat)
	{
		TriangleSet quad;
		quad.AddQuad(Vertex(XMFLOAT3(minx, miny, zDepth), 0xffffffff, 0, 1),
			Vertex(XMFLOAT3(minx, maxy, zDepth), 0xffffffff, 0, 0),
			Vertex(XMFLOAT3(maxx, miny, zDepth), 0xffffffff, 1, 1),
			Vertex(XMFLOAT3(maxx, maxy, zDepth), 0xffffffff, 1, 0));
        Init(&quad);
	}
    ~Model()
    {
        delete Fill; Fill = nullptr;
        delete VertexBuffer; VertexBuffer = nullptr;
        delete IndexBuffer; IndexBuffer = nullptr;
    }

	void Render(XMMATRIX * projView, float R, float G, float B, float A, bool standardUniforms)
	{
		XMMATRIX modelMat = XMMatrixMultiply(XMMatrixRotationQuaternion(XMLoadFloat4(&Rot)), XMMatrixTranslationFromVector(XMLoadFloat3(&Pos)));
		XMMATRIX mat = XMMatrixMultiply(modelMat, *projView);
		float col[] = { R, G, B, A };
		if (standardUniforms) memcpy(DIRECTX.UniformData + 0, &mat, 64); // ProjView
		if (standardUniforms) memcpy(DIRECTX.UniformData + 64, &col, 16); // MasterCol
		D3D11_MAPPED_SUBRESOURCE map;
		DIRECTX.Context->Map(DIRECTX.UniformBufferGen->D3DBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
		memcpy(map.pData, &DIRECTX.UniformData, DIRECTX.UNIFORM_DATA_SIZE);
		DIRECTX.Context->Unmap(DIRECTX.UniformBufferGen->D3DBuffer, 0);
		DIRECTX.Context->IASetInputLayout(Fill->InputLayout);
		DIRECTX.Context->IASetIndexBuffer(IndexBuffer->D3DBuffer, DXGI_FORMAT_R16_UINT, 0);
		UINT offset = 0;
        ID3D11Buffer* vbs[] = { VertexBuffer->D3DBuffer };
		DIRECTX.Context->IASetVertexBuffers(0, std::distance(std::begin(vbs), std::end(vbs)), vbs, &Fill->VertexSize, &offset);
		DIRECTX.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		DIRECTX.Context->VSSetShader(Fill->D3DVert, NULL, 0);
		DIRECTX.Context->PSSetShader(Fill->D3DPix, NULL, 0);
        ID3D11SamplerState* samplerStates[] = { Fill->SamplerState };
		DIRECTX.Context->PSSetSamplers(0, std::distance(std::begin(samplerStates), std::end(samplerStates)), samplerStates);
		DIRECTX.Context->RSSetState(Fill->Rasterizer);
		DIRECTX.Context->OMSetDepthStencilState(Fill->DepthState, 0);
		DIRECTX.Context->OMSetBlendState(Fill->BlendState, NULL, 0xffffffff);
        ID3D11ShaderResourceView* texSrvs[] = { Fill->Tex->TexSv };
		DIRECTX.Context->PSSetShaderResources(0, std::distance(std::begin(texSrvs), std::end(texSrvs)), texSrvs);
		DIRECTX.Context->DrawIndexed((UINT)NumIndices, 0, 0);
	}
};

//------------------------------------------------------------------------- 
struct Scene
{
    static const int MAX_MODELS = 100;
	Model *Models[MAX_MODELS];
    int numModels;

	void Add(Model * n)
    {
        if (numModels < MAX_MODELS)
            Models[numModels++] = n;
    }

	void Render(XMMATRIX * projView, float R, float G, float B, float A, bool standardUniforms)
	{
		for (int i = 0; i < numModels; ++i)
            Models[i]->Render(projView, R, G, B, A, standardUniforms);
	}
    
    void Init(bool includeIntensiveGPUobject)
	{
		TriangleSet cube;
		cube.AddSolidColorBox(0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0xff404040);
		Add(
            new Model(&cube, XMFLOAT3(0, 0, 0), XMFLOAT4(0, 0, 0, 1),
                new Material(
                    new Texture(false, 256, 256, Texture::AUTO_CEILING)
                )
            )
        );

		TriangleSet spareCube;
		spareCube.AddSolidColorBox(0.1f, -0.1f, 0.1f, -0.1f, +0.1f, -0.1f, 0xffff0000);
		Add(
            new Model(&spareCube, XMFLOAT3(0, -10, 0), XMFLOAT4(0, 0, 0, 1),
                new Material(
                    new Texture(false, 256, 256, Texture::AUTO_CEILING)
                )
            )
        );

		TriangleSet walls;
		walls.AddSolidColorBox(10.1f, 0.0f, 20.0f, 10.0f, 4.0f, -20.0f, 0xff808080);  // Left Wall
		walls.AddSolidColorBox(10.0f, -0.1f, 20.1f, -10.0f, 4.0f, 20.0f, 0xff808080); // Back Wall
		walls.AddSolidColorBox(-10.0f, -0.1f, 20.0f, -10.1f, 4.0f, -20.0f, 0xff808080);   // Right Wall
		Add(
            new Model(&walls, XMFLOAT3(0, 0, 0), XMFLOAT4(0, 0, 0, 1),
                new Material(
                    new Texture(false, 256, 256, Texture::AUTO_WALL)
                )
            )
        );

		if (includeIntensiveGPUobject)
		{
			TriangleSet partitions;
			for (float depth = 0.0f; depth > -3.0f; depth -= 0.1f)
				partitions.AddSolidColorBox(9.0f, 0.5f, -depth, -9.0f, 3.5f, -depth, 0x10ff80ff); // Partition
			Add(
                new Model(&partitions, XMFLOAT3(0, 0, 0), XMFLOAT4(0, 0, 0, 1),
                    new Material(
                        new Texture(false, 256, 256, Texture::AUTO_FLOOR)
                    )
                )
            ); // Floors
		}

		TriangleSet floors;
		floors.AddSolidColorBox(10.0f, -0.1f, 20.0f, -10.0f, 0.0f, -20.1f, 0xff808080); // Main floor
		floors.AddSolidColorBox(15.0f, -6.1f, -18.0f, -15.0f, -6.0f, -30.0f, 0xff808080); // Bottom floor
		Add(
            new Model(&floors, XMFLOAT3(0, 0, 0), XMFLOAT4(0, 0, 0, 1),
                new Material(
                    new Texture(false, 256, 256, Texture::AUTO_FLOOR)
                )
            )
        ); // Floors

		TriangleSet ceiling;
		ceiling.AddSolidColorBox(10.0f, 4.0f, 20.0f, -10.0f, 4.1f, -20.1f, 0xff808080);
		Add(
            new Model(&ceiling, XMFLOAT3(0, 0, 0), XMFLOAT4(0, 0, 0, 1),
                new Material(
                    new Texture(false, 256, 256, Texture::AUTO_CEILING)
                )
            )
        ); // Ceiling

		TriangleSet furniture;
		furniture.AddSolidColorBox(-9.5f, 0.75f, -3.0f, -10.1f, 2.5f, -3.1f, 0xff383838);    // Right side shelf// Verticals
		furniture.AddSolidColorBox(-9.5f, 0.95f, -3.7f, -10.1f, 2.75f, -3.8f, 0xff383838);   // Right side shelf
		furniture.AddSolidColorBox(-9.55f, 1.20f, -2.5f, -10.1f, 1.30f, -3.75f, 0xff383838); // Right side shelf// Horizontals
		furniture.AddSolidColorBox(-9.55f, 2.00f, -3.05f, -10.1f, 2.10f, -4.2f, 0xff383838); // Right side shelf
		furniture.AddSolidColorBox(-5.0f, 1.1f, -20.0f, -10.0f, 1.2f, -20.1f, 0xff383838);   // Right railing   
		furniture.AddSolidColorBox(10.0f, 1.1f, -20.0f, 5.0f, 1.2f, -20.1f, 0xff383838);   // Left railing  
		for (float f = 5; f <= 9; f += 1)
            furniture.AddSolidColorBox(-f, 0.0f, -20.0f, -f - 0.1f, 1.1f, -20.1f, 0xff505050); // Left Bars
		for (float f = 5; f <= 9; f += 1)
            furniture.AddSolidColorBox(f, 1.1f, -20.0f, f + 0.1f, 0.0f, -20.1f, 0xff505050); // Right Bars
		furniture.AddSolidColorBox(1.8f, 0.8f, -1.0f, 0.0f, 0.7f, 0.0f, 0xff505000);  // Table
		furniture.AddSolidColorBox(1.8f, 0.0f, 0.0f, 1.7f, 0.7f, -0.1f, 0xff505000); // Table Leg 
		furniture.AddSolidColorBox(1.8f, 0.7f, -1.0f, 1.7f, 0.0f, -0.9f, 0xff505000); // Table Leg 
		furniture.AddSolidColorBox(0.0f, 0.0f, -1.0f, 0.1f, 0.7f, -0.9f, 0xff505000);  // Table Leg 
		furniture.AddSolidColorBox(0.0f, 0.7f, 0.0f, 0.1f, 0.0f, -0.1f, 0xff505000);  // Table Leg 
		furniture.AddSolidColorBox(1.4f, 0.5f, 1.1f, 0.8f, 0.55f, 0.5f, 0xff202050);  // Chair Set
		furniture.AddSolidColorBox(1.401f, 0.0f, 1.101f, 1.339f, 1.0f, 1.039f, 0xff202050); // Chair Leg 1
		furniture.AddSolidColorBox(1.401f, 0.5f, 0.499f, 1.339f, 0.0f, 0.561f, 0xff202050); // Chair Leg 2
		furniture.AddSolidColorBox(0.799f, 0.0f, 0.499f, 0.861f, 0.5f, 0.561f, 0xff202050); // Chair Leg 2
		furniture.AddSolidColorBox(0.799f, 1.0f, 1.101f, 0.861f, 0.0f, 1.039f, 0xff202050); // Chair Leg 2
		furniture.AddSolidColorBox(1.4f, 0.97f, 1.05f, 0.8f, 0.92f, 1.10f, 0xff202050); // Chair Back high bar
		for (float f = 3.0f; f <= 6.6f; f += 0.4f)
            furniture.AddSolidColorBox(3, 0.0f, -f, 2.9f, 1.3f, -f - 0.1f, 0xff404040); // Posts
		Add(
            new Model(&furniture, XMFLOAT3(0, 0, 0), XMFLOAT4(0, 0, 0, 1),
                new Material(
                    new Texture(false, 256, 256, Texture::AUTO_WHITE)
                )
            )
        ); // Fixtures & furniture
	}

	Scene() : numModels(0) {}
	Scene(bool includeIntensiveGPUobject) :
        numModels(0)
    {
        Init(includeIntensiveGPUobject);
    }
    void Release()
    {
        while (numModels-- > 0)
            delete Models[numModels];
    }
    ~Scene()
    {
        Release();
    }
};

//-----------------------------------------------------------
struct Camera
{
	XMVECTOR Pos;
	XMVECTOR Rot;
	Camera() {};
	Camera(XMVECTOR * pos, XMVECTOR * rot) : Pos(*pos), Rot(*rot)	{};
	XMMATRIX GetViewMatrix()
	{
		XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, -1, 0), Rot);
		return(XMMatrixLookAtRH(Pos, XMVectorAdd(Pos, forward), XMVector3Rotate(XMVectorSet(0, 1, 0, 0), Rot)));
	}
};

//----------------------------------------------------
struct Utility
{
	void Output(const char * fnt, ...)
	{
		static char string_text[1000];
		va_list args; va_start(args, fnt);
		vsprintf_s(string_text, fnt, args);
		va_end(args);
		OutputDebugStringA(string_text);
	}
} static Util;

#endif // OVR_Win32_DirectXAppUtil_h





/*

	//Done with other file loading, lets open file for output.
	SYSTEMTIME syst;
	GetSystemTime(&syst);
	FILE * reportFile;
	char fileName[100];
	sprintf_s(fileName,100,"report_Date(%02d_%02d_%04d)_Time(%02d_%02d_%02d).txt",syst.wMonth,syst.wDay,syst.wYear,syst.wHour,syst.wMinute,syst.wSecond);
	fopen_s(&reportFile, File.Path(fileName), "wt");

	//....and lets also open Parse
	ParseRecord t;
	t.OpenRecord(L"ugO4eVcwGiZHMvk7334Me73pY8FOLY67GBgejoMn", L"sfEZ2E3Uf8bbdkEcvovjdQjTdOYaXPxMvISnWDEZ", L"Report");

	//And write the first data into both
	fprintf(reportFile,"----Comfort testing report file----     (View with Wordwrap off)\n\n");
	fprintf(reportFile,"Code version    = %s\n",ComfortTestingCodeVersion);
	fprintf(reportFile,"Headset         = %s\n",basicVR.HMD->ProductName);
	fprintf(reportFile,"Serial number   = %s\n",basicVR.HMD->SerialNumber);
	fprintf(reportFile,"Firmware        = %d.%d\n",basicVR.HMD->FirmwareMajor,basicVR.HMD->FirmwareMinor);
	fprintf(reportFile,"Manufacturer    = %s\n",basicVR.HMD->Manufacturer);
	fprintf(reportFile,"Resolution      = %d x %d\n",basicVR.HMD->Resolution.w,basicVR.HMD->Resolution.h);
	fprintf(reportFile,"Date(US format) = %02d/%02d/%04d\n",syst.wMonth,syst.wDay,syst.wYear);
	fprintf(reportFile,"Time            = %02d:%02d:%02d\n",syst.wHour,syst.wMinute,syst.wSecond);
	fprintf(reportFile,"User            = %s\n",ovr_GetString(basicVR.HMD, OVR_KEY_USER, "Unknown"));
	fprintf(reportFile,"Name            = %s\n",ovr_GetString(basicVR.HMD, OVR_KEY_NAME, "Unknown"));
	fprintf(reportFile,"Gender          = %s\n",ovr_GetString(basicVR.HMD, OVR_KEY_GENDER, "Unknown"));
	fprintf(reportFile,"Player height   = %f metres\n",ovr_GetFloat(basicVR.HMD, OVR_KEY_PLAYER_HEIGHT, -1));
	fprintf(reportFile,"Eye height      = %f metres\n",ovr_GetFloat(basicVR.HMD, OVR_KEY_EYE_HEIGHT, -1));
	fprintf(reportFile,"IPD             = %f metres\n",ovr_GetFloat(basicVR.HMD, OVR_KEY_IPD, -1));
	fprintf(reportFile,"Eye relief dial = %d\n",ovr_GetInt(basicVR.HMD, OVR_KEY_EYE_RELIEF_DIAL, -1));

	t.AddData("Code version", ComfortTestingCodeVersion);
//	t.AddData("Headset",      "%s",basicVR.HMD->ProductName);
//	t.AddData("Serial number","%s",basicVR.HMD->SerialNumber);

	t.SendData();


*/