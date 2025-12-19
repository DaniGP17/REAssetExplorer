#include "Renderer/Viewer.h"

#include <algorithm>
#include <cstring>
#include <d3dcompiler.h>
#include <string>

#include "D3D12Utils.h"
#include "Renderer/RenderMath.h"

using Microsoft::WRL::ComPtr;

namespace {

struct IconConstants {
    float viewProj[16];
    float viewport[2];
    float maxPixels;
    float nearZ;
    float farZ;
    float selectedOccluded;
    float pixelsPerMeter;  // at 1 m of depth
    float worldSize;
    float fadeBegin;
    float fadeEnd;
    float margin;
    float pad;
};

constexpr float SELECTED_OCCLUDED_ALPHA = 0.3f;

// Icons are signed distance shapes in [-1, 1]: a bulb, a spotlight shade and a sun.
const char* ICON_SOURCE = R"(
cbuffer Constants : register(b0) {
    row_major float4x4 viewProj;
    float2 viewport;
    float maxPixels;
    float nearZ;
    float farZ;
    float selectedOccluded;
    float pixelsPerMeter;
    float worldSize;
    float fadeBegin;
    float fadeEnd;
    float margin;
};
Texture2D<float> sceneDepth : register(t0);

struct VSIn {
    float3 position : POSITION;
    uint kind : KIND;
    float4 color : COLOR;
    uint flags : FLAGS;
    uint vertexId : SV_VertexID;
};

struct VSOut {
    float4 position : SV_Position;
    float2 p : TEXCOORD0;
    float depth : TEXCOORD1;
    float fade : TEXCOORD2;
    float4 color : COLOR;
    nointerpolation uint kind : KIND;
    nointerpolation uint flags : FLAGS;
};

VSOut VSMain(VSIn i) {
    const float2 corners[6] = { float2(-1, -1), float2(1, -1), float2(-1, 1),
                                float2(-1, 1), float2(1, -1), float2(1, 1) };
    float2 c = corners[i.vertexId];
    float4 clip = mul(float4(i.position, 1), viewProj);
    VSOut o;
    o.depth = clip.w;
    float pixels = min(maxPixels, worldSize * pixelsPerMeter / max(clip.w, 1e-4));
    o.fade = saturate((pixels - fadeEnd) / (fadeBegin - fadeEnd));
    if ((i.flags & 1) != 0) {
        pixels = max(pixels, fadeBegin);
        o.fade = 1;
    }
    if (clip.w <= nearZ || o.fade <= 0) {
        clip = float4(0, 0, -1, 1);
    } else {
        clip.xy += c * pixels / viewport * clip.w;
        clip.z = clamp(clip.z, 0, clip.w * 0.9999);
    }
    o.position = clip;
    o.p = c;
    o.color = i.color;
    o.kind = i.kind;
    o.flags = i.flags;
    return o;
}

float LinearDepth(float z) {
    return nearZ * farZ / (farZ - z * (farZ - nearZ));
}

float Fill(float d, float aa) {
    return saturate(0.5 - d / aa);
}

float Circle(float2 p, float2 c, float r) {
    return length(p - c) - r;
}

float Box(float2 p, float2 c, float2 b) {
    float2 d = abs(p - c) - b;
    return length(max(d, 0)) + min(max(d.x, d.y), 0);
}

float Segment(float2 p, float2 a, float2 b, float r) {
    float2 pa = p - a;
    float2 ba = b - a;
    float h = saturate(dot(pa, ba) / dot(ba, ba));
    return length(pa - ba * h) - r;
}

// r1 the half width at -he, r2 at +he.
float Trapezoid(float2 p, float r1, float r2, float he) {
    float2 k1 = float2(r2, he);
    float2 k2 = float2(r2 - r1, 2 * he);
    p.x = abs(p.x);
    float2 ca = float2(p.x - min(p.x, p.y < 0 ? r1 : r2), abs(p.y) - he);
    float2 cb = p - k1 + k2 * saturate(dot(k1 - p, k2) / dot(k2, k2));
    float s = cb.x < 0 && ca.y < 0 ? -1 : 1;
    return s * sqrt(min(dot(ca, ca), dot(cb, cb)));
}

float4 PSMain(VSOut i) : SV_Target {
    float2 p = i.p;
    float aa = fwidth(p.x) * 1.2;
    float3 light = i.color.rgb;
    float3 metal = float3(0.78, 0.8, 0.84);
    float d;
    float3 col;
    if (i.kind == 0) {
        float glass = min(Circle(p, float2(0, 0.2), 0.5), Trapezoid(p - float2(0, -0.2), 0.2, 0.34, 0.12));
        float base = Box(p, float2(0, -0.45), float2(0.2, 0.17)) - 0.05;
        d = min(glass, base);
        col = lerp(metal, light, Fill(glass, aa));
        float grooves = min(abs(p.y + 0.39), abs(p.y + 0.51)) - 0.025;
        col = lerp(col, metal * 0.5, Fill(max(grooves, base), aa));
        col = lerp(col, float3(1, 1, 1), Fill(Circle(p, float2(-0.17, 0.36), 0.1), aa) * 0.7);
    } else if (i.kind == 1) {
        float shade = Trapezoid(p - float2(0, 0.3), 0.5, 0.2, 0.28);
        float lens = Box(p, float2(0, 0), float2(0.44, 0.06)) - 0.04;
        float rays = min(Segment(p, float2(0, -0.22), float2(0, -0.78), 0.06),
                         min(Segment(p, float2(-0.3, -0.2), float2(-0.52, -0.7), 0.06),
                             Segment(p, float2(0.3, -0.2), float2(0.52, -0.7), 0.06)));
        d = min(min(shade, lens), rays);
        col = lerp(metal, light, Fill(min(lens, rays), aa));
    } else {
        float rays = 1e5;
        [unroll] for (int k = 0; k < 8; k++) {
            float2 dir;
            sincos(k * 0.785398, dir.y, dir.x);
            rays = min(rays, Segment(p, dir * 0.52, dir * 0.76, 0.065));
        }
        d = min(Circle(p, float2(0, 0), 0.36), rays);
        col = light;
    }
    bool selected = (i.flags & 1) != 0;
    float border = selected ? 0.15 : 0.1;
    float3 edge = selected ? float3(1, 0.62, 0.15) : float3(0.06, 0.06, 0.07);
    float a = Fill(d - border, aa);
    float3 rgb = lerp(edge, col, Fill(d, aa));
    if ((i.flags & 2) != 0) {
        rgb = lerp(rgb, dot(rgb, 1.0 / 3).xxx, 0.8) * 0.8;
        a *= 0.75;
    }
    a *= i.fade;
    float scene = LinearDepth(sceneDepth.Load(int3(i.position.xy, 0)));
    if (scene < i.depth - margin) a *= selected ? selectedOccluded : 0;
    return float4(rgb * a, a);
}
)";

}

void Viewer::CreateGizmoPipeline() {
    if (iconPso) return;

    D3D12_DESCRIPTOR_RANGE depthRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = { 0, 0, sizeof(IconConstants) / 4 };
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = { 1, &depthRange };
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
          "D3D12SerializeRootSignature icons");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&iconRootSignature)), "CreateRootSignature icons");

    auto compile = [](const char* entry, const char* target) {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> compileErrors;
        HRESULT hr = D3DCompile(ICON_SOURCE, std::strlen(ICON_SOURCE), nullptr, nullptr, nullptr,
                                entry, target, 0, 0, &blob, &compileErrors);
        if (FAILED(hr)) {
            std::string msg = std::string("compile icons ") + entry;
            if (compileErrors) msg += std::string(": ") + static_cast<const char*>(compileErrors->GetBufferPointer());
            throw std::runtime_error(msg);
        }
        return blob;
    };
    ComPtr<ID3DBlob> vs = compile("VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compile("PSMain", "ps_5_0");

    constexpr D3D12_INPUT_CLASSIFICATION PER_INSTANCE = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, PER_INSTANCE, 1 },
        { "KIND", 0, DXGI_FORMAT_R32_UINT, 0, 12, PER_INSTANCE, 1 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, PER_INSTANCE, 1 },
        { "FLAGS", 0, DXGI_FORMAT_R32_UINT, 0, 20, PER_INSTANCE, 1 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = iconRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout = { layout, 4 };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&iconPso)), "CreateGraphicsPipelineState icons");
}

// RenderFrame waits for the GPU, so the mapped buffers are free between frames.
void Viewer::SetLightIcons(std::span<const ViewerLightIcon> icons) {
    iconCount = static_cast<uint32_t>(icons.size());
    if (icons.empty()) return;
    CreateGizmoPipeline();
    if (icons.size_bytes() > iconCapacity) {
        iconCapacity = std::max<std::size_t>(icons.size_bytes() * 2, 16 * 1024);
        std::vector<uint8_t> zeros(iconCapacity, 0);
        iconBuffer = CreateUploadBuffer(zeros);
        iconBuffer->SetName(L"LightIcons");
        D3D12_RANGE readRange{};
        Check(iconBuffer->Map(0, &readRange, reinterpret_cast<void**>(&iconMapped)), "Map light icons");
    }
    std::memcpy(iconMapped, icons.data(), icons.size_bytes());
}

void Viewer::SetGizmoLines(std::span<const ViewerDebugVertex> lines) {
    gizmoLineVertices = static_cast<uint32_t>(lines.size() / 2 * 2);
    if (gizmoLineVertices == 0) return;
    CreateDebugPipeline();
    std::size_t bytes = gizmoLineVertices * sizeof(ViewerDebugVertex);
    if (bytes > gizmoCapacity) {
        gizmoCapacity = std::max<std::size_t>(bytes * 2, 16 * 1024);
        std::vector<uint8_t> zeros(gizmoCapacity, 0);
        gizmoBuffer = CreateUploadBuffer(zeros);
        gizmoBuffer->SetName(L"GizmoLines");
        D3D12_RANGE readRange{};
        Check(gizmoBuffer->Map(0, &readRange, reinterpret_cast<void**>(&gizmoMapped)), "Map gizmo lines");
    }
    std::memcpy(gizmoMapped, lines.data(), bytes);
}

void Viewer::SetHandleGeometry(std::span<const ViewerDebugVertex> triangles) {
    handleVertices = static_cast<uint32_t>(triangles.size() / 3 * 3);
    if (handleVertices == 0) return;
    CreateDebugPipeline();
    std::size_t bytes = handleVertices * sizeof(ViewerDebugVertex);
    if (bytes > handleCapacity) {
        handleCapacity = std::max<std::size_t>(bytes * 2, 64 * 1024);
        std::vector<uint8_t> zeros(handleCapacity, 0);
        handleBuffer = CreateUploadBuffer(zeros);
        handleBuffer->SetName(L"TransformHandles");
        D3D12_RANGE readRange{};
        Check(handleBuffer->Map(0, &readRange, reinterpret_cast<void**>(&handleMapped)), "Map transform handles");
    }
    std::memcpy(handleMapped, triangles.data(), bytes);
}

void Viewer::SetDrawBounds(std::span<const ViewerDrawBounds> bounds) {
    if (bounds.empty()) return;
    uint8_t* spheres = nullptr;
    D3D12_RANGE none{};
    if (cullSpheres && FAILED(cullSpheres->Map(0, &none, reinterpret_cast<void**>(&spheres)))) spheres = nullptr;
    if (drawIndexById.size() != draws.size()) {
        drawIndexById.assign(draws.size(), UINT32_MAX);
        for (std::size_t i = 0; i < draws.size(); i++) {
            if (draws[i].id < drawIndexById.size()) drawIndexById[draws[i].id] = static_cast<uint32_t>(i);
        }
    }
    for (const ViewerDrawBounds& b : bounds) {
        if (b.id >= drawIndexById.size() || drawIndexById[b.id] == UINT32_MAX) continue;
        std::size_t index = drawIndexById[b.id];
        std::copy(b.center, b.center + 3, draws[index].boundsCenter);
        draws[index].boundsRadius = b.radius;
        if (spheres && index < cullDrawCount) {
            float sphere[4] = { b.center[0], b.center[1], b.center[2], b.radius };
            std::memcpy(spheres + index * 16, sphere, 16);
        }
    }
    if (spheres) cullSpheres->Unmap(0, nullptr);
}

// After the overlay, on the back buffer with the depth buffer readable.
void Viewer::DrawGizmos() {
    if (gizmoLineVertices > 0 && gizmoBuffer) DrawGizmoLines();
    DrawIcons();
    if (handleVertices > 0 && handleBuffer) DrawHandles();
}

void Viewer::DrawIcons() {
    if (iconCount == 0 || !iconBuffer) return;
    Mat4 view;
    Mat4 proj;
    std::memcpy(view.m, camView, sizeof(view.m));
    std::memcpy(proj.m, camProj, sizeof(proj.m));
    Mat4 viewProj = Mul(view, proj);
    IconConstants constants{};
    std::memcpy(constants.viewProj, viewProj.m, sizeof(constants.viewProj));
    constants.viewport[0] = static_cast<float>(width);
    constants.viewport[1] = static_cast<float>(height);
    constants.maxPixels = LIGHT_ICON_MAX_PIXELS;
    constants.nearZ = camNear;
    constants.farZ = camFar;
    constants.selectedOccluded = SELECTED_OCCLUDED_ALPHA;
    constants.pixelsPerMeter = camProj[5] * static_cast<float>(height) * 0.5f;
    constants.worldSize = LIGHT_ICON_WORLD_SIZE;
    constants.fadeBegin = LIGHT_ICON_FADE_BEGIN_PIXELS;
    constants.fadeEnd = LIGHT_ICON_FADE_END_PIXELS;
    constants.margin = LIGHT_ICON_OCCLUSION_MARGIN;

    commandList->SetGraphicsRootSignature(iconRootSignature.Get());
    commandList->SetPipelineState(iconPso.Get());
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(IconConstants) / 4, &constants, 0);
    commandList->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(SRV_RESOLVE_DEPTH));
    D3D12_VERTEX_BUFFER_VIEW instances{ iconBuffer->GetGPUVirtualAddress(), static_cast<UINT>(iconCapacity),
                                        sizeof(ViewerLightIcon) };
    commandList->IASetVertexBuffers(0, 1, &instances);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(6, iconCount, 0, 0);
}
