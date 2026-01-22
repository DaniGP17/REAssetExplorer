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

struct CollisionConstants {
    float viewProj[16];
    float light[4];
    float edges;
    float pad[3];
};

constexpr float COLLISION_BACKDROP = 0.045f;

const char* COLLISION_SOURCE = R"(
cbuffer Constants : register(b0) {
    row_major float4x4 viewProj;
    float4 light;
    float edges;
};

struct VSIn {
    float3 position : POSITION;
    float4 row0 : WORLD0;
    float4 row1 : WORLD1;
    float4 row2 : WORLD2;
    float4 color : COLOR;
};

struct VSOut {
    float4 position : SV_Position;
    float3 world : WORLDPOS;
    float4 color : COLOR;
};

VSOut VSMain(VSIn i) {
    float4 p = float4(i.position, 1);
    float3 world = float3(dot(p, i.row0), dot(p, i.row1), dot(p, i.row2));
    VSOut o;
    o.position = mul(float4(world, 1), viewProj);
    o.world = world;
    o.color = i.color;
    return o;
}

// The unlit resolve shows the effect target as x / (1 + x); undo that so the
// colours come out as written.
float3 Encode(float3 c) {
    c = min(c, 0.97);
    return c / (1 - c);
}

// Collision meshes have no normals: shade by the triangle plane.
float4 PSMain(VSOut i) : SV_Target {
    float3 rgb;
    float a = i.color.a;
    if (edges > 0) {
        rgb = i.color.rgb * 0.45;
        a = a >= 1 ? 0.35 : 0.5;
    } else {
        float3 n = normalize(cross(ddy(i.world), ddx(i.world)));
        rgb = i.color.rgb * (0.32 + 0.68 * abs(dot(n, light.xyz)));
    }
    return float4(Encode(rgb) * a, a);
}
)";

}

void Viewer::CreateCollisionPipeline() {
    if (collisionSolidPso) return;

    D3D12_ROOT_PARAMETER params[1]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = { 0, 0, sizeof(CollisionConstants) / 4 };
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
          "D3D12SerializeRootSignature collision");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&collisionRootSignature)), "CreateRootSignature collision");

    auto compile = [](const char* entry, const char* target) {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> compileErrors;
        HRESULT hr = D3DCompile(COLLISION_SOURCE, std::strlen(COLLISION_SOURCE), nullptr, nullptr, nullptr,
                                entry, target, 0, 0, &blob, &compileErrors);
        if (FAILED(hr)) {
            std::string msg = std::string("compile collision ") + entry;
            if (compileErrors) msg += std::string(": ") + static_cast<const char*>(compileErrors->GetBufferPointer());
            throw std::runtime_error(msg);
        }
        return blob;
    };
    ComPtr<ID3DBlob> vs = compile("VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compile("PSMain", "ps_5_0");

    constexpr D3D12_INPUT_CLASSIFICATION PER_INSTANCE = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, PER_INSTANCE, 1 },
        { "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, PER_INSTANCE, 1 },
        { "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, PER_INSTANCE, 1 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 48, PER_INSTANCE, 1 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = collisionRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout = { layout, 5 };
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
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&collisionSolidPso)),
          "CreateGraphicsPipelineState collision solids");
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&collisionVolumePso)),
          "CreateGraphicsPipelineState collision volumes");
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.RasterizerState.DepthBias = -16;
    psoDesc.RasterizerState.SlopeScaledDepthBias = -1.0f;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&collisionEdgePso)),
          "CreateGraphicsPipelineState collision edges");
}

void Viewer::SetCollisionGeometry(std::span<const float> positions, std::span<const uint32_t> indices) {
    collisionBatches.clear();
    collisionPositionBuffer.Reset();
    collisionIndexBuffer.Reset();
    if (positions.empty() || indices.empty()) return;
    CreateCollisionPipeline();
    collisionPositionBuffer = CreateUploadBuffer({ reinterpret_cast<const uint8_t*>(positions.data()), positions.size_bytes() });
    collisionPositionBuffer->SetName(L"CollisionPositions");
    collisionIndexBuffer = CreateUploadBuffer({ reinterpret_cast<const uint8_t*>(indices.data()), indices.size_bytes() });
    collisionIndexBuffer->SetName(L"CollisionIndices");
    collisionPositionView = { collisionPositionBuffer->GetGPUVirtualAddress(), static_cast<UINT>(positions.size_bytes()),
                              12 };
    collisionIndexView = { collisionIndexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(indices.size_bytes()),
                           DXGI_FORMAT_R32_UINT };
}

// RenderFrame waits for the GPU, so the mapped buffer is free between frames.
void Viewer::SetCollisionInstances(std::span<const ViewerCollisionInstance> instances,
                                   std::span<const ViewerCollisionBatch> batches) {
    collisionBatches.assign(batches.begin(), batches.end());
    if (instances.empty()) return;
    if (instances.size_bytes() > collisionInstanceCapacity) {
        collisionInstanceCapacity = std::max<std::size_t>(instances.size_bytes(), 64 * 1024);
        std::vector<uint8_t> zeros(collisionInstanceCapacity, 0);
        collisionInstanceBuffer = CreateUploadBuffer(zeros);
        collisionInstanceBuffer->SetName(L"CollisionInstances");
        D3D12_RANGE readRange{};
        Check(collisionInstanceBuffer->Map(0, &readRange, reinterpret_cast<void**>(&collisionInstanceMapped)),
              "Map collision instances");
    }
    std::memcpy(collisionInstanceMapped, instances.data(), instances.size_bytes());
}

void Viewer::EnsureCollisionDepth() {
    if (collisionDepth) {
        D3D12_RESOURCE_DESC current = collisionDepth->GetDesc();
        if (current.Width == width && current.Height == height) return;
    }
    if (!collisionDsvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&collisionDsvHeap)), "CreateDescriptorHeap collision DSV");
    }
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = desc.Format;
    clear.DepthStencil.Depth = 1.0f;
    collisionDepth.Reset();
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                          &clear, IID_PPV_ARGS(&collisionDepth)), "CreateCommittedResource collision depth");
    collisionDepth->SetName(L"CollisionDepth");
    device->CreateDepthStencilView(collisionDepth.Get(), nullptr, collisionDsvHeap->GetCPUDescriptorHandleForHeapStart());
}

// Like Unreal's Player Collision view: the scene is hidden (alpha 0 in the effect
// target), colliders are opaque with their own depth, trigger volumes translucent.
void Viewer::DrawCollision(D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    const float backdrop[4] = { COLLISION_BACKDROP, COLLISION_BACKDROP, COLLISION_BACKDROP * 1.1f, 0 };
    commandList->ClearRenderTargetView(rtv, backdrop, 0, nullptr);
    if (!collisionInstanceBuffer || !collisionIndexBuffer || collisionBatches.empty()) return;
    EnsureCollisionDepth();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = collisionDsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    Mat4 view;
    Mat4 proj;
    std::memcpy(view.m, camView, sizeof(view.m));
    std::memcpy(proj.m, camProj, sizeof(proj.m));
    Mat4 viewProj = Mul(view, proj);
    CollisionConstants constants{};
    std::memcpy(constants.viewProj, viewProj.m, sizeof(constants.viewProj));
    // Same camera-fixed key light as the resolve.
    float light[3];
    for (int i = 0; i < 3; i++) light[i] = -camView[i * 4] * 0.6f + camView[i * 4 + 1] * 0.7f + camView[i * 4 + 2] * 0.55f;
    float length = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    for (int i = 0; i < 3; i++) constants.light[i] = light[i] / length;

    commandList->SetGraphicsRootSignature(collisionRootSignature.Get());
    D3D12_VERTEX_BUFFER_VIEW views[2] = {
        collisionPositionView,
        { collisionInstanceBuffer->GetGPUVirtualAddress(), static_cast<UINT>(collisionInstanceCapacity),
          sizeof(ViewerCollisionInstance) },
    };
    commandList->IASetVertexBuffers(0, 2, views);
    commandList->IASetIndexBuffer(&collisionIndexView);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto draw = [&](ID3D12PipelineState* pso, bool edges, bool translucent) {
        constants.edges = edges ? 1.0f : 0.0f;
        commandList->SetGraphicsRoot32BitConstants(0, sizeof(CollisionConstants) / 4, &constants, 0);
        commandList->SetPipelineState(pso);
        for (const ViewerCollisionBatch& batch : collisionBatches) {
            if (batch.instanceCount == 0 || batch.translucent != translucent) continue;
            commandList->DrawIndexedInstanced(batch.indexCount, batch.instanceCount, batch.firstIndex,
                                              static_cast<INT>(batch.baseVertex), batch.firstInstance);
        }
    };
    draw(collisionSolidPso.Get(), false, false);
    draw(collisionEdgePso.Get(), true, false);
    draw(collisionVolumePso.Get(), false, true);
    draw(collisionEdgePso.Get(), true, true);
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
}
