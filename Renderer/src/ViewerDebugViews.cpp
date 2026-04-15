#include "Renderer/Viewer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <string>

#include "D3D12Utils.h"
#include "Renderer/RenderMath.h"

using Microsoft::WRL::ComPtr;

namespace {

struct DebugViewConstants {
    float viewProj[16];  // viewProjInv for the full-screen passes
    float eye[3];
    uint32_t color;
    uint32_t instanceOffset;  // bytes
    uint32_t lightCount;
    float pad[2];
};

// One shader set: geometry passes read positions and the InstanceWorld rows, full-screen ones the depth.
const char* DEBUG_VIEW_SOURCE = R"(
cbuffer Constants : register(b0) {
    row_major float4x4 transform;
    float3 eye;
    uint color;
    uint instanceOffset;
    uint lightCount;
};
ByteAddressBuffer data : register(t0);
Texture2D<float> source : register(t1);

struct GeometryOut {
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
};

GeometryOut GeometryVS(float3 position : POSITION) {
    float4 p = float4(position, 1);
    float3 world = float3(dot(p, asfloat(data.Load4(instanceOffset))),
                          dot(p, asfloat(data.Load4(instanceOffset + 16))),
                          dot(p, asfloat(data.Load4(instanceOffset + 32))));
    GeometryOut o;
    o.position = mul(float4(world, 1), transform);
    o.world = world;
    return o;
}

float4 FlatPS(GeometryOut i) : SV_Target {
    float3 n = normalize(cross(ddy(i.world), ddx(i.world)));
    float3 toEye = normalize(eye - i.world);
    float3 rgb = float3(color & 255, (color >> 8) & 255, (color >> 16) & 255) / 255.0;
    return float4(rgb * (0.35 + 0.65 * abs(dot(n, toEye))), 1);
}

float OverdrawPS(GeometryOut i) : SV_Target {
    return 1;
}

float4 FullscreenVS(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 GreyPS(float4 pos : SV_Position) : SV_Target {
    return float4(0.5, 0.5, 0.5, 0);
}

// Unreal's shader complexity ramp: black, green, yellow, red, white.
float3 Heat(float t) {
    t = saturate(t);
    if (t < 0.25) return lerp(float3(0, 0, 0), float3(0, 0.8, 0.1), t / 0.25);
    if (t < 0.5) return lerp(float3(0, 0.8, 0.1), float3(1, 0.9, 0), (t - 0.25) / 0.25);
    if (t < 0.75) return lerp(float3(1, 0.9, 0), float3(1, 0.1, 0), (t - 0.5) / 0.25);
    return lerp(float3(1, 0.1, 0), float3(1, 1, 1), (t - 0.75) / 0.25);
}

float4 OverdrawHeatPS(float4 pos : SV_Position) : SV_Target {
    float count = source.Load(int3(pos.xy, 0));
    return float4(Heat(count / 12.0), 1);
}

float4 LightComplexityPS(float4 pos : SV_Position) : SV_Target {
    int2 p = int2(pos.xy);
    float depth = source.Load(int3(p, 0));
    if (depth >= 1.0) return float4(0, 0, 0, 1);
    uint w, h;
    source.GetDimensions(w, h);
    float2 ndc = float2((p.x + 0.5) / w * 2 - 1, 1 - (p.y + 0.5) / h * 2);
    float4 world = mul(float4(ndc, depth, 1), transform);
    float3 at = world.xyz / world.w;
    uint count = 0;
    for (uint k = 0; k < lightCount; k++) {
        float4 sphere = asfloat(data.Load4(k * 16));
        float3 d = at - sphere.xyz;
        if (dot(d, d) < sphere.w * sphere.w) count++;
    }
    return float4(Heat(count / 8.0), 1);
}
)";

ComPtr<ID3DBlob> CompileDebugView(const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(DEBUG_VIEW_SOURCE, std::strlen(DEBUG_VIEW_SOURCE), nullptr, nullptr, nullptr, entry, target, 0, 0,
                            &blob, &errors);
    if (FAILED(hr)) {
        std::string msg = std::string("compile debug view ") + entry;
        if (errors) msg += std::string(": ") + static_cast<const char*>(errors->GetBufferPointer());
        throw std::runtime_error(msg);
    }
    return blob;
}

bool Inverse4(const Mat4& m, Mat4& out) {
    float a[4][8];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            a[r][c] = m.m[r * 4 + c];
            a[r][c + 4] = r == c ? 1.0f : 0.0f;
        }
    }
    for (int c = 0; c < 4; c++) {
        int pivot = c;
        for (int r = c + 1; r < 4; r++) {
            if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
        }
        if (std::fabs(a[pivot][c]) < 1e-20f) return false;
        if (pivot != c) std::swap(a[pivot], a[c]);
        float inv = 1.0f / a[c][c];
        for (float& v : a[c]) v *= inv;
        for (int r = 0; r < 4; r++) {
            if (r == c) continue;
            float f = a[r][c];
            for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k];
        }
    }
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) out.m[r * 4 + c] = a[r][c + 4];
    }
    return true;
}

}

void Viewer::SetLightSpheres(std::span<const float> spheres) {
    lightSphereCount = static_cast<uint32_t>(spheres.size() / 4);
    if (lightSphereCount == 0) return;
    std::size_t bytes = static_cast<std::size_t>(lightSphereCount) * 16;
    if (bytes > lightSphereCapacity) {
        lightSphereCapacity = std::max<std::size_t>(bytes * 2, 4096);
        std::vector<uint8_t> zeros(lightSphereCapacity, 0);
        lightSphereBuffer = CreateUploadBuffer(zeros);
        lightSphereBuffer->SetName(L"LightSpheres");
        D3D12_RANGE readRange{};
        Check(lightSphereBuffer->Map(0, &readRange, reinterpret_cast<void**>(&lightSphereMapped)), "Map light spheres");
    }
    std::memcpy(lightSphereMapped, spheres.data(), bytes);
}

void Viewer::CreateDebugViewPipelines() {
    if (debugViewRootSignature) return;

    D3D12_DESCRIPTOR_RANGE sourceRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 };
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = { 0, 0, sizeof(DebugViewConstants) / 4 };
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor = { 0, 0 };
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable = { 1, &sourceRange };
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 3;
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
          "D3D12SerializeRootSignature debug view");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&debugViewRootSignature)), "CreateRootSignature debug view");

    ComPtr<ID3DBlob> geometryVs = CompileDebugView("GeometryVS", "vs_5_0");
    ComPtr<ID3DBlob> fullscreenVs = CompileDebugView("FullscreenVS", "vs_5_0");
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto make = [&](const char* ps, bool geometry, DXGI_FORMAT format, bool depth, bool additive, UINT8 writeMask,
                    ComPtr<ID3D12PipelineState>& out) {
        ComPtr<ID3DBlob> pixel = CompileDebugView(ps, "ps_5_0");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = debugViewRootSignature.Get();
        ID3DBlob* vs = geometry ? geometryVs.Get() : fullscreenVs.Get();
        desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        desc.PS = { pixel->GetBufferPointer(), pixel->GetBufferSize() };
        if (geometry) desc.InputLayout = { layout, 1 };
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[0];
        rt.BlendEnable = additive;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_ONE;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ONE;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        rt.RenderTargetWriteMask = writeMask;
        if (depth) {
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            desc.DepthStencilState.DepthEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        }
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = format;
        desc.SampleDesc.Count = 1;
        Check(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&out)), ps);
    };
    constexpr UINT8 ALL = D3D12_COLOR_WRITE_ENABLE_ALL;
    constexpr UINT8 RGB = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
    make("GreyPS", false, GBUFFER_FORMATS[1], false, false, RGB, lightingOnlyPso);
    make("FlatPS", true, DXGI_FORMAT_R8G8B8A8_UNORM, true, false, ALL, flatColorPso);
    make("OverdrawPS", true, DXGI_FORMAT_R16_FLOAT, false, true, ALL, overdrawPso);
    make("OverdrawHeatPS", false, DXGI_FORMAT_R8G8B8A8_UNORM, false, false, ALL, heatPso);
    make("LightComplexityPS", false, DXGI_FORMAT_R8G8B8A8_UNORM, false, false, ALL, lightComplexityPso);
}

void Viewer::EnsureDebugViewTargets() {
    if (debugViewDepth && debugViewDepth->GetDesc().Width == width && debugViewDepth->GetDesc().Height == height) return;
    if (!debugViewDsvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&debugViewDsvHeap)), "CreateDescriptorHeap debug view DSV");
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        Check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&overdrawRtvHeap)), "CreateDescriptorHeap overdraw RTV");
    }
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = desc.Format;
    depthClear.DepthStencil.Depth = 1.0f;
    debugViewDepth.Reset();
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
                                          IID_PPV_ARGS(&debugViewDepth)), "CreateCommittedResource debug view depth");
    debugViewDepth->SetName(L"DebugViewDepth");
    device->CreateDepthStencilView(debugViewDepth.Get(), nullptr, debugViewDsvHeap->GetCPUDescriptorHandleForHeapStart());

    desc.Format = DXGI_FORMAT_R16_FLOAT;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE colorClear{};
    colorClear.Format = desc.Format;
    overdrawTarget.Reset();
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                          &colorClear, IID_PPV_ARGS(&overdrawTarget)), "CreateCommittedResource overdraw");
    overdrawTarget->SetName(L"Overdraw");
    device->CreateRenderTargetView(overdrawTarget.Get(), nullptr, overdrawRtvHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateShaderResourceView(overdrawTarget.Get(), nullptr, SrvCpuHandle(SRV_OVERDRAW));
}

// While the G-buffer targets are bound: the lighting then sees a grey albedo.
void Viewer::GreyAlbedo() {
    CreateDebugViewPipelines();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = gbufferRtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += rtvStride;
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    commandList->SetGraphicsRootSignature(debugViewRootSignature.Get());
    commandList->SetPipelineState(lightingOnlyPso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}

// On the back buffer after the resolve, with the scene depth readable.
void Viewer::DrawDebugView(D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    if (debugView == ViewerDebugView::None || debugView == ViewerDebugView::LightingOnly) return;
    CreateDebugViewPipelines();
    EnsureDebugViewTargets();
    DebugViewConstants constants{};
    Mat4 view;
    Mat4 proj;
    std::memcpy(view.m, camView, sizeof(view.m));
    std::memcpy(proj.m, camProj, sizeof(proj.m));
    Mat4 viewProj = Mul(view, proj);
    std::memcpy(constants.eye, camEye, sizeof(constants.eye));
    commandList->SetGraphicsRootSignature(debugViewRootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (debugView == ViewerDebugView::LightComplexity) {
        if (!lightSphereBuffer) return;
        Mat4 inverse;
        if (!Inverse4(viewProj, inverse)) return;
        std::memcpy(constants.viewProj, inverse.m, sizeof(constants.viewProj));
        constants.lightCount = lightSphereCount;
        commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        commandList->SetPipelineState(lightComplexityPso.Get());
        commandList->SetGraphicsRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
        commandList->SetGraphicsRootShaderResourceView(1, lightSphereBuffer->GetGPUVirtualAddress());
        commandList->SetGraphicsRootDescriptorTable(2, SrvGpuHandle(SRV_RESOLVE_DEPTH));
        commandList->DrawInstanced(3, 1, 0, 0);
        return;
    }

    if (!instanceBuffer || !hasMesh) return;
    std::memcpy(constants.viewProj, viewProj.m, sizeof(constants.viewProj));
    bool overdraw = debugView == ViewerDebugView::Overdraw;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = debugViewDsvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE overdrawRtv = overdrawRtvHeap->GetCPUDescriptorHandleForHeapStart();
    if (overdraw) {
        D3D12_RESOURCE_BARRIER toRt = Transition(overdrawTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &toRt);
        const float zero[4] = {};
        commandList->ClearRenderTargetView(overdrawRtv, zero, 0, nullptr);
        commandList->OMSetRenderTargets(1, &overdrawRtv, FALSE, nullptr);
        commandList->SetPipelineState(overdrawPso.Get());
    } else {
        const float black[4] = { 0.02f, 0.02f, 0.025f, 1 };
        commandList->ClearRenderTargetView(rtv, black, 0, nullptr);
        commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        commandList->SetPipelineState(flatColorPso.Get());
    }
    commandList->SetGraphicsRootShaderResourceView(1, instanceBuffer->GetGPUVirtualAddress());
    commandList->IASetVertexBuffers(0, 1, &positionView);
    commandList->IASetIndexBuffer(&indexView);
    for (std::size_t di = 0; di < draws.size(); di++) {
        const ViewerMeshDraw& draw = draws[di];
        if (!drawVisible.empty() && !drawVisible[di]) continue;
        constants.color = draw.id < drawColors.size() ? drawColors[draw.id] : 0xFFFFFFFFu;
        constants.instanceOffset = draw.instanceIndex * instanceStride;
        commandList->SetGraphicsRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
        commandList->DrawIndexedInstanced(draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
    }
    if (overdraw) {
        D3D12_RESOURCE_BARRIER toSrv = Transition(overdrawTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &toSrv);
        commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        commandList->SetPipelineState(heatPso.Get());
        commandList->SetGraphicsRootDescriptorTable(2, SrvGpuHandle(SRV_OVERDRAW));
        commandList->DrawInstanced(3, 1, 0, 0);
    }
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
}
