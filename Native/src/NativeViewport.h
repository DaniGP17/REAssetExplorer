#ifndef REASSETEXPLORER_NATIVEVIEWPORT_H
#define REASSETEXPLORER_NATIVEVIEWPORT_H
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <condition_variable>
#include <functional>
#include <map>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#include "Explorer/EffectSimulator.h"
#include "Explorer/PostProcessZones.h"
#include "Explorer/SceneBuilder.h"
#include "Explorer/SceneUpload.h"
#include "Explorer/SkeletalAnimator.h"
#include "NativeMovie.h"
#include "REAssetNative.h"
#include "Renderer/Camera.h"
#include "Renderer/Viewer.h"
#include "TransformGizmo.h"

enum class ViewportShading : int32_t {
    Lit,
    Unlit,
    Wireframe,
    Collision,
    AiMap,
    Normals,
    Roughness,
    Metallic,
    Occlusion,
    BaseColor,
    Emissive,
    Depth,
    Velocity,
    LightingOnly,
    LodColoration,
    LightComplexity,
    Overdraw
};

// rae_viewport_set_show_flags bits.
enum ViewportShow : uint32_t {
    SHOW_STATIC_MESHES = 1,
    SHOW_SKINNED_MESHES = 2,
    SHOW_DECALS = 4,
    SHOW_EFFECTS = 8,
    SHOW_FOG = 16,
    SHOW_VOLUMETRIC_FOG = 32,
    SHOW_SHADOWS = 64,
    SHOW_POST_PROCESS = 128,
    SHOW_SKY = 256,
    SHOW_LOCAL_CUBEMAPS = 512,
    SHOW_ALL = 1023
};

// Perspective, then the orthographic views Unreal offers.
enum class ViewportView : int32_t { Perspective, Top, Bottom, Left, Right, Front, Back };

struct ViewportCollisionGroup {
    std::string name;
    uint32_t colliders = 0;
    uint8_t color[3]{};
    bool volume = false;  // drawn see-through (triggers, sensors)
};

struct ViewportAiMapGroup {
    std::string name;
    std::string type;
    uint32_t nodes = 0;
    uint8_t color[3]{};
};

struct ViewportImage {
    uint32_t texture = 0;
    bool atlas = false;            // .uvs
    UvsData uvs;
    std::vector<uint32_t> pages;   // uvs texture index -> preview texture
    std::string movie;             // played into preview texture 0
};

struct ViewportScene {
    const LoadedGame* game = nullptr;
    SceneBuild build;
    TextureCache textures;
    SceneLook look;
    bool meshAsset = false;
    // Instance whose LODs and submeshes are shown; other character parts show LOD0.
    uint32_t mainInstance = 0;
    uint32_t lodCount = 1;
    bool keepCamera = false;
    bool materialPreview = false;
    bool frameWhole = false;
    std::optional<TerrainData> collision;
    std::optional<AiMapData> aiMap;
    std::optional<LoadedEffect> effect;
    std::optional<ViewportImage> image;
};

// The window lives on the creating (UI) thread; the Viewer on a render thread that also uploads.
// Without a host the window is parked under a hidden window, keeping the device and the scene.
class NativeViewport {
public:
    explicit NativeViewport(HWND parent);
    ~NativeViewport();

    NativeViewport(const NativeViewport&) = delete;
    NativeViewport& operator=(const NativeViewport&) = delete;

    HWND Hwnd() const { return hwnd; }
    void SetParentWindow(HWND parent);
    // Blocks until the render thread has uploaded the scene; rethrows its errors.
    void Load(std::unique_ptr<ViewportScene> scene);
    void Clear();
    void SetSky(std::shared_ptr<const TextureData> texture, float intensity);
    void SetBackground(const RaeViewportBackground& value);
    void SetSkeletonStyle(const RaeSkeletonStyle& value);
    void SetShading(ViewportShading value) { shading = value; }
    void SetGrid(bool on) { grid = on; }
    // GIZMOS_ENABLED shows gizmos at all (grid included); bit (1 << SceneLightKind) that kind of light icon,
    // GIZMO_SELECTION the selection outline.
    static constexpr uint32_t GIZMOS_ENABLED = 0x80000000u;
    static constexpr uint32_t GIZMO_SELECTION = 8;
    void SetGizmos(uint32_t mask) { gizmoMask = mask; }
    void SetTransformTool(GizmoMode mode, bool local) {
        gizmoMode = static_cast<int32_t>(mode);
        gizmoLocal = local;
    }
    GizmoMode TransformMode() const { return static_cast<GizmoMode>(gizmoMode.load()); }
    bool TransformLocal() const { return gizmoLocal.load(); }
    void SetSnap(const GizmoSnap& value) {
        std::lock_guard lock(mutex);
        snap = value;
    }
    // Objects the gizmo moved since the last call, a line each: key, then position, rotation (XYZ euler
    // degrees) and scale as "x,y,z", tab-separated.
    std::string TakeTransformChanges();
    void SetShowFlags(uint32_t mask) { showMask = mask; }
    // forced -1 lets the distance pick the LOD; streamAll draws every zone and room whatever the camera.
    void SetLodOverride(int32_t forced, bool streamAll) {
        forcedLod = forced;
        streamAllZones = streamAll;
    }
    void SetView(ViewportView view) { viewRequest = static_cast<int32_t>(view); }
    ViewportView View() const { return static_cast<ViewportView>(viewShown.load()); }
    void SetCameraSpeed(float speed) { speedRequest = speed; }
    float CameraSpeed() const { return speedShown.load(); }
    void SetCameraFov(float radians) { fovRequest = radians; }
    // Ctrl+digit saves, digit recalls; the mask has bit i for a saved slot i.
    void Bookmark(int32_t slot, bool save);
    uint32_t BookmarkMask() const { return bookmarkMask.load(); }
    // Off draws a frame only when something changed.
    void SetRealtime(bool on) { realtime = on; }
    // The frame is rendered at this fraction of the window and stretched to it.
    void SetRenderScale(float scale) { renderScale = scale; }
    void SetTemporalAA(bool on) { temporalAAOn = on; }
    void SetExposure(bool fixed, float ev) {
        exposureFixed = fixed;
        exposureEv = ev;
    }
    void Undo() { undoRequests++; }
    void Redo() { redoRequests++; }
    // Bit 0 something to undo, bit 1 to redo.
    uint32_t UndoState() const { return undoState.load(); }
    void SnapSelectionToFloor() { floorRequests++; }
    void SetMeasuring(bool on) { measuring = on; }
    bool Measuring() const { return measuring.load(); }
    void SetStatsOverlay(bool on) { statsOverlay = on; }
    void SetProjectionJitter(float x, float y) {
        std::lock_guard lock(mutex);
        jitter[0] = x;
        jitter[1] = y;
    }
    void SetEnvironment(const ViewerEnvironment& value) {
        std::lock_guard lock(mutex);
        environment = value;
    }
    void ResetCamera() { resetCamera = true; }
    CameraPose GetCameraPose();
    // Applied on the next frame.
    void SetCameraPose(const CameraPose& pose);
    // Blocks until a frame begun after the call is presented; top-down RGBA8.
    bool CaptureFrame(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height, uint32_t timeoutMs);
    bool CaptureTargets(ViewerTargetCapture& out, uint32_t timeoutMs);
    std::string PickedDraw();
    RaeViewportStats Stats();
    void Select(int32_t lod, int32_t submesh);
    void SelectObjects(std::vector<std::string> keys);
    void SetHiddenObjects(std::vector<std::string> keys);
    void SetCollisionGroups(std::vector<std::string> names);
    std::vector<ViewportCollisionGroup> CollisionGroups();
    void SetAiMapGroups(std::vector<std::string> names);
    std::vector<ViewportAiMapGroup> AiMapGroups();
    // The loaded light state first, then every state the scene offers.
    std::vector<std::string> LightStates();
    void SetMaterialParam(std::string key, std::vector<float> values);
    uint32_t Selection(int32_t& lod, int32_t& submesh);
    std::string SelectedObject();
    void SetEffectPaused(bool paused) { effectPaused = paused; }
    void RestartEffect() { effectRestart = true; }
    void SetEffectSpeed(float speed) { effectSpeed = speed; }
    bool EffectStatus(float& time, uint32_t& particles);
    void SetChannels(uint32_t mask) { channelMask = mask; }
    void SetFlipbook(bool playing, float fps);
    bool ImageStatus(float& zoom, int32_t& pattern);
    void SetSkeletons(bool on) {
        skeletons = on;
        skeletonsDirty = true;
    }
    void PlayMotion(std::shared_ptr<const MotlistData> motlist, std::size_t motion);
    void SetFollow(bool on) {
        followCharacter = on;
        if (on) followRecenter = true;
    }
    bool Following() const { return followCharacter.load(); }
    void SetMotionPaused(bool paused) { motionPauseRequest = paused ? 1 : 0; }
    void SetMotionSpeed(float speed) { motionSpeed = speed; }
    void SetMotionLoop(bool loop) { motionLoop = loop; }
    void SeekMotion(float frame) { motionSeekRequest = std::max(frame, 0.0f); }
    bool MotionStatus(float& frame, float& frameCount, float& frameRate, bool& paused, uint32_t& drivenJoints);
    std::shared_ptr<const MotlistData> CachedMotlist(const std::string& path);
    void CacheMotlist(const std::string& path, std::shared_ptr<const MotlistData> motlist);
    void SelectJoint(int32_t joint) {
        requestedJoint = joint;
        skeletonsDirty = true;
    }
    bool TakeHideRequest() { return hideRequests.exchange(0) > 0; }
    // Presses of G since the last call: the UI owns the gizmo toggle.
    uint32_t TakeGizmoToggles() { return gizmoToggles.exchange(0); }
    void SetMoviePaused(bool paused) { moviePauseRequest = paused ? 1 : 0; }
    void SeekMovie(float seconds) { movieSeekRequest = std::max(seconds, 0.0f); }
    bool MovieStatus(float& position, float& duration, bool& paused);

private:
    struct Job {
        std::unique_ptr<ViewportScene> scene;  // null clears the viewport
        std::promise<void> done;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // CPU copy of the uploaded geometry for picking; draws[i].id == i.
    struct PickGeometry {
        std::vector<uint8_t> positions;
        std::vector<uint8_t> indices;
        std::vector<ViewerMeshDraw> draws;
        std::vector<SceneDrawTag> tags;
        std::vector<ViewerInstanceWorld> worlds;
        std::vector<std::string> owners;
        std::vector<std::string> meshes;
        std::vector<std::vector<uint32_t>> instanceMaterials;
        std::vector<std::string> materialNames;
        std::unordered_map<std::string, std::vector<uint32_t>> instancesByOwner;
        std::unordered_multimap<std::string, SceneMaterialParam> params;
    };

    void RenderLoop();
    void RunJob(Job& job);
    // withSky false when a scene upload follows.
    void CreateBlankViewer(bool withSky = true);
    void ResetEmptyCamera();
    void StepEffect(float dt, const float eye[3]);
    void StepSceneEffects(float dt, const float eye[3]);
    void UpdateSkeletonOverlay(const float eye[3]);
    void UpdateCollisionOverlay(float boundsMin[3], float boundsMax[3]);
    void UpdateAiMapOverlay(float boundsMin[3], float boundsMax[3]);
    void SetSceneAiMaps(std::vector<SceneAiMap> maps);
    void StepMotion(float dt);
    void FollowCharacter(float dt);
    // Consumes the frame's reset, select and pick requests.
    void StepImage(const CameraInput& input, float panX, float panY, int cursorX, int cursorY, float dt,
                   bool& selectNow, int32_t selectLod, int32_t selectSubmesh, bool& pickNow, int pickAtX, int pickAtY);
    int32_t PatternAt(int x, int y) const;
    bool PickRay(int x, int y, float eye[3], float dir[3]) const;
    // Pixel position and view depth; false behind the camera.
    bool ProjectToScreen(const float point[3], float& x, float& y, float& depth) const;
    bool LightIconShown(const SceneLightMarker& light, uint32_t mask) const;
    int32_t PickLight(int x, int y, bool meshesShown) const;
    void UpdateLightGizmos(uint32_t mask, const float eye[3]);
    void UpdateStatsOverlay(bool statsChanged);
    GizmoView MakeGizmoView() const;
    int32_t ActiveObject() const;
    // Selected objects without a selected ancestor.
    std::vector<int32_t> SelectionRoots() const;
    Mat4 ParentWorld(int32_t object) const;
    // Moves the object and everything it and its children own: instances, colliders, lights, skeletons.
    void MoveObject(int32_t object, const Mat4& world);
    void ApplyObjectDelta(int32_t object, const Mat4& delta, float grow, std::vector<ViewerDrawBounds>& bounds);
    void SetObjectTransformField(const std::string& key, const std::string& field, const std::vector<float>& values);
    std::string LocalTransformText(int32_t object) const;
    void UpdateTransformGizmo();
    // Static, skinned or decal, per draw id, for the Show flags.
    void ClassifyDraws();
    bool DrawAllowed(std::size_t id) const;
    // The Show menu's forced LOD for an instance with LODs, -1 otherwise.
    int32_t ForcedInstanceLod(uint32_t instance) const;
    void UpdateViewCamera(const CameraInput& input, float panX, float panY, float dt);
    void EnterView(ViewportView view);
    void ApplyBookmarks();
    // Nearest mesh hit along the ray, skipping the given instances; FLT_MAX when none.
    float RaycastMeshes(const float origin[3], const float dir[3], const std::vector<uint8_t>* skipInstances) const;
    void SnapToFloor();
    void RecordUndo(const std::vector<int32_t>& targets, const std::vector<Mat4>& before, bool coalesce);
    void ApplyUndo(bool redo);
    void UpdateMeasure();
    void ApplyViewerSettings();
    // Lights of hidden GameObjects go dark; shown again they come back.
    void UpdateLightVisibility();
    void UploadLight(std::size_t index);
    void ApplyDirectionalLight();
    void RefreshSelectionBounds();
    bool LightsBounds(const std::set<std::string>& owners, float center[3], float& radius) const;
    // distance: along the pick ray, FLT_MAX without a hit.
    int32_t PickDraw(int x, int y, float* distance = nullptr) const;
    void BuildRoomGround();
    std::string PickCollider(int x, int y) const;
    void UpdateCollisionInstances();
    bool CollidersBounds(const std::set<std::string>& owners, float center[3], float& radius) const;
    void ShowLod(int32_t lod);
    void UpdateSceneLods(const float eye[3]);
    void UpdateCurrentRoom(const float eye[3]);
    void UpdateMeshLods(const float eye[3], const float target[3]);
    void UpdateDrawMask();
    std::vector<uint32_t> SubmeshDraws(int32_t submesh) const;
    std::vector<uint32_t> ObjectDraws(const std::vector<std::string>& keys) const;
    void SetSelection(const std::vector<uint32_t>& ids, int32_t submesh, std::string object);
    bool DrawsBounds(const std::vector<uint32_t>& ids, float center[3], float& radius) const;

    HWND hwnd = nullptr;
    std::thread renderThread;
    std::atomic<bool> running{ true };
    std::atomic<bool> finished{ false };
    std::atomic<ViewportShading> shading{ ViewportShading::Unlit };
    std::atomic<bool> grid{ true };
    std::atomic<uint32_t> gizmoMask{ GIZMOS_ENABLED | GIZMO_SELECTION | 7 };
    std::atomic<int32_t> gizmoMode{ 0 };
    std::atomic<bool> gizmoLocal{ false };
    std::atomic<int32_t> gizmoHover{ 0 };
    std::atomic<bool> statsOverlay{ false };
    std::atomic<uint32_t> showMask{ SHOW_ALL };
    std::atomic<int32_t> forcedLod{ -1 };
    std::atomic<bool> streamAllZones{ false };
    std::atomic<int32_t> viewRequest{ -1 };
    std::atomic<int32_t> viewShown{ 0 };
    std::atomic<float> speedRequest{ -1 };
    std::atomic<float> speedShown{ 1 };
    std::atomic<float> fovRequest{ -1 };
    std::atomic<uint32_t> bookmarkMask{ 0 };
    std::atomic<bool> realtime{ true };
    std::atomic<uint32_t> inputSerial{ 0 };
    std::atomic<float> renderScale{ 1 };
    std::atomic<bool> temporalAAOn{ true };
    std::atomic<bool> exposureFixed{ false };
    std::atomic<float> exposureEv{ 0 };
    std::atomic<uint32_t> undoRequests{ 0 };
    std::atomic<uint32_t> redoRequests{ 0 };
    std::atomic<uint32_t> undoState{ 0 };
    std::atomic<uint32_t> floorRequests{ 0 };
    std::atomic<bool> measuring{ false };
    std::atomic<bool> resetCamera{ false };
    CameraPose shownPose{};
    CameraPose requestedPose{};
    bool poseRequested = false;
    uint64_t captureRequested = 0;
    uint64_t captureDone = 0;
    std::vector<uint8_t> capturePixels;
    uint32_t captureWidth = 0;
    uint32_t captureHeight = 0;
    std::string pickedDraw;
    ViewerEnvironment environment;
    float jitter[2]{};
    uint64_t targetsRequested = 0;
    uint64_t targetsDone = 0;
    ViewerTargetCapture targetCapture;
    std::condition_variable captured;
    std::atomic<bool> effectPaused{ false };
    std::atomic<bool> effectRestart{ false };
    std::atomic<float> effectSpeed{ 1 };
    std::atomic<uint32_t> channelMask{ 7 };
    std::atomic<bool> flipbookPlaying{ false };
    std::atomic<float> flipbookFps{ 15 };
    std::atomic<bool> skeletons{ false };
    std::atomic<bool> skeletonsDirty{ false };
    std::atomic<int32_t> requestedJoint{ -1 };
    std::atomic<bool> followCharacter{ true };
    std::atomic<bool> followRecenter{ false };
    std::atomic<int32_t> motionPauseRequest{ -1 };
    std::atomic<float> motionSeekRequest{ -1 };
    std::atomic<float> motionSpeed{ 1 };
    std::atomic<bool> motionLoop{ true };
    std::atomic<int32_t> moviePauseRequest{ -1 };
    std::atomic<float> movieSeekRequest{ -1 };
    std::atomic<uint32_t> hideRequests{ 0 };
    std::atomic<uint32_t> gizmoToggles{ 0 };

    std::mutex mutex;
    std::condition_variable wake;
    std::unique_ptr<Job> pendingJob;
    uint32_t pendingWidth = 0;
    uint32_t pendingHeight = 0;
    CameraInput input;
    bool leftMouse = false;
    bool rightMouse = false;
    bool middleMouse = false;
    float panInputX = 0;
    float panInputY = 0;
    int wheelX = 0;
    int wheelY = 0;
    int lastMouseX = 0;
    int lastMouseY = 0;
    bool mouseInside = false;
    bool trackingMouse = false;
    bool gizmoDragging = false;
    bool gizmoDragStart = false;
    bool gizmoDragEnd = false;
    int32_t gizmoDragHandle = 0;
    // The widget as last drawn, hit-tested by the UI thread when a button goes down.
    TransformGizmo pressGizmo;
    GizmoView pressView;
    bool pressGizmoValid = false;
    GizmoSnap snap;
    std::map<std::string, std::string> transformChanges;
    RaeViewportStats stats{};
    bool clickCandidate = false;
    int clickX = 0;
    int clickY = 0;
    bool pickRequested = false;
    int pickX = 0;
    int pickY = 0;
    bool selectRequested = false;
    int32_t requestedLod = -1;
    int32_t requestedSubmesh = -1;
    bool objectsRequested = false;
    std::vector<std::string> requestedObjects;
    std::vector<std::pair<std::string, std::vector<float>>> pendingParams;
    bool hiddenRequested = false;
    std::vector<std::string> requestedHidden;
    bool collisionGroupsRequested = false;
    std::vector<std::string> requestedCollisionGroups;
    std::vector<ViewportCollisionGroup> collisionGroupCounts;
    bool aiMapGroupsRequested = false;
    std::vector<std::string> requestedAiMapGroups;
    std::vector<ViewportAiMapGroup> aiMapGroupCounts;
    std::vector<std::string> lightStates;
    std::shared_ptr<const TextureData> sky;
    float skyIntensity = SceneLook{}.skyIntensity;
    bool skyChanged = false;
    RaeViewportBackground background{ 0, { 0.23f, 0.23f, 0.23f }, { 0.09f, 0.09f, 0.1f }, 1, 0, 0 };
    RaeSkeletonStyle skeletonStyle{ 1, 1, 0, 0, 0, 0, 0xF03232E6u, 0xFF28C8FFu, 1, 1 };
    bool effectShown = false;
    bool imageShown = false;
    float imageShownZoom = 0;
    int32_t imageShownPattern = -1;
    bool motionRequested = false;
    std::shared_ptr<const MotlistData> requestedMotlist;
    std::size_t requestedMotion = 0;
    std::string cachedMotlistPath;
    std::shared_ptr<const MotlistData> cachedMotlist;
    bool motionShown = false;
    float motionShownFrame = 0;
    float motionShownCount = 0;
    float motionShownRate = 0;
    bool motionShownPaused = false;
    uint32_t motionShownDriven = 0;
    bool movieShown = false;
    float movieShownPosition = 0;
    float movieShownDuration = 0;
    bool movieShownPaused = false;
    float effectShownTime = 0;
    uint32_t effectShownParticles = 0;
    uint32_t selectionSerial = 0;
    int32_t shownLod = 0;
    int32_t shownSubmesh = -1;
    std::string shownObject;

    // Render thread only.
    std::unique_ptr<Viewer> viewer;
    Camera camera;
    SceneFraming framing{};
    bool hasScene = false;
    bool deviceLost = false;
    PickGeometry pick;
    std::unique_ptr<EffectInstance> effect;
    struct SceneEffectInstance {
        EffectInstance instance;
        std::string owner;
        float position[3];
        float loopFrames;
        float frames;
        std::vector<uint32_t> zones;  // SceneBuild::effectZones; empty plays always
        bool zoneActive = true;
    };
    std::vector<SceneEffectInstance> sceneEffects;
    std::vector<SceneEffectZone> effectZones;
    std::vector<uint8_t> insideEffectZone;
    std::vector<SceneZoneInstance> zoneInstances;
    std::vector<uint8_t> zoneHiddenInstances;
    void UpdateEffectZones(const float eye[3]);
    std::string effectPath;
    float effectTime = 0;
    std::vector<ViewerParticle> particles;
    std::vector<ViewerMaterialVertex> materialVertices;
    std::vector<ViewerMaterialBatch> materialBatches;
    std::vector<ViewerParticleLight> materialLights;
    std::optional<ViewportImage> image;
    ViewerImageView imageView;
    int32_t imageSequence = -1;
    int32_t imagePattern = -1;
    bool flipbookShown = false;
    float flipbookTime = 0;
    std::unique_ptr<MoviePlayer> movie;
    struct SkeletonPose {
        std::string owner;
        std::shared_ptr<const SkeletonData> skeleton;
        Mat4 world;
        uint32_t jointOffset = 0;
        std::vector<Mat4> joints;
        std::vector<int32_t> parents;
        float radius = 0;
        int32_t follows = -1;           // pose whose same-named joints this one takes
        std::vector<int32_t> leaderJoint;  // joint -> joint of the followed pose, -1 for none
    };
    std::vector<SkeletonPose> poses;
    bool skeletonSolid = false;
    float skeletonEye[3]{};
    std::vector<SceneJointAttachment> attachments;
    uint32_t skinMatrixCount = 0;
    std::optional<TerrainData> collision;
    std::set<int32_t> hiddenLayers;
    SceneCollision sceneCollision;
    std::vector<uint32_t> filterGroups;  // filter index -> index into collisionGroups
    std::vector<ViewportCollisionGroup> collisionGroups;
    bool allCollisionGroups = true;
    std::set<std::string> shownCollisionGroups;
    std::set<std::string> hiddenOwners;
    std::vector<uint8_t> colliderShown;
    bool collisionDirty = false;
    std::optional<AiMapData> aiMap;
    std::set<std::string> hiddenAiMapParts;
    std::vector<SceneAiMap> sceneAiMaps;
    std::vector<ViewportAiMapGroup> aiMapGroups;
    bool allAiMaps = true;
    std::set<std::string> shownAiMaps;
    bool aiMapsAlways = false;
    bool aiMapOverlayOn = false;
    bool aiMapDirty = false;
    std::vector<SceneLightMarker> lightMarkers;
    uint32_t lightIconMask = 0;
    bool statsOverlayShown = false;
    std::vector<SceneObject> objects;
    std::unordered_map<std::string, int32_t> objectIndex;
    std::vector<std::vector<int32_t>> objectChildren;
    std::unordered_map<std::string, std::vector<uint32_t>> collidersByOwner;
    std::unordered_map<std::string, std::vector<uint32_t>> lightsByOwner;
    std::vector<std::vector<uint32_t>> drawsByInstance;
    std::vector<int32_t> meshLodByInstance;
    std::vector<GameLightParam> sceneLights;
    std::vector<float> sceneLightScattering;   // rgb per sceneLights entry
    std::vector<float> cameraLightScattering;
    std::vector<uint8_t> lightHidden;          // per sceneLights entry: its GameObject is hidden
    std::optional<SceneDirectionalLight> mainDirectional;
    std::string directionalOwner;
    bool directionalHidden = false;
    bool posesMoved = false;
    TransformGizmo gizmo;
    std::string activeObject;
    std::vector<int32_t> dragTargets;
    std::vector<Mat4> dragStartWorlds;
    std::vector<ViewerDebugVertex> handleTriangles;
    enum : uint8_t { DRAW_STATIC, DRAW_SKINNED, DRAW_DECAL };
    std::vector<uint8_t> drawKind;
    uint32_t appliedShowMask = SHOW_ALL;
    int32_t appliedForcedLod = -1;
    bool appliedStreamAll = false;
    ViewportView viewKind = ViewportView::Perspective;
    std::vector<uint8_t> instanceSkinned;
    CameraPose perspectivePose{};
    float orthoCenter[3]{};
    float orthoHalfHeight = 10;
    float clipNear = 0.05f;
    float clipFar = 1000;
    struct SavedView {
        bool saved = false;
        CameraPose pose{};
    };
    SavedView bookmarks[10];
    std::vector<std::pair<int32_t, bool>> bookmarkRequests;  // mutex
    struct UndoEntry {
        std::vector<std::string> keys;
        std::vector<Mat4> before;
        std::vector<Mat4> after;
        std::chrono::steady_clock::time_point time;
        bool coalescable = false;
    };
    std::vector<UndoEntry> undoStack;
    std::vector<UndoEntry> redoStack;
    bool measureDragging = false;  // mutex
    bool measureStart = false;     // mutex
    bool measureHasStart = false;
    bool measureValid = false;
    float measureFrom[3]{};
    float measureTo[3]{};
    bool measureShown = false;
    std::string measureLabelText;
    std::vector<uint32_t> measureLabelPixels;
    uint32_t measureLabelSize[2]{};
    uint32_t renderedInputSerial = 0;
    std::chrono::steady_clock::time_point lastRendered{};
    CameraPose renderedPose{};
    std::shared_ptr<const MotlistData> motlist;
    const MotData* motion = nullptr;
    std::vector<SkeletalAnimator> animators;
    uint32_t drivenJoints = 0;
    float motionFrame = 0;
    bool motionPaused = false;
    std::vector<float> skinMatrices;
    // Joint of poses[0] the camera follows.
    int32_t followJoint = -1;
    bool followValid = false;
    float followLast[3]{};
    std::set<std::string> selectedOwners;
    bool meshAsset = false;
    uint32_t mainInstance = 0;
    bool materialPreview = false;
    uint32_t lodCount = 1;
    int32_t displayedLod = 0;
    std::vector<uint8_t> hiddenInstances;
    SceneLods sceneLods;
    std::unique_ptr<PostProcessZones> postProcessZones;
    std::unique_ptr<LightProbeNetworks> probeNetworks;
    std::vector<uint8_t> lodNear;
    // Room group of each instance, -1 outside rooms.
    std::vector<int32_t> instanceRoom;
    int32_t currentRoom = -1;
    float roomProbe[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
    // The rooms' Terrain-filter collision in world space, binned by XZ cell.
    struct RoomGround {
        std::vector<float> triangles;  // three xyz each
        std::vector<int32_t> rooms;    // room group per triangle
        std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
    };
    RoomGround roomGround;
    // By group: rooms with ground close to the camera, drawn whatever the current room lists.
    std::vector<uint8_t> roomsNear;
    std::vector<uint8_t> lodHiddenInstances;
    std::vector<SceneMeshLod> meshLods;
    std::vector<uint8_t> instanceLod;
    // Per draw id; empty = all shown.
    std::vector<uint8_t> drawShown;
    int32_t selectedSubmesh = -1;
    bool hasSelectionBounds = false;
    float selectionCenter[3]{};
    float selectionRadius = 0;
};

#endif
