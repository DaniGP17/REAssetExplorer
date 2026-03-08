#include "Renderer/Viewer.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "D3D12Utils.h"
#include "DxilBindings.h"

using Microsoft::WRL::ComPtr;

namespace {

// IndirectIllumination's 256 threads cover a 16x16 tile (the game dispatches 120x68 groups at 1920x1080).
constexpr UINT GROUP_WIDTH = 16;
constexpr UINT GROUP_HEIGHT = 16;
constexpr uint32_t PROBE_CACHE_EMPTY = 0xFFFFFFFF;
// IBLCubemap2DArray: LightRenderer converts local cubemaps into 512x512 octahedral slices with 5 mips.
constexpr uint32_t LOCAL_CUBEMAP_WIDTH = 512;
constexpr uint32_t LOCAL_CUBEMAP_MIPS = 5;
constexpr uint32_t LOCAL_CUBEMAP_SLOTS = 128;
constexpr uint32_t LOCAL_CUBEMAP_RECORD_FLOATS = 16;
constexpr UINT CONVERT_GROUP_SIZE = 16;

}

ComPtr<ID3D12RootSignature> Viewer::CreateComputeRoot(const GameComputeDesc& desc, std::vector<IndirectBind>& binds,
                                                      bool (*bound)(const GameComputeSlot*)) {
    binds.clear();
    std::vector<D3D12_ROOT_PARAMETER> params;
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    std::vector<DxilBinding> bindings = ParseDxilBindings(desc.cs);
    ranges.reserve(bindings.size());

    auto slotOf = [&desc](GameComputeSlot::Kind kind, uint32_t reg) -> const GameComputeSlot* {
        for (const GameComputeSlot& s : desc.slots) {
            if (s.kind == kind && s.reg == reg) return &s;
        }
        return nullptr;
    };

    for (const DxilBinding& b : bindings) {
        if (b.type == 1) {
            const GameComputeSlot* s = slotOf(GameComputeSlot::Kind::Sampler, b.reg);
            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = s ? s->filter : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = s ? s->address : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc = s ? s->comparison : D3D12_COMPARISON_FUNC_NEVER;
            sampler.MaxAnisotropy = 16;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.ShaderRegister = b.reg;
            sampler.RegisterSpace = b.space;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            samplers.push_back(sampler);
            continue;
        }
        D3D12_ROOT_PARAMETER param{};
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        IndirectBind bind{};
        if (b.type == 2) {
            const GameComputeSlot* s = slotOf(GameComputeSlot::Kind::Cbv, b.reg);
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            param.Descriptor = { b.reg, b.space };
            bind = { IndirectBind::Cbv, s ? s->resource : GameComputeResource::Other };
        } else if ((b.type == 4 || b.type == 5) && bound && !bound(slotOf(GameComputeSlot::Kind::Srv, b.reg))) {
            ranges.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, b.reg, b.space, 0 });
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable = { 1, &ranges.back() };
            bind = { IndirectBind::NullBuffer, GameComputeResource::Other, {}, SRV_NULL_BUFFERS + (b.type == 5 ? 1u : 0u) };
        } else if (b.type == 4 || b.type == 5) {
            const GameComputeSlot* s = slotOf(GameComputeSlot::Kind::Srv, b.reg);
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            param.Descriptor = { b.reg, b.space };
            bind = { IndirectBind::BufferSrv, s ? s->resource : GameComputeResource::Other };
        } else {
            bool uav = b.type >= 6;
            const GameComputeSlot* s = slotOf(uav ? GameComputeSlot::Kind::Uav : GameComputeSlot::Kind::Srv, b.reg);
            ranges.push_back({ uav ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, b.reg, b.space, 0 });
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable = { 1, &ranges.back() };
            bind = { uav ? IndirectBind::Uav : IndirectBind::TextureSrv, s ? s->resource : GameComputeResource::Other };
        }
        for (GameComputeSlot::Kind kind : { GameComputeSlot::Kind::Cbv, GameComputeSlot::Kind::Srv, GameComputeSlot::Kind::Uav }) {
            bool matches = (kind == GameComputeSlot::Kind::Cbv) == (b.type == 2) &&
                           (kind == GameComputeSlot::Kind::Uav) == (b.type >= 6);
            if (const GameComputeSlot* s = matches ? slotOf(kind, b.reg) : nullptr) bind.name = s->name;
        }
        params.push_back(param);
        binds.push_back(bind);
    }

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(params.size());
    rootDesc.pParameters = params.data();
    rootDesc.NumStaticSamplers = static_cast<UINT>(samplers.size());
    rootDesc.pStaticSamplers = samplers.data();
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors))) {
        throw std::runtime_error(std::string("compute root signature: ") +
                                 (errors ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
    }
    ComPtr<ID3D12RootSignature> rootSignature;
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)),
          "CreateRootSignature compute");
    return rootSignature;
}

void Viewer::CreateIndirectIllumination(const GameComputeDesc& desc) {
    EnsureGameCommon();
    // DepthBlocker (probe blockers) is not bound: the game's newer lighting.sdf no longer reads it.
    indirectRoot = CreateComputeRoot(desc, indirectBinds,
                                     [](const GameComputeSlot* s) { return s != nullptr && s->resource != GameComputeResource::Other; });
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = indirectRoot.Get();
    psoDesc.CS = { desc.cs.data(), desc.cs.size() };
    Check(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&indirectPso)), "CreateComputePipelineState IndirectIllumination");

    if (!indirectCubemaps[0]) {
        Check(allocator->Reset(), "allocator Reset indirect");
        Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset indirect");
        std::vector<ComPtr<ID3D12Resource>> staging;
        std::vector<uint8_t> black(8, 0);
        GameTextureDesc single{ 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {} };
        single.mips.push_back({ black, 8 });
        indirectCubemaps[0] = CreateTexture(single, 1, commandList.Get(), staging);
        GameTextureDesc cube{ 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {} };
        for (int i = 0; i < 6; i++) cube.mips.push_back({ black, 8 });
        indirectCubemaps[1] = CreateTexture(cube, 6, commandList.Get(), staging);
        Check(commandList->Close(), "Close indirect");
        ID3D12CommandList* lists[] = { commandList.Get() };
        queue->ExecuteCommandLists(1, lists);
        WaitForGpu();

        D3D12_SHADER_RESOURCE_VIEW_DESC arraySrv{};
        arraySrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        arraySrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        arraySrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        arraySrv.Texture2DArray.MipLevels = 1;
        arraySrv.Texture2DArray.ArraySize = 1;
        device->CreateShaderResourceView(indirectCubemaps[0].Get(), &arraySrv, SrvCpuHandle(SRV_INDIRECT_CUBEMAP_ARRAY));
        D3D12_SHADER_RESOURCE_VIEW_DESC cubeSrv{};
        cubeSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        cubeSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        cubeSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        cubeSrv.TextureCube.MipLevels = 1;
        device->CreateShaderResourceView(indirectCubemaps[1].Get(), &cubeSrv, SrvCpuHandle(SRV_INDIRECT_CUBEMAP));
    }
    CreateIndirectTargets();
}

void Viewer::LoadLocalCubemaps(const GameComputeDesc& convert, std::span<const GameTextureDesc> cubes,
                               std::span<const float> records) {
    uint32_t count = static_cast<uint32_t>(std::min<std::size_t>({ cubes.size(), records.size() / LOCAL_CUBEMAP_RECORD_FLOATS,
                                                                  LOCAL_CUBEMAP_SLOTS }));
    if (count == 0) return;
    EnsureGameCommon();
    std::vector<IndirectBind> binds;
    ComPtr<ID3D12RootSignature> rootSignature = CreateComputeRoot(convert, binds);
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.CS = { convert.cs.data(), convert.cs.size() };
    ComPtr<ID3D12PipelineState> pso;
    Check(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso)), "CreateComputePipelineState CubemapTo2DOct");

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC arrayDesc{};
    arrayDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    arrayDesc.Width = LOCAL_CUBEMAP_WIDTH;
    arrayDesc.Height = LOCAL_CUBEMAP_WIDTH;
    arrayDesc.DepthOrArraySize = static_cast<UINT16>(count);
    arrayDesc.MipLevels = LOCAL_CUBEMAP_MIPS;
    arrayDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    arrayDesc.SampleDesc.Count = 1;
    arrayDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &arrayDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                          IID_PPV_ARGS(localCubemapArray.ReleaseAndGetAddressOf())),
          "CreateCommittedResource local cubemaps");

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = count * (1 + LOCAL_CUBEMAP_MIPS);
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)), "CreateDescriptorHeap CubemapTo2DOct");
    auto cpu = [&](uint32_t i) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(i) * srvStride;
        return h;
    };
    auto gpu = [&](uint32_t i) {
        D3D12_GPU_DESCRIPTOR_HANDLE h = heap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<UINT64>(i) * srvStride;
        return h;
    };

    struct Constants {
        uint32_t targetWidth;
        float inverseTargetWidthMinus1;
        float mipIndex;
        float blend;
        uint8_t pad[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 16];
    };
    std::vector<Constants> constants(count * LOCAL_CUBEMAP_MIPS);

    Check(allocator->Reset(), "allocator Reset local cubemaps");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset local cubemaps");
    std::vector<ComPtr<ID3D12Resource>> staging;
    std::vector<ComPtr<ID3D12Resource>> sources;
    for (uint32_t i = 0; i < count; i++) {
        const GameTextureDesc& cube = cubes[i];
        uint32_t cubeMips = static_cast<uint32_t>(cube.mips.size() / 6);
        sources.push_back(CreateTexture(cube, 6, commandList.Get(), staging));
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = static_cast<DXGI_FORMAT>(cube.format);
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.TextureCube.MipLevels = cubeMips;
        device->CreateShaderResourceView(sources.back().Get(), &srv, cpu(i * (1 + LOCAL_CUBEMAP_MIPS)));
        for (uint32_t mip = 0; mip < LOCAL_CUBEMAP_MIPS; mip++) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = DXGI_FORMAT_R11G11B10_FLOAT;
            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            uav.Texture2DArray.MipSlice = mip;
            uav.Texture2DArray.FirstArraySlice = i;
            uav.Texture2DArray.ArraySize = 1;
            device->CreateUnorderedAccessView(localCubemapArray.Get(), nullptr, &uav, cpu(i * (1 + LOCAL_CUBEMAP_MIPS) + 1 + mip));
            uint32_t targetWidth = LOCAL_CUBEMAP_WIDTH >> mip;
            constants[i * LOCAL_CUBEMAP_MIPS + mip] = { targetWidth, 1.0f / static_cast<float>(targetWidth - 1),
                                                        mip * (cubeMips - 1) / static_cast<float>(LOCAL_CUBEMAP_MIPS - 1), 0.0f, {} };
        }
    }
    ComPtr<ID3D12Resource> constantBuffer =
        CreateUploadBuffer(std::span(reinterpret_cast<const uint8_t*>(constants.data()), constants.size() * sizeof(Constants)));

    ID3D12DescriptorHeap* heaps[] = { heap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(rootSignature.Get());
    commandList->SetPipelineState(pso.Get());
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t mip = 0; mip < LOCAL_CUBEMAP_MIPS; mip++) {
            for (UINT p = 0; p < binds.size(); p++) {
                switch (binds[p].kind) {
                    case IndirectBind::Cbv:
                        commandList->SetComputeRootConstantBufferView(
                            p, constantBuffer->GetGPUVirtualAddress() + (i * LOCAL_CUBEMAP_MIPS + mip) * sizeof(Constants));
                        break;
                    case IndirectBind::TextureSrv:
                        commandList->SetComputeRootDescriptorTable(p, gpu(i * (1 + LOCAL_CUBEMAP_MIPS)));
                        break;
                    case IndirectBind::Uav:
                        commandList->SetComputeRootDescriptorTable(p, gpu(i * (1 + LOCAL_CUBEMAP_MIPS) + 1 + mip));
                        break;
                    case IndirectBind::BufferSrv:
                        commandList->SetComputeRootShaderResourceView(p, zeroBuffer->GetGPUVirtualAddress());
                        break;
                    case IndirectBind::NullBuffer:
                        commandList->SetComputeRootDescriptorTable(p, SrvGpuHandle(binds[p].nullView));
                        break;
                }
            }
            UINT groups = ((LOCAL_CUBEMAP_WIDTH >> mip) + CONVERT_GROUP_SIZE - 1) / CONVERT_GROUP_SIZE;
            commandList->Dispatch(groups, groups, 1);
        }
    }
    D3D12_RESOURCE_BARRIER toSrv = Transition(localCubemapArray.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &toSrv);
    Check(commandList->Close(), "Close local cubemaps");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    D3D12_SHADER_RESOURCE_VIEW_DESC arraySrv{};
    arraySrv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    arraySrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    arraySrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    arraySrv.Texture2DArray.MipLevels = LOCAL_CUBEMAP_MIPS;
    arraySrv.Texture2DArray.ArraySize = count;
    device->CreateShaderResourceView(localCubemapArray.Get(), &arraySrv, SrvCpuHandle(SRV_INDIRECT_CUBEMAP_ARRAY));

    std::vector<float> table(LOCAL_CUBEMAP_SLOTS * LOCAL_CUBEMAP_RECORD_FLOATS, 0.0f);
    std::copy_n(records.begin(), count * LOCAL_CUBEMAP_RECORD_FLOATS, table.begin());
    localCubemapRecords = CreateUploadBuffer(std::span(reinterpret_cast<const uint8_t*>(table.data()), table.size() * 4));
    localCubemapCount = count;
    UploadCullingVolume();
}

void Viewer::CreateIndirectTargets() {
    if (!indirectPso || !gidTarget) return;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_UINT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                          IID_PPV_ARGS(probeIndexCache.ReleaseAndGetAddressOf())), "CreateCommittedResource probe cache");
    if (!uavClearHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1;
        Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&uavClearHeap)), "CreateDescriptorHeap UAV clear");
    }
    device->CreateUnorderedAccessView(probeIndexCache.Get(), nullptr, nullptr, SrvCpuHandle(SRV_INDIRECT_UAVS));
    device->CreateUnorderedAccessView(probeIndexCache.Get(), nullptr, nullptr, uavClearHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateUnorderedAccessView(gidTarget.Get(), nullptr, nullptr, SrvCpuHandle(SRV_INDIRECT_UAVS + 1));
    device->CreateUnorderedAccessView(gisTarget.Get(), nullptr, nullptr, SrvCpuHandle(SRV_INDIRECT_UAVS + 2));
    probeCacheCleared = false;
}

// LightRenderer's probe fields of LightInfo; the smooth step rate is the RE8 capture's.
void Viewer::WriteProbeLightInfo(uint32_t tetrahedronCount) {
    if (!lightInfoMapped) return;
    struct {
        uint32_t lightProbeOffset;
        uint32_t sparseLightProbeAreaNum;
        uint32_t tetNumMinus1;
        uint32_t sparseTetNumMinus1;
        float smoothStepRateMinus;
        float smoothStepRateRcp;
        float reserve1;
        float reserve2;
    } probes{ 0, 0, tetrahedronCount - 1, 0, -19.0f, 20.0f, 1.0f, 0.0f };
    std::memcpy(lightInfoMapped + 272, &probes, sizeof(probes));
}

void Viewer::DispatchIndirectIllumination() {
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES readAll = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER before[5] = {
        Transition(depthBuffer.Get(), read, readAll),
        Transition(gbuffer[2].Get(), read, readAll),
        Transition(gbuffer[3].Get(), read, readAll),
        Transition(gidTarget.Get(), read, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        Transition(gisTarget.Get(), read, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    };
    commandList->ResourceBarrier(5, before);
    if (!probeCacheCleared) {
        const UINT empty[4] = { PROBE_CACHE_EMPTY, PROBE_CACHE_EMPTY, PROBE_CACHE_EMPTY, PROBE_CACHE_EMPTY };
        commandList->ClearUnorderedAccessViewUint(SrvGpuHandle(SRV_INDIRECT_UAVS), uavClearHeap->GetCPUDescriptorHandleForHeapStart(),
                                                  probeIndexCache.Get(), empty, 0, nullptr);
        D3D12_RESOURCE_BARRIER uav{};
        uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uav.UAV.pResource = probeIndexCache.Get();
        commandList->ResourceBarrier(1, &uav);
        probeCacheCleared = true;
    }

    commandList->SetComputeRootSignature(indirectRoot.Get());
    commandList->SetPipelineState(indirectPso.Get());
    for (UINT i = 0; i < indirectBinds.size(); i++) {
        const IndirectBind& bind = indirectBinds[i];
        switch (bind.kind) {
            case IndirectBind::Cbv: {
                ID3D12Resource* buffer = bind.resource == GameComputeResource::SceneInfo    ? sceneInfoBuffer.Get()
                                       : bind.resource == GameComputeResource::Environment  ? environmentBuffer.Get()
                                       : bind.resource == GameComputeResource::LightInfo    ? lightInfoBuffer.Get()
                                       : bind.resource == GameComputeResource::CheckerBoard ? checkerBoardBuffer.Get()
                                                                                            : zeroBuffer.Get();
                commandList->SetComputeRootConstantBufferView(i, buffer->GetGPUVirtualAddress());
                break;
            }
            case IndirectBind::BufferSrv: {
                ID3D12Resource* buffer = bind.resource == GameComputeResource::CullingList && lightListBuffer ? lightListBuffer.Get()
                                       : bind.resource == GameComputeResource::ProbeBspTree && probeBspTree ? probeBspTree.Get()
                                       : bind.resource == GameComputeResource::ProbeTetrahedra && probeBuffers[0] ? probeBuffers[0].Get()
                                       : bind.resource == GameComputeResource::ProbeValues && probeBuffers[1] ? probeBuffers[1].Get()
                                       : bind.resource == GameComputeResource::CubemapList && localCubemapRecords ? localCubemapRecords.Get()
                                                                                                                : zeroBuffer.Get();
                commandList->SetComputeRootShaderResourceView(i, buffer->GetGPUVirtualAddress());
                break;
            }
            case IndirectBind::TextureSrv: {
                uint32_t slot = bind.resource == GameComputeResource::Depth          ? SRV_LIGHT_DEPTH
                              : bind.resource == GameComputeResource::CullingVolume  ? SRV_LIGHT_VOLUME
                              : bind.resource == GameComputeResource::AmbientBrdf    ? SRV_AMBIENT_BRDF
                              : bind.resource == GameComputeResource::Normal         ? SRV_GBUFFER0 + 2
                              : bind.resource == GameComputeResource::Occlusion      ? SRV_GBUFFER0 + 3
                              : bind.resource == GameComputeResource::CubemapArray   ? SRV_INDIRECT_CUBEMAP_ARRAY
                              : bind.resource == GameComputeResource::Cubemap        ? SRV_INDIRECT_CUBEMAP
                                                                                     : SRV_LIGHT_BLACK;
                commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(slot));
                break;
            }
            case IndirectBind::Uav: {
                uint32_t slot = bind.resource == GameComputeResource::Gid ? SRV_INDIRECT_UAVS + 1
                              : bind.resource == GameComputeResource::Gis ? SRV_INDIRECT_UAVS + 2
                                                                          : SRV_INDIRECT_UAVS;
                commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(slot));
                break;
            }
            case IndirectBind::NullBuffer:
                commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(bind.nullView));
                break;
        }
    }
    commandList->Dispatch((width + GROUP_WIDTH - 1) / GROUP_WIDTH, (height + GROUP_HEIGHT - 1) / GROUP_HEIGHT, 1);

    D3D12_RESOURCE_BARRIER after[5];
    for (int i = 0; i < 5; i++) {
        after[i] = before[i];
        std::swap(after[i].Transition.StateBefore, after[i].Transition.StateAfter);
    }
    commandList->ResourceBarrier(5, after);
}
