#include "Renderer/Viewer.h"

#include <algorithm>
#include <cstring>
#include <d3dcompiler.h>
#include <string_view>

#include "D3D12Utils.h"
#include "DxilBindings.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr std::string_view ROOT_BUFFERS[] = { "InstanceWorldInfo", "IBLCubemapArrayList2SRV", "LightCullingListSRV", "BSPTree",
                                              "TetraCoordinate", "IndirectProbe", "BindlessRedirectTbl", "BindlessBuffer",
                                              "SkinningMatrices", "LightParameterSRV" };

struct BlurConstants {
    float srcTexel[2];
    uint32_t addAux;
    uint32_t pad;
};

// Solid::createBlurTextures' chain, box filtered: mip 0 is the lit scene, each level a 4x4 tent of the one above.
const char* BLUR_SOURCE = R"(
Texture2D source : register(t0);
Texture2D aux : register(t1);
SamplerState linearClamp : register(s0);
cbuffer Constants : register(b0) {
    float2 srcTexel;
    uint addAux;
};

float4 VSMain(uint id : SV_VertexID, out float2 uv : TEXCOORD0) : SV_Position {
    uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    if (addAux != 0) return source.SampleLevel(linearClamp, uv, 0) + aux.SampleLevel(linearClamp, uv, 0);
    float4 sum = source.SampleLevel(linearClamp, uv + float2(-srcTexel.x, -srcTexel.y), 0);
    sum += source.SampleLevel(linearClamp, uv + float2(srcTexel.x, -srcTexel.y), 0);
    sum += source.SampleLevel(linearClamp, uv + float2(-srcTexel.x, srcTexel.y), 0);
    sum += source.SampleLevel(linearClamp, uv + float2(srcTexel.x, srcTexel.y), 0);
    return sum * 0.25;
}
)";

D3D12_RESOURCE_BARRIER MipTransition(ID3D12Resource* resource, UINT mip, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = Transition(resource, before, after);
    barrier.Transition.Subresource = mip;
    return barrier;
}

}

Viewer::ForwardPipeline Viewer::BuildForwardPipeline(const GameDeferredDesc& desc) {
    ForwardPipeline pipe;
    std::vector<D3D12_ROOT_PARAMETER> params;
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    ranges.reserve(64);

    auto addStage = [&](std::span<const uint8_t> blob, const std::vector<GameComputeSlot>& slots, D3D12_SHADER_VISIBILITY visibility) {
        auto slotName = [&](GameComputeSlot::Kind kind, const DxilBinding& b) -> std::string {
            if (b.space != 0) return {};
            for (const GameComputeSlot& slot : slots) {
                if (slot.kind == kind && slot.reg == b.reg) return slot.name;
            }
            return {};
        };
        for (const DxilBinding& b : ParseDxilBindings(blob)) {
            if (b.type == 1) {
                D3D12_STATIC_SAMPLER_DESC sampler{};
                sampler.Filter = D3D12_FILTER_ANISOTROPIC;
                sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                for (const GameComputeSlot& slot : slots) {
                    if (slot.kind == GameComputeSlot::Kind::Sampler && slot.reg == b.reg && b.space == 0) {
                        sampler.Filter = slot.filter;
                        sampler.AddressU = slot.address;
                    }
                }
                sampler.AddressV = sampler.AddressW = sampler.AddressU;
                sampler.MaxAnisotropy = 16;
                sampler.MaxLOD = D3D12_FLOAT32_MAX;
                sampler.ShaderRegister = b.reg;
                sampler.RegisterSpace = b.space;
                sampler.ShaderVisibility = visibility;
                samplers.push_back(sampler);
                continue;
            }
            D3D12_ROOT_PARAMETER param{};
            param.ShaderVisibility = visibility;
            ForwardBind bind{};
            if (b.type == 2 && b.space == 32) {
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                param.Constants = { b.reg, b.space, 1 };
                bind.kind = ForwardBind::Constant;
                pipe.rootConstParams.push_back(static_cast<uint32_t>(params.size()));
            } else if (b.type == 2) {
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                param.Descriptor = { b.reg, b.space };
                bind = { ForwardBind::Cbv, slotName(GameComputeSlot::Kind::Cbv, b) };
            } else if (b.type == 4 || b.type == 5) {
                std::string name = slotName(GameComputeSlot::Kind::Srv, b);
                if (std::find(std::begin(ROOT_BUFFERS), std::end(ROOT_BUFFERS), name) != std::end(ROOT_BUFFERS)) {
                    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
                    param.Descriptor = { b.reg, b.space };
                    bind = { ForwardBind::RootSrv, name };
                } else {
                    ranges.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, b.reg, b.space, 0 });
                    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    param.DescriptorTable = { 1, &ranges.back() };
                    bind = { ForwardBind::Table, name, SRV_NULL_BUFFERS + (b.type == 5 ? 1u : 0u) };
                }
            } else if (b.type == 3) {
                bool bindless = b.space == 4 || b.space == 5 || b.space == 7;
                ranges.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, bindless ? UINT_MAX : 1, b.reg, b.space, 0 });
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.DescriptorTable = { 1, &ranges.back() };
                uint32_t base = b.space == 4 ? SRV_BINDLESS_BASE : b.space == 5 ? SRV_BINDLESS_ARRAY_BASE : SRV_BINDLESS_CUBE_BASE;
                bind = bindless ? ForwardBind{ ForwardBind::Table, {}, base } : ForwardBind{ ForwardBind::Texture, slotName(GameComputeSlot::Kind::Srv, b) };
            } else {
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
                param.Descriptor = { b.reg, b.space };
                bind.kind = ForwardBind::Uav;
            }
            params.push_back(param);
            pipe.binds.push_back(std::move(bind));
        }
    };
    addStage(desc.vs, desc.vsSlots, D3D12_SHADER_VISIBILITY_VERTEX);
    addStage(desc.ps, desc.psSlots, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(params.size());
    rootDesc.pParameters = params.data();
    rootDesc.NumStaticSamplers = static_cast<UINT>(samplers.size());
    rootDesc.pStaticSamplers = samplers.data();
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors))) {
        throw std::runtime_error(std::string("forward root signature: ") + (errors ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
    }
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pipe.root)),
          "CreateRootSignature forward");

    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
    for (const GameInputElement& e : desc.inputLayout) {
        inputLayout.push_back({ e.semanticName, e.semanticIndex, e.format, e.slot, e.offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = pipe.root.Get();
    psoDesc.VS = { desc.vs.data(), desc.vs.size() };
    psoDesc.PS = { desc.ps.data(), desc.ps.size() };
    psoDesc.InputLayout = { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };

    uint32_t raster;
    std::memcpy(&raster, desc.rasterizerState.data(), 4);
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = (raster & 0x100) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = (raster >> 10) & 1;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    uint32_t w;
    std::memcpy(&w, desc.blendState.data(), 4);
    psoDesc.BlendState.IndependentBlendEnable = TRUE;
    D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[0];
    rt.BlendEnable = (w & 1) ? TRUE : FALSE;
    rt.SrcBlend = (w & 1) ? static_cast<D3D12_BLEND>((w >> 1) & 0x1F) : D3D12_BLEND_ONE;
    rt.DestBlend = (w & 1) ? static_cast<D3D12_BLEND>((w >> 6) & 0x1F) : D3D12_BLEND_ZERO;
    rt.BlendOp = static_cast<D3D12_BLEND_OP>(((w >> 11) & 0x7) ? (w >> 11) & 0x7 : 1);
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.RTVFormats[0] = HDR_FORMAT;
    psoDesc.NumRenderTargets = 1;
    // The resolve adds DeferredLight's diffuse target: an opaque forward pixel clears it.
    if (lightingRtCount > 1) {
        D3D12_RENDER_TARGET_BLEND_DESC& aux = psoDesc.BlendState.RenderTarget[1];
        aux.BlendEnable = (w & 1) ? FALSE : TRUE;
        aux.SrcBlend = D3D12_BLEND_ZERO;
        aux.DestBlend = (w & 1) ? D3D12_BLEND_ONE : D3D12_BLEND_ZERO;
        aux.BlendOp = D3D12_BLEND_OP_ADD;
        aux.SrcBlendAlpha = D3D12_BLEND_ZERO;
        aux.DestBlendAlpha = D3D12_BLEND_ZERO;
        aux.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        aux.LogicOp = D3D12_LOGIC_OP_NOOP;
        aux.RenderTargetWriteMask = (w & 1) ? 0 : D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.RTVFormats[1] = HDR_FORMAT;
        psoDesc.NumRenderTargets = 2;
    }
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipe.pso)), "CreateGraphicsPipelineState forward");
    return pipe;
}

void Viewer::CreateBlurredSolid() {
    blurredSolid.Reset();
    if (width == 0 || height == 0) return;
    blurredMips = 1;
    while (blurredMips < BLURRED_MAX_MIPS && ((width >> blurredMips) > 0 || (height >> blurredMips) > 0)) blurredMips++;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = static_cast<UINT16>(blurredMips);
    desc.Format = HDR_FORMAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                          IID_PPV_ARGS(&blurredSolid)), "CreateCommittedResource blurred solid");
    blurredSolid->SetName(L"BlurredSolid");
    if (!blurredRtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = BLURRED_MAX_MIPS;
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&blurredRtvHeap)), "CreateDescriptorHeap blurred RTV");
    }
    device->CreateShaderResourceView(blurredSolid.Get(), nullptr, SrvCpuHandle(SRV_BLURRED_SOLID));
    for (uint32_t mip = 0; mip < blurredMips; mip++) {
        D3D12_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = HDR_FORMAT;
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtv.Texture2D.MipSlice = mip;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = blurredRtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += mip * static_cast<SIZE_T>(rtvStride);
        device->CreateRenderTargetView(blurredSolid.Get(), &rtv, handle);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = HDR_FORMAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MostDetailedMip = mip;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(blurredSolid.Get(), &srv, SrvCpuHandle(SRV_BLURRED_MIPS + mip));
    }
}

void Viewer::CreateBlurPipeline() {
    if (blurPso) return;
    D3D12_DESCRIPTOR_RANGE sourceRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
    D3D12_DESCRIPTOR_RANGE auxRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 };
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = { 0, 0, sizeof(BlurConstants) / 4 };
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = { 1, &sourceRange };
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable = { 1, &auxRange };
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 3;
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors), "D3D12SerializeRootSignature blur");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&blurRoot)),
          "CreateRootSignature blur");
    auto compile = [](const char* entry, const char* target) {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> compileErrors;
        if (FAILED(D3DCompile(BLUR_SOURCE, std::strlen(BLUR_SOURCE), nullptr, nullptr, nullptr, entry, target, 0, 0, &blob, &compileErrors))) {
            throw std::runtime_error(std::string("compile blur: ") +
                                     (compileErrors ? static_cast<const char*>(compileErrors->GetBufferPointer()) : ""));
        }
        return blob;
    };
    ComPtr<ID3DBlob> vs = compile("VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compile("PSMain", "ps_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = blurRoot.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = HDR_FORMAT;
    psoDesc.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&blurPso)), "CreateGraphicsPipelineState blur");
}

void Viewer::BuildBlurredSolid() {
    CreateBlurPipeline();
    commandList->SetGraphicsRootSignature(blurRoot.Get());
    commandList->SetPipelineState(blurPso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    for (uint32_t mip = 0; mip < blurredMips; mip++) {
        UINT mipWidth = std::max<UINT>(1, width >> mip);
        UINT mipHeight = std::max<UINT>(1, height >> mip);
        D3D12_RESOURCE_BARRIER toRt = MipTransition(blurredSolid.Get(), mip, read, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &toRt);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = blurredRtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += mip * static_cast<SIZE_T>(rtvStride);
        commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(mipWidth), static_cast<float>(mipHeight), 0, 1 };
        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(mipWidth), static_cast<LONG>(mipHeight) };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);
        BlurConstants constants{};
        if (mip == 0) {
            constants.addAux = 1;
            commandList->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(SRV_HDR));
            commandList->SetGraphicsRootDescriptorTable(2, SrvGpuHandle(lightingRtCount > 1 && hdrAuxTarget ? SRV_HDR_AUX : SRV_LIGHT_BLACK));
        } else {
            constants.srcTexel[0] = 1.0f / static_cast<float>(std::max<UINT>(1, width >> (mip - 1)));
            constants.srcTexel[1] = 1.0f / static_cast<float>(std::max<UINT>(1, height >> (mip - 1)));
            commandList->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(SRV_BLURRED_MIPS + mip - 1));
            commandList->SetGraphicsRootDescriptorTable(2, SrvGpuHandle(SRV_LIGHT_BLACK));
        }
        commandList->SetGraphicsRoot32BitConstants(0, sizeof(BlurConstants) / 4, &constants, 0);
        commandList->DrawInstanced(3, 1, 0, 0);
        D3D12_RESOURCE_BARRIER toRead = MipTransition(blurredSolid.Get(), mip, D3D12_RESOURCE_STATE_RENDER_TARGET, read);
        commandList->ResourceBarrier(1, &toRead);
    }
    D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
    D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
}

// The ForwardSolid segment: after DeferredLighting, over the lit scene, depth tested and not written.
void Viewer::DrawForward(bool culled, uint32_t& drawn, uint64_t& triangles) {
    bool any = false;
    for (std::size_t di = 0; di < draws.size() && !any; di++) {
        any = draws[di].pipelineIndex < forwardPipelines.size() && forwardPipelines[draws[di].pipelineIndex].pso &&
              (drawVisible.empty() || drawVisible[di]);
    }
    if (!any || !blurredSolid) return;
    BuildBlurredSolid();

    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES depthRead = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    bool aux = lightingRtCount > 1 && hdrAuxTarget;
    D3D12_RESOURCE_BARRIER before[3] = {
        Transition(depthBuffer.Get(), read, depthRead),
        Transition(hdrTarget.Get(), read, D3D12_RESOURCE_STATE_RENDER_TARGET),
        Transition(aux ? hdrAuxTarget.Get() : nullptr, read, D3D12_RESOURCE_STATE_RENDER_TARGET),
    };
    commandList->ResourceBarrier(aux ? 3 : 2, before);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
    rtvs[0] = hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvs[1] = { rtvs[0].ptr + rtvStride };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    dsv.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    commandList->OMSetRenderTargets(aux ? 2 : 1, rtvs, FALSE, &dsv);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW views[] = { positionView, normalView, uv0View, uv1View, weightsView };
    commandList->IASetVertexBuffers(0, 5, views);
    commandList->IASetIndexBuffer(&indexView);

    auto buffer = [&](const std::string& n) -> ID3D12Resource* {
        ID3D12Resource* found = n == "InstanceWorldInfo"                           ? instanceBuffer.Get()
                              : n == "IBLCubemapArrayList2SRV"                     ? localCubemapRecords.Get()
                              : n == "LightCullingListSRV"                         ? lightListBuffer.Get()
                              : n == "BSPTree"                                     ? probeBspTree.Get()
                              : n == "TetraCoordinate"                             ? probeBuffers[0].Get()
                              : n == "IndirectProbe"                               ? probeBuffers[1].Get()
                              : n == "BindlessRedirectTbl"                         ? redirectBuffer.Get()
                              : n == "BindlessBuffer"                              ? bindlessDataBuffer.Get()
                              : n == "SkinningMatrices"                            ? skinningBuffer.Get()
                              : n == "LightParameterSRV"                           ? lightParamsBuffer.Get()
                                                                                   : nullptr;
        return found ? found : zeroBuffer.Get();
    };
    auto texture = [&](const std::string& n) -> uint32_t {
        bool volume = n == "AerialPerspectiveTexture" || n == "TransmittanceFromCameraTexture" || n == "VolumetricFogTexture";
        return n == "ReadonlyDepth"          ? SRV_LIGHT_DEPTH
             : n == "LightCullingVolumeSRV"  ? SRV_LIGHT_VOLUME
             : n == "AmbientBRDF"            ? SRV_AMBIENT_BRDF
             : n == "IBLCubemap2DArraySRV"   ? SRV_INDIRECT_CUBEMAP_ARRAY
             : n == "CubemapSRV"             ? SRV_INDIRECT_CUBEMAP
             : n == "BlurredSolid"           ? SRV_BLURRED_SOLID
             : n == "BlueNoise16"            ? SRV_BLUE_NOISE
             : volume && fogBlackVolume      ? SRV_FOG_BASE + FOG_VIEW_BLACK_VOLUME
                                             : SRV_LIGHT_BLACK;
    };

    uint32_t current = UINT32_MAX;
    for (std::size_t di = 0; di < draws.size(); di++) {
        const ViewerMeshDraw& draw = draws[di];
        if (draw.pipelineIndex >= forwardPipelines.size() || !forwardPipelines[draw.pipelineIndex].pso) continue;
        if (!drawVisible.empty() && !drawVisible[di]) continue;
        if (culled && cullFlagsCpu[di] == 2) continue;
        const ForwardPipeline& pipe = forwardPipelines[draw.pipelineIndex];
        if (draw.pipelineIndex != current) {
            current = draw.pipelineIndex;
            commandList->SetGraphicsRootSignature(pipe.root.Get());
            commandList->SetPipelineState(pipe.pso.Get());
            for (UINT i = 0; i < pipe.binds.size(); i++) {
                const ForwardBind& bind = pipe.binds[i];
                const std::string& n = bind.name;
                switch (bind.kind) {
                    case ForwardBind::Cbv: {
                        D3D12_GPU_VIRTUAL_ADDRESS address = n == "SceneInfo"          ? sceneInfoBuffer->GetGPUVirtualAddress()
                                                          : n == "EnvironmentInfo"    ? environmentBuffer->GetGPUVirtualAddress()
                                                          : n == "CheckerBoardInfo"   ? checkerBoardBuffer->GetGPUVirtualAddress()
                                                          : n == "LightInfo"          ? lightInfoBuffer->GetGPUVirtualAddress()
                                                          : n == "ShadowSamplingRotation" && shadowRotationBuffer
                                                              ? shadowRotationBuffer->GetGPUVirtualAddress()
                                                              : zeroBuffer->GetGPUVirtualAddress();
                        commandList->SetGraphicsRootConstantBufferView(i, address);
                        break;
                    }
                    case ForwardBind::RootSrv:
                        commandList->SetGraphicsRootShaderResourceView(i, buffer(n)->GetGPUVirtualAddress());
                        break;
                    case ForwardBind::Table:
                        commandList->SetGraphicsRootDescriptorTable(i, SrvGpuHandle(bind.data));
                        break;
                    case ForwardBind::Texture:
                        commandList->SetGraphicsRootDescriptorTable(i, SrvGpuHandle(texture(n)));
                        break;
                    case ForwardBind::Uav:
                        commandList->SetGraphicsRootUnorderedAccessView(i, scratchUavBuffer->GetGPUVirtualAddress());
                        break;
                    case ForwardBind::Constant:
                        break;
                }
            }
        }
        for (uint32_t paramIndex : pipe.rootConstParams) {
            commandList->SetGraphicsRoot32BitConstant(paramIndex, draw.instanceIndex | (draw.materialSlot << 24), 0);
        }
        commandList->DrawIndexedInstanced(draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
        drawn++;
        triangles += draw.indexCount / 3;
    }

    D3D12_RESOURCE_BARRIER after[3];
    for (int i = 0; i < 3; i++) {
        after[i] = before[i];
        std::swap(after[i].Transition.StateBefore, after[i].Transition.StateAfter);
    }
    commandList->ResourceBarrier(aux ? 3 : 2, after);
}
