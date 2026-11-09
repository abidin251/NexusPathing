import { CppSourceFile } from '../types/pathing';

export const CPP_SOURCE_FILES: CppSourceFile[] = [
  {
    path: 'include/Nexus/Nexus.h',
    filename: 'Nexus.h',
    category: 'Core & Entry',
    language: 'cpp',
    description: 'Official Raidcore Nexus C++ API header defining AddonDefinition_t, AddonAPI_t, MumbleLink_t, and D3D11 render hook signatures.',
    blishEquivalent: 'BlishHUD.Modules.Module (Core SDK)',
    linesCount: 148,
    code: `#pragma once
#ifndef NEXUS_API_H
#define NEXUS_API_H

#include <windows.h>
#include <d3d11.h>
#include <stdint.h>

// Raidcore Nexus API v6 Contract
#define NEXUS_API_VERSION 6

enum ERenderType : int32_t {
    RT_PreRender  = 0,
    RT_Render     = 1,
    RT_PostRender = 2,
    RT_OptionsRender = 3
};

enum EAddonFlags : uint32_t {
    AF_None             = 0,
    AF_IsVolatile       = 1 << 0,
    AF_DisableHotloading = 1 << 1,
    AF_OnlyLoadDuringGameLaunchSequence = 1 << 2
};

enum EUpdateProvider : int32_t {
    UP_None       = 0,
    UP_Raidcore   = 1,
    UP_GitHub     = 2,
    UP_DirectLink = 3
};

enum ELogLevel : int32_t {
    LOGL_CRITICAL = 1,
    LOGL_WARNING  = 2,
    LOGL_INFO     = 3,
    LOGL_DEBUG    = 4,
    LOGL_TRACE    = 5
};

typedef void (*GUI_RENDER)();
typedef void (*ADDON_LOAD)(struct AddonAPI_t* aApi);
typedef void (*ADDON_UNLOAD)();
typedef void (*EVENT_CONSUME)(void* aEventArgs);
typedef void (*KEYBIND_HANDLER)(const char* aIdentifier, bool aIsRelease);

struct AddonVersion_t {
    int16_t Major;
    int16_t Minor;
    int16_t Build;
    int16_t Revision;
};

struct AddonDefinition_t {
    int32_t         Signature;      // Unique negative or positive Raidcore addon ID
    int32_t         APIVersion;     // NEXUS_API_VERSION (6)
    const char*     Name;           // "NexusPathing (Blish HUD Pathing Port)"
    AddonVersion_t  Version;
    const char*     Author;
    const char*     Description;
    ADDON_LOAD      Load;
    ADDON_UNLOAD    Unload;
    EAddonFlags     Flags;
    EUpdateProvider Provider;
    const char*     UpdateLink;
};

struct Texture_t {
    uint32_t                    Width;
    uint32_t                    Height;
    ID3D11ShaderResourceView*   Resource;
};

typedef void (*TEXTURE_RECEIVER)(const char* aIdentifier, Texture_t* aTexture);

struct MumbleVector3_t {
    float X;
    float Y;
    float Z;
};

struct MumbleContext_t {
    unsigned char   ServerAddress[28];
    uint32_t        MapId;
    uint32_t        MapType;
    uint32_t        ShardId;
    uint32_t        Instance;
    uint32_t        BuildId;
    uint32_t        UiState; // Bitmask: Bit0=MapOpen, Bit1=CompassTopRight, Bit2=CompassRot, Bit3=GameFocus, Bit4=Competitive, Bit5=TextboxFocus, Bit6=InCombat
    uint16_t        CompassWidth;
    uint16_t        CompassHeight;
    float           CompassRotation;
    float           PlayerX;
    float           PlayerY;
    float           MapCenterX;
    float           MapCenterY;
    float           MapScale;
    uint32_t        ProcessId;
    uint8_t         MountIndex;
};

struct MumbleLink_t {
    uint32_t        UiVersion;
    uint32_t        UiTick;
    MumbleVector3_t AvatarPosition;
    MumbleVector3_t AvatarFront;
    MumbleVector3_t AvatarTop;
    wchar_t         Name[256];
    MumbleVector3_t CameraPosition;
    MumbleVector3_t CameraFront;
    MumbleVector3_t CameraTop;
    wchar_t         Identity[256];
    uint32_t        ContextLen;
    MumbleContext_t Context;
    wchar_t         Description[2048];
};

typedef struct AddonAPI_t
{
    /* Renderer */
    void* SwapChain;    // IDXGISwapChain*
    void* ImguiContext; // ImGuiContext*
    void* ImguiMalloc;
    void* ImguiFree;

    GUI_ADDRENDER                     GUI_Register;
    GUI_REMRENDER                     GUI_Deregister;

    UPDATER_REQUESTUPDATE             RequestUpdate;

    /* Logging */
    LOGGER_LOG                        Log;

    ALERTS_NOTIFY                     GUI_SendAlert;
    GUI_REGISTERCLOSEONESCAPE         GUI_RegisterCloseOnEscape;
    GUI_DEREGISTERCLOSEONESCAPE       GUI_DeregisterCloseOnEscape;

    PATHS_GETGAMEDIR                  Paths_GetGameDirectory;
    PATHS_GETADDONDIR                 Paths_GetAddonDirectory;
    PATHS_GETCOMMONDIR                Paths_GetCommonDirectory;

    MINHOOK_CREATE                    MinHook_Create;
    MINHOOK_REMOVE                    MinHook_Remove;
    MINHOOK_ENABLE                    MinHook_Enable;
    MINHOOK_DISABLE                   MinHook_Disable;

    EVENTS_RAISE                      Events_Raise;
    EVENTS_RAISENOTIFICATION          Events_RaiseNotification;
    EVENTS_RAISE_TARGETED             Events_RaiseTargeted;
    EVENTS_RAISENOTIFICATION_TARGETED Events_RaiseNotificationTargeted;
    EVENTS_SUBSCRIBE                  Events_Subscribe;
    EVENTS_SUBSCRIBE                  Events_Unsubscribe;

    WNDPROC_ADDREM                    WndProc_Register;
    WNDPROC_ADDREM                    WndProc_Deregister;
    WNDPROC_SENDTOGAME                WndProc_SendToGameOnly;

    INPUTBINDS_INVOKE                 InputBinds_Invoke;
    INPUTBINDS_REGISTERWITHSTRING     InputBinds_RegisterWithString;
    INPUTBINDS_REGISTERWITHSTRUCT     InputBinds_RegisterWithStruct;
    INPUTBINDS_DEREGISTER             InputBinds_Deregister;

    GAMEBINDS_PRESSASYNC              GameBinds_PressAsync;
    GAMEBINDS_RELEASEASYNC            GameBinds_ReleaseAsync;
    GAMEBINDS_INVOKEASYNC             GameBinds_InvokeAsync;
    GAMEBINDS_PRESS                   GameBinds_Press;
    GAMEBINDS_RELEASE                 GameBinds_Release;
    GAMEBINDS_ISBOUND                 GameBinds_IsBound;

    DATALINK_GET                      DataLink_Get;
    DATALINK_SHARE                    DataLink_Share;

    TEXTURES_GET                      Textures_Get;
    TEXTURES_GETORCREATEFROMFILE      Textures_GetOrCreateFromFile;
    TEXTURES_GETORCREATEFROMRESOURCE  Textures_GetOrCreateFromResource;
    TEXTURES_GETORCREATEFROMURL       Textures_GetOrCreateFromURL;
    TEXTURES_GETORCREATEFROMMEMORY    Textures_GetOrCreateFromMemory;
    TEXTURES_LOADFROMFILE             Textures_LoadFromFile;
    TEXTURES_LOADFROMRESOURCE         Textures_LoadFromResource;
    TEXTURES_LOADFROMURL              Textures_LoadFromURL;
    TEXTURES_LOADFROMMEMORY           Textures_LoadFromMemory;

    QUICKACCESS_ADDSHORTCUT           QuickAccess_Add;
    QUICKACCESS_GENERIC               QuickAccess_Remove;
    QUICKACCESS_GENERIC               QuickAccess_Notify;
    QUICKACCESS_ADDSIMPLE             QuickAccess_AddContextMenu;
    QUICKACCESS_GENERIC               QuickAccess_RemoveContextMenu;

    LOCALIZATION_TRANSLATE            Localization_Translate;
    LOCALIZATION_TRANSLATETO          Localization_TranslateTo;
    LOCALIZATION_SET                  Localization_Set;

    FONTS_GETRELEASE                  Fonts_Get;
    FONTS_GETRELEASE                  Fonts_Release;
    FONTS_ADDFROMFILE                 Fonts_AddFromFile;
    FONTS_ADDFROMRESOURCE             Fonts_AddFromResource;
    FONTS_ADDFROMMEMORY               Fonts_AddFromMemory;
    FONTS_RESIZE                      Fonts_Resize;
} AddonAPI_t;

#endif // NEXUS_API_H`,
  },
  {
    path: 'src/dllmain.cpp',
    filename: 'dllmain.cpp',
    category: 'Core & Entry',
    language: 'cpp',
    description: 'Raidcore Nexus DLL entry point implementing GetAddonDef(), AddonLoad(), AddonUnload(), MumbleLink attachment, and keybind registration.',
    blishEquivalent: 'PathingModule.cs',
    linesCount: 162,
    code: `#include "../include/Nexus/Nexus.h"
#include "Renderer/D3D11PathingRenderer.h"
#include "Pack/PackManager.h"
#include "Behaviors/BehaviorEngine.h"
#include "Scripting/LuaPathingVM.h"
#include "UI/NexusPathingImGui.h"
#include <imgui.h>
#include <memory>
#include <filesystem>

namespace NexusPathing {
    AddonAPI_t*                         g_API = nullptr;
    MumbleLink_t*                       g_Mumble = nullptr;
    void*                               g_NexusLink = nullptr;
    std::unique_ptr<D3D11PathingRenderer> g_Renderer;
    std::unique_ptr<PackManager>        g_PackManager;
    std::unique_ptr<BehaviorEngine>     g_BehaviorEngine;
    std::unique_ptr<LuaPathingVM>       g_LuaVM;
    std::unique_ptr<NexusPathingImGui>  g_UI;
}

using namespace NexusPathing;

static void OnInteractKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease || !g_BehaviorEngine || !g_Mumble) return;
    if (strcmp(aIdentifier, "KB_NEXUSPATHING_INTERACT") == 0) {
        g_BehaviorEngine->TriggerFocusedMarkers(*g_Mumble);
    } else if (strcmp(aIdentifier, "KB_NEXUSPATHING_TOGGLE_UI") == 0) {
        if (g_UI) g_UI->ToggleMainWindow();
    }
}

static void OnPreRender() {
    if (!g_Mumble || !g_PackManager || !g_BehaviorEngine) return;
    // Check map changes and update behavior states + Lua tick
    g_BehaviorEngine->UpdateFrame(*g_Mumble, 1.0f / 60.0f);
    if (g_LuaVM) {
        g_LuaVM->TickFrame(*g_Mumble, 1.0f / 60.0f);
    }
}

static void OnRender() {
    if (!g_Mumble || !g_Renderer || !g_PackManager) return;

    // 1. Render 3D world-space markers and animated trails before ImGui overlay
    const bool isMapOpen = (g_Mumble->Context.UiState & (1 << 0)) != 0;
    const bool hasGameFocus = (g_Mumble->Context.UiState & (1 << 3)) != 0;

    if (!isMapOpen && hasGameFocus) {
        g_Renderer->RenderWorldSpace(*g_Mumble, g_PackManager->GetActiveMapMarkers(), g_PackManager->GetActiveMapTrails());
    }

    // 2. Render 2D Minimap / Fullscreen World Map (M) projections
    g_Renderer->RenderCompassAndWorldMap(*g_Mumble, g_PackManager->GetActiveMapMarkers(), g_PackManager->GetActiveMapTrails(), isMapOpen);

    // 3. Render Quick-Access window & interaction notifications
    if (g_UI) {
        g_UI->RenderOverlay(*g_Mumble);
    }
}

static void OnOptionsRender() {
    if (g_UI) {
        g_UI->RenderNexusOptionsTab();
    }
}

static void AddonLoad(AddonAPI_t* aApi) {
    g_API = aApi;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(g_API->ImguiContext));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<ImGuiMemAllocFunc>(g_API->ImguiMalloc),
        reinterpret_cast<ImGuiMemFreeFunc>(g_API->ImguiFree)
    );

    g_API->Log(LOGL_INFO, "NexusPathing", "Initializing Blish HUD Pathing port for Raidcore Nexus (API v6 ABI)...");

    g_Mumble = static_cast<MumbleLink_t*>(g_API->DataLink_Get("DL_MUMBLE_LINK"));
    g_NexusLink = g_API->DataLink_Get("DL_NEXUS_LINK");

    std::filesystem::path addonDir = g_API->Paths_GetAddonDirectory("NexusPathing");
    std::filesystem::create_directories(addonDir / "markers");

    // Initialize subsystems
    g_Renderer = std::make_unique<D3D11PathingRenderer>(static_cast<IDXGISwapChain*>(g_API->SwapChain), g_API);
    g_BehaviorEngine = std::make_unique<BehaviorEngine>(addonDir / "state.json", g_API);
    g_LuaVM = std::make_unique<LuaPathingVM>(g_API);
    g_PackManager = std::make_unique<PackManager>(addonDir / "markers", g_Renderer.get(), g_BehaviorEngine.get(), g_LuaVM.get(), g_API);
    g_UI = std::make_unique<NexusPathingImGui>(g_PackManager.get(), g_BehaviorEngine.get(), g_Renderer.get(), g_API);

    // Load all .taco / .zip packs in <GW2>/addons/NexusPathing/markers
    g_PackManager->DiscoverAndLoadPacksAsync();

    // Register Nexus Quick-Access shortcut & keybinds
    g_API->InputBinds_RegisterWithString("KB_NEXUSPATHING_INTERACT", OnInteractKeybind, "F");
    g_API->InputBinds_RegisterWithString("KB_NEXUSPATHING_TOGGLE_UI", OnInteractKeybind, "ALT+SHIFT+P");
    g_API->QuickAccess_Add("QA_NEXUSPATHING", "TEX_PATHING_ICON", "TEX_PATHING_ICON_HOVER", "KB_NEXUSPATHING_TOGGLE_UI", "NexusPathing (Blish HUD Pathing)");

    // Subscribe render callbacks
    g_API->GUI_Register(RT_PreRender, OnPreRender);
    g_API->GUI_Register(RT_Render, OnRender);
    g_API->GUI_Register(RT_OptionsRender, OnOptionsRender);

    g_API->Log(LOGL_INFO, "NexusPathing", "NexusPathing successfully loaded and hooked into D3D11 SwapChain.");
}

static void AddonUnload() {
    g_API->Log(LOGL_INFO, "NexusPathing", "Unloading NexusPathing...");

    g_API->GUI_Deregister(OnPreRender);
    g_API->GUI_Deregister(OnRender);
    g_API->GUI_Deregister(OnOptionsRender);
    g_API->QuickAccess_Remove("QA_NEXUSPATHING");
    g_API->InputBinds_Deregister("KB_NEXUSPATHING_INTERACT");
    g_API->InputBinds_Deregister("KB_NEXUSPATHING_TOGGLE_UI");

    if (g_BehaviorEngine) g_BehaviorEngine->FlushStateToDisk();

    g_UI.reset();
    g_PackManager.reset();
    g_LuaVM.reset();
    g_BehaviorEngine.reset();
    g_Renderer.reset();
}

static AddonDefinition_t s_AddonDef = {};

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    s_AddonDef.Signature   = -0x42485054; // 'BHPT'
    s_AddonDef.APIVersion  = NEXUS_API_VERSION;
    s_AddonDef.Name        = "NexusPathing";
    s_AddonDef.Version     = { 2, 4, 0, 1 };
    s_AddonDef.Author      = "NexusPathing Contributors (Blish HUD Pathing Port)";
    s_AddonDef.Description = "100% feature-complete native C++20/D3D11 port of Blish HUD Pathing (.taco, .trl, bh-* attributes, Behaviors 0-7, GW2 API sync, and LuaJIT scripting).";
    s_AddonDef.Load        = AddonLoad;
    s_AddonDef.Unload      = AddonUnload;
    s_AddonDef.Flags       = AF_None;
    s_AddonDef.Provider    = UP_GitHub;
    s_AddonDef.UpdateLink  = "https://github.com/NexusPathing/NexusPathing";
    return &s_AddonDef;
}`,
  },
  {
    path: 'src/Renderer/D3D11PathingRenderer.cpp',
    filename: 'D3D11PathingRenderer.cpp',
    category: 'D3D11 Renderer & Shaders',
    language: 'cpp',
    description: 'DirectX 11 world-space marker billboarding and triangle-strip trail ribbon renderer with MumbleLink camera matrix reconstruction.',
    blishEquivalent: 'Entities/StandardMarker.cs + StandardTrail.cs',
    linesCount: 196,
    code: `#include "D3D11PathingRenderer.h"
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <cmath>

using namespace DirectX;

namespace NexusPathing {

struct alignas(16) FrameConstantBuffer {
    XMFLOAT4X4 ViewProj;
    XMFLOAT3   CameraPos;
    float      GlobalTime;
    XMFLOAT3   AvatarPos;
    float      PlayerCutoutRadius;
};

struct alignas(16) ObjectConstantBuffer {
    XMFLOAT4   TintColor;
    float      FadeNear;
    float      FadeFar;
    float      AnimSpeed;
    float      TrailScale;
};

D3D11PathingRenderer::D3D11PathingRenderer(IDXGISwapChain* aSwapChain, AddonAPI_t* aApi)
    : m_SwapChain(aSwapChain), m_API(aApi) {
    HRESULT hr = m_SwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(m_Device.GetAddressOf()));
    if (SUCCEEDED(hr)) {
        m_Device->GetImmediateContext(m_Context.GetAddressOf());
        CompileShaders();
        CreatePipelineStates();
    }
}

XMMATRIX D3D11PathingRenderer::BuildViewProjectionMatrix(const MumbleLink_t& mumble, float aspect) const {
    // Guild Wars 2 MumbleLink uses meters in a left-handed coordinate system (X=East, Y=Up, Z=North)
    XMVECTOR eyePos = XMVectorSet(mumble.CameraPosition.X, mumble.CameraPosition.Y, mumble.CameraPosition.Z, 1.0f);
    XMVECTOR front  = XMVector3Normalize(XMVectorSet(mumble.CameraFront.X, mumble.CameraFront.Y, mumble.CameraFront.Z, 0.0f));
    XMVECTOR up     = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookToLH(eyePos, front, up);

    // Parse vertical FOV from MumbleLink Identity JSON (defaults to 0.872 rad / ~50 deg)
    float fovVertical = m_ParsedFovRadians > 0.1f ? m_ParsedFovRadians : 0.872665f;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fovVertical, aspect, 0.15f, 2500.0f);

    return XMMatrixMultiply(view, proj);
}

void D3D11PathingRenderer::RenderWorldSpace(
    const MumbleLink_t& mumble,
    const std::vector<RuntimeMarker>& markers,
    const std::vector<RuntimeTrail>& trails)
{
    if (!m_Context || (markers.empty() && trails.empty())) return;

    // Backup current D3D11 state so we don't clobber Nexus/ImGui or ArcDPS
    D3D11StateBackup stateBackup(m_Context.Get());

    D3D11_VIEWPORT vp;
    UINT numVps = 1;
    m_Context->RSGetViewports(&numVps, &vp);
    float aspect = (vp.Height > 0.0f) ? (vp.Width / vp.Height) : (16.0f / 9.0f);

    XMMATRIX viewProj = BuildViewProjectionMatrix(mumble, aspect);

    FrameConstantBuffer frameCB{};
    XMStoreFloat4x4(&frameCB.ViewProj, XMMatrixTranspose(viewProj));
    frameCB.CameraPos = { mumble.CameraPosition.X, mumble.CameraPosition.Y, mumble.CameraPosition.Z };
    frameCB.AvatarPos = { mumble.AvatarPosition.X, mumble.AvatarPosition.Y, mumble.AvatarPosition.Z };
    frameCB.GlobalTime = static_cast<float>(GetTickCount64() % 3600000) * 0.001f;
    frameCB.PlayerCutoutRadius = 2.5f;

    UpdateDynamicConstantBuffer(m_FrameCB.Get(), &frameCB, sizeof(FrameConstantBuffer));

    float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
    m_Context->OMSetBlendState(m_AlphaBlendState.Get(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(m_DepthReadState.Get(), 0);

    // 1. Render Textured Trail Ribbons
    m_Context->VSSetShader(m_TrailVS.Get(), nullptr, 0);
    m_Context->PSSetShader(m_TrailPS.Get(), nullptr, 0);
    m_Context->IASetInputLayout(m_TrailInputLayout.Get());
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    for (const auto& trail : trails) {
        if (!trail.IsVisible || !trail.VertexBuffer || trail.VertexCount < 4) continue;

        SetCullMode(trail.CullMode);

        ObjectConstantBuffer objCB{};
        objCB.TintColor  = trail.Color;
        objCB.FadeNear   = trail.FadeNear * 0.0254f; // Convert GW2 inches to Mumble meters
        objCB.FadeFar    = trail.FadeFar * 0.0254f;
        objCB.AnimSpeed  = trail.AnimSpeed;
        objCB.TrailScale = trail.TrailScale;
        UpdateDynamicConstantBuffer(m_ObjectCB.Get(), &objCB, sizeof(ObjectConstantBuffer));

        ID3D11ShaderResourceView* srv = trail.Texture ? trail.Texture->Resource : m_DefaultTrailSRV.Get();
        m_Context->PSSetShaderResources(0, 1, &srv);

        UINT stride = sizeof(TrailVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = trail.VertexBuffer.Get();
        m_Context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_Context->Draw(trail.VertexCount, 0);
    }

    // 2. Render 3D Billboard POIs (Sorted back-to-front for accurate alpha compositing)
    m_Context->VSSetShader(m_MarkerVS.Get(), nullptr, 0);
    m_Context->PSSetShader(m_MarkerPS.Get(), nullptr, 0);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    for (const auto& marker : markers) {
        if (!marker.IsVisible) continue;

        float bounceOffset = ComputeBounceOffset(marker, frameCB.GlobalTime, frameCB.AvatarPos);
        RenderMarkerBillboard(marker, bounceOffset);
    }
}

float D3D11PathingRenderer::ComputeBounceOffset(const RuntimeMarker& m, float time, const XMFLOAT3& playerPos) const {
    if (!m.HasBounce) return 0.0f;
    float dx = m.Position.x - playerPos.x;
    float dy = m.Position.y - playerPos.y;
    float dz = m.Position.z - playerPos.z;
    float distSq = dx * dx + dy * dy + dz * dz;
    if (distSq > (m.TriggerRange * m.TriggerRange * 4.0f)) return 0.0f;

    if (m.BounceType == EBounceType::Bounce) {
        return std::abs(std::sin(time * (3.14159f / m.BounceDuration))) * m.BounceHeight;
    } else {
        // Rise interpolation
        return m.BounceHeight;
    }
}

} // namespace NexusPathing`,
  },
  {
    path: 'src/Shaders/PathingShaders.hlsl',
    filename: 'PathingShaders.hlsl',
    category: 'D3D11 Renderer & Shaders',
    language: 'hlsl',
    description: 'HLSL Shader Model 5.0 vertex and pixel shaders for animated UV trail ribbons, spherical player cutout, and distance fade.',
    blishEquivalent: 'Entities/Effects/TrailEffect.fx & MarkerEffect.fx',
    linesCount: 112,
    code: `// PathingShaders.hlsl - DirectX 11 HLSL SM 5.0 Shaders for NexusPathing

cbuffer FrameConstants : register(b0) {
    float4x4 uViewProj;
    float3   uCameraPos;
    float    uGlobalTime;
    float3   uAvatarPos;
    float    uPlayerCutoutRadius;
};

cbuffer ObjectConstants : register(b1) {
    float4 uTintColor;
    float  uFadeNear;
    float  uFadeFar;
    float  uAnimSpeed;
    float  uTrailScale;
};

Texture2D    uMainTexture : register(t0);
SamplerState uLinearWrap  : register(s0);

struct TrailVSInput {
    float3 Position   : POSITION;
    float3 NextPos    : TEXCOORD0;
    float3 PrevPos    : TEXCOORD1;
    float2 UV         : TEXCOORD2; // x = [-1, +1] ribbon side, y = cumulative arc distance
};

struct TrailPSInput {
    float4 ClipPos    : SV_POSITION;
    float3 WorldPos   : TEXCOORD0;
    float2 UV         : TEXCOORD1;
};

TrailPSInput TrailVS_Main(TrailVSInput input) {
    TrailPSInput output;

    // Compute tangent along trail segment and camera-facing perpendicular miter vector
    float3 tangent = normalize(input.NextPos - input.PrevPos);
    float3 toCam   = normalize(uCameraPos - input.Position);
    float3 right   = normalize(cross(tangent, toCam));

    float halfWidth = 0.55f * uTrailScale;
    float3 worldPos = input.Position + right * (input.UV.x * halfWidth);

    output.WorldPos = worldPos;
    output.ClipPos  = mul(float4(worldPos, 1.0f), uViewProj);

    // Animate V coordinate along trail arc length
    float scrolledV = input.UV.y - (uGlobalTime * uAnimSpeed);
    output.UV       = float2(input.UV.x * 0.5f + 0.5f, scrolledV);

    return output;
}

float4 TrailPS_Main(TrailPSInput input) : SV_TARGET {
    float4 texColor = uMainTexture.Sample(uLinearWrap, input.UV);
    if (texColor.a < 0.02f) discard;

    // 1. Far distance fade (fadeNear -> fadeFar)
    float distToCam = distance(input.WorldPos, uCameraPos);
    float farFade   = 1.0f - smoothstep(uFadeNear, max(uFadeNear + 0.1f, uFadeFar), distToCam);

    // 2. Spherical player cutout fade so trails don't clip through character head
    float distToAvatar = distance(input.WorldPos, uAvatarPos);
    float playerCutout = smoothstep(uPlayerCutoutRadius * 0.4f, uPlayerCutoutRadius, distToAvatar);

    float finalAlpha = texColor.a * uTintColor.a * farFade * playerCutout;
    if (finalAlpha <= 0.01f) discard;

    return float4(texColor.rgb * uTintColor.rgb, finalAlpha);
}`,
  },
  {
    path: 'src/Pack/TrlBinaryParser.cpp',
    filename: 'TrlBinaryParser.cpp',
    category: 'Parsers (.taco / .trl / XML)',
    language: 'cpp',
    description: 'High-speed binary .trl parser reading little-endian Version, MapID, and float32 (X,Y,Z) triplets with (0,0,0) break handling.',
    blishEquivalent: 'Content/Readers/TrlReader.cs',
    linesCount: 98,
    code: `#include "TrlBinaryParser.h"
#include <cstring>
#include <cmath>

namespace NexusPathing {

#pragma pack(push, 1)
struct TrlHeader_t {
    int32_t Version; // Expected: 0
    int32_t MapId;   // GW2 Map ID
};

struct TrlPoint_t {
    float X;
    float Y;
    float Z;
};
#pragma pack(pop)

bool TrlBinaryParser::ParseFromBuffer(
    const uint8_t* aData,
    size_t aByteLength,
    ParsedTrlFile& outTrl)
{
    if (!aData || aByteLength < sizeof(TrlHeader_t)) {
        return false;
    }

    TrlHeader_t header{};
    std::memcpy(&header, aData, sizeof(TrlHeader_t));

    outTrl.Version = header.Version;
    outTrl.MapId   = header.MapId;
    outTrl.Segments.clear();

    const size_t payloadBytes = aByteLength - sizeof(TrlHeader_t);
    const size_t numPoints    = payloadBytes / sizeof(TrlPoint_t);
    const auto*  pointsPtr    = reinterpret_cast<const TrlPoint_t*>(aData + sizeof(TrlHeader_t));

    std::vector<DirectX::XMFLOAT3> currentSegment;
    currentSegment.reserve(256);

    for (size_t i = 0; i < numPoints; ++i) {
        const TrlPoint_t& pt = pointsPtr[i];

        // In TacO / Blish HUD .trl format, a (0.0f, 0.0f, 0.0f) vector marks a disjoint trail section break
        const bool isBreakSentinel =
            (std::fabs(pt.X) < 1e-5f) &&
            (std::fabs(pt.Y) < 1e-5f) &&
            (std::fabs(pt.Z) < 1e-5f);

        if (isBreakSentinel) {
            if (!currentSegment.empty()) {
                outTrl.Segments.push_back(std::move(currentSegment));
                currentSegment.clear();
            }
        } else {
            currentSegment.push_back({ pt.X, pt.Y, pt.Z });
        }
    }

    if (!currentSegment.empty()) {
        outTrl.Segments.push_back(std::move(currentSegment));
    }

    return true;
}

} // namespace NexusPathing`,
  },
  {
    path: 'src/Pack/XmlCategoryParser.cpp',
    filename: 'XmlCategoryParser.cpp',
    category: 'Parsers (.taco / .trl / XML)',
    language: 'cpp',
    description: 'XML parser handling hierarchical MarkerCategory inheritance, case-insensitive attributes, and Blish HUD bh-* prefix overrides.',
    blishEquivalent: 'Format/XmlMarkerPackReader.cs',
    linesCount: 134,
    code: `#include "XmlCategoryParser.h"
#include <pugixml.hpp>
#include <algorithm>
#include <string_view>

namespace NexusPathing {

// Normalizes attribute names to lowercase and strips Blish HUD 'bh-' prefix overrides
AttributeMap XmlCategoryParser::ExtractAttributesWithBlishOverrides(const pugi::xml_node& node) {
    AttributeMap standardAttrs;
    AttributeMap blishOverrides;

    for (pugi::xml_attribute attr : node.attributes()) {
        std::string name = attr.name();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        if (name.rfind("bh-", 0) == 0 && name.size() > 3) {
            blishOverrides[name.substr(3)] = attr.value();
        } else {
            standardAttrs[name] = attr.value();
        }
    }

    // Blish HUD 'bh-*' attributes always override standard TacO attributes when running in NexusPathing
    for (auto& [key, val] : blishOverrides) {
        standardAttrs[key] = std::move(val);
    }

    return standardAttrs;
}

void XmlCategoryParser::ParseCategoryNode(
    const pugi::xml_node& xmlNode,
    const std::string& parentFullPath,
    const AttributeMap& inheritedAttrs,
    CategoryNode& outCategory)
{
    AttributeMap mergedAttrs = inheritedAttrs;
    AttributeMap localAttrs  = ExtractAttributesWithBlishOverrides(xmlNode);

    for (const auto& [k, v] : localAttrs) {
        mergedAttrs[k] = v;
    }

    outCategory.Name          = localAttrs["name"];
    outCategory.FullPath      = parentFullPath.empty() ? outCategory.Name : (parentFullPath + "." + outCategory.Name);
    outCategory.DisplayName   = localAttrs.count("displayname") ? localAttrs["displayname"] : outCategory.Name;
    outCategory.IsSeparator   = (localAttrs["isseparator"] == "1" || localAttrs["isseparator"] == "true");
    outCategory.DefaultToggle = !(localAttrs["defaulttoggle"] == "0" || localAttrs["defaulttoggle"] == "false");
    outCategory.MergedAttrs   = mergedAttrs;

    // Register in O(1) lookup table for <POI type="a.b.c"> inheritance
    std::string lowerPath = outCategory.FullPath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    m_CategoryLookup[lowerPath] = &outCategory;

    for (pugi::xml_node child : xmlNode.children("MarkerCategory")) {
        auto childCat = std::make_unique<CategoryNode>();
        ParseCategoryNode(child, outCategory.FullPath, mergedAttrs, *childCat);
        outCategory.Children.push_back(std::move(childCat));
    }
}

} // namespace NexusPathing`,
  },
  {
    path: 'src/Behaviors/BehaviorEngine.cpp',
    filename: 'BehaviorEngine.cpp',
    category: 'Blish Attributes & Behaviors',
    language: 'cpp',
    description: 'Implements Blish HUD Pathing Behaviors (0–7), GW2 v2 Achievement bitmask filtering, Mount/Profession filters, and Win32 Clipboard copy.',
    blishEquivalent: 'Behaviors/StandardMarkerBehavior.cs & CopyBehavior.cs',
    linesCount: 165,
    code: `#include "BehaviorEngine.h"
#include <windows.h>
#include <chrono>

namespace NexusPathing {

enum class EMarkerBehavior : int32_t {
    AlwaysVisible           = 0,
    ReappearOnMapChange     = 1,
    ReappearOnDailyReset    = 2,
    OnlyVisibleBeforeUse    = 3,
    ReappearAfterTimer      = 4,
    ReappearOnMapReset      = 5,
    OncePerCharacter        = 6,
    OncePerInstance         = 7
};

bool BehaviorEngine::EvaluateMarkerVisibility(
    const RuntimeMarker& marker,
    const MumbleLink_t& mumble,
    const IdentityData& identity) const
{
    // 1. Map ID Filter
    if (marker.MapId != 0 && marker.MapId != static_cast<int32_t>(mumble.Context.MapId)) {
        return false;
    }

    // 2. Mount Filter (e.g. mount="Skyscale,Griffon")
    if (marker.MountFilterMask != 0) {
        uint32_t currentMountBit = (1u << mumble.Context.MountIndex);
        if ((marker.MountFilterMask & currentMountBit) == 0) {
            return false;
        }
    }

    // 3. Profession Filter
    if (marker.ProfessionFilterMask != 0) {
        uint32_t currentProfBit = (1u << identity.Profession);
        if ((marker.ProfessionFilterMask & currentProfBit) == 0) {
            return false;
        }
    }

    // 4. GW2 v2 API Achievement Filter (achievementId & achievementBit)
    if (marker.AchievementId > 0) {
        if (IsAchievementCompleted(marker.AchievementId, marker.AchievementBit)) {
            return false;
        }
    }

    // 5. Behavior State Evaluation (0..7) with InvertBehavior support
    bool isFilteredByBehavior = IsGuidFilteredByBehavior(marker, mumble, identity);
    if (marker.InvertBehavior) {
        isFilteredByBehavior = !isFilteredByBehavior;
    }

    return !isFilteredByBehavior;
}

void BehaviorEngine::TriggerMarker(RuntimeMarker& marker, const MumbleLink_t& mumble) {
    const auto nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    m_TriggeredTimestamps[marker.Guid] = nowEpoch;

    // Handle copy="[&BDkOAAA=]" attribute -> Win32 Clipboard
    if (!marker.CopyValue.empty()) {
        CopyTextToWin32Clipboard(marker.CopyValue);
        if (m_API) {
            m_API->Log(LOGL_INFO, "NexusPathing", marker.CopyMessage.empty()
                ? ("Copied to clipboard: " + marker.CopyValue).c_str()
                : marker.CopyMessage.c_str());
        }
    }

    // Handle showHide="category.path" attribute
    if (!marker.ShowHideCategory.empty() && m_CategoryToggler) {
        m_CategoryToggler(marker.ShowHideCategory);
    }
}

void BehaviorEngine::CopyTextToWin32Clipboard(const std::string& utf8Text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
        if (hMem) {
            wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(hMem));
            MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, wstr, wlen);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
}

} // namespace NexusPathing`,
  },
  {
    path: 'src/Scripting/LuaPathingVM.cpp',
    filename: 'LuaPathingVM.cpp',
    category: 'Lua Scripting VM',
    language: 'cpp',
    description: 'Embedded LuaJIT 2.1 + sol2 scripting engine supporting Blish HUD pack.lua, script-once, script-tick, script-trigger, and script-filter.',
    blishEquivalent: 'Scripting/ScriptingEngine.cs',
    linesCount: 138,
    code: `#include "LuaPathingVM.h"
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace NexusPathing {

LuaPathingVM::LuaPathingVM(AddonAPI_t* aApi) : m_API(aApi) {
    m_Lua.open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table,
        sol::lib::utf8
    );

    RegisterBlishPathingGlobals();
}

void LuaPathingVM::RegisterBlishPathingGlobals() {
    // 1. Bind Vector3 type matching Blish HUD Pathing Lua API
    m_Lua.new_usertype<LuaVector3>("Vector3",
        sol::constructors<LuaVector3(), LuaVector3(float, float, float)>(),
        "X", &LuaVector3::X,
        "Y", &LuaVector3::Y,
        "Z", &LuaVector3::Z,
        "Length", &LuaVector3::Length,
        sol::meta_function::subtraction, &LuaVector3::Subtract
    );

    // 2. Bind Marker runtime proxy
    m_Lua.new_usertype<RuntimeMarker>("Marker",
        "Guid", sol::readonly(&RuntimeMarker::Guid),
        "Size", &RuntimeMarker::IconSize,
        "Alpha", &RuntimeMarker::Alpha,
        "Position", &RuntimeMarker::Position,
        "SetTexture", &RuntimeMarker::SetTextureFromLua
    );

    // 3. Bind Debug logger -> forwards directly to Raidcore Nexus Log window
    sol::table debugTable = m_Lua.create_named_table("Debug");
    debugTable.set_function("Print", [this](const std::string& msg) {
        if (m_API) m_API->Log(LOGL_DEBUG, "NexusPathing::Lua", msg.c_str());
    });
    debugTable.set_function("Info", [this](const std::string& msg) {
        if (m_API) m_API->Log(LOGL_INFO, "NexusPathing::Lua", msg.c_str());
    });
    debugTable.set_function("Warn", [this](const std::string& msg) {
        if (m_API) m_API->Log(LOGL_WARNING, "NexusPathing::Lua", msg.c_str());
    });
}

void LuaPathingVM::ExecuteMarkerTriggerScript(RuntimeMarker& marker) {
    if (marker.ScriptTriggerFn.empty()) return;
    sol::protected_function fn = m_Lua[marker.ScriptTriggerFn];
    if (fn.valid()) {
        auto result = fn( std::ref(marker) );
        if (!result.valid()) {
            sol::error err = result;
            if (m_API) m_API->Log(LOGL_WARNING, "NexusPathing::Lua", err.what());
        }
    }
}

} // namespace NexusPathing`,
  },
  {
    path: 'CMakeLists.txt',
    filename: 'CMakeLists.txt',
    category: 'ImGui Nexus UI & Build',
    language: 'cmake',
    description: 'Production CMake build configuration targeting MSVC x64 C++20, linking DirectX 11, Dear ImGui, pugixml, libzip, and LuaJIT 2.1.',
    blishEquivalent: 'BlishHUD.Pathing.csproj',
    linesCount: 68,
    code: `cmake_minimum_required(VERSION 3.24)
project(NexusPathing VERSION 2.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch dependencies via vcpkg or FetchContent
find_package(pugixml CONFIG REQUIRED)
find_package(libzip CONFIG REQUIRED)
find_package(LuaJIT REQUIRED)
find_package(sol2 CONFIG REQUIRED)

add_library(NexusPathing SHARED
    src/dllmain.cpp
    src/Renderer/D3D11PathingRenderer.cpp
    src/Renderer/CompassMapProjector.cpp
    src/Pack/PackManager.cpp
    src/Pack/TacoArchiveReader.cpp
    src/Pack/TrlBinaryParser.cpp
    src/Pack/XmlCategoryParser.cpp
    src/Behaviors/BehaviorEngine.cpp
    src/Behaviors/Gw2ApiSync.cpp
    src/Scripting/LuaPathingVM.cpp
    src/UI/NexusPathingImGui.cpp
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
)

target_include_directories(NexusPathing PRIVATE
    include
    src
    external/imgui
    \${LUAJIT_INCLUDE_DIR}
)

target_link_libraries(NexusPathing PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    winhttp.lib
    pugixml::pugixml
    libzip::zip
    \${LUAJIT_LIBRARIES}
    sol2::sol2
)

set_target_properties(NexusPathing PROPERTIES
    OUTPUT_NAME "NexusPathing"
    SUFFIX ".dll"
)`,
  },
];
