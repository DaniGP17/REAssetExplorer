#include "Renderer/Viewer.h"

#include <algorithm>
#include <cstring>
#include <d3dcompiler.h>
#include <string>

#include "D3D12Utils.h"
#include "Renderer/RenderMath.h"

using Microsoft::WRL::ComPtr;

namespace {

struct ParticleConstants {
    float viewProj[16];
    float camRight[3];
    float nearZ;
    float camUp[3];
    float farZ;
    float intensity;
    float exposure;
    float tonemapRange;
    float histogramScale;
    uint32_t autoExposure;
    uint32_t lighting;
    float pad[2];
};

// Mirrors the Billboard3D/Ribbon path of the game's primitivevfx PS (cbEvOffsetPow2 = 1).
const char* PARTICLE_SOURCE = R"(
struct Particle {
    float3 position;
    float rotation;
    float2 size;
    uint textureIndex;
    uint flags;
    float4 color;
    float4 uvRect;
    float alphaRate;
    float emissiveRate;
    float softDistance;
    float evPow2;
    float3 axis;
    float detonemapRate;
    uint lightIndex;
    float3 pad;
};

struct ParticleLight {
    float3 diffuse;
    float reserved;
    float4 fogInfluence;
};

StructuredBuffer<Particle> particles : register(t0);
StructuredBuffer<ParticleLight> particleLights : register(t2);
Texture2D<float> sceneDepth : register(t1);
Texture2D textures[] : register(t0, space1);
RWByteAddressBuffer exposureState : register(u0);
SamplerState linearClamp : register(s0);

cbuffer Constants : register(b0) {
    row_major float4x4 viewProj;
    float3 camRight;
    float nearZ;
    float3 camUp;
    float farZ;
    float intensity;
    float exposure;
    float tonemapRange;
    float histogramScale;
    uint autoExposure;
    uint lighting;
};

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float4 color : COLOR0;
    nointerpolation uint2 textureFlags : TEXCOORD1;
    nointerpolation float3 shading : TEXCOORD2;
    nointerpolation float2 exposureTerms : TEXCOORD3;
};

VSOut VSMain(uint vertexId : SV_VertexID) {
    static const float2 corners[6] = {
        float2(-1, -1), float2(1, -1), float2(-1, 1),
        float2(-1, 1), float2(1, -1), float2(1, 1)
    };
    Particle p = particles[vertexId / 6];
    float2 c = corners[vertexId % 6];
    float2 t = c * 0.5 + 0.5;
    float3 world;
    float2 uv;
    if (p.flags & 16) {
        float3 side = cross(p.axis, cross(camRight, camUp));
        float len = length(side);
        side = len > 1e-6 ? side / len : camRight;
        world = p.position + p.axis * t.y + side * (c.x * 0.5 * p.size.x);
        uv = float2(lerp(p.uvRect.x, p.uvRect.z, t.x), lerp(p.uvRect.y, p.uvRect.w, t.y));
    } else {
        float s, co;
        sincos(p.rotation, s, co);
        float2 local = c * 0.5 * p.size;
        float2 r = float2(local.x * co - local.y * s, local.x * s + local.y * co);
        world = p.position + camRight * r.x + camUp * r.y;
        uv = float2(lerp(p.uvRect.x, p.uvRect.z, t.x), lerp(p.uvRect.w, p.uvRect.y, t.y));
    }

    VSOut o;
    o.pos = mul(float4(world, 1), viewProj);
    o.uv = uv;
    o.color = p.color;
    if (lighting != 0 && p.lightIndex != 0xFFFFFFFF) o.color.rgb *= particleLights[p.lightIndex].diffuse;
    o.textureFlags = uint2(p.textureIndex, p.flags);
    o.shading = float3(p.alphaRate, p.emissiveRate, p.softDistance);
    o.exposureTerms = float2(p.evPow2, p.detonemapRate);
    return o;
}

float LinearDepth(float z) {
    return nearZ * farZ / (farZ - z * (farZ - nearZ));
}

float4 PSMain(VSOut i) : SV_Target {
    float scene = LinearDepth(sceneDepth.Load(int3(i.pos.xy, 0)));
    float frag = LinearDepth(i.pos.z);
    float soft = i.shading.z > 0 ? saturate((scene - frag) / i.shading.z) : (frag <= scene ? 1.0 : 0.0);
    if (soft <= 0) discard;

    float4 tex = textures[NonUniformResourceIndex(i.textureFlags.x)].Sample(linearClamp, i.uv);
    if (i.textureFlags.y & 64) tex.a = pow(tex.a, 4.84);
    float a = pow(max(i.color.a, 1e-5), i.shading.x) * i.color.a * tex.a * soft;
    uint blend = i.textureFlags.y & 15;
    float3 rgb = tex.rgb * i.color.rgb * intensity;
    if (blend == 1) {
        float lum = dot(tex.rgb, float3(0.2126, 0.7152, 0.0722));
        rgb *= 1 + 37.5 * i.shading.y * i.exposureTerms.x * lum * lum;
    } else {
        rgb *= i.shading.y;
    }
    if (i.exposureTerms.y > 0) {
        float white = 1;
        if (autoExposure) {
            white = asfloat(exposureState.Load(0));
            if (white <= 0) white = histogramScale;
        }
        float3 detoned = rgb / max(1 - tonemapRange, 1e-4) / max(white * exposure, 1e-8);
        rgb = lerp(rgb, detoned, i.exposureTerms.y);
    }
    bool keepsAlpha = blend == 0 || (blend == 1 && (i.textureFlags.y & 32));
    return float4(rgb * a, keepsAlpha ? a : 0);
}
)";

}

void Viewer::CreateEffectTarget() {
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = desc.Format;
    clear.Color[3] = 1.0f;

    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                                          IID_PPV_ARGS(&effectTarget)), "CreateCommittedResource effect");
    effectTarget->SetName(L"EffectAccum");

    if (!effectRtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = 1;
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&effectRtvHeap)), "CreateDescriptorHeap effect RTV");
    }
    device->CreateRenderTargetView(effectTarget.Get(), nullptr, effectRtvHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateShaderResourceView(effectTarget.Get(), nullptr, SrvCpuHandle(SRV_EFFECT));
}

void Viewer::CreateParticlePipeline() {
    if (particlePso) return;

    D3D12_DESCRIPTOR_RANGE depthRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 };
    D3D12_DESCRIPTOR_RANGE textureRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 1, 0 };

    D3D12_ROOT_PARAMETER params[6]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = { 0, 0, sizeof(ParticleConstants) / 4 };
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor = { 0, 0 };
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable = { 1, &depthRange };
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable = { 1, &textureRange };
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[4].Descriptor = { 0, 0 };
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[5].Descriptor = { 2, 0 };
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 6;
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
          "D3D12SerializeRootSignature particles");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&particleRootSignature)), "CreateRootSignature particles");

    auto compile = [](const char* entry, const char* target) {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> compileErrors;
        HRESULT hr = D3DCompile(PARTICLE_SOURCE, std::strlen(PARTICLE_SOURCE), nullptr, nullptr, nullptr,
                                entry, target, D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES, 0,
                                &blob, &compileErrors);
        if (FAILED(hr)) {
            std::string msg = std::string("compile particle ") + entry;
            if (compileErrors) msg += std::string(": ") + static_cast<const char*>(compileErrors->GetBufferPointer());
            throw std::runtime_error(msg);
        }
        return blob;
    };
    ComPtr<ID3DBlob> vs = compile("VSMain", "vs_5_1");
    ComPtr<ID3DBlob> ps = compile("PSMain", "ps_5_1");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = particleRootSignature.Get();
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
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&particlePso)), "CreateGraphicsPipelineState particles");

    D3D12_HEAP_PROPERTIES uploadProps{};
    uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = static_cast<UINT64>(PARTICLE_CAPACITY) * sizeof(ViewerParticle);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Check(device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                          IID_PPV_ARGS(&particleBuffer)), "CreateCommittedResource particles");
    particleBuffer->SetName(L"Particles");
    D3D12_RANGE readRange{};
    Check(particleBuffer->Map(0, &readRange, reinterpret_cast<void**>(&particleMapped)), "Map particles");
}

// Safe to overwrite in place: RenderFrame waits for the GPU every frame.
void Viewer::SetParticles(std::span<const ViewerParticle> particles) {
    if (particleMapped == nullptr) {
        particleCount = 0;
        return;
    }
    std::size_t count = particles.size() < PARTICLE_CAPACITY ? particles.size() : PARTICLE_CAPACITY;
    std::memcpy(particleMapped, particles.data(), count * sizeof(ViewerParticle));
    particleCount = static_cast<uint32_t>(count);
}

// Expects the depth buffer in PIXEL_SHADER_RESOURCE: particles depth-test
// manually for soft particles.
void Viewer::RenderParticles() {
    if (!effectTarget) return;

    D3D12_RESOURCE_BARRIER toRt = Transition(effectTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &toRt);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = effectRtvHeap->GetCPUDescriptorHandleForHeapStart();
    const float clear[4] = { 0, 0, 0, 1 };
    commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    if (collisionView) DrawCollision(rtv);
    bool lighting = particleLightCount > 0 && particleLightPso && particleLightOutput;
    if (lighting) {
        ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
        DispatchParticleLighting();
    }
    RenderMaterialParticles(rtv, lighting);

    if (particleCount > 0 && particlePso) {
        Mat4 view;
        Mat4 proj;
        std::memcpy(view.m, camView, sizeof(view.m));
        std::memcpy(proj.m, camProj, sizeof(proj.m));
        Mat4 viewProj = Mul(view, proj);

        ParticleConstants constants{};
        std::memcpy(constants.viewProj, viewProj.m, sizeof(constants.viewProj));
        for (int i = 0; i < 3; i++) {
            constants.camRight[i] = view.m[i * 4 + 0];
            constants.camUp[i] = view.m[i * 4 + 1];
        }
        constants.nearZ = camNear;
        constants.farZ = camFar;
        bool lit = !unlit && !wireframe && gbufferView == GBufferView::None;
        constants.intensity = particleIntensity;
        constants.exposure = lit ? toneMap.exposure : 1.0f;
        constants.tonemapRange = temporalAA.tonemapRange;
        constants.histogramScale = 1.0f / std::max(toneMap.minWhite, 1e-6f);
        constants.autoExposure = lit && toneMap.autoExposure && exposureState ? 1u : 0u;
        constants.lighting = lighting ? 1u : 0u;

        commandList->SetGraphicsRootSignature(particleRootSignature.Get());
        commandList->SetPipelineState(particlePso.Get());
        commandList->SetGraphicsRoot32BitConstants(0, sizeof(ParticleConstants) / 4, &constants, 0);
        commandList->SetGraphicsRootShaderResourceView(1, particleBuffer->GetGPUVirtualAddress());
        commandList->SetGraphicsRootDescriptorTable(2, SrvGpuHandle(SRV_RESOLVE_DEPTH));
        commandList->SetGraphicsRootDescriptorTable(3, SrvGpuHandle(SRV_BINDLESS_BASE));
        commandList->SetGraphicsRootUnorderedAccessView(4, exposureState->GetGPUVirtualAddress());
        commandList->SetGraphicsRootShaderResourceView(5, (lighting ? particleLightOutput : zeroBuffer)->GetGPUVirtualAddress());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawInstanced(particleCount * 6, 1, 0, 0);
    }
    DrawDebugGeometry();

    D3D12_RESOURCE_BARRIER after[2] = {
        Transition(effectTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        Transition(lighting ? particleLightOutput.Get() : nullptr,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    };
    commandList->ResourceBarrier(lighting ? 2 : 1, after);
}
