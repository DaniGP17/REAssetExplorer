#ifndef REASSETEXPLORER_VIEWER_H
#define REASSETEXPLORER_VIEWER_H
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "RenderTypes.h"

// Milliseconds of the last frame. The GPU split: alpha prepass, GBuffer draws, the rest
// (lighting, resolve, overlays).
struct ViewerFrameTimings {
    float cullMs = 0;
    float recordMs = 0;
    float presentMs = 0;
    float waitMs = 0;
    float gpuPrepassMs = 0;
    float gpuGBufferMs = 0;
    float gpuShadowMs = 0;
    float gpuCacaoMs = 0;
    float gpuGiMs = 0;
    float gpuLightingMs = 0;
    float gpuForwardFogMs = 0;
    float gpuRestMs = 0;
    uint32_t drawn = 0;  // draws that survived culling
    uint64_t triangles = 0;
    uint32_t shadowDraws = 0;
    uint32_t shadowPipelineSwitches = 0;
};

class Viewer {
public:
    Viewer(HWND hwnd, uint32_t width, uint32_t height);
    ~Viewer();

    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;

    void LoadMesh(const ViewerMesh& mesh);
    void RenderFrame();
    const ViewerFrameTimings& GetFrameTimings() const { return timings; }

    void CreateGamePrepass(const GamePrepassDesc& desc);
    void CreateGameDeferred(const std::vector<GameDeferredDesc>& descs);
    void CreateGameLighting(const GameLightingDesc& desc);
    void LoadGameMaterials(const std::vector<GameMaterialDesc>& materials,
                           const std::vector<GameTextureDesc>& textures,
                           const std::vector<std::vector<uint32_t>>& instanceMaterials);
    void LoadInstances(const std::vector<ViewerInstanceWorld>& worlds, uint32_t stride);
    // Between frames only.
    void SetInstanceWorld(uint32_t index, const float world[12]);
    // offset is into the mdf2 parameter block. Between frames only.
    void UpdateMaterialParams(uint32_t materialIndex, uint32_t offset, std::span<const uint8_t> bytes);
    void LoadLights(const std::vector<GameLightParam>& lights);
    // Between frames only.
    void UpdateLight(uint32_t index, const GameLightParam& light);
    // lights[first..] follow the camera: their positions and directions are relative to it.
    void SetCameraLights(uint32_t first, const std::vector<GameLightParam>& lights);
    void LoadSky(const GameTextureDesc& desc);
    bool HasSky() const { return skyTexture != nullptr; }
    // Only with no mesh loaded: the textures take the bindless slots.
    void LoadEffectTextures(const std::vector<GameTextureDesc>& textures);

    void SetUnlit(bool on) { unlit = on; }
    void SetWireframe(bool on) { wireframe = on; }
    void SetGBufferView(GBufferView view) { gbufferView = view; }
    void SetGrid(bool on) { gridEnabled = on; }
    // Indexed by draw id; 0 hides the draw. Empty shows every draw.
    void SetDrawMask(std::vector<uint8_t> mask) {
        drawMask = std::move(mask);
        cullMaskDirty = true;
    }
    void SetSelection(std::span<const uint32_t> drawIds);
    // Off keeps the selection but draws no outline.
    void SetSelectionOutline(bool on) { selectionOutlineEnabled = on; }
    void SetSkyIntensity(float intensity) { skyIntensity = intensity; }
    // rotation in turns about the up axis; blur 0 (sharp) .. 1 (blurriest mip used).
    void SetSkyView(float rotation, float blur) {
        skyRotation = rotation;
        skyBlur = blur;
    }
    void SetClearColor(float r, float g, float b) { clearColor[0] = r; clearColor[1] = g; clearColor[2] = b; }
    uint32_t GetWidth() const { return width; }
    uint32_t GetHeight() const { return height; }
    void SetParticles(std::span<const ViewerParticle> particles);
    // Programs by index for SetMaterialParticles; replaces the previous set.
    void SetMaterialPrograms(std::vector<ViewerMaterialProgram> programs);
    void SetMaterialParticles(std::span<const ViewerMaterialVertex> vertices, std::vector<ViewerMaterialBatch> batches,
                              std::span<const ViewerParticleLight> lights = {});
    // primitive.sdf's PreCalculateLighting*: lights the ViewerParticleLight points for the *Lighting programs.
    void CreateParticleLighting(const GameComputeDesc& desc);
    // Row-major float3x4 per entry (12 floats), indexed by instance jointOffset.
    void SetSkinningMatrices(std::span<const float> matrices);
    void SetDebugGeometry(std::span<const ViewerDebugVertex> triangles, std::span<const ViewerDebugVertex> lines);
    // Drawn over the final image. Triangles are shaded with a headlight and not culled.
    void SetOverlayGeometry(std::span<const ViewerDebugVertex> triangles, std::span<const ViewerDebugVertex> lines);
    // Drawn over the final image, after the overlay. Call between frames.
    void SetLightIcons(std::span<const ViewerLightIcon> icons);
    void SetGizmoLines(std::span<const ViewerDebugVertex> lines);
    // The transform gizmo: flat triangles over everything, after the light gizmos. Call between frames.
    void SetHandleGeometry(std::span<const ViewerDebugVertex> triangles);
    // Culling spheres of moved draws. Call between frames.
    void SetDrawBounds(std::span<const ViewerDrawBounds> bounds);
    // Premultiplied RGBA8 (R in the low byte), top-down, drawn over everything at pixel (x, y).
    // Zero size removes it. Call between frames.
    void SetTextOverlay(std::span<const uint32_t> rgba, uint32_t textWidth, uint32_t textHeight, int32_t x, int32_t y,
                        uint32_t slot = 0);
    static constexpr uint32_t TEXT_SLOTS = 2;
    // Alpha kept where the overlay is behind the scene: 1 draws it on top, 0 hides it.
    void SetOverlayOcclusion(float alpha) { overlayOccluded = alpha; }
    // Positions are xyz; indices are relative to each batch's baseVertex.
    void SetCollisionGeometry(std::span<const float> positions, std::span<const uint32_t> indices);
    // Instances contiguous per batch (firstInstance). Call between frames.
    void SetCollisionInstances(std::span<const ViewerCollisionInstance> instances,
                               std::span<const ViewerCollisionBatch> batches);
    void SetCollisionView(bool on) { collisionView = on; }
    void SetParticleIntensity(float intensity) { particleIntensity = intensity; }
    void ApplySceneMatrix();
    // up defaults to +Y.
    void SetCamera(const float eye[3], const float target[3], const float up[3] = nullptr);
    // NDC offset added to every projected position (a captured frame's TAA jitter).
    // With TemporalAA the frames between captures follow the engine's jitter sequence.
    void SetProjectionJitter(float x, float y) {
        explicitJitter[0] = projectionJitter[0] = x;
        explicitJitter[1] = projectionJitter[1] = y;
        hasExplicitJitter = true;
    }
    void SetClipPlanes(float nearPlane, float farPlane);
    void GetClipPlanes(float& nearPlane, float& farPlane) const {
        nearPlane = camNear;
        farPlane = camFar;
    }
    void SetFov(float fov);
    void Resize(uint32_t newWidth, uint32_t newHeight);
    void GetMeshBounds(float outCenter[3], float& outRadius) const;
    // color is the light color times its intensity; minAlpha is MinRoughness squared; scattering the
    // volumetric fog's in-scattered color.
    void SetDirectionalLight(const float direction[3], const float color[3], float minAlpha = 0,
                             const float* scattering = nullptr);
    void ClearDirectionalLight();
    void SetEnvironment(const ViewerEnvironment& value) { environment = value; }
    void SetToneMap(const ViewerToneMap& map, bool resetExposure = true) {
        toneMapSource = map;
        toneMap = map;
        if (exposureOverride) {
            toneMap.exposure = std::exp2(-exposureOverrideEv);
            toneMap.autoExposure = false;
        }
        exposureReset = exposureReset || resetExposure;
    }
    // A fixed EV in place of the scene's exposure and its auto exposure.
    void SetExposureOverride(bool on, float ev) {
        exposureOverride = on;
        exposureOverrideEv = ev;
        SetToneMap(toneMapSource, false);
    }
    // Off keeps TemporalAA off whatever the scene asks.
    void SetTemporalAAAllowed(bool on);
    void SetShowFlags(const ViewerShowFlags& flags) { showFlags = flags; }
    void SetDebugView(ViewerDebugView view) { debugView = view; }
    // RGBA8 per draw id for LodColoration.
    void SetDrawColors(std::span<const uint32_t> colors) { drawColors.assign(colors.begin(), colors.end()); }
    // xyz and radius of every light, for LightComplexity.
    void SetLightSpheres(std::span<const float> spheres);
    // Draws in place of the sky and lights the GBuffer (GIDSRV); filtered is a mipped
    // equirect of exposure * sky + addBlend * add.
    void LoadSceneIbl(const GameTextureDesc& sky, const GameTextureDesc* add, const GameTextureDesc& filtered,
                      const ViewerSceneIbl& params);
    void LoadAmbientBrdf(const GameTextureDesc& desc);
    // Replaces the IBL irradiance in GIDSRV. tetrahedra: ProbeTetrahedron words; values: 12 per
    // probe; grid: origin xyz, cell size, dims xyz, 0, then a start tetrahedron per cell (x fastest).
    void LoadLightProbes(std::span<const uint32_t> tetrahedra, std::span<const uint32_t> values,
                         std::span<const uint32_t> grid, std::span<const uint8_t> bspTree);
    // The engine's IndirectIllumination compute; with light probes it replaces the ambient pass.
    void CreateIndirectIllumination(const GameComputeDesc& desc);
    // convert is the engine's CubemapTo2DOct; cube i becomes slice i of IndirectIllumination's octahedral
    // array and records[i * 16..] its IBLCubemapArrayList2 element.
    void LoadLocalCubemaps(const GameComputeDesc& convert, std::span<const GameTextureDesc> cubes,
                           std::span<const float> records);
    // FidelityFX CACAO as CACAOImplement::renderCACAO runs it: lowers gb3's occlusion before IndirectIllumination.
    void CreateCacao(const GameCacaoDesc& desc);
    // renderCACAO's per-frame radius and shadow multiplier; enabled = false skips it.
    void SetCacaoFrame(float radius, float shadowMultiplier, bool enabled) {
        cacaoSettings.radius = radius;
        cacaoSettings.shadowMultiplier = shadowMultiplier;
        cacaoEnabled = enabled;
    }
    void SetDirectionalShadow(const ViewerDirectionalShadow& shadow);
    void CreateFog(const GameFogDesc& desc);
    // nullptr: the Fog component is off.
    void SetFogParam(const ViewerFogParam* param);
    void CreateVolumetricFog(const GameVolumetricFogDesc& desc);
    void SetVolumetricFog(const ViewerVolumetricFogControl& control, std::vector<ViewerVolumetricFog> media);
    // LightRenderer's volumetric scattering color (rgb) per LightParameterSRV entry.
    void SetVolumetricScattering(std::span<const float> rgb);
    // Replaces the resolve's tone map in Lit mode once created.
    void CreatePostProcess(const GamePostProcessDesc& desc);
    void SetPostProcess(const ViewerPostProcessParams& params);
    // The color cubes SetPostProcess's colorCubes index.
    void LoadColorCubes(std::span<const GameTextureDesc> cubes);
    // ToneMapping's MultiZoneMeteringTexture: the histogram's weight per screen area.
    void LoadMeteringTexture(const GameTextureDesc& texture);
    void SetSoftBloom(const ViewerSoftBloomParams& params);
    void SetTemporalAA(const ViewerTemporalAA& params);

    // Copies the next presented frame into out as top-down RGBA8.
    void RequestCapture(std::vector<uint8_t>* out) { captureOut = out; }
    // Float copies of the next frame's G-buffer, GID/GIS, depth, lit HDR, fogged HDR and final image.
    void RequestTargetCapture(ViewerTargetCapture* out) { targetCaptureOut = out; }
    // Display-space color replacing the sky, blending top to bottom into bottom when given; nullptr
    // restores the sky.
    void SetBackground(const float* rgb, const float* bottom = nullptr);
    // Lit mode shades this sphere with studio lights (game lighting needs probes a
    // preview lacks). radius 0 turns it off.
    void SetPreviewSphere(const float center[3], float radius);
    // Drawn instead of the scene.
    void SetPreviewTexture(const GameTextureDesc& desc);
    void SetPreviewTextures(const std::vector<GameTextureDesc>& textures);
    void SetPreviewView(const ViewerImageView& view) { previewView = view; }
    void SetPreviewOverlay(std::span<const ViewerOverlayRect> rects);
    // Replaces mip 0; applied on the next RenderFrame.
    void UpdatePreviewTexture(uint32_t texture, const uint8_t* pixels, uint32_t pitch);
    // Frame pixel rect the view's region fills.
    bool PreviewRect(const ViewerImageView& view, float out[4]) const;
    // Mip 0 size; 0 when missing.
    void PreviewTextureSize(uint32_t texture, uint32_t& texWidth, uint32_t& texHeight) const;

private:
    static constexpr UINT FRAME_COUNT = 2;
    // The resolve and ambient passes read b0 from here: resolve at 0, ambient at PASS_CONSTANTS_AMBIENT.
    static constexpr UINT PASS_CONSTANTS_AMBIENT = 512;
    static constexpr UINT PASS_CONSTANTS_BYTES = 1024;
    // The game's G-buffer (RE8 capture): emissive, base color + metallic, normal + roughness, velocity + occlusion.
    static constexpr DXGI_FORMAT GBUFFER_FORMATS[4] = { DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                                                        DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_SNORM };
    static constexpr DXGI_FORMAT HDR_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;

    static constexpr uint32_t SRV_BLUE_NOISE = 0;
    static constexpr uint32_t SRV_GBUFFER0 = 1;
    static constexpr uint32_t SRV_HDR = 5;
    static constexpr uint32_t SRV_SKY = 6;
    static constexpr uint32_t SRV_RESOLVE_DEPTH = 7;
    static constexpr uint32_t SRV_EFFECT = 8;
    static constexpr uint32_t SRV_SELECTION_DEPTH = 9;
    static constexpr uint32_t SRV_PREVIEW = 10;
    // ProbeIndexCache, GID and GIS UAVs.
    static constexpr uint32_t SRV_INDIRECT_UAVS = 74;
    static constexpr uint32_t SRV_INDIRECT_CUBEMAP_ARRAY = 77;
    static constexpr uint32_t SRV_INDIRECT_CUBEMAP = 78;
    static constexpr uint32_t SRV_LIGHT_DEPTH = 100;
    static constexpr uint32_t SRV_LIGHT_VOLUME = 101;
    static constexpr uint32_t SRV_LIGHT_BRDF = 102;
    static constexpr uint32_t SRV_LIGHT_SHADOW = 103;
    static constexpr uint32_t SRV_LIGHT_IES = 104;
    static constexpr uint32_t SRV_LIGHT_BLACK = 105;
    static constexpr uint32_t SRV_LIGHT_GID = 106;
    static constexpr uint32_t SRV_HIZ = 107;
    static constexpr uint32_t SRV_HIZ_MIPS = 108;
    static constexpr uint32_t HIZ_MAX_MIPS = 16;
    // The resolve's second table, t8 on.
    static constexpr uint32_t SRV_HDR_AUX = 124;
    static constexpr uint32_t SRV_SCENE_IBL = 125;
    static constexpr uint32_t SRV_SCENE_IBL_ADD = 126;
    static constexpr uint32_t SRV_SCENE_IBL_FILTERED = 127;
    static constexpr uint32_t SRV_AMBIENT_BRDF = 128;
    static constexpr uint32_t SRV_PROBE_TETRAHEDRA = 129;
    static constexpr uint32_t SRV_PROBE_VALUES = 130;
    static constexpr uint32_t SRV_PROBE_GRID = 131;
    static constexpr uint32_t RESOLVE_EXTRA_SRVS = 8;
    // Histogram and white point state, the resolve's u1/u2 (u0 aliases SV_Target0 in ps_5_0).
    static constexpr uint32_t UAV_EXPOSURE = 132;
    static constexpr uint32_t SRV_LIGHT_GIS = 134;
    static constexpr uint32_t SRV_LIGHT_STATIC_SHADOW = 135;
    // Each texture has a Texture2D view at BINDLESS_BASE + i and a Texture2DArray
    // view at ARRAY_BASE + i; st11_env alone loads ~4.4k textures.
    static constexpr uint32_t SRV_BINDLESS_BASE = 136;
    static constexpr uint32_t SRV_BINDLESS_CAPACITY = 8192;
    static constexpr uint32_t SRV_BINDLESS_ARRAY_BASE = SRV_BINDLESS_BASE + SRV_BINDLESS_CAPACITY;
    static constexpr uint32_t SRV_CACAO_BASE = SRV_BINDLESS_ARRAY_BASE + SRV_BINDLESS_CAPACITY;
    static constexpr uint32_t SRV_CACAO_COUNT = 40;
    static constexpr uint32_t SRV_FOG_BASE = SRV_CACAO_BASE + SRV_CACAO_COUNT;
    enum FogView : uint32_t {
        FOG_VIEW_SHADED_SRV = 0,
        FOG_VIEW_SHADED_UAV = 2,
        FOG_VIEW_VOLUME_SRV = 4,
        FOG_VIEW_VOLUME_UAV,
        FOG_VIEW_TARGET_SRV,
        FOG_VIEW_BLACK_VOLUME,
        FOG_VIEW_COUNT,
    };
    static constexpr uint32_t SRV_FOG_COUNT = FOG_VIEW_COUNT;
    // Null raw and structured buffer views: reads of a buffer the pass does not bind return 0.
    static constexpr uint32_t SRV_NULL_BUFFERS = SRV_FOG_BASE + SRV_FOG_COUNT;
    enum PostView : uint32_t {
        POST_VIEW_SCENE_SRV = 0,
        POST_VIEW_LDR_A_SRV,
        POST_VIEW_LDR_A_UAV,
        POST_VIEW_LDR_B_SRV,
        POST_VIEW_METERING,
        POST_VIEW_TAA_SRV,
        POST_VIEW_HISTORY_SRV,
        POST_VIEW_BLOOM,
        POST_VIEW_CUBES = POST_VIEW_BLOOM + 7,
        POST_VIEW_COUNT = POST_VIEW_CUBES + 32,
    };
    static constexpr uint32_t SRV_POST_BASE = SRV_NULL_BUFFERS + 2;
    // The space7 TextureCube table, same index as the other two; non-cube textures get a white cube.
    static constexpr uint32_t SRV_BINDLESS_CUBE_BASE = SRV_POST_BASE + POST_VIEW_COUNT;
    static constexpr uint32_t UAV_PARTICLE_LIGHT = SRV_BINDLESS_CUBE_BASE + SRV_BINDLESS_CAPACITY;
    static constexpr uint32_t BLURRED_MAX_MIPS = 16;
    static constexpr uint32_t SRV_BLURRED_SOLID = UAV_PARTICLE_LIGHT + 1;
    static constexpr uint32_t SRV_BLURRED_MIPS = SRV_BLURRED_SOLID + 1;
    static constexpr uint32_t SRV_OVERDRAW = SRV_BLURRED_MIPS + BLURRED_MAX_MIPS;
    static constexpr uint32_t SRV_HEAP_SIZE = SRV_OVERDRAW + 1;

    struct GameBindSlot {
        uint8_t kind;
        uint32_t data;
    };

    struct GamePipeline {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        std::vector<GameBindSlot> binds;
        std::vector<uint32_t> rootConstParams;
        bool blends = false;
        bool skipPrepass = false;
        // Built lazily for RAE_SHADOW_INDIRECT: one CONSTANT arg per rootConstParams entry, then DRAW_INDEXED.
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> indirectSignature;
    };

    struct GamePipelineConfig {
        bool lighting;
        UINT renderTargetCount;
        DXGI_FORMAT renderTargetFormat;
        bool depth;
        bool useInputLayout;
        uint32_t gidRegister = 13;
        DXGI_FORMAT renderTarget0Format = DXGI_FORMAT_UNKNOWN;
        bool additiveBlend = false;
        const uint8_t* sdfBlend = nullptr;
        const uint8_t* sdfDepth = nullptr;
        const uint8_t* sdfRaster = nullptr;
        const std::vector<GameBindOverride>* bindOverrides = nullptr;
        const std::vector<GameSamplerOverride>* samplers = nullptr;
        const std::vector<GameLightingSlot>* lightingSlots = nullptr;
        bool wireframe = false;
        bool afterPrepass = false;
        bool gbufferTargets = false;
        bool shadowCast = false;
    };

    void CreateDevice();
    void CreateSwapChain(HWND hwnd);
    void CreateRenderTargets();
    void CreateDepthBuffer();
    void CreateCommandObjects();
    void WaitForGpu();
    void Stamp(uint32_t index);
    void ReadTimestamps();

    void CreateCullPipelines();
    bool EnsureCulling();
    void CullDraws();
    // After the frame's GPU work: the flags the next frame draws with.
    void ReadCullFlags();
    void BuildHiZ();

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBuffer(std::span<const uint8_t> data);
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTexture(const GameTextureDesc& desc, uint32_t arraySize,
                                                         ID3D12GraphicsCommandList* uploadList,
                                                         std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& staging);

    void EnsureGameCommon();
    void EnsureStandalonePasses();
    void CreateGBufferTargets();
    void CreateHdrTargets();
    void UpdateFrustumCull();
    void WriteTonemapConstants();
    void WriteEnvironmentConstants();
    GamePipeline BuildGamePipeline(std::span<const uint8_t> vs, std::span<const uint8_t> ps,
                                   const std::vector<GameInputElement>& inputLayout,
                                   const GamePipelineConfig& config);
    void BindGamePipeline(const GamePipeline& pipeline);
    void CreateResolvePipeline();
    void CreateEffectTarget();
    void CreateParticlePipeline();
    void RenderParticles();
    // depth must be in a shader resource state.
    void DrawResolve(D3D12_CPU_DESCRIPTOR_HANDLE rtv, bool studio, ID3D12PipelineState* pso = nullptr);
    void DrawAmbient();
    void UploadCullingVolume();
    void CreateShadowTargets();
    void UpdateShadowCascades();
    void RenderShadowCascades();
    void RenderShadowCascadeIndirect(const float* m, float half, float depthRange, uint32_t& writeCursor);
    ID3D12CommandSignature* EnsureIndirectSignature(GamePipeline& pipeline);
    void EnsureShadowIndirectBuffer(uint32_t recordsNeeded);
    void CreateCacaoTargets();
    void CreateFogTarget();
    void EnsureFogConstants();
    void CreatePostTargets();
    bool PostProcessActive(bool studio) const;
    // From the composed HDR scene to the back buffer.
    void RenderPostProcess(D3D12_CPU_DESCRIPTOR_HANDLE rtv);
    // Adds the bloom of the composed scene to it.
    void RenderSoftBloom();
    // Scene::updateSceneInfo's TemporalAA jitter (HaltonTbl_2_3_x16) for this frame.
    void BeginFrameJitter();
    // A cut TemporalAA starts from the current scene.
    void PrepareTemporalHistory();
    void RenderTemporalAA();
    void DispatchVolumetricFog();
    // Clears the fog target to no fog, then draws the Fog pass when draw is set.
    void DrawFog(bool draw);
    void WriteCacaoConstants();
    void DispatchCacao();
    void CreateIndirectTargets();
    void WriteProbeLightInfo(uint32_t tetrahedronCount);
    void DispatchIndirectIllumination();
    // GIDSRV/GISSRV: the ambient pass targets once it has something to write.
    void BindIndirectTargets();
    void CreateExposureResources();
    void UpdateExposure();
    bool GameExposureReady() const;
    void UpdateGameExposure(ID3D12Resource* source, uint32_t sourceView);
    void RenderSkyOnly(D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv);
    // Right after DrawResolve, with its constants and states still in place.
    void RecordTargetCapture(D3D12_CPU_DESCRIPTOR_HANDLE rtv);
    void ReadTargetCapture(const std::vector<uint8_t>& finalRgba);
    void CreateDebugPipeline();
    void DrawDebugGeometry();
    // After the resolve; the depth buffer must be readable.
    void DrawOverlay();
    void CreateCollisionPipeline();
    void CreateGizmoPipeline();
    void DrawGizmos();
    void DrawGizmoLines();
    void DrawHandles();
    void DrawIcons();
    void CreateDebugViewPipelines();
    void EnsureDebugViewTargets();
    void GreyAlbedo();
    void DrawDebugView(D3D12_CPU_DESCRIPTOR_HANDLE rtv);
    void CreateTextPipeline();
    void DrawTextOverlay(D3D12_CPU_DESCRIPTOR_HANDLE rtv);
    void EnsureCollisionDepth();
    void DrawCollision(D3D12_CPU_DESCRIPTOR_HANDLE rtv);
    void CreateGridPipeline();
    void DrawGrid();
    void CreatePreviewPipeline();
    void DrawPreviewTexture();
    void CreateSelectionTarget();
    void CreateOutlinePipeline();
    void RenderSelectionMask();
    void DrawSelectionOutline();

    D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle(uint32_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle(uint32_t index) const;

    UINT width;
    UINT height;
    UINT frameIndex = 0;
    UINT rtvStride = 0;
    uint32_t srvStride = 0;
    float frustumPlanes[6][4]{};
    std::vector<uint8_t> drawVisible;
    std::vector<uint8_t> drawMask;

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> renderTargets[FRAME_COUNT];
    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    uint64_t fenceValue = 0;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestampHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestampReadback;
    uint32_t stampsWritten = 0;
    uint32_t stampsMeasured = 0;
    uint64_t timestampFrequency = 0;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debugMessages;
    ViewerFrameTimings timings;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> cullRoot;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> hizRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> cullPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> hizPso;
    Microsoft::WRL::ComPtr<ID3D12Resource> cullConstants;
    Microsoft::WRL::ComPtr<ID3D12Resource> cullSpheres;
    Microsoft::WRL::ComPtr<ID3D12Resource> cullEnabled;
    Microsoft::WRL::ComPtr<ID3D12Resource> cullFlags;
    Microsoft::WRL::ComPtr<ID3D12Resource> cullFlagsReadback;
    Microsoft::WRL::ComPtr<ID3D12Resource> hizTexture;
    uint8_t* cullConstantsMapped = nullptr;
    uint8_t* cullEnabledMapped = nullptr;
    bool cullDirty = true;
    bool cullMaskDirty = true;
    bool cullReadbackReady = false;
    uint32_t cullDrawCount = 0;
    uint32_t hizWidth = 0;
    uint32_t hizHeight = 0;
    uint32_t hizMips = 0;
    std::vector<uint32_t> cullFlagsCpu;

    Microsoft::WRL::ComPtr<ID3D12Resource> positionBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> normalBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> uv0Buffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> uv1Buffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> weightsBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW positionView{};
    D3D12_VERTEX_BUFFER_VIEW normalView{};
    D3D12_VERTEX_BUFFER_VIEW uv0View{};
    D3D12_VERTEX_BUFFER_VIEW uv1View{};
    D3D12_VERTEX_BUFFER_VIEW weightsView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
    std::vector<ViewerMeshDraw> draws;
    bool hasMesh = false;

    float camView[16]{};
    float camProj[16]{};
    float camEye[3]{};
    float camNear = 0.01f;
    float camFar = 100.0f;
    float camFov = 1.0472f;
    float projectionJitter[2]{};
    float meshCenter[3]{};
    float meshRadius = 1.0f;

    GamePipeline prepassPipeline;
    std::vector<GamePipeline> prepassPipelines;
    std::vector<GamePipeline> deferredPipelines;
    struct ForwardBind {
        enum Kind : uint8_t { Cbv, RootSrv, Table, Texture, Uav, Constant } kind = Cbv;
        std::string name;
        uint32_t data = 0;
    };
    struct ForwardPipeline {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        std::vector<ForwardBind> binds;
        std::vector<uint32_t> rootConstParams;
    };
    // By deferredPipelines index; set for the Forward* materials, whose GBuffer pipeline is empty.
    std::vector<ForwardPipeline> forwardPipelines;
    ForwardPipeline BuildForwardPipeline(const GameDeferredDesc& desc);
    void DrawForward(bool culled, uint32_t& drawn, uint64_t& triangles);
    // Solid::mBlurredSrvPtr: the lit scene with a blurred mip chain, the forward programs' BlurredSolid.
    Microsoft::WRL::ComPtr<ID3D12Resource> blurredSolid;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> blurredRtvHeap;
    uint32_t blurredMips = 0;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> blurRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blurPso;
    void CreateBlurredSolid();
    void CreateBlurPipeline();
    void BuildBlurredSolid();
    std::vector<GamePipeline> wireframePipelines;
    std::vector<GamePipeline> selectionPipelines;
    GamePipeline lightingPipeline;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> resolveRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> resolvePso;
    // Shares the resolve root signature; writes GIDSRV.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ambientPso;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneInfoBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> gbufferTypeBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> checkerBoardBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> tonemapBuffer;
    uint8_t* tonemapMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> environmentBuffer;
    uint8_t* environmentMapped = nullptr;
    ViewerEnvironment environment;
    uint64_t frameCounter = 0;
    uint64_t clockStartMs = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> instanceBuffer;
    uint32_t instanceStride = 0;
    std::size_t instanceCount = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> skinningBuffer;
    uint8_t* skinningMapped = nullptr;
    std::size_t skinningCapacity = 0;  // in matrices
    Microsoft::WRL::ComPtr<ID3D12Resource> zeroBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> scratchUavBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> bindlessDataBuffer;
    std::vector<uint32_t> materialRecordOffsets;
    std::vector<uint32_t> materialParamSizes;
    Microsoft::WRL::ComPtr<ID3D12Resource> redirectBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> blueNoise;
    Microsoft::WRL::ComPtr<ID3D12Resource> whiteCube;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Heap>> materialHeaps;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> materialTextures;
    uint8_t* sceneInfoMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> gbuffer[4];
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> gbufferRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> hdrTarget;
    Microsoft::WRL::ComPtr<ID3D12Resource> hdrAuxTarget;
    Microsoft::WRL::ComPtr<ID3D12Resource> gidTarget;
    Microsoft::WRL::ComPtr<ID3D12Resource> gisTarget;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> hdrRtvHeap;
    UINT lightingRtCount = 1;
    Microsoft::WRL::ComPtr<ID3D12Resource> lightInfoBuffer;
    uint8_t* lightInfoMapped = nullptr;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> lightingDummies;
    Microsoft::WRL::ComPtr<ID3D12Resource> lightParamsBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> lightListBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> lightVolumeTexture;
    uint32_t lightCount = 0;
    uint32_t cameraLightFirst = 0;
    std::vector<GameLightParam> cameraLights;
    void UpdateCameraLights();

    // Premultiplied RGBA16F effect target (a = remaining scene transmittance);
    // the resolve applies scene * a + rgb.
    static constexpr uint32_t PARTICLE_CAPACITY = 65536;
    Microsoft::WRL::ComPtr<ID3D12Resource> effectTarget;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> effectRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> particleRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> particlePso;
    Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer;
    uint8_t* particleMapped = nullptr;
    uint32_t particleCount = 0;
    float particleIntensity = 1.0f;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> debugRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> debugTrianglePso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> debugLinePso;
    Microsoft::WRL::ComPtr<ID3D12Resource> debugBuffer;
    uint32_t debugTriangleVertices = 0;
    uint32_t debugLineVertices = 0;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> overlayLinePso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> overlayTrianglePso;
    Microsoft::WRL::ComPtr<ID3D12Resource> overlayGeometryBuffer;
    uint8_t* overlayGeometryMapped = nullptr;
    std::size_t overlayGeometryCapacity = 0;
    uint32_t overlayTriangleVertices = 0;
    uint32_t overlayLineVertices = 0;
    float overlayOccluded = 1.0f;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> collisionRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> collisionSolidPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> collisionVolumePso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> collisionEdgePso;
    Microsoft::WRL::ComPtr<ID3D12Resource> collisionDepth;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> collisionDsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> collisionPositionBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> collisionIndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW collisionPositionView{};
    D3D12_INDEX_BUFFER_VIEW collisionIndexView{};
    Microsoft::WRL::ComPtr<ID3D12Resource> collisionInstanceBuffer;
    uint8_t* collisionInstanceMapped = nullptr;
    std::size_t collisionInstanceCapacity = 0;  // in bytes
    std::vector<ViewerCollisionBatch> collisionBatches;
    bool collisionView = false;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> iconRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> iconPso;
    Microsoft::WRL::ComPtr<ID3D12Resource> iconBuffer;
    uint8_t* iconMapped = nullptr;
    std::size_t iconCapacity = 0;  // in bytes
    uint32_t iconCount = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> gizmoBuffer;
    uint8_t* gizmoMapped = nullptr;
    std::size_t gizmoCapacity = 0;  // in bytes
    uint32_t gizmoLineVertices = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> handleBuffer;
    uint8_t* handleMapped = nullptr;
    std::size_t handleCapacity = 0;  // in bytes
    uint32_t handleVertices = 0;
    std::vector<uint32_t> drawIndexById;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> textRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> textPso;
    struct TextSlot {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        uint8_t* mapped = nullptr;
        std::size_t capacity = 0;  // in bytes
        uint32_t size[2]{};
        int32_t position[2]{};
    };
    TextSlot textSlots[TEXT_SLOTS];

    Microsoft::WRL::ComPtr<ID3D12RootSignature> gridRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gridPso;
    bool gridEnabled = true;
    ViewerToneMap toneMapSource;
    bool exposureOverride = false;
    float exposureOverrideEv = 0;
    ViewerTemporalAA temporalAASource;
    bool temporalAAAllowed = true;
    ViewerShowFlags showFlags;
    ViewerFogParam fogParamCopy{};
    ViewerDebugView debugView = ViewerDebugView::None;
    std::vector<uint32_t> drawColors;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> debugViewRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> lightingOnlyPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> flatColorPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> overdrawPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> heatPso;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> lightComplexityPso;
    Microsoft::WRL::ComPtr<ID3D12Resource> debugViewDepth;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> debugViewDsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> overdrawTarget;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> overdrawRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> lightSphereBuffer;
    uint8_t* lightSphereMapped = nullptr;
    std::size_t lightSphereCapacity = 0;
    uint32_t lightSphereCount = 0;
    bool selectionOutlineEnabled = true;

    std::vector<uint8_t> selectionMask;
    bool hasSelection = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> selectionDepth;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> selectionDsvHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> outlineRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> outlinePso;

    Microsoft::WRL::ComPtr<ID3D12Resource> skyTexture;
    uint32_t skyMips = 1;
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneIblTextures[3];
    Microsoft::WRL::ComPtr<ID3D12Resource> ambientBrdf;
    Microsoft::WRL::ComPtr<ID3D12Resource> probeBuffers[3];
    Microsoft::WRL::ComPtr<ID3D12Resource> probeBspTree;
    uint32_t probeTetrahedronCount = 0;
    struct IndirectBind {
        enum Kind : uint8_t { Cbv, BufferSrv, TextureSrv, Uav, NullBuffer } kind;
        GameComputeResource resource;
        std::string name;
        uint32_t nullView = 0;
    };
    std::vector<IndirectBind> indirectBinds;
    // bound: whether the pass binds a buffer SRV slot; the others read a null view (no root descriptor, which has no
    // bounds). Without it every buffer SRV is a root descriptor.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateComputeRoot(const GameComputeDesc& desc, std::vector<IndirectBind>& binds,
                                                                  bool (*bound)(const GameComputeSlot*) = nullptr);
    Microsoft::WRL::ComPtr<ID3D12RootSignature> indirectRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> indirectPso;
    Microsoft::WRL::ComPtr<ID3D12Resource> indirectCubemaps[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> localCubemapArray;
    Microsoft::WRL::ComPtr<ID3D12Resource> localCubemapRecords;
    uint32_t localCubemapCount = 0;
    enum CacaoProgramId : uint32_t {
        CACAO_PREPARE_DEPTHS,
        CACAO_PREPARE_NORMALS,
        CACAO_GENERATE_BASE,
        CACAO_GENERATE,
        CACAO_IMPORTANCE_MAP,
        CACAO_IMPORTANCE_A,
        CACAO_IMPORTANCE_B,
        CACAO_BLUR,
        CACAO_UPSCALE,
        CACAO_PROGRAM_COUNT,
    };
    struct CacaoProgram {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        std::vector<IndirectBind> binds;
    };
    CacaoProgram cacaoPrograms[CACAO_PROGRAM_COUNT];
    GameCacaoSettings cacaoSettings{};
    bool hasCacao = false;
    bool cacaoEnabled = true;
    uint32_t cacaoSsaoWidth = 0;
    uint32_t cacaoSsaoHeight = 0;
    uint32_t cacaoImportanceWidth = 0;
    uint32_t cacaoImportanceHeight = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> cacaoDepths;
    Microsoft::WRL::ComPtr<ID3D12Resource> cacaoNormals;
    Microsoft::WRL::ComPtr<ID3D12Resource> cacaoPing;
    Microsoft::WRL::ComPtr<ID3D12Resource> cacaoPong;
    Microsoft::WRL::ComPtr<ID3D12Resource> cacaoImportance;
    Microsoft::WRL::ComPtr<ID3D12Resource> cacaoImportancePong;
    Microsoft::WRL::ComPtr<ID3D12Resource> cacaoLoadCounter;
    Microsoft::WRL::ComPtr<ID3D12Resource> cacaoOcclusionCopy;
    Microsoft::WRL::ComPtr<ID3D12Resource> cacaoConstants;
    uint8_t* cacaoConstantsMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cacaoClearHeap;
    // LightRenderer's ShadowMap array (its ShadowResolution in the RE8 capture); the directional cascades
    // start at slice 0.
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
    static constexpr uint32_t SHADOW_CASCADES = 4;
    std::vector<GamePipeline> shadowPipelines;
    ViewerDirectionalShadow directionalShadow;
    bool shadowCascadesValid = false;
    float cascadeView[SHADOW_CASCADES][16] = {};
    float cascadeHalfSize[SHADOW_CASCADES] = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowMap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadowDsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowConstants;
    uint8_t* shadowConstantsMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowRotationBuffer;
    uint8_t* shadowRotationMapped = nullptr;
    // RAE_SHADOW_INDIRECT: one persistently-mapped upload buffer of {rootConstant, D3D12_DRAW_INDEXED_ARGUMENTS}
    // records, rewritten every frame. Safe because RenderFrame() fully waits on the GPU before the next frame
    // touches it (see WaitForGpu() at the end of RenderFrame): no in-flight frame ever reads a buffer another
    // frame is writing.
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowIndirectBuffer;
    uint8_t* shadowIndirectMapped = nullptr;
    uint32_t shadowIndirectCapacity = 0;
    D3D12_GPU_VIRTUAL_ADDRESS sceneInfoOverride = 0;
    D3D12_GPU_VIRTUAL_ADDRESS shadowCastAddress = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> probeIndexCache;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> uavClearHeap;
    bool probeCacheCleared = false;
    bool hasLightProbes = false;
    bool hasSceneIbl = false;
    ViewerSceneIbl sceneIbl;
    uint32_t sceneIblFilteredMips = 1;
    ViewerToneMap toneMap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> fogRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> fogPso;
    std::vector<IndirectBind> fogBinds;
    bool fogEnabled = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> fogTarget;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> fogRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> fogBlackVolume;
    // FrustumVolume, VolumetricFogControlParams, GlobalVolumetricFog and FogParam, 256 B apart.
    Microsoft::WRL::ComPtr<ID3D12Resource> fogConstants;
    uint8_t* fogConstantsMapped = nullptr;
    CacaoProgram injectProgram;
    CacaoProgram integrateProgram;
    ViewerVolumetricFogControl volumetricControl;
    std::vector<ViewerVolumetricFog> volumetricMedia;
    bool volumetricHistory = false;
    bool volumetricRunning = false;
    uint32_t volumetricFrame = 0;
    uint32_t integrateGroup = 4;
    uint32_t volumetricDepth = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> shadedFog[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> volumetricFogTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> volumetricFogList;
    uint8_t* volumetricFogListMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> scatteringLightList;
    uint8_t* scatteringLightListMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> blueNoiseTables[3];
    struct PassBind {
        enum Kind : uint8_t { Cbv, RootSrv, Table } kind;
        std::string name;
    };
    struct PostPass {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        std::vector<PassBind> binds;
        // cbSoftBloom is SoftBloomImplement's output buffer rather than the reduction one.
        bool bloomOutput = false;
    };
    struct PassStage {
        const GameComputeDesc* desc;
        D3D12_SHADER_VISIBILITY visibility;
    };
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreatePassRoot(std::span<const PassStage> stages, std::vector<PassBind>& binds,
                                                               bool inputLayout = false);
    struct MaterialPass {
        ViewerMaterialProgram program;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        std::vector<PassBind> binds;
    };
    std::vector<MaterialPass> materialPasses;
    std::vector<ViewerMaterialBatch> materialBatches;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialVertexBuffer;
    uint8_t* materialVertexMapped = nullptr;
    std::size_t materialVertexCapacity = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialConstantBuffer;
    uint8_t* materialConstantMapped = nullptr;
    std::size_t materialConstantCapacity = 0;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> particleLightRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> particleLightPso;
    std::vector<IndirectBind> particleLightBinds;
    Microsoft::WRL::ComPtr<ID3D12Resource> particleLightInput;
    uint8_t* particleLightInputMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> particleLightOutput;
    std::size_t particleLightCapacity = 0;
    std::size_t particleLightCount = 0;
    void DispatchParticleLighting();
    // FogParam when the scene has fog, else zeros.
    D3D12_GPU_VIRTUAL_ADDRESS FogParamAddress() const;
    void RenderMaterialParticles(D3D12_CPU_DESCRIPTOR_HANDLE rtv, bool lighting);
    // Binds the post-process resources by name; textures the pass does not name read sourceView.
    void BindPostPass(const PostPass& pass, bool compute, uint32_t sourceView);
    void DrawPostPass(const PostPass& pass, D3D12_CPU_DESCRIPTOR_HANDLE target, uint32_t sourceView, uint32_t targetWidth,
                      uint32_t targetHeight);
    D3D12_CPU_DESCRIPTOR_HANDLE PostRtv(uint32_t index) const;
    PostPass temporalPass;
    ViewerTemporalAA temporalAA;
    Microsoft::WRL::ComPtr<ID3D12Resource> taaTarget;
    Microsoft::WRL::ComPtr<ID3D12Resource> historyTarget;
    bool temporalFrame = false;
    bool temporalCut = true;
    bool temporalVelocityCut = false;
    uint32_t temporalAccumulated = 0;
    uint32_t jitterIndex = 0;
    float explicitJitter[2]{};
    bool hasExplicitJitter = false;
    float frameViewProjection[16]{};
    float prevViewProjection[16]{};
    bool hasPrevViewProjection = false;
    PostPass bloomReductionPass;
    PostPass bloomFilterPass;
    PostPass bloomBlendingPass[7];
    PostPass bloomFinalPass;
    Microsoft::WRL::ComPtr<ID3D12Resource> bloomTargets[7];
    uint32_t bloomLevels = 0;
    PostPass histogramPass;
    PostPass whitePointPass;
    PostPass ldrPass;
    PostPass fxaaPass;
    PostPass casPass;
    PostPass outputPass;
    bool hasPostProcess = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> postConstants;
    uint8_t* postConstantsMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneHdrTarget;
    Microsoft::WRL::ComPtr<ID3D12Resource> ldrTargets[2];
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> postRtvHeap;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> colorCubes;
    uint32_t colorCubeSlots[3] = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> meteringTexture;
    uint32_t exposureFrame = 0;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> resolveSceneHdrPso;
    Microsoft::WRL::ComPtr<ID3D12Resource> passConstants;
    uint8_t* passConstantsMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> exposureHistogram;
    Microsoft::WRL::ComPtr<ID3D12Resource> exposureState;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> exposureRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> exposurePso;
    bool exposureReset = true;
    float skyIntensity = 1.0f;
    float skyRotation = 0;
    float skyBlur = 0;
    float skyEye[3]{};
    float skyViewProjInv[16]{};

    float clearColor[4] = { 0.05f, 0.05f, 0.08f, 1.0f };
    bool unlit = false;
    GBufferView gbufferView = GBufferView::None;
    bool wireframe = false;

    std::vector<uint8_t>* captureOut = nullptr;
    struct TargetCopy {
        std::string name;
        DXGI_FORMAT format;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    };
    ViewerTargetCapture* targetCaptureOut = nullptr;
    std::vector<TargetCopy> targetCopies;
    std::vector<uint8_t> targetFinal;
    Microsoft::WRL::ComPtr<ID3D12Resource> targetReadback;
    Microsoft::WRL::ComPtr<ID3D12Resource> targetHdr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> targetRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> resolveHdrPso;
    float background[3] = { -1, -1, -1 };
    float backgroundBottom[3] = { -1, -1, -1 };
    float previewSphere[4] = { 0, 0, 0, 0 };

    struct PreviewTexture {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint32_t flags = 0;
        uint32_t size[2]{};
        uint32_t mips = 1;
        uint32_t slices = 1;
    };
    static constexpr uint32_t PREVIEW_CAPACITY = 64;
    static constexpr uint32_t OVERLAY_CAPACITY = 4096;
    std::vector<PreviewTexture> previewTextures;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> previewRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> previewPso;
    ViewerImageView previewView;
    Microsoft::WRL::ComPtr<ID3D12Resource> overlayBuffer;
    ViewerOverlayRect* overlayMapped = nullptr;
    uint32_t overlayCount = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> previewUpload;
    uint8_t* previewUploadMapped = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT previewUploadFootprint{};
    uint32_t previewUploadTarget = 0;
    bool previewUploadPending = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> screenshotBuffer;
};

#endif
