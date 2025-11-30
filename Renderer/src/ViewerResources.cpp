#include "Renderer/Viewer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "D3D12Utils.h"
#include "ParallelFor.h"
#include "Renderer/RenderMath.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT64 TEXTURE_STAGING_BYTES = 256ull << 20;
constexpr UINT64 TEXTURE_HEAP_BYTES = 256ull << 20;

struct TextureLayout {
    D3D12_RESOURCE_DESC desc{};
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
    std::vector<UINT> rows;
    std::vector<UINT64> rowSizes;
    UINT64 bytes = 0;
};

TextureLayout DescribeTexture(ID3D12Device* device, const GameTextureDesc& desc, uint32_t arraySize) {
    TextureLayout layout;
    bool volume = desc.depth > 1;
    layout.desc.Dimension = volume ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    layout.desc.Width = desc.width;
    layout.desc.Height = desc.height;
    layout.desc.DepthOrArraySize = static_cast<UINT16>(volume ? desc.depth : arraySize);
    layout.desc.MipLevels = static_cast<UINT16>(volume ? desc.mips.size() : desc.mips.size() / arraySize);
    layout.desc.Format = static_cast<DXGI_FORMAT>(desc.format);
    layout.desc.SampleDesc.Count = 1;

    UINT subresourceCount = static_cast<UINT>(desc.mips.size());
    layout.footprints.resize(subresourceCount);
    layout.rows.resize(subresourceCount);
    layout.rowSizes.resize(subresourceCount);
    device->GetCopyableFootprints(&layout.desc, 0, subresourceCount, 0, layout.footprints.data(), layout.rows.data(),
                                  layout.rowSizes.data(), &layout.bytes);
    return layout;
}

ComPtr<ID3D12Resource> CreateTextureResource(ID3D12Device* device, const TextureLayout& layout) {
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &layout.desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          IID_PPV_ARGS(&texture)), "CreateCommittedResource texture");
    return texture;
}

ComPtr<ID3D12Resource> CreateStagingBuffer(ID3D12Device* device, UINT64 bytes) {
    D3D12_HEAP_PROPERTIES uploadProps{};
    uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = bytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload;
    Check(device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&upload)), "CreateCommittedResource texture upload");
    return upload;
}

void WriteTextureData(const GameTextureDesc& desc, const TextureLayout& layout, uint8_t* mapped) {
    for (std::size_t s = 0; s < layout.footprints.size(); s++) {
        const GameTextureMip& mip = desc.mips[s];
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = layout.footprints[s];
        uint8_t* dst = mapped + footprint.Offset;
        const uint8_t* src = mip.data.data();
        UINT64 copyBytes = layout.rowSizes[s] < mip.pitch ? layout.rowSizes[s] : mip.pitch;
        UINT rows = layout.rows[s];
        std::size_t sourceSlice = static_cast<std::size_t>(mip.pitch) * rows;
        for (UINT slice = 0; slice < footprint.Footprint.Depth; slice++) {
            if ((slice + 1) * sourceSlice > mip.data.size()) break;
            for (UINT row = 0; row < rows; row++) {
                std::memcpy(dst + (slice * rows + row) * footprint.Footprint.RowPitch, src + slice * sourceSlice + row * mip.pitch,
                            copyBytes);
            }
        }
    }
}

void RecordTextureCopy(ID3D12GraphicsCommandList* list, ID3D12Resource* texture, const TextureLayout& layout,
                       ID3D12Resource* upload, UINT64 uploadOffset) {
    for (std::size_t s = 0; s < layout.footprints.size(); s++) {
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = texture;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = static_cast<UINT>(s);

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = upload;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = layout.footprints[s];
        srcLoc.PlacedFootprint.Offset += uploadOffset;

        list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    }
    // Character shaders sample material textures from the vertex stage too.
    D3D12_RESOURCE_BARRIER barrier = Transition(texture, D3D12_RESOURCE_STATE_COPY_DEST,
                                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    list->ResourceBarrier(1, &barrier);
}

}

void Viewer::LoadMesh(const ViewerMesh& mesh) {
    positionBuffer = CreateUploadBuffer(mesh.positions);
    positionBuffer->SetName(L"Positions");
    normalBuffer = CreateUploadBuffer(mesh.normals);
    normalBuffer->SetName(L"Normals");
    indexBuffer = CreateUploadBuffer(mesh.indices);
    indexBuffer->SetName(L"Indices");

    positionView = { positionBuffer->GetGPUVirtualAddress(), static_cast<UINT>(mesh.positions.size()), 12 };
    normalView = { normalBuffer->GetGPUVirtualAddress(), static_cast<UINT>(mesh.normals.size()), 8 };
    indexView = { indexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(mesh.indices.size()), DXGI_FORMAT_R16_UINT };

    if (!mesh.uv0.empty()) {
        uv0Buffer = CreateUploadBuffer(mesh.uv0);
        uv0View = { uv0Buffer->GetGPUVirtualAddress(), static_cast<UINT>(mesh.uv0.size()), 4 };
    }
    if (!mesh.uv1.empty()) {
        uv1Buffer = CreateUploadBuffer(mesh.uv1);
        uv1View = { uv1Buffer->GetGPUVirtualAddress(), static_cast<UINT>(mesh.uv1.size()), 4 };
    }
    if (!mesh.weights.empty()) {
        weightsBuffer = CreateUploadBuffer(mesh.weights);
        weightsBuffer->SetName(L"SkinWeights");
        weightsView = { weightsBuffer->GetGPUVirtualAddress(), static_cast<UINT>(mesh.weights.size()), 16 };
    }

    draws = mesh.draws;
    cullDirty = true;

    float center[3];
    float extent[3];
    for (int i = 0; i < 3; i++) {
        center[i] = (mesh.aabbMin[i] + mesh.aabbMax[i]) * 0.5f;
        extent[i] = mesh.aabbMax[i] - mesh.aabbMin[i];
    }
    float radius = 0.5f * std::sqrt(extent[0] * extent[0] + extent[1] * extent[1] + extent[2] * extent[2]);

    std::memcpy(meshCenter, center, sizeof(meshCenter));
    meshRadius = radius;

    camNear = radius * 0.01f;
    camFar = radius * 100.0f;
    Mat4 proj = PerspectiveFovRH(camFov, static_cast<float>(width) / static_cast<float>(height),
                                 camNear, camFar);
    std::memcpy(camProj, proj.m, sizeof(camProj));

    float eye[3] = { center[0], center[1] + radius * 0.3f, center[2] - radius * 2.0f };
    SetCamera(eye, center);

    hasMesh = true;
}

void Viewer::SetCamera(const float eye[3], const float target[3], const float up[3]) {
    const float worldUp[3] = { 0, 1, 0 };
    Mat4 view = LookAtRH(eye, target, up ? up : worldUp);
    std::memcpy(camView, view.m, sizeof(camView));
    std::memcpy(camEye, eye, sizeof(camEye));
    if (sceneInfoMapped) ApplySceneMatrix();
}

void Viewer::GetMeshBounds(float outCenter[3], float& outRadius) const {
    for (int i = 0; i < 3; i++) outCenter[i] = meshCenter[i];
    outRadius = meshRadius;
}

void Viewer::SetClipPlanes(float nearPlane, float farPlane) {
    camNear = nearPlane;
    camFar = farPlane;
    Mat4 proj = PerspectiveFovRH(camFov, static_cast<float>(width) / static_cast<float>(height),
                                 camNear, camFar);
    std::memcpy(camProj, proj.m, sizeof(camProj));
    if (sceneInfoMapped) ApplySceneMatrix();
}

void Viewer::SetFov(float fov) {
    if (fov == camFov) return;
    camFov = fov;
    Mat4 proj = PerspectiveFovRH(camFov, static_cast<float>(width) / static_cast<float>(height),
                                 camNear, camFar);
    std::memcpy(camProj, proj.m, sizeof(camProj));
    if (sceneInfoMapped) ApplySceneMatrix();
}

void Viewer::Resize(uint32_t newWidth, uint32_t newHeight) {
    if (newWidth == 0 || newHeight == 0) return;
    if (newWidth == width && newHeight == height) return;
    WaitForGpu();

    width = newWidth;
    height = newHeight;

    for (UINT i = 0; i < FRAME_COUNT; i++) renderTargets[i].Reset();
    Check(swapChain->ResizeBuffers(FRAME_COUNT, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0),
          "ResizeBuffers");
    frameIndex = swapChain->GetCurrentBackBufferIndex();
    CreateRenderTargets();
    CreateDepthBuffer();

    if (srvHeap) {
        if (gbuffer[0]) CreateGBufferTargets();
        if (hdrTarget) CreateHdrTargets();
    }

    Mat4 proj = PerspectiveFovRH(camFov, static_cast<float>(width) / static_cast<float>(height),
                                 camNear, camFar);
    std::memcpy(camProj, proj.m, sizeof(camProj));
    if (sceneInfoMapped) ApplySceneMatrix();
}

void Viewer::EnsureGameCommon() {
    if (srvHeap) return;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = SRV_HEAP_SIZE;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&srvHeap)), "CreateDescriptorHeap SRV");
    srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC nullRaw{};
    nullRaw.Format = DXGI_FORMAT_R32_TYPELESS;
    nullRaw.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    nullRaw.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullRaw.Buffer.NumElements = 1;
    nullRaw.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    device->CreateShaderResourceView(nullptr, &nullRaw, SrvCpuHandle(SRV_NULL_BUFFERS));
    D3D12_SHADER_RESOURCE_VIEW_DESC nullStructured{};
    nullStructured.Format = DXGI_FORMAT_UNKNOWN;
    nullStructured.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    nullStructured.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullStructured.Buffer.NumElements = 1;
    nullStructured.Buffer.StructureByteStride = 4;
    device->CreateShaderResourceView(nullptr, &nullStructured, SrvCpuHandle(SRV_NULL_BUFFERS + 1));

    // Values bound by the game (RE8 capture): gbufferTypeFlag and reserves 1; cbr (1, 1), no bias, checkerboard off.
    const float gbufferType[4] = { 1, 1, 1, 1 };
    const float checkerBoard[4] = { 1, 1, 0, 0 };
    gbufferTypeBuffer = CreateUploadBuffer({ reinterpret_cast<const uint8_t*>(gbufferType), sizeof(gbufferType) });
    tonemapBuffer = CreateUploadBuffer(std::vector<uint8_t>(256, 0));
    D3D12_RANGE noRead{};
    Check(tonemapBuffer->Map(0, &noRead, reinterpret_cast<void**>(&tonemapMapped)), "Map tonemap");
    environmentBuffer = CreateUploadBuffer(std::vector<uint8_t>(768, 0));
    Check(environmentBuffer->Map(0, &noRead, reinterpret_cast<void**>(&environmentMapped)), "Map environment");
    checkerBoardBuffer = CreateUploadBuffer({ reinterpret_cast<const uint8_t*>(checkerBoard), sizeof(checkerBoard) });

    // Backs every zero-bound root CBV/SRV; sized so indexed reads stay in bounds
    // (PivotBuffer lookups reach ~3 KB, cbuffers 4 KB).
    std::vector<uint8_t> zeros(65536, 0);
    zeroBuffer = CreateUploadBuffer(zeros);

    D3D12_HEAP_PROPERTIES scratchProps{};
    scratchProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC scratchDesc{};
    scratchDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    scratchDesc.Width = 65536;
    scratchDesc.Height = 1;
    scratchDesc.DepthOrArraySize = 1;
    scratchDesc.MipLevels = 1;
    scratchDesc.SampleDesc.Count = 1;
    scratchDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    scratchDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Check(device->CreateCommittedResource(&scratchProps, D3D12_HEAP_FLAG_NONE, &scratchDesc,
                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                          IID_PPV_ARGS(&scratchUavBuffer)), "CreateCommittedResource scratch UAV");

    if (!instanceBuffer) LoadInstances({ ViewerInstanceWorld{{ 1,0,0,0, 0,1,0,0, 0,0,1,0 }} }, 112);

    const float identity[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
    SetSkinningMatrices(identity);

    std::vector<uint8_t> sceneZeros(528, 0);
    sceneInfoBuffer = CreateUploadBuffer(sceneZeros);
    D3D12_RANGE readRange{};
    Check(sceneInfoBuffer->Map(0, &readRange, reinterpret_cast<void**>(&sceneInfoMapped)), "Map sceneInfo");
}

D3D12_CPU_DESCRIPTOR_HANDLE Viewer::SrvCpuHandle(uint32_t index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * srvStride;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE Viewer::SrvGpuHandle(uint32_t index) const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = srvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * srvStride;
    return handle;
}

Microsoft::WRL::ComPtr<ID3D12Resource> Viewer::CreateTexture(const GameTextureDesc& desc, uint32_t arraySize,
                                                             ID3D12GraphicsCommandList* uploadList,
                                                             std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& staging) {
    TextureLayout layout = DescribeTexture(device.Get(), desc, arraySize);
    ComPtr<ID3D12Resource> texture = CreateTextureResource(device.Get(), layout);
    ComPtr<ID3D12Resource> upload = CreateStagingBuffer(device.Get(), layout.bytes);
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{};
    Check(upload->Map(0, &readRange, reinterpret_cast<void**>(&mapped)), "Map texture upload");
    WriteTextureData(desc, layout, mapped);
    upload->Unmap(0, nullptr);
    RecordTextureCopy(uploadList, texture.Get(), layout, upload.Get(), 0);
    staging.push_back(upload);
    return texture;
}

// InstanceWorld element (112B): worldMat float3x4, prevWorldMat float3x4,
// jointOffset u32, prevJointOffset u32. At least 1024 elements so character VS
// joint reads (jointOffset 0 + joint index) stay in bounds.
void Viewer::LoadInstances(const std::vector<ViewerInstanceWorld>& worlds, uint32_t stride) {
    std::size_t elements = worlds.size() + 1;
    if (elements < 1024) elements = 1024;
    std::vector<uint8_t> data(elements * stride, 0);
    for (std::size_t i = 0; i < worlds.size(); i++) {
        uint8_t* element = data.data() + i * stride;
        std::memcpy(element, worlds[i].m, sizeof(worlds[i].m));
        std::memcpy(element + 48, worlds[i].m, sizeof(worlds[i].m));
        std::memcpy(element + 96, &worlds[i].jointOffset, 4);
        std::memcpy(element + 100, &worlds[i].jointOffset, 4);
    }
    instanceBuffer = CreateUploadBuffer(data);
    instanceBuffer->SetName(L"InstanceWorld");
    instanceStride = stride;
    instanceCount = elements;
}

// Upload heap; RenderFrame waits for the GPU, so it is free between frames.
void Viewer::SetInstanceWorld(uint32_t index, const float world[12]) {
    if (!instanceBuffer || index >= instanceCount || instanceStride < 96) return;
    uint8_t* mapped = nullptr;
    D3D12_RANGE none{};
    if (FAILED(instanceBuffer->Map(0, &none, reinterpret_cast<void**>(&mapped)))) return;
    std::memcpy(mapped + static_cast<std::size_t>(index) * instanceStride, world, 48);
    std::memcpy(mapped + static_cast<std::size_t>(index) * instanceStride + 48, world, 48);
    D3D12_RANGE written{ static_cast<SIZE_T>(index) * instanceStride, static_cast<SIZE_T>(index + 1) * instanceStride };
    instanceBuffer->Unmap(0, &written);
}

// RenderFrame waits for the GPU, so the mapped buffer is free between frames.
void Viewer::SetSkinningMatrices(std::span<const float> matrices) {
    std::size_t count = matrices.size() / 12;
    if (count > skinningCapacity || !skinningBuffer) {
        skinningCapacity = count < 256 ? 256 : count;
        std::vector<uint8_t> zeros(skinningCapacity * 48, 0);
        skinningBuffer = CreateUploadBuffer(zeros);
        skinningBuffer->SetName(L"SkinningMatrices");
        D3D12_RANGE readRange{};
        Check(skinningBuffer->Map(0, &readRange, reinterpret_cast<void**>(&skinningMapped)), "Map skinning");
    }
    std::memcpy(skinningMapped, matrices.data(), count * 48);
}

void Viewer::SetCameraLights(uint32_t first, const std::vector<GameLightParam>& lights) {
    cameraLightFirst = first;
    cameraLights = lights;
    UpdateCameraLights();
}

void Viewer::UpdateCameraLights() {
    if (cameraLights.empty() || !lightParamsBuffer || cameraLightFirst + cameraLights.size() > lightCount) return;
    Mat4 view;
    std::memcpy(view.m, camView, sizeof(view.m));
    uint8_t* mapped = nullptr;
    D3D12_RANGE none{};
    if (FAILED(lightParamsBuffer->Map(0, &none, reinterpret_cast<void**>(&mapped)))) return;
    for (std::size_t i = 0; i < cameraLights.size(); i++) {
        GameLightParam light = cameraLights[i];
        const float* p = cameraLights[i].position;
        const float* d = cameraLights[i].direction;
        for (int c = 0; c < 3; c++) {
            // camView is orthonormal: its inverse rotation is the transpose, the eye its translation.
            light.position[c] = camEye[c] + p[0] * view.m[c * 4] + p[1] * view.m[c * 4 + 1] + p[2] * view.m[c * 4 + 2];
            light.direction[c] = d[0] * view.m[c * 4] + d[1] * view.m[c * 4 + 1] + d[2] * view.m[c * 4 + 2];
        }
        std::memcpy(mapped + (cameraLightFirst + i) * sizeof(GameLightParam), &light, sizeof(light));
    }
    D3D12_RANGE written{ cameraLightFirst * sizeof(GameLightParam), (cameraLightFirst + cameraLights.size()) * sizeof(GameLightParam) };
    lightParamsBuffer->Unmap(0, &written);
}

void Viewer::UpdateLight(uint32_t index, const GameLightParam& light) {
    if (!lightParamsBuffer || index >= lightCount) return;
    uint8_t* mapped = nullptr;
    D3D12_RANGE none{};
    if (FAILED(lightParamsBuffer->Map(0, &none, reinterpret_cast<void**>(&mapped)))) return;
    std::memcpy(mapped + static_cast<std::size_t>(index) * sizeof(GameLightParam), &light, sizeof(light));
    D3D12_RANGE written{ index * sizeof(GameLightParam), (index + 1) * sizeof(GameLightParam) };
    lightParamsBuffer->Unmap(0, &written);
}

void Viewer::LoadLights(const std::vector<GameLightParam>& lights) {
    std::vector<uint8_t> params(lights.size() * sizeof(GameLightParam));
    if (!lights.empty()) std::memcpy(params.data(), lights.data(), params.size());
    if (params.empty()) params.resize(sizeof(GameLightParam), 0);
    lightParamsBuffer = CreateUploadBuffer(params);
    lightCount = static_cast<uint32_t>(lights.size());
    localCubemapCount = 0;
    UploadCullingVolume();
}

// Clustered-culling inputs that make every pixel iterate every light and local cubemap: a 1x2x1
// culling volume whose texels are (bitmaskWordCount | wordOffset << 8), lights at y 0 and cubemaps at
// y + LightCullingOffsetScale * 32 = 1, and full bitmask words.
void Viewer::UploadCullingVolume() {
    uint32_t lightWords = std::max(1u, (lightCount + 31) / 32);
    uint32_t cubemapWords = (localCubemapCount + 31) / 32;
    std::vector<uint32_t> words(lightWords + cubemapWords, 0);
    for (uint32_t i = 0; i < lightCount; i++) words[i / 32] |= 1u << (i % 32);
    for (uint32_t i = 0; i < localCubemapCount; i++) words[lightWords + i / 32] |= 1u << (i % 32);
    lightListBuffer = CreateUploadBuffer(std::span(reinterpret_cast<const uint8_t*>(words.data()), words.size() * 4));

    if (lightInfoMapped) {
        float cullingScreen[4] = { 1, 1, 1, 1 };
        float offsetScale = 1.0f / 32.0f;
        std::memcpy(lightInfoMapped + 0, &lightCount, 4);
        std::memcpy(lightInfoMapped + 16, cullingScreen, 16);
        std::memcpy(lightInfoMapped + 32, &offsetScale, 4);
        std::memcpy(lightInfoMapped + 44, &localCubemapCount, 4);
    }

    Check(allocator->Reset(), "allocator Reset lights");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset lights");
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> staging;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC volumeDesc{};
    volumeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    volumeDesc.Width = 1;
    volumeDesc.Height = 2;
    volumeDesc.DepthOrArraySize = 1;
    volumeDesc.MipLevels = 1;
    volumeDesc.Format = DXGI_FORMAT_R32_UINT;
    volumeDesc.SampleDesc.Count = 1;

    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &volumeDesc,
                                          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          IID_PPV_ARGS(lightVolumeTexture.ReleaseAndGetAddressOf())), "CreateCommittedResource light volume");

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT rows;
    UINT64 rowSize, total;
    device->GetCopyableFootprints(&volumeDesc, 0, 1, 0, &footprint, &rows, &rowSize, &total);

    std::vector<uint8_t> texels(total, 0);
    uint32_t lightTexel = lightWords;
    uint32_t cubemapTexel = cubemapWords | (lightWords << 8);
    std::memcpy(texels.data(), &lightTexel, 4);
    std::memcpy(texels.data() + footprint.Footprint.RowPitch, &cubemapTexel, 4);
    Microsoft::WRL::ComPtr<ID3D12Resource> upload = CreateUploadBuffer(texels);
    staging.push_back(upload);

    D3D12_TEXTURE_COPY_LOCATION dst{ lightVolumeTexture.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
    D3D12_TEXTURE_COPY_LOCATION src{ upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {} };
    src.PlacedFootprint = footprint;
    commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = Transition(lightVolumeTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_UINT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture3D.MipLevels = 1;
    device->CreateShaderResourceView(lightVolumeTexture.Get(), &srvDesc, SrvCpuHandle(SRV_LIGHT_VOLUME));

    Check(commandList->Close(), "Close lights upload");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();
}

void Viewer::LoadGameMaterials(const std::vector<GameMaterialDesc>& materials,
                               const std::vector<GameTextureDesc>& textures,
                               const std::vector<std::vector<uint32_t>>& instanceMaterials) {
    Check(allocator->Reset(), "allocator Reset upload");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset upload");

    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> staging;

    GameTextureDesc noiseDesc;
    noiseDesc.width = 16;
    noiseDesc.height = 16;
    noiseDesc.format = DXGI_FORMAT_R8_UNORM;
    std::vector<uint8_t> noiseData(16 * 16, 0x80);
    for (int i = 0; i < 16; i++) {
        noiseDesc.mips.push_back({ std::span<const uint8_t>(noiseData), 16 });
    }
    blueNoise = CreateTexture(noiseDesc, 16, commandList.Get(), staging);

    D3D12_SHADER_RESOURCE_VIEW_DESC noiseSrv{};
    noiseSrv.Format = DXGI_FORMAT_R8_UNORM;
    noiseSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    noiseSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    noiseSrv.Texture2DArray.MipLevels = 1;
    noiseSrv.Texture2DArray.ArraySize = 16;
    device->CreateShaderResourceView(blueNoise.Get(), &noiseSrv, SrvCpuHandle(SRV_BLUE_NOISE));

    std::vector<uint8_t> white(4, 0xFF);
    GameTextureDesc whiteDesc{ 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 6, {} };
    for (int i = 0; i < 6; i++) whiteDesc.mips.push_back({ std::span<const uint8_t>(white), 4 });
    whiteCube = CreateTexture(whiteDesc, 6, commandList.Get(), staging);
    D3D12_SHADER_RESOURCE_VIEW_DESC whiteSrv{};
    whiteSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    whiteSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    whiteSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    whiteSrv.TextureCube.MipLevels = 1;

    // Shaders index the space4 (Texture2D) and space5 (Texture2DArray) tables
    // with the same record index.
    std::size_t textureCount = textures.size();
    if (textureCount > SRV_BINDLESS_CAPACITY) {
        std::fprintf(stderr, "LoadGameMaterials: %zu textures exceed the bindless heap capacity (%u); extra ones dropped\n",
                     textureCount, SRV_BINDLESS_CAPACITY);
        textureCount = SRV_BINDLESS_CAPACITY;
    }
    // Placed in a few large heaps: one committed allocation per texture costs more than the copies.
    std::vector<TextureLayout> layouts(textureCount);
    std::vector<D3D12_RESOURCE_ALLOCATION_INFO> allocations(textureCount);
    ParallelFor(textureCount, [&](std::size_t t) {
        uint32_t arraySize = textures[t].arraySize == 0 ? 1 : textures[t].arraySize;
        layouts[t] = DescribeTexture(device.Get(), textures[t], arraySize);
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& top = layouts[t].footprints[0];
        UINT64 topBytes = static_cast<UINT64>(top.Footprint.RowPitch) * layouts[t].rows[0] * top.Footprint.Depth;
        // Only textures whose top mip fits in 64 KB may take the small alignment.
        if (topBytes <= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
            layouts[t].desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
            allocations[t] = device->GetResourceAllocationInfo(0, 1, &layouts[t].desc);
        }
        if (allocations[t].Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
            layouts[t].desc.Alignment = 0;
            allocations[t] = device->GetResourceAllocationInfo(0, 1, &layouts[t].desc);
        }
    });
    std::vector<std::pair<ID3D12Heap*, UINT64>> placements(textureCount);
    for (std::size_t t = 0, heapUsed = TEXTURE_HEAP_BYTES, heapSize = TEXTURE_HEAP_BYTES; t < textureCount; t++) {
        UINT64 offset = (heapUsed + allocations[t].Alignment - 1) & ~(allocations[t].Alignment - 1);
        if (offset + allocations[t].SizeInBytes > heapSize) {
            D3D12_HEAP_DESC heapDesc{};
            heapDesc.SizeInBytes = std::max(TEXTURE_HEAP_BYTES, allocations[t].SizeInBytes);
            heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
            ComPtr<ID3D12Heap> heap;
            Check(device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap)), "CreateHeap textures");
            materialHeaps.push_back(heap);
            heapSize = heapDesc.SizeInBytes;
            offset = 0;
        }
        placements[t] = { materialHeaps.back().Get(), offset };
        heapUsed = offset + allocations[t].SizeInBytes;
    }
    std::size_t firstTexture = materialTextures.size();
    materialTextures.resize(firstTexture + textureCount);
    ParallelFor(textureCount, [&](std::size_t t) {
        uint32_t arraySize = textures[t].arraySize == 0 ? 1 : textures[t].arraySize;
        ComPtr<ID3D12Resource> texture;
        Check(device->CreatePlacedResource(placements[t].first, placements[t].second, &layouts[t].desc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)),
              "CreatePlacedResource texture");
        wchar_t texName[32];
        swprintf(texName, 32, L"Tex%zu", t);
        texture->SetName(texName);
        uint32_t mipLevels = layouts[t].desc.MipLevels;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = static_cast<DXGI_FORMAT>(textures[t].format);
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = mipLevels;
        device->CreateShaderResourceView(texture.Get(), &srvDesc,
                                         SrvCpuHandle(SRV_BINDLESS_BASE + static_cast<uint32_t>(t)));

        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MipLevels = mipLevels;
        srvDesc.Texture2DArray.ArraySize = arraySize;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        device->CreateShaderResourceView(texture.Get(), &srvDesc,
                                         SrvCpuHandle(SRV_BINDLESS_ARRAY_BASE + static_cast<uint32_t>(t)));

        D3D12_CPU_DESCRIPTOR_HANDLE cube = SrvCpuHandle(SRV_BINDLESS_CUBE_BASE + static_cast<uint32_t>(t));
        if (arraySize == 6 && textures[t].width == textures[t].height && textures[t].depth <= 1) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube = { 0, mipLevels, 0.0f };
            device->CreateShaderResourceView(texture.Get(), &srvDesc, cube);
        } else {
            device->CreateShaderResourceView(whiteCube.Get(), &whiteSrv, cube);
        }

        materialTextures[firstTexture + t] = texture;
    });

    // Batches share one staging buffer; a texture larger than it gets its own.
    ComPtr<ID3D12Resource> shared = CreateStagingBuffer(device.Get(), TEXTURE_STAGING_BYTES);
    uint8_t* sharedMapped = nullptr;
    D3D12_RANGE noRead{};
    Check(shared->Map(0, &noRead, reinterpret_cast<void**>(&sharedMapped)), "Map texture staging");
    std::vector<UINT64> offsets(textureCount);
    for (std::size_t begin = 0; begin < textureCount;) {
        std::size_t end = begin;
        UINT64 used = 0;
        for (; end < textureCount; end++) {
            UINT64 offset = (used + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
            if (offset + layouts[end].bytes > TEXTURE_STAGING_BYTES) break;
            offsets[end] = offset;
            used = offset + layouts[end].bytes;
        }
        if (end == begin) {
            ComPtr<ID3D12Resource> upload = CreateStagingBuffer(device.Get(), layouts[begin].bytes);
            uint8_t* mapped = nullptr;
            Check(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)), "Map texture upload");
            WriteTextureData(textures[begin], layouts[begin], mapped);
            upload->Unmap(0, nullptr);
            RecordTextureCopy(commandList.Get(), materialTextures[firstTexture + begin].Get(), layouts[begin], upload.Get(), 0);
            staging.push_back(upload);
            end = begin + 1;
        } else {
            ParallelFor(end - begin, [&](std::size_t i) {
                WriteTextureData(textures[begin + i], layouts[begin + i], sharedMapped + offsets[begin + i]);
            });
            for (std::size_t t = begin; t < end; t++) {
                RecordTextureCopy(commandList.Get(), materialTextures[firstTexture + t].Get(), layouts[t], shared.Get(), offsets[t]);
            }
        }
        begin = end;
        if (begin < textureCount) {
            Check(commandList->Close(), "Close texture upload");
            ID3D12CommandList* batch[] = { commandList.Get() };
            queue->ExecuteCommandLists(1, batch);
            WaitForGpu();
            staging.clear();
            Check(allocator->Reset(), "allocator Reset texture upload");
            Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset texture upload");
        }
    }

    // Material record: mdf2 param block, then one u32 heap index per texture at
    // offset PropertyDataBlockSize. Padded so a mismatched shader reads zeros.
    std::vector<uint8_t> bindlessData;
    std::vector<uint32_t> recordOffsets;

    materialParamSizes.clear();
    for (const GameMaterialDesc& material : materials) {
        recordOffsets.push_back(static_cast<uint32_t>(bindlessData.size()));
        materialParamSizes.push_back(static_cast<uint32_t>(material.paramBlock.size()));

        bindlessData.insert(bindlessData.end(), material.paramBlock.begin(), material.paramBlock.end());

        for (uint32_t textureIndex : material.textureIndices) {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&textureIndex);
            bindlessData.insert(bindlessData.end(), bytes, bytes + 4);
        }
        while (bindlessData.size() - recordOffsets.back() < 1024) bindlessData.push_back(0);
    }

    // Redirect table: the shader reads entry [instanceIdx * 255 + materialSlot].
    constexpr uint32_t SLOTS_PER_INSTANCE = 255;
    std::size_t instanceCount = instanceMaterials.empty() ? 1 : instanceMaterials.size();
    std::vector<uint32_t> redirect(instanceCount * SLOTS_PER_INSTANCE, 0);
    for (std::size_t i = 0; i < instanceMaterials.size(); i++) {
        for (std::size_t s = 0; s < instanceMaterials[i].size() && s < SLOTS_PER_INSTANCE; s++) {
            uint32_t materialIndex = instanceMaterials[i][s];
            if (materialIndex < recordOffsets.size()) {
                redirect[i * SLOTS_PER_INSTANCE + s] = recordOffsets[materialIndex];
            }
        }
    }

    bindlessDataBuffer = CreateUploadBuffer(bindlessData);
    bindlessDataBuffer->SetName(L"BindlessData");
    materialRecordOffsets = recordOffsets;
    redirectBuffer = CreateUploadBuffer(std::span(reinterpret_cast<const uint8_t*>(redirect.data()), redirect.size() * 4));
    redirectBuffer->SetName(L"RedirectTbl");

    Check(commandList->Close(), "Close upload");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();
    shared->Unmap(0, nullptr);
}

void Viewer::UpdateMaterialParams(uint32_t materialIndex, uint32_t offset, std::span<const uint8_t> bytes) {
    if (!bindlessDataBuffer || materialIndex >= materialRecordOffsets.size()) return;
    if (offset + bytes.size() > materialParamSizes[materialIndex]) return;
    uint8_t* mapped = nullptr;
    D3D12_RANGE none{};
    Check(bindlessDataBuffer->Map(0, &none, reinterpret_cast<void**>(&mapped)), "Map material params");
    std::memcpy(mapped + materialRecordOffsets[materialIndex] + offset, bytes.data(), bytes.size());
    D3D12_RANGE written{ materialRecordOffsets[materialIndex] + offset, materialRecordOffsets[materialIndex] + offset + bytes.size() };
    bindlessDataBuffer->Unmap(0, &written);
}

void Viewer::EnsureStandalonePasses() {
    EnsureGameCommon();
    if (!gbuffer[0]) CreateGBufferTargets();
    if (!resolvePso) CreateResolvePipeline();
    CreateParticlePipeline();
    if (!hdrTarget) {
        // The resolve reads the HDR slot even with nothing lit.
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
        nullSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(nullptr, &nullSrv, SrvCpuHandle(SRV_HDR));
    }
}

void Viewer::LoadEffectTextures(const std::vector<GameTextureDesc>& textures) {
    EnsureStandalonePasses();
    // One empty material: the upload expects at least one record.
    LoadGameMaterials({ GameMaterialDesc{} }, textures, {});
}

void Viewer::LoadSky(const GameTextureDesc& desc) {
    EnsureStandalonePasses();

    Check(allocator->Reset(), "allocator Reset sky");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset sky");
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> staging;

    skyTexture = CreateTexture(desc, 1, commandList.Get(), staging);
    skyMips = static_cast<uint32_t>(desc.mips.size());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = static_cast<DXGI_FORMAT>(desc.format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = static_cast<UINT>(desc.mips.size());
    device->CreateShaderResourceView(skyTexture.Get(), &srvDesc, SrvCpuHandle(SRV_SKY));

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depthBuffer.Get(), &depthSrv, SrvCpuHandle(SRV_RESOLVE_DEPTH));

    Check(commandList->Close(), "Close sky upload");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();
}

// Field offsets follow the RE7 SceneInfo layout recovered from shader reflection.
void Viewer::ApplySceneMatrix() {
    Mat4 view;
    Mat4 proj;
    std::memcpy(view.m, camView, sizeof(view.m));
    std::memcpy(proj.m, camProj, sizeof(proj.m));
    proj.m[8] += projectionJitter[0] * proj.m[11];
    proj.m[9] += projectionJitter[1] * proj.m[11];
    Mat4 viewProj = Mul(view, proj);

    // Gribb-Hartmann planes, normalized for the sphere cull test.
    {
        const float* m = viewProj.m;
        auto setPlane = [this](int i, float a, float b, float c, float d) {
            float len = std::sqrt(a * a + b * b + c * c);
            if (len < 1e-6f) len = 1;
            frustumPlanes[i][0] = a / len;
            frustumPlanes[i][1] = b / len;
            frustumPlanes[i][2] = c / len;
            frustumPlanes[i][3] = d / len;
        };
        setPlane(0, m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]);
        setPlane(1, m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]);
        setPlane(2, m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]);
        setPlane(3, m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]);
        setPlane(4, m[2], m[6], m[10], m[14]);
        setPlane(5, m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);
    }

    Mat4 viewInv{};
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            viewInv.m[r * 4 + c] = view.m[c * 4 + r];
        }
    }
    viewInv.m[12] = camEye[0];
    viewInv.m[13] = camEye[1];
    viewInv.m[14] = camEye[2];
    viewInv.m[15] = 1;

    const float* p = proj.m;
    Mat4 projInv{};
    projInv.m[0] = 1.0f / p[0];
    projInv.m[5] = 1.0f / p[5];
    projInv.m[11] = 1.0f / p[14];
    projInv.m[12] = -p[8] / (p[0] * p[11]);
    projInv.m[13] = -p[9] / (p[5] * p[11]);
    projInv.m[14] = 1.0f / p[11];
    projInv.m[15] = -p[10] / (p[14] * p[11]);
    Mat4 viewProjInv = Mul(projInv, viewInv);
    Mat4 unjittered;
    std::memcpy(unjittered.m, camProj, sizeof(unjittered.m));
    Mat4 prevViewProj = Mul(view, unjittered);
    std::memcpy(frameViewProjection, prevViewProj.m, sizeof(frameViewProjection));
    if (temporalFrame && hasPrevViewProjection) std::memcpy(prevViewProj.m, prevViewProjection, sizeof(prevViewProj.m));

    auto write = [&](std::size_t offset, const void* src, std::size_t bytes) {
        std::memcpy(sceneInfoMapped + offset, src, bytes);
    };
    auto transpose34 = [](const Mat4& m, float* out) {
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                out[r * 4 + c] = m.m[c * 4 + r];
            }
        }
    };

    float t34[12];
    write(0, viewProj.m, 64);
    transpose34(view, t34);
    write(64, t34, 48);
    transpose34(viewInv, t34);
    write(112, t34, 48);
    const float* pi = projInv.m;
    float projElement[8] = { p[0], p[5], p[8], p[9], p[10], p[11], p[14], p[15] };
    write(160, projElement, 32);
    float projInvElements[8] = { pi[0], pi[5], pi[10], pi[11], pi[12], pi[13], pi[14], pi[15] };
    write(192, projInvElements, 32);
    write(224, viewProjInv.m, 64);
    write(288, prevViewProj.m, 64);
    // Scene::updateSceneInfo: linear depth = z / (x - y * depth) scaled so z is the near plane.
    UpdateCameraLights();
    float depthScale = camNear / p[14];
    float zToLinear[3] = { p[10] * depthScale, -depthScale, camNear };
    write(352, zToLinear, 12);
    float screenSizeValues[2] = { static_cast<float>(width), static_cast<float>(height) };
    write(368, screenSizeValues, 8);
    float screenInverse[2] = { 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height) };
    write(376, screenInverse, 8);
    // cullingHelper.y = 0 forces light culling Z slice 0.
    float cullingHelper[2] = { 1.0f, 0.0f };
    write(384, cullingHelper, 8);
    write(392, &camNear, 4);
    write(396, &camFar, 4);
    float vrsVelocityThreshold[2] = { 0.25f, 0.25f * static_cast<float>(width) / static_cast<float>(height) };
    write(512, vrsVelocityThreshold, 8);
    // Light channel mask; the lighting shader ANDs it with DL_Enable.
    uint32_t renderOutputId = 0xFFFFFFFF;
    write(520, &renderOutputId, 4);

    std::memcpy(skyEye, camEye, sizeof(skyEye));
    std::memcpy(skyViewProjInv, viewProjInv.m, sizeof(skyViewProjInv));
}

void Viewer::SetDirectionalLight(const float direction[3], const float color[3], float minAlpha, const float* scattering) {
    float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    float normalized[3] = { direction[0] / length, direction[1] / length, direction[2] / length };
    uint32_t enable = 1;
    std::memcpy(lightInfoMapped + 48, normalized, 12);
    std::memcpy(lightInfoMapped + 60, &enable, 4);
    std::memcpy(lightInfoMapped + 64, color, 12);
    std::memcpy(lightInfoMapped + 76, &minAlpha, 4);
    const float none[3] = {};
    std::memcpy(lightInfoMapped + 80, scattering ? scattering : none, 12);
}

void Viewer::ClearDirectionalLight() {
    uint32_t enable = 0;
    float black[3] = {};
    std::memcpy(lightInfoMapped + 60, &enable, 4);
    std::memcpy(lightInfoMapped + 64, black, 12);
    std::memcpy(lightInfoMapped + 80, black, 12);
}

void Viewer::LoadSceneIbl(const GameTextureDesc& sky, const GameTextureDesc* add, const GameTextureDesc& filtered,
                          const ViewerSceneIbl& params) {
    Check(allocator->Reset(), "allocator Reset scene IBL");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset scene IBL");
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> staging;
    const GameTextureDesc* descs[3] = { &sky, add, &filtered };
    for (uint32_t i = 0; i < 3; i++) {
        sceneIblTextures[i].Reset();
        if (!descs[i]) continue;
        sceneIblTextures[i] = CreateTexture(*descs[i], 1, commandList.Get(), staging);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = static_cast<DXGI_FORMAT>(descs[i]->format);
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = static_cast<UINT>(descs[i]->mips.size());
        device->CreateShaderResourceView(sceneIblTextures[i].Get(), &srvDesc, SrvCpuHandle(SRV_SCENE_IBL + i));
    }
    Check(commandList->Close(), "Close scene IBL upload");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    sceneIbl = params;
    sceneIblFilteredMips = std::max<uint32_t>(static_cast<uint32_t>(filtered.mips.size()), 1);
    hasSceneIbl = true;
    BindIndirectTargets();
}

void Viewer::LoadLightProbes(std::span<const uint32_t> tetrahedra, std::span<const uint32_t> values,
                             std::span<const uint32_t> grid, std::span<const uint8_t> bspTree) {
    Check(allocator->Reset(), "allocator Reset light probes");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset light probes");
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> staging;
    std::span<const uint32_t> sources[4] = { tetrahedra, values, grid,
                                             { reinterpret_cast<const uint32_t*>(bspTree.data()), bspTree.size() / 4 } };
    for (uint32_t i = 0; i < 4; i++) {
        std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(sources[i].data()), sources[i].size_bytes());
        if (bytes.empty()) continue;
        ComPtr<ID3D12Resource>& target = i < 3 ? probeBuffers[i] : probeBspTree;
        staging.push_back(CreateUploadBuffer(bytes));

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes.size();
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                              nullptr, IID_PPV_ARGS(target.ReleaseAndGetAddressOf())), "CreateCommittedResource probes");
        commandList->CopyBufferRegion(target.Get(), 0, staging.back().Get(), 0, bytes.size());
        D3D12_RESOURCE_BARRIER toSrv = Transition(target.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &toSrv);
        if (i == 3) continue;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R32_UINT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = static_cast<UINT>(sources[i].size());
        device->CreateShaderResourceView(probeBuffers[i].Get(), &srvDesc, SrvCpuHandle(SRV_PROBE_TETRAHEDRA + i));
    }
    Check(commandList->Close(), "Close light probe upload");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    hasLightProbes = true;
    probeCacheCleared = false;
    probeTetrahedronCount = static_cast<uint32_t>(tetrahedra.size() * 4 / 80);
    WriteProbeLightInfo(probeTetrahedronCount);
    BindIndirectTargets();
}

void Viewer::LoadAmbientBrdf(const GameTextureDesc& desc) {
    Check(allocator->Reset(), "allocator Reset ambient BRDF");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset ambient BRDF");
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> staging;
    ambientBrdf = CreateTexture(desc, 1, commandList.Get(), staging);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = static_cast<DXGI_FORMAT>(desc.format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = static_cast<UINT>(desc.mips.size());
    device->CreateShaderResourceView(ambientBrdf.Get(), &srvDesc, SrvCpuHandle(SRV_LIGHT_BRDF));
    device->CreateShaderResourceView(ambientBrdf.Get(), &srvDesc, SrvCpuHandle(SRV_AMBIENT_BRDF));
    Check(commandList->Close(), "Close ambient BRDF upload");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();
}
