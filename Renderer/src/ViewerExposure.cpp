#include "Renderer/Viewer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include <d3dcompiler.h>

#include "D3D12Utils.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT HISTOGRAM_BINS = 1024;
constexpr UINT EXPOSURE_STATE_BYTES = 64;
constexpr UINT EXPOSURE_CONSTANTS = 6;

// WhitePointCS: state [0] white point (1 / L), [1..8] the last L values, [9] next slot.
const char* EXPOSURE_SOURCE = R"(
RWByteAddressBuffer histogram : register(u0);
RWByteAddressBuffer state : register(u1);

cbuffer Constants : register(b0) {
    float minWhite;
    float whiteRatio;
    float whiteRange;
    float brightRate;
    float darkRate;
    uint reset;
};

groupshared uint sums[1024];
groupshared uint quantileBin;

[numthreads(1024, 1, 1)]
void CSMain(uint i : SV_GroupIndex) {
    sums[i] = histogram.Load(i * 4);
    histogram.Store(i * 4, 0);
    if (i == 0) quantileBin = 1023;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint offset = 1; offset < 1024; offset <<= 1) {
        uint add = i >= offset ? sums[i - offset] : 0;
        GroupMemoryBarrierWithGroupSync();
        sums[i] += add;
        GroupMemoryBarrierWithGroupSync();
    }
    uint total = sums[1023];
    if (sums[i] > (uint)(total * whiteRange)) InterlockedMin(quantileBin, i);
    GroupMemoryBarrierWithGroupSync();
    if (i != 0 || total == 0) return;

    float l = minWhite * exp2((quantileBin - 0.5) / 1024.0 * log2(whiteRatio));
    uint slot = reset ? 0 : state.Load(36) % 8;
    float sum = 0;
    for (uint k = 0; k < 8; k++) {
        float value = reset || k == slot ? l : asfloat(state.Load(4 + k * 4));
        state.Store(4 + k * 4, asuint(value));
        sum += value;
    }
    float average = clamp(sum * 0.125, 1e-8, 1e8);
    float previous = reset ? average : 1 / max(1e-8, asfloat(state.Load(0)));
    float adapted = previous + (average > previous ? brightRate : darkRate) * (average - previous);
    state.Store(0, asuint(1 / max(1e-8, adapted)));
    state.Store(36, slot + 1);
}
)";

ComPtr<ID3D12Resource> CreateUavBuffer(ID3D12Device* device, UINT64 bytes) {
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> buffer;
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                          nullptr, IID_PPV_ARGS(&buffer)), "CreateCommittedResource exposure");
    return buffer;
}

D3D12_RESOURCE_BARRIER UavBarrierOf(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

}

void Viewer::CreateExposureResources() {
    if (!exposureHistogram) {
        exposureHistogram = CreateUavBuffer(device.Get(), HISTOGRAM_BINS * 4);
        exposureState = CreateUavBuffer(device.Get(), EXPOSURE_STATE_BYTES);
    }
    ID3D12Resource* buffers[2] = { exposureHistogram.Get(), exposureState.Get() };
    UINT words[2] = { HISTOGRAM_BINS, EXPOSURE_STATE_BYTES / 4 };
    for (uint32_t i = 0; i < 2; i++) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_R32_TYPELESS;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = words[i];
        uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        device->CreateUnorderedAccessView(buffers[i], nullptr, &uav, SrvCpuHandle(UAV_EXPOSURE + i));
    }
    if (exposurePso) return;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[0].Descriptor = { 0, 0 };
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[1].Descriptor = { 1, 0 };
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants = { 0, 0, EXPOSURE_CONSTANTS };
    for (D3D12_ROOT_PARAMETER& p : params) p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 3;
    rootDesc.pParameters = params;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors), "exposure root signature");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&exposureRoot)),
          "exposure root signature");

    ComPtr<ID3DBlob> cs;
    if (FAILED(D3DCompile(EXPOSURE_SOURCE, std::strlen(EXPOSURE_SOURCE), nullptr, nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &cs,
                          &errors))) {
        throw std::runtime_error(std::string("compile exposure: ") +
                                 (errors ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = exposureRoot.Get();
    psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    Check(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&exposurePso)), "exposure PSO");
}

void Viewer::UpdateExposure() {
    D3D12_RESOURCE_BARRIER before[2] = { UavBarrierOf(exposureHistogram.Get()), UavBarrierOf(exposureState.Get()) };
    commandList->ResourceBarrier(2, before);
    struct {
        float minWhite;
        float whiteRatio;
        float whiteRange;
        float brightRate;
        float darkRate;
        uint32_t reset;
    } constants{ toneMap.minWhite, toneMap.maxWhite / std::max(toneMap.minWhite, 1e-6f), toneMap.whiteRange, toneMap.brightRate,
                 toneMap.darkRate, exposureReset ? 1u : 0u };
    static_assert(sizeof(constants) == EXPOSURE_CONSTANTS * 4);
    commandList->SetComputeRootSignature(exposureRoot.Get());
    commandList->SetPipelineState(exposurePso.Get());
    commandList->SetComputeRootUnorderedAccessView(0, exposureHistogram->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(1, exposureState->GetGPUVirtualAddress());
    commandList->SetComputeRoot32BitConstants(2, EXPOSURE_CONSTANTS, &constants, 0);
    commandList->Dispatch(1, 1, 1);
    commandList->ResourceBarrier(2, before);
    exposureReset = false;
}
