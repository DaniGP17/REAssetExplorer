#include "Renderer/Viewer.h"

#include <cstring>
#include <d3dcompiler.h>
#include <string>

#include "D3D12Utils.h"

using Microsoft::WRL::ComPtr;

namespace {

struct OutlineConstants {
    float visibleColor[4];
    float hiddenColor[4];
    float depthA;
    float depthB;
    int32_t size[2];
};

constexpr float OUTLINE_COLOR[3] = { 1.0f, 0.62f, 0.12f };
constexpr float HIDDEN_ALPHA = 0.35f;

const char* OUTLINE_SOURCE = R"(
cbuffer Constants : register(b0) {
    float4 visibleColor;
    float4 hiddenColor;
    float depthA;
    float depthB;
    int2 size;
};
Texture2D<float> sceneDepth : register(t0);
Texture2D<float> selectionDepth : register(t1);

static const float THICKNESS = 2.0;

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float ViewDistance(float d) {
    return depthB / (d + depthA);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target {
    int2 p = int2(pos.xy);
    if (selectionDepth.Load(int3(p, 0)) < 1) return 0;

    float visible = 0;
    float hidden = 0;
    [unroll] for (int y = -3; y <= 3; y++) {
        [unroll] for (int x = -3; x <= 3; x++) {
            float dist = length(float2(x, y));
            if (dist > THICKNESS + 0.5) continue;
            int2 q = clamp(p + int2(x, y), int2(0, 0), size - 1);
            float selected = selectionDepth.Load(int3(q, 0));
            if (selected >= 1) continue;
            float weight = saturate(THICKNESS + 0.5 - dist);
            float scene = sceneDepth.Load(int3(q, 0));
            if (ViewDistance(selected) <= ViewDistance(scene) * 1.002 + 0.002) visible = max(visible, weight);
            else hidden = max(hidden, weight);
        }
    }
    float a = visible * visibleColor.a;
    float b = hidden * hiddenColor.a;
    float3 color = a >= b ? visibleColor.rgb : hiddenColor.rgb;
    float alpha = max(a, b);
    return float4(color * alpha, alpha);
}
)";

}

void Viewer::SetSelection(std::span<const uint32_t> drawIds) {
    selectionMask.clear();
    for (uint32_t id : drawIds) {
        if (id >= selectionMask.size()) selectionMask.resize(id + 1, 0);
        selectionMask[id] = 1;
    }
    hasSelection = !drawIds.empty();
}

void Viewer::CreateSelectionTarget() {
    if (!selectionDsvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&selectionDsvHeap)), "CreateDescriptorHeap selection");
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
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                                          IID_PPV_ARGS(&selectionDepth)), "CreateCommittedResource selection");
    selectionDepth->SetName(L"SelectionDepth");
    device->CreateDepthStencilView(selectionDepth.Get(), nullptr, selectionDsvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(selectionDepth.Get(), &srvDesc, SrvCpuHandle(SRV_SELECTION_DEPTH));
}

void Viewer::CreateOutlinePipeline() {
    if (outlinePso) return;

    D3D12_DESCRIPTOR_RANGE sceneRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
    D3D12_DESCRIPTOR_RANGE selectionRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 };
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = { 0, 0, sizeof(OutlineConstants) / 4 };
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = { 1, &sceneRange };
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable = { 1, &selectionRange };
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 3;
    rootDesc.pParameters = params;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
          "D3D12SerializeRootSignature outline");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&outlineRootSignature)), "CreateRootSignature outline");

    auto compile = [](const char* entry, const char* target) {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> compileErrors;
        HRESULT hr = D3DCompile(OUTLINE_SOURCE, std::strlen(OUTLINE_SOURCE), nullptr, nullptr, nullptr,
                                entry, target, 0, 0, &blob, &compileErrors);
        if (FAILED(hr)) {
            std::string msg = std::string("compile outline ") + entry;
            if (compileErrors) msg += std::string(": ") + static_cast<const char*>(compileErrors->GetBufferPointer());
            throw std::runtime_error(msg);
        }
        return blob;
    };
    ComPtr<ID3DBlob> vs = compile("VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compile("PSMain", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = outlineRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
    rt.DestBlendAlpha = D3D12_BLEND_ONE;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outlinePso)), "CreateGraphicsPipelineState outline");
}

// Same vertex shaders as the GBuffer pass so skinning matches; leaves the mask
// as a shader resource.
void Viewer::RenderSelectionMask() {
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = selectionDsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

    const GamePipeline* current = nullptr;
    for (std::size_t di = 0; di < draws.size(); di++) {
        const ViewerMeshDraw& draw = draws[di];
        if (!drawVisible.empty() && !drawVisible[di]) continue;
        if (draw.id >= selectionMask.size() || !selectionMask[draw.id]) continue;
        uint32_t pipelineIndex = draw.pipelineIndex < selectionPipelines.size() ? draw.pipelineIndex : 0;
        // Alpha-tested masters clip in their prepass program, keeping cut-outs out of the mask.
        const GamePipeline& pipeline = pipelineIndex < prepassPipelines.size() &&
                                       prepassPipelines[pipelineIndex].pso
            ? prepassPipelines[pipelineIndex] : selectionPipelines[pipelineIndex];
        if (&pipeline != current) {
            current = &pipeline;
            BindGamePipeline(pipeline);
        }
        for (uint32_t paramIndex : pipeline.rootConstParams) {
            commandList->SetGraphicsRoot32BitConstant(paramIndex, draw.instanceIndex | (draw.materialSlot << 24), 0);
        }
        commandList->DrawIndexedInstanced(draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
    }

    D3D12_RESOURCE_BARRIER toSrv = Transition(selectionDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &toSrv);
}

// After the resolve, while the scene depth is a shader resource.
void Viewer::DrawSelectionOutline() {
    CreateOutlinePipeline();

    OutlineConstants constants{};
    for (int i = 0; i < 3; i++) {
        constants.visibleColor[i] = OUTLINE_COLOR[i];
        constants.hiddenColor[i] = OUTLINE_COLOR[i];
    }
    constants.visibleColor[3] = 1.0f;
    constants.hiddenColor[3] = HIDDEN_ALPHA;
    // Right-handed projection: depth d maps back to view distance B / (d + A).
    constants.depthA = camProj[10];
    constants.depthB = camProj[14];
    constants.size[0] = static_cast<int32_t>(width);
    constants.size[1] = static_cast<int32_t>(height);

    commandList->SetGraphicsRootSignature(outlineRootSignature.Get());
    commandList->SetPipelineState(outlinePso.Get());
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(OutlineConstants) / 4, &constants, 0);
    commandList->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(SRV_RESOLVE_DEPTH));
    commandList->SetGraphicsRootDescriptorTable(2, SrvGpuHandle(SRV_SELECTION_DEPTH));
    commandList->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER toDepth = Transition(selectionDepth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                D3D12_RESOURCE_STATE_DEPTH_WRITE);
    commandList->ResourceBarrier(1, &toDepth);
}
