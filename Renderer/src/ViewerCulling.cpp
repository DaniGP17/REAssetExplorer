#include "Renderer/Viewer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <d3dcompiler.h>
#include <string>

#include "D3D12Utils.h"
#include "Renderer/RenderMath.h"

using Microsoft::WRL::ComPtr;

namespace {

struct CullConstants {
    float viewProj[16];
    float planes[6][4];
    float hizSize[2];
    uint32_t hizMips;
    uint32_t drawCount;
};

const char* CULL_SOURCE = R"(
cbuffer Cull : register(b0) {
    row_major float4x4 viewProj;
    float4 planes[6];
    float2 hizSize;
    uint hizMips;
    uint drawCount;
};

StructuredBuffer<float4> spheres : register(t0);
ByteAddressBuffer enabled : register(t1);
Texture2D<float> hiz : register(t2);
RWByteAddressBuffer flags : register(u0);

// Depth is 0 near, 1 far; the pyramid keeps the farthest depth of each texel's footprint.
bool Occluded(float3 c, float r, float4x4 m) {
    float2 lo = 1e9;
    float2 hi = -1e9;
    float zNear = 1;
    [unroll]
    for (uint i = 0; i < 8; i++) {
        float3 p = c + r * float3((i & 1) ? 1 : -1, (i & 2) ? 1 : -1, (i & 4) ? 1 : -1);
        float4 h = mul(float4(p, 1), m);
        if (h.w <= 1e-3) return false;
        float3 n = h.xyz / h.w;
        lo = min(lo, n.xy);
        hi = max(hi, n.xy);
        zNear = min(zNear, n.z);
    }
    if (zNear <= 0) return false;
    float2 uvMin = float2(lo.x, -hi.y) * 0.5 + 0.5;
    float2 uvMax = float2(hi.x, -lo.y) * 0.5 + 0.5;
    if (uvMax.x <= 0 || uvMax.y <= 0 || uvMin.x >= 1 || uvMin.y >= 1) return false;
    float2 pMin = saturate(uvMin) * hizSize;
    float2 pMax = saturate(uvMax) * hizSize;
    float extent = max(pMax.x - pMin.x, pMax.y - pMin.y);
    uint level = min((uint)ceil(log2(max(extent, 1.0))), hizMips - 1);
    uint w, h, mips;
    hiz.GetDimensions(level, w, h, mips);
    float scale = 1.0 / (float)(1u << level);
    int2 a = min(int2(pMin * scale), int2(w - 1, h - 1));
    int2 b = min(int2(pMax * scale), int2(w - 1, h - 1));
    float d = max(max(hiz.Load(int3(a, level)), hiz.Load(int3(b.x, a.y, level))),
                  max(hiz.Load(int3(a.x, b.y, level)), hiz.Load(int3(b, level))));
    return zNear > d;
}

// Runs after the frame's opaque geometry, against its pyramid. Flags per draw, read by the
// next frame: 0 culled, 1 visible, 2 occluded.
[numthreads(64, 1, 1)]
void CSCull(uint id : SV_DispatchThreadID) {
    if (id >= drawCount) return;
    float4 s = spheres[id];
    uint flag = enabled.Load(id * 4) != 0 ? 1 : 0;
    if (flag != 0 && s.w > 0) {
        for (uint p = 0; p < 6; p++) {
            if (dot(planes[p].xyz, s.xyz) + planes[p].w < -s.w) flag = 0;
        }
        if (flag != 0 && Occluded(s.xyz, s.w, viewProj)) flag = 2;
    }
    flags.Store(id * 4, flag);
}
)";

const char* HIZ_SOURCE = R"(
cbuffer Build : register(b0) {
    uint2 srcSize;
    uint2 dstSize;
    uint fromDepth;
};
Texture2D<float> depth : register(t0);
RWTexture2D<float> src : register(u0);
RWTexture2D<float> dst : register(u1);

[numthreads(8, 8, 1)]
void CSHiZ(uint2 id : SV_DispatchThreadID) {
    if (id.x >= dstSize.x || id.y >= dstSize.y) return;
    if (fromDepth != 0) {
        dst[id] = depth.Load(int3(id, 0));
        return;
    }
    // Mips round down, so the last texel of an odd row or column also takes the leftover one.
    uint2 s = id * 2;
    uint2 e = min(s + 1 + uint2(id.x == dstSize.x - 1 ? srcSize.x & 1 : 0, id.y == dstSize.y - 1 ? srcSize.y & 1 : 0),
                  srcSize - 1);
    float d = 0;
    for (uint y = s.y; y <= e.y; y++) {
        for (uint x = s.x; x <= e.x; x++) d = max(d, src[uint2(x, y)]);
    }
    dst[id] = d;
}
)";

ComPtr<ID3DBlob> CompileCompute(const char* source, const char* entry) {
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, "cs_5_0", 0, 0, &blob, &errors);
    if (FAILED(hr)) {
        std::string msg = std::string("compile ") + entry;
        if (errors) msg += std::string(": ") + static_cast<const char*>(errors->GetBufferPointer());
        throw std::runtime_error(msg);
    }
    return blob;
}

D3D12_RESOURCE_BARRIER UavBarrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

void ViewProjection(const float view[16], const float proj[16], float out[16]) {
    Mat4 v;
    Mat4 p;
    std::memcpy(v.m, view, sizeof(v.m));
    std::memcpy(p.m, proj, sizeof(p.m));
    std::memcpy(out, Mul(v, p).m, sizeof(float) * 16);
}

}

void Viewer::CreateCullPipelines() {
    if (cullPso) return;

    auto makeRoot = [this](const D3D12_ROOT_PARAMETER* rootParams, UINT count, ComPtr<ID3D12RootSignature>& out,
                           const char* what) {
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = count;
        desc.pParameters = rootParams;
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors), what);
        Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                          IID_PPV_ARGS(&out)), what);
    };

    D3D12_DESCRIPTOR_RANGE hizRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, 0 };
    D3D12_ROOT_PARAMETER params[5]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor = { 0, 0 };
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor = { 0, 0 };
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[2].Descriptor = { 1, 0 };
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[3].Descriptor = { 0, 0 };
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[4].DescriptorTable = { 1, &hizRange };
    for (D3D12_ROOT_PARAMETER& p : params) p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    makeRoot(params, 5, cullRoot, "cull root signature");

    D3D12_DESCRIPTOR_RANGE depthRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
    D3D12_DESCRIPTOR_RANGE srcRange{ D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 };
    D3D12_DESCRIPTOR_RANGE dstRange{ D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, 0 };
    D3D12_ROOT_PARAMETER hizParams[4]{};
    hizParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    hizParams[0].Constants = { 0, 0, 5 };
    hizParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    hizParams[1].DescriptorTable = { 1, &depthRange };
    hizParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    hizParams[2].DescriptorTable = { 1, &srcRange };
    hizParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    hizParams[3].DescriptorTable = { 1, &dstRange };
    for (D3D12_ROOT_PARAMETER& p : hizParams) p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    makeRoot(hizParams, 4, hizRoot, "hi-z root signature");

    auto makePso = [this](ID3D12RootSignature* root, const char* source, const char* entry,
                          ComPtr<ID3D12PipelineState>& out) {
        ComPtr<ID3DBlob> cs = CompileCompute(source, entry);
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root;
        desc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        Check(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&out)), entry);
    };
    makePso(cullRoot.Get(), CULL_SOURCE, "CSCull", cullPso);
    makePso(hizRoot.Get(), HIZ_SOURCE, "CSHiZ", hizPso);

    cullConstants = CreateUploadBuffer(std::vector<uint8_t>(256, 0));
    D3D12_RANGE none{};
    Check(cullConstants->Map(0, &none, reinterpret_cast<void**>(&cullConstantsMapped)), "Map cull constants");
}

bool Viewer::EnsureCulling() {
    static const bool disabled = std::getenv("RAE_NO_GPU_CULL") != nullptr;
    if (disabled || deferredPipelines.empty() || draws.empty()) return false;
    CreateCullPipelines();

    if (cullDirty) {
        cullDirty = false;
        cullMaskDirty = true;
        cullDrawCount = static_cast<uint32_t>(draws.size());
        std::vector<float> spheres(draws.size() * 4);
        for (std::size_t i = 0; i < draws.size(); i++) {
            std::memcpy(&spheres[i * 4], draws[i].boundsCenter, sizeof(float) * 3);
            spheres[i * 4 + 3] = draws[i].boundsRadius;
        }
        cullSpheres = CreateUploadBuffer(std::span(reinterpret_cast<const uint8_t*>(spheres.data()), spheres.size() * 4));
        cullSpheres->SetName(L"CullSpheres");
        cullEnabled = CreateUploadBuffer(std::vector<uint8_t>(draws.size() * 4, 0));
        cullEnabled->SetName(L"CullEnabled");
        D3D12_RANGE none{};
        Check(cullEnabled->Map(0, &none, reinterpret_cast<void**>(&cullEnabledMapped)), "Map cull enabled");

        D3D12_HEAP_PROPERTIES props{};
        props.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = std::max<uint64_t>(static_cast<uint64_t>(draws.size()) * 4, 256);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                              nullptr, IID_PPV_ARGS(&cullFlags)), "CreateCommittedResource cull flags");
        cullFlags->SetName(L"CullFlags");
        props.Type = D3D12_HEAP_TYPE_READBACK;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                              nullptr, IID_PPV_ARGS(&cullFlagsReadback)), "CreateCommittedResource cull readback");
        // Until the first readback everything in the frustum is drawn.
        cullFlagsCpu.assign(draws.size(), 1);
        cullReadbackReady = false;
    }

    if (cullMaskDirty) {
        cullMaskDirty = false;
        auto* enabled = reinterpret_cast<uint32_t*>(cullEnabledMapped);
        for (std::size_t i = 0; i < draws.size(); i++) {
            enabled[i] = drawMask.empty() || (draws[i].id < drawMask.size() && drawMask[draws[i].id]) ? 1 : 0;
        }
    }

    if (!hizTexture || hizWidth != width || hizHeight != height) {
        hizWidth = width;
        hizHeight = height;
        hizMips = 1;
        while ((std::max(hizWidth, hizHeight) >> hizMips) > 0 && hizMips < HIZ_MAX_MIPS) hizMips++;
        D3D12_HEAP_PROPERTIES props{};
        props.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = hizWidth;
        desc.Height = hizHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = static_cast<UINT16>(hizMips);
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                              IID_PPV_ARGS(&hizTexture)), "CreateCommittedResource hi-z");
        hizTexture->SetName(L"HiZ");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = hizMips;
        device->CreateShaderResourceView(hizTexture.Get(), &srv, SrvCpuHandle(SRV_HIZ));
        for (uint32_t mip = 0; mip < hizMips; mip++) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = DXGI_FORMAT_R32_FLOAT;
            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uav.Texture2D.MipSlice = mip;
            device->CreateUnorderedAccessView(hizTexture.Get(), nullptr, &uav, SrvCpuHandle(SRV_HIZ_MIPS + mip));
        }
    }
    return true;
}

void Viewer::CullDraws() {
    CullConstants constants{};
    ViewProjection(camView, camProj, constants.viewProj);
    std::memcpy(constants.planes, frustumPlanes, sizeof(constants.planes));
    constants.hizSize[0] = static_cast<float>(hizWidth);
    constants.hizSize[1] = static_cast<float>(hizHeight);
    constants.hizMips = hizMips;
    constants.drawCount = cullDrawCount;
    std::memcpy(cullConstantsMapped, &constants, sizeof(constants));

    commandList->SetComputeRootSignature(cullRoot.Get());
    commandList->SetPipelineState(cullPso.Get());
    commandList->SetComputeRootConstantBufferView(0, cullConstants->GetGPUVirtualAddress());
    commandList->SetComputeRootShaderResourceView(1, cullSpheres->GetGPUVirtualAddress());
    commandList->SetComputeRootShaderResourceView(2, cullEnabled->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(3, cullFlags->GetGPUVirtualAddress());
    commandList->SetComputeRootDescriptorTable(4, SrvGpuHandle(SRV_HIZ));
    commandList->Dispatch((cullDrawCount + 63) / 64, 1, 1);

    D3D12_RESOURCE_BARRIER toCopy = Transition(cullFlags.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(1, &toCopy);
    commandList->CopyBufferRegion(cullFlagsReadback.Get(), 0, cullFlags.Get(), 0, static_cast<uint64_t>(cullDrawCount) * 4);
    D3D12_RESOURCE_BARRIER back = Transition(cullFlags.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier(1, &back);
    cullReadbackReady = true;
}

void Viewer::ReadCullFlags() {
    if (!cullReadbackReady) return;
    cullReadbackReady = false;
    uint32_t* flags = nullptr;
    D3D12_RANGE range{ 0, static_cast<SIZE_T>(cullDrawCount) * 4 };
    Check(cullFlagsReadback->Map(0, &range, reinterpret_cast<void**>(&flags)), "Map cull flags");
    std::memcpy(cullFlagsCpu.data(), flags, static_cast<std::size_t>(cullDrawCount) * 4);
    D3D12_RANGE none{};
    cullFlagsReadback->Unmap(0, &none);
}

void Viewer::BuildHiZ() {
    D3D12_RESOURCE_BARRIER begin[2] = {
        Transition(hizTexture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        Transition(depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
    };
    commandList->ResourceBarrier(2, begin);

    commandList->SetComputeRootSignature(hizRoot.Get());
    commandList->SetPipelineState(hizPso.Get());
    commandList->SetComputeRootDescriptorTable(1, SrvGpuHandle(SRV_RESOLVE_DEPTH));
    uint32_t srcW = hizWidth;
    uint32_t srcH = hizHeight;
    for (uint32_t mip = 0; mip < hizMips; mip++) {
        uint32_t dstW = std::max(1u, hizWidth >> mip);
        uint32_t dstH = std::max(1u, hizHeight >> mip);
        uint32_t constants[5] = { srcW, srcH, dstW, dstH, mip == 0 ? 1u : 0u };
        commandList->SetComputeRoot32BitConstants(0, 5, constants, 0);
        commandList->SetComputeRootDescriptorTable(2, SrvGpuHandle(SRV_HIZ_MIPS + (mip == 0 ? 0 : mip - 1)));
        commandList->SetComputeRootDescriptorTable(3, SrvGpuHandle(SRV_HIZ_MIPS + mip));
        commandList->Dispatch((dstW + 7) / 8, (dstH + 7) / 8, 1);
        D3D12_RESOURCE_BARRIER written = UavBarrier(hizTexture.Get());
        commandList->ResourceBarrier(1, &written);
        srcW = dstW;
        srcH = dstH;
    }

    D3D12_RESOURCE_BARRIER end[2] = {
        Transition(hizTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        Transition(depthBuffer.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
    };
    commandList->ResourceBarrier(2, end);
}
