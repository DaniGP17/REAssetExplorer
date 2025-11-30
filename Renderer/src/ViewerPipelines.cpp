#include "Renderer/Viewer.h"

#include <cstring>
#include <d3dcompiler.h>
#include <string>

#include "D3D12Utils.h"
#include "DxilBindings.h"
#include "GameBinds.h"
#include "ParallelFor.h"

using Microsoft::WRL::ComPtr;

Viewer::GamePipeline Viewer::BuildGamePipeline(std::span<const uint8_t> vs, std::span<const uint8_t> ps,
                                               const std::vector<GameInputElement>& inputLayoutDesc,
                                               const GamePipelineConfig& config) {
    EnsureGameCommon();

    GamePipeline pipe;
    std::vector<D3D12_ROOT_PARAMETER> params;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    ranges.reserve(32);

    auto lightingSlot = [&config](bool sampler, uint32_t reg) -> const GameLightingSlot* {
        if (config.lightingSlots == nullptr) return nullptr;
        for (const GameLightingSlot& slot : *config.lightingSlots) {
            if (!slot.constantBuffer && slot.sampler == sampler && slot.reg == reg) return &slot;
        }
        return nullptr;
    };
    auto lightingConstantBuffer = [&config](uint32_t reg) -> const GameLightingSlot* {
        if (config.lightingSlots == nullptr) return nullptr;
        for (const GameLightingSlot& slot : *config.lightingSlots) {
            if (slot.constantBuffer && slot.reg == reg) return &slot;
        }
        return nullptr;
    };
    // DeferredLight* register-to-heap map; the register fallback is RE7's layout.
    auto lightingTexture = [&config, &lightingSlot](uint32_t reg) -> uint32_t {
        if (const GameLightingSlot* slot = lightingSlot(false, reg)) {
            switch (slot->resource) {
                case GameLightingResource::BlueNoise: return SRV_BLUE_NOISE;
                case GameLightingResource::Depth: return SRV_LIGHT_DEPTH;
                case GameLightingResource::CullingVolume: return SRV_LIGHT_VOLUME;
                case GameLightingResource::AmbientBrdf: return SRV_LIGHT_BRDF;
                case GameLightingResource::BaseColor: return SRV_GBUFFER0 + 1;
                case GameLightingResource::Normal: return SRV_GBUFFER0 + 2;
                case GameLightingResource::Occlusion: return SRV_GBUFFER0 + 3;
                case GameLightingResource::StaticShadow: return SRV_LIGHT_STATIC_SHADOW;
                case GameLightingResource::Shadow: return SRV_LIGHT_SHADOW;
                case GameLightingResource::Ies: return SRV_LIGHT_IES;
                case GameLightingResource::Gid: return SRV_LIGHT_GID;
                case GameLightingResource::Gis: return SRV_LIGHT_GIS;
                default: return SRV_LIGHT_BLACK;
            }
        }
        if (reg == config.gidRegister) return SRV_LIGHT_GID;
        switch (reg) {
            case 0: return SRV_LIGHT_DEPTH;
            case 3: return SRV_LIGHT_VOLUME;
            case 5: return SRV_LIGHT_BRDF;
            case 6: return SRV_GBUFFER0 + 1;
            case 7: return SRV_GBUFFER0 + 2;
            case 8: return SRV_GBUFFER0 + 3;
            case 9: return SRV_LIGHT_SHADOW;
            case 10: return SRV_LIGHT_SHADOW;
            case 11: return SRV_LIGHT_IES;
            case 12: return SRV_GBUFFER0;
            default: return SRV_LIGHT_BLACK;
        }
    };

    // A resolved SDF slot name wins over the register heuristics below.
    auto findOverride = [&config](bool pixelStage, bool constantBuffer, uint32_t reg)
        -> const GameBindOverride* {
        if (config.bindOverrides == nullptr) return nullptr;
        for (const GameBindOverride& o : *config.bindOverrides) {
            if (o.pixelStage == pixelStage && o.constantBuffer == constantBuffer && o.reg == reg) {
                return &o;
            }
        }
        return nullptr;
    };
    auto overrideKind = [](const GameBindOverride& o) -> uint8_t {
        switch (o.kind) {
            case GameBindSlotKind::Scene: return BIND_SCENE;
            case GameBindSlotKind::GBufferType: return BIND_GBUFFER_TYPE;
            case GameBindSlotKind::CheckerBoard: return BIND_CHECKERBOARD;
            case GameBindSlotKind::ZeroCb: return BIND_ZERO_CBV;
            case GameBindSlotKind::Instance: return BIND_INSTANCE;
            case GameBindSlotKind::BindlessData: return BIND_BINDLESS_DATA;
            case GameBindSlotKind::Redirect: return BIND_REDIRECT;
            case GameBindSlotKind::ZeroSrv: return BIND_ZERO_SRV;
            case GameBindSlotKind::Skinning: return BIND_SKINNING;
            case GameBindSlotKind::Tonemap: return BIND_TONEMAP;
            case GameBindSlotKind::WhitePoint: return BIND_WHITE_POINT;
            case GameBindSlotKind::Environment: return BIND_ENVIRONMENT;
            case GameBindSlotKind::ShadowCast: return BIND_SHADOW_CAST;
        }
        return BIND_ZERO_CBV;
    };

    auto addStage = [&](std::span<const uint8_t> blob, D3D12_SHADER_VISIBILITY visibility, bool isVertexStage) {
        for (const DxilBinding& b : ParseDxilBindings(blob)) {
            if (b.type == 1) {
                D3D12_STATIC_SAMPLER_DESC s{};
                if (config.lighting) {
                    const GameLightingSlot* slot = lightingSlot(true, b.reg);
                    bool compare = slot ? slot->resource == GameLightingResource::CompareSampler : b.reg == 12;
                    s.Filter = compare ? D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
                                       : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                    // LinearCompare in the RE8 capture.
                    s.ComparisonFunc = compare ? D3D12_COMPARISON_FUNC_LESS : D3D12_COMPARISON_FUNC_NEVER;
                    s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                } else {
                    s.Filter = D3D12_FILTER_ANISOTROPIC;
                    s.MaxAnisotropy = 16;
                    s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                    if (config.samplers) {
                        for (const GameSamplerOverride& o : *config.samplers) {
                            if (o.pixelStage == !isVertexStage && o.reg == b.reg && b.space == 0) {
                                s.Filter = o.filter;
                                s.AddressU = o.address;
                            }
                        }
                    }
                    s.AddressV = s.AddressU;
                    s.AddressW = s.AddressU;
                }
                s.MaxLOD = D3D12_FLOAT32_MAX;
                s.ShaderRegister = b.reg;
                s.RegisterSpace = b.space;
                s.ShaderVisibility = visibility;
                samplers.push_back(s);
                continue;
            }

            D3D12_ROOT_PARAMETER param{};
            param.ShaderVisibility = visibility;
            GameBindSlot bind{ BIND_ZERO_CBV, 0 };

            if (b.type == 2) {
                if (b.space == 32) {
                    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                    param.Constants = { b.reg, b.space, 1 };
                    bind.kind = BIND_ROOT_CONST;
                    pipe.rootConstParams.push_back(static_cast<uint32_t>(params.size()));
                } else {
                    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    param.Descriptor = { b.reg, b.space };
                    const GameBindOverride* o = b.space == 0 ? findOverride(!isVertexStage, true, b.reg) : nullptr;
                    if (o != nullptr) {
                        bind.kind = overrideKind(*o);
                    } else if (const GameLightingSlot* slot = config.lighting ? lightingConstantBuffer(b.reg) : nullptr;
                               slot && slot->resource == GameLightingResource::ShadowRotation) {
                        bind.kind = BIND_SHADOW_ROTATION;
                    } else if (config.lighting) {
                        bind.kind = b.reg == 0 ? BIND_SCENE
                                  : b.reg == 2 ? BIND_LIGHT_INFO
                                  : b.reg == 3 ? BIND_CHECKERBOARD
                                  : BIND_ZERO_CBV;
                    } else {
                        bind.kind = b.reg == 0 ? BIND_SCENE
                                  : b.reg == 1 ? BIND_GBUFFER_TYPE
                                  : b.reg == 2 ? BIND_CHECKERBOARD
                                  : BIND_ZERO_CBV;
                    }
                }
            } else if (b.space == 4 || b.space == 5 || b.space == 7 || b.type == 3) {
                // space4 = bindless Texture2D table, space5 = Texture2DArray, space7 = TextureCube (RE8).
                bool bindless = b.space == 4 || b.space == 5 || b.space == 7;
                ranges.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                                   bindless ? UINT_MAX : 1, b.reg, b.space, 0 });
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.DescriptorTable = { 1, &ranges.back() };
                bind.kind = BIND_TABLE;
                bind.data = b.space == 4 ? SRV_BINDLESS_BASE
                          : b.space == 5 ? SRV_BINDLESS_ARRAY_BASE
                          : b.space == 7 ? SRV_BINDLESS_CUBE_BASE
                          : config.lighting ? lightingTexture(b.reg)
                          : SRV_BLUE_NOISE;
            } else if (b.type == 4) {
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
                param.Descriptor = { b.reg, b.space };
                const GameBindOverride* o = b.space == 0 ? findOverride(!isVertexStage, false, b.reg) : nullptr;
                if (o != nullptr) {
                    bind.kind = overrideKind(*o);
                } else {
                    const GameLightingSlot* slot = config.lighting ? lightingSlot(false, b.reg) : nullptr;
                    bind.kind = slot ? (slot->resource == GameLightingResource::CullingList ? BIND_LIGHT_LIST : BIND_ZERO_SRV)
                              : config.lighting ? (b.reg == 4 ? BIND_LIGHT_LIST : BIND_ZERO_SRV)
                              : BIND_REDIRECT;
                }
            } else if (b.type == 5) {
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
                param.Descriptor = { b.reg, b.space };
                const GameBindOverride* o = b.space == 0 ? findOverride(!isVertexStage, false, b.reg) : nullptr;
                if (o != nullptr) {
                    bind.kind = overrideKind(*o);
                } else {
                    const GameLightingSlot* slot = config.lighting ? lightingSlot(false, b.reg) : nullptr;
                    bind.kind = slot ? (slot->resource == GameLightingResource::LightParams ? BIND_LIGHT_PARAMS : BIND_ZERO_SRV)
                              : config.lighting ? (b.reg == 1 ? BIND_LIGHT_PARAMS : BIND_ZERO_SRV)
                              : isVertexStage ? BIND_INSTANCE
                              : BIND_BINDLESS_DATA;
                }
            } else if (b.type >= 6) {
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
                param.Descriptor = { b.reg, b.space };
                bind.kind = BIND_SCRATCH_UAV;
            } else {
                throw std::runtime_error("BuildGamePipeline: unsupported binding type " + std::to_string(b.type));
            }

            params.push_back(param);
            pipe.binds.push_back(bind);
        }
    };

    addStage(vs, D3D12_SHADER_VISIBILITY_VERTEX, true);
    if (!ps.empty()) addStage(ps, D3D12_SHADER_VISIBILITY_PIXEL, false);

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(params.size());
    rootDesc.pParameters = params.data();
    rootDesc.NumStaticSamplers = static_cast<UINT>(samplers.size());
    rootDesc.pStaticSamplers = samplers.data();
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (FAILED(hr)) {
        std::string msg = "D3D12SerializeRootSignature game";
        if (errors) msg += std::string(": ") + static_cast<const char*>(errors->GetBufferPointer());
        throw std::runtime_error(msg);
    }
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&pipe.root)), "CreateRootSignature game");

    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
    for (const GameInputElement& e : inputLayoutDesc) {
        inputLayout.push_back({ e.semanticName, e.semanticIndex, e.format, e.slot, e.offset,
                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = pipe.root.Get();
    psoDesc.VS = { vs.data(), vs.size() };
    if (!ps.empty()) psoDesc.PS = { ps.data(), ps.size() };
    if (config.useInputLayout) {
        psoDesc.InputLayout = { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };
    }

    psoDesc.RasterizerState.FillMode = config.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    if (config.sdfRaster) {
        // SDF raster word 0: bit 8 culls back faces (0x5c3 plain, 0x4c3 TwoSide programs; the RE8 capture draws
        // them cull back / none), bit 10 front counter-clockwise (every captured draw).
        // Words 1-3: DepthBias (int), DepthBiasClamp, SlopeScaledDepthBias.
        uint32_t word;
        std::memcpy(&word, config.sdfRaster, 4);
        if (word & 0x100) psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        psoDesc.RasterizerState.FrontCounterClockwise = (word >> 10) & 1;
        int32_t depthBias;
        float biasClamp, slopeBias;
        std::memcpy(&depthBias, config.sdfRaster + 4, 4);
        std::memcpy(&biasClamp, config.sdfRaster + 8, 4);
        std::memcpy(&slopeBias, config.sdfRaster + 12, 4);
        psoDesc.RasterizerState.DepthBias = depthBias;
        psoDesc.RasterizerState.DepthBiasClamp = biasClamp;
        psoDesc.RasterizerState.SlopeScaledDepthBias = slopeBias;
    }

    // SDF blend word per RT: enable(1) src(5) dst(5) op(3) srcA(5) dstA(5)
    // opA(3) writeMask(4); factor/op values match the D3D12 enums. Trailing
    // u32 of the 36B block enables independent per-RT words.
    auto alphaLegalFactor = [](uint32_t f) -> uint32_t {
        switch (f) {
            case D3D12_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_ALPHA;
            case D3D12_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_ALPHA;
            case D3D12_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_ALPHA;
            case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
            default: return f == 0 ? D3D12_BLEND_ONE : f;
        }
    };

    uint32_t blendWords[8]{};
    uint32_t independentBlend = 0;
    if (config.sdfBlend) {
        std::memcpy(blendWords, config.sdfBlend, 32);
        std::memcpy(&independentBlend, config.sdfBlend + 32, 4);
        psoDesc.BlendState.IndependentBlendEnable = independentBlend != 0;
    }

    for (UINT i = 0; i < config.renderTargetCount; i++) {
        D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[i];
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;

        if (config.sdfBlend) {
            uint32_t w = independentBlend ? blendWords[i] : blendWords[0];
            bool enable = (w & 1) != 0;
            rt.BlendEnable = enable ? TRUE : FALSE;
            if (enable) {
                rt.SrcBlend = static_cast<D3D12_BLEND>((w >> 1) & 0x1F);
                rt.DestBlend = static_cast<D3D12_BLEND>((w >> 6) & 0x1F);
                rt.BlendOp = static_cast<D3D12_BLEND_OP>(((w >> 11) & 0x7) ? (w >> 11) & 0x7 : 1);
                rt.SrcBlendAlpha = static_cast<D3D12_BLEND>(alphaLegalFactor((w >> 14) & 0x1F));
                rt.DestBlendAlpha = static_cast<D3D12_BLEND>(alphaLegalFactor((w >> 19) & 0x1F));
                rt.BlendOpAlpha = static_cast<D3D12_BLEND_OP>(((w >> 24) & 0x7) ? (w >> 24) & 0x7 : 1);
                pipe.blends = true;
            } else {
                rt.SrcBlend = D3D12_BLEND_ONE;
                rt.DestBlend = D3D12_BLEND_ZERO;
                rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            }
            rt.RenderTargetWriteMask = static_cast<UINT8>((w >> 27) & 0xF);
        } else {
            rt.BlendEnable = config.additiveBlend ? TRUE : FALSE;
            rt.SrcBlend = D3D12_BLEND_ONE;
            rt.DestBlend = config.additiveBlend ? D3D12_BLEND_ONE : D3D12_BLEND_ZERO;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        psoDesc.RTVFormats[i] = config.gbufferTargets ? GBUFFER_FORMATS[i]
                              : i == 0 && config.renderTarget0Format != DXGI_FORMAT_UNKNOWN ? config.renderTarget0Format
                                                                                             : config.renderTargetFormat;
    }

    if (config.shadowCast) {
        // The RE8 capture's ShadowCast draws: D16 reverse-Z (the cascade projections are flipped).
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        psoDesc.DSVFormat = DXGI_FORMAT_D16_UNORM;
    } else if (config.depth) {
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        // SDF depth byte 0: bit0 = depthEnable, bit1 = depthWrite, bits 3-6 = func
        // (reversed-Z). Only EQUAL (3) transfers to our non-reversed depth; the rest stays LESS.
        bool depthWrite = !config.afterPrepass && (config.sdfDepth == nullptr || (config.sdfDepth[0] & 2) != 0);
        uint32_t func = config.sdfDepth ? (config.sdfDepth[0] >> 3) & 0xF : 0;
        psoDesc.DepthStencilState.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL
                                                              : D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.DepthStencilState.DepthFunc = func == 3 ? D3D12_COMPARISON_FUNC_EQUAL
                                            : config.afterPrepass ? D3D12_COMPARISON_FUNC_LESS_EQUAL
                                                                  : D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    }

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = config.renderTargetCount;
    psoDesc.SampleDesc.Count = 1;

    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipe.pso)), "CreateGraphicsPipelineState game");
    return pipe;
}

void Viewer::BindGamePipeline(const GamePipeline& pipeline) {
    commandList->SetGraphicsRootSignature(pipeline.root.Get());
    commandList->SetPipelineState(pipeline.pso.Get());

    for (UINT i = 0; i < pipeline.binds.size(); i++) {
        const GameBindSlot& bind = pipeline.binds[i];
        switch (bind.kind) {
            case BIND_SCENE:
                commandList->SetGraphicsRootConstantBufferView(
                    i, sceneInfoOverride ? sceneInfoOverride : sceneInfoBuffer->GetGPUVirtualAddress());
                break;
            case BIND_SHADOW_CAST:
                commandList->SetGraphicsRootConstantBufferView(
                    i, shadowCastAddress ? shadowCastAddress : zeroBuffer->GetGPUVirtualAddress());
                break;
            case BIND_SHADOW_ROTATION:
                commandList->SetGraphicsRootConstantBufferView(
                    i, (shadowRotationBuffer ? shadowRotationBuffer : zeroBuffer)->GetGPUVirtualAddress());
                break;
            case BIND_GBUFFER_TYPE:
                commandList->SetGraphicsRootConstantBufferView(i, gbufferTypeBuffer->GetGPUVirtualAddress());
                break;
            case BIND_CHECKERBOARD:
                commandList->SetGraphicsRootConstantBufferView(i, checkerBoardBuffer->GetGPUVirtualAddress());
                break;
            case BIND_LIGHT_INFO:
                commandList->SetGraphicsRootConstantBufferView(i, lightInfoBuffer->GetGPUVirtualAddress());
                break;
            case BIND_ZERO_CBV:
                commandList->SetGraphicsRootConstantBufferView(i, zeroBuffer->GetGPUVirtualAddress());
                break;
            case BIND_ROOT_CONST:
                commandList->SetGraphicsRoot32BitConstant(i, 0, 0);
                break;
            case BIND_INSTANCE:
                commandList->SetGraphicsRootShaderResourceView(i, instanceBuffer->GetGPUVirtualAddress());
                break;
            case BIND_REDIRECT:
                commandList->SetGraphicsRootShaderResourceView(
                    i, (redirectBuffer ? redirectBuffer : zeroBuffer)->GetGPUVirtualAddress());
                break;
            case BIND_BINDLESS_DATA:
                commandList->SetGraphicsRootShaderResourceView(
                    i, (bindlessDataBuffer ? bindlessDataBuffer : zeroBuffer)->GetGPUVirtualAddress());
                break;
            case BIND_ZERO_SRV:
                commandList->SetGraphicsRootShaderResourceView(i, zeroBuffer->GetGPUVirtualAddress());
                break;
            case BIND_LIGHT_PARAMS:
                commandList->SetGraphicsRootShaderResourceView(
                    i, (lightParamsBuffer ? lightParamsBuffer : zeroBuffer)->GetGPUVirtualAddress());
                break;
            case BIND_LIGHT_LIST:
                commandList->SetGraphicsRootShaderResourceView(
                    i, (lightListBuffer ? lightListBuffer : zeroBuffer)->GetGPUVirtualAddress());
                break;
            case BIND_SCRATCH_UAV:
                commandList->SetGraphicsRootUnorderedAccessView(i, scratchUavBuffer->GetGPUVirtualAddress());
                break;
            case BIND_SKINNING:
                commandList->SetGraphicsRootShaderResourceView(i, skinningBuffer->GetGPUVirtualAddress());
                break;
            case BIND_TONEMAP:
                commandList->SetGraphicsRootConstantBufferView(i, tonemapBuffer->GetGPUVirtualAddress());
                break;
            case BIND_ENVIRONMENT:
                commandList->SetGraphicsRootConstantBufferView(i, environmentBuffer->GetGPUVirtualAddress());
                break;
            case BIND_WHITE_POINT:
                commandList->SetGraphicsRootShaderResourceView(
                    i, (exposureState ? exposureState : zeroBuffer)->GetGPUVirtualAddress());
                break;
            case BIND_TABLE:
                commandList->SetGraphicsRootDescriptorTable(i, SrvGpuHandle(bind.data));
                break;
        }
    }
}

void Viewer::CreateGamePrepass(const GamePrepassDesc& desc) {
    GamePipelineConfig config{ false, 0, DXGI_FORMAT_UNKNOWN, true, true };
    config.bindOverrides = &desc.bindOverrides;
    prepassPipeline = BuildGamePipeline(desc.vs, {}, desc.inputLayout, config);
}

void Viewer::CreateGameDeferred(const std::vector<GameDeferredDesc>& descs) {
    EnsureGameCommon();
    struct Pipelines {
        GamePipeline gbuffer, prepass, wireframe, selection, shadow;
        ForwardPipeline forward;
    };
    std::vector<Pipelines> built(descs.size());
    ParallelFor(descs.size(), [&](std::size_t d) {
        const GameDeferredDesc& desc = descs[d];
        if (desc.forward) {
            built[d].forward = BuildForwardPipeline(desc);
            GamePipelineConfig selectionConfig{ false, 0, DXGI_FORMAT_UNKNOWN, true, true };
            selectionConfig.bindOverrides = &desc.bindOverrides;
            built[d].selection = BuildGamePipeline(desc.vs, {}, desc.inputLayout, selectionConfig);
            if (!desc.shadowVs.empty()) {
                GamePipelineConfig shadowConfig{ false, 0, DXGI_FORMAT_UNKNOWN, false, true };
                shadowConfig.shadowCast = true;
                shadowConfig.bindOverrides = &desc.shadowBindOverrides;
                shadowConfig.samplers = &desc.shadowSamplers;
                shadowConfig.sdfRaster = desc.shadowRasterizerState.data();
                built[d].shadow = BuildGamePipeline(desc.shadowVs, desc.shadowPs, desc.shadowLayout, shadowConfig);
            }
            return;
        }
        GamePipelineConfig config{ false, 4, DXGI_FORMAT_UNKNOWN, true, true };
        config.gbufferTargets = true;
        config.sdfBlend = desc.blendState.data();
        config.sdfDepth = desc.depthStencilState.data();
        config.sdfRaster = desc.rasterizerState.data();
        config.bindOverrides = &desc.bindOverrides;
        config.samplers = &desc.samplers;
        config.afterPrepass = !desc.prepassVs.empty();
        GamePipeline gbuffer = BuildGamePipeline(desc.vs, desc.ps, desc.inputLayout, config);
        // Blended materials draw over the finished depth instead of into it.
        if (gbuffer.blends && config.afterPrepass) {
            config.afterPrepass = false;
            gbuffer = BuildGamePipeline(desc.vs, desc.ps, desc.inputLayout, config);
        }
        gbuffer.skipPrepass = gbuffer.blends || desc.alphaTest;

        GamePipeline prepass;
        if (config.afterPrepass) {
            GamePipelineConfig prepassConfig{ false, 0, DXGI_FORMAT_UNKNOWN, true, true };
            prepassConfig.bindOverrides = &desc.prepassBindOverrides;
            prepassConfig.samplers = &desc.prepassSamplers;
            prepassConfig.sdfRaster = desc.prepassRasterizerState.data();
            prepass = BuildGamePipeline(desc.prepassVs, desc.prepassPs, desc.prepassLayout, prepassConfig);
        }
        built[d].gbuffer = std::move(gbuffer);
        built[d].prepass = std::move(prepass);

        // Same shaders so skinning still applies.
        GamePipelineConfig wireConfig{ false, 4, DXGI_FORMAT_UNKNOWN, true, true };
        wireConfig.gbufferTargets = true;
        wireConfig.bindOverrides = &desc.bindOverrides;
        wireConfig.samplers = &desc.samplers;
        wireConfig.wireframe = true;
        built[d].wireframe = BuildGamePipeline(desc.vs, desc.ps, desc.inputLayout, wireConfig);

        GamePipelineConfig selectionConfig{ false, 0, DXGI_FORMAT_UNKNOWN, true, true };
        selectionConfig.bindOverrides = &desc.bindOverrides;
        selectionConfig.samplers = &desc.samplers;
        built[d].selection = BuildGamePipeline(desc.vs, {}, desc.inputLayout, selectionConfig);

        GamePipeline shadow;
        if (!desc.shadowVs.empty()) {
            GamePipelineConfig shadowConfig{ false, 0, DXGI_FORMAT_UNKNOWN, false, true };
            shadowConfig.shadowCast = true;
            shadowConfig.bindOverrides = &desc.shadowBindOverrides;
            shadowConfig.samplers = &desc.shadowSamplers;
            shadowConfig.sdfRaster = desc.shadowRasterizerState.data();
            shadow = BuildGamePipeline(desc.shadowVs, desc.shadowPs, desc.shadowLayout, shadowConfig);
        }
        built[d].shadow = std::move(shadow);
    });
    for (Pipelines& pipelines : built) {
        deferredPipelines.push_back(std::move(pipelines.gbuffer));
        prepassPipelines.push_back(std::move(pipelines.prepass));
        wireframePipelines.push_back(std::move(pipelines.wireframe));
        selectionPipelines.push_back(std::move(pipelines.selection));
        shadowPipelines.push_back(std::move(pipelines.shadow));
        forwardPipelines.push_back(std::move(pipelines.forward));
    }

    CreateGBufferTargets();
    CreateResolvePipeline();
    CreateParticlePipeline();
    cullDirty = true;
}

// GBuffer: RT0=HDR ambient/emissive (float), RT1=basecolor+metallic,
// RT2=normals+roughness, RT3=velocity+occlusion.
void Viewer::CreateGBufferTargets() {
    for (int i = 0; i < 4; i++) {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = GBUFFER_FORMATS[i];
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (i == 3) texDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = texDesc.Format;

        Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                                              IID_PPV_ARGS(&gbuffer[i])), "CreateCommittedResource gbuffer");
    }

    if (!gbufferRtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = 4;
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&gbufferRtvHeap)), "CreateDescriptorHeap gbuffer RTV");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = gbufferRtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < 4; i++) {
        device->CreateRenderTargetView(gbuffer[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvStride;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = GBUFFER_FORMATS[i];
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(gbuffer[i].Get(), &srvDesc, SrvCpuHandle(SRV_GBUFFER0 + i));
        // SRV_SKY aliases GBuffer0 until LoadSky sets the real sky.
        if (i == 0 && !skyTexture) {
            device->CreateShaderResourceView(gbuffer[i].Get(), &srvDesc, SrvCpuHandle(SRV_SKY));
        }
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depthBuffer.Get(), &depthSrv, SrvCpuHandle(SRV_RESOLVE_DEPTH));

    CreateCacaoTargets();
    CreateEffectTarget();
    CreateFogTarget();
    CreatePostTargets();
    CreateSelectionTarget();
}

void Viewer::CreateResolvePipeline() {
    D3D12_DESCRIPTOR_RANGE resolveRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, 0 };
    D3D12_DESCRIPTOR_RANGE extraRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, RESOLVE_EXTRA_SRVS, 8, 0, 0 };
    D3D12_DESCRIPTOR_RANGE exposureRange{ D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1, 0, 0 };
    D3D12_DESCRIPTOR_RANGE fogRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 16, 0, 0 };
    D3D12_ROOT_PARAMETER resolveParams[5]{};
    resolveParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    resolveParams[0].DescriptorTable = { 1, &resolveRange };
    resolveParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    resolveParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    resolveParams[1].Descriptor = { 0, 0 };
    if (!passConstants) {
        passConstants = CreateUploadBuffer(std::vector<uint8_t>(PASS_CONSTANTS_BYTES, 0));
        D3D12_RANGE none{};
        Check(passConstants->Map(0, &none, reinterpret_cast<void**>(&passConstantsMapped)), "Map pass constants");
    }
    resolveParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    resolveParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    resolveParams[2].DescriptorTable = { 1, &extraRange };
    resolveParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    resolveParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    resolveParams[3].DescriptorTable = { 1, &exposureRange };
    resolveParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    resolveParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    resolveParams[4].DescriptorTable = { 1, &fogRange };
    resolveParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    CreateExposureResources();

    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
    nullSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrv.Texture2D.MipLevels = 1;
    for (uint32_t i = SRV_HDR_AUX; i < SRV_PROBE_TETRAHEDRA; i++) {
        device->CreateShaderResourceView(nullptr, &nullSrv, SrvCpuHandle(i));
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC nullBuffer{};
    nullBuffer.Format = DXGI_FORMAT_R32_UINT;
    nullBuffer.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    nullBuffer.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullBuffer.Buffer.NumElements = 1;
    for (uint32_t i = SRV_PROBE_TETRAHEDRA; i < SRV_HDR_AUX + RESOLVE_EXTRA_SRVS; i++) {
        device->CreateShaderResourceView(nullptr, &nullBuffer, SrvCpuHandle(i));
    }

    D3D12_STATIC_SAMPLER_DESC skySampler{};
    skySampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    skySampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    skySampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    skySampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    skySampler.MaxLOD = D3D12_FLOAT32_MAX;
    skySampler.ShaderRegister = 0;
    skySampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC resolveDesc{};
    resolveDesc.NumParameters = 5;
    resolveDesc.pParameters = resolveParams;
    resolveDesc.NumStaticSamplers = 1;
    resolveDesc.pStaticSamplers = &skySampler;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&resolveDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
          "D3D12SerializeRootSignature resolve");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&resolveRootSignature)), "CreateRootSignature resolve");

    static const char* RESOLVE_SOURCE = R"(
Texture2D gb0 : register(t0);
Texture2D gb1 : register(t1);
Texture2D gb2 : register(t2);
Texture2D gb3 : register(t3);
Texture2D hdr : register(t4);
Texture2D sky : register(t5);
Texture2D depthTex : register(t6);
Texture2D effect : register(t7);
Texture2D hdrAux : register(t8);
Texture2D iblSky : register(t9);
Texture2D iblAdd : register(t10);
Texture2D fogTex : register(t16);
RWByteAddressBuffer exposureHistogram : register(u1);
RWByteAddressBuffer exposureState : register(u2);
SamplerState skySampler : register(s0);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

cbuffer Constants : register(b0) {
    uint mode;
    float3 eye;
    row_major float4x4 viewProjInv;
    float skyIntensity;
    float3 background;
    float4 sphere;
    float4 light;
    float3 backgroundBottom;
    float skyRotation;
    float skyLod;
    uint sceneIbl;
    float exposure;
    float toneContrast;
    float toneLinearBegin;
    float toneLinearLength;
    float toneToe;
    float iblExposure;
    float iblRotation;
    float iblAddBlend;
    float iblVirtualOffset;
    float4 iblTrim;
    uint autoExposure;
    float histogramScale;
    float histogramBins;
    uint fogApplied;
};

// The Fog pass blends One + dest * SrcAlpha: in-scattered light, then the scene's transmittance.
float3 ApplyFog(int2 p, float3 color) {
    if (fogApplied == 0) return color;
    float4 fog = fogTex.Load(int3(p, 0));
    return fog.rgb + color * fog.a;
}

float WhitePoint() {
    if (!autoExposure) return 1;
    float white = asfloat(exposureState.Load(0));
    return white > 0 ? white : histogramScale;
}

float3 ViewDirection(int2 p) {
    uint w, h;
    depthTex.GetDimensions(w, h);
    float2 ndc = float2(p.x / (float)w * 2 - 1, 1 - p.y / (float)h * 2);
    float4 world = mul(float4(ndc, 1, 1), viewProjInv);
    return normalize(world.xyz / world.w - eye);
}

float3 SampleSky(int2 p) {
    float3 dir = ViewDirection(p);
    float2 uv = float2(atan2(dir.x, dir.z) / 6.2831853 + 0.5 + skyRotation, acos(clamp(dir.y, -1, 1)) / 3.14159265);
    return sky.SampleLevel(skySampler, uv, skyLod).rgb * skyIntensity;
}

// CubemapFarPlane2DAdd2D: the view ray hits a radius-100 sphere centered iblVirtualOffset up.
float3 SampleSceneIbl(int2 p) {
    float3 dir = ViewDirection(p);
    if (iblVirtualOffset != 0) {
        float b = -2 * iblVirtualOffset * dir.y;
        float c = iblVirtualOffset * iblVirtualOffset - 10000;
        float disc = b * b - 4 * c;
        if (disc >= 0) {
            float t = max((sqrt(disc) - b) * 0.5, 0);
            dir = normalize(t * dir - float3(0, iblVirtualOffset, 0));
        }
    }
    float2 uv = float2(frac(atan2(dir.z, dir.x) / 6.2831853 + 0.5 - iblRotation), acos(clamp(dir.y, -1, 1)) / 3.14159265);
    uv = saturate(uv * iblTrim.xy + iblTrim.zw);
    return iblSky.SampleLevel(skySampler, uv, 0).rgb * iblExposure + iblAdd.SampleLevel(skySampler, uv, 0).rgb * iblAddBlend;
}

float3 ToneMap(float3 x) {
    float m = toneLinearBegin;
    float a = toneContrast;
    float l0 = (1 - m) * toneLinearLength / a;
    float s0 = m + l0;
    float s1 = m + a * l0;
    float cp = -a / (1 - s1);
    float3 toeWeight = 1 - smoothstep(0, m, x);
    float3 shoulderWeight = step(s0, x);
    float3 linearWeight = 1 - toeWeight - shoulderWeight;
    float3 toe = m * pow(max(x / m, 0), toneToe);
    float3 shoulder = 1 - (1 - s1) * exp(cp * (x - s0));
    float3 lin = m + a * (x - m);
    return toe * toeWeight + lin * linearWeight + shoulder * shoulderWeight;
}

// Dithered: an 8-bit gradient bands across a large viewport.
float3 Background(int2 p) {
    if (backgroundBottom.x < 0) return background;
    uint w, h;
    depthTex.GetDimensions(w, h);
    float noise = frac(sin(dot(float2(p), float2(12.9898, 78.233))) * 43758.5453) - 0.5;
    return lerp(background, backgroundBottom, (p.y + 0.5) / h) + noise / 255;
}

float3 SceneHdr(int2 p) {
    float depth = depthTex.Load(int3(p, 0)).r;
    float3 color;
    if (depth >= 1.0) color = sceneIbl ? SampleSceneIbl(p) : SampleSky(p);
    else color = hdr.Load(int3(p, 0)).rgb + hdrAux.Load(int3(p, 0)).rgb;
    return ApplyFog(p, color);
}

float3 ComposedHdr(int2 p) {
    float3 color = SceneHdr(p);
    float4 e = effect.Load(int3(p, 0));
    color = color * e.a + e.rgb;
    if (autoExposure && ((p.x | p.y) & 3) == 0) {
        float luminance = dot(color, float3(0.25, 0.5, 0.25));
        int bin = clamp((int)(log2(max(luminance * histogramScale, 1e-8)) * histogramBins), 0, 1023);
        uint previous;
        exposureHistogram.InterlockedAdd(bin * 4, 1, previous);
    }
    return color;
}

float3 Shade(int2 p) {
    return pow(saturate(ToneMap(ComposedHdr(p) * exposure * WhitePoint())), 1.0 / 2.2);
}

float3 Unlit(int2 p) {
    float depth = depthTex.Load(int3(p, 0)).r;
    float3 base;
    if (depth >= 1.0 && sceneIbl) {
        base = saturate(ToneMap(SampleSceneIbl(p) * exposure * WhitePoint()));
    } else if (depth >= 1.0) {
        float3 s = SampleSky(p);
        base = s / (1 + s);
    } else {
        base = gb1.Load(int3(p, 0)).rgb;
    }
    float4 e = effect.Load(int3(p, 0));
    return pow(saturate(base * e.a + e.rgb / (1 + e.rgb)), 1.0 / 2.2);
}

float3 Studio(int2 p) {
    float depth = depthTex.Load(int3(p, 0)).r;
    float3 color = Unlit(p);
    if (depth < 1.0) {
        uint w, h;
        depthTex.GetDimensions(w, h);
        float2 ndc = float2((p.x + 0.5) / w * 2 - 1, 1 - (p.y + 0.5) / h * 2);
        float4 world = mul(float4(ndc, depth, 1), viewProjInv);
        float3 pos = world.xyz / world.w;
        float3 n = normalize(pos - sphere.xyz);
        float3 v = normalize(eye - pos);
        float3 base = gb1.Load(int3(p, 0)).rgb;
        float diffuse = saturate(dot(n, light.xyz));
        float ambient = 0.16 + 0.14 * (n.y * 0.5 + 0.5);
        float specular = pow(saturate(dot(n, normalize(light.xyz + v))), 40) * 0.22;
        color = pow(saturate(base * (ambient + 0.95 * diffuse) + specular), 1.0 / 2.2);
    }
    return color;
}

float3 DecodeNormal(float2 e) {
    float2 f = e * 2 - 1;
    float3 n = float3(f, 1 - abs(f.x) - abs(f.y));
    if (n.z < 0) n.xy = (1 - abs(n.yx)) * float2(n.x >= 0 ? 1 : -1, n.y >= 0 ? 1 : -1);
    return normalize(n);
}

float3 GBufferView(int2 p) {
    if (depthTex.Load(int3(p, 0)).r >= 1.0) return float3(0, 0, 0);
    float4 g1 = gb1.Load(int3(p, 0));
    float4 g2 = gb2.Load(int3(p, 0));
    float4 g3 = gb3.Load(int3(p, 0));
    if (mode == 5) return pow(DecodeNormal(g2.xy) * 0.5 + 0.5, 1.0 / 2.2);
    if (mode == 6) return g2.zzz;
    if (mode == 7) return g1.www;
    if (mode == 9) return pow(saturate(g1.rgb), 1.0 / 2.2);
    if (mode == 10) {
        float3 e = gb0.Load(int3(p, 0)).rgb;
        return pow(saturate(e / (1 + e)), 1.0 / 2.2);
    }
    if (mode == 11) {
        uint w, h;
        depthTex.GetDimensions(w, h);
        float2 ndc = float2((p.x + 0.5) / w * 2 - 1, 1 - (p.y + 0.5) / h * 2);
        float4 world = mul(float4(ndc, depthTex.Load(int3(p, 0)).r, 1), viewProjInv);
        float distance = length(world.xyz / world.w - eye);
        return (1 - saturate(log2(1 + distance) / log2(1 + 2000.0))).xxx;
    }
    if (mode == 12) return float3(saturate(g3.xy * 20 + 0.5), 0.5);
    return g3.zzz;
}

float3 Wireframe(int2 p) {
    return depthTex.Load(int3(p, 0)).r >= 1.0 ? float3(0.10, 0.10, 0.11) : float3(0.78, 0.78, 0.78);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target {
    int2 p = int2(pos.xy);
#ifdef HDR_TARGET
    return float4(SceneHdr(p), exposure * WhitePoint());
#endif
#ifdef SCENE_HDR_TARGET
    return float4(ComposedHdr(p), 1);
#endif
    if (background.x >= 0 && depthTex.Load(int3(p, 0)).r >= 1.0) {
        float4 e = effect.Load(int3(p, 0));
        float3 under = pow(saturate(Background(p)), 2.2);
        return float4(pow(saturate(under * e.a + e.rgb / (1 + e.rgb)), 1.0 / 2.2), 1);
    }
    if (mode >= 5) return float4(GBufferView(p), 1);
    if (mode == 3) return float4(Wireframe(p), 1);
    if (mode == 4) return float4(Studio(p), 1);
    if (mode == 2) return float4(Unlit(p), 1);
    return float4(Shade(p), 1);
}
)";

    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ComPtr<ID3DBlob> compileErrors;
    Check(D3DCompile(RESOLVE_SOURCE, std::strlen(RESOLVE_SOURCE), nullptr, nullptr, nullptr,
                     "VSMain", "vs_5_0", 0, 0, &vs, &compileErrors), "compile resolve VS");
    if (FAILED(D3DCompile(RESOLVE_SOURCE, std::strlen(RESOLVE_SOURCE), nullptr, nullptr, nullptr,
                          "PSMain", "ps_5_0", 0, 0, &ps, &compileErrors))) {
        throw std::runtime_error(std::string("compile resolve PS: ") +
                                 (compileErrors ? static_cast<const char*>(compileErrors->GetBufferPointer()) : ""));
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = resolveRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[0];
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_ZERO;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&resolvePso)), "CreateGraphicsPipelineState resolve");

    const D3D_SHADER_MACRO hdrTarget[] = { { "HDR_TARGET", "1" }, { nullptr, nullptr } };
    ComPtr<ID3DBlob> hdrPs;
    if (FAILED(D3DCompile(RESOLVE_SOURCE, std::strlen(RESOLVE_SOURCE), nullptr, hdrTarget, nullptr,
                          "PSMain", "ps_5_0", 0, 0, &hdrPs, &compileErrors))) {
        throw std::runtime_error(std::string("compile resolve HDR PS: ") +
                                 (compileErrors ? static_cast<const char*>(compileErrors->GetBufferPointer()) : ""));
    }
    psoDesc.PS = { hdrPs->GetBufferPointer(), hdrPs->GetBufferSize() };
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&resolveHdrPso)), "CreateGraphicsPipelineState resolve HDR");

    const D3D_SHADER_MACRO sceneHdrTarget[] = { { "SCENE_HDR_TARGET", "1" }, { nullptr, nullptr } };
    ComPtr<ID3DBlob> sceneHdrPs;
    if (FAILED(D3DCompile(RESOLVE_SOURCE, std::strlen(RESOLVE_SOURCE), nullptr, sceneHdrTarget, nullptr,
                          "PSMain", "ps_5_0", 0, 0, &sceneHdrPs, &compileErrors))) {
        throw std::runtime_error(std::string("compile resolve scene HDR PS: ") +
                                 (compileErrors ? static_cast<const char*>(compileErrors->GetBufferPointer()) : ""));
    }
    psoDesc.PS = { sceneHdrPs->GetBufferPointer(), sceneHdrPs->GetBufferSize() };
    psoDesc.RTVFormats[0] = HDR_FORMAT;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&resolveSceneHdrPso)),
          "CreateGraphicsPipelineState resolve scene HDR");

    // IndirectIllumination's outputs: GIDSRV = irradiance / pi * AmbientBRDF.z * AO, GISSRV = the specular
    // radiance * AO; with no local cubemap the engine falls back to the probe value for both.
    static const char* AMBIENT_SOURCE = R"(
Texture2D gb1 : register(t1);
Texture2D gb2 : register(t2);
Texture2D gb3 : register(t3);
Texture2D depthTex : register(t6);
Texture2D iblFiltered : register(t11);
Texture2D ambientBrdf : register(t12);
Buffer<uint> probeTetrahedra : register(t13);
Buffer<uint> probeValues : register(t14);
Buffer<uint> probeGrid : register(t15);
SamplerState linearSampler : register(s0);

cbuffer Constants : register(b0) {
    row_major float4x4 viewProjInv;
    float3 eye;
    float filteredLod;
    float4 sh[9];
    float rotation;
    uint lightProbes;
};

float4 TetraWeights(uint t, float3 p) {
    uint b = t * 20 + 8;
    float3 d = p - asfloat(uint3(probeTetrahedra[b + 3], probeTetrahedra[b + 7], probeTetrahedra[b + 11]));
    float3 w;
    [unroll] for (uint i = 0; i < 3; i++) {
        w[i] = dot(asfloat(uint3(probeTetrahedra[b + i * 4], probeTetrahedra[b + i * 4 + 1], probeTetrahedra[b + i * 4 + 2])), d);
    }
    return float4(w, 1 - w.x - w.y - w.z);
}

float3 UnpackProbe(uint v) {
    return float3(abs(f16tof32(v << 4)), f16tof32((v >> 7) & 0x7FFF), abs(f16tof32(v >> 17)));
}

// IndirectIllumination: the icosahedron face around n picks three of a probe's 12 values.
void IcosahedronFace(float3 n, out uint3 index, out float3 weight) {
    const float K = 0.850651;
    const float G = 0.618034;
    float3 s = float3(n.x < 0 ? -1 : 1, n.y < 0 ? -1 : 1, n.z < 0 ? -1 : 1);
    float3 up = max(s, 0);
    float3 an = abs(n);
    bool c0 = dot(an, float3(1, 0.381966, -0.618034)) > 0;
    bool c1 = dot(an, float3(-0.618034, 1, 0.381966)) > 0;
    bool c2 = dot(an, float3(0.381966, -0.618034, 1)) > 0;
    float3 a = c0 ? float3(s.x * K, s.y * G, 0) : float3(-s.x * G, 0, s.z * K);
    float3 b = c1 ? float3(0, s.y * K, s.z * G) : float3(s.x * K, -s.y * G, 0);
    float3 c = c2 ? float3(s.x * G, 0, s.z * K) : float3(0, s.y * K, -s.z * G);
    index.x = (uint)(c0 ? 2 * (1 - up.y) + (1 - up.x) : up.x + 8 + 2 * (1 - up.z));
    index.y = (uint)(c1 ? 5 - up.y + 2 * (1 - up.z) : 2 * up.y + (1 - up.x));
    index.z = (uint)(c2 ? 9 - up.x + 2 * (1 - up.z) : 5 - up.y + 2 * up.z);
    float3 e1 = b - (c + a) * 0.5;
    float3 e2 = c - (b + a) * 0.5;
    weight.y = saturate((dot(n, e1) - dot(a, e1)) / (dot(b, e1) - dot(a, e1)));
    weight.z = saturate((dot(n, e2) - dot(a, e2)) / (dot(c, e2) - dot(a, e2)));
    weight.x = saturate(1 - weight.y - weight.z);
}

// The engine finds the tetrahedron with a BSP tree; here a grid cell names one to walk from.
float3 ProbeLight(float3 p, float3 n) {
    float3 origin = asfloat(uint3(probeGrid[0], probeGrid[1], probeGrid[2]));
    float cellSize = asfloat(probeGrid[3]);
    int3 dims = int3(probeGrid[4], probeGrid[5], probeGrid[6]);
    int3 cell = clamp(int3(floor((p - origin) / cellSize)), 0, dims - 1);
    uint t = probeGrid[8 + (cell.z * dims.y + cell.y) * dims.x + cell.x];
    if (t == 0xFFFFFFFF) return 0;
    float4 w = TetraWeights(t, p);
    for (uint step = 0; step < 64; step++) {
        float lowest = min(min(w.x, w.y), min(w.z, w.w));
        if (lowest >= -1e-4) break;
        uint k = lowest == w.x ? 0 : lowest == w.y ? 1 : lowest == w.z ? 2 : 3;
        uint next = probeTetrahedra[t * 20 + 4 + k];
        if (next == 0xFFFFFFFF) break;
        t = next;
        w = TetraWeights(t, p);
    }
    w = max(w, 0);
    w /= max(w.x + w.y + w.z + w.w, 1e-6);
    uint3 index;
    float3 weight;
    IcosahedronFace(n, index, weight);
    float3 light = 0;
    [unroll] for (uint v = 0; v < 4; v++) {
        uint probe = probeTetrahedra[t * 20 + v] * 12;
        light += w[v] * (weight.x * UnpackProbe(probeValues[probe + index.x]) +
                         weight.y * UnpackProbe(probeValues[probe + index.y]) +
                         weight.z * UnpackProbe(probeValues[probe + index.z]));
    }
    return light;
}

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float3 DecodeNormal(float2 e) {
    float2 f = e * 2 - 1;
    float3 n = float3(f, 1 - abs(f.x) - abs(f.y));
    if (n.z < 0) n.xy = (1 - abs(n.yx)) * float2(n.x >= 0 ? 1 : -1, n.y >= 0 ? 1 : -1);
    return normalize(n);
}

float3 Irradiance(float3 n) {
    float3 e = sh[0].xyz * 0.282095
             + (sh[1].xyz * n.y + sh[2].xyz * n.z + sh[3].xyz * n.x) * 0.488603
             + (sh[4].xyz * n.x * n.y + sh[5].xyz * n.y * n.z + sh[7].xyz * n.x * n.z) * 1.092548
             + sh[6].xyz * 0.315392 * (3 * n.z * n.z - 1)
             + sh[8].xyz * 0.546274 * (n.x * n.x - n.y * n.y);
    return max(e, 0);
}

uint Pack(float3 c) {
    c = max(c, 0);
    return ((f32tof16(c.r) >> 4) & 0x7FF) | (((f32tof16(c.g) >> 4) & 0x7FF) << 11) | (((f32tof16(c.b) >> 5) & 0x3FF) << 22);
}

struct Indirect {
    uint diffuse : SV_Target0;
    uint specular : SV_Target1;
};

Indirect PSMain(float4 pos : SV_Position) {
    Indirect o;
    o.diffuse = 0;
    o.specular = 0;
    int2 p = int2(pos.xy);
    float depth = depthTex.Load(int3(p, 0)).r;
    if (depth >= 1.0) return o;
    float4 g2 = gb2.Load(int3(p, 0));
    float occlusion = gb3.Load(int3(p, 0)).z;
    float3 n = DecodeNormal(g2.xy);
    uint w, h;
    depthTex.GetDimensions(w, h);
    float2 ndc = float2((p.x + 0.5) / w * 2 - 1, 1 - (p.y + 0.5) / h * 2);
    float4 world = mul(float4(ndc, depth, 1), viewProjInv);
    float3 v = normalize(eye - world.xyz / world.w);
    float roughness = saturate(g2.z);
    float2 brdfUv = float2(clamp(saturate(dot(n, v)), 1.0 / 512, 1 - 1.0 / 512), roughness);
    float brdfDiffuse = ambientBrdf.SampleLevel(linearSampler, brdfUv, 0).z;
    float3 diffuse;
    float3 specular;
    if (lightProbes) {
        diffuse = ProbeLight(world.xyz / world.w, n);
        specular = diffuse;
    } else {
        diffuse = Irradiance(n) / 3.14159265;
        float3 r = reflect(-v, n);
        float2 uv = float2(frac(atan2(r.z, r.x) / 6.2831853 + 0.5 - rotation), acos(clamp(r.y, -1, 1)) / 3.14159265);
        specular = iblFiltered.SampleLevel(linearSampler, uv, roughness * filteredLod).rgb;
    }
    o.diffuse = Pack(diffuse * brdfDiffuse * occlusion);
    o.specular = Pack(specular * occlusion);
    return o;
}
)";
    ComPtr<ID3DBlob> ambientVs;
    ComPtr<ID3DBlob> ambientPs;
    Check(D3DCompile(AMBIENT_SOURCE, std::strlen(AMBIENT_SOURCE), nullptr, nullptr, nullptr,
                     "VSMain", "vs_5_0", 0, 0, &ambientVs, &compileErrors), "compile ambient VS");
    if (FAILED(D3DCompile(AMBIENT_SOURCE, std::strlen(AMBIENT_SOURCE), nullptr, nullptr, nullptr,
                          "PSMain", "ps_5_0", 0, 0, &ambientPs, &compileErrors))) {
        throw std::runtime_error(std::string("compile ambient PS: ") +
                                 (compileErrors ? static_cast<const char*>(compileErrors->GetBufferPointer()) : ""));
    }
    psoDesc.VS = { ambientVs->GetBufferPointer(), ambientVs->GetBufferSize() };
    psoDesc.PS = { ambientPs->GetBufferPointer(), ambientPs->GetBufferSize() };
    psoDesc.NumRenderTargets = 2;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_UINT;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R32_UINT;
    psoDesc.BlendState.RenderTarget[1] = psoDesc.BlendState.RenderTarget[0];
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&ambientPso)), "CreateGraphicsPipelineState ambient");
}

void Viewer::BindIndirectTargets() {
    if (!(hasSceneIbl || hasLightProbes) || !gidTarget) return;
    device->CreateShaderResourceView(gidTarget.Get(), nullptr, SrvCpuHandle(SRV_LIGHT_GID));
    device->CreateShaderResourceView(gisTarget.Get(), nullptr, SrvCpuHandle(SRV_LIGHT_GIS));
}

void Viewer::CreateHdrTargets() {
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC hdrDesc{};
    hdrDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    hdrDesc.Width = width;
    hdrDesc.Height = height;
    hdrDesc.DepthOrArraySize = 1;
    hdrDesc.MipLevels = 1;
    hdrDesc.Format = HDR_FORMAT;
    hdrDesc.SampleDesc.Count = 1;
    hdrDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE hdrClear{};
    hdrClear.Format = hdrDesc.Format;

    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &hdrDesc,
                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &hdrClear,
                                          IID_PPV_ARGS(&hdrTarget)), "CreateCommittedResource hdr");

    if (!hdrRtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = 4;
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&hdrRtvHeap)), "CreateDescriptorHeap hdr RTV");
    }
    device->CreateRenderTargetView(hdrTarget.Get(), nullptr, hdrRtvHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateShaderResourceView(hdrTarget.Get(), nullptr, SrvCpuHandle(SRV_HDR));

    // DeferredLight writes specular to RT0 and diffuse to RT1; the resolve adds them.
    if (lightingRtCount > 1) {
        Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &hdrDesc,
                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &hdrClear,
                                              IID_PPV_ARGS(&hdrAuxTarget)), "CreateCommittedResource hdr aux");
        D3D12_CPU_DESCRIPTOR_HANDLE auxRtv = hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
        auxRtv.ptr += rtvStride;
        device->CreateRenderTargetView(hdrAuxTarget.Get(), nullptr, auxRtv);
        device->CreateShaderResourceView(hdrAuxTarget.Get(), nullptr, SrvCpuHandle(SRV_HDR_AUX));
    }

    D3D12_RESOURCE_DESC gidDesc = hdrDesc;
    gidDesc.Format = DXGI_FORMAT_R32_UINT;
    gidDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_CLEAR_VALUE gidClear{};
    gidClear.Format = gidDesc.Format;
    ComPtr<ID3D12Resource>* indirect[2] = { &gidTarget, &gisTarget };
    for (int i = 0; i < 2; i++) {
        Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &gidDesc,
                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &gidClear,
                                              IID_PPV_ARGS(indirect[i]->ReleaseAndGetAddressOf())), "CreateCommittedResource gi");
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += (2 + i) * static_cast<SIZE_T>(rtvStride);
        device->CreateRenderTargetView(indirect[i]->Get(), nullptr, rtv);
    }
    BindIndirectTargets();
    CreateIndirectTargets();
    CreateBlurredSolid();

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depthBuffer.Get(), &depthSrv, SrvCpuHandle(SRV_LIGHT_DEPTH));
}

void Viewer::CreateGameLighting(const GameLightingDesc& desc) {
    EnsureGameCommon();
    lightingRtCount = desc.renderTargetCount;
    // Non-Single DeferredLight variants blend additively on top of the GBuffer
    // HDR output; the Single variants read it back as HDRImage instead.
    bool additive = desc.renderTargetCount > 1;
    GamePipelineConfig lightingConfig{ true, desc.renderTargetCount, HDR_FORMAT,
                                       false, false, desc.gidRegister, DXGI_FORMAT_UNKNOWN, additive };
    lightingConfig.lightingSlots = &desc.slots;
    if (!desc.blendState.empty()) lightingConfig.sdfBlend = desc.blendState.data();
    lightingPipeline = BuildGamePipeline(desc.vs, desc.ps, {}, lightingConfig);

    CreateHdrTargets();

    std::vector<uint8_t> lightZeros(352, 0);
    lightInfoBuffer = CreateUploadBuffer(lightZeros);
    D3D12_RANGE readRange{};
    Check(lightInfoBuffer->Map(0, &readRange, reinterpret_cast<void**>(&lightInfoMapped)), "Map lightInfo");

    float defaultDirection[3] = { 0.25f, 0.45f, -0.86f };
    float defaultColor[3] = { 10.0f, 10.0f, 10.0f };
    SetDirectionalLight(defaultDirection, defaultColor);

    Check(allocator->Reset(), "allocator Reset lighting");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset lighting");
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> staging;

    // Neutral stand-ins: white shadow maps = unshadowed, black masks = effects off.
    GameTextureDesc shadowDesc{ 4, 4, DXGI_FORMAT_R32_FLOAT, {} };
    std::vector<float> whiteDepth(16, 1.0f);
    std::span<const uint8_t> whiteSpan(reinterpret_cast<const uint8_t*>(whiteDepth.data()), 64);
    for (int i = 0; i < 8; i++) shadowDesc.mips.push_back({ whiteSpan, 16 });
    lightingDummies.push_back(CreateTexture(shadowDesc, 8, commandList.Get(), staging));

    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrv{};
    shadowSrv.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    shadowSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shadowSrv.Texture2DArray.MipLevels = 1;
    shadowSrv.Texture2DArray.ArraySize = 8;
    device->CreateShaderResourceView(lightingDummies.back().Get(), &shadowSrv, SrvCpuHandle(SRV_LIGHT_SHADOW));
    device->CreateShaderResourceView(lightingDummies.back().Get(), &shadowSrv, SrvCpuHandle(SRV_LIGHT_STATIC_SHADOW));

    GameTextureDesc brdfDesc{ 4, 4, DXGI_FORMAT_R8G8_UNORM, {} };
    std::vector<uint8_t> brdfData(32);
    for (int i = 0; i < 16; i++) { brdfData[i * 2] = 255; brdfData[i * 2 + 1] = 0; }
    brdfDesc.mips.push_back({ std::span<const uint8_t>(brdfData), 8 });
    lightingDummies.push_back(CreateTexture(brdfDesc, 1, commandList.Get(), staging));

    D3D12_SHADER_RESOURCE_VIEW_DESC brdfSrv{};
    brdfSrv.Format = DXGI_FORMAT_R8G8_UNORM;
    brdfSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    brdfSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    brdfSrv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(lightingDummies.back().Get(), &brdfSrv, SrvCpuHandle(SRV_LIGHT_BRDF));

    GameTextureDesc blackDesc{ 4, 4, DXGI_FORMAT_R16G16B16A16_FLOAT, {} };
    std::vector<uint8_t> blackData(4 * 4 * 8, 0);
    blackDesc.mips.push_back({ std::span<const uint8_t>(blackData), 32 });
    lightingDummies.push_back(CreateTexture(blackDesc, 1, commandList.Get(), staging));
    device->CreateShaderResourceView(lightingDummies.back().Get(), nullptr, SrvCpuHandle(SRV_LIGHT_BLACK));

    GameTextureDesc gidDesc{ 4, 4, DXGI_FORMAT_R32_UINT, {} };
    std::vector<uint8_t> gidData(64, 0);
    gidDesc.mips.push_back({ std::span<const uint8_t>(gidData), 16 });
    lightingDummies.push_back(CreateTexture(gidDesc, 1, commandList.Get(), staging));
    device->CreateShaderResourceView(lightingDummies.back().Get(), nullptr, SrvCpuHandle(SRV_LIGHT_GID));
    device->CreateShaderResourceView(lightingDummies.back().Get(), nullptr, SrvCpuHandle(SRV_LIGHT_GIS));

    auto createSimple = [&](D3D12_RESOURCE_DIMENSION dimension, DXGI_FORMAT format,
                            D3D12_SRV_DIMENSION viewDimension, uint32_t heapIndex) {
        D3D12_RESOURCE_DESC simpleDesc{};
        simpleDesc.Dimension = dimension;
        simpleDesc.Width = 4;
        simpleDesc.Height = dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ? 1 : 4;
        simpleDesc.DepthOrArraySize = dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 4 : 1;
        simpleDesc.MipLevels = 1;
        simpleDesc.Format = format;
        simpleDesc.SampleDesc.Count = 1;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        ComPtr<ID3D12Resource> texture;
        Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &simpleDesc,
                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&texture)), "CreateCommittedResource simple");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT rows;
        UINT64 rowSize, total;
        device->GetCopyableFootprints(&simpleDesc, 0, 1, 0, &footprint, &rows, &rowSize, &total);

        std::vector<uint8_t> zeros(total, 0);
        ComPtr<ID3D12Resource> upload = CreateUploadBuffer(zeros);
        staging.push_back(upload);

        D3D12_TEXTURE_COPY_LOCATION dst{ texture.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
        D3D12_TEXTURE_COPY_LOCATION src{ upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {} };
        src.PlacedFootprint = footprint;
        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier = Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &barrier);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = format;
        srvDesc.ViewDimension = viewDimension;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (viewDimension == D3D12_SRV_DIMENSION_TEXTURE3D) srvDesc.Texture3D.MipLevels = 1;
        else if (viewDimension == D3D12_SRV_DIMENSION_TEXTURE1DARRAY) {
            srvDesc.Texture1DArray.MipLevels = 1;
            srvDesc.Texture1DArray.ArraySize = 1;
        }
        device->CreateShaderResourceView(texture.Get(), &srvDesc, SrvCpuHandle(heapIndex));
        lightingDummies.push_back(texture);
    };

    createSimple(D3D12_RESOURCE_DIMENSION_TEXTURE3D, DXGI_FORMAT_R32_UINT,
                 D3D12_SRV_DIMENSION_TEXTURE3D, SRV_LIGHT_VOLUME);
    createSimple(D3D12_RESOURCE_DIMENSION_TEXTURE1D, DXGI_FORMAT_R32_FLOAT,
                 D3D12_SRV_DIMENSION_TEXTURE1DARRAY, SRV_LIGHT_IES);

    Check(commandList->Close(), "Close lighting upload");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    WaitForGpu();
}
