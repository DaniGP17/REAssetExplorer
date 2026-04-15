#include "Renderer/Viewer.h"

#include <algorithm>
#include <cstring>
#include <d3dcompiler.h>
#include <string>

#include "D3D12Utils.h"

using Microsoft::WRL::ComPtr;

namespace {

struct TextConstants {
    float rect[4];  // x, y, width, height in pixels
    float viewport[2];
    float pad[2];
};

const char* TEXT_SOURCE = R"(
cbuffer Constants : register(b0) {
    float4 rect;
    float2 viewport;
};
StructuredBuffer<uint> pixels : register(t0);

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position {
    const float2 corners[6] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(0, 1), float2(1, 0), float2(1, 1) };
    float2 p = rect.xy + corners[vertexId] * rect.zw;
    return float4(p.x / viewport.x * 2 - 1, 1 - p.y / viewport.y * 2, 0, 1);
}

float4 PSMain(float4 position : SV_Position) : SV_Target {
    uint2 p = uint2(position.xy - rect.xy);
    uint c = pixels[p.y * (uint)rect.z + p.x];
    return float4(c & 255, (c >> 8) & 255, (c >> 16) & 255, c >> 24) / 255.0;
}
)";

}

void Viewer::CreateTextPipeline() {
    if (textPso) return;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = { 0, 0, sizeof(TextConstants) / 4 };
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor = { 0, 0 };
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = params;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
          "D3D12SerializeRootSignature text");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&textRootSignature)), "CreateRootSignature text");

    auto compile = [](const char* entry, const char* target) {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> compileErrors;
        HRESULT hr = D3DCompile(TEXT_SOURCE, std::strlen(TEXT_SOURCE), nullptr, nullptr, nullptr,
                                entry, target, 0, 0, &blob, &compileErrors);
        if (FAILED(hr)) {
            std::string msg = std::string("compile text ") + entry;
            if (compileErrors) msg += std::string(": ") + static_cast<const char*>(compileErrors->GetBufferPointer());
            throw std::runtime_error(msg);
        }
        return blob;
    };
    ComPtr<ID3DBlob> vs = compile("VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compile("PSMain", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = textRootSignature.Get();
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
    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&textPso)), "CreateGraphicsPipelineState text");
}

// RenderFrame waits for the GPU, so the mapped buffer is free between frames.
void Viewer::SetTextOverlay(std::span<const uint32_t> rgba, uint32_t textWidth, uint32_t textHeight, int32_t x, int32_t y,
                            uint32_t slot) {
    if (slot >= TEXT_SLOTS) return;
    TextSlot& text = textSlots[slot];
    text.size[0] = textWidth;
    text.size[1] = textHeight;
    text.position[0] = x;
    text.position[1] = y;
    if (textWidth == 0 || textHeight == 0 || rgba.size() < static_cast<std::size_t>(textWidth) * textHeight) {
        text.size[0] = text.size[1] = 0;
        return;
    }
    CreateTextPipeline();
    if (rgba.size_bytes() > text.capacity) {
        text.capacity = std::max<std::size_t>(rgba.size_bytes() * 2, 64 * 1024);
        std::vector<uint8_t> zeros(text.capacity, 0);
        text.buffer = CreateUploadBuffer(zeros);
        text.buffer->SetName(L"TextOverlay");
        D3D12_RANGE readRange{};
        Check(text.buffer->Map(0, &readRange, reinterpret_cast<void**>(&text.mapped)), "Map text overlay");
    }
    std::memcpy(text.mapped, rgba.data(), rgba.size_bytes());
}

// Last on the back buffer; left out of captures.
void Viewer::DrawTextOverlay(D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    bool bound = false;
    for (const TextSlot& text : textSlots) {
        if (text.size[0] == 0 || !text.buffer) continue;
        if (!bound) {
            commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            commandList->SetGraphicsRootSignature(textRootSignature.Get());
            commandList->SetPipelineState(textPso.Get());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            bound = true;
        }
        TextConstants constants{};
        constants.rect[0] = static_cast<float>(text.position[0]);
        constants.rect[1] = static_cast<float>(text.position[1]);
        constants.rect[2] = static_cast<float>(text.size[0]);
        constants.rect[3] = static_cast<float>(text.size[1]);
        constants.viewport[0] = static_cast<float>(width);
        constants.viewport[1] = static_cast<float>(height);
        commandList->SetGraphicsRoot32BitConstants(0, sizeof(TextConstants) / 4, &constants, 0);
        commandList->SetGraphicsRootShaderResourceView(1, text.buffer->GetGPUVirtualAddress());
        commandList->DrawInstanced(6, 1, 0, 0);
    }
}
