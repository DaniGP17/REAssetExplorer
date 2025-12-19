#include "Renderer/Viewer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <string>

#include "D3D12Utils.h"

using Microsoft::WRL::ComPtr;

namespace {

enum PreviewFlags : uint32_t {
    PREVIEW_SRGB = 1,        // sampling linearized the texels; re-encode for display
    PREVIEW_GRAY = 2,
    PREVIEW_NORMAL_XY = 4,   // two channel normal map: show with z = 1
    PREVIEW_HDR = 8,
    PREVIEW_POINT = 16,
    PREVIEW_CHECKER = 32
};

struct PreviewConstants {
    float rect[4];
    float source[4];
    float background[4];
    uint32_t flags;
    float lod;
    uint32_t channels;
    uint32_t overlayCount;
    float slice;
    float pad[3];
};

const char* PREVIEW_SOURCE = R"(
cbuffer Constants : register(b0) {
    float4 rect;
    float4 source;
    float4 background;
    uint flags;
    float lod;
    uint channels;
    uint overlayCount;
    float slice;
};
struct OverlayRect {
    float4 uv;
    float4 color;
    float4 fill;
};
Texture2DArray tex : register(t0);
StructuredBuffer<OverlayRect> overlay : register(t1);
SamplerState linearSampler : register(s0);
SamplerState pointSampler : register(s1);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float3 Checker(float2 p) {
    float2 cell = floor(p / 8);
    return fmod(cell.x + cell.y, 2) >= 1 ? float3(0.24, 0.24, 0.24) : float3(0.15, 0.15, 0.15);
}

float2 ToFrame(float2 uv) {
    return rect.xy + (uv - source.xy) / (source.zw - source.xy) * (rect.zw - rect.xy);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target {
    float2 local = (pos.xy - rect.xy) / (rect.zw - rect.xy);
    float3 color = background.rgb;
    if (all(local >= 0) && all(local <= 1)) {
        float3 uvw = float3(source.xy + local * (source.zw - source.xy), slice);
        float4 c = (flags & 16) ? tex.SampleLevel(pointSampler, uvw, lod) : tex.SampleLevel(linearSampler, uvw, lod);
        if (flags & 2) c.rgb = c.rrr;
        if (flags & 4) c.rgb = float3(saturate(c.rg), 1);
        if (flags & 8) c.rgb = c.rgb / (1 + max(c.rgb, 0));
        if (flags & 1) c.rgb = pow(saturate(c.rgb), 1.0 / 2.2);
        uint rgb = channels & 7;
        float3 shown = c.rgb * float3(rgb & 1, (rgb >> 1) & 1, (rgb >> 2) & 1);
        if (rgb == 0) shown = c.aaa;
        if (rgb == 1) shown = c.rrr;
        if (rgb == 2) shown = c.ggg;
        if (rgb == 4) shown = c.bbb;
        float alpha = (channels & 8) != 0 && rgb != 0 ? saturate(c.a) : 1;
        float3 under = (flags & 32) ? Checker(pos.xy - rect.xy) : background.rgb;
        color = lerp(under, saturate(shown), alpha);
    }
    for (uint i = 0; i < overlayCount; i++) {
        OverlayRect o = overlay[i];
        float2 a = ToFrame(o.uv.xy);
        float2 b = ToFrame(o.uv.zw);
        float2 lo = min(a, b);
        float2 hi = max(a, b);
        if (any(pos.xy < lo) || any(pos.xy > hi)) continue;
        float edge = min(min(pos.x - lo.x, hi.x - pos.x), min(pos.y - lo.y, hi.y - pos.y));
        color = lerp(color, o.fill.rgb, o.fill.a);
        if (edge < 1) color = lerp(color, o.color.rgb, o.color.a);
    }
    return float4(color, 1);
}
)";

uint32_t FlagsFor(uint32_t format) {
    switch (format) {
        case 29: case 72: case 75: case 78: case 91: case 93: case 99:
            return PREVIEW_SRGB;
        case 61: case 63: case 56: case 80: case 81:  // R8, R8_SNORM, R16, BC4
            return PREVIEW_GRAY;
        case 49: case 51: case 83: case 84:           // R8G8, R8G8_SNORM, BC5
            return PREVIEW_NORMAL_XY;
        case 10: case 95: case 96:                    // R16G16B16A16_FLOAT, BC6H
            return PREVIEW_HDR;
        default:
            return 0;
    }
}

}

void Viewer::SetPreviewSphere(const float center[3], float radius) {
    for (int i = 0; i < 3; i++) previewSphere[i] = center[i];
    previewSphere[3] = radius;
}

void Viewer::SetBackground(const float* rgb, const float* bottom) {
    for (int i = 0; i < 3; i++) {
        background[i] = rgb ? rgb[i] : -1.0f;
        backgroundBottom[i] = rgb && bottom ? bottom[i] : -1.0f;
    }
}

void Viewer::SetPreviewTexture(const GameTextureDesc& desc) {
    SetPreviewTextures({ desc });
}

void Viewer::SetPreviewTextures(const std::vector<GameTextureDesc>& textures) {
    EnsureGameCommon();
    CreatePreviewPipeline();

    Check(allocator->Reset(), "allocator Reset preview");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset preview");
    std::vector<ComPtr<ID3D12Resource>> staging;
    previewTextures.clear();
    for (std::size_t i = 0; i < textures.size() && i < PREVIEW_CAPACITY; i++) {
        const GameTextureDesc& desc = textures[i];
        uint32_t arraySize = std::max<uint32_t>(desc.arraySize, 1);
        PreviewTexture preview;
        preview.resource = CreateTexture(desc, arraySize, commandList.Get(), staging);
        preview.flags = FlagsFor(desc.format);
        preview.size[0] = desc.width;
        preview.size[1] = desc.height;
        preview.mips = std::max<uint32_t>(static_cast<uint32_t>(desc.mips.size() / arraySize), 1);
        preview.slices = arraySize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = static_cast<DXGI_FORMAT>(desc.format);
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2DArray.MipLevels = preview.mips;
        srvDesc.Texture2DArray.ArraySize = arraySize;
        device->CreateShaderResourceView(preview.resource.Get(), &srvDesc,
                                         SrvCpuHandle(SRV_PREVIEW + static_cast<uint32_t>(i)));
        previewTextures.push_back(std::move(preview));
    }

    previewUploadPending = false;
    Check(commandList->Close(), "Close preview upload");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();
}

void Viewer::SetPreviewOverlay(std::span<const ViewerOverlayRect> rects) {
    CreatePreviewPipeline();
    overlayCount = static_cast<uint32_t>(std::min<std::size_t>(rects.size(), OVERLAY_CAPACITY));
    std::memcpy(overlayMapped, rects.data(), overlayCount * sizeof(ViewerOverlayRect));
}

void Viewer::UpdatePreviewTexture(uint32_t texture, const uint8_t* pixels, uint32_t pitch) {
    if (texture >= previewTextures.size()) return;
    D3D12_RESOURCE_DESC desc = previewTextures[texture].resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 total = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
    if (!previewUpload || previewUpload->GetDesc().Width < total) {
        std::vector<uint8_t> zeros(static_cast<std::size_t>(total), 0);
        previewUpload = CreateUploadBuffer(zeros);
        D3D12_RANGE readRange{};
        Check(previewUpload->Map(0, &readRange, reinterpret_cast<void**>(&previewUploadMapped)), "Map preview upload");
    }
    // RenderFrame waits for the GPU, so the previous copy is done with the buffer.
    std::size_t copy = std::min<std::size_t>(static_cast<std::size_t>(rowBytes), pitch);
    for (UINT row = 0; row < rows; row++) {
        std::memcpy(previewUploadMapped + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                    pixels + static_cast<std::size_t>(row) * pitch, copy);
    }
    previewUploadFootprint = footprint;
    previewUploadTarget = texture;
    previewUploadPending = true;
}

void Viewer::PreviewTextureSize(uint32_t texture, uint32_t& texWidth, uint32_t& texHeight) const {
    bool valid = texture < previewTextures.size();
    texWidth = valid ? previewTextures[texture].size[0] : 0;
    texHeight = valid ? previewTextures[texture].size[1] : 0;
}

bool Viewer::PreviewRect(const ViewerImageView& view, float out[4]) const {
    if (view.texture >= previewTextures.size()) return false;
    const PreviewTexture& tex = previewTextures[view.texture];
    float regionW = std::max(std::fabs(view.source[2] - view.source[0]) * static_cast<float>(tex.size[0]), 1.0f);
    float regionH = std::max(std::fabs(view.source[3] - view.source[1]) * static_cast<float>(tex.size[1]), 1.0f);
    float scale = std::min(static_cast<float>(width) / regionW, static_cast<float>(height) / regionH) * view.zoom;
    float centerX = static_cast<float>(width) * 0.5f + view.pan[0];
    float centerY = static_cast<float>(height) * 0.5f + view.pan[1];
    out[0] = centerX - regionW * scale * 0.5f;
    out[1] = centerY - regionH * scale * 0.5f;
    out[2] = centerX + regionW * scale * 0.5f;
    out[3] = centerY + regionH * scale * 0.5f;
    return true;
}

void Viewer::CreatePreviewPipeline() {
    if (previewPso) return;

    D3D12_DESCRIPTOR_RANGE range{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = { 0, 0, sizeof(PreviewConstants) / 4 };
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = { 1, &range };
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[2].Descriptor = { 1, 0 };
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (UINT i = 0; i < 2; i++) {
        samplers[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 3;
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 2;
    rootDesc.pStaticSamplers = samplers;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
          "D3D12SerializeRootSignature preview");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&previewRootSignature)), "CreateRootSignature preview");

    auto compile = [](const char* entry, const char* target) {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> compileErrors;
        HRESULT hr = D3DCompile(PREVIEW_SOURCE, std::strlen(PREVIEW_SOURCE), nullptr, nullptr, nullptr,
                                entry, target, 0, 0, &blob, &compileErrors);
        if (FAILED(hr)) {
            std::string msg = std::string("compile preview ") + entry;
            if (compileErrors) msg += std::string(": ") + static_cast<const char*>(compileErrors->GetBufferPointer());
            throw std::runtime_error(msg);
        }
        return blob;
    };
    ComPtr<ID3DBlob> vs = compile("VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compile("PSMain", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = previewRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&previewPso)), "CreateGraphicsPipelineState preview");

    std::vector<uint8_t> overlayZeros(OVERLAY_CAPACITY * sizeof(ViewerOverlayRect), 0);
    overlayBuffer = CreateUploadBuffer(overlayZeros);
    D3D12_RANGE readRange{};
    Check(overlayBuffer->Map(0, &readRange, reinterpret_cast<void**>(&overlayMapped)), "Map preview overlay");
}

void Viewer::DrawPreviewTexture() {
    ViewerImageView view = previewView;
    if (view.texture >= previewTextures.size()) view.texture = 0;
    const PreviewTexture& tex = previewTextures[view.texture];

    PreviewConstants constants{};
    PreviewRect(view, constants.rect);
    std::copy(view.source, view.source + 4, constants.source);
    for (int i = 0; i < 3; i++) constants.background[i] = background[0] >= 0 ? background[i] : clearColor[i];
    float drawW = std::max(constants.rect[2] - constants.rect[0], 1.0f);
    float regionW = std::max(std::fabs(view.source[2] - view.source[0]) * static_cast<float>(tex.size[0]), 1.0f);
    float texelsPerPixel = regionW / drawW;
    constants.flags = tex.flags | (view.checker ? PREVIEW_CHECKER : 0) | (texelsPerPixel < 1.0f ? PREVIEW_POINT : 0);
    float maxMip = static_cast<float>(tex.mips - 1);
    constants.lod = view.mip >= 0 ? std::min(static_cast<float>(view.mip), maxMip)
                                  : std::min(texelsPerPixel > 1.0f ? std::log2(texelsPerPixel) : 0.0f, maxMip);
    constants.channels = view.channels;
    constants.overlayCount = overlayCount;
    constants.slice = static_cast<float>(std::min(view.slice, tex.slices - 1));

    if (previewUploadPending) {
        previewUploadPending = false;
        ID3D12Resource* target = previewTextures[previewUploadTarget].resource.Get();
        constexpr D3D12_RESOURCE_STATES SHADER_READ =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_BARRIER toCopy = Transition(target, SHADER_READ, D3D12_RESOURCE_STATE_COPY_DEST);
        commandList->ResourceBarrier(1, &toCopy);
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = target;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = previewUpload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = previewUploadFootprint;
        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER toRead = Transition(target, D3D12_RESOURCE_STATE_COPY_DEST, SHADER_READ);
        commandList->ResourceBarrier(1, &toRead);
    }

    ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(previewRootSignature.Get());
    commandList->SetPipelineState(previewPso.Get());
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(PreviewConstants) / 4, &constants, 0);
    commandList->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(SRV_PREVIEW + view.texture));
    commandList->SetGraphicsRootShaderResourceView(2, overlayBuffer->GetGPUVirtualAddress());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}
