#include "Renderer/Viewer.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string_view>

#include "D3D12Utils.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr std::size_t CONSTANT_STRIDE = 256;
constexpr std::size_t PRIMITIVE_MATERIAL_CONSTANT_SIZE = 192;
constexpr std::size_t PARTICLE_LIGHT_GROUP = 64;
constexpr std::size_t PARTICLE_LIGHT_RESULT_SIZE = 32;

bool ParticleLightBuffer(const GameComputeSlot* slot) {
    static constexpr std::string_view BOUND[] = { "LightParameterSRV", "IBLCubemapArrayList2SRV", "BSPTree", "TetraCoordinate",
                                                  "IndirectProbe", "LightedParticles" };
    return slot != nullptr && std::find(std::begin(BOUND), std::end(BOUND), slot->name) != std::end(BOUND);
}

// PrimitiveMaterialConstant as Polygon3DConstant::Initialize leaves it: fades, scale by depth and the
// occlusion test all off.
void WritePrimitiveMaterialConstant(uint8_t* out, const float emitterPosition[3], uint32_t flags) {
    std::memset(out, 0, PRIMITIVE_MATERIAL_CONSTANT_SIZE);
    auto f = [&](std::size_t offset, float v) { std::memcpy(out + offset, &v, 4); };
    auto u = [&](std::size_t offset, uint32_t v) { std::memcpy(out + offset, &v, 4); };
    f(48, 1.0f);
    f(52, -FLT_MAX);
    f(56, FLT_MAX);
    f(60, 1.0f);
    u(44, flags);
    for (int c = 0; c < 3; c++) f(64 + c * 4, emitterPosition[c]);
    u(80, 0xFFFFFFFF);
    f(88, 1.0f);
    f(112, -FLT_MAX);
    f(116, -FLT_MAX);
    f(120, FLT_MAX);
    f(124, FLT_MAX);
    f(128, -1.0f);
    f(132, -1.0f);
}

}

void Viewer::SetMaterialPrograms(std::vector<ViewerMaterialProgram> programs) {
    materialPasses.clear();
    materialBatches.clear();
    for (ViewerMaterialProgram& program : programs) {
        MaterialPass pass;
        pass.program = std::move(program);
        const ViewerMaterialProgram& p = pass.program;
        GameComputeDesc vsDesc{ p.vs, p.vsSlots };
        GameComputeDesc psDesc{ p.ps, p.psSlots };
        const PassStage stages[] = { { &vsDesc, D3D12_SHADER_VISIBILITY_VERTEX }, { &psDesc, D3D12_SHADER_VISIBILITY_PIXEL } };
        pass.root = CreatePassRoot(stages, pass.binds, true);

        const D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "GENERIC", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "GENERIC", 1, DXGI_FORMAT_R32G32B32A32_UINT, 0, 104, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "GENERIC", 2, DXGI_FORMAT_R32G32B32A32_UINT, 0, 72, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 104, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "GENERIC", 3, DXGI_FORMAT_R32_UINT, 0, 104, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "GENERIC", 4, DXGI_FORMAT_R32G32B32A32_UINT, 0, 88, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 104, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 104, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "GENERIC", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "GENERIC", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        static_assert(sizeof(ViewerMaterialVertex) == 120);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = pass.root.Get();
        psoDesc.VS = { p.vs.data(), p.vs.size() };
        psoDesc.PS = { p.ps.data(), p.ps.size() };
        psoDesc.InputLayout = { layout, static_cast<UINT>(std::size(layout)) };

        uint32_t raster;
        std::memcpy(&raster, p.raster.data(), 4);
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = (raster & 0x100) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.FrontCounterClockwise = (raster >> 10) & 1;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;

        // The game writes into a transparent buffer composited like the effect target (scene * a + rgb), so
        // its first target's color factors carry over; alpha only keeps the transmittance.
        uint32_t w;
        std::memcpy(&w, p.blend.data(), 4);
        D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[0];
        rt.BlendEnable = (w & 1) ? TRUE : FALSE;
        rt.SrcBlend = static_cast<D3D12_BLEND>((w >> 1) & 0x1F);
        rt.DestBlend = static_cast<D3D12_BLEND>((w >> 6) & 0x1F);
        rt.BlendOp = static_cast<D3D12_BLEND_OP>(((w >> 11) & 0x7) ? (w >> 11) & 0x7 : 1);
        rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
        rt.DestBlendAlpha = rt.DestBlend == D3D12_BLEND_SRC_ALPHA || rt.DestBlend == D3D12_BLEND_INV_SRC_ALPHA
                                ? rt.DestBlend : D3D12_BLEND_ONE;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (!rt.BlendEnable) {
            rt.SrcBlend = D3D12_BLEND_ONE;
            rt.DestBlend = D3D12_BLEND_ZERO;
        }

        // SDF depth byte 0: bit0 enable, bit1 write; the scene depth is not reversed here.
        psoDesc.DepthStencilState.DepthEnable = (p.depth[0] & 1) ? TRUE : FALSE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.SampleDesc.Count = 1;
        Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pass.pso)), "CreateGraphicsPipelineState material particles");
        materialPasses.push_back(std::move(pass));
    }
}

void Viewer::CreateParticleLighting(const GameComputeDesc& desc) {
    EnsureGameCommon();
    particleLightRoot = CreateComputeRoot(desc, particleLightBinds, ParticleLightBuffer);
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = particleLightRoot.Get();
    psoDesc.CS = { desc.cs.data(), desc.cs.size() };
    Check(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(particleLightPso.ReleaseAndGetAddressOf())),
          "CreateComputePipelineState PreCalculateLighting");
}

void Viewer::SetMaterialParticles(std::span<const ViewerMaterialVertex> vertices, std::vector<ViewerMaterialBatch> batches,
                                  std::span<const ViewerParticleLight> lights) {
    materialBatches.clear();
    bool lit = particleLightPso && !unlit && !wireframe && gbufferView == GBufferView::None && !lights.empty();
    particleLightCount = lit ? lights.size() : 0;
    if (lit) {
        std::size_t padded = (lights.size() + PARTICLE_LIGHT_GROUP - 1) / PARTICLE_LIGHT_GROUP * PARTICLE_LIGHT_GROUP;
        if (padded > particleLightCapacity) {
            particleLightCapacity = std::max<std::size_t>(padded * 2, 1024);
            particleLightInput.Reset();
            particleLightInputMapped = nullptr;
            particleLightInput = CreateUploadBuffer(std::vector<uint8_t>(particleLightCapacity * sizeof(ViewerParticleLight), 0));
            Check(particleLightInput->Map(0, nullptr, reinterpret_cast<void**>(&particleLightInputMapped)), "Map particle lights");
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = particleLightCapacity * PARTICLE_LIGHT_RESULT_SIZE;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                  IID_PPV_ARGS(particleLightOutput.ReleaseAndGetAddressOf())),
                  "CreateCommittedResource particle lighting");
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = DXGI_FORMAT_UNKNOWN;
            uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.NumElements = static_cast<UINT>(particleLightCapacity);
            uav.Buffer.StructureByteStride = PARTICLE_LIGHT_RESULT_SIZE;
            device->CreateUnorderedAccessView(particleLightOutput.Get(), nullptr, &uav, SrvCpuHandle(UAV_PARTICLE_LIGHT));
        }
        std::memcpy(particleLightInputMapped, lights.data(), lights.size_bytes());
        std::memset(particleLightInputMapped + lights.size_bytes(), 0, (padded - lights.size()) * sizeof(ViewerParticleLight));
    }
    std::size_t bytes = vertices.size_bytes();
    if (bytes > materialVertexCapacity) {
        materialVertexCapacity = std::max<std::size_t>(bytes * 2, 64 * 1024);
        materialVertexBuffer.Reset();
        materialVertexMapped = nullptr;
        std::vector<uint8_t> zeros(materialVertexCapacity, 0);
        materialVertexBuffer = CreateUploadBuffer(zeros);
        Check(materialVertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&materialVertexMapped)), "Map material vertices");
    }
    std::size_t constants = batches.size() * 2 * CONSTANT_STRIDE;
    if (constants > materialConstantCapacity) {
        materialConstantCapacity = std::max<std::size_t>(constants * 2, 64 * CONSTANT_STRIDE);
        materialConstantBuffer.Reset();
        materialConstantMapped = nullptr;
        std::vector<uint8_t> zeros(materialConstantCapacity, 0);
        materialConstantBuffer = CreateUploadBuffer(zeros);
        Check(materialConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&materialConstantMapped)), "Map material constants");
    }
    if (bytes) std::memcpy(materialVertexMapped, vertices.data(), bytes);
    for (std::size_t i = 0; i < batches.size(); i++) {
        uint8_t* pmc = materialConstantMapped + i * 2 * CONSTANT_STRIDE;
        WritePrimitiveMaterialConstant(pmc, batches[i].emitterPosition, lit ? batches[i].primitiveFlags : batches[i].primitiveFlags & ~1u);
        std::size_t user = std::min<std::size_t>(batches[i].userMaterial.size(), CONSTANT_STRIDE);
        std::memset(pmc + CONSTANT_STRIDE, 0, CONSTANT_STRIDE);
        if (user) std::memcpy(pmc + CONSTANT_STRIDE, batches[i].userMaterial.data(), user);
    }
    for (ViewerMaterialBatch& batch : batches) {
        if (batch.program < materialPasses.size() && batch.vertexCount > 0) materialBatches.push_back(std::move(batch));
    }
}

// One thread per lit point, 64 per group; the padding points (segment 0) write nothing.
void Viewer::DispatchParticleLighting() {
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES computeRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (shadowMap) {
        D3D12_RESOURCE_BARRIER toCompute = Transition(shadowMap.Get(), read, read | computeRead);
        commandList->ResourceBarrier(1, &toCompute);
    }
    commandList->SetComputeRootSignature(particleLightRoot.Get());
    commandList->SetPipelineState(particleLightPso.Get());
    for (UINT i = 0; i < particleLightBinds.size(); i++) {
        const IndirectBind& bind = particleLightBinds[i];
        const std::string& n = bind.name;
        switch (bind.kind) {
            case IndirectBind::Cbv: {
                D3D12_GPU_VIRTUAL_ADDRESS address = n == "SceneInfo"                                     ? sceneInfoBuffer->GetGPUVirtualAddress()
                                                  : n == "LightInfo"                                     ? lightInfoBuffer->GetGPUVirtualAddress()
                                                  : n == "ShadowSamplingRotation" && shadowRotationBuffer ? shadowRotationBuffer->GetGPUVirtualAddress()
                                                  : n == "FogParam"                                      ? FogParamAddress()
                                                                                                         : zeroBuffer->GetGPUVirtualAddress();
                commandList->SetComputeRootConstantBufferView(i, address);
                break;
            }
            case IndirectBind::BufferSrv: {
                ID3D12Resource* buffer = n == "LightParameterSRV" && lightParamsBuffer             ? lightParamsBuffer.Get()
                                       : n == "IBLCubemapArrayList2SRV" && localCubemapRecords     ? localCubemapRecords.Get()
                                       : n == "BSPTree" && probeBspTree                           ? probeBspTree.Get()
                                       : n == "TetraCoordinate" && probeBuffers[0]                 ? probeBuffers[0].Get()
                                       : n == "IndirectProbe" && probeBuffers[1]                   ? probeBuffers[1].Get()
                                       : n == "LightedParticles"                                   ? particleLightInput.Get()
                                                                                                   : zeroBuffer.Get();
                commandList->SetComputeRootShaderResourceView(i, buffer->GetGPUVirtualAddress());
                break;
            }
            case IndirectBind::TextureSrv: {
                uint32_t slot = n == "BlueNoise16"            ? SRV_BLUE_NOISE
                              : n == "ShadowMapSRV"           ? SRV_LIGHT_SHADOW
                              : n == "IESLightTableSRV"       ? SRV_LIGHT_IES
                              : n == "IBLCubemap2DArraySRV"   ? SRV_INDIRECT_CUBEMAP_ARRAY
                              : n == "CubemapSRV"             ? SRV_INDIRECT_CUBEMAP
                              : fogBlackVolume && (n == "AerialPerspectiveTexture" || n == "TransmittanceFromCameraTexture")
                                  ? SRV_FOG_BASE + FOG_VIEW_BLACK_VOLUME
                                  : SRV_LIGHT_BLACK;
                commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(slot));
                break;
            }
            case IndirectBind::Uav:
                commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(UAV_PARTICLE_LIGHT));
                break;
            case IndirectBind::NullBuffer:
                commandList->SetComputeRootDescriptorTable(i, SrvGpuHandle(bind.nullView));
                break;
        }
    }
    commandList->Dispatch(static_cast<UINT>((particleLightCount + PARTICLE_LIGHT_GROUP - 1) / PARTICLE_LIGHT_GROUP), 1, 1);
    D3D12_RESOURCE_BARRIER after[2] = {
        Transition(particleLightOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, read | computeRead),
        Transition(shadowMap ? shadowMap.Get() : nullptr, read | computeRead, read),
    };
    commandList->ResourceBarrier(shadowMap ? 2 : 1, after);
}

// Before the particles, into the effect target; depth tested against the scene, read only.
void Viewer::RenderMaterialParticles(D3D12_CPU_DESCRIPTOR_HANDLE rtv, bool lighting) {
    if (materialBatches.empty() || !materialVertexBuffer) return;
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES depthRead = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER toRead = Transition(depthBuffer.Get(), read, depthRead);
    commandList->ResourceBarrier(1, &toRead);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    dsv.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW view{ materialVertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(materialVertexCapacity),
                                   sizeof(ViewerMaterialVertex) };
    commandList->IASetVertexBuffers(0, 1, &view);

    D3D12_GPU_VIRTUAL_ADDRESS constants = materialConstantBuffer->GetGPUVirtualAddress();
    for (std::size_t b = 0; b < materialBatches.size(); b++) {
        const ViewerMaterialBatch& batch = materialBatches[b];
        const MaterialPass& pass = materialPasses[batch.program];
        commandList->SetGraphicsRootSignature(pass.root.Get());
        commandList->SetPipelineState(pass.pso.Get());
        D3D12_GPU_VIRTUAL_ADDRESS pmc = constants + b * 2 * CONSTANT_STRIDE;
        for (UINT i = 0; i < pass.binds.size(); i++) {
            const PassBind& bind = pass.binds[i];
            if (bind.kind == PassBind::Cbv) {
                D3D12_GPU_VIRTUAL_ADDRESS address = bind.name == "SceneInfo"                   ? sceneInfoBuffer->GetGPUVirtualAddress()
                                                  : bind.name == "EnvironmentInfo"             ? environmentBuffer->GetGPUVirtualAddress()
                                                  : bind.name == "CheckerBoardInfo"            ? checkerBoardBuffer->GetGPUVirtualAddress()
                                                  : bind.name == "PrimitiveMaterialConstant"   ? pmc
                                                  : bind.name == "UserMaterial"                ? pmc + CONSTANT_STRIDE
                                                  : bind.name == "FogParam"                    ? FogParamAddress()
                                                                                               : zeroBuffer->GetGPUVirtualAddress();
                commandList->SetGraphicsRootConstantBufferView(i, address);
            } else if (bind.kind == PassBind::RootSrv) {
                ID3D12Resource* buffer = bind.name == "PreCalcParticlesResult" && lighting ? particleLightOutput.Get() : zeroBuffer.Get();
                commandList->SetGraphicsRootShaderResourceView(i, buffer->GetGPUVirtualAddress());
            } else {
                bool volume = bind.name == "AerialPerspectiveTexture" || bind.name == "TransmittanceFromCameraTexture";
                uint32_t slot = bind.name == "ReadonlyDepth"   ? SRV_RESOLVE_DEPTH
                              : volume && fogBlackVolume        ? SRV_FOG_BASE + FOG_VIEW_BLACK_VOLUME
                                                                : SRV_BINDLESS_BASE;
                for (const auto& [name, texture] : batch.textures) {
                    if (name == bind.name) slot = SRV_BINDLESS_BASE + texture;
                }
                commandList->SetGraphicsRootDescriptorTable(i, SrvGpuHandle(slot));
            }
        }
        commandList->DrawInstanced(batch.vertexCount, 1, batch.firstVertex, 0);
    }

    D3D12_RESOURCE_BARRIER back = Transition(depthBuffer.Get(), depthRead, read);
    commandList->ResourceBarrier(1, &back);
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
}
