#include "Renderer/Viewer.h"

#include <algorithm>
#include <cstring>
#include <d3dcompiler.h>
#include <string>

#include "D3D12Utils.h"
#include "Renderer/RenderMath.h"

using Microsoft::WRL::ComPtr;

namespace {

struct DebugConstants {
    float viewProj[16];
    float eye[3];
    float nearZ;
    float farZ;
    float occluded;
    float bias;
    float shade;
};

// Collision and render geometry usually coincide, so their depth test allows
// 5 cm + 0.2% of the distance; hidden fragments stay faintly visible.
constexpr float DEBUG_OCCLUDED_ALPHA = 0.15f;
constexpr float DEBUG_DEPTH_BIAS = 0.05f;
constexpr float OVERLAY_DEPTH_BIAS = 0.002f;
constexpr float GIZMO_OCCLUDED_ALPHA = 0.35f;

const char* DEBUG_SOURCE = R"(
cbuffer Constants : register(b0) {
    row_major float4x4 viewProj;
    float3 eye;
    float nearZ;
    float farZ;
    float occluded;
    float bias;
    float shade;
};
Texture2D<float> sceneDepth : register(t0);

struct VSIn {
    float3 position : POSITION;
    float4 color : COLOR;
};

struct VSOut {
    float4 position : SV_Position;
    float4 color : COLOR;
    float3 world : TEXCOORD0;
};

VSOut VSMain(VSIn i) {
    VSOut o;
    o.position = mul(float4(i.position, 1), viewProj);
    o.color = i.color;
    o.world = i.position;
    return o;
}

float LinearDepth(float z) {
    return nearZ * farZ / (farZ - z * (farZ - nearZ));
}

float4 PSMain(VSOut i) : SV_Target {
    float scene = LinearDepth(sceneDepth.Load(int3(i.position.xy, 0)));
    float frag = LinearDepth(i.position.z);
    float a = i.color.a * (frag <= scene + bias + scene * 0.002 ? 1.0 : occluded);
    float3 rgb = i.color.rgb;
    if (shade > 0) {
        float3 n = normalize(cross(ddx(i.world), ddy(i.world)));
        rgb *= 0.35 + 0.65 * abs(dot(n, normalize(eye - i.world)));
    }
    return float4(rgb * a, a);
}
)";

DebugConstants MakeDebugConstants(const float view[16], const float proj[16], const float eye[3], float nearZ, float farZ) {
    Mat4 v;
    Mat4 p;
    std::memcpy(v.m, view, sizeof(v.m));
    std::memcpy(p.m, proj, sizeof(p.m));
    Mat4 viewProj = Mul(v, p);
    DebugConstants constants{};
    std::memcpy(constants.viewProj, viewProj.m, sizeof(constants.viewProj));
    std::memcpy(constants.eye, eye, sizeof(constants.eye));
    constants.nearZ = nearZ;
    constants.farZ = farZ;
    return constants;
}

}

void Viewer::CreateDebugPipeline() {
    if (debugTrianglePso) return;

    D3D12_DESCRIPTOR_RANGE depthRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = { 0, 0, sizeof(DebugConstants) / 4 };
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
          "D3D12SerializeRootSignature debug");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&debugRootSignature)), "CreateRootSignature debug");

    auto compile = [](const char* entry, const char* target) {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> compileErrors;
        HRESULT hr = D3DCompile(DEBUG_SOURCE, std::strlen(DEBUG_SOURCE), nullptr, nullptr, nullptr,
                                entry, target, 0, 0, &blob, &compileErrors);
        if (FAILED(hr)) {
            std::string msg = std::string("compile debug ") + entry;
            if (compileErrors) msg += std::string(": ") + static_cast<const char*>(compileErrors->GetBufferPointer());
            throw std::runtime_error(msg);
        }
        return blob;
    };
    ComPtr<ID3DBlob> vs = compile("VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compile("PSMain", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = debugRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout = { layout, 2 };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    // Premultiplied "over" into the effect target, as particles.
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
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&debugTrianglePso)), "CreateGraphicsPipelineState debug triangles");
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&debugLinePso)), "CreateGraphicsPipelineState debug lines");

    // The overlay goes straight to the back buffer (display space).
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&overlayTrianglePso)), "CreateGraphicsPipelineState overlay triangles");
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psoDesc.RasterizerState.AntialiasedLineEnable = TRUE;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&overlayLinePso)), "CreateGraphicsPipelineState overlay lines");
}

// RenderFrame waits for the GPU, so the mapped buffer is free between frames.
void Viewer::SetOverlayGeometry(std::span<const ViewerDebugVertex> triangles, std::span<const ViewerDebugVertex> lines) {
    CreateDebugPipeline();
    overlayTriangleVertices = static_cast<uint32_t>(triangles.size() / 3 * 3);
    overlayLineVertices = static_cast<uint32_t>(lines.size() / 2 * 2);
    std::size_t triangleBytes = overlayTriangleVertices * sizeof(ViewerDebugVertex);
    std::size_t bytes = triangleBytes + overlayLineVertices * sizeof(ViewerDebugVertex);
    if (bytes == 0) return;
    if (bytes > overlayGeometryCapacity) {
        overlayGeometryCapacity = std::max<std::size_t>(bytes + bytes / 2, 64 * 1024);
        std::vector<uint8_t> zeros(overlayGeometryCapacity, 0);
        overlayGeometryBuffer = CreateUploadBuffer(zeros);
        overlayGeometryBuffer->SetName(L"OverlayGeometry");
        D3D12_RANGE readRange{};
        Check(overlayGeometryBuffer->Map(0, &readRange, reinterpret_cast<void**>(&overlayGeometryMapped)), "Map overlay geometry");
    }
    std::memcpy(overlayGeometryMapped, triangles.data(), triangleBytes);
    std::memcpy(overlayGeometryMapped + triangleBytes, lines.data(), overlayLineVertices * sizeof(ViewerDebugVertex));
}

void Viewer::DrawOverlay() {
    if (!overlayGeometryBuffer || overlayTriangleVertices + overlayLineVertices == 0) return;

    DebugConstants constants = MakeDebugConstants(camView, camProj, camEye, camNear, camFar);
    constants.occluded = overlayOccluded;
    constants.bias = OVERLAY_DEPTH_BIAS;

    commandList->SetGraphicsRootSignature(debugRootSignature.Get());
    commandList->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(SRV_RESOLVE_DEPTH));
    D3D12_VERTEX_BUFFER_VIEW vertices{ overlayGeometryBuffer->GetGPUVirtualAddress(),
                                       static_cast<UINT>((overlayTriangleVertices + overlayLineVertices) * sizeof(ViewerDebugVertex)),
                                       sizeof(ViewerDebugVertex) };
    commandList->IASetVertexBuffers(0, 1, &vertices);
    if (overlayTriangleVertices > 0) {
        constants.shade = 1;
        commandList->SetGraphicsRoot32BitConstants(0, sizeof(DebugConstants) / 4, &constants, 0);
        commandList->SetPipelineState(overlayTrianglePso.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawInstanced(overlayTriangleVertices, 1, 0, 0);
    }
    if (overlayLineVertices > 0) {
        constants.shade = 0;
        commandList->SetGraphicsRoot32BitConstants(0, sizeof(DebugConstants) / 4, &constants, 0);
        commandList->SetPipelineState(overlayLinePso.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        commandList->DrawInstanced(overlayLineVertices, 1, overlayTriangleVertices, 0);
    }
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Viewer::DrawGizmoLines() {
    DebugConstants constants = MakeDebugConstants(camView, camProj, camEye, camNear, camFar);
    constants.occluded = GIZMO_OCCLUDED_ALPHA;
    constants.bias = OVERLAY_DEPTH_BIAS;
    commandList->SetGraphicsRootSignature(debugRootSignature.Get());
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(DebugConstants) / 4, &constants, 0);
    commandList->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(SRV_RESOLVE_DEPTH));
    D3D12_VERTEX_BUFFER_VIEW vertices{ gizmoBuffer->GetGPUVirtualAddress(),
                                       static_cast<UINT>(gizmoLineVertices * sizeof(ViewerDebugVertex)),
                                       sizeof(ViewerDebugVertex) };
    commandList->IASetVertexBuffers(0, 1, &vertices);
    commandList->SetPipelineState(overlayLinePso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList->DrawInstanced(gizmoLineVertices, 1, 0, 0);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// Always on top and flat: nothing hides a handle the user is dragging.
void Viewer::DrawHandles() {
    DebugConstants constants = MakeDebugConstants(camView, camProj, camEye, camNear, camFar);
    constants.occluded = 1;
    constants.bias = OVERLAY_DEPTH_BIAS;
    constants.shade = 0;
    commandList->SetGraphicsRootSignature(debugRootSignature.Get());
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(DebugConstants) / 4, &constants, 0);
    commandList->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(SRV_RESOLVE_DEPTH));
    D3D12_VERTEX_BUFFER_VIEW vertices{ handleBuffer->GetGPUVirtualAddress(),
                                       static_cast<UINT>(handleVertices * sizeof(ViewerDebugVertex)), sizeof(ViewerDebugVertex) };
    commandList->IASetVertexBuffers(0, 1, &vertices);
    commandList->SetPipelineState(overlayTrianglePso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(handleVertices, 1, 0, 0);
}

void Viewer::SetDebugGeometry(std::span<const ViewerDebugVertex> triangles, std::span<const ViewerDebugVertex> lines) {
    CreateDebugPipeline();
    debugTriangleVertices = static_cast<uint32_t>(triangles.size() / 3 * 3);
    debugLineVertices = static_cast<uint32_t>(lines.size() / 2 * 2);
    std::vector<ViewerDebugVertex> vertices(triangles.begin(), triangles.end());
    vertices.insert(vertices.end(), lines.begin(), lines.end());
    if (vertices.empty()) {
        debugBuffer.Reset();
        return;
    }
    debugBuffer = CreateUploadBuffer({ reinterpret_cast<const uint8_t*>(vertices.data()), vertices.size() * sizeof(ViewerDebugVertex) });
    debugBuffer->SetName(L"DebugGeometry");
}

// Runs with the effect target bound and the depth buffer readable.
void Viewer::DrawDebugGeometry() {
    if (!debugBuffer || (debugTriangleVertices == 0 && debugLineVertices == 0)) return;

    DebugConstants constants = MakeDebugConstants(camView, camProj, camEye, camNear, camFar);
    constants.occluded = DEBUG_OCCLUDED_ALPHA;
    constants.bias = DEBUG_DEPTH_BIAS;

    commandList->SetGraphicsRootSignature(debugRootSignature.Get());
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(DebugConstants) / 4, &constants, 0);
    commandList->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(SRV_RESOLVE_DEPTH));
    D3D12_VERTEX_BUFFER_VIEW view0{ debugBuffer->GetGPUVirtualAddress(),
                                    static_cast<UINT>((debugTriangleVertices + debugLineVertices) * sizeof(ViewerDebugVertex)),
                                    sizeof(ViewerDebugVertex) };
    commandList->IASetVertexBuffers(0, 1, &view0);
    if (debugTriangleVertices > 0) {
        commandList->SetPipelineState(debugTrianglePso.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawInstanced(debugTriangleVertices, 1, 0, 0);
    }
    if (debugLineVertices > 0) {
        commandList->SetPipelineState(debugLinePso.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        commandList->DrawInstanced(debugLineVertices, 1, debugTriangleVertices, 0);
    }
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
