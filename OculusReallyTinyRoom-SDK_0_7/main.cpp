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

// Include DirectX
#include "Win32_DirectXAppUtil.h"

// Include the Oculus SDK
#include <OVR_CAPI_D3D.h>

// ovrSwapTextureSet wrapper class that also maintains the render target views needed for D3D11
// rendering.
struct OculusTexture {
    std::unique_ptr<ovrSwapTextureSet, std::function<void(ovrSwapTextureSet*)>> TextureSet;
    ID3D11RenderTargetViewPtr TexRtv[2];

    OculusTexture(ovrHmd hmd, ovrSizei size)
        : TextureSet{[hmd, size, &texRtv = TexRtv] {
                         // Create and validate the swap texture set and stash it in unique_ptr
                         CD3D11_TEXTURE2D_DESC dsDesc(
                             DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, size.w, size.h, 1, 1,
                             D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);

                         ovrSwapTextureSet* ts{};
                         auto result = ovr_CreateSwapTextureSetD3D11(
                             hmd, DIRECTX.Device, &dsDesc, ovrSwapTextureSetD3D11_Typeless, &ts);
                         VALIDATE(OVR_SUCCESS(result), "Failed to create SwapTextureSet.");
                         VALIDATE(ts->TextureCount == std::size(texRtv), "TextureCount mismatch.");
                         return ts;
                     }(),
                     // unique_ptr Deleter lambda to clean up the swap texture set
                     [hmd](ovrSwapTextureSet* ts) { ovr_DestroySwapTextureSet(hmd, ts); }} {
        // Create render target views for each of the textures in the swap texture set
        std::transform(TextureSet->Textures, TextureSet->Textures + TextureSet->TextureCount,
                       TexRtv, [](auto tex) {
                           CD3D11_RENDER_TARGET_VIEW_DESC rtvd(D3D11_RTV_DIMENSION_TEXTURE2D,
                                                               DXGI_FORMAT_R8G8B8A8_UNORM);
                           ID3D11RenderTargetViewPtr rtv;
                           DIRECTX.Device->CreateRenderTargetView(
                               reinterpret_cast<ovrD3D11Texture&>(tex).D3D11.pTexture, &rtvd, &rtv);
                           return rtv;
                       });
    }

    auto AdvanceToNextTexture() {
        return TextureSet->CurrentIndex = (TextureSet->CurrentIndex + 1) % TextureSet->TextureCount;
    }
};

// Helper to wrap ovr types like ovrHmd and ovrTexture* in a unique_ptr with custom create / destroy
template <typename CreateFunc, typename DestroyFunc>
auto create_unique(CreateFunc createFunc, DestroyFunc destroyFunc) {
    return std::unique_ptr<std::remove_pointer_t<std::result_of_t<CreateFunc()>>, DestroyFunc>{
        createFunc(), destroyFunc};
}

// return true to retry later (e.g. after display lost)
static bool MainLoop(bool retryCreate) {
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
    if (!DIRECTX.InitDevice(hmdDesc.Resolution.w / 2, hmdDesc.Resolution.h / 2,
                            reinterpret_cast<LUID*>(&luid)))
        return retryCreate;

    // Start the sensor which informs of the Rift's pose and motion
    result = ovr_ConfigureTracking(
        HMD.get(),
        ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection | ovrTrackingCap_Position, 0);
    VALIDATE(OVR_SUCCESS(result), "Failed to configure tracking.");

    // Make the eye render buffers (caution if actual size < requested due to HW limits).
    ovrSizei idealSizes[] = {
        ovr_GetFovTextureSize(HMD.get(), ovrEye_Left, hmdDesc.DefaultEyeFov[ovrEye_Left], 1.0f),
        ovr_GetFovTextureSize(HMD.get(), ovrEye_Right, hmdDesc.DefaultEyeFov[ovrEye_Right], 1.0f)};
    OculusTexture EyeRenderTexture[] = {{HMD.get(), idealSizes[0]}, {HMD.get(), idealSizes[1]}};
    DepthBuffer EyeDepthBuffer[] = {{DIRECTX.Device, idealSizes[0]},
                                    {DIRECTX.Device, idealSizes[1]}};
    ovrRecti eyeRenderViewport[] = {{{0, 0}, idealSizes[0]}, {{0, 0}, idealSizes[1]}};

    // Create a mirror to see on the monitor, stash it in a unique_ptr for automatic cleanup.
    auto mirrorTexture =
        create_unique([&result, hmd = HMD.get() ] {
            CD3D11_TEXTURE2D_DESC td(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DIRECTX.WinSizeW,
                                     DIRECTX.WinSizeH, 1, 1);
            ovrTexture* mirrorTexture{};
            result = ovr_CreateMirrorTextureD3D11(hmd, DIRECTX.Device, &td, 0, &mirrorTexture);
            return mirrorTexture;
        },
                      [hmd = HMD.get()](ovrTexture * mt) { ovr_DestroyMirrorTexture(hmd, mt); });
    VALIDATE(OVR_SUCCESS(result), "Failed to create mirror texture.");

    // Initialize the scene
    auto roomScene = Scene{};

    auto mainCam = Camera{XMVectorSet(0.0f, 1.6f, 5.0f, 0), XMQuaternionIdentity()};

    ovrEyeRenderDesc eyeRenderDesc[] = {
        ovr_GetRenderDesc(HMD.get(), ovrEye_Left, hmdDesc.DefaultEyeFov[ovrEye_Left]),
        ovr_GetRenderDesc(HMD.get(), ovrEye_Right, hmdDesc.DefaultEyeFov[ovrEye_Right])};

    auto isVisible = true;

    // Main loop
    while (DIRECTX.HandleMessages()) {
        // Handle input
        [&mainCam] {
            const auto forward = XMVector3Rotate(XMVectorSet(0, 0, -0.05f, 0), mainCam.Rot);
            const auto right = XMVector3Rotate(XMVectorSet(0.05f, 0, 0, 0), mainCam.Rot);
            if (DIRECTX.Key['W'] || DIRECTX.Key[VK_UP])
                mainCam.Pos = XMVectorAdd(mainCam.Pos, forward);
            if (DIRECTX.Key['S'] || DIRECTX.Key[VK_DOWN])
                mainCam.Pos = XMVectorSubtract(mainCam.Pos, forward);
            if (DIRECTX.Key['D']) mainCam.Pos = XMVectorAdd(mainCam.Pos, right);
            if (DIRECTX.Key['A']) mainCam.Pos = XMVectorSubtract(mainCam.Pos, right);
            static auto Yaw = 0.0f;
            if (DIRECTX.Key[VK_LEFT])
                mainCam.Rot = XMQuaternionRotationRollPitchYaw(0, Yaw += 0.02f, 0);
            if (DIRECTX.Key[VK_RIGHT])
                mainCam.Rot = XMQuaternionRotationRollPitchYaw(0, Yaw -= 0.02f, 0);
        }();

        // Animate the cube
        [&cube = roomScene.Models[0]] {
            static auto cubeClock = 0.0f;
            cube->Pos = XMFLOAT3(9 * sin(cubeClock), 3, 9 * cos(cubeClock += 0.015f));
        }();

        // Get both eye poses simultaneously, with IPD offset already included.
        ovrPosef EyeRenderPose[2] = {};
        [&EyeRenderPose, hmd = HMD.get(), &eyeRenderDesc ] {
            const auto ftiming = ovr_GetFrameTiming(hmd, 0);
            const auto hmdState = ovr_GetTrackingState(hmd, ftiming.DisplayMidpointSeconds);
            const ovrVector3f HmdToEyeViewOffset[] = {
                eyeRenderDesc[ovrEye_Left].HmdToEyeViewOffset,
                eyeRenderDesc[ovrEye_Right].HmdToEyeViewOffset};
            ovr_CalcEyePoses(hmdState.HeadPose.ThePose, HmdToEyeViewOffset, EyeRenderPose);
        }();

        // Render Scene to Eye Buffers
        if (isVisible) {
            for (int eye = 0; eye < 2; ++eye) {
                // Increment to use next texture, just before writing
                const auto texIndex = EyeRenderTexture[eye].AdvanceToNextTexture();
                DIRECTX.SetAndClearRenderTarget(EyeRenderTexture[eye].TexRtv[texIndex],
                                                &EyeDepthBuffer[eye]);
                DIRECTX.SetViewport(eyeRenderViewport[eye]);

                // Get the pose information in XM format
                const auto& ori = EyeRenderPose[eye].Orientation;
                const auto eyeQuat = XMVectorSet(ori.x, ori.y, ori.z, ori.w);
                const auto& pos = EyeRenderPose[eye].Position;
                const auto eyePos = XMVectorSet(pos.x, pos.y, pos.z, 0);

                // Get view and projection matrices for the Rift camera
                const auto CombinedPos =
                    XMVectorAdd(mainCam.Pos, XMVector3Rotate(eyePos, mainCam.Rot));
                const auto finalCam =
                    Camera{CombinedPos, XMQuaternionMultiply(eyeQuat, mainCam.Rot)};
                const auto p = ovrMatrix4f_Projection(eyeRenderDesc[eye].Fov, 0.2f, 1000.0f,
                                                      ovrProjection_RightHanded);
                const auto xp = XMFLOAT4X4{&p.M[0][0]};
                const auto proj = XMMatrixTranspose(XMLoadFloat4x4(&xp));
                roomScene.Render(XMMatrixMultiply(finalCam.GetViewMatrix(), proj));
            }
        }

        // Initialize our single full screen Fov layer.
        auto ld = ovrLayerEyeFov{};
        ld.Header.Type = ovrLayerType_EyeFov;
        ld.Header.Flags = 0;

        for (int eye = 0; eye < 2; ++eye) {
            ld.ColorTexture[eye] = EyeRenderTexture[eye].TextureSet.get();
            ld.Viewport[eye] = eyeRenderViewport[eye];
            ld.Fov[eye] = hmdDesc.DefaultEyeFov[eye];
            ld.RenderPose[eye] = EyeRenderPose[eye];
        }

        auto layers = &ld.Header;
        result = ovr_SubmitFrame(HMD.get(), 0, nullptr, &layers, 1);
        // exit the rendering loop if submit returns an error, will retry on ovrError_DisplayLost
        if (!OVR_SUCCESS(result)) return retryCreate;

        isVisible = result == ovrSuccess;

        // Render mirror
        DIRECTX.Context->CopyResource(
            DIRECTX.BackBuffer,
            reinterpret_cast<ovrD3D11Texture*>(mirrorTexture.get())->D3D11.pTexture);
        DIRECTX.SwapChain->Present(0, 0);
    }

    // Retry on ovrError_DisplayLost
    return retryCreate || OVR_SUCCESS(result) || (result == ovrError_DisplayLost);
}

//-------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int) {
    // Initializes LibOVR, and the Rift
    auto result = ovr_Initialize(nullptr);
    VALIDATE(OVR_SUCCESS(result), "Failed to initialize libOVR.");

    VALIDATE(DIRECTX.InitWindow(hinst, L"Oculus Room Tiny (DX11)"), "Failed to open window.");

    DIRECTX.Run(MainLoop);

    ovr_Shutdown();
}
