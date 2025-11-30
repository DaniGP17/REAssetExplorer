#include "Renderer/Viewer.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "D3D12Utils.h"

void Viewer::UpdateFrustumCull() {
    drawVisible.assign(draws.size(), 1);
    for (std::size_t i = 0; i < draws.size(); i++) {
        const ViewerMeshDraw& d = draws[i];
        if (d.shadowOnly || (!drawMask.empty() && (d.id >= drawMask.size() || !drawMask[d.id]))) {
            drawVisible[i] = 0;
            continue;
        }
        if (d.boundsRadius <= 0) continue;
        for (int p = 0; p < 6; p++) {
            float dist = frustumPlanes[p][0] * d.boundsCenter[0] +
                         frustumPlanes[p][1] * d.boundsCenter[1] +
                         frustumPlanes[p][2] * d.boundsCenter[2] + frustumPlanes[p][3];
            if (dist < -d.boundsRadius) {
                drawVisible[i] = 0;
                break;
            }
        }
    }
}

// The ToneMapping cbuffer game shaders read. Fields the scene data does not drive keep the values of the
// RE8 capture; emissive G-buffer output is divided by the exposure and white point.
void Viewer::WriteTonemapConstants() {
    struct {
        float exposureAdjustment;
        float tonemapRange;
        float sharpness;
        float preTonemapRange;
        int32_t useAutoExposure;
        float echoBlend;
        float aaBlend;
        float aaSubPixel;
        float responsiveAaRate;
    } constants{ toneMap.exposure,
                 temporalAA.tonemapRange,
                 temporalAA.sharpness,
                 std::max(temporalAA.tonemapRange, temporalAA.preTonemapRange),
                 toneMap.autoExposure && exposureState && !exposureReset ? 1 : 0,
                 0.0f,
                 std::min(static_cast<float>(temporalAccumulated) / static_cast<float>(temporalAccumulated + 1), temporalAA.blend),
                 temporalAA.subPixel,
                 temporalAA.responsiveRate };
    std::memcpy(tonemapMapped, &constants, sizeof(constants));
}

// rayTracingParams and the breakingPBR terms are the RE8 capture's.
void Viewer::WriteEnvironmentConstants() {
    struct {
        uint32_t timeMillisecond;
        uint32_t frameCount;
        uint32_t isOddFrame;
        uint32_t reserve0;
        uint32_t rayTracingParams[4];
        float breakingPbr[4];
        uint32_t reserve1[4];
        float userGlobalParams[32][4];
    } constants{};
    static_assert(sizeof(constants) == 576);
    uint64_t now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    if (clockStartMs == 0) clockStartMs = now;
    uint64_t frame = environment.frame >= 0 ? static_cast<uint64_t>(environment.frame) : frameCounter;
    constants.timeMillisecond = static_cast<uint32_t>(environment.timeMs >= 0 ? environment.timeMs : now - clockStartMs);
    constants.frameCount = static_cast<uint32_t>(frame);
    constants.isOddFrame = static_cast<uint32_t>(frame & 1);
    constants.rayTracingParams[1] = 0x3E000000;
    constants.rayTracingParams[2] = 0x3E000000;
    constants.breakingPbr[0] = 1;
    constants.breakingPbr[2] = 1;
    std::memcpy(constants.userGlobalParams, environment.userGlobalParams, sizeof(constants.userGlobalParams));
    std::memcpy(environmentMapped, &constants, sizeof(constants));
    frameCounter++;
}

void Viewer::DrawResolve(D3D12_CPU_DESCRIPTOR_HANDLE rtv, bool studio, ID3D12PipelineState* pso) {
    struct ResolveConstants {
        uint32_t mode;
        float eye[3];
        float viewProjInv[16];
        float skyIntensity;
        float background[3];
        float sphere[4];
        float light[4];
        float backgroundBottom[3];
        float skyRotation;
        float skyLod;
        uint32_t sceneIbl;
        float exposure;
        float toneContrast;
        float toneLinearBegin;
        float toneLinearLength;
        float toneToe;
        float iblExposure;
        float iblRotation;
        float iblAddBlend;
        float iblVirtualOffset;
        float pad0;
        float iblTrim[4];
        uint32_t autoExposure;
        float histogramScale;
        float histogramBins;
        uint32_t fogApplied;
    } resolveConstants{};
    static_assert(sizeof(ResolveConstants) <= PASS_CONSTANTS_AMBIENT);
    resolveConstants.mode = gbufferView != GBufferView::None ? 4 + static_cast<uint32_t>(gbufferView)
                          : wireframe                          ? 3
                          : unlit                              ? 2
                          : studio                             ? 4
                                                               : 0;
    for (int i = 0; i < 4; i++) resolveConstants.sphere[i] = previewSphere[i];
    // Camera-fixed key light, upper left in front; camView columns are right, up, back.
    float light[3];
    for (int i = 0; i < 3; i++) {
        light[i] = -camView[i * 4] * 0.6f + camView[i * 4 + 1] * 0.7f + camView[i * 4 + 2] * 0.55f;
    }
    float lightLen = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    for (int i = 0; i < 3; i++) resolveConstants.light[i] = light[i] / lightLen;
    resolveConstants.light[3] = 0;
    for (int i = 0; i < 3; i++) resolveConstants.eye[i] = skyEye[i];
    for (int i = 0; i < 16; i++) resolveConstants.viewProjInv[i] = skyViewProjInv[i];
    resolveConstants.skyIntensity = skyIntensity;
    for (int i = 0; i < 3; i++) {
        resolveConstants.background[i] = background[i];
        resolveConstants.backgroundBottom[i] = backgroundBottom[i];
    }
    resolveConstants.skyRotation = skyRotation;
    // The last mips of an equirect sky are a few texels: stop where the horizon still reads.
    resolveConstants.skyLod = skyBlur * static_cast<float>(skyMips > 4 ? skyMips - 4 : 0);
    resolveConstants.sceneIbl = hasSceneIbl ? 1 : 0;
    resolveConstants.exposure = toneMap.exposure;
    resolveConstants.toneContrast = toneMap.contrast;
    resolveConstants.toneLinearBegin = toneMap.linearBegin;
    resolveConstants.toneLinearLength = toneMap.linearLength;
    resolveConstants.toneToe = toneMap.toe;
    resolveConstants.iblExposure = sceneIbl.exposure;
    resolveConstants.iblRotation = sceneIbl.rotation;
    resolveConstants.iblAddBlend = sceneIbl.addBlend;
    resolveConstants.iblVirtualOffset = sceneIbl.virtualOffset;
    resolveConstants.iblTrim[0] = sceneIbl.trimScale[0];
    resolveConstants.iblTrim[1] = sceneIbl.trimScale[1];
    resolveConstants.iblTrim[2] = sceneIbl.trimOffset[0];
    resolveConstants.iblTrim[3] = sceneIbl.trimOffset[1];
    resolveConstants.autoExposure = toneMap.autoExposure ? 1 : 0;
    resolveConstants.histogramScale = 1.0f / std::max(toneMap.minWhite, 1e-6f);
    resolveConstants.histogramBins = 1024.0f / std::max(std::log2(toneMap.maxWhite / std::max(toneMap.minWhite, 1e-6f)), 1e-6f);
    resolveConstants.fogApplied = fogTarget ? 1 : 0;

    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    commandList->SetGraphicsRootSignature(resolveRootSignature.Get());
    commandList->SetPipelineState(pso ? pso : resolvePso.Get());
    commandList->SetGraphicsRootDescriptorTable(0, SrvGpuHandle(SRV_GBUFFER0));
    std::memcpy(passConstantsMapped, &resolveConstants, sizeof(resolveConstants));
    commandList->SetGraphicsRootConstantBufferView(1, passConstants->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(2, SrvGpuHandle(SRV_HDR_AUX));
    commandList->SetGraphicsRootDescriptorTable(3, SrvGpuHandle(UAV_EXPOSURE));
    commandList->SetGraphicsRootDescriptorTable(4, SrvGpuHandle(SRV_FOG_BASE + FOG_VIEW_TARGET_SRV));
    commandList->DrawInstanced(3, 1, 0, 0);
}

void Viewer::DrawAmbient() {
    struct AmbientConstants {
        float viewProjInv[16];
        float eye[3];
        float filteredLod;
        float sh[9][4];
        float rotation;
        uint32_t lightProbes;
        float pad[2];
    } constants{};
    static_assert(sizeof(AmbientConstants) <= PASS_CONSTANTS_BYTES - PASS_CONSTANTS_AMBIENT);
    for (int i = 0; i < 16; i++) constants.viewProjInv[i] = skyViewProjInv[i];
    for (int i = 0; i < 3; i++) constants.eye[i] = skyEye[i];
    constants.filteredLod = static_cast<float>(sceneIblFilteredMips - 1);
    std::memcpy(constants.sh, sceneIbl.irradianceSh, sizeof(constants.sh));
    constants.rotation = sceneIbl.rotation;
    constants.lightProbes = hasLightProbes ? 1 : 0;

    D3D12_RESOURCE_BARRIER toRt[2] = {
        Transition(gidTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        Transition(gisTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
    };
    commandList->ResourceBarrier(2, toRt);
    D3D12_CPU_DESCRIPTOR_HANDLE indirectRtv = hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
    indirectRtv.ptr += 2 * static_cast<SIZE_T>(rtvStride);
    commandList->OMSetRenderTargets(2, &indirectRtv, TRUE, nullptr);
    commandList->SetGraphicsRootSignature(resolveRootSignature.Get());
    commandList->SetPipelineState(ambientPso.Get());
    commandList->SetGraphicsRootDescriptorTable(0, SrvGpuHandle(SRV_GBUFFER0));
    std::memcpy(passConstantsMapped + PASS_CONSTANTS_AMBIENT, &constants, sizeof(constants));
    commandList->SetGraphicsRootConstantBufferView(1, passConstants->GetGPUVirtualAddress() + PASS_CONSTANTS_AMBIENT);
    commandList->SetGraphicsRootDescriptorTable(2, SrvGpuHandle(SRV_HDR_AUX));
    commandList->DrawInstanced(3, 1, 0, 0);
    D3D12_RESOURCE_BARRIER toSrv[2] = {
        Transition(gidTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        Transition(gisTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
    };
    commandList->ResourceBarrier(2, toSrv);
}

// The cleared depth makes the resolve show the sky everywhere.
void Viewer::RenderSkyOnly(D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
    ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_RESOURCE_BARRIER depthToSrv = Transition(depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &depthToSrv);
    DrawFog(false);
    RenderParticles();
    DrawResolve(rtv, false);
    if (gridEnabled) DrawGrid();
    DrawOverlay();
    DrawGizmos();
    D3D12_RESOURCE_BARRIER depthBack = Transition(depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                  D3D12_RESOURCE_STATE_DEPTH_WRITE);
    commandList->ResourceBarrier(1, &depthBack);
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
}

namespace {

// 0 start, 1 after prepass, 2 after gbuffer, 3 after shadows, 4 after CACAO,
// 5 after GI/ambient, 6 after the deferred lighting composite, 7 after forward+fog, 8 end.
constexpr uint32_t TIMESTAMP_COUNT = 9;

using Clock = std::chrono::steady_clock;

float Milliseconds(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<float, std::milli>(to - from).count();
}

}

void Viewer::Stamp(uint32_t index) {
    if (!timestampHeap) {
        D3D12_QUERY_HEAP_DESC desc{ D3D12_QUERY_HEAP_TYPE_TIMESTAMP, TIMESTAMP_COUNT, 0 };
        Check(device->CreateQueryHeap(&desc, IID_PPV_ARGS(&timestampHeap)), "CreateQueryHeap timestamps");
        D3D12_HEAP_PROPERTIES props{};
        props.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = TIMESTAMP_COUNT * sizeof(uint64_t);
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                              nullptr, IID_PPV_ARGS(&timestampReadback)), "CreateCommittedResource timestamps");
        Check(queue->GetTimestampFrequency(&timestampFrequency), "GetTimestampFrequency");
    }
    commandList->EndQuery(timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
    stampsWritten |= 1u << index;
}

void Viewer::ReadTimestamps() {
    if (!timestampReadback || timestampFrequency == 0) return;
    uint64_t* t = nullptr;
    D3D12_RANGE range{ 0, TIMESTAMP_COUNT * sizeof(uint64_t) };
    if (FAILED(timestampReadback->Map(0, &range, reinterpret_cast<void**>(&t)))) return;
    double toMs = 1000.0 / static_cast<double>(timestampFrequency);
    auto span = [&](uint32_t a, uint32_t b) { return static_cast<float>(static_cast<double>(t[b] - t[a]) * toMs); };
    bool geometry = (stampsMeasured & 0b0110) == 0b0110;
    timings.gpuPrepassMs = geometry ? span(0, 1) : 0;
    timings.gpuGBufferMs = geometry ? span(1, 2) : 0;
    bool lighting = (stampsMeasured & 0b11111000) == 0b11111000;
    timings.gpuShadowMs = lighting ? span(2, 3) : 0;
    timings.gpuCacaoMs = lighting ? span(3, 4) : 0;
    timings.gpuGiMs = lighting ? span(4, 5) : 0;
    timings.gpuLightingMs = lighting ? span(5, 6) : 0;
    timings.gpuForwardFogMs = lighting ? span(6, 7) : 0;
    uint32_t last = lighting ? 7 : geometry ? 2 : 0;
    timings.gpuRestMs = span(last, 8);
    D3D12_RANGE none{};
    timestampReadback->Unmap(0, &none);
}

void Viewer::RenderFrame() {
    Clock::time_point start = Clock::now();
    Check(allocator->Reset(), "allocator Reset");
    Check(commandList->Reset(allocator.Get(), nullptr), "commandList Reset");
    stampsWritten = 0;
    Stamp(0);
    std::vector<uint8_t>* finalOut = nullptr;
    if (targetCaptureOut) {
        targetCopies.clear();
        targetFinal.clear();
        if (!captureOut) captureOut = &targetFinal;
        finalOut = captureOut;
    }
    BeginFrameJitter();
    UpdateFrustumCull();
    uint32_t cpuDrawn = 0;
    uint64_t triangles = 0;
    Clock::time_point culled = Clock::now();

    D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
    D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    ID3D12Resource* backBuffer = renderTargets[frameIndex].Get();
    D3D12_RESOURCE_BARRIER toRenderTarget = Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &toRenderTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameIndex) * rtvStride;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    if (hasMesh && srvHeap) {
        ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VERTEX_BUFFER_VIEW views[] = { positionView, normalView, uv0View, uv1View, weightsView };
        commandList->IASetVertexBuffers(0, 5, views);
        commandList->IASetIndexBuffer(&indexView);

        if (!deferredPipelines.empty()) {
            WriteTonemapConstants();
            WriteEnvironmentConstants();
            if (lightInfoMapped) {
                uint32_t cubemaps = showFlags.localCubemaps ? localCubemapCount : 0;
                std::memcpy(lightInfoMapped + 44, &cubemaps, 4);
            }
            if (exposureState) {
                D3D12_RESOURCE_BARRIER toSrv = Transition(exposureState.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                commandList->ResourceBarrier(1, &toSrv);
            }
            bool culled = !wireframe && EnsureCulling();
            const std::vector<GamePipeline>& pipelines = wireframe ? wireframePipelines : deferredPipelines;
            auto drawList = [&](const std::vector<GamePipeline>& set, bool prepass, bool blendPass) {
                uint32_t current = UINT32_MAX;
                for (std::size_t di = 0; di < draws.size(); di++) {
                    const ViewerMeshDraw& draw = draws[di];
                    if (!drawVisible.empty() && !drawVisible[di]) continue;
                    // Occluded last frame; a draw that has just come out from behind shows a frame late.
                    if (culled && cullFlagsCpu[di] == 2) continue;
                    uint32_t pipelineIndex = draw.pipelineIndex < deferredPipelines.size() ? draw.pipelineIndex : 0;
                    if (pipelineIndex >= set.size() || !set[pipelineIndex].pso) continue;
                    if (!prepass && set[pipelineIndex].blends != blendPass) continue;
                    if (pipelineIndex != current) {
                        current = pipelineIndex;
                        BindGamePipeline(set[pipelineIndex]);
                    }
                    for (uint32_t paramIndex : set[pipelineIndex].rootConstParams) {
                        commandList->SetGraphicsRoot32BitConstant(paramIndex, draw.instanceIndex | (draw.materialSlot << 24), 0);
                    }
                    commandList->DrawIndexedInstanced(draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
                    if (!prepass) {
                        cpuDrawn++;
                        triangles += draw.indexCount / 3;
                    }
                }
            };

            // GBuffer PSOs of materials with a prepass test depth EQUAL, so the prepass runs first.
            if (!wireframe) {
                commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
                drawList(prepassPipelines, true, false);
            }

            Stamp(1);
            for (int i = 0; i < 4; i++) {
                D3D12_RESOURCE_BARRIER toRT = Transition(gbuffer[i].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                         D3D12_RESOURCE_STATE_RENDER_TARGET);
                commandList->ResourceBarrier(1, &toRT);
            }

            D3D12_CPU_DESCRIPTOR_HANDLE gbufferRtvs[4];
            D3D12_CPU_DESCRIPTOR_HANDLE base = gbufferRtvHeap->GetCPUDescriptorHandleForHeapStart();
            const float black[4] = { 0, 0, 0, 0 };
            for (int i = 0; i < 4; i++) {
                gbufferRtvs[i] = { base.ptr + static_cast<SIZE_T>(i) * rtvStride };
                commandList->ClearRenderTargetView(gbufferRtvs[i], black, 0, nullptr);
            }
            commandList->OMSetRenderTargets(4, gbufferRtvs, FALSE, &dsv);

            drawList(pipelines, false, false);
            if (culled) {
                BuildHiZ();
                CullDraws();
            }
            drawList(pipelines, false, true);
            if (debugView == ViewerDebugView::LightingOnly && !wireframe) GreyAlbedo();

            Stamp(2);
            if (exposureState) {
                D3D12_RESOURCE_BARRIER toUav = Transition(exposureState.Get(),
                                                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                commandList->ResourceBarrier(1, &toUav);
            }
            bool outline = hasSelection && selectionDepth && selectionOutlineEnabled;
            if (outline) RenderSelectionMask();

            for (int i = 0; i < 4; i++) {
                D3D12_RESOURCE_BARRIER toSrv = Transition(gbuffer[i].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                commandList->ResourceBarrier(1, &toSrv);
            }

            D3D12_RESOURCE_BARRIER depthToSrv = Transition(depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            commandList->ResourceBarrier(1, &depthToSrv);

            bool studio = previewSphere[3] > 0 && !unlit && !wireframe;
            // Unlit's resolve shader reads only gb1 (albedo) and the sky/IBL, never the lit result,
            // so shadows/CACAO/GI/volumetric fog below only cost time without changing its output.
            bool skipLighting = unlit && gbufferView == GBufferView::None;
            if (lightingPipeline.pso && !wireframe && !studio && !skipLighting) {
                UpdateShadowCascades();
                RenderShadowCascades();
                Stamp(3);
                if (hasCacao && cacaoEnabled) DispatchCacao();
                Stamp(4);
                if (hasLightProbes && indirectPso && probeBspTree) DispatchIndirectIllumination();
                else if ((hasSceneIbl || hasLightProbes) && gidTarget && ambientPso) DrawAmbient();
                Stamp(5);
                // gbuffer[0] holds ambient/emissive; DeferredLight adds direct lighting on top.
                D3D12_RESOURCE_BARRIER toCopy[2] = {
                    Transition(hdrTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                               D3D12_RESOURCE_STATE_COPY_DEST),
                    Transition(gbuffer[0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE),
                };
                commandList->ResourceBarrier(2, toCopy);
                commandList->CopyResource(hdrTarget.Get(), gbuffer[0].Get());
                D3D12_RESOURCE_BARRIER fromCopy[2] = {
                    Transition(hdrTarget.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_RENDER_TARGET),
                    Transition(gbuffer[0].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                };
                commandList->ResourceBarrier(2, fromCopy);

                D3D12_CPU_DESCRIPTOR_HANDLE hdrRtvs[2];
                hdrRtvs[0] = hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
                hdrRtvs[1] = { hdrRtvs[0].ptr + rtvStride };
                if (hdrAuxTarget) {
                    D3D12_RESOURCE_BARRIER auxToRt = Transition(hdrAuxTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                                D3D12_RESOURCE_STATE_RENDER_TARGET);
                    commandList->ResourceBarrier(1, &auxToRt);
                }
                for (UINT rt = 1; rt < lightingRtCount; rt++) {
                    commandList->ClearRenderTargetView(hdrRtvs[rt], black, 0, nullptr);
                }
                commandList->OMSetRenderTargets(lightingRtCount, hdrRtvs, FALSE, nullptr);
                BindGamePipeline(lightingPipeline);
                commandList->DrawInstanced(3, 1, 0, 0);
                Stamp(6);

                D3D12_RESOURCE_BARRIER hdrToSrv = Transition(hdrTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                commandList->ResourceBarrier(1, &hdrToSrv);
                if (hdrAuxTarget) {
                    D3D12_RESOURCE_BARRIER auxToSrv = Transition(hdrAuxTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    commandList->ResourceBarrier(1, &auxToSrv);
                }
                DrawForward(culled, cpuDrawn, triangles);
                DispatchVolumetricFog();
                DrawFog(true);
                Stamp(7);
            } else {
                DrawFog(false);
            }

            RenderParticles();
            bool post = PostProcessActive(studio);
            if (post) {
                D3D12_RESOURCE_BARRIER toRt = Transition(sceneHdrTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                         D3D12_RESOURCE_STATE_RENDER_TARGET);
                commandList->ResourceBarrier(1, &toRt);
                DrawResolve(postRtvHeap->GetCPUDescriptorHandleForHeapStart(), studio, resolveSceneHdrPso.Get());
                D3D12_RESOURCE_BARRIER toSrv = Transition(sceneHdrTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                commandList->ResourceBarrier(1, &toSrv);
            } else {
                DrawResolve(rtv, studio);
            }
            if (targetCaptureOut) RecordTargetCapture(rtv);
            if (post && temporalFrame) PrepareTemporalHistory();
            if (toneMap.autoExposure && !unlit && !wireframe && !studio && gbufferView == GBufferView::None) {
                if (post && GameExposureReady()) {
                    UpdateGameExposure(temporalFrame ? historyTarget.Get() : sceneHdrTarget.Get(),
                                       SRV_POST_BASE + (temporalFrame ? POST_VIEW_HISTORY_SRV : POST_VIEW_SCENE_SRV));
                } else {
                    UpdateExposure();
                }
            }
            if (post) RenderPostProcess(rtv);
            DrawDebugView(rtv);
            if (gridEnabled) DrawGrid();
            if (outline) DrawSelectionOutline();
            DrawOverlay();
            DrawGizmos();

            D3D12_RESOURCE_BARRIER depthBack = Transition(depthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                          D3D12_RESOURCE_STATE_DEPTH_WRITE);
            commandList->ResourceBarrier(1, &depthBack);
        } else if (prepassPipeline.pso) {
            commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
            BindGamePipeline(prepassPipeline);
            for (std::size_t di = 0; di < draws.size(); di++) {
                const ViewerMeshDraw& draw = draws[di];
                if (!drawVisible.empty() && !drawVisible[di]) continue;
                if (draw.pipelineIndex < deferredPipelines.size() &&
                    deferredPipelines[draw.pipelineIndex].skipPrepass) continue;
                for (uint32_t paramIndex : prepassPipeline.rootConstParams) {
                    commandList->SetGraphicsRoot32BitConstant(paramIndex, draw.instanceIndex | (draw.materialSlot << 24), 0);
                }
                commandList->DrawIndexedInstanced(draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
            }
        }

        commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    } else if (!previewTextures.empty()) {
        DrawPreviewTexture();
    } else if (resolvePso) {
        RenderSkyOnly(rtv, dsv);
    }

    bool screenshot = captureOut != nullptr;
    if (!screenshot && targetCaptureOut == nullptr) DrawTextOverlay(rtv);

    const UINT screenshotPitch = (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                                 ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    if (screenshot) {
        D3D12_RESOURCE_BARRIER toCopySource = Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->ResourceBarrier(1, &toCopySource);

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = backBuffer;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

        {
            UINT64 bytes = static_cast<UINT64>(screenshotPitch) * height;
            if (!screenshotBuffer || screenshotBuffer->GetDesc().Width < bytes) {
                D3D12_HEAP_PROPERTIES props{};
                props.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                desc.Width = bytes;
                desc.Height = 1;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.SampleDesc.Count = 1;
                desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
                                                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                      IID_PPV_ARGS(&screenshotBuffer)), "CreateCommittedResource screenshot");
            }
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = screenshotBuffer.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, screenshotPitch };
            commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }

        D3D12_RESOURCE_BARRIER toPresent = Transition(backBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        commandList->ResourceBarrier(1, &toPresent);
    } else {
        D3D12_RESOURCE_BARRIER toPresent = Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        commandList->ResourceBarrier(1, &toPresent);
    }

    if (temporalFrame) {
        std::memcpy(prevViewProjection, frameViewProjection, sizeof(prevViewProjection));
        hasPrevViewProjection = true;
    }
    stampsMeasured = stampsWritten;
    for (uint32_t i = 1; i + 1 < TIMESTAMP_COUNT; i++) {
        if (!(stampsWritten & (1u << i))) Stamp(i);
    }
    Stamp(TIMESTAMP_COUNT - 1);
    commandList->ResolveQueryData(timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, TIMESTAMP_COUNT,
                                  timestampReadback.Get(), 0);

    Check(commandList->Close(), "Close");
    ID3D12CommandList* lists[] = { commandList.Get() };
    queue->ExecuteCommandLists(1, lists);
    Clock::time_point submitted = Clock::now();

    // No vsync wait: the frame already waits for the GPU, and a missed vblank would double it.
    HRESULT presentResult = swapChain->Present(0, 0);
    Clock::time_point presented = Clock::now();
    if (FAILED(presentResult)) {
        std::fprintf(stderr, "present 0x%08X removedReason 0x%08X\n",
                     static_cast<unsigned>(presentResult),
                     static_cast<unsigned>(device->GetDeviceRemovedReason()));
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        if (SUCCEEDED(device.As(&dred))) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT crumbs{};
            if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&crumbs))) {
                for (const D3D12_AUTO_BREADCRUMB_NODE* node = crumbs.pHeadAutoBreadcrumbNode;
                     node != nullptr; node = node->pNext) {
                    UINT last = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
                    if (last == node->BreadcrumbCount) continue;
                    std::fprintf(stderr, "[dred] list stuck at %u/%u\n", last, node->BreadcrumbCount);
                    for (UINT i = last > 6 ? last - 6 : 0; i < node->BreadcrumbCount && i <= last + 2; i++) {
                        std::fprintf(stderr, "  %s op[%u] = %d\n", i == last ? ">>" : "  ",
                                     i, static_cast<int>(node->pCommandHistory[i]));
                    }
                }
            }
            D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
            if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&fault)) && fault.PageFaultVA != 0) {
                std::fprintf(stderr, "[dred] page fault VA 0x%llX\n",
                             static_cast<unsigned long long>(fault.PageFaultVA));
                auto dumpNodes = [](const char* label, const D3D12_DRED_ALLOCATION_NODE* node) {
                    for (int i = 0; node != nullptr && i < 12; node = node->pNext, i++) {
                        std::fprintf(stderr, "[dred] %s type=%d name=%s\n", label,
                                     static_cast<int>(node->AllocationType),
                                     node->ObjectNameA ? node->ObjectNameA : "?");
                    }
                };
                dumpNodes("alloc", fault.pHeadExistingAllocationNode);
                dumpNodes("freed", fault.pHeadRecentFreedAllocationNode);
            }
        }
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(device.As(&infoQueue))) {
            UINT64 count = infoQueue->GetNumStoredMessages();
            for (UINT64 i = 0; i < count; i++) {
                SIZE_T length = 0;
                infoQueue->GetMessage(i, nullptr, &length);
                std::vector<uint8_t> buffer(length);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
                if (SUCCEEDED(infoQueue->GetMessage(i, message, &length))) {
                    std::fprintf(stderr, "[d3d] %s\n", message->pDescription);
                }
            }
        }
        Check(presentResult, "Present");
    }
    WaitForGpu();
    Clock::time_point finished = Clock::now();
    frameIndex = swapChain->GetCurrentBackBufferIndex();
    timings.cullMs = Milliseconds(start, culled);
    timings.recordMs = Milliseconds(culled, submitted);
    timings.presentMs = Milliseconds(submitted, presented);
    timings.waitMs = Milliseconds(presented, finished);
    ReadTimestamps();
    ReadCullFlags();
    timings.drawn = cpuDrawn;
    timings.triangles = triangles;
    if (debugMessages) {
        UINT64 count = debugMessages->GetNumStoredMessages();
        for (UINT64 i = 0; i < count; i++) {
            SIZE_T length = 0;
            debugMessages->GetMessage(i, nullptr, &length);
            std::vector<uint8_t> buffer(length);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            if (SUCCEEDED(debugMessages->GetMessage(i, message, &length)) &&
                message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
                std::fprintf(stderr, "[d3d] %s\n", message->pDescription);
            }
        }
        debugMessages->ClearStoredMessages();
    }

    if (screenshot) {
        uint8_t* mapped = nullptr;
        D3D12_RANGE range{ 0, static_cast<SIZE_T>(screenshotPitch) * height };
        Check(screenshotBuffer->Map(0, &range, reinterpret_cast<void**>(&mapped)), "Map screenshot");
        captureOut->resize(static_cast<std::size_t>(width) * height * 4);
        for (UINT y = 0; y < height; y++) {
            std::memcpy(captureOut->data() + static_cast<std::size_t>(y) * width * 4,
                        mapped + static_cast<std::size_t>(y) * screenshotPitch, static_cast<std::size_t>(width) * 4);
        }
        captureOut = nullptr;
        D3D12_RANGE none{};
        screenshotBuffer->Unmap(0, &none);
    }
    if (targetCaptureOut) {
        ReadTargetCapture(*finalOut);
        targetCaptureOut = nullptr;
    }
}
