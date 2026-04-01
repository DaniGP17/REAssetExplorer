#include "Renderer/Viewer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include "D3D12Utils.h"
#include "Renderer/RenderMath.h"

using Microsoft::WRL::ComPtr;

namespace {

// FFX_CACAO_Constants as the ffx_cacao.sdf shaders declare it.
struct CacaoConstants {
    float depthUnpackConsts[2];
    float cameraTanHalfFov[2];
    float ndcToViewMul[2];
    float ndcToViewAdd[2];
    float depthBufferUvToViewMul[2];
    float depthBufferUvToViewAdd[2];
    float effectRadius;
    float effectShadowStrength;
    float effectShadowPow;
    float effectShadowClamp;
    float effectFadeOutMul;
    float effectFadeOutAdd;
    float effectHorizonAngleThreshold;
    float effectSamplingRadiusNearLimitRec;
    float depthPrecisionOffsetMod;
    float negRecEffectRadius;
    float loadCounterAvgDiv;
    float adaptiveSampleCountLimit;
    float invSharpness;
    int32_t passIndex;
    float bilateralSigmaSquared;
    float bilateralSimilarityDistanceSigma;
    float patternRotScaleMatrices[5][4];
    float normalsUnpackMul;
    float normalsUnpackAdd;
    float detailAoStrength;
    float dummy0;
    float ssaoBufferDimensions[2];
    float ssaoBufferInverseDimensions[2];
    float depthBufferDimensions[2];
    float depthBufferInverseDimensions[2];
    int32_t depthBufferOffset[2];
    float perPassFullResUvOffset[2];
    float outputBufferDimensions[2];
    float outputBufferInverseDimensions[2];
    float importanceMapDimensions[2];
    float importanceMapInverseDimensions[2];
    float deinterleavedDepthBufferDimensions[2];
    float deinterleavedDepthBufferInverseDimensions[2];
    float deinterleavedDepthBufferOffset[2];
    float deinterleavedDepthBufferNormalisedOffset[2];
    float normalsWorldToViewspaceMatrix[16];
};
static_assert(sizeof(CacaoConstants) == 384);

constexpr UINT CACAO_CB_STRIDE = 512;
constexpr uint32_t CACAO_DEPTH_MIPS = 4;
constexpr uint32_t CACAO_PASSES = 4;
// ffx_cacao dispatch tiles: 8x8 threads, the blur covers (4 * 16 - 2n) x (3 * 16 - 2n) per group and the
// bilateral upscale 2x2 pixels per thread.
constexpr UINT CACAO_GROUP = 8;
constexpr UINT CACAO_BLUR_WIDTH = 4 * 16;
constexpr UINT CACAO_BLUR_HEIGHT = 3 * 16;
constexpr UINT CACAO_UPSCALE_GROUP = 16;

enum CacaoView : uint32_t {
    VIEW_DEPTH_MIP_UAV = 0,
    VIEW_NORMALS_UAV = VIEW_DEPTH_MIP_UAV + CACAO_DEPTH_MIPS,
    VIEW_DEPTH_PASS_SRV,
    VIEW_NORMALS_SRV = VIEW_DEPTH_PASS_SRV + CACAO_PASSES,
    VIEW_DEPTH_SRV,
    VIEW_PONG_PASS_UAV,
    VIEW_PING_PASS_UAV = VIEW_PONG_PASS_UAV + CACAO_PASSES,
    VIEW_PING_PASS_SRV = VIEW_PING_PASS_UAV + CACAO_PASSES,
    VIEW_PONG_SRV = VIEW_PING_PASS_SRV + CACAO_PASSES,
    VIEW_IMPORTANCE_SRV,
    VIEW_IMPORTANCE_UAV,
    VIEW_IMPORTANCE_PONG_SRV,
    VIEW_IMPORTANCE_PONG_UAV,
    VIEW_LOAD_COUNTER_SRV,
    VIEW_LOAD_COUNTER_UAV,
    VIEW_OCCLUSION_COPY_SRV,
    VIEW_OCCLUSION_UAV,
    VIEW_COUNT,
};

UINT Groups(uint32_t size, uint32_t tile) {
    return (size + tile - 1) / tile;
}

}

void Viewer::CreateCacao(const GameCacaoDesc& desc) {
    EnsureGameCommon();
    const GameComputeDesc* programs[CACAO_PROGRAM_COUNT] = { &desc.prepareDepths, &desc.prepareNormals, &desc.generateBase,
                                                             &desc.generate, &desc.importanceMap, &desc.importanceA,
                                                             &desc.importanceB, &desc.blur, &desc.upscale };
    for (uint32_t i = 0; i < CACAO_PROGRAM_COUNT; i++) {
        CacaoProgram& program = cacaoPrograms[i];
        program.root = CreateComputeRoot(*programs[i], program.binds);
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = program.root.Get();
        psoDesc.CS = { programs[i]->cs.data(), programs[i]->cs.size() };
        Check(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(program.pso.ReleaseAndGetAddressOf())),
              "CreateComputePipelineState CACAO");
    }
    cacaoSettings = desc.settings;
    if (!cacaoConstants) {
        std::vector<uint8_t> zeros(CACAO_CB_STRIDE * (1 + CACAO_PASSES), 0);
        cacaoConstants = CreateUploadBuffer(zeros);
        D3D12_RANGE none{};
        Check(cacaoConstants->Map(0, &none, reinterpret_cast<void**>(&cacaoConstantsMapped)), "Map CACAO constants");
    }
    if (!cacaoClearHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1;
        Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&cacaoClearHeap)), "CreateDescriptorHeap CACAO clear");
    }
    hasCacao = true;
    CreateCacaoTargets();
}

// FFX_CACAO_UpdateBufferSizeInfo for the downsampled path: SSAO at a quarter of the output per axis,
// deinterleaved 2x2 into four slices, the importance map at half of that.
void Viewer::CreateCacaoTargets() {
    static_assert(VIEW_COUNT <= SRV_CACAO_COUNT);
    if (!hasCacao || width == 0 || height == 0 || !gbuffer[3]) return;
    uint32_t halfWidth = (width + 1) / 2;
    uint32_t halfHeight = (height + 1) / 2;
    cacaoSsaoWidth = (halfWidth + 1) / 2;
    cacaoSsaoHeight = (halfHeight + 1) / 2;
    cacaoImportanceWidth = (cacaoSsaoWidth + 1) / 2;
    cacaoImportanceHeight = (cacaoSsaoHeight + 1) / 2;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    auto create = [&](ComPtr<ID3D12Resource>& out, D3D12_RESOURCE_DIMENSION dimension, uint32_t w, uint32_t h, uint16_t slices,
                      uint16_t mips, DXGI_FORMAT format, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = dimension;
        texDesc.Width = w;
        texDesc.Height = h;
        texDesc.DepthOrArraySize = slices;
        texDesc.MipLevels = mips;
        texDesc.Format = format;
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = flags;
        Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, state, nullptr,
                                              IID_PPV_ARGS(out.ReleaseAndGetAddressOf())), "CreateCommittedResource CACAO");
    };
    const D3D12_RESOURCE_STATES uav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    const D3D12_RESOURCE_FLAGS uavFlag = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const D3D12_RESOURCE_DIMENSION tex2d = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    create(cacaoDepths, tex2d, cacaoSsaoWidth, cacaoSsaoHeight, CACAO_PASSES, CACAO_DEPTH_MIPS, DXGI_FORMAT_R16_FLOAT, uav, uavFlag);
    create(cacaoNormals, tex2d, cacaoSsaoWidth, cacaoSsaoHeight, CACAO_PASSES, 1, DXGI_FORMAT_R8G8B8A8_SNORM, uav, uavFlag);
    create(cacaoPing, tex2d, cacaoSsaoWidth, cacaoSsaoHeight, CACAO_PASSES, 1, DXGI_FORMAT_R8G8_UNORM, uav, uavFlag);
    create(cacaoPong, tex2d, cacaoSsaoWidth, cacaoSsaoHeight, CACAO_PASSES, 1, DXGI_FORMAT_R8G8_UNORM, uav, uavFlag);
    create(cacaoImportance, tex2d, cacaoImportanceWidth, cacaoImportanceHeight, 1, 1, DXGI_FORMAT_R8_UNORM, uav, uavFlag);
    create(cacaoImportancePong, tex2d, cacaoImportanceWidth, cacaoImportanceHeight, 1, 1, DXGI_FORMAT_R8_UNORM, uav, uavFlag);
    create(cacaoLoadCounter, D3D12_RESOURCE_DIMENSION_TEXTURE1D, 1, 1, 1, 1, DXGI_FORMAT_R32_UINT, uav, uavFlag);
    create(cacaoOcclusionCopy, tex2d, width, height, 1, 1, GBUFFER_FORMATS[3], D3D12_RESOURCE_STATE_COPY_DEST,
           D3D12_RESOURCE_FLAG_NONE);

    auto view = [&](uint32_t index) { return SrvCpuHandle(SRV_CACAO_BASE + index); };
    auto arrayUav = [&](ID3D12Resource* resource, DXGI_FORMAT format, uint32_t mip, uint32_t first, uint32_t count, uint32_t index) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
        d.Format = format;
        d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        d.Texture2DArray = { mip, first, count, 0 };
        device->CreateUnorderedAccessView(resource, nullptr, &d, view(index));
    };
    auto arraySrv = [&](ID3D12Resource* resource, DXGI_FORMAT format, uint32_t mips, uint32_t first, uint32_t count, uint32_t index) {
        D3D12_SHADER_RESOURCE_VIEW_DESC d{};
        d.Format = format;
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.Texture2DArray.MipLevels = mips;
        d.Texture2DArray.FirstArraySlice = first;
        d.Texture2DArray.ArraySize = count;
        device->CreateShaderResourceView(resource, &d, view(index));
    };
    auto texSrv = [&](ID3D12Resource* resource, DXGI_FORMAT format, uint32_t index) {
        D3D12_SHADER_RESOURCE_VIEW_DESC d{};
        d.Format = format;
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(resource, &d, view(index));
    };
    auto texUav = [&](ID3D12Resource* resource, DXGI_FORMAT format, uint32_t index) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
        d.Format = format;
        d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(resource, nullptr, &d, view(index));
    };
    for (uint32_t mip = 0; mip < CACAO_DEPTH_MIPS; mip++) {
        arrayUav(cacaoDepths.Get(), DXGI_FORMAT_R16_FLOAT, mip, 0, CACAO_PASSES, VIEW_DEPTH_MIP_UAV + mip);
    }
    arrayUav(cacaoNormals.Get(), DXGI_FORMAT_R8G8B8A8_SNORM, 0, 0, CACAO_PASSES, VIEW_NORMALS_UAV);
    arraySrv(cacaoNormals.Get(), DXGI_FORMAT_R8G8B8A8_SNORM, 1, 0, CACAO_PASSES, VIEW_NORMALS_SRV);
    arraySrv(cacaoDepths.Get(), DXGI_FORMAT_R16_FLOAT, CACAO_DEPTH_MIPS, 0, CACAO_PASSES, VIEW_DEPTH_SRV);
    arraySrv(cacaoPong.Get(), DXGI_FORMAT_R8G8_UNORM, 1, 0, CACAO_PASSES, VIEW_PONG_SRV);
    for (uint32_t pass = 0; pass < CACAO_PASSES; pass++) {
        arraySrv(cacaoDepths.Get(), DXGI_FORMAT_R16_FLOAT, 1, pass, 1, VIEW_DEPTH_PASS_SRV + pass);
        arrayUav(cacaoPong.Get(), DXGI_FORMAT_R8G8_UNORM, 0, pass, 1, VIEW_PONG_PASS_UAV + pass);
        arrayUav(cacaoPing.Get(), DXGI_FORMAT_R8G8_UNORM, 0, pass, 1, VIEW_PING_PASS_UAV + pass);
        arraySrv(cacaoPing.Get(), DXGI_FORMAT_R8G8_UNORM, 1, pass, 1, VIEW_PING_PASS_SRV + pass);
    }
    texSrv(cacaoImportance.Get(), DXGI_FORMAT_R8_UNORM, VIEW_IMPORTANCE_SRV);
    texUav(cacaoImportance.Get(), DXGI_FORMAT_R8_UNORM, VIEW_IMPORTANCE_UAV);
    texSrv(cacaoImportancePong.Get(), DXGI_FORMAT_R8_UNORM, VIEW_IMPORTANCE_PONG_SRV);
    texUav(cacaoImportancePong.Get(), DXGI_FORMAT_R8_UNORM, VIEW_IMPORTANCE_PONG_UAV);
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.Format = DXGI_FORMAT_R32_UINT;
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Texture1D.MipLevels = 1;
        device->CreateShaderResourceView(cacaoLoadCounter.Get(), &s, view(VIEW_LOAD_COUNTER_SRV));
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = DXGI_FORMAT_R32_UINT;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
        device->CreateUnorderedAccessView(cacaoLoadCounter.Get(), nullptr, &u, view(VIEW_LOAD_COUNTER_UAV));
        device->CreateUnorderedAccessView(cacaoLoadCounter.Get(), nullptr, &u, cacaoClearHeap->GetCPUDescriptorHandleForHeapStart());
    }
    texSrv(cacaoOcclusionCopy.Get(), GBUFFER_FORMATS[3], VIEW_OCCLUSION_COPY_SRV);
    texUav(gbuffer[3].Get(), GBUFFER_FORMATS[3], VIEW_OCCLUSION_UAV);
}

// CACAOImplement::updateConstants / updatePerPassConstants. The engine hands CACAO its projection times
// flipZ (z' = 1 - z), which turns the reverse-Z depth into a forward one.
void Viewer::WriteCacaoConstants() {
    const GameCacaoSettings& s = cacaoSettings;
    Mat4 proj;
    std::memcpy(proj.m, camProj, sizeof(proj.m));
    float m22 = proj.m[11] - proj.m[10];
    float m32 = proj.m[15] - proj.m[14];

    CacaoConstants c{};
    c.bilateralSigmaSquared = s.bilateralSigmaSquared;
    c.bilateralSimilarityDistanceSigma = s.bilateralSimilarityDistanceSigma;
    std::memcpy(c.normalsWorldToViewspaceMatrix, camView, sizeof(c.normalsWorldToViewspaceMatrix));
    c.loadCounterAvgDiv = 9.0f / static_cast<float>(static_cast<double>(cacaoImportanceWidth * cacaoImportanceHeight) * 255.0);
    c.depthUnpackConsts[0] = -m32;
    c.depthUnpackConsts[1] = m22 * -m32 < 0 ? -m22 : m22;
    float tanHalfY = 1.0f / proj.m[5];
    float tanHalfX = 1.0f / proj.m[0];
    c.cameraTanHalfFov[0] = tanHalfX;
    c.cameraTanHalfFov[1] = tanHalfY;
    c.ndcToViewMul[0] = tanHalfX * 2.0f;
    c.ndcToViewMul[1] = tanHalfY * -2.0f;
    c.ndcToViewAdd[0] = tanHalfX * -1.0f;
    c.ndcToViewAdd[1] = tanHalfY;
    float ratio = 1.0f;
    float border = (1.0f - ratio) * 0.5f;
    c.depthBufferUvToViewMul[0] = c.ndcToViewMul[0] / ratio;
    c.depthBufferUvToViewAdd[0] = c.ndcToViewAdd[0] - c.ndcToViewMul[0] * border / ratio;
    c.depthBufferUvToViewMul[1] = c.ndcToViewMul[1] / ratio;
    c.depthBufferUvToViewAdd[1] = c.ndcToViewAdd[1] - border * c.ndcToViewMul[1] / ratio;
    c.effectRadius = std::min(100000.0f, std::max(0.0f, s.radius));
    c.effectShadowStrength = std::min(10.0f, std::max(0.0f, s.shadowMultiplier * 4.3f));
    c.effectShadowPow = std::min(10.0f, std::max(0.0f, s.shadowPower));
    c.effectShadowClamp = std::min(1.0f, std::max(0.0f, s.shadowClamp));
    c.effectFadeOutMul = -1.0f / (s.fadeOutTo - s.fadeOutFrom);
    c.effectFadeOutAdd = s.fadeOutFrom / (s.fadeOutTo - s.fadeOutFrom) + 1.0f;
    c.effectHorizonAngleThreshold = std::min(1.0f, std::max(0.0f, s.horizonAngleThreshold));
    c.depthPrecisionOffsetMod = 0.9992f;
    c.effectSamplingRadiusNearLimitRec = 1.0f / (s.radius * 1.2f / tanHalfY);
    c.adaptiveSampleCountLimit = s.adaptiveQualityLimit;
    c.negRecEffectRadius = -1.0f / c.effectRadius;
    c.invSharpness = std::min(1.0f, std::max(0.0f, 1.0f - s.sharpness));
    c.detailAoStrength = s.detailShadowStrength;
    c.ssaoBufferDimensions[0] = static_cast<float>(cacaoSsaoWidth);
    c.ssaoBufferDimensions[1] = static_cast<float>(cacaoSsaoHeight);
    c.ssaoBufferInverseDimensions[0] = 1.0f / c.ssaoBufferDimensions[0];
    c.ssaoBufferInverseDimensions[1] = 1.0f / c.ssaoBufferDimensions[1];
    c.depthBufferDimensions[0] = static_cast<float>(width);
    c.depthBufferDimensions[1] = static_cast<float>(height);
    c.depthBufferInverseDimensions[0] = 1.0f / c.depthBufferDimensions[0];
    c.depthBufferInverseDimensions[1] = 1.0f / c.depthBufferDimensions[1];
    c.outputBufferDimensions[0] = c.depthBufferDimensions[0];
    c.outputBufferDimensions[1] = c.depthBufferDimensions[1];
    c.outputBufferInverseDimensions[0] = c.depthBufferInverseDimensions[0];
    c.outputBufferInverseDimensions[1] = c.depthBufferInverseDimensions[1];
    c.importanceMapDimensions[0] = static_cast<float>(cacaoImportanceWidth);
    c.importanceMapDimensions[1] = static_cast<float>(cacaoImportanceHeight);
    c.importanceMapInverseDimensions[0] = 1.0f / c.importanceMapDimensions[0];
    c.importanceMapInverseDimensions[1] = 1.0f / c.importanceMapDimensions[1];
    c.deinterleavedDepthBufferDimensions[0] = c.ssaoBufferDimensions[0];
    c.deinterleavedDepthBufferDimensions[1] = c.ssaoBufferDimensions[1];
    c.deinterleavedDepthBufferInverseDimensions[0] = c.ssaoBufferInverseDimensions[0];
    c.deinterleavedDepthBufferInverseDimensions[1] = c.ssaoBufferInverseDimensions[1];
    c.normalsUnpackMul = 2.0f;
    c.normalsUnpackAdd = -1.0f;
    std::memcpy(cacaoConstantsMapped, &c, sizeof(c));

    for (int pass = 0; pass < static_cast<int>(CACAO_PASSES); pass++) {
        CacaoConstants p = c;
        p.perPassFullResUvOffset[0] = static_cast<float>(pass % 2) / static_cast<float>(cacaoSsaoWidth);
        p.perPassFullResUvOffset[1] = static_cast<float>(pass / 2) / static_cast<float>(cacaoSsaoHeight);
        p.passIndex = pass;
        const float offsets[5] = { 0.0f, 0.2f, 0.8f, 0.6f, 0.4f };
        const float scales[5] = { -0.4f, -0.2f, 0.4f, 0.2f, 0.0f };
        float sub = static_cast<float>(pass) - 1.5f;
        for (int i = 0; i < 5; i++) {
            float angle = (static_cast<float>(pass) + offsets[i]) * std::numbers::pi_v<float> * 0.5f;
            float scale = (sub + scales[i]) * 0.07f + 1.0f;
            float cs = scale * std::cos(angle);
            float sn = -(scale * std::sin(angle));
            p.patternRotScaleMatrices[i][0] = cs;
            p.patternRotScaleMatrices[i][1] = sn;
            p.patternRotScaleMatrices[i][2] = sn;
            p.patternRotScaleMatrices[i][3] = -cs;
        }
        std::memcpy(cacaoConstantsMapped + CACAO_CB_STRIDE * (1 + pass), &p, sizeof(p));
    }
}

void Viewer::DispatchCacao() {
    WriteCacaoConstants();
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES readAll = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES computeRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES uav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    auto barrier = [&](std::initializer_list<D3D12_RESOURCE_BARRIER> list) {
        commandList->ResourceBarrier(static_cast<UINT>(list.size()), list.begin());
    };
    auto uavBarrier = [](ID3D12Resource* resource) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = resource;
        return b;
    };
    D3D12_GPU_VIRTUAL_ADDRESS constants = cacaoConstants->GetGPUVirtualAddress();
    auto passConstants = [&](uint32_t pass) { return constants + CACAO_CB_STRIDE * (1 + pass); };

    // Each SDF resource name picks its view; pass selects the per-pass slices.
    auto run = [&](CacaoProgramId id, D3D12_GPU_VIRTUAL_ADDRESS cb, uint32_t pass, UINT x, UINT y) {
        const CacaoProgram& program = cacaoPrograms[id];
        commandList->SetComputeRootSignature(program.root.Get());
        commandList->SetPipelineState(program.pso.Get());
        for (UINT i = 0; i < program.binds.size(); i++) {
            const IndirectBind& bind = program.binds[i];
            const std::string& n = bind.name;
            if (bind.kind == IndirectBind::Cbv) {
                commandList->SetComputeRootConstantBufferView(i, n == "SceneInfo" ? sceneInfoBuffer->GetGPUVirtualAddress() : cb);
                continue;
            }
            uint32_t slot = SRV_LIGHT_BLACK;
            auto cacao = [](uint32_t v) { return SRV_CACAO_BASE + v; };
            if (n == "g_DepthIn" || n == "g_BilateralUpscaleDepth") slot = SRV_LIGHT_DEPTH;
            else if (n == "g_PrepareNormalsFromNormalsInput") slot = SRV_GBUFFER0 + 2;
            else if (n.starts_with("g_PrepareDepthsAndMips_OutMip")) slot = cacao(VIEW_DEPTH_MIP_UAV + (n.back() - '0'));
            else if (n == "g_PrepareNormals_NormalOut") slot = cacao(VIEW_NORMALS_UAV);
            else if (n == "g_ViewspaceDepthSource") slot = cacao(VIEW_DEPTH_PASS_SRV + pass);
            else if (n == "g_DeinterleavedNormals") slot = cacao(VIEW_NORMALS_SRV);
            else if (n == "g_SSAOOutput") slot = cacao((id == CACAO_GENERATE_BASE ? VIEW_PONG_PASS_UAV : VIEW_PING_PASS_UAV) + pass);
            else if (n == "g_FinalSSAO" || n == "g_ImportanceFinalSSAO" || n == "g_BilateralUpscaleInput") slot = cacao(VIEW_PONG_SRV);
            else if (n == "g_LoadCounter") slot = cacao(VIEW_LOAD_COUNTER_SRV);
            else if (n == "g_ImportanceMap" || n == "g_ImportanceAIn") slot = cacao(VIEW_IMPORTANCE_SRV);
            else if (n == "g_ImportanceOut" || n == "g_ImportanceBOut") slot = cacao(VIEW_IMPORTANCE_UAV);
            else if (n == "g_ImportanceAOut") slot = cacao(VIEW_IMPORTANCE_PONG_UAV);
            else if (n == "g_ImportanceBIn") slot = cacao(VIEW_IMPORTANCE_PONG_SRV);
            else if (n == "g_ImportanceBLoadCounter") slot = cacao(VIEW_LOAD_COUNTER_UAV);
            else if (n == "g_EdgeSensitiveBlur_Input") slot = cacao(VIEW_PING_PASS_SRV + pass);
            else if (n == "g_EdgeSensitiveBlur_Output") slot = cacao(VIEW_PONG_PASS_UAV + pass);
            else if (n == "g_BilateralUpscaleDownscaledDepth") slot = cacao(VIEW_DEPTH_SRV);
            else if (n == "VelocityXVelocityYOcclusionSubSurfaceSRV") slot = cacao(VIEW_OCCLUSION_COPY_SRV);
            else if (n == "VelocityXVelocityYOcclusionSubSurfaceUAV") slot = cacao(VIEW_OCCLUSION_UAV);
            commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(slot));
        }
        commandList->Dispatch(x, y, 1);
    };

    barrier({ Transition(depthBuffer.Get(), read, readAll), Transition(gbuffer[2].Get(), read, readAll),
              Transition(gbuffer[3].Get(), read, D3D12_RESOURCE_STATE_COPY_SOURCE) });
    const UINT zero[4] = {};
    commandList->ClearUnorderedAccessViewUint(SrvGpuHandle(SRV_CACAO_BASE + VIEW_LOAD_COUNTER_UAV),
                                              cacaoClearHeap->GetCPUDescriptorHandleForHeapStart(), cacaoLoadCounter.Get(), zero, 0,
                                              nullptr);
    commandList->CopyResource(cacaoOcclusionCopy.Get(), gbuffer[3].Get());
    barrier({ Transition(gbuffer[3].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, uav),
              Transition(cacaoOcclusionCopy.Get(), D3D12_RESOURCE_STATE_COPY_DEST, computeRead) });

    UINT ssaoX = Groups(cacaoSsaoWidth, CACAO_GROUP);
    UINT ssaoY = Groups(cacaoSsaoHeight, CACAO_GROUP);
    run(CACAO_PREPARE_DEPTHS, constants, 0, ssaoX, ssaoY);
    run(CACAO_PREPARE_NORMALS, constants, 0, ssaoX, ssaoY);
    barrier({ Transition(cacaoDepths.Get(), uav, computeRead), Transition(cacaoNormals.Get(), uav, computeRead) });

    for (uint32_t pass = 0; pass < CACAO_PASSES; pass++) run(CACAO_GENERATE_BASE, passConstants(pass), pass, ssaoX, ssaoY);
    barrier({ Transition(cacaoPong.Get(), uav, computeRead) });

    UINT importanceX = Groups(cacaoImportanceWidth, CACAO_GROUP);
    UINT importanceY = Groups(cacaoImportanceHeight, CACAO_GROUP);
    run(CACAO_IMPORTANCE_MAP, constants, 0, importanceX, importanceY);
    barrier({ Transition(cacaoImportance.Get(), uav, computeRead) });
    run(CACAO_IMPORTANCE_A, constants, 0, importanceX, importanceY);
    barrier({ Transition(cacaoImportance.Get(), computeRead, uav), Transition(cacaoImportancePong.Get(), uav, computeRead),
              uavBarrier(cacaoLoadCounter.Get()) });
    run(CACAO_IMPORTANCE_B, constants, 0, importanceX, importanceY);
    barrier({ Transition(cacaoImportance.Get(), uav, computeRead), Transition(cacaoLoadCounter.Get(), uav, computeRead) });

    for (uint32_t pass = 0; pass < CACAO_PASSES; pass++) run(CACAO_GENERATE, passConstants(pass), pass, ssaoX, ssaoY);
    barrier({ Transition(cacaoPing.Get(), uav, computeRead), Transition(cacaoPong.Get(), computeRead, uav) });

    // The game binds the last per-pass buffer for every blur and the upscale.
    UINT blurShrink = 2 * cacaoSettings.blurPassCount;
    UINT blurX = Groups(cacaoSsaoWidth, CACAO_BLUR_WIDTH - blurShrink);
    UINT blurY = Groups(cacaoSsaoHeight, CACAO_BLUR_HEIGHT - blurShrink);
    for (uint32_t pass = 0; pass < CACAO_PASSES; pass++) run(CACAO_BLUR, passConstants(CACAO_PASSES - 1), pass, blurX, blurY);
    barrier({ Transition(cacaoPong.Get(), uav, computeRead) });

    run(CACAO_UPSCALE, passConstants(CACAO_PASSES - 1), 0, Groups(width, CACAO_UPSCALE_GROUP), Groups(height, CACAO_UPSCALE_GROUP));

    barrier({ Transition(depthBuffer.Get(), readAll, read), Transition(gbuffer[2].Get(), readAll, read),
              Transition(gbuffer[3].Get(), uav, read), Transition(cacaoOcclusionCopy.Get(), computeRead, D3D12_RESOURCE_STATE_COPY_DEST),
              Transition(cacaoDepths.Get(), computeRead, uav), Transition(cacaoNormals.Get(), computeRead, uav),
              Transition(cacaoPing.Get(), computeRead, uav), Transition(cacaoPong.Get(), computeRead, uav),
              Transition(cacaoImportance.Get(), computeRead, uav), Transition(cacaoImportancePong.Get(), computeRead, uav),
              Transition(cacaoLoadCounter.Get(), computeRead, uav) });
}
