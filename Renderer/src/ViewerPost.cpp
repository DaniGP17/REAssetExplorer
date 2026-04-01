#include "Renderer/Viewer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "D3D12Utils.h"
#include "DxilBindings.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT POST_CB_STRIDE = 256;
enum PostConstants : uint32_t {
    POST_CB_TONEMAP_PARAM,
    POST_CB_KERARE,
    POST_CB_COLOR_CORRECT,
    POST_CB_OUTPUT,
    POST_CB_CAS,
    POST_CB_ROOT_CONSTANT,
    POST_CB_HISTOGRAM,
    POST_CB_BLOOM,
    POST_CB_BLOOM_OUTPUT,
    POST_CB_CONE,
    POST_CB_BLOOM_SCALE,
    POST_CB_COUNT,
};
enum PostRtvs : uint32_t {
    POST_RTV_SCENE,
    POST_RTV_LDR_A,
    POST_RTV_LDR_B,
    POST_RTV_TAA,
    POST_RTV_BLOOM,
    POST_RTV_COUNT = POST_RTV_BLOOM + 7,
};

float RadicalInverse(uint32_t i, uint32_t base) {
    float inverse = 1.0f / static_cast<float>(base);
    float scale = inverse;
    float value = 0;
    for (; i > 0; i /= base, scale *= inverse) value += static_cast<float>(i % base) * scale;
    return value;
}
// HistogramCS: 8x8 threads of 2x2 samples at every other pixel.
constexpr UINT HISTOGRAM_TILE = 16;
constexpr UINT HISTOGRAM_BYTES = 1024 * 4;
// CAS_CS processes 16x16 pixels per group (the RE8 capture dispatches 120x68 at 1920x1080).
constexpr UINT CAS_TILE = 16;
constexpr DXGI_FORMAT LDR_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;
constexpr DXGI_FORMAT OUTPUT_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

}

ComPtr<ID3D12RootSignature> Viewer::CreatePassRoot(std::span<const PassStage> stages, std::vector<PassBind>& binds,
                                                   bool inputLayout) {
    binds.clear();
    std::vector<D3D12_ROOT_PARAMETER> params;
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    std::size_t total = 0;
    std::vector<std::vector<DxilBinding>> stageBindings;
    for (const PassStage& stage : stages) {
        stageBindings.push_back(ParseDxilBindings(stage.desc->cs));
        total += stageBindings.back().size();
    }
    ranges.reserve(total);
    for (std::size_t s = 0; s < stages.size(); s++) {
        const PassStage& stage = stages[s];
        // The shader's slots are space 0; space 32 holds the engine's root constants.
        auto slotOf = [&](GameComputeSlot::Kind kind, const DxilBinding& binding) -> const GameComputeSlot* {
            if (binding.space != 0) return nullptr;
            for (const GameComputeSlot& slot : stage.desc->slots) {
                if (slot.kind == kind && slot.reg == binding.reg) return &slot;
            }
            return nullptr;
        };
        for (const DxilBinding& b : stageBindings[s]) {
            if (b.type == 1) {
                const GameComputeSlot* slot = slotOf(GameComputeSlot::Kind::Sampler, b);
                D3D12_STATIC_SAMPLER_DESC sampler{};
                sampler.Filter = slot ? slot->filter : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                sampler.AddressU = sampler.AddressV = sampler.AddressW = slot ? slot->address : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                sampler.ComparisonFunc = slot ? slot->comparison : D3D12_COMPARISON_FUNC_NEVER;
                sampler.MaxAnisotropy = 16;
                sampler.MaxLOD = D3D12_FLOAT32_MAX;
                sampler.ShaderRegister = b.reg;
                sampler.RegisterSpace = b.space;
                sampler.ShaderVisibility = stage.visibility;
                samplers.push_back(sampler);
                continue;
            }
            D3D12_ROOT_PARAMETER param{};
            param.ShaderVisibility = stage.visibility;
            PassBind bind{};
            if (b.type == 2) {
                const GameComputeSlot* slot = slotOf(GameComputeSlot::Kind::Cbv, b);
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                param.Descriptor = { b.reg, b.space };
                bind = { PassBind::Cbv, slot ? slot->name : b.space == 32 ? std::string("RootConstant") : std::string() };
            } else if (b.type == 4 || b.type == 5) {
                const GameComputeSlot* slot = slotOf(GameComputeSlot::Kind::Srv, b);
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
                param.Descriptor = { b.reg, b.space };
                bind = { PassBind::RootSrv, slot ? slot->name : std::string() };
            } else {
                bool uav = b.type >= 6;
                const GameComputeSlot* slot = slotOf(uav ? GameComputeSlot::Kind::Uav : GameComputeSlot::Kind::Srv, b);
                ranges.push_back({ uav ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, b.reg, b.space, 0 });
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.DescriptorTable = { 1, &ranges.back() };
                bind = { PassBind::Table, slot ? slot->name : std::string() };
            }
            params.push_back(param);
            binds.push_back(bind);
        }
    }
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(params.size());
    rootDesc.pParameters = params.data();
    rootDesc.NumStaticSamplers = static_cast<UINT>(samplers.size());
    rootDesc.pStaticSamplers = samplers.data();
    if (inputLayout) rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors))) {
        throw std::runtime_error(std::string("pass root signature: ") +
                                 (errors ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
    }
    ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)),
          "CreateRootSignature pass");
    return root;
}

void Viewer::CreatePostProcess(const GamePostProcessDesc& desc) {
    EnsureGameCommon();
    auto graphics = [&](PostPass& pass, const GameComputeDesc& vs, const GameComputeDesc& ps, DXGI_FORMAT format, const char* what,
                        bool additive = false) {
        const PassStage stages[] = { { &vs, D3D12_SHADER_VISIBILITY_VERTEX }, { &ps, D3D12_SHADER_VISIBILITY_PIXEL } };
        pass.root = CreatePassRoot(stages, pass.binds);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = pass.root.Get();
        psoDesc.VS = { vs.cs.data(), vs.cs.size() };
        psoDesc.PS = { ps.cs.data(), ps.cs.size() };
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (additive) {
            D3D12_RENDER_TARGET_BLEND_DESC& blend = psoDesc.BlendState.RenderTarget[0];
            blend.BlendEnable = TRUE;
            blend.SrcBlend = blend.DestBlend = blend.SrcBlendAlpha = blend.DestBlendAlpha = D3D12_BLEND_ONE;
            blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = format;
        psoDesc.SampleDesc.Count = 1;
        Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pass.pso.ReleaseAndGetAddressOf())), what);
    };
    graphics(ldrPass, desc.ldrVs, desc.ldrPs, LDR_FORMAT, "CreateGraphicsPipelineState LwLDRPostProcess");
    graphics(fxaaPass, desc.fxaaVs, desc.fxaaPs, LDR_FORMAT, "CreateGraphicsPipelineState FXAA");
    graphics(outputPass, desc.outputVs, desc.outputPs, OUTPUT_FORMAT, "CreateGraphicsPipelineState ScreenOutput");
    graphics(bloomReductionPass, desc.bloomVs, desc.bloomReduction, HDR_FORMAT, "CreateGraphicsPipelineState NewReduction");
    graphics(bloomFilterPass, desc.bloomVs, desc.bloomFilter, HDR_FORMAT, "CreateGraphicsPipelineState NewFilter");
    for (int i = 1; i < 7; i++) {
        graphics(bloomBlendingPass[i], desc.bloomVs, desc.bloomBlending[i], HDR_FORMAT, "CreateGraphicsPipelineState NewBlending", true);
    }
    graphics(bloomFinalPass, desc.bloomVs, desc.bloomFinal, HDR_FORMAT, "CreateGraphicsPipelineState NewFinal", true);
    temporalPass = {};
    if (!desc.temporalPs.cs.empty()) {
        graphics(temporalPass, desc.temporalVs, desc.temporalPs, HDR_FORMAT, "CreateGraphicsPipelineState PreTonemap");
    }
    bloomFinalPass.bloomOutput = true;
    auto compute = [&](PostPass& pass, const GameComputeDesc& cs, const char* what) {
        const PassStage stage[] = { { &cs, D3D12_SHADER_VISIBILITY_ALL } };
        pass.root = CreatePassRoot(stage, pass.binds);
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = pass.root.Get();
        psoDesc.CS = { cs.cs.data(), cs.cs.size() };
        Check(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(pass.pso.ReleaseAndGetAddressOf())), what);
    };
    compute(casPass, desc.cas, "CreateComputePipelineState CAS");
    compute(histogramPass, desc.histogram, "CreateComputePipelineState Histogram");
    compute(whitePointPass, desc.whitePoint, "CreateComputePipelineState WhitePoint");
    if (!postConstants) {
        postConstants = CreateUploadBuffer(std::vector<uint8_t>(POST_CB_STRIDE * POST_CB_COUNT, 0));
        D3D12_RANGE none{};
        Check(postConstants->Map(0, &none, reinterpret_cast<void**>(&postConstantsMapped)), "Map post constants");
    }
    hasPostProcess = true;
    CreatePostTargets();
}

void Viewer::SetPostProcess(const ViewerPostProcessParams& p) {
    if (!postConstantsMapped) return;
    std::memcpy(postConstantsMapped + POST_CB_TONEMAP_PARAM * POST_CB_STRIDE, p.tonemapParam, sizeof(p.tonemapParam));
    std::memcpy(postConstantsMapped + POST_CB_KERARE * POST_CB_STRIDE, p.cameraKerare, sizeof(p.cameraKerare));
    std::memcpy(postConstantsMapped + POST_CB_COLOR_CORRECT * POST_CB_STRIDE, p.colorCorrect, sizeof(p.colorCorrect));
    std::memcpy(postConstantsMapped + POST_CB_OUTPUT * POST_CB_STRIDE, p.outputColorAdjustment, sizeof(p.outputColorAdjustment));
    std::memcpy(postConstantsMapped + POST_CB_CAS * POST_CB_STRIDE, p.cas, sizeof(p.cas));
    for (int i = 0; i < 3; i++) colorCubeSlots[i] = p.colorCubes[i] < colorCubes.size() ? p.colorCubes[i] : 0;
}

void Viewer::LoadColorCubes(std::span<const GameTextureDesc> cubes) {
    EnsureGameCommon();
    Check(allocator->Reset(), "allocator Reset color cubes");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset color cubes");
    std::vector<ComPtr<ID3D12Resource>> staging;
    std::size_t count = std::min<std::size_t>(cubes.size(), POST_VIEW_COUNT - POST_VIEW_CUBES);
    colorCubes.clear();
    for (std::size_t i = 0; i < count; i++) colorCubes.push_back(CreateTexture(cubes[i], 1, commandList.Get(), staging));
    Check(commandList->Close(), "Close color cubes");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();
    for (std::size_t i = 0; i < count; i++) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = static_cast<DXGI_FORMAT>(cubes[i].format);
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture3D.MipLevels = static_cast<UINT>(cubes[i].mips.size());
        device->CreateShaderResourceView(colorCubes[i].Get(), &srv, SrvCpuHandle(SRV_POST_BASE + POST_VIEW_CUBES + static_cast<uint32_t>(i)));
    }
}

void Viewer::SetTemporalAA(const ViewerTemporalAA& params) {
    temporalAASource = params;
    ViewerTemporalAA effective = params;
    effective.enabled = params.enabled && temporalAAAllowed;
    if (effective.enabled && !temporalAA.enabled) temporalCut = true;
    temporalAA = effective;
}

void Viewer::SetTemporalAAAllowed(bool on) {
    temporalAAAllowed = on;
    SetTemporalAA(temporalAASource);
}

void Viewer::SetSoftBloom(const ViewerSoftBloomParams& p) {
    if (!postConstantsMapped) return;
    std::memcpy(postConstantsMapped + POST_CB_BLOOM * POST_CB_STRIDE, p.reduction, sizeof(p.reduction));
    std::memcpy(postConstantsMapped + POST_CB_BLOOM_OUTPUT * POST_CB_STRIDE, p.output, sizeof(p.output));
    std::memcpy(postConstantsMapped + POST_CB_CONE * POST_CB_STRIDE, p.cone, sizeof(p.cone));
    std::memcpy(postConstantsMapped + POST_CB_BLOOM_SCALE * POST_CB_STRIDE, p.scale, sizeof(p.scale));
    bloomLevels = bloomFinalPass.pso ? std::min<uint32_t>(p.levels, 7) : 0;
}

void Viewer::LoadMeteringTexture(const GameTextureDesc& texture) {
    EnsureGameCommon();
    Check(allocator->Reset(), "allocator Reset metering");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset metering");
    std::vector<ComPtr<ID3D12Resource>> staging;
    meteringTexture = CreateTexture(texture, 1, commandList.Get(), staging);
    Check(commandList->Close(), "Close metering");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = static_cast<DXGI_FORMAT>(texture.format);
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = static_cast<UINT>(texture.mips.size());
    device->CreateShaderResourceView(meteringTexture.Get(), &srv, SrvCpuHandle(SRV_POST_BASE + POST_VIEW_METERING));
}

void Viewer::CreatePostTargets() {
    if (!hasPostProcess || width == 0 || height == 0) return;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    auto create = [&](ComPtr<ID3D12Resource>& out, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, const wchar_t* name,
                      uint32_t level = 0) {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = std::max<uint32_t>(width >> level, 1);
        desc.Height = std::max<uint32_t>(height >> level, 1);
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = flags;
        Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                              IID_PPV_ARGS(out.ReleaseAndGetAddressOf())), "CreateCommittedResource post target");
        out->SetName(name);
    };
    create(sceneHdrTarget, HDR_FORMAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, L"PostSceneHdr");
    create(ldrTargets[0], LDR_FORMAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"PostLdrA");
    create(ldrTargets[1], LDR_FORMAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, L"PostLdrB");
    for (uint32_t i = 0; i < 7; i++) create(bloomTargets[i], HDR_FORMAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, L"PostBloom", i + 1);
    create(taaTarget, HDR_FORMAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, L"PostTemporalAA");
    create(historyTarget, HDR_FORMAT, D3D12_RESOURCE_FLAG_NONE, L"PostTemporalHistory");
    temporalCut = true;
    if (!postRtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = POST_RTV_COUNT;
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&postRtvHeap)), "CreateDescriptorHeap post RTV");
    }
    ID3D12Resource* rts[POST_RTV_COUNT] = { sceneHdrTarget.Get(), ldrTargets[0].Get(), ldrTargets[1].Get(), taaTarget.Get() };
    device->CreateShaderResourceView(taaTarget.Get(), nullptr, SrvCpuHandle(SRV_POST_BASE + POST_VIEW_TAA_SRV));
    device->CreateShaderResourceView(historyTarget.Get(), nullptr, SrvCpuHandle(SRV_POST_BASE + POST_VIEW_HISTORY_SRV));
    for (uint32_t i = 0; i < 7; i++) {
        rts[POST_RTV_BLOOM + i] = bloomTargets[i].Get();
        device->CreateShaderResourceView(bloomTargets[i].Get(), nullptr, SrvCpuHandle(SRV_POST_BASE + POST_VIEW_BLOOM + i));
    }
    for (uint32_t i = 0; i < POST_RTV_COUNT; i++) device->CreateRenderTargetView(rts[i], nullptr, PostRtv(i));
    device->CreateShaderResourceView(sceneHdrTarget.Get(), nullptr, SrvCpuHandle(SRV_POST_BASE + POST_VIEW_SCENE_SRV));
    device->CreateShaderResourceView(ldrTargets[0].Get(), nullptr, SrvCpuHandle(SRV_POST_BASE + POST_VIEW_LDR_A_SRV));
    device->CreateUnorderedAccessView(ldrTargets[0].Get(), nullptr, nullptr, SrvCpuHandle(SRV_POST_BASE + POST_VIEW_LDR_A_UAV));
    device->CreateShaderResourceView(ldrTargets[1].Get(), nullptr, SrvCpuHandle(SRV_POST_BASE + POST_VIEW_LDR_B_SRV));
}

D3D12_CPU_DESCRIPTOR_HANDLE Viewer::PostRtv(uint32_t index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = postRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * rtvStride;
    return handle;
}

bool Viewer::PostProcessActive(bool studio) const {
    return hasPostProcess && showFlags.postProcess && sceneHdrTarget && !colorCubes.empty() && !unlit && !wireframe && !studio &&
           gbufferView == GBufferView::None && background[0] < 0;
}

void Viewer::BindPostPass(const PostPass& pass, bool compute, uint32_t sourceView) {
    D3D12_GPU_VIRTUAL_ADDRESS constants = postConstants->GetGPUVirtualAddress();
    auto constantAddress = [&](const std::string& n) -> D3D12_GPU_VIRTUAL_ADDRESS {
        if (n == "SceneInfo") return sceneInfoBuffer->GetGPUVirtualAddress();
        if (n == "Tonemap") return tonemapBuffer->GetGPUVirtualAddress();
        if (n == "TonemapParam") return constants + POST_CB_TONEMAP_PARAM * POST_CB_STRIDE;
        if (n == "CameraKerare") return constants + POST_CB_KERARE * POST_CB_STRIDE;
        if (n == "ColorCorrectTexture") return constants + POST_CB_COLOR_CORRECT * POST_CB_STRIDE;
        if (n == "OutputColorAdjustment") return constants + POST_CB_OUTPUT * POST_CB_STRIDE;
        if (n == "cbCAS") return constants + POST_CB_CAS * POST_CB_STRIDE;
        if (n == "RootConstant") return constants + POST_CB_ROOT_CONSTANT * POST_CB_STRIDE;
        if (n == "HitogramAdjustment") return constants + POST_CB_HISTOGRAM * POST_CB_STRIDE;
        if (n == "cbSoftBloom") return constants + (pass.bloomOutput ? POST_CB_BLOOM_OUTPUT : POST_CB_BLOOM) * POST_CB_STRIDE;
        if (n == "cbCone") return constants + POST_CB_CONE * POST_CB_STRIDE;
        if (n == "cbSoftBloomScale") return constants + POST_CB_BLOOM_SCALE * POST_CB_STRIDE;
        return zeroBuffer->GetGPUVirtualAddress();
    };
    for (UINT i = 0; i < pass.binds.size(); i++) {
        const PassBind& b = pass.binds[i];
        if (b.kind == PassBind::Cbv) {
            if (compute) commandList->SetComputeRootConstantBufferView(i, constantAddress(b.name));
            else commandList->SetGraphicsRootConstantBufferView(i, constantAddress(b.name));
            continue;
        }
        if (b.kind == PassBind::RootSrv) {
            ID3D12Resource* buffer = b.name == "WhitePtSrv" && exposureState ? exposureState.Get() : zeroBuffer.Get();
            if (compute) commandList->SetComputeRootShaderResourceView(i, buffer->GetGPUVirtualAddress());
            else commandList->SetGraphicsRootShaderResourceView(i, buffer->GetGPUVirtualAddress());
            continue;
        }
        uint32_t slot = b.name == "tTextureMap0"        ? SRV_POST_BASE + POST_VIEW_CUBES + colorCubeSlots[0]
                      : b.name == "tTextureMap1"        ? SRV_POST_BASE + POST_VIEW_CUBES + colorCubeSlots[1]
                      : b.name == "tTextureMap2"        ? SRV_POST_BASE + POST_VIEW_CUBES + colorCubeSlots[2]
                      : b.name == "OutputImage"         ? SRV_POST_BASE + POST_VIEW_LDR_A_UAV
                      : b.name == "MultiZoneMetering"   ? SRV_POST_BASE + POST_VIEW_METERING
                      : b.name == "HistogramUav"        ? UAV_EXPOSURE
                      : b.name == "WhitePtUav"          ? UAV_EXPOSURE + 1
                      : b.name == "GlobalWhitePtUav"    ? UAV_EXPOSURE + 1
                      : b.name == "ReadonlyDepth"       ? SRV_RESOLVE_DEPTH
                      : b.name == "PrevHDRImage"        ? SRV_POST_BASE + POST_VIEW_HISTORY_SRV
                      : b.name == "Velocity"            ? (temporalVelocityCut ? SRV_LIGHT_BLACK : SRV_GBUFFER0 + 3)
                                                        : sourceView;
        if (compute) commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(slot));
        else commandList->SetGraphicsRootDescriptorTable(i, SrvGpuHandle(slot));
    }
}

bool Viewer::GameExposureReady() const {
    return histogramPass.pso && whitePointPass.pso && meteringTexture && exposureState;
}

// ToneMappingImplement's Histogram and WhitePoint on the composed scene. The engine measures the previous frame's
// TemporalAA output and keeps WhitePtUav and GlobalWhitePtUav apart; both hold the same white point. A reset
// starts the adaptation at the measured white point.
void Viewer::UpdateGameExposure(ID3D12Resource* source, uint32_t sourceView) {
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES readAll = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    float minWhite = std::max(toneMap.minWhite, 1e-6f);
    float ratio = std::max(toneMap.maxWhite / minWhite, 1.0f + 1e-6f);
    struct {
        float converter[4];
        float brightRate;
        float darkRate;
        float whiteRange;
        int32_t updateWp;
    } adjustment{ { 1.0f / minWhite, 1024.0f / std::log2(ratio), minWhite, ratio }, toneMap.brightRate, toneMap.darkRate,
                  toneMap.whiteRange, static_cast<int32_t>(exposureFrame++ % 8) };
    uint32_t size = (height << 16) | width;
    std::memcpy(postConstantsMapped + POST_CB_HISTOGRAM * POST_CB_STRIDE, &adjustment, sizeof(adjustment));
    std::memcpy(postConstantsMapped + POST_CB_ROOT_CONSTANT * POST_CB_STRIDE, &size, sizeof(size));

    D3D12_RESOURCE_BARRIER toClear[2] = { Transition(exposureHistogram.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                     D3D12_RESOURCE_STATE_COPY_DEST),
                                          Transition(source, read, readAll) };
    commandList->ResourceBarrier(2, toClear);
    commandList->CopyBufferRegion(exposureHistogram.Get(), 0, zeroBuffer.Get(), 0, HISTOGRAM_BYTES);
    D3D12_RESOURCE_BARRIER toCount = Transition(exposureHistogram.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier(1, &toCount);
    commandList->SetComputeRootSignature(histogramPass.root.Get());
    commandList->SetPipelineState(histogramPass.pso.Get());
    BindPostPass(histogramPass, true, sourceView);
    commandList->Dispatch((width + HISTOGRAM_TILE - 1) / HISTOGRAM_TILE, (height + HISTOGRAM_TILE - 1) / HISTOGRAM_TILE, 1);
    D3D12_RESOURCE_BARRIER counted[2] = { {}, Transition(source, readAll, read) };
    counted[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    counted[0].UAV.pResource = exposureHistogram.Get();
    commandList->ResourceBarrier(2, counted);
    if (exposureReset) {
        UpdateExposure();
        return;
    }
    commandList->SetComputeRootSignature(whitePointPass.root.Get());
    commandList->SetPipelineState(whitePointPass.pso.Get());
    BindPostPass(whitePointPass, true, sourceView);
    commandList->Dispatch(1, 1, 1);
    D3D12_RESOURCE_BARRIER written[2] = {};
    written[0].Type = written[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    written[0].UAV.pResource = exposureHistogram.Get();
    written[1].UAV.pResource = exposureState.Get();
    commandList->ResourceBarrier(2, written);
}

void Viewer::DrawPostPass(const PostPass& pass, D3D12_CPU_DESCRIPTOR_HANDLE target, uint32_t sourceView, uint32_t targetWidth,
                          uint32_t targetHeight) {
    D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0, 1 };
    D3D12_RECT scissor{ 0, 0, static_cast<LONG>(targetWidth), static_cast<LONG>(targetHeight) };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->OMSetRenderTargets(1, &target, FALSE, nullptr);
    commandList->SetGraphicsRootSignature(pass.root.Get());
    commandList->SetPipelineState(pass.pso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    BindPostPass(pass, false, sourceView);
    commandList->DrawInstanced(3, 1, 0, 0);
}

void Viewer::BeginFrameJitter() {
    bool studio = previewSphere[3] > 0 && !unlit && !wireframe;
    temporalFrame = temporalPass.pso && temporalAA.enabled && hasMesh && !deferredPipelines.empty() && PostProcessActive(studio);
    float jitter[2] = { hasExplicitJitter ? explicitJitter[0] : 0.0f, hasExplicitJitter ? explicitJitter[1] : 0.0f };
    if (temporalFrame) {
        temporalAccumulated = temporalCut ? 1 : temporalAccumulated + 1;
        uint32_t k = jitterIndex++ % 16 + 1;
        if (!(hasExplicitJitter && (targetCaptureOut || captureOut))) {
            jitter[0] = temporalAA.jitterScale * (RadicalInverse(k, 2) - 0.5f) / static_cast<float>(width);
            jitter[1] = temporalAA.jitterScale * (RadicalInverse(k, 3) - 0.5f) / static_cast<float>(height);
        }
    } else {
        temporalCut = true;
        hasPrevViewProjection = false;
    }
    projectionJitter[0] = jitter[0];
    projectionJitter[1] = jitter[1];
    if (sceneInfoMapped) ApplySceneMatrix();
}

void Viewer::PrepareTemporalHistory() {
    temporalVelocityCut = temporalCut;
    if (!temporalCut) return;
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER toCopy[2] = { Transition(sceneHdrTarget.Get(), read, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                         Transition(historyTarget.Get(), read, D3D12_RESOURCE_STATE_COPY_DEST) };
    commandList->ResourceBarrier(2, toCopy);
    commandList->CopyResource(historyTarget.Get(), sceneHdrTarget.Get());
    for (D3D12_RESOURCE_BARRIER& barrier : toCopy) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commandList->ResourceBarrier(2, toCopy);
    temporalCut = false;
}

// The TemporalAA output replaces the scene and becomes the next frame's history (FilterHDRTarget[2]).
void Viewer::RenderTemporalAA() {
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES target = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_BARRIER toTarget = Transition(taaTarget.Get(), read, target);
    commandList->ResourceBarrier(1, &toTarget);
    DrawPostPass(temporalPass, PostRtv(POST_RTV_TAA), SRV_POST_BASE + POST_VIEW_SCENE_SRV, width, height);
    D3D12_RESOURCE_BARRIER toCopy[3] = { Transition(taaTarget.Get(), target, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                         Transition(historyTarget.Get(), read, D3D12_RESOURCE_STATE_COPY_DEST),
                                         Transition(sceneHdrTarget.Get(), read, D3D12_RESOURCE_STATE_COPY_DEST) };
    commandList->ResourceBarrier(3, toCopy);
    commandList->CopyResource(historyTarget.Get(), taaTarget.Get());
    commandList->CopyResource(sceneHdrTarget.Get(), taaTarget.Get());
    D3D12_RESOURCE_BARRIER back[3] = { Transition(taaTarget.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, read),
                                       Transition(historyTarget.Get(), D3D12_RESOURCE_STATE_COPY_DEST, read),
                                       Transition(sceneHdrTarget.Get(), D3D12_RESOURCE_STATE_COPY_DEST, read) };
    commandList->ResourceBarrier(3, back);
    temporalVelocityCut = false;
}

// SoftBloomImplement::drawUnUsedThreasholdBloom: reduce, filter down the levels, blend each level into the one above,
// add the top level to the scene.
void Viewer::RenderSoftBloom() {
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES target = D3D12_RESOURCE_STATE_RENDER_TARGET;
    auto level = [&](uint32_t i, const PostPass& pass, uint32_t sourceView) {
        D3D12_RESOURCE_BARRIER toTarget = Transition(bloomTargets[i].Get(), read, target);
        commandList->ResourceBarrier(1, &toTarget);
        D3D12_RESOURCE_DESC desc = bloomTargets[i]->GetDesc();
        DrawPostPass(pass, PostRtv(POST_RTV_BLOOM + i), sourceView, static_cast<uint32_t>(desc.Width), desc.Height);
        D3D12_RESOURCE_BARRIER toRead = Transition(bloomTargets[i].Get(), target, read);
        commandList->ResourceBarrier(1, &toRead);
    };
    level(0, bloomReductionPass, SRV_POST_BASE + POST_VIEW_SCENE_SRV);
    for (uint32_t i = 1; i < bloomLevels; i++) level(i, bloomFilterPass, SRV_POST_BASE + POST_VIEW_BLOOM + i - 1);
    for (uint32_t i = bloomLevels - 1; i >= 1; i--) level(i - 1, bloomBlendingPass[i], SRV_POST_BASE + POST_VIEW_BLOOM + i);
    D3D12_RESOURCE_BARRIER toTarget = Transition(sceneHdrTarget.Get(), read, target);
    commandList->ResourceBarrier(1, &toTarget);
    DrawPostPass(bloomFinalPass, PostRtv(POST_RTV_SCENE), SRV_POST_BASE + POST_VIEW_BLOOM, width, height);
    D3D12_RESOURCE_BARRIER toRead = Transition(sceneHdrTarget.Get(), target, read);
    commandList->ResourceBarrier(1, &toRead);
}

// TemporalAA, SoftBloom, then ToneMappingImplement::postUpdate (LwLDRPostProcess), FXAA, executeCASFilter and the
// screen output.
void Viewer::RenderPostProcess(D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES readAll = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES target = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (exposureState) {
        D3D12_RESOURCE_BARRIER toRead = Transition(exposureState.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, readAll);
        commandList->ResourceBarrier(1, &toRead);
    }
    if (temporalFrame) RenderTemporalAA();
    if (bloomLevels > 0) RenderSoftBloom();
    auto draw = [&](const PostPass& pass, D3D12_CPU_DESCRIPTOR_HANDLE out, uint32_t sourceView) {
        DrawPostPass(pass, out, sourceView, width, height);
    };
    auto postRtv = [&](uint32_t index) { return PostRtv(index); };
    ID3D12Resource* ldrA = ldrTargets[0].Get();
    ID3D12Resource* ldrB = ldrTargets[1].Get();

    D3D12_RESOURCE_BARRIER begin = Transition(ldrA, read, target);
    commandList->ResourceBarrier(1, &begin);
    draw(ldrPass, postRtv(1), SRV_POST_BASE + POST_VIEW_SCENE_SRV);

    D3D12_RESOURCE_BARRIER toFxaa[2] = { Transition(ldrA, target, read), Transition(ldrB, read, target) };
    commandList->ResourceBarrier(2, toFxaa);
    draw(fxaaPass, postRtv(2), SRV_POST_BASE + POST_VIEW_LDR_A_SRV);

    D3D12_RESOURCE_BARRIER toCas[2] = { Transition(ldrB, target, readAll), Transition(ldrA, read, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) };
    commandList->ResourceBarrier(2, toCas);
    commandList->SetComputeRootSignature(casPass.root.Get());
    commandList->SetPipelineState(casPass.pso.Get());
    BindPostPass(casPass, true, SRV_POST_BASE + POST_VIEW_LDR_B_SRV);
    commandList->Dispatch((width + CAS_TILE - 1) / CAS_TILE, (height + CAS_TILE - 1) / CAS_TILE, 1);

    std::vector<D3D12_RESOURCE_BARRIER> toOutput = { Transition(ldrA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, read),
                                                     Transition(ldrB, readAll, read) };
    if (exposureState) toOutput.push_back(Transition(exposureState.Get(), readAll, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    commandList->ResourceBarrier(static_cast<UINT>(toOutput.size()), toOutput.data());
    draw(outputPass, rtv, SRV_POST_BASE + POST_VIEW_LDR_A_SRV);
}
