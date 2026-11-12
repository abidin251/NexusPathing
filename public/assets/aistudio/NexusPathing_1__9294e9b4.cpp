///----------------------------------------------------------------------------------------------------
/// NexusPathing.cpp - Native C++20 / DirectX 11 Raidcore Nexus Addon (v2.7.0)
/// Complete Port of Blish HUD Pathing for Guild Wars 2
///
/// Fixes & Features in v2.7.0:
/// 1. Fixed 3D Trail Arrow Orientation & Flow Direction (matching Blish HUD StandardTrail.World.cs & trail.hlsl
///    where pastDistance decreases along the trail and +FlowSpeed scrolls forward)
/// 2. Full Blish HUD Lua 5.4 Scripting Support for xrandox_TehsTrails.taco & Marker Packs:
///    - Global `PathingVersion` ("1.13.2"), `Pathing`, `Packs`, `World`, `Pack`, `User`, `Menu`, `Storage`
///    - `I:Guid`, `I:Color`, `I:Texture`, `I:Vector3` (+/-/length), `table.ToLson` / `table.FromLson`
///    - Live `RuntimeTrail` and `RuntimeMarker` Lua proxy tables with `Category:GetTrails(true)` and
///      `Category:GetMarkers(true)` so `TehsTrails` (which sets `traildata=""` in XML and dynamically assigns
///      `trail.Texture`, `trail.Alpha`, `trail.InGameVisibility`, `trail.Tint` in Lua) renders all trails!
///    - Automatic `pack.lua` re-execution on MapID transitions so map-filtered scripts initialize trails
///      when entering a map or loading into gameplay
/// 3. Procedural PNG Quick Access Shortcut Textures (`TEX_PATHING_ICON` & `TEX_PATHING_ICON_HOVER`)
///    registered in `AddonLoad` so Raidcore Nexus Quick Access loads cleanly with zero warnings
///----------------------------------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "Nexus.h"
#include "Mumble.h"
#include "imgui/imgui.h"
#include "miniz/miniz.h"
#include "pugixml/pugixml.hpp"
#include "stb_image.h"

extern "C" {
#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
}

#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>

using Microsoft::WRL::ComPtr;

namespace NexusPathing {

static constexpr float METERS_TO_INCHES = 39.3700787f;
static constexpr float INCHES_TO_METERS = 0.0254f;

// ============================================================================
// 1. 3D LINEAR ALGEBRA & MUMBLE CAMERA PROJECTION
// ============================================================================
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f; // Altitude (Up) in GW2 Mumble coordinates
    float z = 0.0f; // North/South in GW2 Mumble coordinates

    Vec3 operator+(const Vec3& r) const { return { x + r.x, y + r.y, z + r.z }; }
    Vec3 operator-(const Vec3& r) const { return { x - r.x, y - r.y, z - r.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }

    float LengthSq() const { return x * x + y * y + z * z; }
    float Length() const { return std::sqrt(LengthSq()); }
    Vec3 Normalized() const {
        float len = Length();
        return (len > 1e-6f) ? Vec3{ x / len, y / len, z / len } : Vec3{ 0.0f, 0.0f, 0.0f };
    }
    static Vec3 Cross(const Vec3& a, const Vec3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }
    static float Dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
};

struct Vec4 {
    float x = 1.0f, y = 1.0f, z = 1.0f, w = 1.0f;
};

struct Mat4x4 {
    float m[4][4] = {};

    static Mat4x4 LookToLH(const Vec3& eye, const Vec3& frontDir, const Vec3& upDir) {
        Vec3 zaxis = frontDir.Normalized();
        Vec3 xaxis = Vec3::Cross(upDir, zaxis).Normalized();
        Vec3 yaxis = Vec3::Cross(zaxis, xaxis);

        Mat4x4 res{};
        res.m[0][0] = xaxis.x; res.m[0][1] = yaxis.x; res.m[0][2] = zaxis.x; res.m[0][3] = 0.0f;
        res.m[1][0] = xaxis.y; res.m[1][1] = yaxis.y; res.m[1][2] = zaxis.y; res.m[1][3] = 0.0f;
        res.m[2][0] = xaxis.z; res.m[2][1] = yaxis.z; res.m[2][2] = zaxis.z; res.m[2][3] = 0.0f;
        res.m[3][0] = -Vec3::Dot(xaxis, eye);
        res.m[3][1] = -Vec3::Dot(yaxis, eye);
        res.m[3][2] = -Vec3::Dot(zaxis, eye);
        res.m[3][3] = 1.0f;
        return res;
    }

    static Mat4x4 PerspectiveFovLH(float fovY, float aspect, float zn, float zf) {
        float yScale = 1.0f / std::tan(fovY * 0.5f);
        float xScale = yScale / aspect;
        Mat4x4 res{};
        res.m[0][0] = xScale;
        res.m[1][1] = yScale;
        res.m[2][2] = zf / (zf - zn);
        res.m[2][3] = 1.0f;
        res.m[3][2] = -zn * zf / (zf - zn);
        return res;
    }

    static Mat4x4 Multiply(const Mat4x4& a, const Mat4x4& b) {
        Mat4x4 prod{};
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                prod.m[r][c] =
                    a.m[r][0] * b.m[0][c] +
                    a.m[r][1] * b.m[1][c] +
                    a.m[r][2] * b.m[2][c] +
                    a.m[r][3] * b.m[3][c];
            }
        }
        return prod;
    }

    static Mat4x4 Transpose(const Mat4x4& a) {
        Mat4x4 t{};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                t.m[r][c] = a.m[c][r];
        return t;
    }
};

// ============================================================================
// 2. DATA STRUCTURES FOR MARKERS, TRAILS, CATEGORIES & PACKS
// ============================================================================
using AttributeMap = std::unordered_map<std::string, std::string>;

struct CategoryNode {
    std::string  Name;
    std::string  FullPath;
    std::string  DisplayName;
    bool         IsSeparator       = false;
    bool         DefaultToggle     = true;
    bool         Enabled           = true;
    size_t       CachedMapTrails   = 0;
    size_t       CachedMapPois     = 0;
    size_t       CachedTotalTrails = 0;
    size_t       CachedTotalPois   = 0;
    AttributeMap MergedAttrs;
    std::vector<std::unique_ptr<CategoryNode>> Children;
};

struct TrailVertex {
    float Pos[3];
    float NextPos[3];
    float PrevPos[3];
    float UV[2];
};

struct RuntimeMarker {
    std::string Guid;
    std::string TypePath;
    std::string PackName;
    int32_t     MapId               = 0;
    Vec3        Position            = {};
    float       IconSize            = 1.0f;
    float       HeightOffset        = 1.5f;
    Vec4        TintColor           = { 1.0f, 1.0f, 1.0f, 1.0f };
    float       FadeNear            = 1200.0f;
    float       FadeFar             = 1800.0f;
    float       MapDisplaySize      = 20.0f;
    bool        InGameVisibility    = true;
    bool        MiniMapVisibility   = true;
    bool        MapVisibility       = true;
    int32_t     Behavior            = 0;
    bool        InvertBehavior      = false;
    int32_t     ResetLengthSec      = 0;
    int32_t     AchievementId       = 0;
    int32_t     AchievementBit      = -1;
    uint32_t    MountFilterMask     = 0;
    float       TriggerRange        = 8.0f;
    float       InfoRange           = 20.0f;
    bool        AutoTrigger         = false;
    bool        HasBounce           = false;
    bool        BounceIsRise        = false;
    float       BounceHeight        = 2.0f;
    float       BounceDuration      = 1.5f;
    std::string CopyValue;
    std::string CopyMessage;
    std::string ShowHideCategory;
    std::string InfoText;
    std::string IconFileRelative;
    std::string IconTextureId;
    Texture_t*  CachedTexture       = nullptr;
    ID3D11ShaderResourceView* NativeSRV = nullptr;
    // Lua script attributes
    std::string ScriptOnce;
    std::string ScriptTick;
    std::string ScriptTrigger;
    std::string ScriptFocus;
    std::string ScriptFilter;
    bool        ScriptOnceExecuted  = false;
    bool        ScriptAutoTriggered = false;
    bool        IsFocused           = false;
    bool        IsVisible           = true;
    bool        FromLua             = false;   // created by Pack:CreateMarker; purged before pack.lua re-runs
};

struct RuntimeTrail {
    std::string          Guid;
    std::string          TypePath;
    std::string          PackName;
    int32_t              MapId              = 0;
    std::string          TrailDataPath;
    std::string          TextureRelative;
    std::string          TextureId;
    Texture_t*           CachedTexture      = nullptr;
    ID3D11ShaderResourceView* NativeSRV     = nullptr;
    Vec4                 TintColor          = { 1.0f, 1.0f, 1.0f, 0.92f };
    float                AnimSpeed          = 1.0f;
    float                TrailScale         = 1.0f;
    float                FadeNear           = 1200.0f;
    float                FadeFar            = 2000.0f;
    bool                 InGameVisibility   = true;
    bool                 MiniMapVisibility  = true;
    bool                 MapVisibility      = true;
    bool                 IsWall             = false;
    bool                 CanFade            = true;
    bool                 UserEnabled        = true;
    uint32_t             MountFilterMask    = 0;
    std::vector<std::vector<Vec3>> Segments;
    std::vector<std::vector<Vec3>> CornerRights;
    bool                 IsVisible          = true;
};

struct LoadedPackSummary {
    std::string Filename;
    std::string FullPath;
    size_t      XmlCount    = 0;
    size_t      TrailCount  = 0;
    size_t      MarkerCount = 0;
    size_t      LuaCount    = 0;
    bool        Enabled     = true;
};

struct ManagedTacoRepo {
    std::string RepoUrl;       // e.g. "https://github.com/xrandox/TehsTrails"
    std::string OwnerRepo;     // e.g. "xrandox/TehsTrails"
    std::string LocalFilename; // e.g. "xrandox_TehsTrails.taco"
    std::string InstalledTag;
    std::string LatestTag;
    std::string DownloadUrl;
    std::string AssetName;
    bool        UpdateAvailable = false;
    std::string StatusText      = "Ready";
};

static bool IsPackEnabled(const std::string& packName);
static bool IsCategoryPathEnabled(const std::string& typePath);
static bool IsMarkerHiddenByAchievement(const RuntimeMarker& m);
static ID3D11ShaderResourceView* ResolveMarkerSRV(RuntimeMarker& m);
static ID3D11ShaderResourceView* ResolveTrailSRV(RuntimeTrail& t);
extern bool g_OnlyShowCurrentMap;
extern float g_TrailSubdivisionStep;
extern float g_TrailOpacity;
extern float g_MaxTrailDistance;

// ============================================================================
// 3. STRING, COLOR & PATH NORMALIZATION HELPERS
// ============================================================================
static std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string NormalizeArchivePath(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && (s.front() == '/' || s.front() == '.')) {
        if (s.rfind("./", 0) == 0) s.erase(0, 2);
        else if (s.front() == '/') s.erase(0, 1);
        else break;
    }
    return ToLowerAscii(s);
}

static Vec4 ParseHexColor(const std::string& hexStr, float alpha = 1.0f) {
    if (hexStr.empty()) return { 1.0f, 1.0f, 1.0f, alpha };
    std::string clean = (hexStr[0] == '#') ? hexStr.substr(1) : hexStr;
    if (clean.size() >= 6) {
        uint32_t val = std::strtoul(clean.substr(clean.size() - 6).c_str(), nullptr, 16);
        float r = ((val >> 16) & 0xFF) / 255.0f;
        float g = ((val >> 8) & 0xFF) / 255.0f;
        float b = (val & 0xFF) / 255.0f;
        return { r, g, b, alpha };
    }
    return { 1.0f, 1.0f, 1.0f, alpha };
}

static float SafeParseFloat(const std::string& s, float fallback) {
    if (s.empty()) return fallback;
    char* endPtr = nullptr;
    float v = std::strtof(s.c_str(), &endPtr);
    return (endPtr != s.c_str()) ? v : fallback;
}

static int32_t SafeParseInt(const std::string& s, int32_t fallback) {
    if (s.empty()) return fallback;
    char* endPtr = nullptr;
    long v = std::strtol(s.c_str(), &endPtr, 10);
    return (endPtr != s.c_str()) ? static_cast<int32_t>(v) : fallback;
}

static bool SafeParseBool(const std::string& s, bool fallback) {
    if (s.empty()) return fallback;
    std::string low = ToLowerAscii(s);
    if (low == "0" || low == "false" || low == "no") return false;
    if (low == "1" || low == "true" || low == "yes") return true;
    return fallback;
}

// ============================================================================
// 4. BINARY .TRL PARSER (Little-Endian Version, MapID, Float32 X,Y,Z + (0,0,0))
// ============================================================================
#pragma pack(push, 1)
struct TrlHeader_t {
    int32_t Version;
    int32_t MapId;
};
struct TrlPoint_t {
    float X;
    float Y;
    float Z;
};
#pragma pack(pop)

static bool ParseTrlBinaryBuffer(const uint8_t* data, size_t byteLen, int32_t& outMapId, std::vector<std::vector<Vec3>>& outSegments) {
    if (!data || byteLen < sizeof(TrlHeader_t)) return false;

    TrlHeader_t hdr{};
    std::memcpy(&hdr, data, sizeof(TrlHeader_t));
    outMapId = hdr.MapId;
    outSegments.clear();

    const size_t numPts = (byteLen - sizeof(TrlHeader_t)) / sizeof(TrlPoint_t);
    const auto* pts = reinterpret_cast<const TrlPoint_t*>(data + sizeof(TrlHeader_t));

    std::vector<Vec3> currentSeg;
    currentSeg.reserve(128);

    for (size_t i = 0; i < numPts; ++i) {
        const TrlPoint_t& p = pts[i];
        bool isBreak = (std::fabs(p.X) < 1e-5f) && (std::fabs(p.Y) < 1e-5f) && (std::fabs(p.Z) < 1e-5f);
        if (isBreak) {
            if (!currentSeg.empty()) {
                outSegments.push_back(std::move(currentSeg));
                currentSeg.clear();
            }
        } else {
            // In TacO .trl files, p.X = East/West, p.Y = Altitude (Up), p.Z = North/South (matches Mumble AvatarPosition X, Y, Z)
            currentSeg.push_back({ p.X, p.Y, p.Z });
        }
    }
    if (!currentSeg.empty()) {
        outSegments.push_back(std::move(currentSeg));
    }
    return !outSegments.empty();
}

// ============================================================================
// 5. D3D11 STATE BACKUP & RESTORE GUARD
// ============================================================================
struct D3D11StateBackup {
    ID3D11DeviceContext*        ctx = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY    topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11InputLayout*          inputLayout = nullptr;
    ID3D11VertexShader*         vs = nullptr;
    ID3D11PixelShader*          ps = nullptr;
    ID3D11Buffer*               vsCBs[2] = { nullptr, nullptr };
    ID3D11Buffer*               psCBs[2] = { nullptr, nullptr };
    ID3D11ShaderResourceView*   psSRV = nullptr;
    ID3D11SamplerState*         psSampler = nullptr;
    ID3D11Buffer*               vb = nullptr;
    UINT                        vbStride = 0;
    UINT                        vbOffset = 0;
    ID3D11BlendState*           blendState = nullptr;
    FLOAT                       blendFactor[4] = {};
    UINT                        sampleMask = 0;
    ID3D11DepthStencilState*    depthState = nullptr;
    UINT                        stencilRef = 0;
    ID3D11RasterizerState*      rasterState = nullptr;

    explicit D3D11StateBackup(ID3D11DeviceContext* aCtx) : ctx(aCtx) {
        if (!ctx) return;
        ctx->IAGetPrimitiveTopology(&topology);
        ctx->IAGetInputLayout(&inputLayout);
        ctx->VSGetShader(&vs, nullptr, nullptr);
        ctx->PSGetShader(&ps, nullptr, nullptr);
        ctx->VSGetConstantBuffers(0, 2, vsCBs);
        ctx->PSGetConstantBuffers(0, 2, psCBs);
        ctx->PSGetShaderResources(0, 1, &psSRV);
        ctx->PSGetSamplers(0, 1, &psSampler);
        ctx->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
        ctx->OMGetBlendState(&blendState, blendFactor, &sampleMask);
        ctx->OMGetDepthStencilState(&depthState, &stencilRef);
        ctx->RSGetState(&rasterState);
    }

    ~D3D11StateBackup() {
        if (!ctx) return;
        ctx->IASetPrimitiveTopology(topology);
        ctx->IASetInputLayout(inputLayout);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 2, vsCBs);
        ctx->PSSetConstantBuffers(0, 2, psCBs);
        ctx->PSSetShaderResources(0, 1, &psSRV);
        ctx->PSSetSamplers(0, 1, &psSampler);
        ctx->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
        ctx->OMSetBlendState(blendState, blendFactor, sampleMask);
        ctx->OMSetDepthStencilState(depthState, stencilRef);
        ctx->RSSetState(rasterState);

        if (inputLayout) inputLayout->Release();
        if (vs) vs->Release();
        if (ps) ps->Release();
        for (auto* b : vsCBs) if (b) b->Release();
        for (auto* b : psCBs) if (b) b->Release();
        if (psSRV) psSRV->Release();
        if (psSampler) psSampler->Release();
        if (vb) vb->Release();
        if (blendState) blendState->Release();
        if (depthState) depthState->Release();
        if (rasterState) rasterState->Release();
    }
};

// ============================================================================
// 6. DIRECTX 11 HLSL SHADERS & WORLD-SPACE + MINIMAP RENDERER
// ============================================================================
static const char* g_PathingHlslSource = R"(
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

Texture2D    uMainTex    : register(t0);
SamplerState uLinearWrap : register(s0);

struct TrailVSInput {
    float3 Position : POSITION;
    float3 NextPos  : TEXCOORD0;
    float3 PrevPos  : TEXCOORD1;
    float2 UV       : TEXCOORD2;
};

struct TrailPSInput {
    float4 ClipPos  : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float2 UV       : TEXCOORD1;
};

TrailPSInput TrailVS_Main(TrailVSInput input) {
    TrailPSInput output;
    float3 diff     = input.NextPos - input.PrevPos;
    float3 tangent  = (length(diff) > 0.0001f) ? normalize(diff) : float3(1.0f, 0.0f, 0.0f);
    float3 toCam    = normalize(uCameraPos - input.Position);
    float3 right    = normalize(cross(tangent, toCam));
    float halfWidth = 0.508f * uTrailScale;
    float3 worldPos = input.Position + right * (input.UV.x * halfWidth);

    output.WorldPos = worldPos;
    output.ClipPos  = mul(float4(worldPos, 1.0f), uViewProj);
    output.UV       = float2(input.UV.x * 0.5f + 0.5f, input.UV.y - (uGlobalTime * uAnimSpeed));
    return output;
}

float4 TrailPS_Main(TrailPSInput input) : SV_TARGET {
    float4 texColor    = uMainTex.Sample(uLinearWrap, input.UV);
    float distToCam    = distance(input.WorldPos, uCameraPos);
    float farFade      = 1.0f - smoothstep(uFadeNear, max(uFadeNear + 0.1f, uFadeFar), distToCam);
    float distToAvatar = distance(input.WorldPos, uAvatarPos);
    float playerCutout = smoothstep(uPlayerCutoutRadius * 0.35f, max(0.01f, uPlayerCutoutRadius), distToAvatar);

    float finalAlpha   = texColor.a * uTintColor.a * farFade * playerCutout;
    if (finalAlpha <= 0.01f) discard;

    return float4(texColor.rgb * uTintColor.rgb, finalAlpha);
}
)";

struct alignas(16) FrameConstantBuffer_t {
    Mat4x4 ViewProj;
    Vec3   CameraPos;
    float  GlobalTime;
    Vec3   AvatarPos;
    float  PlayerCutoutRadius;
};

struct alignas(16) ObjectConstantBuffer_t {
    Vec4  TintColor;
    float FadeNear;
    float FadeFar;
    float AnimSpeed;
    float TrailScale;
};

class D3D11Engine {
public:
    bool Initialize(IDXGISwapChain* swapChain, AddonAPI_t* api) {
        m_API = api;
        if (!swapChain) return false;

        HRESULT hr = swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(m_Device.GetAddressOf()));
        if (FAILED(hr) || !m_Device) return false;

        m_Device->GetImmediateContext(m_Context.GetAddressOf());

        ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
        hr = D3DCompile(g_PathingHlslSource, std::strlen(g_PathingHlslSource), "PathingShaders.hlsl",
                        nullptr, nullptr, "TrailVS_Main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                        vsBlob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) return false;

        hr = D3DCompile(g_PathingHlslSource, std::strlen(g_PathingHlslSource), "PathingShaders.hlsl",
                        nullptr, nullptr, "TrailPS_Main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                        psBlob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) return false;

        m_Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_TrailVS.GetAddressOf());
        m_Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_TrailPS.GetAddressOf());

        D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        m_Device->CreateInputLayout(layoutDesc, 4, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_InputLayout.GetAddressOf());

        D3D11_BUFFER_DESC cbDesc{};
        cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        cbDesc.ByteWidth = sizeof(FrameConstantBuffer_t);
        m_Device->CreateBuffer(&cbDesc, nullptr, m_FrameCB.GetAddressOf());

        cbDesc.ByteWidth = sizeof(ObjectConstantBuffer_t);
        m_Device->CreateBuffer(&cbDesc, nullptr, m_ObjectCB.GetAddressOf());

        D3D11_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].BlendEnable           = TRUE;
        blendDesc.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        m_Device->CreateBlendState(&blendDesc, m_BlendState.GetAddressOf());

        // Disable depth testing during RT_Render overlay pass so trails never get rejected by cleared/UI depth buffer!
        D3D11_DEPTH_STENCIL_DESC dsDesc{};
        dsDesc.DepthEnable    = FALSE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsDesc.DepthFunc      = D3D11_COMPARISON_ALWAYS;
        m_Device->CreateDepthStencilState(&dsDesc, m_NoDepthState.GetAddressOf());

        // Disable backface culling so trails are visible from all camera angles
        D3D11_RASTERIZER_DESC rsDesc{};
        rsDesc.FillMode      = D3D11_FILL_SOLID;
        rsDesc.CullMode      = D3D11_CULL_NONE;
        rsDesc.ScissorEnable = FALSE;
        dsDesc.DepthEnable   = FALSE;
        rsDesc.DepthClipEnable = TRUE;
        m_Device->CreateRasterizerState(&rsDesc, m_NoCullState.GetAddressOf());

        // Linear Wrap Sampler for scrolling trail textures
        D3D11_SAMPLER_DESC sampDesc{};
        sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MinLOD         = 0;
        sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;
        m_Device->CreateSamplerState(&sampDesc, m_LinearWrapSampler.GetAddressOf());

        // Create default procedural chevron texture SRV in case a trail texture isn't loaded yet
        CreateDefaultChevronTexture();

        m_Initialized = true;
        return true;
    }

    ID3D11ShaderResourceView* LoadTextureFromMemory(const std::string& key, const void* data, size_t size) {
        if (!m_Device || !data || size == 0) return nullptr;
        auto it = m_NativeTextureCache.find(key);
        if (it != m_NativeTextureCache.end()) {
            return it->second.Get();
        }
        int w = 0, h = 0, channels = 0;
        unsigned char* pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(data),
            static_cast<int>(size),
            &w, &h, &channels, 4
        );
        if (!pixels || w <= 0 || h <= 0) {
            m_NativeTextureCache[key] = nullptr;
            return nullptr;
        }

        // Compute mip levels or single level RGBA8 texture
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width            = static_cast<UINT>(w);
        desc.Height           = static_cast<UINT>(h);
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData{};
        initData.pSysMem     = pixels;
        initData.SysMemPitch = static_cast<UINT>(w * 4);

        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (SUCCEEDED(m_Device->CreateTexture2D(&desc, &initData, tex.GetAddressOf())) && tex) {
            m_Device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf());
        }
        stbi_image_free(pixels);

        m_NativeTextureCache[key] = srv;
        return srv.Get();
    }

    ID3D11ShaderResourceView* GetCachedTextureSRV(const std::string& key) const {
        auto it = m_NativeTextureCache.find(key);
        return (it != m_NativeTextureCache.end()) ? it->second.Get() : nullptr;
    }

    ID3D11SamplerState* GetWrapSampler() const { return m_LinearWrapSampler.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_Context.Get(); }
    ID3D11ShaderResourceView* GetDefaultChevronSRV() const { return m_DefaultChevronSRV.Get(); }

    void CreateDefaultChevronTexture() {
        if (!m_Device) return;
        constexpr int W = 64;
        constexpr int H = 64;
        uint32_t pixels[W * H];
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float u = (x - W * 0.5f) / (W * 0.5f);
                float v = static_cast<float>(y) / H;
                float edgeFade = std::clamp(1.0f - std::fabs(u), 0.0f, 1.0f);
                float chevron = std::fmod(v + std::fabs(u) * 0.45f, 0.5f);
                float alpha = (chevron < 0.30f ? 0.92f : 0.25f) * edgeFade;
                uint8_t a = static_cast<uint8_t>(alpha * 255.0f);
                pixels[y * W + x] = (a << 24) | 0x00FFFFFFu;
            }
        }

        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width            = W;
        texDesc.Height           = H;
        texDesc.MipLevels        = 1;
        texDesc.ArraySize        = 1;
        texDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage            = D3D11_USAGE_IMMUTABLE;
        texDesc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData{};
        initData.pSysMem     = pixels;
        initData.SysMemPitch = W * sizeof(uint32_t);

        ComPtr<ID3D11Texture2D> tex;
        if (SUCCEEDED(m_Device->CreateTexture2D(&texDesc, &initData, tex.GetAddressOf()))) {
            m_Device->CreateShaderResourceView(tex.Get(), nullptr, m_DefaultChevronSRV.GetAddressOf());
        }
    }

    // Releases every cached pack texture. Callers MUST drop all raw NativeSRV pointers
    // (g_Markers / g_Trails) before or together with this call.
    void ClearTextureCache() {
        m_NativeTextureCache.clear();
    }

    void Shutdown() {
        ClearTextureCache();   // FIX 1: previously leaked SRVs (and kept the device alive) past unload
        m_DefaultChevronSRV.Reset();
        m_LinearWrapSampler.Reset();
        m_NoCullState.Reset();
        m_NoDepthState.Reset();
        m_BlendState.Reset();
        m_ObjectCB.Reset();
        m_FrameCB.Reset();
        m_InputLayout.Reset();
        m_TrailPS.Reset();
        m_TrailVS.Reset();
        m_Context.Reset();
        m_Device.Reset();
        m_Initialized = false;
    }

    void PrecomputeTrailCornerRights(RuntimeTrail& trail) {
        trail.CornerRights.clear();
        trail.CornerRights.resize(trail.Segments.size());
        Vec3 up{ 0.0f, 1.0f, 0.0f };

        for (size_t sIdx = 0; sIdx < trail.Segments.size(); ++sIdx) {
            const auto& seg = trail.Segments[sIdx];
            const size_t nPts = seg.size();
            if (nPts < 2) continue;

            std::vector<Vec3> segRights(nPts - 1, Vec3{ 1.0f, 0.0f, 0.0f });
            Vec3 lastRight{ 0.0f, 0.0f, 0.0f };
            for (size_t i = 0; i + 1 < nPts; ++i) {
                Vec3 diff = seg[i + 1] - seg[i];
                float lenSq = diff.LengthSq();
                if (lenSq < 1e-8f) {
                    segRights[i] = (lastRight.LengthSq() > 1e-4f) ? lastRight : Vec3{ 1.0f, 0.0f, 0.0f };
                    continue;
                }
                float invLen = 1.0f / std::sqrt(lenSq);
                Vec3 tangent = diff * invLen;
                Vec3 r = trail.IsWall
                    ? Vec3::Cross(tangent, Vec3::Cross(tangent, up))
                    : Vec3::Cross(up, tangent);

                if (r.LengthSq() < 0.02f) {
                    r = (lastRight.LengthSq() > 1e-4f) ? lastRight : Vec3{ 1.0f, 0.0f, 0.0f };
                } else {
                    r = r.Normalized();
                }
                if (lastRight.LengthSq() > 1e-4f && Vec3::Dot(r, lastRight) < 0.0f) {
                    r = r * -1.0f;
                }
                segRights[i] = r;
                lastRight = r;
            }

            auto& cornerRights = trail.CornerRights[sIdx];
            cornerRights.resize(nPts);
            cornerRights[0] = segRights[0];
            cornerRights[nPts - 1] = segRights[nPts - 2];
            for (size_t i = 1; i + 1 < nPts; ++i) {
                Vec3 sum = segRights[i - 1] + segRights[i];
                if (sum.LengthSq() > 1e-4f) {
                    Vec3 avg = sum.Normalized();
                    float miterCos = std::clamp(Vec3::Dot(avg, segRights[i]), 0.74f, 1.0f);
                    cornerRights[i] = avg * (1.0f / miterCos);
                } else {
                    cornerRights[i] = segRights[i];
                }
            }
        }
    }

    static float ComputeClipW(const Vec3& worldPos, const Mat4x4& viewProj) {
        return worldPos.x * viewProj.m[0][3] + worldPos.y * viewProj.m[1][3] + worldPos.z * viewProj.m[2][3] + viewProj.m[3][3];
    }

    static bool ProjectWorldToScreen(const Vec3& worldPos, const Mat4x4& viewProj, float width, float height, ImVec2& outScreen, float& outDepth, float minClipW = 0.2f) {
        float clipX = worldPos.x * viewProj.m[0][0] + worldPos.y * viewProj.m[1][0] + worldPos.z * viewProj.m[2][0] + viewProj.m[3][0];
        float clipY = worldPos.x * viewProj.m[0][1] + worldPos.y * viewProj.m[1][1] + worldPos.z * viewProj.m[2][1] + viewProj.m[3][1];
        float clipW = worldPos.x * viewProj.m[0][3] + worldPos.y * viewProj.m[1][3] + worldPos.z * viewProj.m[2][3] + viewProj.m[3][3];

        outDepth = clipW;
        if (clipW <= minClipW) return false;

        float ndcX = clipX / clipW;
        float ndcY = clipY / clipW;
        outScreen.x = (ndcX * 0.5f + 0.5f) * width;
        outScreen.y = (0.5f - ndcY * 0.5f) * height;
        return true;
    }

    void RenderWorld(
        const Mumble::Data& mumble,
        float fovRadians,
        std::vector<RuntimeTrail>& trails,
        std::vector<RuntimeMarker>& markers,
        bool renderTrails,
        bool renderMarkers,
        float playerCutoutRadius)
    {
        if (!m_Initialized || !m_Context) return;

        ImGuiIO& io = ImGui::GetIO();
        float width  = (io.DisplaySize.x > 0.0f) ? io.DisplaySize.x : 1920.0f;
        float height = (io.DisplaySize.y > 0.0f) ? io.DisplaySize.y : 1080.0f;
        float aspect = width / height;

        Vec3 eye{ mumble.CameraPosition.X, mumble.CameraPosition.Y, mumble.CameraPosition.Z };
        Vec3 front{ mumble.CameraFront.X, mumble.CameraFront.Y, mumble.CameraFront.Z };
        if (front.LengthSq() < 1e-4f) return;
        Vec3 up{ 0.0f, 1.0f, 0.0f };

        float fov = (fovRadians > 0.1f && fovRadians < 3.0f) ? fovRadians : 0.872665f;
        Mat4x4 view = Mat4x4::LookToLH(eye, front, up);
        Mat4x4 proj = Mat4x4::PerspectiveFovLH(fov, aspect, 0.15f, 2500.0f);
        Mat4x4 viewProj = Mat4x4::Multiply(view, proj);

        int32_t currentMapId = static_cast<int32_t>(mumble.Context.MapID);
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (!drawList) return;

        Vec3 avatar{ mumble.AvatarPosition.X, mumble.AvatarPosition.Y, mumble.AvatarPosition.Z };
        Vec3 camToAvatar = avatar - eye;
        float camToAvatarLenSq = camToAvatar.LengthSq();
        float timeSec = static_cast<float>(GetTickCount64() % 3600000) * 0.001f;

        // Helper to compute smooth Player Spherical Cutout + Sightline Cutout opacity multiplier [0.0 .. 1.0]
        auto computePlayerCutoutMultiplier = [&](const Vec3& pt, bool canFade) -> float {
            if (!canFade || playerCutoutRadius <= 0.05f) return 1.0f;

            // 1. Oblate 3D spherical distance around player character (vertical Y weighted by 0.45 so trails
            //    on uneven ground or below mounts still cleanly fade out around the player!)
            float dx = pt.x - avatar.x;
            float dy = (pt.y - avatar.y) * 0.45f;
            float dz = pt.z - avatar.z;
            float minDist = std::sqrt(dx * dx + dy * dy + dz * dz);

            // 2. Camera-to-Player sightline cylinder cutout (matching Blish HUD trail.hlsl ProjectedPosition)
            //    when camera is zoomed out > 1.5m from the player
            if (camToAvatarLenSq > 2.25f) {
                float tLine = Vec3::Dot(pt - eye, camToAvatar) / camToAvatarLenSq;
                if (tLine >= 0.35f && tLine <= 1.05f) {
                    Vec3 closestOnSightline = eye + camToAvatar * std::clamp(tLine, 0.35f, 1.0f);
                    float sightDist = (pt - closestOnSightline).Length();
                    // Sightline cylinder radius is proportional to playerCutoutRadius
                    float eqDist = sightDist * 1.35f;
                    if (eqDist < minDist) minDist = eqDist;
                }
            }

            if (minDist >= playerCutoutRadius) return 1.0f;
            float innerZeroRadius = playerCutoutRadius * 0.28f;
            if (minDist <= innerZeroRadius) return 0.0f;

            float t = (minDist - innerZeroRadius) / std::max(0.05f, playerCutoutRadius - innerZeroRadius);
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t); // smoothstep from 0.0 (completely faded) to 1.0
        };

        // 1. Render Textured & Animated 3D Trail Ribbons onto ImGui::GetBackgroundDrawList()
        //    Uses Blish HUD world-up mitered ribbon normals + 0.45m 3D subdivision + 3D near-plane clipping
        //    so arrows never twist/distort at steep angles and trails never vanish when crossing the camera!
        if (renderTrails && !trails.empty() && g_TrailOpacity > 0.005f) {
            drawList->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
                auto* eng = static_cast<D3D11Engine*>(cmd->UserCallbackData);
                if (eng && eng->GetContext() && eng->GetWrapSampler()) {
                    ID3D11SamplerState* s = eng->GetWrapSampler();
                    eng->GetContext()->PSSetSamplers(0, 1, &s);
                }
            }, this);

            for (auto& trail : trails) {
                if (g_OnlyShowCurrentMap ? (trail.MapId != currentMapId) : (trail.MapId != 0 && trail.MapId != currentMapId)) continue;
                if (!trail.IsVisible || !trail.InGameVisibility || trail.Segments.empty()) continue;

                if (trail.CornerRights.size() != trail.Segments.size()) {
                    PrecomputeTrailCornerRights(trail);
                }

                ID3D11ShaderResourceView* srv = ResolveTrailSRV(trail);
                if (!srv) {
                    srv = m_DefaultChevronSRV.Get();
                }

                float halfWidth = 0.508f * trail.TrailScale;
                float fadeNearM = trail.FadeNear * INCHES_TO_METERS;
                float fadeFarM  = trail.FadeFar * INCHES_TO_METERS;
                float vScroll   = +(timeSec * trail.AnimSpeed);
                float subStepM  = std::clamp(g_TrailSubdivisionStep, 0.35f, 2.5f);
                float maxDistM  = (g_MaxTrailDistance > 10.0f) ? g_MaxTrailDistance : 0.0f;

                for (size_t sIdx = 0; sIdx < trail.Segments.size(); ++sIdx) {
                    const auto& seg = trail.Segments[sIdx];
                    const size_t nPts = seg.size();
                    if (nPts < 2) continue;
                    const auto& cornerRights = trail.CornerRights[sIdx];
                    if (cornerRights.size() != nPts) continue;

                    float arcV = 0.0f;

                    for (size_t i = 0; i + 1 < nPts; ++i) {
                        const Vec3& p0 = seg[i];
                        const Vec3& p1 = seg[i + 1];
                        Vec3 diff = p1 - p0;
                        float segLenSq = diff.LengthSq();
                        if (segLenSq < 1e-6f) continue;
                        float segLen = std::sqrt(segLenSq);

                        float nextArcV = arcV - segLen / (20.0f * INCHES_TO_METERS * 2.0f);

                        // High-performance distance culling: discard distant segments
                        float cutoffM = (fadeFarM > 0.0f) ? (maxDistM > 0.0f ? std::min(fadeFarM, maxDistM) : fadeFarM) : maxDistM;
                        if (cutoffM > 0.0f) {
                            float tClosest = std::clamp(Vec3::Dot(eye - p0, diff) / segLenSq, 0.0f, 1.0f);
                            Vec3 closestPt = p0 + diff * tClosest;
                            if ((closestPt - eye).LengthSq() > (cutoffM + 2.0f) * (cutoffM + 2.0f)) {
                                arcV = nextArcV;
                                continue;
                            }
                        }

                        const Vec3& r0 = cornerRights[i];
                        const Vec3& r1 = cornerRights[i + 1];

                        // Adaptive LOD: subdivide finely near player/camera, coarser in the distance to save CPU/GPU vertices
                        float midDistSq = (((p0 + p1) * 0.5f) - eye).LengthSq();
                        float effectiveStep = (midDistSq > 900.0f) ? (subStepM * 2.5f) : subStepM;
                        int steps = std::clamp(static_cast<int>(std::ceil(segLen / effectiveStep)), 1, 128);
                        float invSteps = 1.0f / static_cast<float>(steps);

                        for (int s = 0; s < steps; ++s) {
                            float t0 = static_cast<float>(s) * invSteps;
                            float t1 = static_cast<float>(s + 1) * invSteps;

                            Vec3 a  = p0 + diff * t0;
                            Vec3 b  = p0 + diff * t1;
                            Vec3 subMid = (a + b) * 0.5f;

                            float distCam = (subMid - eye).Length();
                            if (fadeFarM > 0.0f && distCam >= fadeFarM) continue;
                            if (maxDistM > 0.0f && distCam >= maxDistM) continue;

                            float alpha = trail.TintColor.w * g_TrailOpacity;
                            if (fadeFarM > fadeNearM && distCam > fadeNearM) {
                                alpha *= std::clamp(1.0f - (distCam - fadeNearM) / (fadeFarM - fadeNearM), 0.0f, 1.0f);
                            }

                            alpha *= computePlayerCutoutMultiplier(subMid, trail.CanFade);
                            if (alpha <= 0.015f) continue;

                            Vec3 ra = r0 + (r1 - r0) * t0;
                            Vec3 rb = r0 + (r1 - r0) * t1;
                            float va = arcV + (nextArcV - arcV) * t0 + vScroll;
                            float vb = arcV + (nextArcV - arcV) * t1 + vScroll;

                            // Clip sub-segment against the 3D camera near plane (clipW = 0.22f) so trails never vanish
                            // when one end passes under or behind the camera!
                            constexpr float kNearW = 0.22f;
                            float wa = ComputeClipW(a, viewProj);
                            float wb = ComputeClipW(b, viewProj);
                            if (wa < kNearW && wb < kNearW) continue;
                            if (wa < kNearW) {
                                float clipT = std::clamp((kNearW - wa) / (wb - wa), 0.0f, 1.0f);
                                a  = a + (b - a) * clipT;
                                ra = ra + (rb - ra) * clipT;
                                va = va + (vb - va) * clipT;
                            } else if (wb < kNearW) {
                                float clipT = std::clamp((kNearW - wa) / (wb - wa), 0.0f, 1.0f);
                                b  = a + (b - a) * clipT;
                                rb = ra + (rb - ra) * clipT;
                                vb = va + (vb - va) * clipT;
                            }

                            Vec3 w0L = a - ra * halfWidth;
                            Vec3 w0R = a + ra * halfWidth;
                            Vec3 w1R = b + rb * halfWidth;
                            Vec3 w1L = b - rb * halfWidth;

                            ImVec2 s0L{}, s0R{}, s1R{}, s1L{};
                            float d0L = 0.f, d0R = 0.f, d1R = 0.f, d1L = 0.f;
                            if (ProjectWorldToScreen(w0L, viewProj, width, height, s0L, d0L, 0.05f) &&
                                ProjectWorldToScreen(w0R, viewProj, width, height, s0R, d0R, 0.05f) &&
                                ProjectWorldToScreen(w1R, viewProj, width, height, s1R, d1R, 0.05f) &&
                                ProjectWorldToScreen(w1L, viewProj, width, height, s1L, d1L, 0.05f))
                            {
                                // Skip off-screen quads with generous guard band
                                float minX = std::min({ s0L.x, s0R.x, s1R.x, s1L.x });
                                float maxX = std::max({ s0L.x, s0R.x, s1R.x, s1L.x });
                                float minY = std::min({ s0L.y, s0R.y, s1R.y, s1L.y });
                                float maxY = std::max({ s0L.y, s0R.y, s1R.y, s1L.y });
                                if (maxX < -128.0f || minX > width + 128.0f || maxY < -128.0f || minY > height + 128.0f) {
                                    continue;
                                }

                                ImU32 col = ImGui::ColorConvertFloat4ToU32(
                                    ImVec4(trail.TintColor.x, trail.TintColor.y, trail.TintColor.z, alpha)
                                );

                                if (srv) {
                                    drawList->AddImageQuad(
                                        reinterpret_cast<ImTextureID>(srv),
                                        s0L, s0R, s1R, s1L,
                                        ImVec2(0.0f, va),
                                        ImVec2(1.0f, va),
                                        ImVec2(1.0f, vb),
                                        ImVec2(0.0f, vb),
                                        col
                                    );
                                } else {
                                    drawList->AddQuadFilled(s0L, s0R, s1R, s1L, col);
                                }
                            }
                        }

                        arcV = nextArcV;
                    }
                }
            }
        }

        // 2. Render 3D World-Space POI Icons onto ImGui Background DrawList using actual PNG textures!
        if (renderMarkers && !markers.empty()) {
            for (auto& m : markers) {
                if (g_OnlyShowCurrentMap ? (m.MapId != currentMapId) : (m.MapId != 0 && m.MapId != currentMapId)) continue;
                if (!m.IsVisible || !m.InGameVisibility) continue;

                float bounceY = 0.0f;
                float distToPlayer = (m.Position - avatar).Length();
                if (m.HasBounce && distToPlayer <= m.TriggerRange * 2.5f) {
                    if (!m.BounceIsRise) {
                        bounceY = std::fabs(std::sin(timeSec * (3.14159f / std::max(0.2f, m.BounceDuration)))) * m.BounceHeight;
                    } else {
                        bounceY = m.BounceHeight * 0.75f;
                    }
                }

                Vec3 worldPos{ m.Position.x, m.Position.y + m.HeightOffset + bounceY, m.Position.z };
                ImVec2 screenPt{};
                float depth = 0.0f;
                if (!ProjectWorldToScreen(worldPos, viewProj, width, height, screenPt, depth)) continue;
                if (screenPt.x < -64.0f || screenPt.x > width + 64.0f || screenPt.y < -64.0f || screenPt.y > height + 64.0f) continue;

                float fadeNearM = m.FadeNear * INCHES_TO_METERS;
                float fadeFarM  = m.FadeFar * INCHES_TO_METERS;
                float alpha     = m.TintColor.w;
                if (fadeFarM > 0.0f && depth > fadeNearM && fadeFarM > fadeNearM) {
                    alpha *= std::max(0.0f, 1.0f - (depth - fadeNearM) / (fadeFarM - fadeNearM));
                }
                alpha *= computePlayerCutoutMultiplier(worldPos, true);
                if (alpha <= 0.02f) continue;

                float halfSize = std::clamp((550.0f / depth) * m.IconSize, 8.0f, 42.0f);
                ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(m.TintColor.x, m.TintColor.y, m.TintColor.z, alpha));

                ID3D11ShaderResourceView* iconSRV = ResolveMarkerSRV(m);

                if (iconSRV) {
                    drawList->AddImage(
                        reinterpret_cast<ImTextureID>(iconSRV),
                        ImVec2(screenPt.x - halfSize, screenPt.y - halfSize),
                        ImVec2(screenPt.x + halfSize, screenPt.y + halfSize),
                        ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f),
                        col
                    );
                } else {
                    // Fallback tactical icon if no iconFile was specified on the POI
                    ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.05f, 0.07f, 0.12f, alpha));
                    drawList->AddQuadFilled(
                        ImVec2(screenPt.x, screenPt.y - halfSize * 0.7f),
                        ImVec2(screenPt.x + halfSize * 0.7f, screenPt.y),
                        ImVec2(screenPt.x, screenPt.y + halfSize * 0.7f),
                        ImVec2(screenPt.x - halfSize * 0.7f, screenPt.y),
                        col
                    );
                    drawList->AddQuad(
                        ImVec2(screenPt.x, screenPt.y - halfSize * 0.7f),
                        ImVec2(screenPt.x + halfSize * 0.7f, screenPt.y),
                        ImVec2(screenPt.x, screenPt.y + halfSize * 0.7f),
                        ImVec2(screenPt.x - halfSize * 0.7f, screenPt.y),
                        borderCol,
                        1.5f
                    );
                }
            }

            // 2b. Render Blish HUD Pathing InfoModifier / Hint Banners for focused or nearby markers!
            //     Even markers with InGameVisibility=false (like TehsTrails HeartFollower when visible pointer is off,
            //     or transparent trigger markers) display their InfoText when the player is within InfoRange/TriggerRange.
            const RuntimeMarker* bestHintMarker = nullptr;
            float bestHintDist = 999999.0f;
            ImVec2 bestHintScreenPt(width * 0.5f, height * 0.28f);
            bool bestHintProjected = false;

            for (const auto& m : markers) {
                if (g_OnlyShowCurrentMap ? (m.MapId != currentMapId) : (m.MapId != 0 && m.MapId != currentMapId)) continue;
                if (!m.IsVisible || m.InfoText.empty()) continue;

                float distToPlayer = (m.Position - avatar).Length();
                float effectiveRange = std::max(m.InfoRange, m.TriggerRange);
                if (effectiveRange <= 0.0f) effectiveRange = 15.0f;

                if (distToPlayer <= effectiveRange) {
                    if (distToPlayer < bestHintDist) {
                        bestHintDist = distToPlayer;
                        bestHintMarker = &m;

                        Vec3 worldPos{ m.Position.x, m.Position.y + m.HeightOffset + 0.8f, m.Position.z };
                        ImVec2 sp{};
                        float dp = 0.0f;
                        if (ProjectWorldToScreen(worldPos, viewProj, width, height, sp, dp) &&
                            sp.x >= 80.0f && sp.x <= width - 80.0f && sp.y >= 60.0f && sp.y <= height - 100.0f) {
                            bestHintScreenPt = ImVec2(sp.x, sp.y - 36.0f);
                            bestHintProjected = true;
                        } else {
                            bestHintScreenPt = ImVec2(width * 0.5f, height * 0.26f);
                            bestHintProjected = false;
                        }
                    }
                }
            }

            if (bestHintMarker && !bestHintMarker->InfoText.empty()) {
                const std::string& text = bestHintMarker->InfoText;
                float wrapWidth = 380.0f;
                ImFont* font = ImGui::GetFont();
                float fontSize = ImGui::GetFontSize();
                ImVec2 textSize = font
                    ? font->CalcTextSizeA(fontSize, FLT_MAX, wrapWidth, text.c_str())
                    : ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth);

                float padX = 14.0f;
                float padY = 10.0f;
                float boxW = textSize.x + padX * 2.0f;
                float boxH = textSize.y + padY * 2.0f;

                // Keep hint banner cleanly on screen above the marker or centered top-middle
                float cx = std::clamp(bestHintScreenPt.x, boxW * 0.5f + 16.0f, width - boxW * 0.5f - 16.0f);
                float bottomY = bestHintProjected
                    ? std::clamp(bestHintScreenPt.y, boxH + 20.0f, height - 120.0f)
                    : std::clamp(height * 0.26f + boxH * 0.5f, boxH + 20.0f, height - 120.0f);

                ImVec2 boxMin(cx - boxW * 0.5f, bottomY - boxH);
                ImVec2 boxMax(cx + boxW * 0.5f, bottomY);

                // Tactical dark glass background + golden/cyan border matching GW2 / Blish HUD tooltip style
                drawList->AddRectFilled(boxMin, boxMax, IM_COL32(14, 18, 26, 232), 6.0f);
                drawList->AddRect(boxMin, boxMax, IM_COL32(74, 213, 255, 220), 6.0f, 0, 1.8f);

                if (font) {
                    drawList->AddText(font, fontSize, ImVec2(boxMin.x + padX, boxMin.y + padY),
                                      IM_COL32(245, 248, 255, 255), text.c_str(), nullptr, wrapWidth);
                } else {
                    drawList->AddText(ImVec2(boxMin.x + padX, boxMin.y + padY),
                                      IM_COL32(245, 248, 255, 255), text.c_str());
                }
            }
        }
    }

    // ========================================================================
    // 3. RENDER 2D MINIMAP COMPASS & FULLSCREEN WORLD MAP [M]
    // ========================================================================
    void RenderCompassAndMap(
        const Mumble::Data& mumble,
        const Mumble::Identity* identity,
        float uiScaling,
        std::vector<RuntimeTrail>& trails,
        std::vector<RuntimeMarker>& markers,
        bool enableCompassOverlay,
        bool enableWorldMapOverlay)
    {
        bool isMapOpen = mumble.Context.IsMapOpen != 0;
        if (isMapOpen && !enableWorldMapOverlay) return;
        if (!isMapOpen && !enableCompassOverlay) return;
        if (mumble.Context.Compass.Width < 10 || mumble.Context.Compass.Height < 10) return;
        if (mumble.Context.Compass.Scale <= 0.0001f) return;

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (!drawList) return;

        ImGuiIO& io = ImGui::GetIO();
        float screenW = io.DisplaySize.x;
        float screenH = io.DisplaySize.y;
        if (screenW <= 0.0f || screenH <= 0.0f) return;

        // Determine effective UI scaling factor from NexusLink or Mumble Identity UISize
        float scaleUI = 1.0f;
        if (uiScaling > 0.3f && uiScaling < 4.0f && std::fabs(uiScaling - 1.0f) > 0.001f) {
            scaleUI = uiScaling;
        } else if (identity) {
            switch (identity->UISize) {
                case Mumble::EUIScale::Small:  scaleUI = 0.898f; break;
                case Mumble::EUIScale::Normal: scaleUI = 1.000f; break;
                case Mumble::EUIScale::Large:  scaleUI = 1.113f; break;
                case Mumble::EUIScale::Larger: scaleUI = 1.224f; break;
                default:                       scaleUI = 1.000f; break;
            }
        }
        int32_t currentMapId = static_cast<int32_t>(mumble.Context.MapID);

        // Compute bounds of the Minimap (Compass) or Fullscreen Map
        ImVec2 clipMin(0.0f, 0.0f);
        ImVec2 clipMax(screenW, screenH);

        if (!isMapOpen) {
            float compW = static_cast<float>(mumble.Context.Compass.Width) * scaleUI;
            float compH = static_cast<float>(mumble.Context.Compass.Height) * scaleUI;
            if (mumble.Context.IsCompassTopRight) {
                clipMin = ImVec2(screenW - compW - 1.0f, 1.0f);
                clipMax = ImVec2(screenW - 1.0f, 1.0f + compH);
            } else {
                float bottomMargin = 37.0f * scaleUI;
                clipMin = ImVec2(screenW - compW - 1.0f, screenH - compH - bottomMargin);
                clipMax = ImVec2(screenW - 1.0f, screenH - bottomMargin);
            }
        }

        ImVec2 mapCenterScreen((clipMin.x + clipMax.x) * 0.5f, (clipMin.y + clipMax.y) * 0.5f);

        constexpr float METERS_TO_CONTINENT = METERS_TO_INCHES / 24.0f; // 1.64041995f
        float playerContX = mumble.Context.Compass.PlayerPosition.X;
        float playerContY = mumble.Context.Compass.PlayerPosition.Y;
        float centerContX = mumble.Context.Compass.Center.X;
        float centerContY = mumble.Context.Compass.Center.Y;

        float mapScale = mumble.Context.Compass.Scale / scaleUI;
        if (mapScale <= 0.0001f) return;

        bool rotateCompass = (!isMapOpen && mumble.Context.IsCompassRotating != 0);
        float cosRot = std::cos(mumble.Context.Compass.Rotation);
        float sinRot = std::sin(mumble.Context.Compass.Rotation);

        auto worldToMapScreen = [&](float worldX, float worldZ) -> ImVec2 {
            float contX = playerContX + (worldX - mumble.AvatarPosition.X) * METERS_TO_CONTINENT;
            float contY = playerContY - (worldZ - mumble.AvatarPosition.Z) * METERS_TO_CONTINENT;

            float dx = (contX - centerContX) / mapScale;
            float dy = (contY - centerContY) / mapScale;

            if (rotateCompass) {
                float rx = dx * cosRot - dy * sinRot;
                float ry = dx * sinRot + dy * cosRot;
                dx = rx;
                dy = ry;
            }
            return ImVec2(mapCenterScreen.x + dx, mapCenterScreen.y + dy);
        };

        drawList->PushClipRect(clipMin, clipMax, true);

        // 1. Draw Trails on Minimap / World Map
        for (const auto& trail : trails) {
            if (g_OnlyShowCurrentMap ? (trail.MapId != currentMapId) : (trail.MapId != 0 && trail.MapId != currentMapId)) continue;
            if (!trail.IsVisible) continue;
            if (isMapOpen && !trail.MapVisibility) continue;
            if (!isMapOpen && !trail.MiniMapVisibility) continue;

            ImU32 trailCol = ImGui::ColorConvertFloat4ToU32(
                ImVec4(trail.TintColor.x, trail.TintColor.y, trail.TintColor.z, std::min(1.0f, (trail.TintColor.w + 0.15f) * g_TrailOpacity))
            );
            float thickness = std::clamp(3.0f * trail.TrailScale, 1.5f, 6.0f);

            for (const auto& seg : trail.Segments) {
                if (seg.size() < 2) continue;
                for (size_t i = 0; i < seg.size() - 1; ++i) {
                    ImVec2 p1 = worldToMapScreen(seg[i].x, seg[i].z);
                    ImVec2 p2 = worldToMapScreen(seg[i + 1].x, seg[i + 1].z);
                    drawList->AddLine(p1, p2, trailCol, thickness);
                }
            }
        }

        // 2. Draw POI Icons on Minimap / World Map
        for (auto& m : markers) {
            if (g_OnlyShowCurrentMap ? (m.MapId != currentMapId) : (m.MapId != 0 && m.MapId != currentMapId)) continue;
            if (!m.IsVisible) continue;
            if (isMapOpen && !m.MapVisibility) continue;
            if (!isMapOpen && !m.MiniMapVisibility) continue;

            ImVec2 pt = worldToMapScreen(m.Position.x, m.Position.z);
            if (pt.x < clipMin.x - 24.0f || pt.x > clipMax.x + 24.0f ||
                pt.y < clipMin.y - 24.0f || pt.y > clipMax.y + 24.0f) {
                continue;
            }

            float halfSz = std::clamp(m.MapDisplaySize * 0.5f * scaleUI, 6.0f, 20.0f);
            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(m.TintColor.x, m.TintColor.y, m.TintColor.z, m.TintColor.w));

            ID3D11ShaderResourceView* iconSRV = ResolveMarkerSRV(m);

            if (iconSRV) {
                drawList->AddImage(
                    reinterpret_cast<ImTextureID>(iconSRV),
                    ImVec2(pt.x - halfSz, pt.y - halfSz),
                    ImVec2(pt.x + halfSz, pt.y + halfSz),
                    ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f),
                    col
                );
            } else {
                drawList->AddCircleFilled(pt, halfSz * 0.6f, col);
                drawList->AddCircle(pt, halfSz * 0.6f, IM_COL32(10, 15, 25, 230), 12, 1.5f);
            }
        }

        drawList->PopClipRect();
    }

private:
    bool                                m_Initialized = false;
    AddonAPI_t*                         m_API = nullptr;
    ComPtr<ID3D11Device>                m_Device;
    ComPtr<ID3D11DeviceContext>         m_Context;
    ComPtr<ID3D11VertexShader>          m_TrailVS;
    ComPtr<ID3D11PixelShader>           m_TrailPS;
    ComPtr<ID3D11InputLayout>           m_InputLayout;
    ComPtr<ID3D11Buffer>                m_FrameCB;
    ComPtr<ID3D11Buffer>                m_ObjectCB;
    ComPtr<ID3D11BlendState>            m_BlendState;
    ComPtr<ID3D11DepthStencilState>     m_NoDepthState;
    ComPtr<ID3D11RasterizerState>       m_NoCullState;
    ComPtr<ID3D11SamplerState>          m_LinearWrapSampler;
    ComPtr<ID3D11ShaderResourceView>    m_DefaultChevronSRV;
    std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> m_NativeTextureCache;
};

// ============================================================================
// 7. GLOBAL ADDON STATE & EMBEDDED LUA 5.4 SCRIPTING ENGINE
// ============================================================================
static AddonAPI_t*                                      g_API                   = nullptr;
static Mumble::Data*                                    g_Mumble                = nullptr;
static Mumble::Identity*                                g_MumbleIdentity        = nullptr;
static NexusLinkData_t*                                 g_NexusLink             = nullptr;
static D3D11Engine                                      g_D3DEngine;

static std::vector<std::unique_ptr<CategoryNode>>       g_RootCategories;
static std::unordered_map<std::string, CategoryNode*>   g_CategoryLookup;
static std::vector<RuntimeMarker>                       g_Markers;
static std::vector<RuntimeTrail>                        g_Trails;
static std::vector<LoadedPackSummary>                   g_LoadedPacks;
static std::vector<std::string>                         g_ScannedDirectories;
static std::unordered_map<std::string, int64_t>         g_TriggeredTimestamps;
static std::unordered_set<std::string>                  g_UploadedTextureIds;

static lua_State*                                       g_Lua                   = nullptr;
static std::vector<int>                                 g_LuaTickRefs;
static std::unordered_map<std::string, std::string>     g_LuaScriptsInPack;
static std::vector<std::pair<std::string, std::string>> g_PackEntryLuaScripts;
static std::unordered_map<std::string, std::vector<uint8_t>> g_PackBinaryFiles;
static std::string                                      g_CurrentPackName;
static int32_t                                          g_LastKnownMapId        = -1;
static std::vector<std::string>                         g_LuaConsoleLogs;

static bool                                             g_ShowMainWindow        = false;
static bool                                             g_EnableWorldTrails     = true;
static bool                                             g_EnableWorldMarkers    = true;
static bool                                             g_EnableCompassOverlay  = true;
static bool                                             g_EnableWorldMapOverlay = true;
static bool                                             g_EnablePlayerCutout    = true;
static float                                            g_PlayerCutoutRadius    = 3.5f;
static bool                                             g_EnableAutoTrigger     = true;
bool                                                    g_OnlyShowCurrentMap    = true;
float                                                   g_TrailSubdivisionStep  = 0.65f;
float                                                   g_TrailOpacity          = 1.0f;
float                                                   g_MaxTrailDistance      = 500.0f;
static char                                             g_CategorySearchBuf[128]= {};

// Win32 thread-safe CriticalSection RAII guard (compatible with win32-threading MinGW-w64 without libwinpthread!)
struct Win32Mutex {
    CRITICAL_SECTION cs;
    Win32Mutex() { InitializeCriticalSection(&cs); }
    ~Win32Mutex() { DeleteCriticalSection(&cs); }
};
struct Win32LockGuard {
    CRITICAL_SECTION* cs;
    explicit Win32LockGuard(Win32Mutex& m) : cs(&m.cs) { EnterCriticalSection(cs); }
    ~Win32LockGuard() { LeaveCriticalSection(cs); }
};

// Worker-thread tracking so AddonUnload() can wait for in-flight network jobs instead of
// letting them touch destroyed statics / unloaded code.
static std::atomic<bool> g_ShuttingDown{ false };
static std::atomic<int>  g_ActiveWorkerThreads{ 0 };

// Each worker pins this DLL (GetModuleHandleEx without UNCHANGED_REFCOUNT bumps the module refcount)
// and exits through FreeLibraryAndExitThread. That removes the unload race entirely: the DLL cannot be
// unmapped while a worker is still executing its code, and the final release of the reference happens
// from kernel32 code after our last instruction has run.
struct Win32WorkerCtx {
    std::function<void()> Fn;
    HMODULE               PinnedModule = nullptr;
};

template <typename Fn>
static void SpawnDetachedWin32Thread(Fn&& fn) {
    if (g_ShuttingDown.load()) return;

    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&g_ShuttingDown), &pinned)) {
        pinned = nullptr;   // could not pin; still run, but without the unload guarantee
    }

    g_ActiveWorkerThreads.fetch_add(1);
    auto* ctx = new Win32WorkerCtx{ std::function<void()>(std::forward<Fn>(fn)), pinned };
    HANDLE hThread = CreateThread(
        nullptr,
        0,
        [](LPVOID lpParam) -> DWORD {
            auto* c = static_cast<Win32WorkerCtx*>(lpParam);
            HMODULE hMod = c->PinnedModule;
            c->Fn();
            delete c;                                 // destroys captures while code is still guaranteed mapped
            g_ActiveWorkerThreads.fetch_sub(1);
            if (hMod) FreeLibraryAndExitThread(hMod, 0);   // never returns into our (possibly unloading) code
            return 0;
        },
        ctx,
        0,
        nullptr
    );
    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete ctx;
        g_ActiveWorkerThreads.fetch_sub(1);
        if (pinned) FreeLibrary(pinned);
    }
}

// GW2 API Key & Account Achievement Filtering State
static char                                             g_Gw2ApiKeyBuf[128]     = {};
static bool                                             g_ShowGw2ApiKeyPlain    = false;
static bool                                             g_HideCompletedAchPois  = true;
static Win32Mutex                                       g_Gw2ApiMutex;
static std::unordered_set<int32_t>                      g_CompletedAchievements;
static std::unordered_map<int32_t, std::unordered_set<int32_t>> g_AchievementBits;
static std::string                                      g_Gw2ApiAccountName;
static std::string                                      g_Gw2ApiStatusText      = "Not synced (Paste GW2 API Key with 'account' & 'progression' permissions)";
static std::atomic<bool>                                g_Gw2ApiFetchInProgress{ false };
static std::atomic<bool>                                g_Gw2ApiNeedsResync{ false };

// GitHub .taco / .zip File Manager State (3 Buttons: Paste & Download, Check Updates, Update All Files)
static char                                             g_GithubRepoInputBuf[512] = {};
static Win32Mutex                                       g_TacoManagerMutex;
static std::vector<ManagedTacoRepo>                     g_ManagedTacoRepos;
static std::string                                      g_TacoManagerStatusText = "Paste a GitHub repository URL or release link (e.g. https://github.com/xrandox/TehsTrails)";
static std::atomic<bool>                                g_TacoTaskInProgress{ false };
static std::atomic<bool>                                g_PendingReloadPacksOnMainThread{ false };
static std::atomic<bool>                                g_PendingSaveConfigOnMainThread{ false };

// Persistent user configuration state saved to <GW2>/addons/NexusPathing/config.ini
static std::unordered_map<std::string, bool>            g_SavedPackStates;
static std::unordered_map<std::string, bool>            g_SavedCategoryStates;
static std::string                                      g_ConfigFilePathStr;

// Cached category & map counts (rebuilt ONLY on map change or pack load/toggle, zero per-frame cost!)
static bool                                             g_CategoryCountsCacheDirty  = true;
static int32_t                                          g_CategoryCountsCachedMapId = -1;
static size_t                                           g_CachedActiveMapTrails     = 0;
static size_t                                           g_CachedActiveMapPois       = 0;

static void SaveConfigToDisk();
static void LoadConfigFromDisk();
static void ApplySavedConfigToLoadedPacks();

static void PushLuaLog(const std::string& msg, ELogLevel level = LOGL_INFO) {
    if (g_LuaConsoleLogs.size() >= 50) {
        g_LuaConsoleLogs.erase(g_LuaConsoleLogs.begin());
    }
    g_LuaConsoleLogs.push_back(msg);
    if (g_API && g_API->Log) {
        g_API->Log(level, "NexusPathing::Lua", msg.c_str());
    }
}

// ============================================================================
// Lua hardening helpers (safe error strings, text-only loading, budgeted calls)
// ============================================================================

// lua_tostring() returns NULL for non-string errors (error({}), error(nil)); `std::string + NULL` is UB.
static std::string LuaErrStr(lua_State* L, int idx = -1) {
    if (!L) return "<no lua state>";
    if (const char* s = lua_tostring(L, idx)) return s;
    return std::string("<non-string error: ") + luaL_typename(L, idx) + ">";
}

// luaL_loadbuffer() does NOT skip a UTF-8 BOM (only luaL_loadfile does). Scripts saved by Windows editors
// inside .taco archives start with one and fail with "unexpected symbol near '<\239>'".
// Load as TEXT ONLY (mode "t") so a hostile pack cannot smuggle precompiled bytecode into the VM.
static int LuaLoadText(lua_State* L, const std::string& code, const std::string& chunkName) {
    size_t off = 0;
    if (code.size() >= 3 && static_cast<unsigned char>(code[0]) == 0xEF &&
        static_cast<unsigned char>(code[1]) == 0xBB && static_cast<unsigned char>(code[2]) == 0xBF) {
        off = 3;
    }
    const std::string name = "=" + chunkName;   // '=' => shown verbatim in error messages
    return luaL_loadbufferx(L, code.data() + off, code.size() - off, name.c_str(), "t");
}

// Instruction budget so a runaway `while true do end` in a pack script cannot freeze the render thread.
static constexpr int LUA_HOOK_STEP          = 10000;
static constexpr int LUA_INSTRUCTION_BUDGET = 20000000;   // per top-level call; raise if a big pack.lua needs more
static int           g_LuaHookCounter       = 0;
static int           g_LuaCallDepth         = 0;

static void LuaBudgetHook(lua_State* L, lua_Debug*) {
    g_LuaHookCounter += LUA_HOOK_STEP;
    if (g_LuaHookCounter > LUA_INSTRUCTION_BUDGET) {
        g_LuaHookCounter = 0;
        luaL_error(L, "script exceeded instruction budget (possible infinite loop)");
    }
}

static int LuaMsgHandler(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    if (!msg) {
        // keep non-string error objects intact; just describe them
        msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}

// Drop-in replacement for lua_pcall() with traceback + instruction budget. Same stack contract as lua_pcall.
static int LuaSafeCall(lua_State* L, int nargs, int nresults) {
    const int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, LuaMsgHandler);
    lua_insert(L, base);
    if (g_LuaCallDepth == 0) g_LuaHookCounter = 0;
    ++g_LuaCallDepth;
    const int rc = lua_pcall(L, nargs, nresults, base);
    --g_LuaCallDepth;
    lua_remove(L, base);
    return rc;
}

// Throttle repeated per-frame error spam (a failing Event:OnTick would otherwise log 144x/second).
static void PushLuaLogThrottled(const std::string& msg, ELogLevel level = LOGL_WARNING) {
    static uint64_t s_LastLog = 0;
    static std::string s_LastMsg;
    const uint64_t now = GetTickCount64();
    if (msg == s_LastMsg && now - s_LastLog < 5000) return;
    s_LastMsg = msg;
    s_LastLog = now;
    PushLuaLog(msg, level);
}

// Forward declarations for texture & category helpers used by Lua bindings
using ArchiveFileFetcher = std::function<bool(const std::string& relPath, std::vector<uint8_t>& outBytes)>;
static std::string EnsurePackTextureLoaded(
    const std::string& packName,
    const std::string& relTexturePath,
    const ArchiveFileFetcher& fetchFile,
    ID3D11ShaderResourceView** outNativeSRV = nullptr);
static bool IsCategoryPathEnabled(const std::string& typePath);
static void SetCategoryChildrenEnabled(CategoryNode& node, bool enabled);
static void SyncVisibilityWithCategories();
static void CopyStringToClipboard(const std::string& utf8Text);

static bool FetchFromCachedPackFiles(const std::string& relPath, std::vector<uint8_t>& outBytes) {
    std::string norm = NormalizeArchivePath(relPath);
    auto it = g_PackBinaryFiles.find(norm);
    if (it == g_PackBinaryFiles.end()) {
        std::string baseName = std::filesystem::path(norm).filename().string();
        for (const auto& [k, bytes] : g_PackBinaryFiles) {
            if (std::filesystem::path(k).filename().string() == baseName) {
                it = g_PackBinaryFiles.find(k);
                break;
            }
        }
    }
    if (it != g_PackBinaryFiles.end()) {
        outBytes = it->second;
        return true;
    }
    return false;
}

// Lua C callbacks for Blish HUD Pathing API compatibility
static int Lua_DebugPrint(lua_State* L) {
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        if (i == 1 && lua_istable(L, 1)) continue; // skip self if called as Debug:Print(...)
        const char* s = luaL_tolstring(L, i, nullptr);
        if (s) {
            if (!out.empty()) out += " ";
            out += s;
        }
        lua_pop(L, 1);
    }
    PushLuaLog(out, LOGL_INFO);
    return 0;
}

static int Lua_EventOnTick(lua_State* L) {
    int funcIdx = lua_isfunction(L, 1) ? 1 : (lua_isfunction(L, 2) ? 2 : 0);
    if (funcIdx != 0) {
        if (g_LuaTickRefs.size() >= 256) {            // runaway registration guard (e.g. Event:OnTick inside a loop)
            PushLuaLogThrottled("Event:OnTick: too many handlers registered (limit 256); ignoring");
            return 0;
        }
        lua_pushvalue(L, funcIdx);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        g_LuaTickRefs.push_back(ref);
    }
    return 0;
}

static void PushLuaVector3(lua_State* L, float x, float y, float z);

static int Lua_Vec3_Length(lua_State* L) {
    lua_getfield(L, 1, "X"); float x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 1, "Y"); float y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 1, "Z"); float z = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_pushnumber(L, std::sqrt(x * x + y * y + z * z));
    return 1;
}

static int Lua_Vec3_Add(lua_State* L) {
    lua_getfield(L, 1, "X"); float x1 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 1, "Y"); float y1 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 1, "Z"); float z1 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 2, "X"); float x2 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 2, "Y"); float y2 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 2, "Z"); float z2 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    PushLuaVector3(L, x1 + x2, y1 + y2, z1 + z2);
    return 1;
}

static int Lua_Vec3_Sub(lua_State* L) {
    lua_getfield(L, 1, "X"); float x1 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 1, "Y"); float y1 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 1, "Z"); float z1 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 2, "X"); float x2 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 2, "Y"); float y2 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 2, "Z"); float z2 = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    PushLuaVector3(L, x1 - x2, y1 - y2, z1 - z2);
    return 1;
}

static int Lua_Vec3_Mul(lua_State* L) {
    int vecIdx = lua_istable(L, 1) ? 1 : 2;
    int numIdx = (vecIdx == 1) ? 2 : 1;
    float s = static_cast<float>(lua_tonumber(L, numIdx));
    lua_getfield(L, vecIdx, "X"); float x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, vecIdx, "Y"); float y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, vecIdx, "Z"); float z = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    PushLuaVector3(L, x * s, y * s, z * s);
    return 1;
}

static int Lua_Vec3_Div(lua_State* L) {
    float s = static_cast<float>(lua_tonumber(L, 2));
    if (std::fabs(s) < 1e-6f) s = 1.0f;
    lua_getfield(L, 1, "X"); float x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 1, "Y"); float y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 1, "Z"); float z = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    PushLuaVector3(L, x / s, y / s, z / s);
    return 1;
}

static void PushLuaVector3(lua_State* L, float x, float y, float z) {
    lua_newtable(L);
    lua_pushnumber(L, x); lua_setfield(L, -2, "X");
    lua_pushnumber(L, y); lua_setfield(L, -2, "Y");
    lua_pushnumber(L, z); lua_setfield(L, -2, "Z");
    lua_pushcfunction(L, Lua_Vec3_Length); lua_setfield(L, -2, "Length");

    lua_newtable(L);
    lua_pushcfunction(L, Lua_Vec3_Add); lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, Lua_Vec3_Sub); lua_setfield(L, -2, "__sub");
    lua_pushcfunction(L, Lua_Vec3_Mul); lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, Lua_Vec3_Div); lua_setfield(L, -2, "__div");
    lua_setmetatable(L, -2);
}

static int Lua_IVector3(lua_State* L) {
    int startIdx = lua_istable(L, 1) ? 2 : 1;
    float x = static_cast<float>(luaL_optnumber(L, startIdx, 0.0));
    float y = static_cast<float>(luaL_optnumber(L, startIdx + 1, 0.0));
    float z = static_cast<float>(luaL_optnumber(L, startIdx + 2, 0.0));
    PushLuaVector3(L, x, y, z);
    return 1;
}

static int Lua_IColor(lua_State* L) {
    int startIdx = lua_istable(L, 1) ? 2 : 1;
    float r = static_cast<float>(luaL_optnumber(L, startIdx, 255.0));
    float g = static_cast<float>(luaL_optnumber(L, startIdx + 1, 255.0));
    float b = static_cast<float>(luaL_optnumber(L, startIdx + 2, 255.0));
    float a = static_cast<float>(luaL_optnumber(L, startIdx + 3, 255.0));
    if (r > 1.0f || g > 1.0f || b > 1.0f || a > 1.0f) {
        r /= 255.0f; g /= 255.0f; b /= 255.0f; a /= 255.0f;
    }
    lua_newtable(L);
    lua_pushnumber(L, std::clamp(r, 0.0f, 1.0f)); lua_setfield(L, -2, "R");
    lua_pushnumber(L, std::clamp(g, 0.0f, 1.0f)); lua_setfield(L, -2, "G");
    lua_pushnumber(L, std::clamp(b, 0.0f, 1.0f)); lua_setfield(L, -2, "B");
    lua_pushnumber(L, std::clamp(a, 0.0f, 1.0f)); lua_setfield(L, -2, "A");
    return 1;
}

static void PushLuaGuidObject(lua_State* L, const std::string& guidStr) {
    // Push a standard Lua string with string methods so `..` concatenation, table indexing (`table[marker.Guid]`),
    // equality (`==`), and `:ToBase64()` all work seamlessly in Lua 5.4 without metatable errors!
    lua_pushstring(L, guidStr.c_str());
}

static int Lua_IGuid(lua_State* L) {
    int startIdx = lua_istable(L, 1) ? 2 : 1;
    const char* str = luaL_optstring(L, startIdx, "");
    PushLuaGuidObject(L, str ? str : "");
    return 1;
}

static int Lua_ITexture(lua_State* L) {
    int top = lua_gettop(L);
    const char* pathStr = lua_tostring(L, top);
    lua_newtable(L);
    lua_pushstring(L, pathStr ? pathStr : "");
    lua_setfield(L, -2, "__path");
    return 1;
}

static int Lua_Require(lua_State* L) {
    // NOTE: luaL_checkstring may longjmp, so it runs before any std::string exists in this frame.
    const char* nameC = luaL_checkstring(L, 1);

    std::string key = NormalizeArchivePath(nameC);
    if (key.size() < 4 || key.substr(key.size() - 4) != ".lua") {
        // Lua-style dotted module names ("lib.helpers" -> "lib/helpers.lua")
        if (key.find('/') == std::string::npos) std::replace(key.begin(), key.end(), '.', '/');
        key += ".lua";
    }

    // Resolve: exact path, then unique basename match (packs are inconsistent about folders).
    auto it = g_LuaScriptsInPack.find(key);
    if (it == g_LuaScriptsInPack.end()) {
        const std::string baseName = std::filesystem::path(key).filename().string();
        for (auto cur = g_LuaScriptsInPack.begin(); cur != g_LuaScriptsInPack.end(); ++cur) {
            if (std::filesystem::path(cur->first).filename().string() == baseName) { it = cur; break; }
        }
    }
    if (it == g_LuaScriptsInPack.end()) {
        PushLuaLog("Lua require: module not found in pack: " + key, LOGL_WARNING);
        return 0;
    }
    const std::string resolved = it->first;

    // package.loaded-style cache in the registry: a module runs ONCE per VM (the old code re-ran it on every
    // require(), duplicating Event:OnTick handlers, menus and markers). A lightuserdata sentinel marks
    // "currently loading" so circular requires fail cleanly instead of recursing until the C stack dies.
    static char s_LoadingSentinel;
    lua_getfield(L, LUA_REGISTRYINDEX, "NexusPathing.loaded");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "NexusPathing.loaded");
    }
    const int cacheIdx = lua_gettop(L);

    lua_getfield(L, cacheIdx, resolved.c_str());
    if (lua_touserdata(L, -1) == &s_LoadingSentinel) {
        lua_settop(L, cacheIdx - 1);
        PushLuaLog("Lua require: circular dependency on " + resolved, LOGL_WARNING);
        return 0;
    }
    if (!lua_isnil(L, -1)) {            // cache hit
        lua_remove(L, cacheIdx);        // drop cache table, keep module value
        return 1;
    }
    lua_pop(L, 1);

    lua_pushlightuserdata(L, &s_LoadingSentinel);
    lua_setfield(L, cacheIdx, resolved.c_str());

    if (LuaLoadText(L, it->second, resolved) != LUA_OK) {
        PushLuaLog("Lua require syntax error: " + LuaErrStr(L), LOGL_WARNING);
        lua_settop(L, cacheIdx);
        lua_pushnil(L);
        lua_setfield(L, cacheIdx, resolved.c_str());
        lua_settop(L, cacheIdx - 1);
        return 0;
    }
    if (LuaSafeCall(L, 0, 1) != LUA_OK) {
        PushLuaLog("Lua require runtime error: " + LuaErrStr(L), LOGL_WARNING);
        lua_settop(L, cacheIdx);
        lua_pushnil(L);
        lua_setfield(L, cacheIdx, resolved.c_str());
        lua_settop(L, cacheIdx - 1);
        return 0;
    }
    // stack: cache, result
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }   // modules returning nothing => true
    lua_pushvalue(L, -1);
    lua_setfield(L, cacheIdx, resolved.c_str());
    lua_remove(L, cacheIdx);
    return 1;
}

static int Lua_NoOpReturnSelf(lua_State* L) {
    if (lua_gettop(L) >= 1) {
        lua_pushvalue(L, 1);
        return 1;
    }
    return 0;
}

// Marker proxy methods in Lua
static int Lua_Marker_SetPos(lua_State* L) {
    lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x1001));
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (idx >= 0 && idx < static_cast<int>(g_Markers.size())) {
        if (lua_istable(L, 2)) {
            lua_getfield(L, 2, "X"); float x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
            lua_getfield(L, 2, "Y"); float y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
            lua_getfield(L, 2, "Z"); float z = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
            g_Markers[idx].Position = { x, z, y };
        } else {
            float x = static_cast<float>(luaL_optnumber(L, 2, 0.0));
            float y = static_cast<float>(luaL_optnumber(L, 3, 0.0));
            float z = static_cast<float>(luaL_optnumber(L, 4, 0.0));
            g_Markers[idx].Position = { x, z, y };
        }
    }
    return 0;
}

static int Lua_Marker_Remove(lua_State* L) {
    lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x1001));
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (idx >= 0 && idx < static_cast<int>(g_Markers.size())) {
        g_Markers[idx].IsVisible = false;
        g_Markers[idx].InGameVisibility = false;
        g_Markers[idx].MapVisibility = false;
        g_Markers[idx].MiniMapVisibility = false;
    }
    return 0;
}

static int Lua_Marker_Show(lua_State* L) {
    lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x1001));
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (idx >= 0 && idx < static_cast<int>(g_Markers.size())) {
        g_Markers[idx].IsVisible = true;
        g_Markers[idx].InGameVisibility = true;
    }
    return 0;
}

static int Lua_Marker_SetPosX(lua_State* L) {
    lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x1001));
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (idx >= 0 && idx < static_cast<int>(g_Markers.size())) {
        g_Markers[idx].Position.x = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    }
    return 0;
}

static int Lua_Marker_SetPosY(lua_State* L) {
    lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x1001));
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (idx >= 0 && idx < static_cast<int>(g_Markers.size())) {
        g_Markers[idx].Position.z = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    }
    return 0;
}

static int Lua_Marker_SetPosZ(lua_State* L) {
    lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x1001));
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (idx >= 0 && idx < static_cast<int>(g_Markers.size())) {
        g_Markers[idx].Position.y = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    }
    return 0;
}

static int Lua_Marker_SetTexture(lua_State* L) {
    lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x1001));
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (idx >= 0 && idx < static_cast<int>(g_Markers.size())) {
        std::string texPath;
        if (lua_istable(L, 2)) {
            lua_getfield(L, 2, "__path");
            if (const char* p = lua_tostring(L, -1)) texPath = p;
            lua_pop(L, 1);
        } else if (lua_isstring(L, 2) && !lua_isnumber(L, 2)) {
            texPath = lua_tostring(L, 2);
        }
        if (!texPath.empty()) {
            auto& m = g_Markers[idx];
            m.IconFileRelative = texPath;
            m.IconTextureId = EnsurePackTextureLoaded(
                m.PackName.empty() ? g_CurrentPackName : m.PackName,
                texPath,
                FetchFromCachedPackFiles,
                &m.NativeSRV
            );
        }
    }
    return 0;
}

static int Lua_Marker_GetBehavior(lua_State* L) {
    lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x1001));
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);

    lua_newtable(L);
    lua_pushinteger(L, idx);
    lua_rawsetp(L, -2, reinterpret_cast<const void*>(0x1001));

    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_rawgetp(s, 1, reinterpret_cast<const void*>(0x1001));
        int mIdx = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        if (mIdx >= 0 && mIdx < static_cast<int>(g_Markers.size()) && g_Mumble) {
            uint32_t mask = g_Markers[mIdx].MountFilterMask;
            if (mask != 0) {
                uint32_t curMount = static_cast<uint32_t>(g_Mumble->Context.MountIndex);
                bool matches = (curMount > 0) && ((mask & (1u << curMount)) != 0);
                lua_pushboolean(s, matches ? 0 : 1);
                return 1;
            }
        }
        lua_pushboolean(s, 0);
        return 1;
    });
    lua_setfield(L, -2, "IsFiltered");

    // Live metatable so setting behavior.InfoValue, behavior.CopyValue, or behavior.BounceHeight updates g_Markers[idx] immediately!
    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_rawgetp(s, 1, reinterpret_cast<const void*>(0x1001));
        int mIdx = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        const char* k = lua_tostring(s, 2);
        if (!k || mIdx < 0 || mIdx >= static_cast<int>(g_Markers.size())) {
            lua_pushnil(s);
            return 1;
        }
        const auto& m = g_Markers[mIdx];
        std::string key = ToLowerAscii(k);
        if (key == "infovalue" || key == "info") { lua_pushstring(s, m.InfoText.c_str()); return 1; }
        if (key == "copyvalue" || key == "copy") { lua_pushstring(s, m.CopyValue.c_str()); return 1; }
        if (key == "copymessage") { lua_pushstring(s, m.CopyMessage.c_str()); return 1; }
        if (key == "bounceheight") { lua_pushnumber(s, m.BounceHeight); return 1; }
        if (key == "bounceduration") { lua_pushnumber(s, m.BounceDuration); return 1; }
        lua_pushnil(s);
        return 1;
    });
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_rawgetp(s, 1, reinterpret_cast<const void*>(0x1001));
        int mIdx = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        const char* k = lua_tostring(s, 2);
        if (!k || mIdx < 0 || mIdx >= static_cast<int>(g_Markers.size())) return 0;
        auto& m = g_Markers[mIdx];
        std::string key = ToLowerAscii(k);
        if (key == "infovalue" || key == "info") {
            const char* val = lua_tostring(s, 3);
            m.InfoText = val ? val : "";
        } else if (key == "copyvalue" || key == "copy") {
            const char* val = lua_tostring(s, 3);
            m.CopyValue = val ? val : "";
        } else if (key == "copymessage") {
            const char* val = lua_tostring(s, 3);
            m.CopyMessage = val ? val : "";
        } else if (key == "bounceheight") {
            m.BounceHeight = static_cast<float>(lua_tonumber(s, 3));
            m.HasBounce = (m.BounceHeight > 0.001f);
        } else if (key == "bounceduration") {
            m.BounceDuration = static_cast<float>(lua_tonumber(s, 3));
        }
        return 0;
    });
    lua_setfield(L, -2, "__newindex");

    lua_setmetatable(L, -2);
    return 1;
}

static void PushCategoryLuaTable(lua_State* L, const std::string& typePath);

static void PushMarkerLuaTable(lua_State* L, size_t markerIndex) {
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(markerIndex));
    lua_rawsetp(L, -2, reinterpret_cast<const void*>(0x1001));

    // Metatable for live read/write of Marker properties from Lua
    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_rawgetp(s, 1, reinterpret_cast<const void*>(0x1001));
        int idx = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        const char* k = lua_tostring(s, 2);
        if (!k || idx < 0 || idx >= static_cast<int>(g_Markers.size())) {
            lua_pushnil(s);
            return 1;
        }
        const auto& m = g_Markers[idx];
        std::string key = ToLowerAscii(k);
        if (key == "guid") { PushLuaGuidObject(s, m.Guid); return 1; }
        if (key == "size" || key == "iconsize") { lua_pushnumber(s, m.IconSize); return 1; }
        if (key == "alpha") { lua_pushnumber(s, m.TintColor.w); return 1; }
        if (key == "ingamevisibility") { lua_pushboolean(s, m.InGameVisibility ? 1 : 0); return 1; }
        if (key == "mapvisibility") { lua_pushboolean(s, m.MapVisibility ? 1 : 0); return 1; }
        if (key == "minimapvisibility") { lua_pushboolean(s, m.MiniMapVisibility ? 1 : 0); return 1; }
        if (key == "mapdisplaysize") { lua_pushnumber(s, m.MapDisplaySize); return 1; }
        if (key == "triggerrange") { lua_pushnumber(s, m.TriggerRange); return 1; }
        if (key == "focused") { lua_pushboolean(s, m.IsFocused ? 1 : 0); return 1; }
        if (key == "position") { PushLuaVector3(s, m.Position.x, m.Position.z, m.Position.y); return 1; }
        if (key == "category") { PushCategoryLuaTable(s, m.TypePath); return 1; }
        if (key == "setpos") { lua_pushcfunction(s, Lua_Marker_SetPos); return 1; }
        if (key == "setposx") { lua_pushcfunction(s, Lua_Marker_SetPosX); return 1; }
        if (key == "setposy") { lua_pushcfunction(s, Lua_Marker_SetPosY); return 1; }
        if (key == "setposz") { lua_pushcfunction(s, Lua_Marker_SetPosZ); return 1; }
        if (key == "remove" || key == "hide") { lua_pushcfunction(s, Lua_Marker_Remove); return 1; }
        if (key == "show") { lua_pushcfunction(s, Lua_Marker_Show); return 1; }
        if (key == "settexture") { lua_pushcfunction(s, Lua_Marker_SetTexture); return 1; }
        if (key == "getbehavior") { lua_pushcfunction(s, Lua_Marker_GetBehavior); return 1; }
        if (key == "setrot" || key == "setrotx" || key == "setroty" || key == "setrotz" || key == "focus" || key == "unfocus" || key == "interact") {
            lua_pushcfunction(s, Lua_NoOpReturnSelf);
            return 1;
        }
        lua_pushnil(s);
        return 1;
    });
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_rawgetp(s, 1, reinterpret_cast<const void*>(0x1001));
        int idx = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        const char* k = lua_tostring(s, 2);
        if (!k || idx < 0 || idx >= static_cast<int>(g_Markers.size())) return 0;
        auto& m = g_Markers[idx];
        std::string key = ToLowerAscii(k);
        if (key == "size" || key == "iconsize") m.IconSize = static_cast<float>(lua_tonumber(s, 3));
        else if (key == "alpha") m.TintColor.w = static_cast<float>(lua_tonumber(s, 3));
        else if (key == "ingamevisibility") { m.InGameVisibility = lua_toboolean(s, 3) != 0; if (m.InGameVisibility) m.IsVisible = true; }
        else if (key == "mapvisibility") { m.MapVisibility = lua_toboolean(s, 3) != 0; if (m.MapVisibility) m.IsVisible = true; }
        else if (key == "minimapvisibility") { m.MiniMapVisibility = lua_toboolean(s, 3) != 0; if (m.MiniMapVisibility) m.IsVisible = true; }
        else if (key == "mapdisplaysize") m.MapDisplaySize = static_cast<float>(lua_tonumber(s, 3));
        else if (key == "triggerrange") m.TriggerRange = static_cast<float>(lua_tonumber(s, 3));
        else if (key == "tint") {
            if (lua_istable(s, 3)) {
                lua_getfield(s, 3, "R"); m.TintColor.x = static_cast<float>(lua_tonumber(s, -1)); lua_pop(s, 1);
                lua_getfield(s, 3, "G"); m.TintColor.y = static_cast<float>(lua_tonumber(s, -1)); lua_pop(s, 1);
                lua_getfield(s, 3, "B"); m.TintColor.z = static_cast<float>(lua_tonumber(s, -1)); lua_pop(s, 1);
            }
        } else if (key == "texture") {
            lua_pushcfunction(s, Lua_Marker_SetTexture);
            lua_pushvalue(s, 1);
            lua_pushvalue(s, 3);
            lua_call(s, 2, 0);
        }
        return 0;
    });
    lua_setfield(L, -2, "__newindex");

    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_rawgetp(s, 1, reinterpret_cast<const void*>(0x1001));
        int idx1 = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        lua_rawgetp(s, 2, reinterpret_cast<const void*>(0x1001));
        int idx2 = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        lua_pushboolean(s, idx1 == idx2);
        return 1;
    });
    lua_setfield(L, -2, "__eq");

    lua_setmetatable(L, -2);
}

// Live Trail proxy in Lua (propagates changes to InGameVisibility, Alpha, Tint, Texture, MapVisibility, MiniMapVisibility directly to g_Trails[idx]!)
static int Lua_Trail_SetTexture(lua_State* L) {
    lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x2002));
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (idx >= 0 && idx < static_cast<int>(g_Trails.size())) {
        std::string texPath;
        if (lua_istable(L, 2)) {
            lua_getfield(L, 2, "__path");
            if (const char* p = lua_tostring(L, -1)) texPath = p;
            lua_pop(L, 1);
        } else if (lua_isstring(L, 2) && !lua_isnumber(L, 2)) {
            texPath = lua_tostring(L, 2);
        }
        if (!texPath.empty()) {
            auto& t = g_Trails[idx];
            t.TextureRelative = texPath;
            t.TextureId = EnsurePackTextureLoaded(
                t.PackName.empty() ? g_CurrentPackName : t.PackName,
                texPath,
                FetchFromCachedPackFiles,
                &t.NativeSRV
            );
        }
    }
    return 0;
}

static int Lua_Trail_Remove(lua_State* L) {
    lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x2002));
    int idx = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (idx >= 0 && idx < static_cast<int>(g_Trails.size())) {
        g_Trails[idx].IsVisible = false;
        g_Trails[idx].InGameVisibility = false;
        g_Trails[idx].MapVisibility = false;
        g_Trails[idx].MiniMapVisibility = false;
    }
    return 0;
}

static void PushTrailLuaTable(lua_State* L, size_t trailIndex) {
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(trailIndex));
    lua_rawsetp(L, -2, reinterpret_cast<const void*>(0x2002));

    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_rawgetp(s, 1, reinterpret_cast<const void*>(0x2002));
        int idx = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        const char* k = lua_tostring(s, 2);
        if (!k || idx < 0 || idx >= static_cast<int>(g_Trails.size())) {
            lua_pushnil(s);
            return 1;
        }
        const auto& t = g_Trails[idx];
        std::string key = ToLowerAscii(k);
        if (key == "guid") { PushLuaGuidObject(s, t.Guid); return 1; }
        if (key == "alpha") { lua_pushnumber(s, t.TintColor.w); return 1; }
        if (key == "animationspeed" || key == "animspeed") { lua_pushnumber(s, t.AnimSpeed); return 1; }
        if (key == "trailscale") { lua_pushnumber(s, t.TrailScale); return 1; }
        if (key == "fadenear") { lua_pushnumber(s, t.FadeNear); return 1; }
        if (key == "fadefar") { lua_pushnumber(s, t.FadeFar); return 1; }
        if (key == "ingamevisibility") { lua_pushboolean(s, t.InGameVisibility ? 1 : 0); return 1; }
        if (key == "mapvisibility") { lua_pushboolean(s, t.MapVisibility ? 1 : 0); return 1; }
        if (key == "minimapvisibility") { lua_pushboolean(s, t.MiniMapVisibility ? 1 : 0); return 1; }
        if (key == "mapid") { lua_pushinteger(s, t.MapId); return 1; }
        if (key == "category") { PushCategoryLuaTable(s, t.TypePath); return 1; }
        if (key == "settexture") { lua_pushcfunction(s, Lua_Trail_SetTexture); return 1; }
        if (key == "remove") { lua_pushcfunction(s, Lua_Trail_Remove); return 1; }
        if (key == "focus" || key == "unfocus" || key == "interact" || key == "getbehavior" || key == "setpoints") {
            lua_pushcfunction(s, Lua_NoOpReturnSelf);
            return 1;
        }
        lua_pushnil(s);
        return 1;
    });
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_rawgetp(s, 1, reinterpret_cast<const void*>(0x2002));
        int idx = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        const char* k = lua_tostring(s, 2);
        if (!k || idx < 0 || idx >= static_cast<int>(g_Trails.size())) return 0;
        auto& t = g_Trails[idx];
        std::string key = ToLowerAscii(k);
        if (key == "alpha") {
            t.TintColor.w = std::clamp(static_cast<float>(lua_tonumber(s, 3)), 0.0f, 1.0f);
        } else if (key == "animationspeed" || key == "animspeed") {
            t.AnimSpeed = static_cast<float>(lua_tonumber(s, 3));
        } else if (key == "trailscale") {
            t.TrailScale = static_cast<float>(lua_tonumber(s, 3));
        } else if (key == "ingamevisibility") {
            t.InGameVisibility = lua_toboolean(s, 3) != 0;
            if (t.InGameVisibility) t.IsVisible = true;
        } else if (key == "mapvisibility") {
            t.MapVisibility = lua_toboolean(s, 3) != 0;
            if (t.MapVisibility) t.IsVisible = true;
        } else if (key == "minimapvisibility") {
            t.MiniMapVisibility = lua_toboolean(s, 3) != 0;
            if (t.MiniMapVisibility) t.IsVisible = true;
        } else if (key == "tint" || key == "trailsamplecolor") {
            if (lua_istable(s, 3)) {
                lua_getfield(s, 3, "R"); t.TintColor.x = static_cast<float>(lua_tonumber(s, -1)); lua_pop(s, 1);
                lua_getfield(s, 3, "G"); t.TintColor.y = static_cast<float>(lua_tonumber(s, -1)); lua_pop(s, 1);
                lua_getfield(s, 3, "B"); t.TintColor.z = static_cast<float>(lua_tonumber(s, -1)); lua_pop(s, 1);
            } else if (lua_isnil(s, 3)) {
                t.TintColor.x = 1.0f; t.TintColor.y = 1.0f; t.TintColor.z = 1.0f;
            }
        } else if (key == "texture") {
            lua_pushcfunction(s, Lua_Trail_SetTexture);
            lua_pushvalue(s, 1);
            lua_pushvalue(s, 3);
            lua_call(s, 2, 0);
        }
        return 0;
    });
    lua_setfield(L, -2, "__newindex");

    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_rawgetp(s, 1, reinterpret_cast<const void*>(0x2002));
        int idx1 = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        lua_rawgetp(s, 2, reinterpret_cast<const void*>(0x2002));
        int idx2 = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        lua_pushboolean(s, idx1 == idx2);
        return 1;
    });
    lua_setfield(L, -2, "__eq");

    lua_setmetatable(L, -2);
}

static void PushCategoryLuaTable(lua_State* L, const std::string& typePath) {
    std::string normType = ToLowerAscii(typePath);
    std::string shortName = normType;
    auto dotPos = normType.rfind('.');
    if (dotPos != std::string::npos) shortName = normType.substr(dotPos + 1);
    std::string dispName = shortName;
    auto catIt = g_CategoryLookup.find(normType);
    if (catIt != g_CategoryLookup.end() && catIt->second) {
        dispName = catIt->second->DisplayName;
        shortName = catIt->second->Name;
    }

    lua_newtable(L);
    lua_pushstring(L, normType.c_str());  lua_setfield(L, -2, "Namespace");
    lua_pushstring(L, shortName.c_str()); lua_setfield(L, -2, "Name");
    lua_pushstring(L, dispName.c_str());  lua_setfield(L, -2, "DisplayName");

    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_getfield(s, 1, "Namespace");
        std::string ns = ToLowerAscii(luaL_optstring(s, -1, ""));
        lua_pop(s, 1);
        lua_pushboolean(s, IsCategoryPathEnabled(ns) ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "IsVisible");

    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_getfield(s, 1, "Namespace");
        std::string ns = ToLowerAscii(luaL_optstring(s, -1, ""));
        lua_pop(s, 1);
        auto it = g_CategoryLookup.find(ns);
        if (it != g_CategoryLookup.end() && it->second) {
            SetCategoryChildrenEnabled(*it->second, true);
            SyncVisibilityWithCategories();
        }
        return 0;
    });
    lua_setfield(L, -2, "Show");

    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_getfield(s, 1, "Namespace");
        std::string ns = ToLowerAscii(luaL_optstring(s, -1, ""));
        lua_pop(s, 1);
        auto it = g_CategoryLookup.find(ns);
        if (it != g_CategoryLookup.end() && it->second) {
            SetCategoryChildrenEnabled(*it->second, false);
            SyncVisibilityWithCategories();
        }
        return 0;
    });
    lua_setfield(L, -2, "Hide");

    // Category:GetMarkers(getAll)
    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_getfield(s, 1, "Namespace");
        std::string ns = ToLowerAscii(luaL_optstring(s, -1, ""));
        lua_pop(s, 1);
        bool getAll = lua_toboolean(s, 2) != 0;
        std::string prefix = ns + ".";
        int32_t curMap = g_Mumble ? static_cast<int32_t>(g_Mumble->Context.MapID) : 0;

        lua_newtable(s);
        int outIdx = 1;
        for (size_t i = 0; i < g_Markers.size(); ++i) {
            if (curMap != 0 && g_Markers[i].MapId != 0 && g_Markers[i].MapId != curMap) continue;
            const std::string& tp = g_Markers[i].TypePath;
            bool match = (tp == ns) || (getAll && tp.rfind(prefix, 0) == 0);
            if (match) {
                PushMarkerLuaTable(s, i);
                lua_rawseti(s, -2, outIdx++);
            }
        }
        return 1;
    });
    lua_setfield(L, -2, "GetMarkers");

    // Category:GetTrails(getAll)
    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_getfield(s, 1, "Namespace");
        std::string ns = ToLowerAscii(luaL_optstring(s, -1, ""));
        lua_pop(s, 1);
        bool getAll = lua_toboolean(s, 2) != 0;
        std::string prefix = ns + ".";
        int32_t curMap = g_Mumble ? static_cast<int32_t>(g_Mumble->Context.MapID) : 0;

        lua_newtable(s);
        int outIdx = 1;
        for (size_t i = 0; i < g_Trails.size(); ++i) {
            if (curMap != 0 && g_Trails[i].MapId != 0 && g_Trails[i].MapId != curMap) continue;
            const std::string& tp = g_Trails[i].TypePath;
            bool match = (tp == ns) || (getAll && tp.rfind(prefix, 0) == 0);
            if (match) {
                PushTrailLuaTable(s, i);
                lua_rawseti(s, -2, outIdx++);
            }
        }
        return 1;
    });
    lua_setfield(L, -2, "GetTrails");

    lua_pushcfunction(L, [](lua_State* s) -> int {
        lua_getfield(s, 1, "Namespace");
        std::string parentNs = ToLowerAscii(luaL_optstring(s, -1, ""));
        lua_pop(s, 1);
        std::string childNs = ToLowerAscii(luaL_optstring(s, 2, ""));
        std::string full = parentNs.empty() ? childNs : (parentNs + "." + childNs);
        PushCategoryLuaTable(s, full);
        return 1;
    });
    lua_setfield(L, -2, "GetOrAddCategoryFromNamespace");
}

static std::string ExtractGuidArg(lua_State* L, int argIdx) {
    if (lua_istable(L, argIdx)) {
        lua_getfield(L, argIdx, "__guid");
        std::string g = luaL_optstring(L, -1, "");
        lua_pop(L, 1);
        return g;
    }
    return luaL_optstring(L, argIdx, "");
}

static int Lua_World_MarkerByGuid(lua_State* L) {
    int argIdx = lua_istable(L, 1) ? 2 : 1;
    std::string guid = ExtractGuidArg(L, argIdx);
    int32_t curMap = g_Mumble ? static_cast<int32_t>(g_Mumble->Context.MapID) : 0;
    for (size_t i = 0; i < g_Markers.size(); ++i) {
        if (curMap != 0 && g_Markers[i].MapId != 0 && g_Markers[i].MapId != curMap) continue;
        if (g_Markers[i].Guid == guid) {
            PushMarkerLuaTable(L, i);
            return 1;
        }
    }
    for (size_t i = 0; i < g_Markers.size(); ++i) {
        if (g_Markers[i].Guid == guid) {
            PushMarkerLuaTable(L, i);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int Lua_World_TrailByGuid(lua_State* L) {
    int argIdx = lua_istable(L, 1) ? 2 : 1;
    std::string guid = ExtractGuidArg(L, argIdx);
    int32_t curMap = g_Mumble ? static_cast<int32_t>(g_Mumble->Context.MapID) : 0;
    for (size_t i = 0; i < g_Trails.size(); ++i) {
        if (curMap != 0 && g_Trails[i].MapId != 0 && g_Trails[i].MapId != curMap) continue;
        if (g_Trails[i].Guid == guid) {
            PushTrailLuaTable(L, i);
            return 1;
        }
    }
    for (size_t i = 0; i < g_Trails.size(); ++i) {
        if (g_Trails[i].Guid == guid) {
            PushTrailLuaTable(L, i);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int Lua_World_CategoryByType(lua_State* L) {
    int argIdx = lua_istable(L, 1) ? 2 : 1;
    std::string typePath = ToLowerAscii(luaL_optstring(L, argIdx, ""));
    PushCategoryLuaTable(L, typePath);
    return 1;
}

static int Lua_Pack_Require(lua_State* L) {
    if (lua_istable(L, 1)) {
        lua_remove(L, 1);
    }
    return Lua_Require(L);
}

static uint64_t g_LuaMarkerSeq = 0;   // reset together with the VM so re-running pack.lua yields the same GUIDs

static int Lua_Pack_CreateMarker(lua_State* L) {
    int attrIdx = lua_istable(L, 2) ? 2 : (lua_istable(L, 1) ? 1 : 0);
    RuntimeMarker m{};
    m.PackName = g_CurrentPackName;
    m.FromLua = true;
    m.Guid = "lua_marker_" + std::to_string(g_LuaMarkerSeq++);
    if (g_Mumble) {
        m.MapId = static_cast<int32_t>(g_Mumble->Context.MapID);
        m.Position = { g_Mumble->AvatarPosition.X, g_Mumble->AvatarPosition.Y, g_Mumble->AvatarPosition.Z };
    }
    if (attrIdx != 0) {
        lua_pushnil(L);
        while (lua_next(L, attrIdx) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                std::string k = ToLowerAscii(lua_tostring(L, -2));
                if (k == "xpos") m.Position.x = static_cast<float>(lua_tonumber(L, -1));
                else if (k == "ypos") m.Position.y = static_cast<float>(lua_tonumber(L, -1));
                else if (k == "zpos") m.Position.z = static_cast<float>(lua_tonumber(L, -1));
                else if (k == "iconsize" || k == "size") m.IconSize = static_cast<float>(lua_tonumber(L, -1));
                else if (k == "alpha") m.TintColor.w = static_cast<float>(lua_tonumber(L, -1));
                else if (k == "mapdisplaysize") m.MapDisplaySize = static_cast<float>(lua_tonumber(L, -1));
                else if (k == "triggerrange") m.TriggerRange = static_cast<float>(lua_tonumber(L, -1));
                else if (k == "inforange") m.InfoRange = static_cast<float>(lua_tonumber(L, -1));
                else if (k == "autotrigger") m.AutoTrigger = (lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : (lua_tointeger(L, -1) != 0));
                else if (k == "ingamevisibility") m.InGameVisibility = (lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : (lua_tointeger(L, -1) != 0));
                else if (k == "mapvisibility") m.MapVisibility = (lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : (lua_tointeger(L, -1) != 0));
                else if (k == "minimapvisibility") m.MiniMapVisibility = (lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : (lua_tointeger(L, -1) != 0));
                else if (k == "info" || k == "tip-name") { if (const char* s = lua_tostring(L, -1)) m.InfoText = s; }
                else if (k == "copy") { if (const char* s = lua_tostring(L, -1)) m.CopyValue = s; }
                else if (k == "copy-message") { if (const char* s = lua_tostring(L, -1)) m.CopyMessage = s; }
                else if (k == "script-trigger") { if (const char* s = lua_tostring(L, -1)) m.ScriptTrigger = s; }
                else if (k == "script-focus") { if (const char* s = lua_tostring(L, -1)) m.ScriptFocus = s; }
                else if (k == "script-tick") { if (const char* s = lua_tostring(L, -1)) m.ScriptTick = s; }
                else if (k == "script-once") { if (const char* s = lua_tostring(L, -1)) m.ScriptOnce = s; }
                else if (k == "guid") { m.Guid = ExtractGuidArg(L, -1); }
                else if (k == "type") { if (const char* s = lua_tostring(L, -1)) m.TypePath = ToLowerAscii(s); }
                else if (k == "category") {
                    if (lua_istable(L, -1)) {
                        lua_getfield(L, -1, "Namespace");
                        if (const char* ns = lua_tostring(L, -1)) m.TypePath = ToLowerAscii(ns);
                        lua_pop(L, 1);
                    } else if (const char* s = lua_tostring(L, -1)) {
                        m.TypePath = ToLowerAscii(s);
                    }
                }
                else if (k == "iconfile") {
                    if (const char* s = lua_tostring(L, -1)) {
                        m.IconFileRelative = s;
                        m.IconTextureId = EnsurePackTextureLoaded(
                            m.PackName, m.IconFileRelative, FetchFromCachedPackFiles, &m.NativeSRV
                        );
                    }
                }
            }
            lua_pop(L, 1);
        }
    }
    g_Markers.push_back(std::move(m));
    PushMarkerLuaTable(L, g_Markers.size() - 1);
    return 1;
}

struct LuaMenuNode {
    std::string Name;
    std::string Tooltip;
    bool        CanCheck     = false;
    int         LuaTableRef  = LUA_NOREF;
    int         CallbackRef  = LUA_NOREF;
    std::vector<std::unique_ptr<LuaMenuNode>> Children;
};

static std::vector<std::unique_ptr<LuaMenuNode>> g_RootLuaMenus;

static void ClearLuaMenus(lua_State* L, std::vector<std::unique_ptr<LuaMenuNode>>& list) {
    if (L) {
        for (auto& node : list) {
            if (!node) continue;
            ClearLuaMenus(L, node->Children);
            if (node->LuaTableRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, node->LuaTableRef);
            if (node->CallbackRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, node->CallbackRef);
        }
    }
    list.clear();
}

static int Lua_Menu_Add(lua_State* L) {
    // Menu:Add(name, onClickFunc, canCheck, checked, tooltip)
    LuaMenuNode* parentNode = nullptr;
    if (lua_istable(L, 1)) {
        lua_rawgetp(L, 1, reinterpret_cast<const void*>(0x3003));
        parentNode = static_cast<LuaMenuNode*>(lua_touserdata(L, -1));
        lua_pop(L, 1);
    }

    const char* name = luaL_optstring(L, 2, "Menu");
    bool canCheck = lua_toboolean(L, 4) != 0;
    bool checked  = lua_toboolean(L, 5) != 0;
    const char* tooltip = luaL_optstring(L, 6, "");

    auto node = std::make_unique<LuaMenuNode>();
    node->Name     = name ? name : "Menu";
    node->Tooltip  = tooltip ? tooltip : "";
    node->CanCheck = canCheck;
    LuaMenuNode* rawNode = node.get();

    if (lua_isfunction(L, 3)) {
        lua_pushvalue(L, 3);
        node->CallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    lua_newtable(L);
    lua_pushlightuserdata(L, rawNode);
    lua_rawsetp(L, -2, reinterpret_cast<const void*>(0x3003));
    lua_pushstring(L, node->Name.c_str());    lua_setfield(L, -2, "Name");
    lua_pushboolean(L, canCheck ? 1 : 0);     lua_setfield(L, -2, "CanCheck");
    lua_pushboolean(L, checked ? 1 : 0);      lua_setfield(L, -2, "Checked");
    lua_pushstring(L, node->Tooltip.c_str()); lua_setfield(L, -2, "Tooltip");
    lua_pushcfunction(L, Lua_Menu_Add);       lua_setfield(L, -2, "Add");
    lua_pushcfunction(L, Lua_NoOpReturnSelf); lua_setfield(L, -2, "Remove");

    // Store reference to this Lua menu table so ImGui can read/update menu.Checked and pass `menu` to onClickFunc(menu)!
    lua_pushvalue(L, -1);
    node->LuaTableRef = luaL_ref(L, LUA_REGISTRYINDEX);

    if (parentNode) {
        parentNode->Children.push_back(std::move(node));
    } else {
        g_RootLuaMenus.push_back(std::move(node));
    }

    return 1;
}

static std::unordered_map<std::string, std::string> g_LuaStorageKV;

static int Lua_Storage_ReadValue(lua_State* L) {
    int startIdx = lua_istable(L, 1) ? 2 : 1;
    // Fetch ALL C strings first: luaL_optstring can longjmp, which would skip destructors of live std::strings.
    const char* nsC  = luaL_optstring(L, startIdx, "");
    const char* keyC = luaL_optstring(L, startIdx + 1, "");
    auto it = g_LuaStorageKV.find(std::string(nsC) + ":" + keyC);
    if (it != g_LuaStorageKV.end()) {
        lua_pushlstring(L, it->second.data(), it->second.size());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int Lua_Storage_UpsertValue(lua_State* L) {
    int startIdx = lua_istable(L, 1) ? 2 : 1;
    const char* nsC  = luaL_optstring(L, startIdx, "");
    const char* keyC = luaL_optstring(L, startIdx + 1, "");
    size_t valLen = 0;
    const char* valC = luaL_optlstring(L, startIdx + 2, "", &valLen);

    constexpr size_t kMaxValueBytes = 64 * 1024;
    constexpr size_t kMaxEntries    = 2048;
    std::string fullKey = std::string(nsC) + ":" + keyC;
    if (valLen > kMaxValueBytes || (g_LuaStorageKV.size() >= kMaxEntries && !g_LuaStorageKV.count(fullKey))) {
        PushLuaLogThrottled("Storage:UpsertValue rejected (value > 64 KB or more than 2048 keys)");
        lua_pushboolean(L, 0);
        return 1;
    }
    g_LuaStorageKV[fullKey].assign(valC, valLen);
    g_PendingSaveConfigOnMainThread.store(true);   // coalesced; was a synchronous disk write on every call
    lua_pushboolean(L, 1);
    return 1;
}

static int Lua_User_SetClipboard(lua_State* L) {
    int startIdx = lua_istable(L, 1) ? 2 : 1;
    const char* val = luaL_optstring(L, startIdx, "");
    if (val && *val) {
        CopyStringToClipboard(val);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static void InitLuaVM() {
    if (g_Lua) {
        ClearLuaMenus(g_Lua, g_RootLuaMenus);
        lua_close(g_Lua);
        g_Lua = nullptr;
    } else {
        g_RootLuaMenus.clear();
    }
    g_LuaTickRefs.clear();

    g_LuaMarkerSeq   = 0;
    g_LuaHookCounter = 0;
    g_LuaCallDepth   = 0;

    g_Lua = luaL_newstate();
    if (!g_Lua) {
        PushLuaLog("luaL_newstate failed (out of memory)", LOGL_CRITICAL);
        return;
    }
#ifndef NP_LUA_UNSANDBOXED
    // Packs are untrusted downloads: do NOT expose io / os / debug / package / dofile / loadfile.
    {
        static const luaL_Reg kSafeLibs[] = {
            { LUA_GNAME,      luaopen_base },
            { LUA_COLIBNAME,  luaopen_coroutine },
            { LUA_TABLIBNAME, luaopen_table },
            { LUA_STRLIBNAME, luaopen_string },
            { LUA_MATHLIBNAME,luaopen_math },
            { LUA_UTF8LIBNAME,luaopen_utf8 },
            { LUA_OSLIBNAME,  luaopen_os },      // trimmed below to time/clock/date/difftime
            { nullptr, nullptr }
        };
        for (const luaL_Reg* lib = kSafeLibs; lib->func; ++lib) {
            luaL_requiref(g_Lua, lib->name, lib->func, 1);
            lua_pop(g_Lua, 1);
        }
    }
    if (luaL_dostring(g_Lua, R"(
        dofile, loadfile = nil, nil
        for _, k in ipairs({"execute","exit","remove","rename","tmpname","getenv","setlocale"}) do os[k] = nil end
        -- force text-only chunks (no precompiled bytecode) while keeping the standard signature
        local _load = load
        load = function(chunk, name, mode, ...) return _load(chunk, name, "t", ...) end
        -- some Blish scripts probe package.loaded; give them a harmless stub
        package = { loaded = {}, path = "", cpath = "" }
    )") != LUA_OK) {
        PushLuaLog("Lua sandbox prelude failed: " + LuaErrStr(g_Lua), LOGL_WARNING);
        lua_pop(g_Lua, 1);
    }
#else
    luaL_openlibs(g_Lua);
#endif
    lua_gc(g_Lua, LUA_GCGEN, 0, 0);   // generational GC (stable in 5.4.7): cheap minor collections for per-frame temp tables
    lua_sethook(g_Lua, LuaBudgetHook, LUA_MASKCOUNT, LUA_HOOK_STEP);

    // Global PathingVersion & Pathing object (required by xrandox_TehsTrails.taco & Blish HUD scripts!)
    lua_pushstring(g_Lua, "1.13.2");
    lua_setglobal(g_Lua, "PathingVersion");

    lua_newtable(g_Lua);
    lua_pushstring(g_Lua, "1.13.2"); lua_setfield(g_Lua, -2, "Version");
    lua_pushcfunction(g_Lua, [](lua_State* s) -> int {
        lua_pushboolean(s, 1);
        return 1;
    });
    lua_setfield(g_Lua, -2, "IsVersionAtLeast");
    lua_setglobal(g_Lua, "Pathing");

    // Add real recursive table.ToLson, table.FromLson, and string:ToBase64() so both `marker.Guid .. "text"`
    // and `trail.Guid:ToBase64()` work natively in Lua 5.4!
    if (luaL_dostring(g_Lua, R"(
        if string then
            string.ToBase64 = function(self) return self end
        end
        if table then
            local function serializeLson(val)
                local tv = type(val)
                if tv == "string" then
                    return string.format("%q", val)
                elseif tv == "number" or tv == "boolean" then
                    return tostring(val)
                elseif tv == "table" then
                    local parts = {}
                    for k, v in pairs(val) do
                        local ks = (type(k) == "string") and ("[" .. string.format("%q", k) .. "]") or ("[" .. tostring(k) .. "]")
                        table.insert(parts, ks .. "=" .. serializeLson(v))
                    end
                    return "{" .. table.concat(parts, ",") .. "}"
                end
                return "nil"
            end
            table.ToLson = function(t)
                return serializeLson(t)
            end
            table.FromLson = function(s)
                if type(s) ~= "string" or s == "" then return {} end
                local fn = load("return " .. s)
                if fn then
                    local ok, res = pcall(fn)
                    if ok and type(res) == "table" then return res end
                end
                return {}
            end
        end
    )") != LUA_OK) {
        PushLuaLog("Lua prelude failed: " + LuaErrStr(g_Lua), LOGL_WARNING);
        lua_pop(g_Lua, 1);
    }

    // Override print and require
    lua_pushcfunction(g_Lua, Lua_DebugPrint);
    lua_setglobal(g_Lua, "print");

    lua_pushcfunction(g_Lua, Lua_Require);
    lua_setglobal(g_Lua, "require");

    // Global Debug table
    lua_newtable(g_Lua);
    lua_pushcfunction(g_Lua, Lua_DebugPrint);     lua_setfield(g_Lua, -2, "Print");
    lua_pushcfunction(g_Lua, Lua_DebugPrint);     lua_setfield(g_Lua, -2, "Info");
    lua_pushcfunction(g_Lua, Lua_DebugPrint);     lua_setfield(g_Lua, -2, "Warn");
    lua_pushcfunction(g_Lua, Lua_DebugPrint);     lua_setfield(g_Lua, -2, "Error");
    lua_pushcfunction(g_Lua, Lua_DebugPrint);     lua_setfield(g_Lua, -2, "ShowMessage");
    lua_pushcfunction(g_Lua, Lua_NoOpReturnSelf); lua_setfield(g_Lua, -2, "Watch");
    lua_pushcfunction(g_Lua, Lua_NoOpReturnSelf); lua_setfield(g_Lua, -2, "ClearWatch");
    lua_setglobal(g_Lua, "Debug");

    // Global Event table
    lua_newtable(g_Lua);
    lua_pushcfunction(g_Lua, Lua_EventOnTick);    lua_setfield(g_Lua, -2, "OnTick");
    lua_pushcfunction(g_Lua, Lua_NoOpReturnSelf); lua_setfield(g_Lua, -2, "OnExecuteAfter");
    lua_setglobal(g_Lua, "Event");

    // Global I (Instance) table
    lua_newtable(g_Lua);
    lua_pushcfunction(g_Lua, Lua_IVector3);            lua_setfield(g_Lua, -2, "Vector3");
    lua_pushcfunction(g_Lua, Lua_IColor);              lua_setfield(g_Lua, -2, "Color");
    lua_pushcfunction(g_Lua, Lua_IGuid);               lua_setfield(g_Lua, -2, "Guid");
    lua_pushcfunction(g_Lua, Lua_ITexture);            lua_setfield(g_Lua, -2, "Texture");
    lua_pushcfunction(g_Lua, Lua_Pack_CreateMarker);   lua_setfield(g_Lua, -2, "Marker");
    lua_setglobal(g_Lua, "I");

    // Global Pack & User tables (Blish HUD Pathing Scripting Engine compatibility)
    lua_newtable(g_Lua);
    lua_pushcfunction(g_Lua, Lua_Pack_Require);      lua_setfield(g_Lua, -2, "Require");
    lua_pushcfunction(g_Lua, Lua_Pack_CreateMarker); lua_setfield(g_Lua, -2, "CreateMarker");
    lua_pushcfunction(g_Lua, Lua_NoOpReturnSelf);    lua_setfield(g_Lua, -2, "CreateTrail");
    lua_setglobal(g_Lua, "Pack");

    lua_newtable(g_Lua);
    lua_pushcfunction(g_Lua, Lua_User_SetClipboard); lua_setfield(g_Lua, -2, "SetClipboard");
    lua_pushcfunction(g_Lua, Lua_NoOpReturnSelf);    lua_setfield(g_Lua, -2, "ShowInfo");
    lua_pushcfunction(g_Lua, Lua_NoOpReturnSelf);    lua_setfield(g_Lua, -2, "HideInfo");
    lua_setglobal(g_Lua, "User");

    lua_newtable(g_Lua);
    lua_pushcfunction(g_Lua, Lua_Menu_Add);          lua_setfield(g_Lua, -2, "Add");
    lua_pushcfunction(g_Lua, Lua_NoOpReturnSelf);    lua_setfield(g_Lua, -2, "Remove");
    lua_setglobal(g_Lua, "Menu");

    lua_newtable(g_Lua);
    lua_pushcfunction(g_Lua, Lua_Storage_ReadValue);   lua_setfield(g_Lua, -2, "ReadValue");
    lua_pushcfunction(g_Lua, Lua_Storage_UpsertValue); lua_setfield(g_Lua, -2, "UpsertValue");
    lua_pushcfunction(g_Lua, Lua_NoOpReturnSelf);      lua_setfield(g_Lua, -2, "DeleteValue");
    lua_setglobal(g_Lua, "Storage");

    // Global World & Packs tables
    lua_newtable(g_Lua);
    lua_pushcfunction(g_Lua, Lua_World_MarkerByGuid);   lua_setfield(g_Lua, -2, "MarkerByGuid");
    lua_pushcfunction(g_Lua, Lua_World_MarkerByGuid);   lua_setfield(g_Lua, -2, "PathableByGuid");
    lua_pushcfunction(g_Lua, Lua_World_TrailByGuid);    lua_setfield(g_Lua, -2, "TrailByGuid");
    lua_pushcfunction(g_Lua, Lua_World_CategoryByType); lua_setfield(g_Lua, -2, "CategoryByType");
    lua_pushvalue(g_Lua, -1);
    lua_setglobal(g_Lua, "Packs");
    lua_setglobal(g_Lua, "World");

    // Global Mumble table initialized with default values even before first frame!
    int32_t initMapId = g_Mumble ? static_cast<int32_t>(g_Mumble->Context.MapID) : 0;
    const char* initCharName = (g_MumbleIdentity && g_MumbleIdentity->Name[0]) ? g_MumbleIdentity->Name : "Player";

    lua_newtable(g_Lua);
    lua_pushboolean(g_Lua, 1); lua_setfield(g_Lua, -2, "IsAvailable");

    lua_newtable(g_Lua);
    lua_pushstring(g_Lua, initCharName); lua_setfield(g_Lua, -2, "Name");
    lua_pushinteger(g_Lua, g_Mumble ? static_cast<int>(g_Mumble->Context.MountIndex) : 0); lua_setfield(g_Lua, -2, "CurrentMount");
    lua_pushinteger(g_Lua, g_MumbleIdentity ? static_cast<int>(g_MumbleIdentity->Race) : 0); lua_setfield(g_Lua, -2, "Race");
    lua_pushinteger(g_Lua, g_MumbleIdentity ? static_cast<int>(g_MumbleIdentity->Specialization) : 0); lua_setfield(g_Lua, -2, "Specialization");
    lua_pushboolean(g_Lua, g_Mumble ? (g_Mumble->Context.IsInCombat != 0) : 0); lua_setfield(g_Lua, -2, "IsInCombat");
    lua_pushboolean(g_Lua, g_MumbleIdentity ? (g_MumbleIdentity->IsCommander ? 1 : 0) : 0); lua_setfield(g_Lua, -2, "IsCommander");
    PushLuaVector3(g_Lua,
                   g_Mumble ? g_Mumble->AvatarPosition.X : 0.f,
                   g_Mumble ? g_Mumble->AvatarPosition.Z : 0.f,
                   g_Mumble ? g_Mumble->AvatarPosition.Y : 0.f);
    lua_setfield(g_Lua, -2, "Position");
    PushLuaVector3(g_Lua,
                   g_Mumble ? g_Mumble->AvatarFront.X : 0.f,
                   g_Mumble ? g_Mumble->AvatarFront.Z : 1.f,
                   g_Mumble ? g_Mumble->AvatarFront.Y : 0.f);
    lua_setfield(g_Lua, -2, "Forward");
    lua_setfield(g_Lua, -2, "PlayerCharacter");

    lua_newtable(g_Lua);
    PushLuaVector3(g_Lua,
                   g_Mumble ? g_Mumble->CameraPosition.X : 0.f,
                   g_Mumble ? g_Mumble->CameraPosition.Z : 0.f,
                   g_Mumble ? g_Mumble->CameraPosition.Y : 0.f);
    lua_setfield(g_Lua, -2, "Position");
    PushLuaVector3(g_Lua,
                   g_Mumble ? g_Mumble->CameraFront.X : 0.f,
                   g_Mumble ? g_Mumble->CameraFront.Z : 1.f,
                   g_Mumble ? g_Mumble->CameraFront.Y : 0.f);
    lua_setfield(g_Lua, -2, "Forward");
    lua_pushnumber(g_Lua, (g_MumbleIdentity && g_MumbleIdentity->FOV > 0.f) ? g_MumbleIdentity->FOV : 0.872665f);
    lua_setfield(g_Lua, -2, "FieldOfView");
    lua_setfield(g_Lua, -2, "PlayerCamera");

    lua_newtable(g_Lua);
    lua_pushinteger(g_Lua, initMapId); lua_setfield(g_Lua, -2, "Id");
    lua_pushinteger(g_Lua, g_Mumble ? static_cast<int>(g_Mumble->Context.MapType) : 5); lua_setfield(g_Lua, -2, "Type");
    lua_pushboolean(g_Lua, 0); lua_setfield(g_Lua, -2, "IsCompetitiveMode");
    lua_setfield(g_Lua, -2, "CurrentMap");

    lua_newtable(g_Lua);
    lua_pushnumber(g_Lua, g_Mumble ? std::max(0.1f, g_Mumble->Context.Compass.Scale) : 1.0f); lua_setfield(g_Lua, -2, "MapScale");
    lua_pushnumber(g_Lua, g_Mumble ? g_Mumble->Context.Compass.Rotation : 0.0f); lua_setfield(g_Lua, -2, "CompassRotation");
    lua_pushboolean(g_Lua, g_Mumble ? (g_Mumble->Context.IsMapOpen != 0) : 0); lua_setfield(g_Lua, -2, "IsMapOpen");
    lua_pushboolean(g_Lua, g_Mumble ? (g_Mumble->Context.IsCompassTopRight != 0) : 0); lua_setfield(g_Lua, -2, "IsCompassTopRight");
    lua_pushboolean(g_Lua, g_Mumble ? (g_Mumble->Context.IsCompassRotating != 0) : 0); lua_setfield(g_Lua, -2, "IsCompassRotationEnabled");
    lua_setfield(g_Lua, -2, "UI");

    lua_setglobal(g_Lua, "Mumble");
}

// Helper to parse Blish HUD Pathing script expression `FuncName(arg1, arg2, ...)` and invoke it in Lua
// Blish HUD passes (marker, [extraContextArg], parsedArgs...) where unquoted arguments like GUIDs or numbers are pushed as numbers or strings!
static void InvokeMarkerLuaScript(
    size_t markerIndex,
    const std::string& scriptExpr,
    int extraMode, // 0 = none (script-once), 1 = bool extraBool (script-focus / script-trigger), 2 = gameTime table (script-tick)
    bool extraBool = false,
    double totalSeconds = 0.0,
    double deltaSeconds = 0.016)
{
    if (!g_Lua || scriptExpr.empty()) return;

    auto openParen = scriptExpr.find('(');
    std::string fnName = (openParen != std::string::npos) ? scriptExpr.substr(0, openParen) : scriptExpr;
    while (!fnName.empty() && std::isspace(static_cast<unsigned char>(fnName.front()))) fnName.erase(0, 1);
    while (!fnName.empty() && std::isspace(static_cast<unsigned char>(fnName.back()))) fnName.pop_back();
    if (fnName.empty()) return;

    lua_getglobal(g_Lua, fnName.c_str());
    if (!lua_isfunction(g_Lua, -1)) {
        lua_pop(g_Lua, 1);
        return;
    }

    int nargs = 1;
    PushMarkerLuaTable(g_Lua, markerIndex);

    if (extraMode == 1) {
        lua_pushboolean(g_Lua, extraBool ? 1 : 0);
        nargs++;
    } else if (extraMode == 2) {
        lua_newtable(g_Lua);
        lua_newtable(g_Lua);
        lua_pushnumber(g_Lua, totalSeconds); lua_setfield(g_Lua, -2, "TotalSeconds");
        lua_pushnumber(g_Lua, totalSeconds * 1000.0); lua_setfield(g_Lua, -2, "TotalMilliseconds");
        lua_setfield(g_Lua, -2, "TotalGameTime");

        lua_newtable(g_Lua);
        lua_pushnumber(g_Lua, deltaSeconds); lua_setfield(g_Lua, -2, "TotalSeconds");
        lua_pushnumber(g_Lua, deltaSeconds * 1000.0); lua_setfield(g_Lua, -2, "TotalMilliseconds");
        lua_setfield(g_Lua, -2, "ElapsedGameTime");
        nargs++;
    }

    if (openParen != std::string::npos) {
        auto closeParen = scriptExpr.rfind(')');
        std::string argsPart = (closeParen != std::string::npos && closeParen > openParen)
            ? scriptExpr.substr(openParen + 1, closeParen - openParen - 1)
            : scriptExpr.substr(openParen + 1);

        size_t pos = 0;
        while (pos < argsPart.size()) {
            auto comma = argsPart.find(',', pos);
            std::string tok = (comma != std::string::npos)
                ? argsPart.substr(pos, comma - pos)
                : argsPart.substr(pos);
            pos = (comma != std::string::npos) ? (comma + 1) : argsPart.size();

            while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.front()))) tok.erase(0, 1);
            while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.back()))) tok.pop_back();
            if (tok.empty()) continue;

            if (tok.size() >= 2 && ((tok.front() == '"' && tok.back() == '"') || (tok.front() == '\'' && tok.back() == '\''))) {
                tok = tok.substr(1, tok.size() - 2);
                lua_pushstring(g_Lua, tok.c_str());
            } else if (tok == "true") {
                lua_pushboolean(g_Lua, 1);
            } else if (tok == "false") {
                lua_pushboolean(g_Lua, 0);
            } else if (tok == "nil") {
                lua_pushnil(g_Lua);
            } else {
                char* endPtr = nullptr;
                double num = std::strtod(tok.c_str(), &endPtr);
                if (endPtr != tok.c_str() && *endPtr == '\0') {
                    lua_pushnumber(g_Lua, num);
                } else {
                    lua_pushstring(g_Lua, tok.c_str());
                }
            }
            nargs++;
        }
    }

    if (LuaSafeCall(g_Lua, nargs, 0) != LUA_OK) {
        PushLuaLogThrottled("Marker script error (" + fnName + "): " + LuaErrStr(g_Lua));
        lua_pop(g_Lua, 1);
    }
}

static void TriggerMarkerInteraction(size_t markerIndex, bool isAutoTriggered) {
    if (markerIndex >= g_Markers.size()) return;

    // Take safe copies of fields before invoking Lua in case Lua creates new markers and reallocates g_Markers!
    std::string guid        = g_Markers[markerIndex].Guid;
    std::string copyVal     = g_Markers[markerIndex].CopyValue;
    std::string copyMsg     = g_Markers[markerIndex].CopyMessage;
    std::string showHideCat = g_Markers[markerIndex].ShowHideCategory;
    std::string scriptTrig  = g_Markers[markerIndex].ScriptTrigger;
    int32_t     behavior    = g_Markers[markerIndex].Behavior;

    auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    g_TriggeredTimestamps[guid] = nowSec;

    if (!copyVal.empty()) {
        CopyStringToClipboard(copyVal);
        if (g_API && g_API->GUI_SendAlert) {
            std::string alertMsg = copyMsg.empty() ? ("Copied: " + copyVal) : copyMsg;
            auto fmtPos = alertMsg.find("{0}");
            if (fmtPos != std::string::npos) {
                alertMsg.replace(fmtPos, 3, copyVal);
            }
            g_API->GUI_SendAlert(alertMsg.c_str());
        }
    }

    if (!showHideCat.empty()) {
        std::string normCat = ToLowerAscii(showHideCat);
        auto it = g_CategoryLookup.find(normCat);
        if (it != g_CategoryLookup.end() && it->second) {
            SetCategoryChildrenEnabled(*it->second, !it->second->Enabled);
            SyncVisibilityWithCategories();
            SaveConfigToDisk();
        }
    }

    if (!scriptTrig.empty() && g_Lua) {
        InvokeMarkerLuaScript(markerIndex, scriptTrig, 1, isAutoTriggered);
    }

    if (behavior > 0 && markerIndex < g_Markers.size()) {
        g_Markers[markerIndex].IsVisible = false;
    }
}

static void UpdateLuaMumbleGlobals(double totalSeconds, double deltaSeconds) {
    if (!g_Lua || !g_Mumble) return;

    const char* charName = (g_MumbleIdentity && g_MumbleIdentity->Name[0]) ? g_MumbleIdentity->Name : "Player";

    lua_getglobal(g_Lua, "Mumble");
    if (lua_istable(g_Lua, -1)) {
        lua_getfield(g_Lua, -1, "PlayerCharacter");
        if (lua_istable(g_Lua, -1)) {
            lua_pushstring(g_Lua, charName);
            lua_setfield(g_Lua, -2, "Name");

            lua_pushinteger(g_Lua, static_cast<int>(g_Mumble->Context.MountIndex));
            lua_setfield(g_Lua, -2, "CurrentMount");

            // Use PushLuaVector3 so vector math (+, -, *, /, :Length()) in follower.lua & tutorial.lua always works!
            PushLuaVector3(g_Lua,
                           g_Mumble->AvatarPosition.X,
                           g_Mumble->AvatarPosition.Z,
                           g_Mumble->AvatarPosition.Y);
            lua_setfield(g_Lua, -2, "Position");

            PushLuaVector3(g_Lua,
                           g_Mumble->AvatarFront.X,
                           g_Mumble->AvatarFront.Z,
                           g_Mumble->AvatarFront.Y);
            lua_setfield(g_Lua, -2, "Forward");
        }
        lua_pop(g_Lua, 1);

        lua_getfield(g_Lua, -1, "CurrentMap");
        if (lua_istable(g_Lua, -1)) {
            lua_pushinteger(g_Lua, g_Mumble->Context.MapID);
            lua_setfield(g_Lua, -2, "Id");
        }
        lua_pop(g_Lua, 1);

        lua_getfield(g_Lua, -1, "UI");
        if (lua_istable(g_Lua, -1)) {
            lua_pushnumber(g_Lua, std::max(0.1f, g_Mumble->Context.Compass.Scale));
            lua_setfield(g_Lua, -2, "MapScale");
            lua_pushboolean(g_Lua, g_Mumble->Context.IsMapOpen != 0);
            lua_setfield(g_Lua, -2, "IsMapOpen");
        }
        lua_pop(g_Lua, 1);
    }
    lua_pop(g_Lua, 1);

    // Execute registered Event:OnTick(gameTime) listeners with Blish HUD GameTime structure.
    // Index loop + by-value ref: a handler may call Event:OnTick(...) and push_back into g_LuaTickRefs,
    // which invalidated the iterators of the previous range-for (undefined behaviour).
    if (!g_LuaTickRefs.empty()) {
        lua_newtable(g_Lua);
        lua_newtable(g_Lua);
        lua_pushnumber(g_Lua, totalSeconds); lua_setfield(g_Lua, -2, "TotalSeconds");
        lua_pushnumber(g_Lua, totalSeconds * 1000.0); lua_setfield(g_Lua, -2, "TotalMilliseconds");
        lua_setfield(g_Lua, -2, "TotalGameTime");
        lua_newtable(g_Lua);
        lua_pushnumber(g_Lua, deltaSeconds); lua_setfield(g_Lua, -2, "TotalSeconds");
        lua_pushnumber(g_Lua, deltaSeconds * 1000.0); lua_setfield(g_Lua, -2, "TotalMilliseconds");
        lua_setfield(g_Lua, -2, "ElapsedGameTime");
        const int gameTimeIdx = lua_gettop(g_Lua);

        for (size_t i = 0; i < g_LuaTickRefs.size(); ++i) {
            const int ref = g_LuaTickRefs[i];
            lua_rawgeti(g_Lua, LUA_REGISTRYINDEX, ref);
            if (lua_isfunction(g_Lua, -1)) {
                lua_pushvalue(g_Lua, gameTimeIdx);
                if (LuaSafeCall(g_Lua, 1, 0) != LUA_OK) {
                    PushLuaLogThrottled("Event:OnTick runtime error: " + LuaErrStr(g_Lua));
                    lua_pop(g_Lua, 1);
                }
            } else {
                lua_pop(g_Lua, 1);
            }
        }
        lua_settop(g_Lua, gameTimeIdx - 1);   // guarantees no stack growth across frames, whatever the handlers did
    }

    // High-performance CPU throttling: Run marker trigger/distance loop at 40Hz (25ms)
    // instead of every 144Hz-240Hz render frame, cutting background CPU usage by up to 75%!
    static uint64_t s_LastMarkerCheckTick = 0;
    uint64_t currentTick = GetTickCount64();
    if (currentTick - s_LastMarkerCheckTick < 25) {
        return;
    }
    s_LastMarkerCheckTick = currentTick;

    // Update marker focus state and execute script-once, script-focus, autoTrigger, and script-tick
    int32_t currentMapId = static_cast<int32_t>(g_Mumble->Context.MapID);
    Vec3 avatar{ g_Mumble->AvatarPosition.X, g_Mumble->AvatarPosition.Y, g_Mumble->AvatarPosition.Z };
    size_t markerCount = g_Markers.size(); // snapshot size in case Lua creates new markers during callbacks

    for (size_t i = 0; i < markerCount; ++i) {
        if (i >= g_Markers.size()) break;
        if (g_Markers[i].MapId != 0 && g_Markers[i].MapId != currentMapId) continue;

        if (!g_Markers[i].ScriptOnce.empty() && !g_Markers[i].ScriptOnceExecuted) {
            g_Markers[i].ScriptOnceExecuted = true;
            std::string expr = g_Markers[i].ScriptOnce;
            InvokeMarkerLuaScript(i, expr, 0);
        }

        if (!g_Markers[i].IsVisible) continue;

        float dist = (g_Markers[i].Position - avatar).Length();
        float trigRange = (g_Markers[i].TriggerRange > 0.0f) ? g_Markers[i].TriggerRange : 0.0f;
        bool nowFocused = (trigRange > 0.0f) && (dist <= trigRange);
        bool wasFocused = g_Markers[i].IsFocused;
        g_Markers[i].IsFocused = nowFocused;

        if (nowFocused != wasFocused) {
            if (!g_Markers[i].ScriptFocus.empty()) {
                std::string expr = g_Markers[i].ScriptFocus;
                InvokeMarkerLuaScript(i, expr, 1, nowFocused);
            }
            if (nowFocused && g_EnableAutoTrigger && g_Markers[i].AutoTrigger && !g_Markers[i].ScriptAutoTriggered) {
                if (!g_Markers[i].ScriptTrigger.empty()) {
                    // Mark script-trigger auto-triggers as consumed so re-entering range doesn't spam Lua "already consumed" warnings
                    g_Markers[i].ScriptAutoTriggered = true;
                }
                TriggerMarkerInteraction(i, true);
            }
        }

        if (!g_Markers[i].ScriptTick.empty() && g_Markers[i].IsVisible) {
            std::string expr = g_Markers[i].ScriptTick;
            InvokeMarkerLuaScript(i, expr, 2, false, totalSeconds, deltaSeconds);
        }
    }
}

// ============================================================================
// 8. XML, TEXTURE & .TACO ARCHIVE LOADER
// ============================================================================
static AttributeMap ExtractXmlNodeAttributes(const pugi::xml_node& node) {
    AttributeMap stdAttrs;
    AttributeMap bhOverrides;

    for (pugi::xml_attribute attr : node.attributes()) {
        std::string name = ToLowerAscii(attr.name());
        if (name.rfind("bh-", 0) == 0 && name.size() > 3) {
            bhOverrides[name.substr(3)] = attr.value();
        } else {
            stdAttrs[name] = attr.value();
        }
    }
    for (auto& [k, v] : bhOverrides) {
        stdAttrs[k] = std::move(v);
    }
    return stdAttrs;
}

static CategoryNode* FindOrCreateCategoryChild(
    std::vector<std::unique_ptr<CategoryNode>>& list,
    const std::string& name,
    const std::string& fullPath)
{
    std::string lowerName = ToLowerAscii(name);
    for (auto& c : list) {
        if (ToLowerAscii(c->Name) == lowerName) {
            return c.get();
        }
    }
    auto node = std::make_unique<CategoryNode>();
    node->Name        = name;
    node->FullPath    = fullPath;
    node->DisplayName = name;
    CategoryNode* raw = node.get();
    list.push_back(std::move(node));
    g_CategoryLookup[ToLowerAscii(fullPath)] = raw;
    return raw;
}

static void ParseXmlCategoryRecursive(
    const pugi::xml_node& xmlNode,
    const std::string& parentFullPath,
    const AttributeMap& inheritedAttrs,
    std::vector<std::unique_ptr<CategoryNode>>& targetList,
    bool parentEnabled = true)
{
    AttributeMap localAttrs = ExtractXmlNodeAttributes(xmlNode);
    std::string name = localAttrs["name"];
    if (name.empty()) return;

    std::string fullPath = parentFullPath.empty() ? name : (parentFullPath + "." + name);
    CategoryNode* cat = FindOrCreateCategoryChild(targetList, name, fullPath);

    AttributeMap merged = inheritedAttrs;
    for (const auto& [k, v] : cat->MergedAttrs) merged[k] = v;
    for (const auto& [k, v] : localAttrs) merged[k] = v;

    cat->MergedAttrs = merged;
    if (localAttrs.count("displayname") && !localAttrs["displayname"].empty()) {
        cat->DisplayName = localAttrs["displayname"];
    }
    if (localAttrs.count("isseparator")) {
        cat->IsSeparator = SafeParseBool(localAttrs["isseparator"], false);
    }
    if (localAttrs.count("defaulttoggle")) {
        cat->DefaultToggle = SafeParseBool(localAttrs["defaulttoggle"], true);
    }
    cat->Enabled = parentEnabled && cat->DefaultToggle;

    for (pugi::xml_node child : xmlNode.children()) {
        if (ToLowerAscii(child.name()) == "markercategory") {
            ParseXmlCategoryRecursive(child, fullPath, merged, cat->Children, cat->Enabled);
        }
    }
}

using ArchiveFileFetcher = std::function<bool(const std::string& relPath, std::vector<uint8_t>& outBytes)>;

static std::string EnsurePackTextureLoaded(
    const std::string& packName,
    const std::string& relTexturePath,
    const ArchiveFileFetcher& fetchFile,
    ID3D11ShaderResourceView** outNativeSRV)
{
    if (relTexturePath.empty()) return "";
    std::string normRel = NormalizeArchivePath(relTexturePath);
    std::string texId   = "NP_TEX_" + packName + "_" + normRel;

    if (g_UploadedTextureIds.count(texId)) {
        if (outNativeSRV) {
            *outNativeSRV = g_D3DEngine.GetCachedTextureSRV(texId);
        }
        return texId;
    }

    std::vector<uint8_t> imgBytes;
    if (!fetchFile(normRel, imgBytes) || imgBytes.empty()) {
        return "";
    }

    g_UploadedTextureIds.insert(texId);
    ID3D11ShaderResourceView* nativeSrv = g_D3DEngine.LoadTextureFromMemory(texId, imgBytes.data(), imgBytes.size());
    if (outNativeSRV) {
        *outNativeSRV = nativeSrv;
    }

    // FIX 5: only fall back to a Nexus-side copy when our own D3D11 decode failed (e.g. no device yet).
    // Previously every texture was uploaded twice (native cache + Nexus), doubling VRAM per icon/trail.
    if (!nativeSrv && g_API) {
        if (g_API->Textures_GetOrCreateFromMemory) {
            g_API->Textures_GetOrCreateFromMemory(texId.c_str(), imgBytes.data(), imgBytes.size());
        } else if (g_API->Textures_LoadFromMemory) {
            g_API->Textures_LoadFromMemory(texId.c_str(), imgBytes.data(), imgBytes.size(), nullptr);
        }
    }
    return texId;
}

static bool FetchFileFromLoadedPackArchive(const std::string& packName, const std::string& relPath, std::vector<uint8_t>& outBytes) {
    // 1. Check in-memory cached pack files first
    if (FetchFromCachedPackFiles(relPath, outBytes)) {
        return true;
    }

    // 2. Lazy-load directly from the .taco / .zip archive or loose file on disk when needed for rendering!
    //    This avoids extracting & decoding thousands of textures from other maps at startup, saving >300 MB of RAM & VRAM!
    std::string lowPack = ToLowerAscii(packName);
    std::string fullArchivePath;
    for (const auto& p : g_LoadedPacks) {
        if (ToLowerAscii(p.Filename) == lowPack) {
            fullArchivePath = p.FullPath;
            break;
        }
    }
    if (fullArchivePath.empty()) return false;

    std::filesystem::path fsPath(fullArchivePath);
    std::string ext = ToLowerAscii(fsPath.extension().string());
    if (ext == ".taco" || ext == ".zip") {
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, fullArchivePath.c_str(), 0)) return false;

        std::string norm = NormalizeArchivePath(relPath);
        int foundIdx = mz_zip_reader_locate_file(&zip, norm.c_str(), nullptr, 0);
        if (foundIdx < 0) {
            std::string baseName = std::filesystem::path(norm).filename().string();
            mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
            for (mz_uint i = 0; i < numFiles; ++i) {
                mz_zip_archive_file_stat st{};
                if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
                std::string entryNorm = NormalizeArchivePath(st.m_filename);
                if (entryNorm == norm || std::filesystem::path(entryNorm).filename().string() == baseName) {
                    foundIdx = static_cast<int>(i);
                    break;
                }
            }
        }
        bool ok = false;
        if (foundIdx >= 0) {
            size_t uncompSize = 0;
            void* pData = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(foundIdx), &uncompSize, 0);
            if (pData && uncompSize > 0) {
                outBytes.assign(static_cast<uint8_t*>(pData), static_cast<uint8_t*>(pData) + uncompSize);
                mz_free(pData);
                ok = true;
            }
        }
        mz_zip_reader_end(&zip);
        return ok;
    } else {
        std::filesystem::path looseFile = fsPath.parent_path() / relPath;
        std::ifstream ifs(looseFile, std::ios::binary | std::ios::ate);
        if (!ifs) return false;
        auto sz = ifs.tellg();
        if (sz <= 0) return false;
        outBytes.resize(static_cast<size_t>(sz));
        ifs.seekg(0);
        ifs.read(reinterpret_cast<char*>(outBytes.data()), sz);
        return true;
    }
}

static ID3D11ShaderResourceView* ResolveMarkerSRV(RuntimeMarker& m) {
    if (m.NativeSRV) return m.NativeSRV;
    if (!m.IconFileRelative.empty() && m.IconTextureId.empty()) {
        std::string packName = m.PackName;
        m.IconTextureId = EnsurePackTextureLoaded(
            packName,
            m.IconFileRelative,
            [&](const std::string& rel, std::vector<uint8_t>& out) {
                return FetchFileFromLoadedPackArchive(packName, rel, out);
            },
            &m.NativeSRV
        );
        if (m.IconTextureId.empty()) {
            m.IconTextureId = "__FAILED__";
        }
    }
    if (m.NativeSRV) return m.NativeSRV;
    if (!m.CachedTexture && !m.IconTextureId.empty() && m.IconTextureId != "__FAILED__" && g_API && g_API->Textures_Get) {
        m.CachedTexture = g_API->Textures_Get(m.IconTextureId.c_str());
    }
    if (m.CachedTexture && m.CachedTexture->Resource) {
        m.NativeSRV = static_cast<ID3D11ShaderResourceView*>(m.CachedTexture->Resource);
        return m.NativeSRV;
    }
    return nullptr;
}

static ID3D11ShaderResourceView* ResolveTrailSRV(RuntimeTrail& t) {
    if (t.NativeSRV) return t.NativeSRV;
    if (!t.TextureRelative.empty() && t.TextureId.empty()) {
        std::string packName = t.PackName;
        t.TextureId = EnsurePackTextureLoaded(
            packName,
            t.TextureRelative,
            [&](const std::string& rel, std::vector<uint8_t>& out) {
                return FetchFileFromLoadedPackArchive(packName, rel, out);
            },
            &t.NativeSRV
        );
        if (t.TextureId.empty()) {
            t.TextureId = "__FAILED__";
        }
    }
    if (t.NativeSRV) return t.NativeSRV;
    if (!t.CachedTexture && !t.TextureId.empty() && t.TextureId != "__FAILED__" && g_API && g_API->Textures_Get) {
        t.CachedTexture = g_API->Textures_Get(t.TextureId.c_str());
    }
    if (t.CachedTexture && t.CachedTexture->Resource) {
        t.NativeSRV = static_cast<ID3D11ShaderResourceView*>(t.CachedTexture->Resource);
        return t.NativeSRV;
    }
    return nullptr;
}

static void ParseOverlayXmlDocument(
    const char* xmlBuffer,
    size_t xmlSize,
    const std::string& packName,
    const ArchiveFileFetcher& fetchFile,
    LoadedPackSummary& summary,
    bool categoriesPassOnly = false)
{
    pugi::xml_document doc;
    pugi::xml_parse_result res = doc.load_buffer(xmlBuffer, xmlSize);
    if (!res) return;

    pugi::xml_node root = doc.document_element();
    if (!root) return;

    // Pass 1: Parse all MarkerCategory trees across all XML files in the archive first!
    if (categoriesPassOnly) {
        for (pugi::xml_node child : root.children()) {
            if (ToLowerAscii(child.name()) == "markercategory") {
                ParseXmlCategoryRecursive(child, "", {}, g_RootCategories, true);
            }
        }
        return;
    }

    summary.XmlCount++;

    // Also parse MarkerCategory in single-pass loose XML mode if not already parsed
    for (pugi::xml_node child : root.children()) {
        if (ToLowerAscii(child.name()) == "markercategory") {
            ParseXmlCategoryRecursive(child, "", {}, g_RootCategories, true);
        }
    }

    // Pass 2: Parse POIs and Trails inside <POIs> or directly under <OverlayData>
    auto processPoiContainer = [&](const pugi::xml_node& container) {
        for (pugi::xml_node el : container.children()) {
            std::string tag = ToLowerAscii(el.name());
            if (tag != "poi" && tag != "trail") continue;

            AttributeMap attrs = ExtractXmlNodeAttributes(el);
            std::string typePath = ToLowerAscii(attrs["type"]);

            // Inherit category attributes up the hierarchy
            auto catIt = g_CategoryLookup.find(typePath);
            if (catIt != g_CategoryLookup.end() && catIt->second) {
                AttributeMap merged = catIt->second->MergedAttrs;
                for (auto& [k, v] : attrs) merged[k] = std::move(v);
                attrs = std::move(merged);
            }

            if (tag == "poi") {
                RuntimeMarker m{};
                m.PackName     = packName;
                m.TypePath     = typePath;
                m.Guid         = attrs.count("guid") ? attrs["guid"] : ("poi_" + std::to_string(g_Markers.size()));
                m.MapId        = SafeParseInt(attrs["mapid"], 0);

                // In TacO XML (<POI xpos="..." ypos="..." zpos="...">):
                // xpos = East/West (Mumble X), ypos = Altitude (Mumble Y), zpos = North/South (Mumble Z)
                m.Position.x   = SafeParseFloat(attrs["xpos"], 0.0f);
                m.Position.y   = SafeParseFloat(attrs["ypos"], 0.0f);
                m.Position.z   = SafeParseFloat(attrs["zpos"], 0.0f);

                m.IconSize          = SafeParseFloat(attrs["iconsize"], 1.0f);
                m.HeightOffset      = SafeParseFloat(attrs["heightoffset"], 1.5f);
                float alpha         = SafeParseFloat(attrs["alpha"], 1.0f);
                m.TintColor         = ParseHexColor(attrs.count("color") ? attrs["color"] : attrs["tint"], alpha);
                m.FadeNear          = SafeParseFloat(attrs["fadenear"], 1200.0f);
                m.FadeFar           = SafeParseFloat(attrs["fadefar"], 1800.0f);
                m.MapDisplaySize    = SafeParseFloat(attrs["mapdisplaysize"], 20.0f);
                m.InGameVisibility  = SafeParseBool(attrs["ingamevisibility"], true);
                m.MiniMapVisibility = SafeParseBool(attrs["minimapvisibility"], true);
                m.MapVisibility     = SafeParseBool(attrs["mapvisibility"], true);
                m.Behavior          = SafeParseInt(attrs["behavior"], 0);
                m.InvertBehavior    = SafeParseBool(attrs["invertbehavior"], false);
                m.ResetLengthSec    = SafeParseInt(attrs["resetlength"], 0);
                m.AchievementId     = SafeParseInt(attrs["achievementid"], 0);
                m.AchievementBit    = SafeParseInt(attrs["achievementbit"], -1);
                m.TriggerRange      = SafeParseFloat(attrs["triggerrange"], 8.0f);
                m.InfoRange         = SafeParseFloat(attrs["inforange"], 20.0f);
                if (!attrs.count("triggerrange") && attrs.count("inforange")) {
                    m.TriggerRange  = m.InfoRange;
                }
                m.AutoTrigger       = SafeParseBool(attrs["autotrigger"], false);
                m.CopyValue         = attrs["copy"];
                m.CopyMessage       = attrs["copy-message"];
                if (attrs.count("toggle")) m.ShowHideCategory = attrs["toggle"];
                else if (attrs.count("togglecategory")) m.ShowHideCategory = attrs["togglecategory"];
                else if (attrs.count("showhide")) m.ShowHideCategory = attrs["showhide"];
                m.InfoText          = attrs.count("info") ? attrs["info"] : attrs["tip-name"];

                if (attrs.count("mount")) {
                    std::string mnt = ToLowerAscii(attrs["mount"]);
                    if (mnt.find("jackal") != std::string::npos)   m.MountFilterMask |= (1u << 1);
                    if (mnt.find("griffon") != std::string::npos)  m.MountFilterMask |= (1u << 2);
                    if (mnt.find("springer") != std::string::npos) m.MountFilterMask |= (1u << 3);
                    if (mnt.find("skimmer") != std::string::npos)  m.MountFilterMask |= (1u << 4);
                    if (mnt.find("raptor") != std::string::npos)   m.MountFilterMask |= (1u << 5);
                    if (mnt.find("beetle") != std::string::npos)   m.MountFilterMask |= (1u << 6);
                    if (mnt.find("warclaw") != std::string::npos)  m.MountFilterMask |= (1u << 7);
                    if (mnt.find("skyscale") != std::string::npos) m.MountFilterMask |= (1u << 8);
                    if (mnt.find("turtle") != std::string::npos)   m.MountFilterMask |= (1u << 9);
                }

                // Store relative texture path only; texture is lazy-loaded on demand when rendered on current map!
                m.IconFileRelative  = attrs["iconfile"];

                // Lua script attributes
                m.ScriptOnce    = attrs["script-once"];
                m.ScriptTick    = attrs["script-tick"];
                m.ScriptTrigger = attrs["script-trigger"];
                m.ScriptFocus   = attrs["script-focus"];
                m.ScriptFilter  = attrs["script-filter"];

                if (attrs.count("bounce")) {
                    m.HasBounce      = true;
                    m.BounceIsRise   = (ToLowerAscii(attrs["bounce"]) == "rise");
                    m.BounceHeight   = SafeParseFloat(attrs["bounce-height"], 2.0f);
                    m.BounceDuration = SafeParseFloat(attrs["bounce-duration"], 1.5f);
                }

                g_Markers.push_back(std::move(m));
                summary.MarkerCount++;
            } else if (tag == "trail") {
                std::string trailDataRel = attrs["traildata"];
                if (trailDataRel.empty()) continue;

                std::vector<uint8_t> trlBytes;
                if (!fetchFile(trailDataRel, trlBytes)) continue;

                RuntimeTrail trail{};
                trail.PackName          = packName;
                trail.TypePath          = typePath;
                trail.Guid              = attrs.count("guid") ? attrs["guid"] : ("trail_" + std::to_string(g_Trails.size()));
                trail.TrailDataPath     = trailDataRel;
                // Store relative texture path only; texture is lazy-loaded on demand when rendered on current map!
                trail.TextureRelative   = attrs["texture"];

                float alpha             = SafeParseFloat(attrs["alpha"], 0.92f);
                trail.TintColor         = ParseHexColor(attrs.count("color") ? attrs["color"] : attrs["tint"], alpha);
                trail.AnimSpeed         = SafeParseFloat(attrs["animspeed"], 1.0f);
                trail.TrailScale        = SafeParseFloat(attrs["trailscale"], 1.0f);
                trail.FadeNear          = SafeParseFloat(attrs["fadenear"], 1200.0f);
                trail.FadeFar           = SafeParseFloat(attrs["fadefar"], 2000.0f);
                trail.InGameVisibility  = SafeParseBool(attrs["ingamevisibility"], true);
                trail.MiniMapVisibility = SafeParseBool(attrs["minimapvisibility"], true);
                trail.MapVisibility     = SafeParseBool(attrs["mapvisibility"], true);
                trail.IsWall            = SafeParseBool(attrs["iswall"], false) || SafeParseBool(attrs["wall"], false);
                trail.CanFade           = SafeParseBool(attrs["canfade"], true);

                int32_t parsedMapId = 0;
                if (ParseTrlBinaryBuffer(trlBytes.data(), trlBytes.size(), parsedMapId, trail.Segments)) {
                    trail.MapId = attrs.count("mapid") ? SafeParseInt(attrs["mapid"], parsedMapId) : parsedMapId;
                    g_D3DEngine.PrecomputeTrailCornerRights(trail);
                    g_Trails.push_back(std::move(trail));
                    summary.TrailCount++;
                }
            }
        }
    };

    processPoiContainer(root);
    for (pugi::xml_node child : root.children()) {
        if (ToLowerAscii(child.name()) == "pois") {
            processPoiContainer(child);
        }
    }
}

static void LoadTacoZipArchive(const std::filesystem::path& archivePath) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, archivePath.string().c_str(), 0)) {
        return;
    }

    LoadedPackSummary summary{};
    summary.Filename = archivePath.filename().string();
    summary.FullPath = archivePath.string();

    std::unordered_map<std::string, mz_uint> entryLookup;
    std::vector<mz_uint> xmlIndices;
    std::vector<mz_uint> trlIndices;
    std::vector<std::pair<std::string, mz_uint>> luaEntries;

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        if (st.m_is_directory) continue;

        std::string normName = NormalizeArchivePath(st.m_filename);
        entryLookup[normName] = i;

        if (normName.size() >= 4 && normName.substr(normName.size() - 4) == ".xml") {
            xmlIndices.push_back(i);
        } else if (normName.size() >= 4 && normName.substr(normName.size() - 4) == ".trl") {
            trlIndices.push_back(i);
        } else if (normName.size() >= 4 && normName.substr(normName.size() - 4) == ".lua") {
            luaEntries.push_back({ normName, i });
        }
    }

    auto fetchFileFromZip = [&](const std::string& relPath, std::vector<uint8_t>& outBytes) -> bool {
        std::string norm = NormalizeArchivePath(relPath);
        auto it = entryLookup.find(norm);
        if (it == entryLookup.end()) {
            std::string baseName = std::filesystem::path(norm).filename().string();
            for (const auto& [k, idx] : entryLookup) {
                if (std::filesystem::path(k).filename().string() == baseName) {
                    it = entryLookup.find(k);
                    break;
                }
            }
        }
        if (it == entryLookup.end()) return false;

        size_t uncompSize = 0;
        void* pData = mz_zip_reader_extract_to_heap(&zip, it->second, &uncompSize, 0);
        if (!pData) return false;

        outBytes.assign(static_cast<uint8_t*>(pData), static_cast<uint8_t*>(pData) + uncompSize);
        mz_free(pData);
        return true;
    };

    // Pass 1: Parse all <MarkerCategory> trees across all XML files in the .taco archive
    for (mz_uint xmlIdx : xmlIndices) {
        size_t xmlSize = 0;
        void* pXml = mz_zip_reader_extract_to_heap(&zip, xmlIdx, &xmlSize, 0);
        if (pXml && xmlSize > 0) {
            ParseOverlayXmlDocument(static_cast<const char*>(pXml), xmlSize, summary.Filename, fetchFileFromZip, summary, true);
            mz_free(pXml);
        }
    }

    // Pass 2: Parse all <POI> and <Trail> elements with complete category inheritance
    for (mz_uint xmlIdx : xmlIndices) {
        size_t xmlSize = 0;
        void* pXml = mz_zip_reader_extract_to_heap(&zip, xmlIdx, &xmlSize, 0);
        if (pXml && xmlSize > 0) {
            ParseOverlayXmlDocument(static_cast<const char*>(pXml), xmlSize, summary.Filename, fetchFileFromZip, summary, false);
            mz_free(pXml);
        }
    }

    // Only extract binary images for packs that actually contain Lua scripts (e.g. TehsTrails),
    // while standard XML/.taco packs lazy-load textures on demand per map!
    g_CurrentPackName = summary.Filename;
    if (!luaEntries.empty()) {
        for (mz_uint i = 0; i < numFiles; ++i) {
            mz_zip_archive_file_stat st{};
            if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
            std::string normName = NormalizeArchivePath(st.m_filename);
            if (normName.size() >= 4) {
                std::string ext = normName.substr(normName.size() - 4);
                if (ext == ".png" || ext == ".jpg" || ext == ".dds" || ext == ".bmp" || ext == ".tga") {
                    size_t binSize = 0;
                    void* pBin = mz_zip_reader_extract_to_heap(&zip, i, &binSize, 0);
                    if (pBin && binSize > 0) {
                        const auto* bPtr = static_cast<const uint8_t*>(pBin);
                        g_PackBinaryFiles[normName] = std::vector<uint8_t>(bPtr, bPtr + binSize);
                        mz_free(pBin);
                    }
                }
            }
        }
    }

    std::unordered_map<std::string, std::string> thisPackLua;
    for (const auto& [luaPath, luaIdx] : luaEntries) {
        size_t luaSize = 0;
        void* pLua = mz_zip_reader_extract_to_heap(&zip, luaIdx, &luaSize, 0);
        if (pLua && luaSize > 0) {
            std::string code(static_cast<const char*>(pLua), luaSize);
            g_LuaScriptsInPack[luaPath] = code;
            thisPackLua[luaPath] = std::move(code);
            mz_free(pLua);
            summary.LuaCount++;
        }
    }

    // Entry script: root pack.lua, else the shallowest pack.lua in a sub-folder (some packs nest everything).
    std::string packLuaKey;
    if (thisPackLua.count("pack.lua")) {
        packLuaKey = "pack.lua";
    } else {
        for (const auto& [k, code] : thisPackLua) {
            if (std::filesystem::path(k).filename().string() != "pack.lua") continue;
            if (packLuaKey.empty() || k.size() < packLuaKey.size()) packLuaKey = k;
        }
    }
    if (g_Lua && !packLuaKey.empty()) {
        const std::string& packLua = thisPackLua[packLuaKey];
        g_PackEntryLuaScripts.push_back({ summary.Filename, packLua });
        if (LuaLoadText(g_Lua, packLua, summary.Filename + ":" + packLuaKey) == LUA_OK) {
            if (LuaSafeCall(g_Lua, 0, 0) != LUA_OK) {
                PushLuaLog("pack.lua runtime error: " + LuaErrStr(g_Lua), LOGL_WARNING);
                lua_pop(g_Lua, 1);
            } else {
                PushLuaLog("Executed pack.lua from " + summary.Filename, LOGL_INFO);
            }
        } else {
            PushLuaLog("pack.lua load error: " + LuaErrStr(g_Lua), LOGL_WARNING);
            lua_pop(g_Lua, 1);
        }
    }

    if (xmlIndices.empty() && !trlIndices.empty()) {
        for (mz_uint trlIdx : trlIndices) {
            size_t uncompSize = 0;
            void* pData = mz_zip_reader_extract_to_heap(&zip, trlIdx, &uncompSize, 0);
            if (!pData) continue;

            RuntimeTrail trail{};
            trail.PackName      = summary.Filename;
            trail.Guid          = summary.Filename + "#" + std::to_string(trlIdx);
            trail.TrailDataPath = trail.Guid;
            if (ParseTrlBinaryBuffer(static_cast<const uint8_t*>(pData), uncompSize, trail.MapId, trail.Segments)) {
                g_D3DEngine.PrecomputeTrailCornerRights(trail);
                g_Trails.push_back(std::move(trail));
                summary.TrailCount++;
            }
            mz_free(pData);
        }
    }

    mz_zip_reader_end(&zip);
    g_LoadedPacks.push_back(std::move(summary));
}

static std::filesystem::path GetConfigFilePath() {
    if (g_API && g_API->Paths_GetAddonDirectory) {
        if (const char* npDir = g_API->Paths_GetAddonDirectory("NexusPathing")) {
            std::error_code ec;
            std::filesystem::path dirPath(npDir);
            if (!std::filesystem::exists(dirPath, ec)) {
                std::filesystem::create_directories(dirPath, ec);
            }
            std::filesystem::path cfg = dirPath / "config.ini";
            g_ConfigFilePathStr = cfg.string();
            return cfg;
        }
    }
    return std::filesystem::path("NexusPathing_config.ini");
}

static std::filesystem::path GetPrimaryMarkersDirectory() {
    if (!g_ScannedDirectories.empty()) {
        return std::filesystem::path(g_ScannedDirectories.front());
    }
    if (g_API && g_API->Paths_GetAddonDirectory) {
        if (const char* npDir = g_API->Paths_GetAddonDirectory("NexusPathing")) {
            std::error_code ec;
            std::filesystem::path dirPath = std::filesystem::path(npDir) / "markers";
            if (!std::filesystem::exists(dirPath, ec)) {
                std::filesystem::create_directories(dirPath, ec);
            }
            return dirPath;
        }
    }
    return std::filesystem::path(".");
}

// ============================================================================
// 8B. WINHTTP HTTPS CLIENT, GW2 API KEY ACHIEVEMENT SYNC & GITHUB TACO MANAGER
// ============================================================================
static bool PerformHttpsGetToBuffer(const std::string& fullUrl, std::vector<uint8_t>& outBody, const std::string& bearerToken = "") {
    outBody.clear();
    if (fullUrl.empty()) return false;

    int wLen = MultiByteToWideChar(CP_UTF8, 0, fullUrl.c_str(), -1, nullptr, 0);
    if (wLen <= 0) return false;
    std::wstring wUrl(static_cast<size_t>(wLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, fullUrl.c_str(), -1, wUrl.data(), wLen);

    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {};
    wchar_t urlPath[2048] = {};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 2048;

    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &urlComp)) {
        return false;
    }

    HINTERNET hSession = WinHttpOpen(
        L"NexusPathing/3.0 (Raidcore Nexus; Guild Wars 2)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return false;

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        urlPath,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring headers = L"User-Agent: NexusPathing/3.0\r\nAccept: application/json, application/octet-stream, */*\r\n";
    if (!bearerToken.empty()) {
        int bLen = MultiByteToWideChar(CP_UTF8, 0, bearerToken.c_str(), -1, nullptr, 0);
        if (bLen > 1) {
            std::wstring wToken(static_cast<size_t>(bLen - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, bearerToken.c_str(), static_cast<int>(bearerToken.size()), wToken.data(), bLen - 1);
            headers += L"Authorization: Bearer " + wToken + L"\r\n";
        }
    }

    bool ok = false;
    if (WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr))
    {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX
        );

        if (statusCode >= 200 && statusCode < 300) {
            DWORD dwSize = 0;
            do {
                if (g_ShuttingDown.load()) { outBody.clear(); break; }
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (dwSize == 0) break;

                size_t curSize = outBody.size();
                outBody.resize(curSize + dwSize);
                DWORD dwDownloaded = 0;
                if (WinHttpReadData(hRequest, outBody.data() + curSize, dwSize, &dwDownloaded)) {
                    outBody.resize(curSize + dwDownloaded);
                } else {
                    outBody.resize(curSize);
                    break;
                }
            } while (dwSize > 0);
            ok = !outBody.empty();
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

static std::string ExtractJsonStringField(const std::string& json, const std::string& key, size_t startSearchPos = 0) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern, startSearchPos);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t endPos = pos + 1;
    std::string out;
    while (endPos < json.size()) {
        if (json[endPos] == '\\' && endPos + 1 < json.size()) {
            out.push_back(json[endPos + 1]);
            endPos += 2;
        } else if (json[endPos] == '"') {
            break;
        } else {
            out.push_back(json[endPos]);
            endPos++;
        }
    }
    return out;
}

static bool IsMarkerHiddenByAchievement(const RuntimeMarker& m) {
    if (!g_HideCompletedAchPois || m.AchievementId <= 0) return false;
    Win32LockGuard lock(g_Gw2ApiMutex);
    if (g_CompletedAchievements.empty() && g_AchievementBits.empty()) return false;

    bool isComplete = false;
    if (g_CompletedAchievements.count(m.AchievementId) > 0) {
        isComplete = true;
    } else if (m.AchievementBit >= 0) {
        auto it = g_AchievementBits.find(m.AchievementId);
        if (it != g_AchievementBits.end() && it->second.count(m.AchievementBit) > 0) {
            isComplete = true;
        }
    }
    return m.InvertBehavior ? !isComplete : isComplete;
}

static void SyncGw2ApiAchievementsAsync(std::string apiKey) {
    // Trim whitespace
    while (!apiKey.empty() && (apiKey.front() == ' ' || apiKey.front() == '\t' || apiKey.front() == '\r' || apiKey.front() == '\n')) apiKey.erase(0, 1);
    while (!apiKey.empty() && (apiKey.back() == ' ' || apiKey.back() == '\t' || apiKey.back() == '\r' || apiKey.back() == '\n')) apiKey.pop_back();

    if (apiKey.empty()) {
        Win32LockGuard lock(g_Gw2ApiMutex);
        g_Gw2ApiStatusText = "Enter a valid Guild Wars 2 API Key first.";
        return;
    }

    bool expected = false;
    if (!g_Gw2ApiFetchInProgress.compare_exchange_strong(expected, true)) {
        return;
    }

    {
        Win32LockGuard lock(g_Gw2ApiMutex);
        g_Gw2ApiStatusText = "Connecting to api.guildwars2.com/v2/account...";
    }

    SpawnDetachedWin32Thread([apiKey]() {
        std::vector<uint8_t> acctBuf;
        std::string acctName;
        if (PerformHttpsGetToBuffer("https://api.guildwars2.com/v2/account", acctBuf, apiKey)) {
            std::string acctJson(reinterpret_cast<const char*>(acctBuf.data()), acctBuf.size());
            acctName = ExtractJsonStringField(acctJson, "name");
        }

        std::vector<uint8_t> achBuf;
        if (!PerformHttpsGetToBuffer("https://api.guildwars2.com/v2/account/achievements", achBuf, apiKey)) {
            Win32LockGuard lock(g_Gw2ApiMutex);
            g_Gw2ApiStatusText = "GW2 API Error: Check key & ensure 'account' + 'progression' permissions are enabled.";
            g_Gw2ApiFetchInProgress.store(false);
            return;
        }

        std::string json(reinterpret_cast<const char*>(achBuf.data()), achBuf.size());
        std::unordered_set<int32_t> completedSet;
        std::unordered_map<int32_t, std::unordered_set<int32_t>> bitsMap;

        // Lightweight fast parser for `/v2/account/achievements` array of `{ "id": 123, "bits": [0, 2], "done": true }`
        size_t pos = 0;
        while ((pos = json.find('{', pos)) != std::string::npos) {
            size_t objEnd = json.find('}', pos);
            if (objEnd == std::string::npos) break;
            std::string_view obj(json.data() + pos, objEnd - pos + 1);

            int32_t achId = 0;
            auto idPos = obj.find("\"id\"");
            if (idPos != std::string_view::npos) {
                auto colon = obj.find(':', idPos);
                if (colon != std::string_view::npos) {
                    achId = static_cast<int32_t>(std::strtol(obj.data() + colon + 1, nullptr, 10));
                }
            }

            if (achId > 0) {
                auto donePos = obj.find("\"done\"");
                if (donePos != std::string_view::npos) {
                    auto colon = obj.find(':', donePos);
                    if (colon != std::string_view::npos) {
                        std::string_view afterColon = obj.substr(colon + 1);
                        if (afterColon.find("true") < afterColon.find(',')) {
                            completedSet.insert(achId);
                        }
                    }
                }

                auto bitsPos = obj.find("\"bits\"");
                if (bitsPos != std::string_view::npos) {
                    auto brOpen = obj.find('[', bitsPos);
                    auto brClose = obj.find(']', bitsPos);
                    if (brOpen != std::string_view::npos && brClose != std::string_view::npos && brClose > brOpen) {
                        std::string_view arr = obj.substr(brOpen + 1, brClose - brOpen - 1);
                        const char* p = arr.data();
                        const char* endP = arr.data() + arr.size();
                        while (p < endP) {
                            while (p < endP && (*p < '0' || *p > '9')) ++p;
                            if (p >= endP) break;
                            char* nextP = nullptr;
                            long bitVal = std::strtol(p, &nextP, 10);
                            if (nextP == p) break;
                            bitsMap[achId].insert(static_cast<int32_t>(bitVal));
                            p = nextP;
                        }
                    }
                }
            }
            pos = objEnd + 1;
        }

        {
            Win32LockGuard lock(g_Gw2ApiMutex);
            g_CompletedAchievements = std::move(completedSet);
            g_AchievementBits       = std::move(bitsMap);
            g_Gw2ApiAccountName     = acctName;
            g_Gw2ApiStatusText      = "Synced " + (acctName.empty() ? std::string("Account") : acctName) +
                                      ": " + std::to_string(g_CompletedAchievements.size()) + " completed achievements (" +
                                      std::to_string(g_AchievementBits.size()) + " partial bit-sets)";
        }
        g_Gw2ApiNeedsResync.store(true);
        g_Gw2ApiFetchInProgress.store(false);
    });
}

static bool ParseGithubOwnerRepoFromUrl(std::string url, std::string& outOwnerRepo, std::string& outDirectDownloadUrl) {
    outOwnerRepo.clear();
    outDirectDownloadUrl.clear();
    while (!url.empty() && (url.front() == ' ' || url.front() == '\t' || url.front() == '\r' || url.front() == '\n')) url.erase(0, 1);
    while (!url.empty() && (url.back() == ' ' || url.back() == '\t' || url.back() == '\r' || url.back() == '\n' || url.back() == '/')) url.pop_back();
    if (url.empty()) return false;

    std::string low = ToLowerAscii(url);
    if (low.size() >= 5 && (low.substr(low.size() - 5) == ".taco" || low.substr(low.size() - 4) == ".zip")) {
        outDirectDownloadUrl = url;
    }

    auto ghPos = low.find("github.com/");
    std::string pathPart;
    if (ghPos != std::string::npos) {
        pathPart = url.substr(ghPos + 11);
    } else if (url.find("://") == std::string::npos && url.find('/') != std::string::npos) {
        pathPart = url; // Supports shorthand "owner/repo"
    } else {
        return !outDirectDownloadUrl.empty();
    }

    auto firstSlash = pathPart.find('/');
    if (firstSlash == std::string::npos || firstSlash == 0) return !outDirectDownloadUrl.empty();
    std::string owner = pathPart.substr(0, firstSlash);
    std::string rest  = pathPart.substr(firstSlash + 1);
    auto secondSlash  = rest.find('/');
    std::string repo  = (secondSlash == std::string::npos) ? rest : rest.substr(0, secondSlash);
    if (repo.size() > 4 && ToLowerAscii(repo.substr(repo.size() - 4)) == ".git") {
        repo.resize(repo.size() - 4);
    }
    if (!owner.empty() && !repo.empty()) {
        outOwnerRepo = owner + "/" + repo;
        return true;
    }
    return !outDirectDownloadUrl.empty();
}

static bool QueryGithubLatestReleaseAsset(const std::string& ownerRepo, std::string& outTag, std::string& outAssetName, std::string& outDownloadUrl) {
    outTag.clear();
    outAssetName.clear();
    outDownloadUrl.clear();

    std::vector<uint8_t> body;
    std::string apiUrl = "https://api.github.com/repos/" + ownerRepo + "/releases/latest";
    if (!PerformHttpsGetToBuffer(apiUrl, body)) {
        // Fallback to /releases list in case repo uses pre-releases only
        apiUrl = "https://api.github.com/repos/" + ownerRepo + "/releases";
        if (!PerformHttpsGetToBuffer(apiUrl, body)) {
            return false;
        }
    }

    std::string json(reinterpret_cast<const char*>(body.data()), body.size());
    outTag = ExtractJsonStringField(json, "tag_name");
    if (outTag.empty()) outTag = "latest";

    // Scan all "browser_download_url" values and prefer .taco first, then .zip
    std::string fallbackZipUrl;
    std::string fallbackZipName;
    size_t searchPos = 0;
    while (true) {
        size_t keyPos = json.find("\"browser_download_url\"", searchPos);
        if (keyPos == std::string::npos) break;
        std::string dlUrl = ExtractJsonStringField(json, "browser_download_url", keyPos);
        searchPos = keyPos + 22;
        if (dlUrl.empty()) continue;

        std::string lowUrl = ToLowerAscii(dlUrl);
        auto slashPos = dlUrl.rfind('/');
        std::string fileName = (slashPos != std::string::npos) ? dlUrl.substr(slashPos + 1) : (ownerRepo + ".taco");

        if (lowUrl.size() >= 5 && lowUrl.substr(lowUrl.size() - 5) == ".taco") {
            outDownloadUrl = dlUrl;
            outAssetName   = fileName;
            return true;
        }
        if (fallbackZipUrl.empty() && lowUrl.size() >= 4 && lowUrl.substr(lowUrl.size() - 4) == ".zip") {
            fallbackZipUrl  = dlUrl;
            fallbackZipName = fileName;
        }
    }

    if (!fallbackZipUrl.empty()) {
        outDownloadUrl = fallbackZipUrl;
        outAssetName   = fallbackZipName;
        return true;
    }
    return false;
}

// Button 1 Action: Paste & Download GitHub Repo (.taco / .zip)
static void StartDownloadFromGithubUrlAsync(std::string rawUrl) {
    bool expected = false;
    if (!g_TacoTaskInProgress.compare_exchange_strong(expected, true)) {
        return;
    }

    {
        Win32LockGuard lock(g_TacoManagerMutex);
        g_TacoManagerStatusText = "Resolving GitHub repository release for: " + rawUrl + " ...";
    }

    SpawnDetachedWin32Thread([rawUrl]() {
        std::string ownerRepo;
        std::string directUrl;
        if (!ParseGithubOwnerRepoFromUrl(rawUrl, ownerRepo, directUrl)) {
            Win32LockGuard lock(g_TacoManagerMutex);
            g_TacoManagerStatusText = "Invalid URL. Paste a GitHub repo link (e.g. https://github.com/xrandox/TehsTrails) or direct .taco URL.";
            g_TacoTaskInProgress.store(false);
            return;
        }

        std::string tag = "latest";
        std::string assetName;
        std::string downloadUrl = directUrl;

        if (downloadUrl.empty() && !ownerRepo.empty()) {
            if (!QueryGithubLatestReleaseAsset(ownerRepo, tag, assetName, downloadUrl)) {
                Win32LockGuard lock(g_TacoManagerMutex);
                g_TacoManagerStatusText = "No .taco or .zip release asset found in GitHub repo: " + ownerRepo;
                g_TacoTaskInProgress.store(false);
                return;
            }
        } else if (!downloadUrl.empty()) {
            auto slash = downloadUrl.rfind('/');
            assetName = (slash != std::string::npos) ? downloadUrl.substr(slash + 1) : "downloaded_pack.taco";
        }

        std::string localFile = assetName;
        if (!ownerRepo.empty()) {
            std::string safePrefix = ownerRepo;
            std::replace(safePrefix.begin(), safePrefix.end(), '/', '_');
            if (ToLowerAscii(localFile).find(ToLowerAscii(safePrefix)) == std::string::npos &&
                ToLowerAscii(localFile).find(ToLowerAscii(ownerRepo.substr(ownerRepo.find('/') + 1))) == std::string::npos) {
                localFile = safePrefix + "_" + assetName;
            }
        }

        {
            Win32LockGuard lock(g_TacoManagerMutex);
            g_TacoManagerStatusText = "Downloading " + localFile + " (" + tag + ")...";
        }

        std::vector<uint8_t> fileBytes;
        if (!PerformHttpsGetToBuffer(downloadUrl, fileBytes) || fileBytes.size() < 32) {
            Win32LockGuard lock(g_TacoManagerMutex);
            g_TacoManagerStatusText = "Download failed for: " + downloadUrl;
            g_TacoTaskInProgress.store(false);
            return;
        }

        std::filesystem::path markersDir = GetPrimaryMarkersDirectory();
        std::error_code ec;
        std::filesystem::create_directories(markersDir, ec);
        std::filesystem::path destPath = markersDir / localFile;

        {
            std::ofstream ofs(destPath, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                Win32LockGuard lock(g_TacoManagerMutex);
                g_TacoManagerStatusText = "Failed to write file to disk: " + destPath.string();
                g_TacoTaskInProgress.store(false);
                return;
            }
            ofs.write(reinterpret_cast<const char*>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));
        }

        {
            Win32LockGuard lock(g_TacoManagerMutex);
            bool found = false;
            for (auto& item : g_ManagedTacoRepos) {
                if ((!ownerRepo.empty() && ToLowerAscii(item.OwnerRepo) == ToLowerAscii(ownerRepo)) ||
                    ToLowerAscii(item.LocalFilename) == ToLowerAscii(localFile)) {
                    item.RepoUrl         = rawUrl;
                    item.OwnerRepo       = ownerRepo;
                    item.LocalFilename   = localFile;
                    item.InstalledTag    = tag;
                    item.LatestTag       = tag;
                    item.DownloadUrl     = downloadUrl;
                    item.AssetName       = assetName;
                    item.UpdateAvailable = false;
                    item.StatusText      = "Installed (" + tag + ", " + std::to_string(fileBytes.size() / 1024) + " KB)";
                    found = true;
                    break;
                }
            }
            if (!found) {
                ManagedTacoRepo repo{};
                repo.RepoUrl         = rawUrl;
                repo.OwnerRepo       = ownerRepo;
                repo.LocalFilename   = localFile;
                repo.InstalledTag    = tag;
                repo.LatestTag       = tag;
                repo.DownloadUrl     = downloadUrl;
                repo.AssetName       = assetName;
                repo.UpdateAvailable = false;
                repo.StatusText      = "Installed (" + tag + ", " + std::to_string(fileBytes.size() / 1024) + " KB)";
                g_ManagedTacoRepos.push_back(std::move(repo));
            }
            g_TacoManagerStatusText = "Downloaded & installed " + localFile + " (" + tag + ")! Reloading marker packs...";
        }

        g_PendingSaveConfigOnMainThread.store(true);
        g_PendingReloadPacksOnMainThread.store(true);
        g_TacoTaskInProgress.store(false);
    });
}

// Button 2 Action: Check Updates for all tracked GitHub repositories
static void StartCheckTacoUpdatesAsync() {
    bool expected = false;
    if (!g_TacoTaskInProgress.compare_exchange_strong(expected, true)) {
        return;
    }

    SpawnDetachedWin32Thread([]() {
        std::vector<ManagedTacoRepo> snapshot;
        {
            Win32LockGuard lock(g_TacoManagerMutex);
            snapshot = g_ManagedTacoRepos;
            g_TacoManagerStatusText = "Checking GitHub releases for " + std::to_string(snapshot.size()) + " managed pack(s)...";
        }

        if (snapshot.empty()) {
            Win32LockGuard lock(g_TacoManagerMutex);
            g_TacoManagerStatusText = "No GitHub repositories tracked yet. Paste a GitHub repo link above and click Download!";
            g_TacoTaskInProgress.store(false);
            return;
        }

        size_t updatesFound = 0;
        for (auto& repo : snapshot) {
            if (repo.OwnerRepo.empty()) {
                repo.StatusText = "Direct URL (Ready to re-download)";
                continue;
            }
            std::string latestTag, assetName, dlUrl;
            if (QueryGithubLatestReleaseAsset(repo.OwnerRepo, latestTag, assetName, dlUrl)) {
                repo.LatestTag   = latestTag;
                repo.AssetName   = assetName;
                repo.DownloadUrl = dlUrl;
                if (repo.InstalledTag.empty() || repo.InstalledTag != latestTag) {
                    repo.UpdateAvailable = true;
                    repo.StatusText      = "Update Available: " + repo.InstalledTag + " -> " + latestTag;
                    updatesFound++;
                } else {
                    repo.UpdateAvailable = false;
                    repo.StatusText      = "Up to date (" + latestTag + ")";
                }
            } else {
                repo.StatusText = "Failed to query GitHub API";
            }
        }

        {
            Win32LockGuard lock(g_TacoManagerMutex);
            g_ManagedTacoRepos = std::move(snapshot);
            if (updatesFound > 0) {
                g_TacoManagerStatusText = "Found " + std::to_string(updatesFound) + " pack update(s)! Click 'Update All Files' to install.";
            } else {
                g_TacoManagerStatusText = "All " + std::to_string(g_ManagedTacoRepos.size()) + " tracked .taco pack(s) are up to date!";
            }
        }
        g_PendingSaveConfigOnMainThread.store(true);
        g_TacoTaskInProgress.store(false);
    });
}

// Button 3 Action: Update All Files (downloads & replaces any outdated or missing .taco packs)
static void StartUpdateAllTacoFilesAsync() {
    bool expected = false;
    if (!g_TacoTaskInProgress.compare_exchange_strong(expected, true)) {
        return;
    }

    SpawnDetachedWin32Thread([]() {
        std::vector<ManagedTacoRepo> snapshot;
        {
            Win32LockGuard lock(g_TacoManagerMutex);
            snapshot = g_ManagedTacoRepos;
            g_TacoManagerStatusText = "Checking & updating all tracked .taco files...";
        }

        if (snapshot.empty()) {
            Win32LockGuard lock(g_TacoManagerMutex);
            g_TacoManagerStatusText = "No GitHub repositories tracked yet. Paste a GitHub repo link above and click Download!";
            g_TacoTaskInProgress.store(false);
            return;
        }

        std::filesystem::path markersDir = GetPrimaryMarkersDirectory();
        std::error_code ec;
        std::filesystem::create_directories(markersDir, ec);
        size_t updatedCount = 0;

        for (auto& repo : snapshot) {
            if (!repo.OwnerRepo.empty()) {
                std::string latestTag, assetName, dlUrl;
                if (QueryGithubLatestReleaseAsset(repo.OwnerRepo, latestTag, assetName, dlUrl)) {
                    repo.LatestTag       = latestTag;
                    repo.AssetName       = assetName;
                    repo.DownloadUrl     = dlUrl;
                    repo.UpdateAvailable = (repo.InstalledTag != latestTag);
                }
            }

            std::filesystem::path destPath = markersDir / repo.LocalFilename;
            bool fileExists = std::filesystem::exists(destPath, ec);
            if (!repo.UpdateAvailable && fileExists && !repo.InstalledTag.empty()) {
                repo.StatusText = "Up to date (" + repo.InstalledTag + ")";
                continue;
            }

            if (repo.DownloadUrl.empty()) continue;

            {
                Win32LockGuard lock(g_TacoManagerMutex);
                g_TacoManagerStatusText = "Updating " + repo.LocalFilename + " (" + repo.LatestTag + ")...";
            }

            std::vector<uint8_t> fileBytes;
            if (PerformHttpsGetToBuffer(repo.DownloadUrl, fileBytes) && fileBytes.size() >= 32) {
                std::ofstream ofs(destPath, std::ios::binary | std::ios::trunc);
                if (ofs) {
                    ofs.write(reinterpret_cast<const char*>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));
                    ofs.close();
                    repo.InstalledTag    = repo.LatestTag.empty() ? "latest" : repo.LatestTag;
                    repo.UpdateAvailable = false;
                    repo.StatusText      = "Updated (" + repo.InstalledTag + ", " + std::to_string(fileBytes.size() / 1024) + " KB)";
                    updatedCount++;
                }
            } else {
                repo.StatusText = "Download error";
            }
        }

        {
            Win32LockGuard lock(g_TacoManagerMutex);
            g_ManagedTacoRepos = std::move(snapshot);
            if (updatedCount > 0) {
                g_TacoManagerStatusText = "Successfully updated " + std::to_string(updatedCount) + " .taco pack(s)! Reloading...";
            } else {
                g_TacoManagerStatusText = "All tracked .taco files are already on the latest version.";
            }
        }

        g_PendingSaveConfigOnMainThread.store(true);
        if (updatedCount > 0) {
            g_PendingReloadPacksOnMainThread.store(true);
        }
        g_TacoTaskInProgress.store(false);
    });
}

static void SaveConfigToDisk() {
    std::filesystem::path cfgPath = GetConfigFilePath();
    std::ofstream ofs(cfgPath, std::ios::trunc);
    if (!ofs) return;

    ofs << "# NexusPathing Persistent Configuration File\n";
    ofs << "[Settings]\n";
    ofs << "EnableWorldTrails=" << (g_EnableWorldTrails ? 1 : 0) << "\n";
    ofs << "EnableWorldMarkers=" << (g_EnableWorldMarkers ? 1 : 0) << "\n";
    ofs << "EnableCompassOverlay=" << (g_EnableCompassOverlay ? 1 : 0) << "\n";
    ofs << "EnableWorldMapOverlay=" << (g_EnableWorldMapOverlay ? 1 : 0) << "\n";
    ofs << "EnablePlayerCutout=" << (g_EnablePlayerCutout ? 1 : 0) << "\n";
    ofs << "PlayerCutoutRadius=" << g_PlayerCutoutRadius << "\n";
    ofs << "EnableAutoTrigger=" << (g_EnableAutoTrigger ? 1 : 0) << "\n";
    ofs << "OnlyShowCurrentMap=" << (g_OnlyShowCurrentMap ? 1 : 0) << "\n";
    ofs << "TrailSubdivisionStep=" << g_TrailSubdivisionStep << "\n";
    ofs << "TrailOpacity=" << g_TrailOpacity << "\n";
    ofs << "MaxTrailDistance=" << g_MaxTrailDistance << "\n";
    ofs << "HideCompletedAchPois=" << (g_HideCompletedAchPois ? 1 : 0) << "\n";
    ofs << "Gw2ApiKey=" << g_Gw2ApiKeyBuf << "\n\n";

    ofs << "[Packs]\n";
    for (const auto& [packKey, en] : g_SavedPackStates) {
        ofs << packKey << "=" << (en ? 1 : 0) << "\n";
    }
    ofs << "\n[Categories]\n";
    for (const auto& [catPath, en] : g_SavedCategoryStates) {
        ofs << catPath << "=" << (en ? 1 : 0) << "\n";
    }
    ofs << "\n[TacoRepos]\n";
    {
        Win32LockGuard lock(g_TacoManagerMutex);
        for (size_t i = 0; i < g_ManagedTacoRepos.size(); ++i) {
            const auto& r = g_ManagedTacoRepos[i];
            ofs << "Repo" << i << "=" << r.RepoUrl << "|" << r.OwnerRepo << "|" << r.LocalFilename << "|" << r.InstalledTag << "|" << r.DownloadUrl << "\n";
        }
    }
    ofs << "\n[LuaStorage]\n";
    for (const auto& [k, v] : g_LuaStorageKV) {
        std::string cleanV = v;
        std::replace(cleanV.begin(), cleanV.end(), '\n', ' ');
        std::replace(cleanV.begin(), cleanV.end(), '\r', ' ');
        ofs << k << "=" << cleanV << "\n";
    }
}

static void LoadConfigFromDisk() {
    std::filesystem::path cfgPath = GetConfigFilePath();
    std::ifstream ifs(cfgPath);
    if (!ifs) return;

    std::string section;
    std::string line;
    std::vector<ManagedTacoRepo> loadedRepos;

    while (std::getline(ifs, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
            line.pop_back();
        }
        size_t first = 0;
        while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) first++;
        if (first > 0) line.erase(0, first);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = ToLowerAscii(line.substr(1, line.size() - 2));
            continue;
        }

        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;
        std::string key = line.substr(0, eqPos);
        std::string val = line.substr(eqPos + 1);

        if (section == "settings") {
            std::string k = ToLowerAscii(key);
            if (k == "enableworldtrails") g_EnableWorldTrails = SafeParseBool(val, true);
            else if (k == "enableworldmarkers") g_EnableWorldMarkers = SafeParseBool(val, true);
            else if (k == "enablecompassoverlay") g_EnableCompassOverlay = SafeParseBool(val, true);
            else if (k == "enableworldmapoverlay") g_EnableWorldMapOverlay = SafeParseBool(val, true);
            else if (k == "enableplayercutout") g_EnablePlayerCutout = SafeParseBool(val, true);
            else if (k == "playercutoutradius") g_PlayerCutoutRadius = std::clamp(SafeParseFloat(val, 3.5f), 0.5f, 20.0f);
            else if (k == "enableautotrigger") g_EnableAutoTrigger = SafeParseBool(val, true);
            else if (k == "onlyshowcurrentmap") g_OnlyShowCurrentMap = SafeParseBool(val, true);
            else if (k == "trailsubdivisionstep") g_TrailSubdivisionStep = std::clamp(SafeParseFloat(val, 0.65f), 0.35f, 2.5f);
            else if (k == "trailopacity") g_TrailOpacity = std::clamp(SafeParseFloat(val, 1.0f), 0.0f, 1.0f);
            else if (k == "trailtransparency") g_TrailOpacity = 1.0f - std::clamp(SafeParseFloat(val, 0.0f), 0.0f, 1.0f);
            else if (k == "maxtraildistance") g_MaxTrailDistance = std::clamp(SafeParseFloat(val, 500.0f), 50.0f, 2500.0f);
            else if (k == "hidecompletedachpois") g_HideCompletedAchPois = SafeParseBool(val, true);
            else if (k == "gw2apikey") {
                std::snprintf(g_Gw2ApiKeyBuf, sizeof(g_Gw2ApiKeyBuf), "%s", val.c_str());
            }
        } else if (section == "packs") {
            g_SavedPackStates[ToLowerAscii(key)] = SafeParseBool(val, true);
        } else if (section == "categories") {
            g_SavedCategoryStates[ToLowerAscii(key)] = SafeParseBool(val, true);
        } else if (section == "tacorepos") {
            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= val.size()) {
                auto bar = val.find('|', start);
                if (bar == std::string::npos) {
                    parts.push_back(val.substr(start));
                    break;
                }
                parts.push_back(val.substr(start, bar - start));
                start = bar + 1;
            }
            if (parts.size() >= 3 && !parts[2].empty()) {
                ManagedTacoRepo r{};
                r.RepoUrl       = parts[0];
                r.OwnerRepo     = parts[1];
                r.LocalFilename = parts[2];
                r.InstalledTag  = (parts.size() >= 4) ? parts[3] : "";
                r.LatestTag     = r.InstalledTag;
                r.DownloadUrl   = (parts.size() >= 5) ? parts[4] : "";
                r.StatusText    = r.InstalledTag.empty() ? "Installed" : ("Installed (" + r.InstalledTag + ")");
                loadedRepos.push_back(std::move(r));
            }
        } else if (section == "luastorage") {
            g_LuaStorageKV[key] = val;
        }
    }

    if (!loadedRepos.empty()) {
        Win32LockGuard lock(g_TacoManagerMutex);
        g_ManagedTacoRepos = std::move(loadedRepos);
    }
}

static void ApplySavedConfigToLoadedPacks() {
    for (auto& p : g_LoadedPacks) {
        auto it = g_SavedPackStates.find(ToLowerAscii(p.Filename));
        if (it != g_SavedPackStates.end()) {
            p.Enabled = it->second;
        }
    }
    for (auto& [catPath, node] : g_CategoryLookup) {
        if (!node) continue;
        auto it = g_SavedCategoryStates.find(catPath);
        if (it != g_SavedCategoryStates.end()) {
            node->Enabled = it->second;
        }
    }
    g_CategoryCountsCacheDirty = true;
    SyncVisibilityWithCategories();
}

static void DiscoverAllMarkerPacks() {
    g_RootCategories.clear();
    g_CategoryLookup.clear();
    g_Markers.clear();
    g_Trails.clear();
    // FIX 2: markers/trails (which hold raw NativeSRV pointers) are gone, so the texture cache can be
    // dropped safely. This frees textures of removed packs and lets updated .taco files reload their images.
    g_UploadedTextureIds.clear();
    g_D3DEngine.ClearTextureCache();
    g_LoadedPacks.clear();
    g_ScannedDirectories.clear();
    g_LuaScriptsInPack.clear();
    g_PackEntryLuaScripts.clear();
    g_PackBinaryFiles.clear();
    g_LastKnownMapId = g_Mumble ? static_cast<int32_t>(g_Mumble->Context.MapID) : -1;

    InitLuaVM();

    if (!g_API || !g_API->Paths_GetAddonDirectory) return;

    std::vector<std::filesystem::path> candidateDirs;
    if (const char* npDir = g_API->Paths_GetAddonDirectory("NexusPathing")) {
        candidateDirs.push_back(std::filesystem::path(npDir) / "markers");
        candidateDirs.push_back(std::filesystem::path(npDir));
    }
    if (const char* rootAddons = g_API->Paths_GetAddonDirectory(nullptr)) {
        candidateDirs.push_back(std::filesystem::path(rootAddons) / "markers");
        candidateDirs.push_back(std::filesystem::path(rootAddons) / "Taimi" / "markers");
        candidateDirs.push_back(std::filesystem::path(rootAddons) / "Pathing" / "markers");
    }

    std::unordered_set<std::string> visitedFiles;

    for (const auto& dir : candidateDirs) {
        std::error_code ec;
        if (dir.filename() == "markers" && !std::filesystem::exists(dir, ec)) {
            std::filesystem::create_directories(dir, ec);
        }
        if (!std::filesystem::exists(dir, ec)) continue;

        g_ScannedDirectories.push_back(dir.string());

        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;

            std::string canonicalPath = ToLowerAscii(entry.path().string());
            if (visitedFiles.count(canonicalPath)) continue;
            visitedFiles.insert(canonicalPath);

            std::string ext = ToLowerAscii(entry.path().extension().string());
            if (ext == ".taco" || ext == ".zip") {
                LoadTacoZipArchive(entry.path());
            } else if (ext == ".xml") {
                std::ifstream ifs(entry.path(), std::ios::binary | std::ios::ate);
                if (!ifs) continue;
                auto sz = ifs.tellg();
                if (sz <= 0) continue;
                std::vector<char> xmlBuf(static_cast<size_t>(sz));
                ifs.seekg(0);
                ifs.read(xmlBuf.data(), sz);

                LoadedPackSummary summary{};
                summary.Filename = entry.path().filename().string();
                summary.FullPath = entry.path().string();

                std::filesystem::path baseDir = entry.path().parent_path();
                auto fetchLooseFile = [&](const std::string& relPath, std::vector<uint8_t>& outBytes) -> bool {
                    std::filesystem::path fullPath = baseDir / relPath;
                    std::ifstream tfs(fullPath, std::ios::binary | std::ios::ate);
                    if (!tfs) return false;
                    auto tsz = tfs.tellg();
                    if (tsz <= 0) return false;
                    outBytes.resize(static_cast<size_t>(tsz));
                    tfs.seekg(0);
                    tfs.read(reinterpret_cast<char*>(outBytes.data()), tsz);
                    return true;
                };

                ParseOverlayXmlDocument(xmlBuf.data(), xmlBuf.size(), summary.Filename, fetchLooseFile, summary);
                g_LoadedPacks.push_back(std::move(summary));
            } else if (ext == ".trl") {
                std::ifstream ifs(entry.path(), std::ios::binary | std::ios::ate);
                if (!ifs) continue;
                auto sz = ifs.tellg();
                if (sz <= 8) continue;
                std::vector<uint8_t> buf(static_cast<size_t>(sz));
                ifs.seekg(0);
                ifs.read(reinterpret_cast<char*>(buf.data()), sz);

                RuntimeTrail trail{};
                trail.PackName      = entry.path().filename().string();
                trail.Guid          = entry.path().filename().string();
                trail.TrailDataPath = entry.path().filename().string();
                if (ParseTrlBinaryBuffer(buf.data(), buf.size(), trail.MapId, trail.Segments)) {
                    g_D3DEngine.PrecomputeTrailCornerRights(trail);
                    g_Trails.push_back(std::move(trail));

                    LoadedPackSummary summary{};
                    summary.Filename   = entry.path().filename().string();
                    summary.FullPath   = entry.path().string();
                    summary.TrailCount = 1;
                    g_LoadedPacks.push_back(std::move(summary));
                }
            }
        }
    }

    ApplySavedConfigToLoadedPacks();
}

static bool IsPackEnabled(const std::string& packName) {
    if (packName.empty()) return true;
    std::string low = ToLowerAscii(packName);
    for (const auto& p : g_LoadedPacks) {
        if (ToLowerAscii(p.Filename) == low) {
            return p.Enabled;
        }
    }
    auto it = g_SavedPackStates.find(low);
    if (it != g_SavedPackStates.end()) return it->second;
    return true;
}

static bool IsCategoryPathEnabled(const std::string& typePath) {
    if (typePath.empty()) return true;
    std::string norm = ToLowerAscii(typePath);
    // Check the category and every ancestor prefix along the dot-separated path!
    size_t pos = 0;
    while (pos < norm.size()) {
        auto dot = norm.find('.', pos);
        std::string prefix = (dot == std::string::npos) ? norm : norm.substr(0, dot);
        auto it = g_CategoryLookup.find(prefix);
        if (it != g_CategoryLookup.end() && it->second && !it->second->Enabled) {
            return false;
        }
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    return true;
}

static void SetCategoryChildrenEnabled(CategoryNode& node, bool enabled) {
    node.Enabled = enabled;
    g_SavedCategoryStates[ToLowerAscii(node.FullPath)] = enabled;
    for (auto& child : node.Children) {
        if (child) SetCategoryChildrenEnabled(*child, enabled);
    }
}

static void EnableCategoryAncestors(const std::string& fullPath) {
    std::string norm = ToLowerAscii(fullPath);
    size_t pos = 0;
    while (pos < norm.size()) {
        auto dot = norm.find('.', pos);
        if (dot == std::string::npos) break;
        std::string prefix = norm.substr(0, dot);
        auto it = g_CategoryLookup.find(prefix);
        if (it != g_CategoryLookup.end() && it->second) {
            it->second->Enabled = true;
            g_SavedCategoryStates[prefix] = true;
        }
        pos = dot + 1;
    }
}

static void SyncVisibilityWithCategories() {
    for (auto& t : g_Trails) {
        t.IsVisible = IsPackEnabled(t.PackName) && IsCategoryPathEnabled(t.TypePath);
    }
    for (auto& m : g_Markers) {
        m.IsVisible = IsPackEnabled(m.PackName) && IsCategoryPathEnabled(m.TypePath) && !IsMarkerHiddenByAchievement(m);
    }
}

static void EnsureCategoryCountsCacheValid(int32_t currentMapId) {
    if (!g_CategoryCountsCacheDirty && g_CategoryCountsCachedMapId == currentMapId) {
        return;
    }
    g_CategoryCountsCachedMapId = currentMapId;
    g_CategoryCountsCacheDirty  = false;
    g_CachedActiveMapTrails     = 0;
    g_CachedActiveMapPois       = 0;

    for (auto& [catPath, node] : g_CategoryLookup) {
        if (!node) continue;
        node->CachedMapTrails   = 0;
        node->CachedMapPois     = 0;
        node->CachedTotalTrails = 0;
        node->CachedTotalPois   = 0;
    }

    std::unordered_set<std::string> enabledPacks;
    for (const auto& p : g_LoadedPacks) {
        if (p.Enabled) {
            enabledPacks.insert(ToLowerAscii(p.Filename));
        }
    }
    auto isPackEnabledFast = [&](const std::string& packName) -> bool {
        if (packName.empty()) return true;
        return enabledPacks.count(ToLowerAscii(packName)) > 0;
    };

    for (const auto& t : g_Trails) {
        if (!isPackEnabledFast(t.PackName)) continue;
        bool onCurMap = (currentMapId != 0 && t.MapId == currentMapId);
        if (onCurMap) g_CachedActiveMapTrails++;

        const std::string& norm = t.TypePath;
        size_t pos = 0;
        while (pos < norm.size()) {
            auto dot = norm.find('.', pos);
            std::string prefix = (dot == std::string::npos) ? norm : norm.substr(0, dot);
            auto it = g_CategoryLookup.find(prefix);
            if (it != g_CategoryLookup.end() && it->second) {
                it->second->CachedTotalTrails++;
                if (onCurMap) it->second->CachedMapTrails++;
            }
            if (dot == std::string::npos) break;
            pos = dot + 1;
        }
    }

    for (const auto& m : g_Markers) {
        if (!isPackEnabledFast(m.PackName)) continue;
        bool onCurMap = (currentMapId != 0 && m.MapId == currentMapId);
        if (onCurMap) g_CachedActiveMapPois++;

        const std::string& norm = m.TypePath;
        size_t pos = 0;
        while (pos < norm.size()) {
            auto dot = norm.find('.', pos);
            std::string prefix = (dot == std::string::npos) ? norm : norm.substr(0, dot);
            auto it = g_CategoryLookup.find(prefix);
            if (it != g_CategoryLookup.end() && it->second) {
                it->second->CachedTotalPois++;
                if (onCurMap) it->second->CachedMapPois++;
            }
            if (dot == std::string::npos) break;
            pos = dot + 1;
        }
    }
}

static bool DoesCategoryMatchSearch(const CategoryNode& node, const std::string& queryLower) {
    if (queryLower.empty()) return true;
    if (ToLowerAscii(node.DisplayName).find(queryLower) != std::string::npos ||
        ToLowerAscii(node.FullPath).find(queryLower) != std::string::npos) {
        return true;
    }
    for (const auto& child : node.Children) {
        if (child && DoesCategoryMatchSearch(*child, queryLower)) return true;
    }
    return false;
}

static void RenderCategoryTreeImGui(CategoryNode& node, int32_t currentMapId, bool filterByMap, const std::string& queryLower = "") {
    if (!queryLower.empty() && !DoesCategoryMatchSearch(node, queryLower)) {
        return;
    }

    size_t countTrails = (filterByMap && currentMapId != 0) ? node.CachedMapTrails : node.CachedTotalTrails;
    size_t countPois   = (filterByMap && currentMapId != 0) ? node.CachedMapPois   : node.CachedTotalPois;
    if (filterByMap && currentMapId != 0 && countTrails == 0 && countPois == 0) {
        return;
    }

    if (node.IsSeparator) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.85f, 1.0f), "%s", node.DisplayName.c_str());
        return;
    }

    ImGui::PushID(node.FullPath.c_str());
    bool enabled = node.Enabled;
    if (ImGui::Checkbox("##en", &enabled)) {
        SetCategoryChildrenEnabled(node, enabled);
        if (enabled) {
            EnableCategoryAncestors(node.FullPath);
        }
        SyncVisibilityWithCategories();
        SaveConfigToDisk();
    }
    ImGui::SameLine();

    if (node.Children.empty()) {
        if (countTrails > 0 && countPois > 0) {
            ImGui::Text("%s (%zu trails, %zu POIs)", node.DisplayName.c_str(), countTrails, countPois);
        } else if (countTrails > 0) {
            ImGui::Text("%s (%zu trails)", node.DisplayName.c_str(), countTrails);
        } else if (countPois > 0) {
            ImGui::Text("%s (%zu POIs)", node.DisplayName.c_str(), countPois);
        } else {
            ImGui::Text("%s", node.DisplayName.c_str());
        }
    } else {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
        if (!queryLower.empty()) {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }

        bool open = false;
        if (countTrails > 0 && countPois > 0) {
            open = ImGui::TreeNodeEx("##tree", flags, "%s (%zu trails, %zu POIs)", node.DisplayName.c_str(), countTrails, countPois);
        } else if (countTrails > 0) {
            open = ImGui::TreeNodeEx("##tree", flags, "%s (%zu trails)", node.DisplayName.c_str(), countTrails);
        } else if (countPois > 0) {
            open = ImGui::TreeNodeEx("##tree", flags, "%s (%zu POIs)", node.DisplayName.c_str(), countPois);
        } else {
            open = ImGui::TreeNodeEx("##tree", flags, "%s", node.DisplayName.c_str());
        }
        if (open) {
            for (auto& child : node.Children) {
                if (child) RenderCategoryTreeImGui(*child, currentMapId, filterByMap, queryLower);
            }
            ImGui::TreePop();
        }
    }
    ImGui::PopID();
}

static void CopyStringToClipboard(const std::string& utf8Text) {
    if (utf8Text.empty() || !OpenClipboard(nullptr)) return;
    EmptyClipboard();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wlen) * sizeof(wchar_t));
        if (hMem) {
            auto* wstr = static_cast<wchar_t*>(GlobalLock(hMem));
            if (wstr) {
                MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, wstr, wlen);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
    }
    CloseClipboard();
}

static std::string ReadStringFromClipboard() {
    if (!OpenClipboard(nullptr)) return "";
    std::string out;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const auto* wstr = static_cast<const wchar_t*>(GlobalLock(hData));
        if (wstr) {
            int u8Len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
            if (u8Len > 1) {
                out.resize(static_cast<size_t>(u8Len - 1));
                WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), u8Len, nullptr, nullptr);
            }
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return out;
}

static void OnInteractKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease || !aIdentifier) return;

    if (std::strcmp(aIdentifier, "KB_NEXUSPATHING_TOGGLE_UI") == 0) {
        g_ShowMainWindow = !g_ShowMainWindow;
        return;
    }

    if (!g_Mumble || !g_API) return;
    if (std::strcmp(aIdentifier, "KB_NEXUSPATHING_INTERACT") == 0) {
        Vec3 avatar{ g_Mumble->AvatarPosition.X, g_Mumble->AvatarPosition.Y, g_Mumble->AvatarPosition.Z };
        int32_t currentMapId = static_cast<int32_t>(g_Mumble->Context.MapID);
        size_t markerCount = g_Markers.size();

        for (size_t i = 0; i < markerCount; ++i) {
            if (i >= g_Markers.size()) break;
            auto& m = g_Markers[i];
            if (!m.IsVisible || !IsPackEnabled(m.PackName) || !IsCategoryPathEnabled(m.TypePath)) continue;
            if (m.MapId != 0 && m.MapId != currentMapId) continue;
            if ((m.Position - avatar).Length() <= m.TriggerRange) {
                TriggerMarkerInteraction(i, false);
            }
        }
    }
}

// Pack:CreateMarker() appends to g_Markers. Re-running pack.lua on every map change (after InitLuaVM wiped the
// VM) therefore ADDED another full copy of every script-created marker each time: unbounded growth of g_Markers,
// duplicated rendering/trigger work and an ever-longer linear scan in Lua_World_MarkerByGuid.
// The VM is rebuilt right before this runs, so no Lua table can still hold an index into g_Markers.
static void PurgeLuaCreatedMarkers() {
    const size_t before = g_Markers.size();
    g_Markers.erase(std::remove_if(g_Markers.begin(), g_Markers.end(),
                                   [](const RuntimeMarker& m) { return m.FromLua; }),
                    g_Markers.end());
    if (g_Markers.size() != before) g_CategoryCountsCacheDirty = true;
}

static void ReExecuteAllPackScriptsForCurrentMap() {
    if (g_PackEntryLuaScripts.empty()) return;
    InitLuaVM();
    if (!g_Lua) return;
    PurgeLuaCreatedMarkers();
    for (const auto& [packName, packLua] : g_PackEntryLuaScripts) {
        if (!IsPackEnabled(packName)) continue;
        g_CurrentPackName = packName;
        if (LuaLoadText(g_Lua, packLua, packName + ":pack.lua") == LUA_OK) {
            if (LuaSafeCall(g_Lua, 0, 0) != LUA_OK) {
                PushLuaLog("pack.lua runtime error: " + LuaErrStr(g_Lua), LOGL_WARNING);
                lua_pop(g_Lua, 1);
            }
        } else {
            PushLuaLog("pack.lua load error: " + LuaErrStr(g_Lua), LOGL_WARNING);
            lua_pop(g_Lua, 1);
        }
    }
    for (auto& m : g_Markers) {
        m.ScriptOnceExecuted  = false;
        m.ScriptAutoTriggered = false;
        m.IsFocused           = false;
    }
    ApplySavedConfigToLoadedPacks();
}

static void RegisterQuickAccessTextures() {
    if (!g_API) return;
    auto makeIconPng = [](uint8_t rCol, uint8_t gCol, uint8_t bCol) -> std::vector<uint8_t> {
        constexpr int W = 32, H = 32;
        uint8_t rgba[W * H * 4] = {};
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float dx = x - 15.5f;
                float dy = y - 15.5f;
                float dist = std::sqrt(dx * dx + dy * dy);
                float manhattan = std::fabs(dx) + std::fabs(dy * 0.85f);
                uint8_t a = 0, r = rCol, g = gCol, b = bCol;
                if (dist >= 12.0f && dist <= 14.5f) {
                    a = 235;
                } else if (manhattan <= 8.5f) {
                    a = 255;
                    if (manhattan <= 4.0f) {
                        r = 255; g = 255; b = 255;
                    }
                }
                int idx = (y * W + x) * 4;
                rgba[idx + 0] = r;
                rgba[idx + 1] = g;
                rgba[idx + 2] = b;
                rgba[idx + 3] = a;
            }
        }
        size_t pngLen = 0;
        void* pPng = tdefl_write_image_to_png_file_in_memory_ex(rgba, W, H, 4, &pngLen, MZ_DEFAULT_LEVEL, MZ_FALSE);
        std::vector<uint8_t> out;
        if (pPng && pngLen > 0) {
            out.assign(static_cast<uint8_t*>(pPng), static_cast<uint8_t*>(pPng) + pngLen);
            mz_free(pPng);
        }
        return out;
    };

    std::vector<uint8_t> iconNorm  = makeIconPng(56, 189, 248);  // Sky cyan
    std::vector<uint8_t> iconHover = makeIconPng(125, 211, 252); // Bright cyan hover
    if (!iconNorm.empty()) {
        if (g_API->Textures_GetOrCreateFromMemory) {
            g_API->Textures_GetOrCreateFromMemory("TEX_PATHING_ICON", iconNorm.data(), iconNorm.size());
            g_API->Textures_GetOrCreateFromMemory("TEX_PATHING_ICON_HOVER", iconHover.data(), iconHover.size());
        } else if (g_API->Textures_LoadFromMemory) {
            g_API->Textures_LoadFromMemory("TEX_PATHING_ICON", iconNorm.data(), iconNorm.size(), nullptr);
            g_API->Textures_LoadFromMemory("TEX_PATHING_ICON_HOVER", iconHover.data(), iconHover.size(), nullptr);
        }
    }
}

static void OnPreRender() {
    if (g_PendingSaveConfigOnMainThread.exchange(false)) {
        SaveConfigToDisk();
    }
    if (g_PendingReloadPacksOnMainThread.exchange(false)) {
        DiscoverAllMarkerPacks();
    }
    if (g_Gw2ApiNeedsResync.exchange(false)) {
        SyncVisibilityWithCategories();
    }

    if (!g_Mumble) return;
    static uint64_t s_StartTick = GetTickCount64();
    static uint64_t s_LastTick  = s_StartTick;
    uint64_t nowTick = GetTickCount64();
    double totalSec = (nowTick - s_StartTick) * 0.001;
    double dtSec    = std::clamp((nowTick - s_LastTick) * 0.001, 0.001, 0.1);
    s_LastTick      = nowTick;

    int32_t currentMapId = static_cast<int32_t>(g_Mumble->Context.MapID);
    if (currentMapId != 0 && currentMapId != g_LastKnownMapId) {
        g_LastKnownMapId = currentMapId;
        ReExecuteAllPackScriptsForCurrentMap();
    }

    UpdateLuaMumbleGlobals(totalSec, dtSec);

    if (!g_TriggeredTimestamps.empty()) {
        auto nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        for (auto& m : g_Markers) {
            if (m.Behavior == 4 && m.ResetLengthSec > 0 && !m.IsVisible) {
                auto it = g_TriggeredTimestamps.find(m.Guid);
                if (it != g_TriggeredTimestamps.end() && (nowEpoch - it->second) >= m.ResetLengthSec) {
                    m.IsVisible = IsPackEnabled(m.PackName) && IsCategoryPathEnabled(m.TypePath) && !IsMarkerHiddenByAchievement(m);
                }
            }
        }
    }
}

static void RenderLuaMenuNodeImGui(LuaMenuNode& node) {
    if (!g_Lua) return;
    ImGui::PushID(&node);

    if (!node.Children.empty()) {
        if (ImGui::TreeNodeEx(node.Name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
            if (!node.Tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", node.Tooltip.c_str());
            }
            for (auto& child : node.Children) {
                if (child) RenderLuaMenuNodeImGui(*child);
            }
            ImGui::TreePop();
        } else if (!node.Tooltip.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", node.Tooltip.c_str());
        }
        ImGui::PopID();
        return;
    }

    bool isChecked = false;
    if (node.LuaTableRef != LUA_NOREF) {
        lua_rawgeti(g_Lua, LUA_REGISTRYINDEX, node.LuaTableRef);
        if (lua_istable(g_Lua, -1)) {
            lua_getfield(g_Lua, -1, "Checked");
            isChecked = lua_toboolean(g_Lua, -1) != 0;
            lua_pop(g_Lua, 1);
        }
        lua_pop(g_Lua, 1);
    }

    bool clicked = false;
    if (node.CanCheck) {
        if (ImGui::Checkbox(node.Name.c_str(), &isChecked)) {
            clicked = true;
        }
    } else {
        if (ImGui::Button(node.Name.c_str())) {
            clicked = true;
        }
    }

    if (!node.Tooltip.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", node.Tooltip.c_str());
    }

    if (clicked && node.CallbackRef != LUA_NOREF && node.LuaTableRef != LUA_NOREF) {
        // Reset auto-triggered consumed flags in case the user clicked "Reset Trail Highlighting" in Teh's Trails menu
        for (auto& m : g_Markers) {
            m.ScriptAutoTriggered = false;
        }
        lua_rawgeti(g_Lua, LUA_REGISTRYINDEX, node.CallbackRef);
        if (lua_isfunction(g_Lua, -1)) {
            lua_rawgeti(g_Lua, LUA_REGISTRYINDEX, node.LuaTableRef);
            if (LuaSafeCall(g_Lua, 1, 0) != LUA_OK) {
                PushLuaLog("Menu callback error (" + node.Name + "): " + LuaErrStr(g_Lua), LOGL_WARNING);
                lua_pop(g_Lua, 1);
            }
            SaveConfigToDisk();
        } else {
            lua_pop(g_Lua, 1);
        }
    }

    ImGui::PopID();
}

static void RenderOptionsContent() {
    // ------------------------------------------------------------------------
    // TOP BAR: Quick Access & Status Header
    // ------------------------------------------------------------------------
    ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "NexusPathing v3.1.0 (High-Performance Pathing for Guild Wars 2)");
    ImGui::SameLine();
    ImGui::TextDisabled("| Shortcut: ALT+SHIFT+P");

    int32_t curMap = g_Mumble ? static_cast<int32_t>(g_Mumble->Context.MapID) : 0;
    EnsureCategoryCountsCacheValid(curMap);

    // Quick toggles row for immediate 1-click accessibility
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

    auto renderQuickToggle = [](const char* label, bool* val) -> bool {
        bool active = *val;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.44f, 0.32f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.55f, 0.40f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.25f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.34f, 0.40f, 1.0f));
        }
        std::string btnText = std::string(label) + (active ? ": ON" : ": OFF");
        bool clicked = ImGui::Button(btnText.c_str());
        if (clicked) {
            *val = !*val;
        }
        ImGui::PopStyleColor(2);
        return clicked;
    };

    bool quickChanged = false;
    quickChanged |= renderQuickToggle("Trails", &g_EnableWorldTrails);
    ImGui::SameLine();
    quickChanged |= renderQuickToggle("POIs", &g_EnableWorldMarkers);
    ImGui::SameLine();
    quickChanged |= renderQuickToggle("Compass", &g_EnableCompassOverlay);
    ImGui::SameLine();
    quickChanged |= renderQuickToggle("Map [M]", &g_EnableWorldMapOverlay);
    ImGui::SameLine();
    quickChanged |= renderQuickToggle("Auto-Trigger", &g_EnableAutoTrigger);

    ImGui::SameLine();
    if (ImGui::Button(g_EnableWorldTrails || g_EnableWorldMarkers ? "Mute All" : "Unmute All")) {
        if (g_EnableWorldTrails || g_EnableWorldMarkers) {
            g_EnableWorldTrails = false;
            g_EnableWorldMarkers = false;
        } else {
            g_EnableWorldTrails = true;
            g_EnableWorldMarkers = true;
        }
        quickChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Quickly hide or restore all 3D trails and POIs (e.g. during boss fights, cutscenes, or jumping puzzles).");
    }

    ImGui::PopStyleVar(2);

    if (g_Mumble) {
        ImGui::TextDisabled("Current Map: %d | On-Map Content: %zu Trails, %zu POIs", curMap, g_CachedActiveMapTrails, g_CachedActiveMapPois);
    }

    if (quickChanged) {
        SaveConfigToDisk();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ------------------------------------------------------------------------
    // HIGH-PERFORMANCE TAB BAR (Eliminates FPS drop by lazy-rendering tabs)
    // ------------------------------------------------------------------------
    if (ImGui::BeginTabBar("NexusPathingTabs", ImGuiTabBarFlags_None)) {
        // ====================================================================
        // TAB 1: VISUALS & TRAILS
        // ====================================================================
        if (ImGui::BeginTabItem("Visuals & Trails")) {
            ImGui::Spacing();
            bool visualsChanged = false;

            // Trail Opacity & Transparency Slider
            ImGui::TextColored(ImVec4(0.38f, 0.82f, 0.98f, 1.0f), "Trail Opacity & Transparency");
            float trailOpacityPercent = g_TrailOpacity * 100.0f;
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::SliderFloat("##trail_opacity", &trailOpacityPercent, 0.0f, 100.0f, "Opacity: %.0f%%")) {
                g_TrailOpacity = std::clamp(trailOpacityPercent / 100.0f, 0.0f, 1.0f);
                visualsChanged = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Transparency: %.0f%%)", (1.0f - g_TrailOpacity) * 100.0f);

            // Quick Opacity Presets for fast in-game switching
            ImGui::Text("Quick Presets:");
            ImGui::SameLine();
            if (ImGui::Button("100% Solid")) { g_TrailOpacity = 1.0f; visualsChanged = true; }
            ImGui::SameLine();
            if (ImGui::Button("80%")) { g_TrailOpacity = 0.80f; visualsChanged = true; }
            ImGui::SameLine();
            if (ImGui::Button("60%")) { g_TrailOpacity = 0.60f; visualsChanged = true; }
            ImGui::SameLine();
            if (ImGui::Button("40%")) { g_TrailOpacity = 0.40f; visualsChanged = true; }
            ImGui::SameLine();
            if (ImGui::Button("20%")) { g_TrailOpacity = 0.20f; visualsChanged = true; }
            ImGui::SameLine();
            if (ImGui::Button("Ghost 10%")) { g_TrailOpacity = 0.10f; visualsChanged = true; }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Hardware Optimization & Culling Settings
            ImGui::TextColored(ImVec4(0.38f, 0.82f, 0.98f, 1.0f), "Hardware Performance & Culling");

            ImGui::SetNextItemWidth(240.0f);
            visualsChanged |= ImGui::SliderFloat("Max Trail Render Distance", &g_MaxTrailDistance, 50.0f, 1500.0f, "%.0f meters");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Discards trails beyond this distance from the camera, saving significant CPU and GPU cycles on large maps.");
            }

            ImGui::SetNextItemWidth(240.0f);
            visualsChanged |= ImGui::SliderFloat("Trail LOD Subdivision Step", &g_TrailSubdivisionStep, 0.35f, 2.0f, "%.2f m");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Subdivision interval for curved 3D trails. Higher values (0.80m - 1.50m) yield higher FPS with minimal visual change.");
            }

            visualsChanged |= ImGui::Checkbox("Player Spherical Cutout Fade", &g_EnablePlayerCutout);
            if (g_EnablePlayerCutout) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                visualsChanged |= ImGui::SliderFloat("##cutout", &g_PlayerCutoutRadius, 0.5f, 20.0f, "%.1f m");
            }

            visualsChanged |= ImGui::Checkbox("Show Only Trails & Categories in Current Map", &g_OnlyShowCurrentMap);

            if (visualsChanged) {
                SaveConfigToDisk();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Reload All Marker Packs")) {
                DiscoverAllMarkerPacks();
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Configuration")) {
                SaveConfigToDisk();
            }
            ImGui::SameLine();
            if (ImGui::Button("Open Markers Folder")) {
                std::string folder = GetPrimaryMarkersDirectory().string();
                ShellExecuteA(nullptr, "open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }

            if (!g_ConfigFilePathStr.empty()) {
                ImGui::TextDisabled("Config File: %s", g_ConfigFilePathStr.c_str());
            }

            ImGui::EndTabItem();
        }

        // ====================================================================
        // TAB 2: CATEGORIES & MARKERS (With Instant Search & Fast Filtering)
        // ====================================================================
        if (ImGui::BeginTabItem("Categories & Markers")) {
            ImGui::Spacing();
            ImGui::SetNextItemWidth(340.0f);
            ImGui::InputTextWithHint("##catsearch", "Search categories (e.g. chest, mastery, rift)...", g_CategorySearchBuf, sizeof(g_CategorySearchBuf));
            ImGui::SameLine();
            if (ImGui::Button("Clear Filter")) {
                g_CategorySearchBuf[0] = '\0';
            }

            ImGui::SameLine();
            if (ImGui::Checkbox("Only Current Map", &g_OnlyShowCurrentMap)) {
                SaveConfigToDisk();
            }

            // Quick bulk enable/disable actions
            if (ImGui::Button("Enable All Categories")) {
                for (auto& root : g_RootCategories) {
                    if (root) SetCategoryChildrenEnabled(*root, true);
                }
                SyncVisibilityWithCategories();
                SaveConfigToDisk();
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable All Categories")) {
                for (auto& root : g_RootCategories) {
                    if (root) SetCategoryChildrenEnabled(*root, false);
                }
                SyncVisibilityWithCategories();
                SaveConfigToDisk();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            std::string q = ToLowerAscii(g_CategorySearchBuf);
            if (!g_RootCategories.empty()) {
                for (auto& cat : g_RootCategories) {
                    if (cat) RenderCategoryTreeImGui(*cat, curMap, g_OnlyShowCurrentMap, q);
                }
            } else {
                ImGui::TextDisabled("No marker categories found. Add .taco/.zip packs into your markers folder.");
            }

            ImGui::EndTabItem();
        }

        // ====================================================================
        // TAB 3: TACO FILES MANAGER
        // ====================================================================
        if (ImGui::BeginTabItem("TacO Files Manager")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.38f, 0.82f, 0.98f, 1.0f), "GitHub .taco Repository Downloader & Auto-Updater");
            ImGui::SetNextItemWidth(450.0f);
            ImGui::InputTextWithHint("##ghrepo", "https://github.com/xrandox/TehsTrails or owner/repo", g_GithubRepoInputBuf, sizeof(g_GithubRepoInputBuf));

            bool busyTaco = g_TacoTaskInProgress.load();

            // Button 1: Paste GitHub Link & Download
            if (ImGui::Button("1. Paste GitHub Link & Download") && !busyTaco) {
                std::string clip = ReadStringFromClipboard();
                if (!clip.empty() && (clip.find("github.com") != std::string::npos || clip.find('/') != std::string::npos)) {
                    std::snprintf(g_GithubRepoInputBuf, sizeof(g_GithubRepoInputBuf), "%s", clip.c_str());
                }
                StartDownloadFromGithubUrlAsync(g_GithubRepoInputBuf);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Automatically reads a GitHub repository URL from clipboard/input and downloads its latest .taco/.zip release asset!");
            }

            ImGui::SameLine();
            // Button 2: Check Updates
            if (ImGui::Button("2. Check Updates") && !busyTaco) {
                StartCheckTacoUpdatesAsync();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Queries GitHub Releases API for all tracked repositories to detect newer releases.");
            }

            ImGui::SameLine();
            // Button 3: Update All Files
            if (ImGui::Button("3. Update All Files") && !busyTaco) {
                StartUpdateAllTacoFilesAsync();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Downloads and updates all outdated .taco files in your markers directory and hot-reloads them.");
            }

            std::string managerStatus;
            std::vector<ManagedTacoRepo> reposSnapshot;
            {
                Win32LockGuard lock(g_TacoManagerMutex);
                managerStatus = g_TacoManagerStatusText;
                reposSnapshot = g_ManagedTacoRepos;
            }
            ImGui::TextColored(ImVec4(0.38f, 0.82f, 0.98f, 1.0f), "%s", managerStatus.c_str());

            if (!reposSnapshot.empty()) {
                ImGui::Spacing();
                ImGui::Text("Tracked Marker Repositories (%zu):", reposSnapshot.size());
                for (const auto& r : reposSnapshot) {
                    ImVec4 statusCol = r.UpdateAvailable ? ImVec4(1.0f, 0.78f, 0.28f, 1.0f) : ImVec4(0.65f, 0.85f, 0.75f, 1.0f);
                    ImGui::BulletText("%s [%s]", r.LocalFilename.c_str(), r.OwnerRepo.empty() ? r.RepoUrl.c_str() : r.OwnerRepo.c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(statusCol, "- %s", r.StatusText.c_str());
                }
            }

            ImGui::EndTabItem();
        }

        // ====================================================================
        // TAB 4: GW2 API KEY
        // ====================================================================
        if (ImGui::BeginTabItem("GW2 API Key")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.38f, 0.82f, 0.98f, 1.0f), "Guild Wars 2 API Key (Account Achievement Sync)");
            ImGui::SetNextItemWidth(340.0f);
            ImGuiInputTextFlags keyFlags = g_ShowGw2ApiKeyPlain ? 0 : ImGuiInputTextFlags_Password;
            if (ImGui::InputTextWithHint("##gw2apikey", "Paste GW2 API Key (XXXXXXXX-XXXX-...)", g_Gw2ApiKeyBuf, sizeof(g_Gw2ApiKeyBuf), keyFlags)) {
                SaveConfigToDisk();
            }
            ImGui::SameLine();
            if (ImGui::Button("Paste Key")) {
                std::string clip = ReadStringFromClipboard();
                if (!clip.empty()) {
                    std::snprintf(g_Gw2ApiKeyBuf, sizeof(g_Gw2ApiKeyBuf), "%s", clip.c_str());
                    SaveConfigToDisk();
                    SyncGw2ApiAchievementsAsync(g_Gw2ApiKeyBuf);
                }
            }
            ImGui::SameLine();
            bool busyApi = g_Gw2ApiFetchInProgress.load();
            if (ImGui::Button(busyApi ? "Syncing..." : "Sync Achievements") && !busyApi) {
                SaveConfigToDisk();
                SyncGw2ApiAchievementsAsync(g_Gw2ApiKeyBuf);
            }
            ImGui::SameLine();
            ImGui::Checkbox("Show Key", &g_ShowGw2ApiKeyPlain);

            if (ImGui::Checkbox("Auto-Hide Completed Achievement POIs (achievementId & achievementBit)", &g_HideCompletedAchPois)) {
                SyncVisibilityWithCategories();
                SaveConfigToDisk();
            }

            std::string apiStatus;
            {
                Win32LockGuard lock(g_Gw2ApiMutex);
                apiStatus = g_Gw2ApiStatusText;
            }
            ImGui::TextColored(ImVec4(0.55f, 0.88f, 0.68f, 1.0f), "%s", apiStatus.c_str());

            ImGui::EndTabItem();
        }

        // ====================================================================
        // TAB 5: LOADED PACKS
        // ====================================================================
        if (ImGui::BeginTabItem("Loaded Packs")) {
            ImGui::Spacing();
            ImGui::Text("Installed Marker Packs (%zu loaded):", g_LoadedPacks.size());
            for (auto& p : g_LoadedPacks) {
                ImGui::PushID(p.Filename.c_str());
                if (ImGui::Checkbox("##pack_en", &p.Enabled)) {
                    g_SavedPackStates[ToLowerAscii(p.Filename)] = p.Enabled;
                    g_CategoryCountsCacheDirty = true;
                    SyncVisibilityWithCategories();
                    SaveConfigToDisk();
                }
                ImGui::SameLine();
                ImGui::Text("%s (%zu XMLs, %zu Trails, %zu POIs, %zu Lua)",
                            p.Filename.c_str(), p.XmlCount, p.TrailCount, p.MarkerCount, p.LuaCount);
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }

        // ====================================================================
        // TAB 6: PACK SCRIPTS (LUA / TEH'S TRAILS)
        // ====================================================================
        if (!g_RootLuaMenus.empty() && ImGui::BeginTabItem("Pack Scripts (Lua)")) {
            ImGui::Spacing();
            for (auto& menuNode : g_RootLuaMenus) {
                if (menuNode) RenderLuaMenuNodeImGui(*menuNode);
            }
            ImGui::EndTabItem();
        }

        // ====================================================================
        // TAB 7: CONSOLE
        // ====================================================================
        if (!g_LuaConsoleLogs.empty() && ImGui::BeginTabItem("Console")) {
            ImGui::Spacing();
            for (const auto& line : g_LuaConsoleLogs) {
                ImGui::TextWrapped("%s", line.c_str());
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

static void OnRender() {
    if (g_Mumble && (!g_NexusLink || g_NexusLink->IsGameplay)) {
        float fov = (g_MumbleIdentity && g_MumbleIdentity->FOV > 0.1f) ? g_MumbleIdentity->FOV : 0.872665f;
        float uiScale = (g_NexusLink && g_NexusLink->Scaling > 0.2f) ? g_NexusLink->Scaling : 1.0f;

        if (!g_Mumble->Context.IsMapOpen) {
            g_D3DEngine.RenderWorld(
                *g_Mumble,
                fov,
                g_Trails,
                g_Markers,
                g_EnableWorldTrails,
                g_EnableWorldMarkers,
                g_EnablePlayerCutout ? g_PlayerCutoutRadius : 0.0f
            );
        }

        // Render 2D Minimap (Compass) or Fullscreen World Map [M]
        g_D3DEngine.RenderCompassAndMap(
            *g_Mumble,
            g_MumbleIdentity,
            uiScale,
            g_Trails,
            g_Markers,
            g_EnableCompassOverlay,
            g_EnableWorldMapOverlay
        );
    }

    if (g_ShowMainWindow) {
        ImGui::SetNextWindowSize(ImVec2(620, 640), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("NexusPathing // Blish HUD Pathing", &g_ShowMainWindow)) {
            RenderOptionsContent();
        }
        ImGui::End();
    }
}

static void OnOptionsRender() {
    RenderOptionsContent();
}

static void AddonLoad(AddonAPI_t* aApi) {
    g_API = aApi;
    if (!g_API) return;

    if (g_API->ImguiContext) {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(g_API->ImguiContext));
        ImGui::SetAllocatorFunctions(
            reinterpret_cast<void* (*)(size_t, void*)>(g_API->ImguiMalloc),
            reinterpret_cast<void (*)(void*, void*)>(g_API->ImguiFree)
        );
    }

    if (g_API->Log) {
        g_API->Log(LOGL_INFO, "NexusPathing", "Loading NexusPathing v3.0.1 (Auto-Trigger, GW2 API Key & GitHub TacO Manager)...");
    }

    if (g_API->DataLink_Get) {
        g_NexusLink      = static_cast<NexusLinkData_t*>(g_API->DataLink_Get(DL_NEXUS_LINK));
        g_Mumble         = static_cast<Mumble::Data*>(g_API->DataLink_Get(DL_MUMBLE_LINK));
        g_MumbleIdentity = static_cast<Mumble::Identity*>(g_API->DataLink_Get(DL_MUMBLE_LINK_IDENTITY));
    }

    if (g_API->SwapChain) {
        g_D3DEngine.Initialize(static_cast<IDXGISwapChain*>(g_API->SwapChain), g_API);
    }

    LoadConfigFromDisk();
    RegisterQuickAccessTextures();
    DiscoverAllMarkerPacks();

    if (g_Gw2ApiKeyBuf[0] != '\0') {
        SyncGw2ApiAchievementsAsync(g_Gw2ApiKeyBuf);
    }

    if (g_API->InputBinds_RegisterWithString) {
        g_API->InputBinds_RegisterWithString("KB_NEXUSPATHING_INTERACT", OnInteractKeybind, "F");
        g_API->InputBinds_RegisterWithString("KB_NEXUSPATHING_TOGGLE_UI", OnInteractKeybind, "ALT+SHIFT+P");
    }

    if (g_API->QuickAccess_Add) {
        g_API->QuickAccess_Add("QA_NEXUSPATHING", "TEX_PATHING_ICON", "TEX_PATHING_ICON_HOVER",
                               "KB_NEXUSPATHING_TOGGLE_UI", "NexusPathing (Blish HUD Pathing)");
    }

    if (g_API->GUI_RegisterCloseOnEscape) {
        g_API->GUI_RegisterCloseOnEscape("NexusPathing // Blish HUD Pathing", &g_ShowMainWindow);
    }

    if (g_API->GUI_Register) {
        g_API->GUI_Register(RT_PreRender, OnPreRender);
        g_API->GUI_Register(RT_Render, OnRender);
        g_API->GUI_Register(RT_OptionsRender, OnOptionsRender);
    }
}

static void AddonUnload() {
    if (!g_API) return;

    SaveConfigToDisk();

    // FIX 4: stop new worker threads and wait (bounded) for in-flight ones before tearing anything down.
    g_ShuttingDown.store(true);
    for (int waitedMs = 0; g_ActiveWorkerThreads.load() > 0 && waitedMs < 8000; waitedMs += 10) {
        Sleep(10);
    }

    if (g_API->GUI_Deregister) {
        g_API->GUI_Deregister(OnPreRender);
        g_API->GUI_Deregister(OnRender);
        g_API->GUI_Deregister(OnOptionsRender);
    }

    if (g_API->GUI_DeregisterCloseOnEscape) {
        g_API->GUI_DeregisterCloseOnEscape("NexusPathing // Blish HUD Pathing");
    }

    if (g_API->QuickAccess_Remove) {
        g_API->QuickAccess_Remove("QA_NEXUSPATHING");
    }

    if (g_API->InputBinds_Deregister) {
        g_API->InputBinds_Deregister("KB_NEXUSPATHING_INTERACT");
        g_API->InputBinds_Deregister("KB_NEXUSPATHING_TOGGLE_UI");
    }

    if (g_Lua) {
        ClearLuaMenus(g_Lua, g_RootLuaMenus);
        lua_close(g_Lua);
        g_Lua = nullptr;
    } else {
        g_RootLuaMenus.clear();
    }

    // FIX 3: release all remaining global state (swap-with-empty also returns vector capacity).
    std::vector<RuntimeTrail>().swap(g_Trails);
    std::vector<RuntimeMarker>().swap(g_Markers);   // drops raw NativeSRV pointers before the cache is cleared
    g_RootCategories.clear();
    g_CategoryLookup.clear();
    std::vector<LoadedPackSummary>().swap(g_LoadedPacks);
    g_ScannedDirectories.clear();
    g_TriggeredTimestamps.clear();
    g_UploadedTextureIds.clear();
    g_LuaTickRefs.clear();
    g_LuaScriptsInPack.clear();
    g_PackEntryLuaScripts.clear();
    g_PackBinaryFiles.clear();
    g_LuaConsoleLogs.clear();
    {
        Win32LockGuard lock(g_Gw2ApiMutex);
        g_CompletedAchievements.clear();
        g_AchievementBits.clear();
    }
    g_D3DEngine.Shutdown();   // also clears the native texture cache (FIX 1)
    g_ShuttingDown.store(false);
}

} // namespace NexusPathing

// ============================================================================
// 9. EXPORTED RAIDCORE NEXUS ENTRY POINT (GetAddonDef)
// ============================================================================
static AddonDefinition_t s_AddonDef = {};
static HMODULE s_hSelf = nullptr;

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    s_AddonDef.Signature   = static_cast<uint32_t>(-0x42485054); // 'BHPT'
    s_AddonDef.APIVersion  = NEXUS_API_VERSION;
    s_AddonDef.Name        = "NexusPathing";
    s_AddonDef.Version     = { 3, 0, 1, 0 };
    s_AddonDef.Author      = "NexusPathing Contributors (Blish HUD Pathing Port)";
    s_AddonDef.Description = "Native C++20/D3D11 Raidcore Nexus port of Blish HUD Pathing (Hardware Optimized, GW2 API Key & GitHub TacO Manager).";
    s_AddonDef.Load        = NexusPathing::AddonLoad;
    s_AddonDef.Unload      = NexusPathing::AddonUnload;
    s_AddonDef.Flags       = AF_None;
    s_AddonDef.Provider    = UP_GitHub;
    s_AddonDef.UpdateLink  = "https://github.com/NexusPathing/NexusPathing";
    return &s_AddonDef;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        s_hSelf = hModule;
    }
    return TRUE;
}
