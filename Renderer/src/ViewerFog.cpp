#include "Renderer/Viewer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "D3D12Utils.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT FOG_CB_STRIDE = 256;
enum FogConstants : uint32_t { FOG_CB_FRUSTUM, FOG_CB_PARAMS, FOG_CB_GLOBAL, FOG_CB_FOG_PARAM, FOG_CB_COUNT };

// VolumetricFogControl::Implementation: a 160x90 froxel grid, at most 16 media of 64 B, 512 scattering colors
// (LightRenderer's mVolumetricScatteringParamCache), and shade() dispatching 4x4x4 froxels per group.
constexpr uint32_t FROXEL_WIDTH = 160;
constexpr uint32_t FROXEL_HEIGHT = 90;
constexpr uint32_t MAX_MEDIA = 16;
constexpr uint32_t MAX_SCATTERING_LIGHTS = 512;
constexpr UINT SHADE_GROUP = 4;

constexpr DXGI_FORMAT FROXEL_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT FOG_TARGET_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

struct FrustumVolume {
    float cornerRays[4][4];
    float depthEncoding[4];
    float depthDecoding[4];
    uint32_t textureSize[3];
    uint32_t flags;
    float invTextureSize[3];
    float pad;
};
static_assert(sizeof(FrustumVolume) == 128);

struct ControlParams {
    uint32_t volumeFogCount;
    uint32_t jitterInfo;
    uint32_t flags;
    uint32_t debugView;
    float blendFactor;
    float cullingDistance;
    float edgeSoftness;
    float edgeOffsetPlus1;
    float rejectionSensitivity;
    float leakBias;
    float invVolumeFogCount;
    float invEdgeSoftness;
};
static_assert(sizeof(ControlParams) == 48);

struct GlobalFog {
    uint32_t type;
    uint32_t albedo;
    float density;
    float eccentricity;
    float attenuationByHeight;
    float referenceAltitude;
    float pad[2];
};

struct BoxMedium {
    uint32_t typeAndEccentricity;
    uint32_t albedo;
    float density;
    float attenuationByHeight;
    float axisX[3];
    float invExtentX;
    float axisY[3];
    float invExtentY;
    float position[3];
    float invExtentZ;
};
static_assert(sizeof(BoxMedium) == 64);

// Params flags: 1 always, 2 once the history holds a frame, 8 with Rejection.
constexpr uint32_t FLAG_RUNNING = 1;
constexpr uint32_t FLAG_HISTORY = 2;
constexpr uint32_t FLAG_REJECTION = 8;
// Box media whose axes are not the world's.
constexpr uint32_t TYPE_ROTATED_BOX = 257;

// via::render::evaluateDepthParams.
void EvaluateDepthParams(float nearZ, float farZ, float factor, float encoding[4], float decoding[4]) {
    float f = std::max(0.0001f, factor);
    float range = std::log2((farZ - nearZ) * f + 1.0f);
    float offset = nearZ - 1.0f / f;
    const float dec[4] = { 1.0f / f, range, offset, factor };
    const float enc[4] = { std::log2(f) / range, offset, 1.0f / range, factor };
    std::memcpy(decoding, dec, sizeof(dec));
    std::memcpy(encoding, enc, sizeof(enc));
}

}

// Fog and volumetric fog share one constant buffer; a scene may have either.
void Viewer::EnsureFogConstants() {
    if (fogConstants) return;
    fogConstants = CreateUploadBuffer(std::vector<uint8_t>(FOG_CB_STRIDE * FOG_CB_COUNT, 0));
    D3D12_RANGE none{};
    Check(fogConstants->Map(0, &none, reinterpret_cast<void**>(&fogConstantsMapped)), "Map fog constants");
}

void Viewer::CreateFog(const GameFogDesc& desc) {
    EnsureGameCommon();
    fogRoot = CreateComputeRoot(desc.ps, fogBinds);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = fogRoot.Get();
    psoDesc.VS = { desc.vs.data(), desc.vs.size() };
    psoDesc.PS = { desc.ps.cs.data(), desc.ps.cs.size() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = FOG_TARGET_FORMAT;
    psoDesc.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(fogPso.ReleaseAndGetAddressOf())), "CreateGraphicsPipelineState Fog");

    EnsureFogConstants();
    if (!fogBlackVolume) {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        texDesc.Width = 1;
        texDesc.Height = 1;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = FROXEL_FORMAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                              IID_PPV_ARGS(&fogBlackVolume)), "CreateCommittedResource fog black volume");
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> clearHeap;
        Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&clearHeap)), "CreateDescriptorHeap fog clear");
        device->CreateUnorderedAccessView(fogBlackVolume.Get(), nullptr, nullptr, clearHeap->GetCPUDescriptorHandleForHeapStart());
        device->CreateUnorderedAccessView(fogBlackVolume.Get(), nullptr, nullptr, SrvCpuHandle(SRV_FOG_BASE + FOG_VIEW_BLACK_VOLUME));
        Check(allocator->Reset(), "allocator Reset fog");
        Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset fog");
        ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
        const float zero[4] = {};
        commandList->ClearUnorderedAccessViewFloat(SrvGpuHandle(SRV_FOG_BASE + FOG_VIEW_BLACK_VOLUME),
                                                   clearHeap->GetCPUDescriptorHandleForHeapStart(), fogBlackVolume.Get(), zero, 0, nullptr);
        D3D12_RESOURCE_BARRIER toSrv = Transition(fogBlackVolume.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &toSrv);
        Check(commandList->Close(), "Close fog");
        ID3D12CommandList* lists[] = { commandList.Get() };
        queue->ExecuteCommandLists(1, lists);
        WaitForGpu();
        device->CreateShaderResourceView(fogBlackVolume.Get(), nullptr, SrvCpuHandle(SRV_FOG_BASE + FOG_VIEW_BLACK_VOLUME));
    }
}

D3D12_GPU_VIRTUAL_ADDRESS Viewer::FogParamAddress() const {
    return fogEnabled && showFlags.fog ? fogConstants->GetGPUVirtualAddress() + FOG_CB_FOG_PARAM * FOG_CB_STRIDE
                                       : zeroBuffer->GetGPUVirtualAddress();
}

// Kept aside too: hiding the fog zeroes the constants.
void Viewer::SetFogParam(const ViewerFogParam* param) {
    fogEnabled = param != nullptr && fogPso;
    if (!fogEnabled) return;
    fogParamCopy = *param;
    std::memcpy(fogConstantsMapped + FOG_CB_FOG_PARAM * FOG_CB_STRIDE, param, sizeof(*param));
}

void Viewer::CreateFogTarget() {
    if (width == 0 || height == 0) return;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = FOG_TARGET_FORMAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = desc.Format;
    clear.Color[3] = 1.0f;
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                                          IID_PPV_ARGS(fogTarget.ReleaseAndGetAddressOf())), "CreateCommittedResource fog");
    fogTarget->SetName(L"Fog");
    if (!fogRtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = 1;
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&fogRtvHeap)), "CreateDescriptorHeap fog RTV");
    }
    device->CreateRenderTargetView(fogTarget.Get(), nullptr, fogRtvHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateShaderResourceView(fogTarget.Get(), nullptr, SrvCpuHandle(SRV_FOG_BASE + FOG_VIEW_TARGET_SRV));
}

void Viewer::CreateVolumetricFog(const GameVolumetricFogDesc& desc) {
    EnsureGameCommon();
    auto build = [&](CacaoProgram& program, const GameComputeDesc& compute, const char* what) {
        program.root = CreateComputeRoot(compute, program.binds);
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = program.root.Get();
        psoDesc.CS = { compute.cs.data(), compute.cs.size() };
        Check(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(program.pso.ReleaseAndGetAddressOf())), what);
    };
    build(injectProgram, desc.inject, "CreateComputePipelineState InjectShadedVolumeData");
    build(integrateProgram, desc.integrate, "CreateComputePipelineState IntegrateFroxelContribution");
    integrateGroup = desc.integrateGroup;
    EnsureFogConstants();
    blueNoiseTables[0] = CreateUploadBuffer(desc.sobol);
    blueNoiseTables[1] = CreateUploadBuffer(desc.scrambling);
    blueNoiseTables[2] = CreateUploadBuffer(desc.ranking);
    if (!volumetricFogList) {
        volumetricFogList = CreateUploadBuffer(std::vector<uint8_t>(MAX_MEDIA * sizeof(BoxMedium), 0));
        D3D12_RANGE none{};
        Check(volumetricFogList->Map(0, &none, reinterpret_cast<void**>(&volumetricFogListMapped)), "Map volumetric fog list");
    }
    if (!scatteringLightList) {
        scatteringLightList = CreateUploadBuffer(std::vector<uint8_t>(MAX_SCATTERING_LIGHTS * 12, 0));
        D3D12_RANGE none{};
        Check(scatteringLightList->Map(0, &none, reinterpret_cast<void**>(&scatteringLightListMapped)), "Map scattering lights");
    }
}

// VolumetricFogControl::Implementation::createTextures: the shaded froxels twice (this frame, history) and
// their integration along the view.
void Viewer::SetVolumetricFog(const ViewerVolumetricFogControl& control, std::vector<ViewerVolumetricFog> media) {
    if (!volumetricControl.enabled) volumetricHistory = false;
    volumetricControl = control;
    volumetricMedia = std::move(media);
    if (!injectProgram.pso || !control.enabled || volumetricDepth == control.depthSlices) return;
    volumetricDepth = control.depthSlices;
    volumetricHistory = false;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    texDesc.Width = FROXEL_WIDTH;
    texDesc.Height = FROXEL_HEIGHT;
    texDesc.DepthOrArraySize = static_cast<UINT16>(volumetricDepth);
    texDesc.MipLevels = 1;
    texDesc.Format = FROXEL_FORMAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto create = [&](ComPtr<ID3D12Resource>& out, uint32_t srv, uint32_t uav) {
        Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                              IID_PPV_ARGS(out.ReleaseAndGetAddressOf())), "CreateCommittedResource froxels");
        device->CreateShaderResourceView(out.Get(), nullptr, SrvCpuHandle(SRV_FOG_BASE + srv));
        device->CreateUnorderedAccessView(out.Get(), nullptr, nullptr, SrvCpuHandle(SRV_FOG_BASE + uav));
    };
    create(shadedFog[0], FOG_VIEW_SHADED_SRV, FOG_VIEW_SHADED_UAV);
    create(shadedFog[1], FOG_VIEW_SHADED_SRV + 1, FOG_VIEW_SHADED_UAV + 1);
    create(volumetricFogTexture, FOG_VIEW_VOLUME_SRV, FOG_VIEW_VOLUME_UAV);
}

void Viewer::SetVolumetricScattering(std::span<const float> rgb) {
    if (!scatteringLightListMapped) return;
    std::size_t bytes = std::min<std::size_t>(rgb.size() * 4, MAX_SCATTERING_LIGHTS * 12);
    std::memset(scatteringLightListMapped, 0, MAX_SCATTERING_LIGHTS * 12);
    std::memcpy(scatteringLightListMapped, rgb.data(), bytes);
}

// VolumetricFogControl::Implementation::update: updateVolumetricFogList, updateCameraInfo, shade and the integration.
void Viewer::DispatchVolumetricFog() {
    volumetricRunning = false;
    FrustumVolume frustum{};
    if (!injectProgram.pso || !volumetricControl.enabled || !volumetricFogTexture || !showFlags.volumetricFog) {
        // Scenes without any fog (mesh previews) never create the constants.
        if (fogConstantsMapped) std::memcpy(fogConstantsMapped + FOG_CB_FRUSTUM * FOG_CB_STRIDE, &frustum, sizeof(frustum));
        return;
    }
    const ViewerVolumetricFogControl& c = volumetricControl;

    // updateVolumetricFogList: box media inside the view frustum, the first global medium with density.
    std::vector<BoxMedium> boxes;
    GlobalFog global{};
    global.type = 0xFFFFFFFF;
    for (const ViewerVolumetricFog& medium : volumetricMedia) {
        if (medium.type == 0) {
            if (global.density <= 0) {
                global = { medium.type, medium.albedo, medium.density, medium.eccentricity, medium.attenuationByHeight,
                           medium.referenceAltitude, {} };
            }
            continue;
        }
        const float* m = medium.world;
        float axis[3][3];
        float extent[3];
        for (int r = 0; r < 3; r++) {
            extent[r] = std::sqrt(m[r * 4] * m[r * 4] + m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2]);
            for (int k = 0; k < 3; k++) axis[r][k] = extent[r] > 0 ? m[r * 4 + k] / extent[r] : 0;
        }
        bool outside = false;
        for (int p = 0; p < 6 && !outside; p++) {
            const float* plane = frustumPlanes[p];
            float radius = 0;
            for (int r = 0; r < 3; r++) {
                radius += extent[r] * std::abs(plane[0] * axis[r][0] + plane[1] * axis[r][1] + plane[2] * axis[r][2]);
            }
            float distance = plane[0] * m[12] + plane[1] * m[13] + plane[2] * m[14] + plane[3];
            outside = distance < -radius;
        }
        if (outside) continue;
        if (boxes.size() == MAX_MEDIA) break;
        uint32_t eccentricity = std::min<uint32_t>(static_cast<uint32_t>((medium.eccentricity + 1.0f) * 0.5f * 65536.0f), 0xFFFF);
        bool rotated = axis[0][1] != 0 || axis[0][2] != 0 || axis[1][0] != 0 || axis[1][2] != 0 || axis[2][0] != 0 || axis[2][1] != 0;
        uint32_t type = medium.type == 1 && rotated ? TYPE_ROTATED_BOX : (medium.type & 0xFFFF);
        BoxMedium box{};
        box.typeAndEccentricity = type | (eccentricity << 16);
        box.albedo = medium.albedo;
        box.density = medium.density;
        box.attenuationByHeight = medium.attenuationByHeight;
        std::copy_n(axis[0], 3, box.axisX);
        std::copy_n(axis[1], 3, box.axisY);
        std::copy_n(m + 12, 3, box.position);
        box.invExtentX = 1.0f / extent[0];
        box.invExtentY = 1.0f / extent[1];
        box.invExtentZ = 1.0f / extent[2];
        boxes.push_back(box);
    }
    uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(boxes.size()) + (global.density > 0 ? 1 : 0), MAX_MEDIA);
    if (count == 0) {
        std::memcpy(fogConstantsMapped + FOG_CB_FRUSTUM * FOG_CB_STRIDE, &frustum, sizeof(frustum));
        return;
    }
    std::memcpy(volumetricFogListMapped, boxes.data(), boxes.size() * sizeof(BoxMedium));
    std::memcpy(fogConstantsMapped + FOG_CB_GLOBAL * FOG_CB_STRIDE, &global, sizeof(global));

    // updateCameraInfo: the far plane corners (-1, 1), (1, 1), (-1, -1), (1, -1) relative to the camera.
    const float corners[4][2] = { { -1, 1 }, { 1, 1 }, { -1, -1 }, { 1, -1 } };
    for (int i = 0; i < 4; i++) {
        float p[4] = { corners[i][0], corners[i][1], 1, 1 };
        float world[4] = {};
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) world[col] += p[row] * skyViewProjInv[row * 4 + col];
        }
        for (int k = 0; k < 3; k++) frustum.cornerRays[i][k] = world[k] / world[3] - skyEye[k];
    }
    float cullingDistance = std::min(camFar, c.cullingDistance);
    EvaluateDepthParams(camNear, cullingDistance, c.depthDecodingParam, frustum.depthEncoding, frustum.depthDecoding);
    frustum.textureSize[0] = FROXEL_WIDTH;
    frustum.textureSize[1] = FROXEL_HEIGHT;
    frustum.textureSize[2] = volumetricDepth;
    frustum.flags = FLAG_RUNNING;
    for (int k = 0; k < 3; k++) frustum.invTextureSize[k] = 1.0f / static_cast<float>(frustum.textureSize[k]);
    std::memcpy(fogConstantsMapped + FOG_CB_FRUSTUM * FOG_CB_STRIDE, &frustum, sizeof(frustum));

    uint64_t frame = environment.frame >= 0 ? static_cast<uint64_t>(environment.frame) : frameCounter - 1;
    ControlParams params{};
    params.volumeFogCount = count;
    params.jitterInfo = (c.jitterNoise << 8) | static_cast<uint32_t>(frame & 0xFF);
    params.flags = FLAG_RUNNING | (volumetricHistory ? FLAG_HISTORY : 0) | (c.rejection ? FLAG_REJECTION : 0);
    params.blendFactor = c.blendFactor;
    params.cullingDistance = cullingDistance;
    params.edgeSoftness = c.softness;
    params.edgeOffsetPlus1 = 1.0f - std::sqrt(c.softness);
    params.rejectionSensitivity = c.rejectSensitivityFactor * c.rejectSensitivity * 0.01f;
    params.leakBias = c.leakBias;
    params.invVolumeFogCount = 1.0f / static_cast<float>(count);
    params.invEdgeSoftness = -1.0f / c.softness;
    std::memcpy(fogConstantsMapped + FOG_CB_PARAMS * FOG_CB_STRIDE, &params, sizeof(params));

    // shade() writes this frame's froxels and reads the other set as history.
    uint32_t current = volumetricFrame & 1;
    uint32_t history = current ^ 1;
    volumetricFrame++;
    const D3D12_RESOURCE_STATES uav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    const D3D12_RESOURCE_STATES computeRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_GPU_VIRTUAL_ADDRESS constants = fogConstants->GetGPUVirtualAddress();
    auto run = [&](const CacaoProgram& program, UINT x, UINT y, UINT z) {
        commandList->SetComputeRootSignature(program.root.Get());
        commandList->SetPipelineState(program.pso.Get());
        for (UINT i = 0; i < program.binds.size(); i++) {
            const IndirectBind& bind = program.binds[i];
            const std::string& n = bind.name;
            switch (bind.kind) {
                case IndirectBind::Cbv: {
                    D3D12_GPU_VIRTUAL_ADDRESS address = n == "SceneInfo"                    ? sceneInfoBuffer->GetGPUVirtualAddress()
                                                      : n == "LightInfo"                    ? lightInfoBuffer->GetGPUVirtualAddress()
                                                      : n == "FrustumVolume"                ? constants + FOG_CB_FRUSTUM * FOG_CB_STRIDE
                                                      : n == "VolumetricFogControlParams"   ? constants + FOG_CB_PARAMS * FOG_CB_STRIDE
                                                      : n == "GlobalVolumetricFog"          ? constants + FOG_CB_GLOBAL * FOG_CB_STRIDE
                                                                                            : zeroBuffer->GetGPUVirtualAddress();
                    commandList->SetComputeRootConstantBufferView(i, address);
                    break;
                }
                case IndirectBind::BufferSrv: {
                    ID3D12Resource* buffer = n == "LightParameterSRV" && lightParamsBuffer   ? lightParamsBuffer.Get()
                                           : n == "LightCullingListSRV" && lightListBuffer   ? lightListBuffer.Get()
                                           : n == "Sobel"                                    ? blueNoiseTables[0].Get()
                                           : n == "Scramble"                                 ? blueNoiseTables[1].Get()
                                           : n == "Rank"                                     ? blueNoiseTables[2].Get()
                                           : n == "VolumetricScatteringLightList"            ? scatteringLightList.Get()
                                           : n == "VolumetricFogList"                        ? volumetricFogList.Get()
                                                                                             : zeroBuffer.Get();
                    commandList->SetComputeRootShaderResourceView(i, buffer->GetGPUVirtualAddress());
                    break;
                }
                case IndirectBind::TextureSrv: {
                    uint32_t slot = n == "LightCullingVolumeSRV" ? SRV_LIGHT_VOLUME
                                  : n == "ShadowMapSRV"          ? SRV_LIGHT_SHADOW
                                  : n == "IESLightTableSRV"      ? SRV_LIGHT_IES
                                  : n == "ShadedFogTexture"      ? SRV_FOG_BASE + FOG_VIEW_SHADED_SRV + (&program == &injectProgram ? history : current)
                                                                 : SRV_LIGHT_BLACK;
                    commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(slot));
                    break;
                }
                case IndirectBind::Uav: {
                    uint32_t slot = n == "RWVolumetricFogTexture" ? SRV_FOG_BASE + FOG_VIEW_VOLUME_UAV
                                                                  : SRV_FOG_BASE + FOG_VIEW_SHADED_UAV + current;
                    commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(slot));
                    break;
                }
                case IndirectBind::NullBuffer:
                    commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(bind.nullView));
                    break;
            }
        }
        commandList->Dispatch(x, y, z);
    };
    auto groups = [](uint32_t size, uint32_t group) { return std::max<UINT>(1, (size + group - 1) / group); };

    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER before[2] = {
        Transition(shadedFog[history].Get(), uav, computeRead),
        Transition(shadowMap ? shadowMap.Get() : nullptr, read, read | computeRead),
    };
    commandList->ResourceBarrier(shadowMap ? 2 : 1, before);
    run(injectProgram, groups(FROXEL_WIDTH, SHADE_GROUP), groups(FROXEL_HEIGHT, SHADE_GROUP), groups(volumetricDepth, SHADE_GROUP));
    D3D12_RESOURCE_BARRIER shaded = Transition(shadedFog[current].Get(), uav, computeRead);
    commandList->ResourceBarrier(1, &shaded);
    run(integrateProgram, groups(FROXEL_WIDTH, integrateGroup), groups(FROXEL_HEIGHT, integrateGroup), 1);
    D3D12_RESOURCE_BARRIER after[4] = {
        Transition(shadedFog[history].Get(), computeRead, uav),
        Transition(shadedFog[current].Get(), computeRead, uav),
        Transition(volumetricFogTexture.Get(), uav, read),
        Transition(shadowMap ? shadowMap.Get() : nullptr, read | computeRead, read),
    };
    commandList->ResourceBarrier(shadowMap ? 4 : 3, after);
    volumetricHistory = true;
    volumetricRunning = true;
}

// Fog::draw without SeparateSky: one full-screen triangle of the Fog program.
void Viewer::DrawFog(bool draw) {
    if (!draw) volumetricRunning = false;
    if (!fogTarget) return;
    D3D12_RESOURCE_BARRIER toRt = Transition(fogTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &toRt);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = fogRtvHeap->GetCPUDescriptorHandleForHeapStart();
    const float none[4] = { 0, 0, 0, 1 };
    commandList->ClearRenderTargetView(rtv, none, 0, nullptr);
    bool fog = fogEnabled && showFlags.fog;
    if (draw && (fog || volumetricRunning) && fogPso) {
        if (fog) std::memcpy(fogConstantsMapped + FOG_CB_FOG_PARAM * FOG_CB_STRIDE, &fogParamCopy, sizeof(ViewerFogParam));
        else std::memset(fogConstantsMapped + FOG_CB_FOG_PARAM * FOG_CB_STRIDE, 0, sizeof(ViewerFogParam));
        commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        commandList->SetGraphicsRootSignature(fogRoot.Get());
        commandList->SetPipelineState(fogPso.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_GPU_VIRTUAL_ADDRESS constants = fogConstants->GetGPUVirtualAddress();
        for (UINT i = 0; i < fogBinds.size(); i++) {
            const IndirectBind& bind = fogBinds[i];
            const std::string& n = bind.name;
            switch (bind.kind) {
                case IndirectBind::Cbv: {
                    D3D12_GPU_VIRTUAL_ADDRESS address = n == "SceneInfo"     ? sceneInfoBuffer->GetGPUVirtualAddress()
                                                      : n == "FrustumVolume" ? constants + FOG_CB_FRUSTUM * FOG_CB_STRIDE
                                                      : n == "FogParam"      ? constants + FOG_CB_FOG_PARAM * FOG_CB_STRIDE
                                                                             : zeroBuffer->GetGPUVirtualAddress();
                    commandList->SetGraphicsRootConstantBufferView(i, address);
                    break;
                }
                case IndirectBind::BufferSrv:
                    commandList->SetGraphicsRootShaderResourceView(i, zeroBuffer->GetGPUVirtualAddress());
                    break;
                case IndirectBind::TextureSrv: {
                    uint32_t slot = n == "ReadonlyDepth"                             ? SRV_LIGHT_DEPTH
                                  : n == "VolumetricFogTexture" && volumetricRunning ? SRV_FOG_BASE + FOG_VIEW_VOLUME_SRV
                                                                                     : SRV_FOG_BASE + FOG_VIEW_BLACK_VOLUME;
                    commandList->SetGraphicsRootDescriptorTable(i, SrvGpuHandle(slot));
                    break;
                }
                case IndirectBind::Uav:
                    break;
                case IndirectBind::NullBuffer:
                    commandList->SetGraphicsRootDescriptorTable(i, SrvGpuHandle(bind.nullView));
                    break;
            }
        }
        commandList->DrawInstanced(3, 1, 0, 0);
    }
    D3D12_RESOURCE_BARRIER toSrv = Transition(fogTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &toSrv);
    if (volumetricRunning) {
        D3D12_RESOURCE_BARRIER back = Transition(volumetricFogTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->ResourceBarrier(1, &back);
    }
}
