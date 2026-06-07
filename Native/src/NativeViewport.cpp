#include "NativeViewport.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <windowsx.h>

#include "Explorer/EffectParams.h"
#include "Explorer/Log.h"
#include "Explorer/SkeletalAnimator.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

constexpr const wchar_t* CLASS_NAME = L"REAssetNativeViewport";
constexpr float CLEAR_COLOR[3] = { 18 / 255.0f, 20 / 255.0f, 25 / 255.0f };
constexpr uint32_t MIN_SIZE = 8;
constexpr int CLICK_SLOP = 3;
constexpr int BONE_SPHERE_SIDES = 12;
constexpr int SOLID_SPHERE_RINGS = 6;
constexpr int SOLID_SPHERE_SEGMENTS = 10;
constexpr float JOINT_AXIS_LENGTH = 3.0f;
constexpr float JOINT_DIMMED_ALPHA = 0.25f;
// RGBA8, red in the low byte.
constexpr uint32_t JOINT_AXIS_COLORS[3] = { 0xFF3C3CE6u, 0xFF50C850u, 0xFFF07846u };
constexpr uint32_t SIDE_COLORS[3] = { 0xFF46C8EBu, 0xFFFF9650u, 0xFF5050F0u };
constexpr uint32_t JOINT_PALETTE[8] = { 0xFF5050E6u, 0xFF3CAAF0u, 0xFF50DCDCu, 0xFF6EC85Au,
                                        0xFFD2C846u, 0xFFF08C50u, 0xFFEB6EA0u, 0xFFBE64E1u };
struct CollisionGroupColor {
    const char* group;
    uint8_t rgb[3];
};
constexpr CollisionGroupColor COLLISION_GROUP_COLORS[] = {
    { "Terrain", { 110, 190, 80 } }, { "Effect", { 80, 150, 225 } }, { "Press", { 200, 110, 200 } },
    { "Sensor", { 235, 200, 60 } },  { "Load", { 160, 110, 230 } },  { "Sound", { 70, 200, 200 } },
    { "AI", { 230, 100, 120 } },     { "Material", { 170, 130, 90 } },
};
constexpr const char* COLLISION_VOLUME_GROUPS[] = { "Sensor", "Load", "Sound", "AI" };
constexpr uint8_t COLLISION_OTHER_COLOR[3] = { 170, 170, 170 };
constexpr uint8_t COLLISION_SELECTED_COLOR[3] = { 255, 150, 40 };
constexpr uint8_t COLLISION_VOLUME_ALPHA = 70;
constexpr uint8_t AI_MAP_COLORS[][3] = {
    { 60, 200, 110 }, { 70, 150, 230 }, { 230, 150, 60 }, { 170, 110, 230 },
    { 60, 200, 200 }, { 230, 100, 160 }, { 220, 200, 70 }, { 220, 80, 70 },
};
constexpr float LIGHT_ICON_PICK_SLACK = 2.0f;
constexpr float MEASURE_LINE_PIXELS = 3.0f;
constexpr std::chrono::milliseconds IDLE_REDRAW_INTERVAL{ 500 };
constexpr float MEASURE_POINT_PIXELS = 4.0f;
constexpr uint32_t MEASURE_COLOR = 0xFF33D9FFu;
constexpr float LIGHT_FRAME_RADIUS = 1.5f;
constexpr float DIRECTIONAL_ARROW_SCALE = 0.15f;
constexpr float IMAGE_FIT = 0.94f;
constexpr float ROOM_PROBE_STEP = 0.25f;
constexpr float ROOM_GROUND_CELL = 4.0f;
constexpr float ROOM_NEAR_BELOW = 4.0f;
constexpr float ROOM_NEAR_ABOVE = 1.0f;
// RenderConfig::decideLodOffset at the highest mesh quality: one LOD finer than computed.
constexpr int MESH_LOD_OFFSET = -1;
constexpr int EFFECT_FRAMING_FRAMES = 120;
constexpr float EMPTY_CENTER[3] = { 0, 0.5f, 0 };
constexpr float EMPTY_RADIUS = 3;

HINSTANCE ModuleInstance() {
    return reinterpret_cast<HINSTANCE>(&__ImageBase);
}

HWND ParkingWindow() {
    static HWND parking = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = ModuleInstance();
        wc.lpszClassName = L"REAssetNativeParking";
        RegisterClassExW(&wc);
        return CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64,
                               nullptr, nullptr, ModuleInstance(), nullptr);
    }();
    return parking;
}

float Dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void Cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

void Normalize(float v[3]) {
    float len = std::sqrt(Dot(v, v));
    if (len > 0) for (int i = 0; i < 3; i++) v[i] /= len;
}

bool IntersectBox(const float origin[3], const float dir[3], const float lo[3], const float hi[3], float maxT) {
    float t0 = 0;
    float t1 = maxT;
    for (int c = 0; c < 3; c++) {
        if (std::fabs(dir[c]) < 1e-12f) {
            if (origin[c] < lo[c] || origin[c] > hi[c]) return false;
            continue;
        }
        float a = (lo[c] - origin[c]) / dir[c];
        float b = (hi[c] - origin[c]) / dir[c];
        t0 = std::max(t0, std::min(a, b));
        t1 = std::min(t1, std::max(a, b));
        if (t0 > t1) return false;
    }
    return true;
}

// Moller-Trumbore, two-sided: the game pipelines draw without culling.
bool IntersectTriangle(const float origin[3], const float dir[3], const float* v0, const float* v1,
                       const float* v2, float& t) {
    float e1[3] = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] };
    float e2[3] = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
    float p[3];
    Cross(dir, e2, p);
    float det = Dot(e1, p);
    if (std::fabs(det) < 1e-20f) return false;
    float inv = 1.0f / det;
    float s[3] = { origin[0] - v0[0], origin[1] - v0[1], origin[2] - v0[2] };
    float u = Dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    float q[3];
    Cross(s, e1, q);
    float v = Dot(dir, q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = Dot(e2, q) * inv;
    return t > 0;
}

}

NativeViewport::NativeViewport(HWND parent) {
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &NativeViewport::WndProc;
        wc.hInstance = ModuleInstance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = CLASS_NAME;
        RegisterClassExW(&wc);
    });

    hwnd = CreateWindowExW(0, CLASS_NAME, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                           0, 0, MIN_SIZE, MIN_SIZE, parent ? parent : ParkingWindow(), nullptr, ModuleInstance(), this);
    if (!hwnd) throw std::runtime_error("viewport: CreateWindowExW failed");

    RECT rect{};
    GetClientRect(hwnd, &rect);
    pendingWidth = std::max<uint32_t>(static_cast<uint32_t>(rect.right), MIN_SIZE);
    pendingHeight = std::max<uint32_t>(static_cast<uint32_t>(rect.bottom), MIN_SIZE);
    renderThread = std::thread(&NativeViewport::RenderLoop, this);
}

NativeViewport::~NativeViewport() {
    running = false;
    wake.notify_all();
    // DXGI may send messages to the window's thread while the render thread
    // presents or resizes; dispatch them so joining cannot deadlock.
    while (!finished.load()) {
        MSG msg;
        while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(1);
    }
    renderThread.join();
    DestroyWindow(hwnd);
}

void NativeViewport::Load(std::unique_ptr<ViewportScene> scene) {
    auto job = std::make_unique<Job>();
    job->scene = std::move(scene);
    std::future<void> done = job->done.get_future();
    {
        std::lock_guard lock(mutex);
        if (!running) throw std::runtime_error("viewport closed");
        if (pendingJob) {
            pendingJob->done.set_exception(std::make_exception_ptr(std::runtime_error("superseded by a newer load")));
        }
        pendingJob = std::move(job);
    }
    wake.notify_all();
    done.get();
}

void NativeViewport::Clear() {
    std::lock_guard lock(mutex);
    if (pendingJob) {
        pendingJob->done.set_exception(std::make_exception_ptr(std::runtime_error("viewport cleared")));
    }
    pendingJob = std::make_unique<Job>();
    wake.notify_all();
}

void NativeViewport::SetFlipbook(bool playing, float fps) {
    flipbookFps = fps;
    flipbookPlaying = playing;
}

void NativeViewport::PlayMotion(std::shared_ptr<const MotlistData> list, std::size_t index) {
    std::lock_guard lock(mutex);
    requestedMotlist = std::move(list);
    requestedMotion = index;
    motionRequested = true;
}

bool NativeViewport::MotionStatus(float& frame, float& frameCount, float& frameRate, bool& paused, uint32_t& driven) {
    std::lock_guard lock(mutex);
    frame = motionShownFrame;
    frameCount = motionShownCount;
    frameRate = motionShownRate;
    paused = motionShownPaused;
    driven = motionShownDriven;
    return motionShown;
}

std::shared_ptr<const MotlistData> NativeViewport::CachedMotlist(const std::string& path) {
    std::lock_guard lock(mutex);
    return path == cachedMotlistPath ? cachedMotlist : nullptr;
}

void NativeViewport::CacheMotlist(const std::string& path, std::shared_ptr<const MotlistData> list) {
    std::lock_guard lock(mutex);
    cachedMotlistPath = path;
    cachedMotlist = std::move(list);
}

bool NativeViewport::MovieStatus(float& position, float& duration, bool& paused) {
    std::lock_guard lock(mutex);
    position = movieShownPosition;
    duration = movieShownDuration;
    paused = movieShownPaused;
    return movieShown;
}

bool NativeViewport::ImageStatus(float& zoom, int32_t& pattern) {
    std::lock_guard lock(mutex);
    zoom = imageShownZoom;
    pattern = imageShownPattern;
    return imageShown;
}

bool NativeViewport::EffectStatus(float& time, uint32_t& count) {
    std::lock_guard lock(mutex);
    time = effectShownTime;
    count = effectShownParticles;
    return effectShown;
}

CameraPose NativeViewport::GetCameraPose() {
    std::lock_guard lock(mutex);
    return shownPose;
}

void NativeViewport::SetCameraPose(const CameraPose& pose) {
    std::lock_guard lock(mutex);
    requestedPose = pose;
    poseRequested = true;
}

bool NativeViewport::CaptureFrame(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height, uint32_t timeoutMs) {
    std::unique_lock lock(mutex);
    uint64_t ticket = ++captureRequested;
    wake.notify_all();
    if (!captured.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return captureDone >= ticket; })) return false;
    rgba = capturePixels;
    width = captureWidth;
    height = captureHeight;
    return !rgba.empty();
}

bool NativeViewport::CaptureTargets(ViewerTargetCapture& out, uint32_t timeoutMs) {
    std::unique_lock lock(mutex);
    uint64_t ticket = ++targetsRequested;
    wake.notify_all();
    if (!captured.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return targetsDone >= ticket; })) return false;
    out = std::move(targetCapture);
    return !out.images.empty();
}

std::string NativeViewport::PickedDraw() {
    std::lock_guard lock(mutex);
    return pickedDraw;
}

void NativeViewport::SetSky(std::shared_ptr<const TextureData> texture, float intensity) {
    {
        std::lock_guard lock(mutex);
        sky = std::move(texture);
        skyIntensity = intensity;
        skyChanged = true;
    }
    wake.notify_all();
}

void NativeViewport::SetBackground(const RaeViewportBackground& value) {
    std::lock_guard lock(mutex);
    background = value;
}

void NativeViewport::SetSkeletonStyle(const RaeSkeletonStyle& value) {
    std::lock_guard lock(mutex);
    skeletonStyle = value;
    skeletonsDirty = true;
}

void NativeViewport::SetParentWindow(HWND parent) {
    if (parent == nullptr) ShowWindow(hwnd, SW_HIDE);
    SetParent(hwnd, parent ? parent : ParkingWindow());
    wake.notify_all();
}

RaeViewportStats NativeViewport::Stats() {
    std::lock_guard lock(mutex);
    return stats;
}

void NativeViewport::Select(int32_t lod, int32_t submesh) {
    std::lock_guard lock(mutex);
    selectRequested = true;
    requestedLod = lod;
    requestedSubmesh = submesh;
}

void NativeViewport::SelectObjects(std::vector<std::string> keys) {
    std::lock_guard lock(mutex);
    objectsRequested = true;
    requestedObjects = std::move(keys);
}

void NativeViewport::SetMaterialParam(std::string key, std::vector<float> values) {
    std::lock_guard lock(mutex);
    pendingParams.emplace_back(std::move(key), std::move(values));
}

void NativeViewport::SetHiddenObjects(std::vector<std::string> keys) {
    std::lock_guard lock(mutex);
    hiddenRequested = true;
    requestedHidden = std::move(keys);
}

void NativeViewport::SetCollisionGroups(std::vector<std::string> names) {
    std::lock_guard lock(mutex);
    collisionGroupsRequested = true;
    requestedCollisionGroups = std::move(names);
}

std::vector<ViewportCollisionGroup> NativeViewport::CollisionGroups() {
    std::lock_guard lock(mutex);
    return collisionGroupCounts;
}

std::vector<std::string> NativeViewport::LightStates() {
    std::lock_guard lock(mutex);
    return lightStates;
}

void NativeViewport::SetAiMapGroups(std::vector<std::string> names) {
    std::lock_guard lock(mutex);
    aiMapGroupsRequested = true;
    requestedAiMapGroups = std::move(names);
}

std::vector<ViewportAiMapGroup> NativeViewport::AiMapGroups() {
    std::lock_guard lock(mutex);
    return aiMapGroupCounts;
}

std::string NativeViewport::SelectedObject() {
    std::lock_guard lock(mutex);
    return shownObject;
}

uint32_t NativeViewport::Selection(int32_t& lod, int32_t& submesh) {
    std::lock_guard lock(mutex);
    lod = shownLod;
    submesh = shownSubmesh;
    return selectionSerial;
}

LRESULT CALLBACK NativeViewport::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    auto* self = reinterpret_cast<NativeViewport*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, msg, wParam, lParam);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return self->HandleMessage(msg, wParam, lParam);
}

LRESULT NativeViewport::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE: {
            uint32_t w = LOWORD(lParam);
            uint32_t h = HIWORD(lParam);
            if (wParam != SIZE_MINIMIZED && w >= MIN_SIZE && h >= MIN_SIZE) {
                std::lock_guard lock(mutex);
                pendingWidth = w;
                pendingHeight = h;
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            SetFocus(hwnd);
            SetCapture(hwnd);
            inputSerial++;
            std::lock_guard lock(mutex);
            if (msg == WM_LBUTTONDOWN && measuring.load() && !rightMouse && (GetKeyState(VK_MENU) & 0x8000) == 0) {
                measureDragging = true;
                measureStart = true;
                lastMouseX = GET_X_LPARAM(lParam);
                lastMouseY = GET_Y_LPARAM(lParam);
                clickCandidate = false;
                return 0;
            }
            // A press on a gizmo handle drags it: no camera move, no pick.
            if (msg == WM_LBUTTONDOWN && !rightMouse && !middleMouse && (GetKeyState(VK_MENU) & 0x8000) == 0 && pressGizmoValid) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                float scale = renderScale.load();
                GizmoHandle handle = pressGizmo.HitTest(pressView, static_cast<float>(x) * scale, static_cast<float>(y) * scale);
                if (handle != GizmoHandle::None) {
                    gizmoDragging = true;
                    gizmoDragStart = true;
                    gizmoDragHandle = static_cast<int32_t>(handle);
                    lastMouseX = x;
                    lastMouseY = y;
                    clickCandidate = false;
                    return 0;
                }
            }
            if (msg == WM_LBUTTONDOWN) leftMouse = true;
            if (msg == WM_RBUTTONDOWN) rightMouse = true;
            if (msg == WM_MBUTTONDOWN) middleMouse = true;
            lastMouseX = GET_X_LPARAM(lParam);
            lastMouseY = GET_Y_LPARAM(lParam);
            // ALT+LMB orbits; a plain left click selects.
            clickCandidate = msg == WM_LBUTTONDOWN && !rightMouse && (GetKeyState(VK_MENU) & 0x8000) == 0;
            clickX = lastMouseX;
            clickY = lastMouseY;
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            bool anyDown;
            {
                std::lock_guard lock(mutex);
                if (msg == WM_LBUTTONUP && leftMouse && clickCandidate) {
                    int x = GET_X_LPARAM(lParam);
                    int y = GET_Y_LPARAM(lParam);
                    if (std::abs(x - clickX) <= CLICK_SLOP && std::abs(y - clickY) <= CLICK_SLOP) {
                        pickRequested = true;
                        pickX = x;
                        pickY = y;
                    }
                }
                if (msg == WM_LBUTTONUP && gizmoDragging) {
                    gizmoDragging = false;
                    gizmoDragEnd = true;
                }
                if (msg == WM_LBUTTONUP) measureDragging = false;
                if (msg == WM_LBUTTONUP) leftMouse = false;
                if (msg == WM_RBUTTONUP) rightMouse = false;
                if (msg == WM_MBUTTONUP) middleMouse = false;
                anyDown = leftMouse || rightMouse || middleMouse;
            }
            if (!anyDown) ReleaseCapture();
            return 0;
        }
        case WM_CAPTURECHANGED: {
            std::lock_guard lock(mutex);
            leftMouse = false;
            rightMouse = false;
            middleMouse = false;
            clickCandidate = false;
            if (gizmoDragging) {
                gizmoDragging = false;
                gizmoDragEnd = true;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (!trackingMouse) {
                TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                trackingMouse = TrackMouseEvent(&track) != FALSE;
            }
            inputSerial++;
            std::lock_guard lock(mutex);
            mouseInside = true;
            if (leftMouse || rightMouse) {
                input.mouseDx += static_cast<float>(x - lastMouseX);
                input.mouseDy += static_cast<float>(y - lastMouseY);
            }
            if (leftMouse || rightMouse || middleMouse) {
                panInputX += static_cast<float>(x - lastMouseX);
                panInputY += static_cast<float>(y - lastMouseY);
            }
            lastMouseX = x;
            lastMouseY = y;
            return 0;
        }
        case WM_MOUSELEAVE: {
            trackingMouse = false;
            std::lock_guard lock(mutex);
            mouseInside = false;
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT cursor{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &cursor);
            inputSerial++;
            std::lock_guard lock(mutex);
            input.wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            wheelX = cursor.x;
            wheelY = cursor.y;
            return 0;
        }
        case WM_KEYDOWN:
            inputSerial++;
            if (wParam == 'F') ResetCamera();
            // Bit 30: auto-repeat of a held key.
            if (wParam == 'H' && (lParam & (1 << 30)) == 0) hideRequests++;
            if (wParam == 'G' && (lParam & (1 << 30)) == 0) gizmoToggles++;
            // Unreal's widget keys; W and E also fly the camera while the right button is held.
            if ((lParam & (1 << 30)) == 0) {
                bool flying;
                {
                    std::lock_guard lock(mutex);
                    flying = rightMouse || gizmoDragging;
                }
                bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (control && wParam == 'Z') {
                    if (shift) Redo();
                    else Undo();
                }
                if (control && wParam == 'Y') Redo();
                if (wParam == VK_END) SnapSelectionToFloor();
                if (!flying && wParam >= '0' && wParam <= '9') Bookmark(static_cast<int32_t>(wParam - '0'), control);
                if (!flying && !control) {
                    if (wParam == 'W') gizmoMode = static_cast<int32_t>(GizmoMode::Translate);
                    if (wParam == 'E') gizmoMode = static_cast<int32_t>(GizmoMode::Rotate);
                    if (wParam == 'R') gizmoMode = static_cast<int32_t>(GizmoMode::Scale);
                    if (wParam == VK_SPACE) gizmoMode = (gizmoMode.load() + 1) % 3;
                }
                if (!flying && control && wParam == VK_OEM_3) gizmoLocal = !gizmoLocal.load();
            }
            return 0;
        // ALT is the orbit modifier; letting it through would open the host's system menu.
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if (wParam == VK_MENU) return 0;
            break;
        case WM_SYSCHAR:
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void NativeViewport::CreateBlankViewer(bool withSky) {
    uint32_t width;
    uint32_t height;
    std::shared_ptr<const TextureData> texture;
    float intensity;
    {
        std::lock_guard lock(mutex);
        width = pendingWidth;
        height = pendingHeight;
        if (withSky) {
            texture = sky;
            skyChanged = false;
        }
        intensity = skyIntensity;
    }
    viewer = std::make_unique<Viewer>(hwnd, width, height);
    viewer->SetClearColor(CLEAR_COLOR[0], CLEAR_COLOR[1], CLEAR_COLOR[2]);
    viewer->SetClipPlanes(0.05f, 1000.0f);
    deviceLost = false;
    if (texture) {
        viewer->LoadSky(ToGameTexture(*texture));
        viewer->SetSkyIntensity(intensity);
    }
}

// Particles go back to front for the premultiplied "over" blend.
void NativeViewport::StepEffect(float dt, const float eye[3]) {
    if (effectRestart.exchange(false)) {
        effect->Restart();
        effectTime = 0;
    }
    if (!effectPaused.load()) {
        float step = dt * std::clamp(effectSpeed.load(), 0.0f, 8.0f);
        effect->Update(step);
        effectTime += step;
        if (effect->Finished()) {
            effect->Restart();
            effectTime = 0;
        }
    }
    particles.clear();
    materialVertices.clear();
    materialBatches.clear();
    materialLights.clear();
    effect->Collect(particles, &materialLights);
    effect->CollectMaterial(materialVertices, materialBatches, materialLights);
    viewer->SetMaterialParticles(materialVertices, std::move(materialBatches), materialLights);
    std::sort(particles.begin(), particles.end(), [eye](const ViewerParticle& a, const ViewerParticle& b) {
        auto dist2 = [eye](const ViewerParticle& p) {
            float d[3];
            for (int c = 0; c < 3; c++) d[c] = p.position[c] + 0.5f * p.axis[c] - eye[c];
            return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        };
        return dist2(a) > dist2(b);
    });
    viewer->SetParticles(particles);
    std::lock_guard lock(mutex);
    effectShownTime = effectTime;
    effectShownParticles = static_cast<uint32_t>(particles.size());
}

// EffectEmitZoneGroup: EmitterInfo::setEnable on entering and leaving; an effect restarts when it is enabled again.
void NativeViewport::UpdateEffectZones(const float eye[3]) {
    if (effectZones.empty()) return;
    bool changed = false;
    for (std::size_t z = 0; z < effectZones.size(); z++) {
        uint8_t inside = std::any_of(effectZones[z].volumes.begin(), effectZones[z].volumes.end(),
                                     [&](const CollisionVolume& volume) { return VolumeContains(volume, eye); }) ? 1 : 0;
        changed |= inside != insideEffectZone[z];
        insideEffectZone[z] = inside;
    }
    if (!changed) return;
    auto active = [&](const std::vector<uint32_t>& zones) {
        return zones.empty() || std::any_of(zones.begin(), zones.end(), [&](uint32_t z) { return z < insideEffectZone.size() && insideEffectZone[z]; });
    };
    for (SceneEffectInstance& e : sceneEffects) {
        bool now = active(e.zones);
        if (now && !e.zoneActive) {
            e.instance.Restart();
            e.frames = 0;
        }
        e.zoneActive = now;
    }
    for (const SceneZoneInstance& z : zoneInstances) {
        if (z.instance < zoneHiddenInstances.size()) zoneHiddenInstances[z.instance] = active(z.zones) ? 0 : 1;
    }
    if (!zoneInstances.empty()) UpdateDrawMask();
}

// Effects beyond EFFECT_RANGE neither simulate nor draw.
void NativeViewport::StepSceneEffects(float dt, const float eye[3]) {
    constexpr float EFFECT_RANGE = 250.0f;
    float step = effectPaused.load() ? 0.0f : dt;
    particles.clear();
    materialVertices.clear();
    materialBatches.clear();
    materialLights.clear();
    UpdateEffectZones(eye);
    for (SceneEffectInstance& e : sceneEffects) {
        if (hiddenOwners.contains(e.owner) || !e.zoneActive) continue;
        float d[3] = { e.position[0] - eye[0], e.position[1] - eye[1], e.position[2] - eye[2] };
        if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > EFFECT_RANGE * EFFECT_RANGE) continue;
        e.instance.Update(step);
        e.frames += step * 60.0f;
        if (e.loopFrames > 0 && e.frames >= e.loopFrames) {
            e.instance.Restart();
            e.frames = 0;
        }
        e.instance.Collect(particles, &materialLights);
        e.instance.CollectMaterial(materialVertices, materialBatches, materialLights);
    }
    std::sort(particles.begin(), particles.end(), [eye](const ViewerParticle& a, const ViewerParticle& b) {
        auto dist2 = [eye](const ViewerParticle& p) {
            float d[3];
            for (int c = 0; c < 3; c++) d[c] = p.position[c] + 0.5f * p.axis[c] - eye[c];
            return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        };
        return dist2(a) > dist2(b);
    });
    viewer->SetParticles(particles);
    std::sort(materialBatches.begin(), materialBatches.end(), [eye](const ViewerMaterialBatch& a, const ViewerMaterialBatch& b) {
        auto dist2 = [eye](const ViewerMaterialBatch& batch) {
            float d[3];
            for (int c = 0; c < 3; c++) d[c] = batch.emitterPosition[c] - eye[c];
            return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        };
        return dist2(a) > dist2(b);
    });
    viewer->SetMaterialParticles(materialVertices, std::move(materialBatches), materialLights);
}

int32_t NativeViewport::PatternAt(int x, int y) const {
    float rect[4];
    if (!image || !image->atlas || !viewer->PreviewRect(imageView, rect)) return -1;
    float u = imageView.source[0] + (static_cast<float>(x) - rect[0]) / (rect[2] - rect[0]) *
                                        (imageView.source[2] - imageView.source[0]);
    float v = imageView.source[1] + (static_cast<float>(y) - rect[1]) / (rect[3] - rect[1]) *
                                        (imageView.source[3] - imageView.source[1]);
    int32_t best = -1;
    float bestArea = FLT_MAX;
    for (std::size_t p = 0; p < image->uvs.patterns.size(); p++) {
        const UvsPattern& pattern = image->uvs.patterns[p];
        uint32_t page = pattern.textureIndex < image->pages.size() ? image->pages[pattern.textureIndex] : UINT32_MAX;
        if (page != imageView.texture) continue;
        const float* r = pattern.rect;
        if (u < std::min(r[0], r[2]) || u > std::max(r[0], r[2]) || v < std::min(r[1], r[3]) || v > std::max(r[1], r[3])) {
            continue;
        }
        float area = std::fabs((r[2] - r[0]) * (r[3] - r[1]));
        if (area < bestArea) {
            best = static_cast<int32_t>(p);
            bestArea = area;
        }
    }
    return best;
}

void NativeViewport::StepImage(const CameraInput& input, float panX, float panY, int cursorX, int cursorY, float dt,
                               bool& selectNow, int32_t selectLod, int32_t selectSubmesh, bool& pickNow, int pickAtX,
                               int pickAtY) {
    if (movie) {
        int32_t pause = moviePauseRequest.exchange(-1);
        float seekTo = movieSeekRequest.exchange(-1);
        if (seekTo >= 0) movie->Seek(seekTo);
        if (pause >= 0) movie->SetPaused(pause != 0);
        if (movie->Update(dt)) viewer->UpdatePreviewTexture(0, movie->Frame().data(), movie->Info().width * 4);
        std::lock_guard lock(mutex);
        movieShown = true;
        movieShownPosition = static_cast<float>(movie->Position());
        movieShownDuration = static_cast<float>(movie->Info().duration);
        movieShownPaused = movie->Paused();
    }

    const UvsData& uvs = image->uvs;
    auto pageOf = [&](int32_t pattern) {
        if (pattern < 0 || static_cast<std::size_t>(pattern) >= uvs.patterns.size()) return image->pages.empty() ? 0u : image->pages[0];
        uint32_t index = uvs.patterns[pattern].textureIndex;
        return index < image->pages.size() ? image->pages[index] : 0u;
    };
    auto sequenceOf = [&](int32_t pattern) {
        for (std::size_t s = 0; s < uvs.sequences.size(); s++) {
            const UvsSequence& seq = uvs.sequences[s];
            if (static_cast<uint32_t>(pattern) >= seq.firstPattern &&
                static_cast<uint32_t>(pattern) < seq.firstPattern + seq.patternCount) {
                return static_cast<int32_t>(s);
            }
        }
        return -1;
    };

    bool selectionChanged = false;
    if (selectNow) {
        selectNow = false;
        if (image->atlas) {
            imageSequence = selectLod;
            imagePattern = selectSubmesh;
        } else {
            imageView.slice = static_cast<uint32_t>(std::max(selectLod, 0));
            imageView.mip = selectSubmesh;
        }
    }
    if (pickNow) {
        pickNow = false;
        if (image->atlas && !flipbookShown) {
            int32_t hit = PatternAt(pickAtX, pickAtY);
            imagePattern = hit;
            if (hit >= 0) {
                const UvsSequence* current = imageSequence >= 0 && static_cast<std::size_t>(imageSequence) < uvs.sequences.size()
                    ? &uvs.sequences[imageSequence] : nullptr;
                bool inCurrent = current && static_cast<uint32_t>(hit) >= current->firstPattern &&
                                 static_cast<uint32_t>(hit) < current->firstPattern + current->patternCount;
                if (!inCurrent) imageSequence = sequenceOf(hit);
            }
            selectionChanged = true;
        }
    }

    ViewerImageView& view = imageView;
    bool playing = image->atlas && flipbookPlaying.load() && imageSequence >= 0 && !uvs.patterns.empty() &&
                   static_cast<std::size_t>(imageSequence) < uvs.sequences.size() &&
                   uvs.sequences[imageSequence].patternCount > 0;
    if (playing != flipbookShown) {
        flipbookShown = playing;
        flipbookTime = 0;
        view.zoom = IMAGE_FIT;
        view.pan[0] = 0;
        view.pan[1] = 0;
    }
    int32_t shownPattern = imagePattern;
    if (playing) {
        const UvsSequence& seq = uvs.sequences[imageSequence];
        flipbookTime += dt * std::clamp(flipbookFps.load(), 0.1f, 240.0f);
        auto frame = static_cast<uint32_t>(flipbookTime) % seq.patternCount;
        shownPattern = static_cast<int32_t>(std::min<std::size_t>(seq.firstPattern + frame, uvs.patterns.size() - 1));
        view.texture = pageOf(shownPattern);
        std::copy(uvs.patterns[shownPattern].rect, uvs.patterns[shownPattern].rect + 4, view.source);
    } else {
        const float whole[4] = { 0, 0, 1, 1 };
        std::copy(whole, whole + 4, view.source);
        if (image->atlas) {
            int32_t anchor = imagePattern >= 0 ? imagePattern
                : imageSequence >= 0 && static_cast<std::size_t>(imageSequence) < uvs.sequences.size()
                    ? static_cast<int32_t>(uvs.sequences[imageSequence].firstPattern) : -1;
            view.texture = pageOf(anchor);
        }
    }

    if (resetCamera.exchange(false)) {
        view.zoom = IMAGE_FIT;
        view.pan[0] = 0;
        view.pan[1] = 0;
    }
    view.pan[0] += panX;
    view.pan[1] += panY;
    if (input.wheel != 0) {
        // Keeps the texel under the cursor in place.
        float zoom = std::clamp(view.zoom * std::pow(1.25f, input.wheel), 0.05f, 512.0f);
        float factor = zoom / view.zoom;
        float centerX = static_cast<float>(viewer->GetWidth()) * 0.5f;
        float centerY = static_cast<float>(viewer->GetHeight()) * 0.5f;
        float regionX = centerX + view.pan[0];
        float regionY = centerY + view.pan[1];
        view.pan[0] = static_cast<float>(cursorX) - (static_cast<float>(cursorX) - regionX) * factor - centerX;
        view.pan[1] = static_cast<float>(cursorY) - (static_cast<float>(cursorY) - regionY) * factor - centerY;
        view.zoom = zoom;
    }
    view.channels = channelMask.load();
    view.checker = (view.channels & 8) != 0;
    viewer->SetPreviewView(view);

    std::vector<ViewerOverlayRect> overlay;
    std::optional<ViewerOverlayRect> top;
    if (image->atlas && !playing) {
        for (std::size_t p = 0; p < uvs.patterns.size(); p++) {
            if (pageOf(static_cast<int32_t>(p)) != view.texture) continue;
            bool selected = static_cast<int32_t>(p) == imagePattern;
            bool inSequence = sequenceOf(static_cast<int32_t>(p)) == imageSequence && imageSequence >= 0;
            ViewerOverlayRect rect{};
            std::copy(uvs.patterns[p].rect, uvs.patterns[p].rect + 4, rect.uv);
            const float plain[4] = { 1, 1, 1, 0.22f };
            const float sequence[4] = { 0.35f, 0.62f, 1, 0.9f };
            const float picked[4] = { 1, 0.72f, 0.25f, 1 };
            const float* color = selected ? picked : inSequence ? sequence : plain;
            std::copy(color, color + 4, rect.color);
            std::copy(color, color + 3, rect.fill);
            rect.fill[3] = selected ? 0.2f : inSequence ? 0.06f : 0;
            if (selected) top = rect;
            else overlay.push_back(rect);
        }
        // The selected one last, so its outline is on top.
        if (top) overlay.push_back(*top);
    }
    viewer->SetPreviewOverlay(overlay);

    float rect[4];
    uint32_t texWidth = 0;
    uint32_t texHeight = 0;
    viewer->PreviewTextureSize(view.texture, texWidth, texHeight);
    float regionTexels = std::fabs(view.source[2] - view.source[0]) * static_cast<float>(texWidth);
    float zoom = viewer->PreviewRect(view, rect) && regionTexels > 0 ? (rect[2] - rect[0]) / regionTexels : 0;
    std::lock_guard lock(mutex);
    imageShownZoom = zoom;
    imageShownPattern = shownPattern;
    if (selectionChanged) {
        shownLod = imageSequence;
        shownSubmesh = imagePattern;
        selectionSerial++;
    }
}

namespace {

float Length(const float v[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

uint32_t ScaleAlpha(uint32_t color, float opacity) {
    float alpha = static_cast<float>(color >> 24) * std::clamp(opacity, 0.0f, 1.0f);
    return (color & 0x00FFFFFFu) | static_cast<uint32_t>(std::lround(alpha)) << 24;
}

void PushLine(std::vector<ViewerDebugVertex>& out, const float a[3], const float b[3], uint32_t color) {
    out.push_back({ { a[0], a[1], a[2] }, color });
    out.push_back({ { b[0], b[1], b[2] }, color });
}

// The overlay has no depth buffer: faces turned away from the eye are dropped so they cannot show through.
void PushFace(std::vector<ViewerDebugVertex>& out, const float a[3], const float b[3], const float c[3],
              const float inside[3], const float eye[3], uint32_t color) {
    float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    float n[3];
    Cross(e1, e2, n);
    float outward[3] = { a[0] - inside[0], a[1] - inside[1], a[2] - inside[2] };
    float toEye[3] = { eye[0] - a[0], eye[1] - a[1], eye[2] - a[2] };
    if (Dot(n, outward) * Dot(n, toEye) <= 0) return;
    out.push_back({ { a[0], a[1], a[2] }, color });
    out.push_back({ { b[0], b[1], b[2] }, color });
    out.push_back({ { c[0], c[1], c[2] }, color });
}

void JointAxes(const float* m, float axes[3][3]) {
    for (int a = 0; a < 3; a++) {
        float len = Length(m + a * 4);
        for (int c = 0; c < 3; c++) axes[a][c] = len > 1e-6f ? m[a * 4 + c] / len : (a == c ? 1.0f : 0.0f);
    }
}

// Bones start at the parent joint; the base square sits one radius along the bone.
void AppendBone(int32_t style, const float head[3], const float tail[3], float radius, uint32_t color, const float eye[3],
                std::vector<ViewerDebugVertex>& triangles, std::vector<ViewerDebugVertex>& lines) {
    float dir[3] = { tail[0] - head[0], tail[1] - head[1], tail[2] - head[2] };
    float length = Length(dir);
    if (style == 3) {
        if (length > 1e-6f) PushLine(lines, head, tail, color);
        return;
    }
    if (length <= radius * 1.5f) return;
    for (float& c : dir) c /= length;
    float up[3] = { 0, 1, 0 };
    if (std::fabs(dir[1]) > 0.9f) {
        up[0] = 1;
        up[1] = 0;
    }
    float side[3];
    Cross(dir, up, side);
    Normalize(side);
    float other[3];
    Cross(dir, side, other);
    float center[3];
    float base[4][3];
    for (int c = 0; c < 3; c++) center[c] = head[c] + dir[c] * radius;
    for (int k = 0; k < 4; k++) {
        float angle = 1.5707963f * static_cast<float>(k);
        for (int c = 0; c < 3; c++) base[k][c] = center[c] + (side[c] * std::cos(angle) + other[c] * std::sin(angle)) * radius;
    }
    for (int k = 0; k < 4; k++) {
        const float* next = base[(k + 1) % 4];
        if (style == 2) {
            PushFace(triangles, head, base[k], next, center, eye, color);
            PushFace(triangles, tail, next, base[k], center, eye, color);
        } else {
            PushLine(lines, head, base[k], color);
            PushLine(lines, base[k], tail, color);
            PushLine(lines, base[k], next, color);
        }
    }
}

void AppendJoint(int32_t style, const float* m, float radius, uint32_t color, const float eye[3],
                 std::vector<ViewerDebugVertex>& triangles, std::vector<ViewerDebugVertex>& lines) {
    const float center[3] = { m[12], m[13], m[14] };
    float axes[3][3];
    JointAxes(m, axes);
    if (style == 1) {
        for (int plane = 0; plane < 3; plane++) {
            const float* u = axes[plane];
            const float* v = axes[(plane + 1) % 3];
            float previous[3];
            for (int s = 0; s <= BONE_SPHERE_SIDES; s++) {
                float angle = 6.2831853f * static_cast<float>(s) / BONE_SPHERE_SIDES;
                float point[3];
                for (int c = 0; c < 3; c++) point[c] = center[c] + (u[c] * std::cos(angle) + v[c] * std::sin(angle)) * radius;
                if (s > 0) PushLine(lines, previous, point, color);
                std::copy(point, point + 3, previous);
            }
        }
    } else if (style == 2) {
        auto point = [&](int ring, int segment, float out[3]) {
            float theta = 3.14159265f * static_cast<float>(ring) / SOLID_SPHERE_RINGS;
            float phi = 6.2831853f * static_cast<float>(segment) / SOLID_SPHERE_SEGMENTS;
            out[0] = center[0] + std::sin(theta) * std::cos(phi) * radius;
            out[1] = center[1] + std::cos(theta) * radius;
            out[2] = center[2] + std::sin(theta) * std::sin(phi) * radius;
        };
        for (int ring = 0; ring < SOLID_SPHERE_RINGS; ring++) {
            for (int segment = 0; segment < SOLID_SPHERE_SEGMENTS; segment++) {
                float a[3], b[3], c[3], d[3];
                point(ring, segment, a);
                point(ring, segment + 1, b);
                point(ring + 1, segment, c);
                point(ring + 1, segment + 1, d);
                if (ring > 0) PushFace(triangles, a, b, c, center, eye, color);
                if (ring + 1 < SOLID_SPHERE_RINGS) PushFace(triangles, b, d, c, center, eye, color);
            }
        }
    } else if (style == 3) {
        for (const float* axis : axes) {
            float a[3];
            float b[3];
            for (int c = 0; c < 3; c++) {
                a[c] = center[c] - axis[c] * radius * 1.5f;
                b[c] = center[c] + axis[c] * radius * 1.5f;
            }
            PushLine(lines, a, b, color);
        }
    }
}

void AppendJointAxes(const float* m, float radius, float opacity, std::vector<ViewerDebugVertex>& lines) {
    const float center[3] = { m[12], m[13], m[14] };
    float axes[3][3];
    JointAxes(m, axes);
    for (int a = 0; a < 3; a++) {
        float end[3];
        for (int c = 0; c < 3; c++) end[c] = center[c] + axes[a][c] * radius * JOINT_AXIS_LENGTH;
        PushLine(lines, center, end, ScaleAlpha(JOINT_AXIS_COLORS[a], opacity));
    }
}

bool InJoints(int32_t joint, std::size_t count) {
    return joint >= 0 && static_cast<std::size_t>(joint) < count;
}

// Bounded: a malformed parent list must not loop forever.
bool Descends(const std::vector<int32_t>& parents, std::size_t joint, int32_t ancestor) {
    int32_t parent = parents[joint];
    for (int steps = 0; InJoints(parent, parents.size()) && steps < 1024; steps++, parent = parents[parent]) {
        if (parent == ancestor) return true;
    }
    return false;
}

uint32_t JointDepth(const std::vector<int32_t>& parents, std::size_t joint) {
    uint32_t depth = 0;
    for (int32_t parent = parents[joint]; InJoints(parent, parents.size()) && depth < 1024; parent = parents[parent]) depth++;
    return depth;
}

// Names like L_Arm, l_arm or Arm_L; RE7 and RE8 prefix the side.
uint32_t JointSide(std::string_view name) {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    auto sided = [&](char side) {
        const char prefix[3] = { side, '_', 0 };
        const char suffix[3] = { '_', side, 0 };
        const char middle[4] = { '_', side, '_', 0 };
        return lower.starts_with(prefix) || lower.ends_with(suffix) || lower.find(middle) != std::string::npos;
    };
    if (lower.starts_with("left") || sided('l')) return 1;
    if (lower.starts_with("right") || sided('r')) return 2;
    return 0;
}

std::vector<uint32_t> JointColors(const std::vector<std::string>& names, const std::vector<int32_t>& parents,
                                  const RaeSkeletonStyle& style, std::size_t skeleton) {
    std::vector<uint32_t> colors(parents.size(), style.color);
    for (std::size_t j = 0; j < colors.size(); j++) {
        if (style.colors == 1) colors[j] = SIDE_COLORS[j < names.size() ? JointSide(names[j]) : 0];
        if (style.colors == 2) colors[j] = JOINT_PALETTE[JointDepth(parents, j) % std::size(JOINT_PALETTE)];
        if (style.colors == 3) colors[j] = JOINT_PALETTE[skeleton % std::size(JOINT_PALETTE)];
    }
    return colors;
}

float Distance(const float a[3], const float b[3]) {
    float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    return Length(d);
}

}

void NativeViewport::StepMotion(float dt) {
    bool restart = false;
    {
        std::lock_guard lock(mutex);
        if (motionRequested) {
            motionRequested = false;
            motlist = requestedMotlist;
            motion = motlist && requestedMotion < motlist->motions.size() ? &motlist->motions[requestedMotion] : nullptr;
            restart = true;
        }
    }
    if (restart) {
        animators.clear();
        drivenJoints = 0;
        for (const SkeletonPose& pose : poses) {
            animators.emplace_back(pose.skeleton);
            std::size_t driven = animators.back().SetMotion(pose.follows < 0 ? motion : nullptr);
            drivenJoints += static_cast<uint32_t>(driven);
        }
        motionFrame = 0;
        motionPaused = false;
    }
    if (posesMoved && animators.size() != poses.size()) {
        animators.clear();
        for (const SkeletonPose& pose : poses) animators.emplace_back(pose.skeleton);
    }
    if (animators.size() != poses.size()) return;

    float rate = motion && motion->frameRate > 0 ? static_cast<float>(motion->frameRate) : 60.0f;
    float length = motion ? (motion->frameCount > 0 ? motion->frameCount : motion->endFrame) : 0.0f;
    int32_t pause = motionPauseRequest.exchange(-1);
    float seekTo = motionSeekRequest.exchange(-1);
    bool changed = restart || posesMoved;
    posesMoved = false;
    if (pause >= 0) {
        if (pause == 0 && motionPaused && motionFrame >= length) motionFrame = 0;
        motionPaused = pause != 0;
    }
    if (seekTo >= 0) {
        motionFrame = std::min(seekTo, length);
        changed = true;
    }
    if (motion && !motionPaused && length > 0) {
        motionFrame += dt * rate * std::clamp(motionSpeed.load(), 0.0f, 8.0f);
        if (motionFrame >= length) {
            if (motionLoop.load()) {
                motionFrame = std::fmod(motionFrame, length);
            } else {
                motionFrame = length;
                motionPaused = true;
            }
        }
        changed = true;
    }
    if (changed) {
        skinMatrices.assign(static_cast<std::size_t>(skinMatrixCount) * 12, 0.0f);
        for (std::size_t i = 0; i < poses.size(); i++) {
            SkeletonPose& pose = poses[i];
            if ((static_cast<std::size_t>(pose.jointOffset) + pose.skeleton->remap.size()) * 12 > skinMatrices.size()) continue;
            float* out = skinMatrices.data() + pose.jointOffset * 12;
            // Leaders come before their followers.
            if (pose.follows >= 0 && static_cast<std::size_t>(pose.follows) < i) {
                EvaluateFollower(*pose.skeleton, pose.world, pose.leaderJoint, poses[static_cast<std::size_t>(pose.follows)].joints,
                                 out, pose.joints);
            } else {
                animators[i].Evaluate(motionFrame, pose.world, out, &pose.joints);
            }
        }
        for (const SceneJointAttachment& attachment : attachments) {
            if (attachment.skinned < 0 || static_cast<std::size_t>(attachment.skinned) >= poses.size()) continue;
            const std::vector<Mat4>& joints = poses[static_cast<std::size_t>(attachment.skinned)].joints;
            if (attachment.joint < 0 || static_cast<std::size_t>(attachment.joint) >= joints.size()) continue;
            float world[12];
            ToFloat3x4(Mul(attachment.local, joints[static_cast<std::size_t>(attachment.joint)]), world);
            viewer->SetInstanceWorld(attachment.instance, world);
        }
        if (!skinMatrices.empty()) viewer->SetSkinningMatrices(skinMatrices);
        if (skeletons.load()) skeletonsDirty = true;
    }

    std::lock_guard lock(mutex);
    motionShown = motion != nullptr;
    motionShownFrame = motionFrame;
    motionShownCount = length;
    motionShownRate = rate;
    motionShownPaused = motionPaused;
    motionShownDriven = drivenJoints;
}

// Height is smoothed so a walk's bobbing does not shake the view.
void NativeViewport::FollowCharacter(float dt) {
    if (followJoint < 0 || static_cast<std::size_t>(followJoint) >= poses[0].joints.size()) return;
    const float* m = poses[0].joints[static_cast<std::size_t>(followJoint)].m;
    float target[3] = { m[12], m[13], m[14] };
    bool recenter = followRecenter.exchange(false);
    if (followValid) target[1] = followLast[1] + (target[1] - followLast[1]) * (1.0f - std::exp(-dt / 0.25f));
    if (!followCharacter.load() || (!followValid && !recenter)) {
        std::copy(target, target + 3, followLast);
        followValid = true;
        return;
    }
    float delta[3];
    if (recenter) {
        float pivot[3];
        camera.GetPivot(pivot);
        for (int i = 0; i < 3; i++) delta[i] = target[i] - pivot[i];
    } else {
        for (int i = 0; i < 3; i++) delta[i] = target[i] - followLast[i];
    }
    std::copy(target, target + 3, followLast);
    followValid = true;
    if (delta[0] == 0 && delta[1] == 0 && delta[2] == 0) return;
    camera.Translate(delta);
    float eye[3];
    float look[3];
    camera.GetEye(eye);
    camera.GetTarget(look);
    viewer->SetCamera(eye, look);
}

void NativeViewport::UpdateCollisionOverlay(float boundsMin[3], float boundsMax[3]) {
    TerrainData shown = *collision;
    auto hiddenLayer = [&](const auto& item) { return hiddenLayers.contains(item.layer); };
    std::erase_if(shown.triangles, hiddenLayer);
    std::erase_if(shown.spheres, hiddenLayer);
    std::erase_if(shown.capsules, hiddenLayer);
    std::erase_if(shown.boxes, hiddenLayer);
    DebugOverlay overlay;
    AppendTerrainOverlay(shown, overlay);
    viewer->SetDebugGeometry(overlay.triangles, overlay.lines);
    std::copy(overlay.boundsMin, overlay.boundsMin + 3, boundsMin);
    std::copy(overlay.boundsMax, overlay.boundsMax + 3, boundsMax);
}

void NativeViewport::UpdateAiMapOverlay(float boundsMin[3], float boundsMax[3]) {
    DebugOverlay overlay;
    if (aiMap) {
        AiMapStyle style;
        style.hidden = &hiddenAiMapParts;
        AppendAiMapOverlay(*aiMap, style, overlay);
    } else if (aiMapOverlayOn) {
        for (std::size_t i = 0; i < sceneAiMaps.size(); i++) {
            const SceneAiMap& map = sceneAiMaps[i];
            if (!allAiMaps && !shownAiMaps.contains(aiMapGroups[i].name)) continue;
            if (hiddenOwners.contains(map.owner)) continue;
            AiMapStyle style;
            const uint8_t* rgb = aiMapGroups[i].color;
            style.color = uint32_t{ rgb[0] } | uint32_t{ rgb[1] } << 8 | uint32_t{ rgb[2] } << 16 | 0xFF000000u;
            AppendAiMapOverlay(*map.data, style, overlay);
        }
    }
    viewer->SetDebugGeometry(overlay.triangles, overlay.lines);
    std::copy(overlay.boundsMin, overlay.boundsMin + 3, boundsMin);
    std::copy(overlay.boundsMax, overlay.boundsMax + 3, boundsMax);
}

void NativeViewport::SetSceneAiMaps(std::vector<SceneAiMap> maps) {
    sceneAiMaps = std::move(maps);
    aiMapGroups.clear();
    for (std::size_t i = 0; i < sceneAiMaps.size(); i++) {
        const SceneAiMap& map = sceneAiMaps[i];
        ViewportAiMapGroup group;
        std::string file = map.path.substr(map.path.find_last_of('/') + 1);
        group.name = file.substr(0, file.rfind('.'));
        group.type = AiMapTypeName(map.data->type);
        group.nodes = static_cast<uint32_t>(map.data->main.nodes.size() + map.data->secondary.nodes.size());
        const uint8_t* rgb = AI_MAP_COLORS[i % std::size(AI_MAP_COLORS)];
        std::copy(rgb, rgb + 3, group.color);
        aiMapGroups.push_back(std::move(group));
    }
    std::lock_guard lock(mutex);
    aiMapGroupCounts = aiMapGroups;
}

// Selected skeletons go last so they draw on top; solid ones back to front.
void NativeViewport::UpdateSkeletonOverlay(const float eye[3]) {
    RaeSkeletonStyle style;
    {
        std::lock_guard lock(mutex);
        style = skeletonStyle;
    }
    std::copy(eye, eye + 3, skeletonEye);
    skeletonSolid = false;
    std::vector<ViewerDebugVertex> triangles;
    std::vector<ViewerDebugVertex> lines;
    if (skeletons.load()) {
        skeletonSolid = style.bones == 2 || style.joints == 2;
        const std::string editedOwner = meshAsset && mainInstance < pick.owners.size() ? pick.owners[mainInstance] : std::string();
        int32_t requested = requestedJoint.load();
        struct Item {
            bool selected;
            float distance;
            uint32_t pose;
            uint32_t joint;
            bool bone;
        };
        std::vector<Item> items;
        std::vector<std::vector<uint32_t>> colors(poses.size());
        std::vector<int32_t> picks(poses.size(), -1);
        for (std::size_t i = 0; i < poses.size(); i++) {
            const SkeletonPose& pose = poses[i];
            if (!pose.owner.empty() && hiddenOwners.contains(pose.owner)) continue;
            std::size_t count = std::min(pose.joints.size(), pose.parents.size());
            bool selected = !meshAsset && !pose.owner.empty() && selectedOwners.contains(pose.owner);
            int32_t picked = meshAsset && pose.owner == editedOwner && InJoints(requested, count) ? requested : -1;
            picks[i] = picked;
            std::vector<uint8_t> shown(count, style.scope == 0 || selected ? 1 : 0);
            if (style.scope != 0 && picked >= 0) {
                shown[static_cast<std::size_t>(picked)] = 1;
                for (std::size_t j = 0; j < count; j++) {
                    if ((style.scope == 2 && Descends(pose.parents, static_cast<std::size_t>(picked), static_cast<int32_t>(j))) ||
                        (style.scope == 3 && Descends(pose.parents, j, picked))) {
                        shown[j] = 1;
                    }
                }
            }
            colors[i] = selected ? std::vector<uint32_t>(count, style.selectedColor)
                                 : JointColors(pose.skeleton->names, pose.parents, style, i);
            for (std::size_t j = 0; j < count; j++) {
                const float* center = pose.joints[j].m + 12;
                int32_t parent = pose.parents[j];
                if (style.bones != 0 && InJoints(parent, count) && shown[static_cast<std::size_t>(parent)] &&
                    (shown[j] || parent == picked)) {
                    const float* head = pose.joints[static_cast<std::size_t>(parent)].m + 12;
                    const float middle[3] = { (head[0] + center[0]) * 0.5f, (head[1] + center[1]) * 0.5f, (head[2] + center[2]) * 0.5f };
                    items.push_back({ selected, Distance(middle, eye), static_cast<uint32_t>(i), static_cast<uint32_t>(j), true });
                }
                if ((style.joints != 0 || style.axes != 0) && shown[j]) {
                    items.push_back({ selected, Distance(center, eye), static_cast<uint32_t>(i), static_cast<uint32_t>(j), false });
                }
            }
        }
        bool solid = skeletonSolid;
        std::stable_sort(items.begin(), items.end(), [solid](const Item& a, const Item& b) {
            if (a.selected != b.selected) return b.selected;
            return solid && a.distance > b.distance;
        });
        float scale = std::clamp(style.size, 0.05f, 20.0f);
        for (const Item& item : items) {
            const SkeletonPose& pose = poses[item.pose];
            float radius = pose.radius * scale;
            int32_t picked = picks[item.pose];
            const float* m = pose.joints[item.joint].m;
            if (item.bone) {
                auto parent = static_cast<std::size_t>(pose.parents[item.joint]);
                uint32_t color = static_cast<int32_t>(parent) == picked ? style.selectedColor : colors[item.pose][parent];
                AppendBone(style.bones, pose.joints[parent].m + 12, m + 12, radius, ScaleAlpha(color, style.opacity), eye,
                           triangles, lines);
            } else {
                uint32_t color = static_cast<int32_t>(item.joint) == picked ? style.selectedColor : colors[item.pose][item.joint];
                AppendJoint(style.joints, m, radius, ScaleAlpha(color, style.opacity), eye, triangles, lines);
                if (style.axes != 0) AppendJointAxes(m, radius, style.opacity, lines);
            }
        }
    }
    viewer->SetOverlayOcclusion(style.occlusion == 0 ? 1.0f : style.occlusion == 1 ? JOINT_DIMMED_ALPHA : 0.0f);
    viewer->SetOverlayGeometry(triangles, lines);
}

void NativeViewport::ResetEmptyCamera() {
    for (int i = 0; i < 3; i++) framing.center[i] = EMPTY_CENTER[i];
    framing.radius = EMPTY_RADIUS;
    camera = Camera();
    camera.Frame(framing.center, framing.radius);
}

void NativeViewport::RunJob(Job& job) {
    auto setCounts = [this](const ViewportScene* scene) {
        std::lock_guard lock(mutex);
        stats.draws = scene ? static_cast<uint32_t>(scene->build.draws.size()) : 0;
        stats.instances = scene ? static_cast<uint32_t>(scene->build.worlds.size()) : 0;
        stats.materials = scene ? static_cast<uint32_t>(scene->build.materials.size()) : 0;
        stats.textures = scene ? static_cast<uint32_t>(scene->textures.descs.size()) : 0;
        stats.lights = scene ? static_cast<uint32_t>(scene->build.lights.size()) : 0;
        stats.hasScene = scene != nullptr;
    };

    try {
        // Game PSOs are created once per Viewer, so every load starts from a fresh one.
        // The old swapchain must be released before the new one targets the same HWND.
        bool hadScene = hasScene;
        viewer.reset();
        hasScene = false;
        pick = PickGeometry{};
        effect.reset();
        sceneEffects.clear();
        effectPath.clear();
        effectTime = 0;
        bool hadImage = image.has_value();
        image.reset();
        movie.reset();
        {
            std::lock_guard lock(mutex);
            effectShown = false;
            effectShownParticles = 0;
            imageShown = false;
            movieShown = false;
        }
        meshAsset = false;
        mainInstance = 0;
        materialPreview = false;
        lodCount = 1;
        displayedLod = 0;
        hiddenInstances.clear();
        sceneLods = {};
        postProcessZones.reset();
        probeNetworks.reset();
        lodNear.clear();
        instanceRoom.clear();
        currentRoom = -1;
        roomGround = {};
        roomsNear.clear();
        roomProbe[0] = roomProbe[1] = roomProbe[2] = FLT_MAX;
        lodHiddenInstances.clear();
        effectZones.clear();
        insideEffectZone.clear();
        zoneInstances.clear();
        zoneHiddenInstances.clear();
        meshLods.clear();
        instanceLod.clear();
        drawShown.clear();
        poses.clear();
        attachments.clear();
        selectedOwners.clear();
        skeletonsDirty = true;
        collision.reset();
        hiddenLayers.clear();
        sceneCollision = SceneCollision{};
        filterGroups.clear();
        collisionGroups.clear();
        hiddenOwners.clear();
        colliderShown.clear();
        collisionDirty = false;
        aiMap.reset();
        hiddenAiMapParts.clear();
        sceneAiMaps.clear();
        aiMapGroups.clear();
        aiMapsAlways = false;
        aiMapOverlayOn = false;
        aiMapDirty = false;
        lightMarkers.clear();
        objects.clear();
        objectIndex.clear();
        objectChildren.clear();
        collidersByOwner.clear();
        lightsByOwner.clear();
        drawsByInstance.clear();
        meshLodByInstance.clear();
        sceneLights.clear();
        sceneLightScattering.clear();
        cameraLightScattering.clear();
        lightHidden.clear();
        mainDirectional.reset();
        directionalOwner.clear();
        directionalHidden = false;
        posesMoved = false;
        gizmo.EndDrag();
        activeObject.clear();
        undoStack.clear();
        redoStack.clear();
        undoState = 0;
        drawKind.clear();
        instanceSkinned.clear();
        measureValid = false;
        measureHasStart = false;
        if (!(job.scene && job.scene->keepCamera)) {
            viewKind = ViewportView::Perspective;
            viewShown = 0;
        }
        handleTriangles.clear();
        {
            std::lock_guard lock(mutex);
            collisionGroupCounts.clear();
            aiMapGroupCounts.clear();
            lightStates.clear();
        }
        animators.clear();
        motlist.reset();
        motion = nullptr;
        skinMatrixCount = 0;
        followJoint = -1;
        followValid = false;
        {
            std::lock_guard lock(mutex);
            motionShown = false;
        }
        selectedSubmesh = -1;
        hasSelectionBounds = false;
        setCounts(nullptr);
        {
            std::lock_guard lock(mutex);
            shownLod = 0;
            shownSubmesh = -1;
            shownObject.clear();
        }
        bool overlayOnly = job.scene && job.scene->build.draws.empty() &&
                           (job.scene->collision || job.scene->aiMap || !job.scene->build.aiMaps.empty());
        CreateBlankViewer(job.scene == nullptr || job.scene->effect.has_value() || overlayOnly);
        if (!job.scene) ResetEmptyCamera();
        if (overlayOnly) {
            ViewportScene& scene = *job.scene;
            // No mesh: the standalone passes draw the overlay over the sky.
            viewer->LoadEffectTextures(scene.textures.descs);
            float lo[3];
            float hi[3];
            if (scene.collision) {
                collision = std::move(scene.collision);
                UpdateCollisionOverlay(lo, hi);
            } else {
                aiMap = std::move(scene.aiMap);
                SetSceneAiMaps(std::move(scene.build.aiMaps));
                aiMapsAlways = true;
                aiMapOverlayOn = true;
                UpdateAiMapOverlay(lo, hi);
            }
            float extent = 0;
            for (int c = 0; c < 3; c++) {
                framing.center[c] = lo[c] <= hi[c] ? 0.5f * (lo[c] + hi[c]) : 0.0f;
                extent += lo[c] <= hi[c] ? (hi[c] - lo[c]) * (hi[c] - lo[c]) : 0.0f;
            }
            framing.radius = std::max(0.5f * std::sqrt(extent), 0.1f);
            viewer->SetClipPlanes(framing.radius * 0.01f, framing.radius * 100.0f);
            if (!scene.keepCamera || !hadScene) {
                camera = Camera();
                camera.Frame(framing.center, framing.radius);
            }
            hasScene = true;
            setCounts(&scene);
        } else if (job.scene && job.scene->image) {
            ViewportScene& scene = *job.scene;
            image = std::move(scene.image);
            if (!image->movie.empty()) {
                movie = std::make_unique<MoviePlayer>(*scene.game, image->movie);
                const MovieInfo& info = movie->Info();
                GameTextureDesc frame{ info.width, info.height, 87, 1, { { std::span(movie->Frame()), info.width * 4 } } };
                viewer->SetPreviewTextures({ frame });
                image->texture = 0;
            } else {
                viewer->SetPreviewTextures(scene.textures.descs);
            }
            if (!scene.keepCamera || !hadImage) {
                imageView = ViewerImageView{};
                imageView.zoom = IMAGE_FIT;
                imageSequence = image->atlas && !image->uvs.sequences.empty() ? 0 : -1;
                imagePattern = -1;
            }
            imageView.texture = image->texture;
            flipbookTime = 0;
            hasScene = true;
            setCounts(&scene);
            std::lock_guard lock(mutex);
            imageShown = true;
            shownLod = imageSequence;
            shownSubmesh = imagePattern;
        } else if (job.scene && job.scene->effect) {
            ViewportScene& scene = *job.scene;
            viewer->LoadEffectTextures(scene.textures.descs);
            viewer->SetMaterialPrograms(scene.effect->programs);
            effect = std::make_unique<EffectInstance>(scene.effect->data, scene.effect->assets, Identity(), 0x5EED);
            effectPath = scene.effect->path;

            EffectInstance probe = *effect;
            float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
            float hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            for (int frame = 0; frame < EFFECT_FRAMING_FRAMES; frame++) {
                probe.Update(1.0f / 60.0f);
                particles.clear();
                probe.Collect(particles);
                for (const ViewerParticle& p : particles) {
                    float half = 0.5f * std::max(std::fabs(p.size[0]), std::fabs(p.size[1]));
                    for (int c = 0; c < 3; c++) {
                        lo[c] = std::min(lo[c], std::min(p.position[c], p.position[c] + p.axis[c]) - half);
                        hi[c] = std::max(hi[c], std::max(p.position[c], p.position[c] + p.axis[c]) + half);
                    }
                }
            }
            if (lo[0] <= hi[0]) {
                float extent = 0;
                for (int c = 0; c < 3; c++) {
                    framing.center[c] = 0.5f * (lo[c] + hi[c]);
                    extent += (hi[c] - lo[c]) * (hi[c] - lo[c]);
                }
                framing.radius = std::clamp(0.5f * std::sqrt(extent), 0.25f, 50.0f);
            } else {
                for (int i = 0; i < 3; i++) framing.center[i] = EMPTY_CENTER[i];
                framing.radius = EMPTY_RADIUS;
            }
            if (!scene.keepCamera || !hadScene) {
                camera = Camera();
                camera.Frame(framing.center, framing.radius);
            }
            viewer->SetParticleIntensity(1.0f);
            hasScene = true;
            setCounts(&scene);
            std::lock_guard lock(mutex);
            effectShown = true;
        } else if (job.scene) {
            ViewportScene& scene = *job.scene;
            scene.look.unlit = shading.load() != ViewportShading::Lit;
            // Reuses the sky already decoded for the empty viewport.
            std::shared_ptr<const TextureData> skyTexture;
            float intensity;
            {
                std::lock_guard lock(mutex);
                skyTexture = sky;
                intensity = skyIntensity;
                skyChanged = false;
            }
            if (skyTexture) scene.look.skyPath.clear();
            UploadScene(*viewer, *scene.game, scene.build, scene.textures, scene.look);
            viewer->SetMaterialPrograms(std::move(scene.build.materialPrograms));
            if (skyTexture) {
                viewer->LoadSky(ToGameTexture(*skyTexture));
                viewer->SetSkyIntensity(intensity);
            }
            framing = FrameScene(*viewer, scene.build.worlds, !scene.frameWhole);
            for (const SkinnedInstance& instance : scene.build.skinned) {
                SkeletonPose pose;
                pose.owner = instance.owner;
                pose.skeleton = instance.skeleton;
                pose.world = instance.world;
                pose.jointOffset = instance.jointOffset;
                pose.joints = BindPoseWorld(*instance.skeleton, instance.world);
                pose.parents = instance.skeleton->parents;
                std::vector<float> lengths;
                for (std::size_t j = 0; j < pose.joints.size(); j++) {
                    int32_t parent = pose.parents[j];
                    if (parent < 0 || static_cast<std::size_t>(parent) >= pose.joints.size()) continue;
                    const float* a = pose.joints[j].m;
                    const float* b = pose.joints[static_cast<std::size_t>(parent)].m;
                    float d[3] = { a[12] - b[12], a[13] - b[13], a[14] - b[14] };
                    if (float length = Length(d); length > 1e-4f) lengths.push_back(length);
                }
                float typical = 0.1f;
                if (!lengths.empty()) {
                    std::nth_element(lengths.begin(), lengths.begin() + lengths.size() / 2, lengths.end());
                    typical = lengths[lengths.size() / 2];
                }
                pose.radius = std::clamp(typical * 0.1f, 0.003f, 0.05f);
                if (instance.follows >= 0 && static_cast<std::size_t>(instance.follows) < poses.size()) {
                    pose.follows = instance.follows;
                    pose.leaderJoint = MatchJoints(*pose.skeleton, *poses[static_cast<std::size_t>(instance.follows)].skeleton);
                }
                poses.push_back(std::move(pose));
            }
            attachments = std::move(scene.build.attachments);
            skinMatrixCount = scene.build.skinMatrixCount;
            // RE7 and RE8 skeletons start root, Null_Offset, Hip.
            if (!poses.empty()) {
                const std::vector<std::string>& names = poses[0].skeleton->names;
                for (std::size_t j = 0; j < names.size() && followJoint < 0; j++) {
                    std::string name = names[j];
                    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (name == "hip" || name == "hips" || name == "pelvis" || name == "cog") followJoint = static_cast<int32_t>(j);
                }
                if (followJoint < 0 && !names.empty()) followJoint = 0;
            }
            {
                // A reload keeps playing the current motion.
                std::lock_guard lock(mutex);
                if (scene.keepCamera && requestedMotlist && !motionRequested) motionRequested = true;
            }
            if (!scene.keepCamera || !hadScene) {
                camera = Camera();
                camera.Frame(framing.center, framing.radius);
            }
            hasScene = true;
            setCounts(&scene);

            // The upload copied the buffers; the build is dropped with the job.
            pick.positions = std::move(scene.build.positions);
            pick.indices = std::move(scene.build.indices);
            pick.draws = std::move(scene.build.draws);
            pick.tags = std::move(scene.build.drawTags);
            pick.worlds = std::move(scene.build.worlds);
            pick.owners = std::move(scene.build.instanceOwners);
            pick.meshes = std::move(scene.build.instanceMeshes);
            pick.instanceMaterials = std::move(scene.build.instanceMaterials);
            pick.materialNames = std::move(scene.build.materialNames);
            for (std::size_t i = 0; i < pick.owners.size(); i++) {
                if (!pick.owners[i].empty()) pick.instancesByOwner[pick.owners[i]].push_back(static_cast<uint32_t>(i));
            }
            for (SceneMaterialParam& param : scene.build.materialParams) pick.params.emplace(param.key, param);
            sceneLods = std::move(scene.build.lods);
            if (!scene.build.postProcessZones.empty()) postProcessZones = std::make_unique<PostProcessZones>(scene.build);
            if (auto networks = std::make_unique<LightProbeNetworks>(*scene.game, scene.build.lightProbes); networks->Multiple()) {
                probeNetworks = std::move(networks);
            }
            if (!scene.build.lightStates.empty()) {
                std::lock_guard lock(mutex);
                lightStates = { scene.build.lightState };
                lightStates.insert(lightStates.end(), scene.build.lightStates.begin(), scene.build.lightStates.end());
            }
            for (const SceneLodMember& member : sceneLods.members) {
                if (sceneLods.groups[member.group].test != SceneLodTest::Room) continue;
                if (instanceRoom.empty()) instanceRoom.assign(pick.worlds.size(), -1);
                if (member.instance < instanceRoom.size()) instanceRoom[member.instance] = static_cast<int32_t>(member.group);
            }
            meshLods = std::move(scene.build.meshLods);
            instanceLod.assign(pick.worlds.size(), 0);
            if (!meshLods.empty()) UpdateDrawMask();
            if (!scene.build.hiddenObjects.empty()) {
                hiddenOwners = std::set<std::string>(scene.build.hiddenObjects.begin(), scene.build.hiddenObjects.end());
                hiddenInstances.assign(pick.worlds.size(), 0);
                for (const std::string& key : hiddenOwners) {
                    auto found = pick.instancesByOwner.find(key);
                    if (found == pick.instancesByOwner.end()) continue;
                    for (uint32_t instance : found->second) hiddenInstances[instance] = 1;
                }
                collisionDirty = true;
                aiMapDirty = true;
                UpdateDrawMask();
            }
            for (std::size_t i = 0; i < scene.build.effects.size(); i++) {
                const SceneEffect& placed = scene.build.effects[i];
                const LoadedEffect& fx = scene.build.effectAssets[placed.asset];
                sceneEffects.push_back({ EffectInstance(fx.data, fx.assets, placed.world, 0x5EED + static_cast<uint32_t>(i)), placed.owner,
                                         { placed.world.m[12], placed.world.m[13], placed.world.m[14] }, placed.loopFrames, 0,
                                         placed.zones, placed.zones.empty() });
            }
            effectZones = std::move(scene.build.effectZones);
            insideEffectZone.assign(effectZones.size(), 0);
            zoneInstances = std::move(scene.build.zoneInstances);
            zoneHiddenInstances.assign(pick.worlds.size(), 0);
            for (const SceneZoneInstance& z : zoneInstances) {
                if (z.instance < zoneHiddenInstances.size()) zoneHiddenInstances[z.instance] = 1;
            }
            if (!zoneInstances.empty()) UpdateDrawMask();
            meshAsset = scene.meshAsset;
            mainInstance = scene.mainInstance;
            materialPreview = scene.materialPreview;
            lodCount = scene.lodCount;
            if (meshAsset) ShowLod(0);
            if (materialPreview) {
                const float center[3] = { 0, 0, 0 };
                viewer->SetPreviewSphere(center, 0.5f);
            }
            if (scene.collision) {
                collision = std::move(scene.collision);
                float lo[3];
                float hi[3];
                UpdateCollisionOverlay(lo, hi);
            }
            if (!scene.build.collision.colliders.empty()) {
                sceneCollision = std::move(scene.build.collision);
                viewer->SetCollisionGeometry(sceneCollision.positions, sceneCollision.indices);
                BuildRoomGround();
                for (const std::string& filter : sceneCollision.filters) {
                    std::string name = CollisionFilterGroup(filter);
                    auto found = std::find_if(collisionGroups.begin(), collisionGroups.end(),
                                              [&](const ViewportCollisionGroup& g) { return g.name == name; });
                    filterGroups.push_back(static_cast<uint32_t>(found - collisionGroups.begin()));
                    if (found != collisionGroups.end()) continue;
                    ViewportCollisionGroup group{ name, 0, { COLLISION_OTHER_COLOR[0], COLLISION_OTHER_COLOR[1], COLLISION_OTHER_COLOR[2] } };
                    for (const CollisionGroupColor& entry : COLLISION_GROUP_COLORS) {
                        if (name == entry.group) std::copy(entry.rgb, entry.rgb + 3, group.color);
                    }
                    for (const char* volume : COLLISION_VOLUME_GROUPS) group.volume |= name == volume;
                    collisionGroups.push_back(std::move(group));
                }
                for (const SceneCollider& collider : sceneCollision.colliders) collisionGroups[filterGroups[collider.filter]].colliders++;
                UpdateCollisionInstances();
                std::lock_guard lock(mutex);
                collisionGroupCounts = collisionGroups;
            }
            if (!scene.build.aiMaps.empty()) SetSceneAiMaps(std::move(scene.build.aiMaps));
            lightMarkers = std::move(scene.build.lightMarkers);
            objects = std::move(scene.build.objects);
            objectChildren.assign(objects.size(), {});
            for (std::size_t i = 0; i < objects.size(); i++) {
                objectIndex[objects[i].key] = static_cast<int32_t>(i);
                if (objects[i].parent >= 0) objectChildren[static_cast<std::size_t>(objects[i].parent)].push_back(static_cast<int32_t>(i));
            }
            for (std::size_t i = 0; i < sceneCollision.colliders.size(); i++) {
                collidersByOwner[sceneCollision.colliders[i].owner].push_back(static_cast<uint32_t>(i));
            }
            for (std::size_t i = 0; i < lightMarkers.size(); i++) lightsByOwner[lightMarkers[i].owner].push_back(static_cast<uint32_t>(i));
            drawsByInstance.assign(pick.worlds.size(), {});
            for (std::size_t id = 0; id < pick.draws.size(); id++) {
                if (pick.draws[id].instanceIndex < drawsByInstance.size()) {
                    drawsByInstance[pick.draws[id].instanceIndex].push_back(static_cast<uint32_t>(id));
                }
            }
            meshLodByInstance.assign(pick.worlds.size(), -1);
            for (std::size_t i = 0; i < meshLods.size(); i++) {
                if (meshLods[i].instance < meshLodByInstance.size()) meshLodByInstance[meshLods[i].instance] = static_cast<int32_t>(i);
            }
            instanceSkinned.assign(pick.worlds.size(), 0);
            for (const SkinnedInstance& skinned : scene.build.skinned) {
                if (skinned.instance < instanceSkinned.size()) instanceSkinned[skinned.instance] = 1;
            }
            ClassifyDraws();
            UpdateDrawMask();
            sceneLights = std::move(scene.build.lights);
            sceneLightScattering = std::move(scene.build.lightScattering);
            cameraLightScattering = std::move(scene.build.cameraLightScattering);
            lightHidden.assign(sceneLights.size(), 0);
            if (!scene.build.directionalLights.empty()) mainDirectional = scene.build.directionalLights.front();
            directionalOwner = std::move(scene.build.directionalOwner);
            UpdateLightVisibility();
        }
        job.done.set_value();
    } catch (...) {
        viewer.reset();
        hasScene = false;
        try {
            CreateBlankViewer();
        } catch (const std::exception& e) {
            LogError("viewport: %s", e.what());
            deviceLost = true;
        }
        job.done.set_exception(std::current_exception());
    }
}

bool NativeViewport::PickRay(int x, int y, float eye[3], float dir[3]) const {
    uint32_t width = viewer->GetWidth();
    uint32_t height = viewer->GetHeight();
    if (width == 0 || height == 0) return false;
    float target[3];
    camera.GetEye(eye);
    camera.GetTarget(target);
    float forward[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
    Normalize(forward);
    const float worldUp[3] = { 0, 1, 0 };
    float right[3];
    Cross(forward, worldUp, right);
    Normalize(right);
    float up[3];
    Cross(right, forward, up);
    float tanHalf = std::tan(camera.GetFov() * 0.5f);
    float nx = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(width) - 1.0f) * tanHalf *
               static_cast<float>(width) / static_cast<float>(height);
    float ny = (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(height)) * tanHalf;
    for (int i = 0; i < 3; i++) dir[i] = forward[i] + right[i] * nx + up[i] * ny;
    Normalize(dir);
    return true;
}

// Skinned meshes are tested in bind pose.
int32_t NativeViewport::PickDraw(int x, int y, float* distance) const {
    float eye[3];
    float dir[3];
    if (distance) *distance = FLT_MAX;
    if (pick.draws.empty() || !PickRay(x, y, eye, dir)) return -1;

    const float* positions = reinterpret_cast<const float*>(pick.positions.data());
    const uint16_t* indices = reinterpret_cast<const uint16_t*>(pick.indices.data());
    int64_t vertexCount = static_cast<int64_t>(pick.positions.size() / 12);
    std::size_t indexCount = pick.indices.size() / 2;
    float best = FLT_MAX;
    int32_t hit = -1;
    for (std::size_t id = 0; id < pick.draws.size(); id++) {
        const ViewerMeshDraw& draw = pick.draws[id];
        if (!drawShown.empty() && !drawShown[id]) continue;
        if (draw.instanceIndex >= pick.worlds.size()) continue;
        if (draw.boundsRadius > 0) {
            float oc[3] = { draw.boundsCenter[0] - eye[0], draw.boundsCenter[1] - eye[1], draw.boundsCenter[2] - eye[2] };
            float along = Dot(oc, dir);
            float dist2 = Dot(oc, oc) - along * along;
            float r2 = draw.boundsRadius * draw.boundsRadius;
            if (dist2 > r2) continue;
            float half = std::sqrt(r2 - dist2);
            if (along + half < 0 || along - half > best) continue;
        }

        // Row-major 3x4 world; the ray goes to mesh space so t stays in world units.
        const float* m = pick.worlds[draw.instanceIndex].m;
        float a = m[0], b = m[1], c = m[2];
        float d = m[4], e = m[5], f = m[6];
        float g = m[8], h = m[9], k = m[10];
        float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
        if (std::fabs(det) < 1e-20f) continue;
        float inv[9] = { (e * k - f * h) / det, (c * h - b * k) / det, (b * f - c * e) / det,
                         (f * g - d * k) / det, (a * k - c * g) / det, (c * d - a * f) / det,
                         (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det };
        float rel[3] = { eye[0] - m[3], eye[1] - m[7], eye[2] - m[11] };
        float localOrigin[3];
        float localDir[3];
        for (int r = 0; r < 3; r++) {
            localOrigin[r] = inv[r * 3] * rel[0] + inv[r * 3 + 1] * rel[1] + inv[r * 3 + 2] * rel[2];
            localDir[r] = inv[r * 3] * dir[0] + inv[r * 3 + 1] * dir[1] + inv[r * 3 + 2] * dir[2];
        }

        std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(draw.startIndex) + draw.indexCount, indexCount);
        for (std::size_t i = draw.startIndex; i + 2 < end; i += 3) {
            int64_t v0 = draw.baseVertex + indices[i];
            int64_t v1 = draw.baseVertex + indices[i + 1];
            int64_t v2 = draw.baseVertex + indices[i + 2];
            if (v0 < 0 || v1 < 0 || v2 < 0 || v0 >= vertexCount || v1 >= vertexCount || v2 >= vertexCount) continue;
            float t;
            if (IntersectTriangle(localOrigin, localDir, positions + v0 * 3, positions + v1 * 3, positions + v2 * 3, t) &&
                t < best) {
                best = t;
                hit = static_cast<int32_t>(id);
            }
        }
    }
    if (distance) *distance = best;
    return hit;
}

float NativeViewport::RaycastMeshes(const float origin[3], const float dir[3], const std::vector<uint8_t>* skipInstances) const {
    const float* positions = reinterpret_cast<const float*>(pick.positions.data());
    const uint16_t* indices = reinterpret_cast<const uint16_t*>(pick.indices.data());
    int64_t vertexCount = static_cast<int64_t>(pick.positions.size() / 12);
    std::size_t indexCount = pick.indices.size() / 2;
    float best = FLT_MAX;
    for (std::size_t id = 0; id < pick.draws.size(); id++) {
        const ViewerMeshDraw& draw = pick.draws[id];
        if (!drawShown.empty() && !drawShown[id]) continue;
        if (draw.instanceIndex >= pick.worlds.size()) continue;
        if (skipInstances && draw.instanceIndex < skipInstances->size() && (*skipInstances)[draw.instanceIndex]) continue;
        if (draw.boundsRadius > 0) {
            float oc[3] = { draw.boundsCenter[0] - origin[0], draw.boundsCenter[1] - origin[1], draw.boundsCenter[2] - origin[2] };
            float along = Dot(oc, dir);
            float dist2 = Dot(oc, oc) - along * along;
            float r2 = draw.boundsRadius * draw.boundsRadius;
            if (dist2 > r2) continue;
            float half = std::sqrt(r2 - dist2);
            if (along + half < 0 || along - half > best) continue;
        }
        const float* m = pick.worlds[draw.instanceIndex].m;
        float a = m[0], b = m[1], c = m[2];
        float d = m[4], e = m[5], f = m[6];
        float g = m[8], h = m[9], k = m[10];
        float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
        if (std::fabs(det) < 1e-20f) continue;
        float inv[9] = { (e * k - f * h) / det, (c * h - b * k) / det, (b * f - c * e) / det,
                         (f * g - d * k) / det, (a * k - c * g) / det, (c * d - a * f) / det,
                         (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det };
        float rel[3] = { origin[0] - m[3], origin[1] - m[7], origin[2] - m[11] };
        float localOrigin[3];
        float localDir[3];
        for (int r = 0; r < 3; r++) {
            localOrigin[r] = inv[r * 3] * rel[0] + inv[r * 3 + 1] * rel[1] + inv[r * 3 + 2] * rel[2];
            localDir[r] = inv[r * 3] * dir[0] + inv[r * 3 + 1] * dir[1] + inv[r * 3 + 2] * dir[2];
        }
        std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(draw.startIndex) + draw.indexCount, indexCount);
        for (std::size_t i = draw.startIndex; i + 2 < end; i += 3) {
            int64_t v0 = draw.baseVertex + indices[i];
            int64_t v1 = draw.baseVertex + indices[i + 1];
            int64_t v2 = draw.baseVertex + indices[i + 2];
            if (v0 < 0 || v1 < 0 || v2 < 0 || v0 >= vertexCount || v1 >= vertexCount || v2 >= vertexCount) continue;
            float t;
            if (IntersectTriangle(localOrigin, localDir, positions + v0 * 3, positions + v1 * 3, positions + v2 * 3, t) && t < best) {
                best = t;
            }
        }
    }
    return best;
}

std::string NativeViewport::PickCollider(int x, int y) const {
    float eye[3];
    float dir[3];
    if (sceneCollision.colliders.empty() || !PickRay(x, y, eye, dir)) return {};
    const float* positions = sceneCollision.positions.data();
    const uint32_t* indices = sceneCollision.indices.data();
    float best = FLT_MAX;
    const SceneCollider* hit = nullptr;
    for (std::size_t i = 0; i < sceneCollision.colliders.size(); i++) {
        if (i >= colliderShown.size() || !colliderShown[i]) continue;
        const SceneCollider& collider = sceneCollision.colliders[i];
        const CollisionGeometry& geometry = sceneCollision.geometries[collider.geometry];
        // Row vectors: world = local * A + T, so local = (world - T) * inverse(A).
        const float* m = collider.world.m;
        float a = m[0], b = m[1], c = m[2];
        float d = m[4], e = m[5], f = m[6];
        float g = m[8], h = m[9], k = m[10];
        float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
        if (std::fabs(det) < 1e-20f) continue;
        float inv[9] = { (e * k - f * h) / det, (c * h - b * k) / det, (b * f - c * e) / det,
                         (f * g - d * k) / det, (a * k - c * g) / det, (c * d - a * f) / det,
                         (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det };
        float rel[3] = { eye[0] - m[12], eye[1] - m[13], eye[2] - m[14] };
        float localOrigin[3];
        float localDir[3];
        for (int col = 0; col < 3; col++) {
            localOrigin[col] = rel[0] * inv[col] + rel[1] * inv[3 + col] + rel[2] * inv[6 + col];
            localDir[col] = dir[0] * inv[col] + dir[1] * inv[3 + col] + dir[2] * inv[6 + col];
        }
        if (!IntersectBox(localOrigin, localDir, geometry.boundsMin, geometry.boundsMax, best)) continue;
        uint32_t end = geometry.firstIndex + geometry.indexCount;
        for (uint32_t t = geometry.firstIndex; t + 2 < end; t += 3) {
            const float* v0 = positions + (static_cast<std::size_t>(geometry.baseVertex) + indices[t]) * 3;
            const float* v1 = positions + (static_cast<std::size_t>(geometry.baseVertex) + indices[t + 1]) * 3;
            const float* v2 = positions + (static_cast<std::size_t>(geometry.baseVertex) + indices[t + 2]) * 3;
            float distance;
            if (IntersectTriangle(localOrigin, localDir, v0, v1, v2, distance) && distance < best) {
                best = distance;
                hit = &collider;
            }
        }
    }
    return hit ? hit->owner : std::string();
}

bool NativeViewport::ProjectToScreen(const float point[3], float& x, float& y, float& depth) const {
    uint32_t width = viewer->GetWidth();
    uint32_t height = viewer->GetHeight();
    if (width == 0 || height == 0) return false;
    float eye[3];
    float target[3];
    camera.GetEye(eye);
    camera.GetTarget(target);
    float forward[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
    Normalize(forward);
    const float worldUp[3] = { 0, 1, 0 };
    float right[3];
    Cross(forward, worldUp, right);
    Normalize(right);
    float up[3];
    Cross(right, forward, up);
    float rel[3] = { point[0] - eye[0], point[1] - eye[1], point[2] - eye[2] };
    depth = Dot(rel, forward);
    if (depth <= 1e-3f) return false;
    float tanHalf = std::tan(camera.GetFov() * 0.5f);
    float aspect = static_cast<float>(width) / static_cast<float>(height);
    float nx = Dot(rel, right) / (depth * tanHalf * aspect);
    float ny = Dot(rel, up) / (depth * tanHalf);
    x = (nx + 1.0f) * 0.5f * static_cast<float>(width);
    y = (1.0f - ny) * 0.5f * static_cast<float>(height);
    return true;
}

bool NativeViewport::LightIconShown(const SceneLightMarker& light, uint32_t mask) const {
    return (mask & (1u << static_cast<uint32_t>(light.kind))) != 0 && !hiddenOwners.contains(light.owner);
}

// As the viewer draws them: sized by distance, gone once faded out or behind a mesh.
int32_t NativeViewport::PickLight(int x, int y, bool meshesShown) const {
    float wall = FLT_MAX;
    if (meshesShown) PickDraw(x, y, &wall);
    float eye[3];
    camera.GetEye(eye);
    float pixelsPerMeter = static_cast<float>(viewer->GetHeight()) * 0.5f / std::tan(camera.GetFov() * 0.5f);
    float best = FLT_MAX;
    int32_t hit = -1;
    for (std::size_t i = 0; i < lightMarkers.size(); i++) {
        const SceneLightMarker& light = lightMarkers[i];
        if (!LightIconShown(light, lightIconMask)) continue;
        float sx;
        float sy;
        float depth;
        if (!ProjectToScreen(light.position, sx, sy, depth)) continue;
        float pixels = std::min(LIGHT_ICON_MAX_PIXELS, LIGHT_ICON_WORLD_SIZE * pixelsPerMeter / depth);
        if (selectedOwners.contains(light.owner)) {
            pixels = std::max(pixels, LIGHT_ICON_FADE_BEGIN_PIXELS);
        } else if (pixels <= LIGHT_ICON_FADE_END_PIXELS) {
            continue;
        }
        float d[3] = { light.position[0] - eye[0], light.position[1] - eye[1], light.position[2] - eye[2] };
        if (wall < std::sqrt(Dot(d, d)) - LIGHT_ICON_OCCLUSION_MARGIN) continue;
        float dx = sx - static_cast<float>(x);
        float dy = sy - static_cast<float>(y);
        float radius = pixels * 0.5f + LIGHT_ICON_PICK_SLACK;
        if (dx * dx + dy * dy > radius * radius || depth >= best) continue;
        best = depth;
        hit = static_cast<int32_t>(i);
    }
    return hit;
}

bool NativeViewport::LightsBounds(const std::set<std::string>& owners, float center[3], float& radius) const {
    float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
    float hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    bool any = false;
    for (const SceneLightMarker& light : lightMarkers) {
        if (!owners.contains(light.owner)) continue;
        for (int c = 0; c < 3; c++) {
            lo[c] = std::min(lo[c], light.position[c]);
            hi[c] = std::max(hi[c], light.position[c]);
        }
        any = true;
    }
    if (!any) return false;
    float extent = 0;
    for (int c = 0; c < 3; c++) {
        center[c] = (lo[c] + hi[c]) * 0.5f;
        extent += (hi[c] - lo[c]) * (hi[c] - lo[c]);
    }
    radius = std::max(std::sqrt(extent) * 0.5f, LIGHT_FRAME_RADIUS);
    return true;
}

namespace {

struct StatsLine {
    std::string text;
    float rgb[3];
};

// Premultiplied RGBA8 of a dark panel holding the lines, rasterized with GDI.
std::vector<uint32_t> RenderStatsPanel(const std::vector<StatsLine>& lines, float scale, uint32_t& width, uint32_t& height) {
    constexpr float PANEL_GRAY = 0.06f;
    constexpr float PANEL_ALPHA = 0.62f;
    int pad = static_cast<int>(std::lround(6 * scale));
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-static_cast<int>(std::lround(12 * scale)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Consolas");
    HGDIOBJ oldFont = SelectObject(dc, font);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    int lineHeight = metrics.tmHeight;
    int textWidth = 0;
    for (const StatsLine& line : lines) {
        SIZE size{};
        GetTextExtentPoint32A(dc, line.text.c_str(), static_cast<int>(line.text.size()), &size);
        textWidth = std::max(textWidth, static_cast<int>(size.cx));
    }
    width = static_cast<uint32_t>(textWidth + 2 * pad);
    height = static_cast<uint32_t>(lineHeight * static_cast<int>(lines.size()) + 2 * pad);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    std::vector<uint32_t> out;
    if (bitmap && bits) {
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        std::memset(bits, 0, static_cast<std::size_t>(width) * height * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        for (std::size_t i = 0; i < lines.size(); i++) {
            TextOutA(dc, pad, pad + static_cast<int>(i) * lineHeight, lines[i].text.c_str(), static_cast<int>(lines[i].text.size()));
        }
        GdiFlush();
        const uint8_t* pixels = static_cast<const uint8_t*>(bits);
        out.resize(static_cast<std::size_t>(width) * height);
        for (uint32_t y = 0; y < height; y++) {
            int row = std::clamp((static_cast<int>(y) - pad) / std::max(lineHeight, 1), 0, static_cast<int>(lines.size()) - 1);
            const float* text = lines[row].rgb;
            for (uint32_t x = 0; x < width; x++) {
                std::size_t i = static_cast<std::size_t>(y) * width + x;
                float coverage = pixels[i * 4 + 1] / 255.0f;
                float alpha = PANEL_ALPHA + coverage * (1 - PANEL_ALPHA);
                uint32_t packed = static_cast<uint32_t>(alpha * 255 + 0.5f) << 24;
                for (int ch = 0; ch < 3; ch++) {
                    float value = PANEL_GRAY * PANEL_ALPHA * (1 - coverage) + text[ch] * coverage;
                    packed |= static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255 + 0.5f) << (8 * ch);
                }
                out[i] = packed;
            }
        }
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    DeleteDC(dc);
    return out;
}

void AddLine(std::vector<ViewerDebugVertex>& lines, const float a[3], const float b[3], uint32_t color) {
    lines.push_back({ { a[0], a[1], a[2] }, color });
    lines.push_back({ { b[0], b[1], b[2] }, color });
}

// Points center + (u cos t + v sin t) * radius for t in [begin, end].
void AddArc(std::vector<ViewerDebugVertex>& lines, const float center[3], const float u[3], const float v[3], float radius,
            float begin, float end, int segments, uint32_t color) {
    float previous[3];
    for (int s = 0; s <= segments; s++) {
        float t = begin + (end - begin) * static_cast<float>(s) / static_cast<float>(segments);
        float point[3];
        for (int c = 0; c < 3; c++) point[c] = center[c] + (u[c] * std::cos(t) + v[c] * std::sin(t)) * radius;
        if (s > 0) AddLine(lines, previous, point, color);
        std::copy(point, point + 3, previous);
    }
}

void Basis(const float dir[3], float u[3], float v[3]) {
    const float helper[3] = { std::fabs(dir[1]) < 0.9f ? 0.0f : 1.0f, std::fabs(dir[1]) < 0.9f ? 1.0f : 0.0f, 0 };
    Cross(dir, helper, u);
    Normalize(u);
    Cross(dir, u, v);
}

// Unreal's light radius and cone colours.
constexpr uint32_t GIZMO_OUTER_COLOR = 0xF0FFFFC8u;
constexpr uint32_t GIZMO_INNER_COLOR = 0xB4FFC896u;
constexpr float TWO_PI = 6.2831853f;

void AddSpotCone(std::vector<ViewerDebugVertex>& lines, const SceneLightMarker& light, float halfAngle, int spokes,
                 bool cap, uint32_t color) {
    float u[3];
    float v[3];
    Basis(light.direction, u, v);
    float ringCenter[3];
    for (int c = 0; c < 3; c++) ringCenter[c] = light.position[c] + light.direction[c] * light.range * std::cos(halfAngle);
    float ringRadius = light.range * std::sin(halfAngle);
    AddArc(lines, ringCenter, u, v, ringRadius, 0, TWO_PI, 48, color);
    for (int k = 0; k < spokes; k++) {
        float t = TWO_PI * static_cast<float>(k) / static_cast<float>(spokes);
        float end[3];
        for (int c = 0; c < 3; c++) end[c] = ringCenter[c] + (u[c] * std::cos(t) + v[c] * std::sin(t)) * ringRadius;
        AddLine(lines, light.position, end, color);
    }
    if (!cap) return;
    AddArc(lines, light.position, light.direction, u, light.range, -halfAngle, halfAngle, 24, color);
    AddArc(lines, light.position, light.direction, v, light.range, -halfAngle, halfAngle, 24, color);
}

}

void NativeViewport::UpdateLightGizmos(uint32_t mask, const float eye[3]) {
    if (mask == 0 && lightIconMask == 0) return;
    lightIconMask = mask;
    std::vector<ViewerLightIcon> icons;
    std::vector<ViewerDebugVertex> lines;
    if (mask != 0) {
        // Back to front, selected icons on top.
        std::vector<std::tuple<bool, float, uint32_t>> order;
        for (uint32_t i = 0; i < lightMarkers.size(); i++) {
            const SceneLightMarker& light = lightMarkers[i];
            if (!LightIconShown(light, mask)) continue;
            float d[3] = { light.position[0] - eye[0], light.position[1] - eye[1], light.position[2] - eye[2] };
            order.push_back({ selectedOwners.contains(light.owner), -Dot(d, d), i });
        }
        std::sort(order.begin(), order.end());
        icons.reserve(order.size());
        for (const auto& [selected, distance2, i] : order) {
            const SceneLightMarker& light = lightMarkers[i];
            float peak = std::max({ light.color[0], light.color[1], light.color[2], 1e-4f });
            uint32_t color = 0xFF000000u;
            for (int c = 0; c < 3; c++) {
                float channel = 0.25f + 0.75f * std::clamp(light.color[c] / peak, 0.0f, 1.0f);
                color |= static_cast<uint32_t>(channel * 255.0f + 0.5f) << (8 * c);
            }
            ViewerLightIcon icon{};
            std::copy(light.position, light.position + 3, icon.position);
            icon.kind = static_cast<uint32_t>(light.kind);
            icon.color = color;
            icon.flags = (selected ? LIGHT_ICON_SELECTED : 0u) | (light.enabled ? 0u : LIGHT_ICON_DISABLED);
            icons.push_back(icon);
            if (!selected) continue;

            if (light.kind == SceneLightKind::Point && light.range > 0) {
                const float axes[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
                for (int a = 0; a < 3; a++) {
                    AddArc(lines, light.position, axes[a], axes[(a + 1) % 3], light.range, 0, TWO_PI, 64, GIZMO_OUTER_COLOR);
                }
            } else if (light.kind == SceneLightKind::Spot && light.range > 0) {
                constexpr float degrees = 3.14159265f / 180.0f;
                float outer = std::clamp(light.cone * 0.5f, 0.5f, 89.0f) * degrees;
                float inner = std::clamp((light.cone - light.spread) * 0.5f, 0.0f, 89.0f) * degrees;
                AddSpotCone(lines, light, outer, 8, true, GIZMO_OUTER_COLOR);
                if (inner > 0.01f && inner < outer - 0.01f) AddSpotCone(lines, light, inner, 4, false, GIZMO_INNER_COLOR);
            } else if (light.kind == SceneLightKind::Directional) {
                float d[3] = { light.position[0] - eye[0], light.position[1] - eye[1], light.position[2] - eye[2] };
                float length = std::sqrt(Dot(d, d)) * DIRECTIONAL_ARROW_SCALE;
                float travel[3] = { -light.direction[0], -light.direction[1], -light.direction[2] };
                float u[3];
                float v[3];
                Basis(travel, u, v);
                float tip[3];
                for (int c = 0; c < 3; c++) tip[c] = light.position[c] + travel[c] * length;
                AddLine(lines, light.position, tip, GIZMO_INNER_COLOR);
                for (int k = 0; k < 4; k++) {
                    const float* side = k < 2 ? u : v;
                    float sign = k % 2 ? -1.0f : 1.0f;
                    float head[3];
                    for (int c = 0; c < 3; c++) head[c] = tip[c] - travel[c] * length * 0.2f + side[c] * sign * length * 0.08f;
                    AddLine(lines, tip, head, GIZMO_INNER_COLOR);
                }
            }
        }
    }
    viewer->SetLightIcons(icons);
    viewer->SetGizmoLines(lines);
}

void NativeViewport::UpdateStatsOverlay(bool statsChanged) {
    bool on = statsOverlay.load();
    if (!on) {
        if (statsOverlayShown) viewer->SetTextOverlay({}, 0, 0, 0, 0);
        statsOverlayShown = false;
        return;
    }
    if (statsOverlayShown && !statsChanged) return;
    RaeViewportStats now;
    {
        std::lock_guard lock(mutex);
        now = stats;
    }
    constexpr float GOOD[3] = { 0.45f, 0.9f, 0.45f };
    constexpr float FAIR[3] = { 0.95f, 0.8f, 0.3f };
    constexpr float POOR[3] = { 0.95f, 0.4f, 0.35f };
    constexpr float TEXT[3] = { 0.86f, 0.86f, 0.86f };
    const float* rate = now.fps >= 55 ? GOOD : now.fps >= 28 ? FAIR : POOR;
    char text[96];
    std::vector<StatsLine> lines;
    std::snprintf(text, sizeof(text), "%.0f fps  %.2f ms", now.fps, now.frameMs);
    lines.push_back({ text, { rate[0], rate[1], rate[2] } });
    std::snprintf(text, sizeof(text), "GPU %.2f ms  CPU %.2f ms", now.gpuPrepassMs + now.gpuGBufferMs + now.gpuRestMs,
                  now.cpuCullMs + now.cpuRecordMs);
    lines.push_back({ text, { TEXT[0], TEXT[1], TEXT[2] } });
    if (now.triangles > 0) {
        std::snprintf(text, sizeof(text), "%u draws  %.2fM tris", now.drawn, static_cast<double>(now.triangles) / 1e6);
        lines.push_back({ text, { TEXT[0], TEXT[1], TEXT[2] } });
    }
    HDC windowDc = GetDC(hwnd);
    float scale = static_cast<float>(GetDeviceCaps(windowDc, LOGPIXELSY)) / 96.0f * renderScale.load();
    ReleaseDC(hwnd, windowDc);
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> pixels = RenderStatsPanel(lines, std::max(scale, 0.5f), width, height);
    int32_t margin = static_cast<int32_t>(std::lround(8 * scale));
    viewer->SetTextOverlay(pixels, width, height, margin, margin);
    statsOverlayShown = true;
}

GizmoView NativeViewport::MakeGizmoView() const {
    GizmoView view;
    float target[3];
    camera.GetEye(view.eye);
    camera.GetTarget(target);
    for (int c = 0; c < 3; c++) view.forward[c] = target[c] - view.eye[c];
    Normalize(view.forward);
    const float worldUp[3] = { 0, 1, 0 };
    Cross(view.forward, worldUp, view.right);
    Normalize(view.right);
    Cross(view.right, view.forward, view.up);
    view.tanHalf = std::tan(camera.GetFov() * 0.5f);
    view.width = static_cast<float>(std::max(1u, viewer->GetWidth()));
    view.height = static_cast<float>(std::max(1u, viewer->GetHeight()));
    return view;
}

int32_t NativeViewport::ActiveObject() const {
    if (auto found = objectIndex.find(activeObject); found != objectIndex.end() && selectedOwners.contains(activeObject)) {
        return found->second;
    }
    for (const std::string& key : selectedOwners) {
        if (auto found = objectIndex.find(key); found != objectIndex.end()) return found->second;
    }
    return -1;
}

std::vector<int32_t> NativeViewport::SelectionRoots() const {
    std::vector<int32_t> roots;
    for (const std::string& key : selectedOwners) {
        auto found = objectIndex.find(key);
        if (found == objectIndex.end()) continue;
        bool nested = false;
        for (int32_t parent = objects[static_cast<std::size_t>(found->second)].parent; parent >= 0 && !nested;
             parent = objects[static_cast<std::size_t>(parent)].parent) {
            nested = selectedOwners.contains(objects[static_cast<std::size_t>(parent)].key);
        }
        if (!nested) roots.push_back(found->second);
    }
    return roots;
}

Mat4 NativeViewport::ParentWorld(int32_t object) const {
    int32_t parent = objects[static_cast<std::size_t>(object)].parent;
    return parent >= 0 ? objects[static_cast<std::size_t>(parent)].world : Identity();
}

void NativeViewport::MoveObject(int32_t object, const Mat4& world) {
    Mat4 delta = Mul(InverseAffine(objects[static_cast<std::size_t>(object)].world), world);
    float grow = 0;
    for (int r = 0; r < 3; r++) {
        grow = std::max(grow, std::sqrt(delta.m[r * 4] * delta.m[r * 4] + delta.m[r * 4 + 1] * delta.m[r * 4 + 1] +
                                        delta.m[r * 4 + 2] * delta.m[r * 4 + 2]));
    }
    std::vector<ViewerDrawBounds> bounds;
    ApplyObjectDelta(object, delta, grow, bounds);
    viewer->SetDrawBounds(bounds);
}

void NativeViewport::ApplyObjectDelta(int32_t object, const Mat4& delta, float grow, std::vector<ViewerDrawBounds>& bounds) {
    SceneObject& moved = objects[static_cast<std::size_t>(object)];
    moved.world = Mul(moved.world, delta);
    if (auto found = pick.instancesByOwner.find(moved.key); found != pick.instancesByOwner.end()) {
        for (uint32_t instance : found->second) {
            float* m = pick.worlds[instance].m;
            Mat4 world = Identity();
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 4; c++) world.m[c * 4 + r] = m[r * 4 + c];
            }
            ToFloat3x4(Mul(world, delta), m);
            viewer->SetInstanceWorld(instance, m);
            if (instance < drawsByInstance.size()) {
                for (uint32_t id : drawsByInstance[instance]) {
                    ViewerMeshDraw& draw = pick.draws[id];
                    if (draw.boundsRadius <= 0) continue;
                    float center[3];
                    TransformPoint(delta, draw.boundsCenter, center);
                    std::copy(center, center + 3, draw.boundsCenter);
                    draw.boundsRadius *= grow;
                    bounds.push_back({ draw.id, { center[0], center[1], center[2] }, draw.boundsRadius });
                }
            }
            if (instance < meshLodByInstance.size() && meshLodByInstance[instance] >= 0) {
                SceneMeshLod& lod = meshLods[static_cast<std::size_t>(meshLodByInstance[instance])];
                float center[3];
                TransformPoint(delta, lod.center, center);
                std::copy(center, center + 3, lod.center);
            }
        }
    }
    if (auto found = collidersByOwner.find(moved.key); found != collidersByOwner.end()) {
        for (uint32_t collider : found->second) {
            sceneCollision.colliders[collider].world = Mul(sceneCollision.colliders[collider].world, delta);
        }
        collisionDirty = true;
    }
    if (auto found = lightsByOwner.find(moved.key); found != lightsByOwner.end()) {
        for (uint32_t index : found->second) {
            SceneLightMarker& light = lightMarkers[index];
            float position[3];
            float direction[3];
            TransformPoint(delta, light.position, position);
            TransformVector(delta, light.direction, direction);
            Normalize(direction);
            std::copy(position, position + 3, light.position);
            std::copy(direction, direction + 3, light.direction);
            if (light.lightIndex < 0 || static_cast<std::size_t>(light.lightIndex) >= sceneLights.size()) continue;
            GameLightParam& param = sceneLights[static_cast<std::size_t>(light.lightIndex)];
            std::copy(position, position + 3, param.position);
            if (light.kind == SceneLightKind::Spot) std::copy(direction, direction + 3, param.direction);
            UploadLight(static_cast<std::size_t>(light.lightIndex));
        }
    }
    if (mainDirectional && moved.key == directionalOwner) {
        float direction[3];
        TransformVector(delta, mainDirectional->direction, direction);
        Normalize(direction);
        std::copy(direction, direction + 3, mainDirectional->direction);
        if (!directionalHidden) ApplyDirectionalLight();
    }
    for (SkeletonPose& pose : poses) {
        if (pose.owner != moved.key) continue;
        pose.world = Mul(pose.world, delta);
        posesMoved = true;
        skeletonsDirty = true;
    }
    for (int32_t child : objectChildren[static_cast<std::size_t>(object)]) ApplyObjectDelta(child, delta, grow, bounds);
}

void NativeViewport::UploadLight(std::size_t index) {
    GameLightParam param = sceneLights[index];
    if (index < lightHidden.size() && lightHidden[index]) std::fill(param.color, param.color + 3, 0.0f);
    viewer->UpdateLight(static_cast<uint32_t>(index), param);
}

// As UploadScene sets it.
void NativeViewport::ApplyDirectionalLight() {
    const SceneDirectionalLight& light = *mainDirectional;
    float color[3];
    float scattering[3];
    float scatteringIntensity = light.common.usingSameIntensity ? light.common.intensity : light.common.volumetricScatteringIntensity;
    for (int c = 0; c < 3; c++) {
        color[c] = light.common.color[c] * light.common.intensity;
        scattering[c] = light.common.color[c] * scatteringIntensity;
    }
    viewer->SetDirectionalLight(light.direction, color, light.common.minRoughness * light.common.minRoughness, scattering);
}

void NativeViewport::UpdateLightVisibility() {
    bool changed = false;
    for (const SceneLightMarker& light : lightMarkers) {
        if (light.lightIndex < 0 || static_cast<std::size_t>(light.lightIndex) >= sceneLights.size()) continue;
        std::size_t index = static_cast<std::size_t>(light.lightIndex);
        uint8_t hide = hiddenOwners.contains(light.owner) ? 1 : 0;
        if (hide == lightHidden[index]) continue;
        lightHidden[index] = hide;
        UploadLight(index);
        changed = true;
    }
    if (changed) {
        std::vector<float> scattering = sceneLightScattering;
        for (std::size_t i = 0; i < lightHidden.size() && i * 3 + 2 < scattering.size(); i++) {
            if (lightHidden[i]) std::fill(scattering.begin() + static_cast<std::ptrdiff_t>(i * 3), scattering.begin() + static_cast<std::ptrdiff_t>(i * 3 + 3), 0.0f);
        }
        scattering.insert(scattering.end(), cameraLightScattering.begin(), cameraLightScattering.end());
        viewer->SetVolumetricScattering(scattering);
    }
    if (mainDirectional) {
        bool hide = hiddenOwners.contains(directionalOwner);
        if (hide != directionalHidden) {
            directionalHidden = hide;
            if (hide) viewer->ClearDirectionalLight();
            else ApplyDirectionalLight();
        }
    }
}

void NativeViewport::SetObjectTransformField(const std::string& key, const std::string& field, const std::vector<float>& values) {
    auto found = objectIndex.find(key);
    if (found == objectIndex.end() || values.size() < 3) return;
    int32_t object = found->second;
    Mat4 parent = ParentWorld(object);
    Mat4 local = Mul(objects[static_cast<std::size_t>(object)].world, InverseAffine(parent));
    float t[3];
    float q[4];
    float s[3];
    DecomposeTRS(local, t, q, s);
    if (field == "position") std::copy(values.begin(), values.begin() + 3, t);
    else if (field == "rotation") EulerDegreesToQuat(values.data(), q);
    else if (field == "scale") std::copy(values.begin(), values.begin() + 3, s);
    else return;
    Mat4 before = objects[static_cast<std::size_t>(object)].world;
    MoveObject(object, Mul(ComposeTRS(t, q, s), parent));
    RecordUndo({ object }, { before }, true);
}

std::string NativeViewport::LocalTransformText(int32_t object) const {
    Mat4 local = Mul(objects[static_cast<std::size_t>(object)].world, InverseAffine(ParentWorld(object)));
    float t[3];
    float q[4];
    float s[3];
    float euler[3];
    DecomposeTRS(local, t, q, s);
    QuatToEulerDegrees(q, euler);
    char text[256];
    std::snprintf(text, sizeof(text), "%.7g,%.7g,%.7g\t%.7g,%.7g,%.7g\t%.7g,%.7g,%.7g", t[0], t[1], t[2], euler[0], euler[1],
                  euler[2], s[0], s[1], s[2]);
    return objects[static_cast<std::size_t>(object)].key + '\t' + text;
}

// A line between two surface points; the label follows its middle.
void NativeViewport::UpdateMeasure() {
    bool on = measuring.load() && hasScene && !image && !meshAsset;
    float x;
    float y;
    bool start;
    bool dragging;
    {
        std::lock_guard lock(mutex);
        x = static_cast<float>(lastMouseX) * renderScale.load();
        y = static_cast<float>(lastMouseY) * renderScale.load();
        start = std::exchange(measureStart, false);
        dragging = measureDragging;
    }
    if (!on) {
        measureValid = false;
        if (measureShown) viewer->SetTextOverlay({}, 0, 0, 0, 0, 1);
        measureShown = false;
        measureLabelText.clear();
        viewer->SetHandleGeometry(handleTriangles);
        return;
    }
    if (start || dragging) {
        float eye[3];
        float dir[3];
        if (PickRay(static_cast<int>(x), static_cast<int>(y), eye, dir)) {
            float t = RaycastMeshes(eye, dir, nullptr);
            if (t == FLT_MAX) {
                float pivot[3];
                camera.GetPivot(pivot);
                float d[3] = { pivot[0] - eye[0], pivot[1] - eye[1], pivot[2] - eye[2] };
                t = std::sqrt(Dot(d, d));
            }
            float point[3] = { eye[0] + dir[0] * t, eye[1] + dir[1] * t, eye[2] + dir[2] * t };
            if (start) std::copy(point, point + 3, measureFrom);
            std::copy(point, point + 3, measureTo);
            measureValid = true;
        }
    }
    if (!measureValid) {
        viewer->SetHandleGeometry(handleTriangles);
        return;
    }
    GizmoView view = MakeGizmoView();
    float middle[3];
    for (int c = 0; c < 3; c++) middle[c] = (measureFrom[c] + measureTo[c]) * 0.5f;
    float metersPerPixel = view.MetersPerPixel(middle);
    auto point = [&](const float p[3], uint32_t color) {
        float half = MEASURE_POINT_PIXELS * view.MetersPerPixel(p);
        float corners[4][3];
        for (int k = 0; k < 4; k++) {
            float sx = (k == 1 || k == 2) ? half : -half;
            float sy = k >= 2 ? half : -half;
            for (int c = 0; c < 3; c++) corners[k][c] = p[c] + view.right[c] * sx + view.up[c] * sy;
        }
        for (int k : { 0, 1, 2, 0, 2, 3 }) handleTriangles.push_back({ { corners[k][0], corners[k][1], corners[k][2] }, color });
    };
    float along[3] = { measureTo[0] - measureFrom[0], measureTo[1] - measureFrom[1], measureTo[2] - measureFrom[2] };
    float toEye[3] = { view.eye[0] - middle[0], view.eye[1] - middle[1], view.eye[2] - middle[2] };
    float side[3];
    Cross(along, toEye, side);
    Normalize(side);
    float halfWidth = MEASURE_LINE_PIXELS * 0.5f * metersPerPixel;
    float quad[4][3];
    for (int c = 0; c < 3; c++) {
        quad[0][c] = measureFrom[c] - side[c] * halfWidth;
        quad[1][c] = measureFrom[c] + side[c] * halfWidth;
        quad[2][c] = measureTo[c] + side[c] * halfWidth;
        quad[3][c] = measureTo[c] - side[c] * halfWidth;
    }
    for (int k : { 0, 1, 2, 0, 2, 3 }) handleTriangles.push_back({ { quad[k][0], quad[k][1], quad[k][2] }, MEASURE_COLOR });
    point(measureFrom, MEASURE_COLOR);
    point(measureTo, MEASURE_COLOR);
    viewer->SetHandleGeometry(handleTriangles);

    float distance = std::sqrt(Dot(along, along));
    char text[64];
    if (distance < 1.0f) std::snprintf(text, sizeof(text), "%.1f cm", distance * 100.0f);
    else std::snprintf(text, sizeof(text), "%.3f m", distance);
    if (measureLabelText != text) {
        measureLabelText = text;
        HDC windowDc = GetDC(hwnd);
        float scale = static_cast<float>(GetDeviceCaps(windowDc, LOGPIXELSY)) / 96.0f * renderScale.load();
        ReleaseDC(hwnd, windowDc);
        measureLabelPixels = RenderStatsPanel({ { text, { 1.0f, 0.85f, 0.2f } } }, std::max(scale, 0.5f), measureLabelSize[0],
                                              measureLabelSize[1]);
    }
    float sx;
    float sy;
    float depth;
    if (ProjectToScreen(middle, sx, sy, depth)) {
        viewer->SetTextOverlay(measureLabelPixels, measureLabelSize[0], measureLabelSize[1],
                               static_cast<int32_t>(sx) - static_cast<int32_t>(measureLabelSize[0] / 2),
                               static_cast<int32_t>(sy) - static_cast<int32_t>(measureLabelSize[1]) - 8, 1);
        measureShown = true;
    }
}

std::string NativeViewport::TakeTransformChanges() {
    std::lock_guard lock(mutex);
    std::string text;
    for (const auto& [key, line] : transformChanges) text += line + '\n';
    transformChanges.clear();
    return text;
}

void NativeViewport::RefreshSelectionBounds() {
    std::vector<std::string> keys(selectedOwners.begin(), selectedOwners.end());
    hasSelectionBounds = DrawsBounds(ObjectDraws(keys), selectionCenter, selectionRadius);
    if (!hasSelectionBounds) hasSelectionBounds = CollidersBounds(selectedOwners, selectionCenter, selectionRadius);
    if (!hasSelectionBounds) hasSelectionBounds = LightsBounds(selectedOwners, selectionCenter, selectionRadius);
}

void NativeViewport::UpdateTransformGizmo() {
    int32_t active = -1;
    bool shown = (gizmoMask.load() & GIZMOS_ENABLED) != 0 && hasScene && !meshAsset && !materialPreview && !effect && !image;
    if (shown) active = ActiveObject();
    float x;
    float y;
    bool inside;
    bool dragStart;
    bool dragEnd;
    bool dragging;
    GizmoHandle pressed;
    GizmoSnap snapping;
    {
        std::lock_guard lock(mutex);
        x = static_cast<float>(lastMouseX) * renderScale.load();
        y = static_cast<float>(lastMouseY) * renderScale.load();
        inside = mouseInside;
        dragStart = std::exchange(gizmoDragStart, false);
        dragEnd = std::exchange(gizmoDragEnd, false);
        dragging = gizmoDragging;
        pressed = static_cast<GizmoHandle>(gizmoDragHandle);
        snapping = snap;
    }
    handleTriangles.clear();
    if (active < 0 || measuring.load()) {
        gizmo.EndDrag();
        gizmoHover = 0;
        std::lock_guard lock(mutex);
        pressGizmoValid = false;
        return;
    }
    if (gizmo.Dragging() == GizmoHandle::None) {
        gizmo.mode = static_cast<GizmoMode>(gizmoMode.load());
        gizmo.local = gizmoLocal.load();
    }
    GizmoView view = MakeGizmoView();
    auto setFrame = [&] {
        const Mat4& world = objects[static_cast<std::size_t>(active)].world;
        float pivot[3] = { world.m[12], world.m[13], world.m[14] };
        float axes[3][3];
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) axes[r][c] = world.m[r * 4 + c];
            Normalize(axes[r]);
        }
        gizmo.SetFrame(pivot, axes);
    };
    setFrame();

    if (dragStart && gizmo.Dragging() == GizmoHandle::None && pressed != GizmoHandle::None) {
        gizmo.BeginDrag(view, pressed, x, y);
        dragTargets = SelectionRoots();
        if (std::find(dragTargets.begin(), dragTargets.end(), active) == dragTargets.end()) dragTargets.push_back(active);
        dragStartWorlds.clear();
        for (int32_t target : dragTargets) dragStartWorlds.push_back(objects[static_cast<std::size_t>(target)].world);
    }
    if (gizmo.Dragging() != GizmoHandle::None) {
        Mat4 delta = gizmo.Drag(view, x, y, snapping);
        for (std::size_t i = 0; i < dragTargets.size(); i++) MoveObject(dragTargets[i], Mul(dragStartWorlds[i], delta));
        {
            std::lock_guard lock(mutex);
            for (int32_t target : dragTargets) transformChanges[objects[static_cast<std::size_t>(target)].key] = LocalTransformText(target);
        }
        if (dragEnd || !dragging) {
            gizmo.EndDrag();
            RecordUndo(dragTargets, dragStartWorlds, false);
            RefreshSelectionBounds();
        }
        setFrame();
    } else {
        gizmoHover = inside ? static_cast<int32_t>(gizmo.HitTest(view, x, y)) : 0;
    }
    gizmo.Build(view, static_cast<GizmoHandle>(gizmoHover.load()), handleTriangles);
    std::lock_guard lock(mutex);
    pressGizmo = gizmo;
    pressView = view;
    pressGizmoValid = gizmo.Dragging() == GizmoHandle::None;
}

void NativeViewport::UpdateCollisionInstances() {
    const std::vector<SceneCollider>& colliders = sceneCollision.colliders;
    colliderShown.assign(colliders.size(), 0);
    std::vector<uint32_t> order;
    for (uint32_t i = 0; i < colliders.size(); i++) {
        const std::string& group = collisionGroups[filterGroups[colliders[i].filter]].name;
        if (!allCollisionGroups && !shownCollisionGroups.contains(group)) continue;
        if (hiddenOwners.contains(colliders[i].owner)) continue;
        colliderShown[i] = 1;
        order.push_back(i);
    }
    auto groupOf = [&](uint32_t i) -> const ViewportCollisionGroup& { return collisionGroups[filterGroups[colliders[i].filter]]; };
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return std::pair(groupOf(a).volume, colliders[a].geometry) < std::pair(groupOf(b).volume, colliders[b].geometry);
    });

    std::vector<ViewerCollisionInstance> instances;
    std::vector<ViewerCollisionBatch> batches;
    instances.reserve(order.size());
    for (uint32_t i : order) {
        const SceneCollider& collider = colliders[i];
        const ViewportCollisionGroup& group = groupOf(i);
        bool selected = selectedOwners.contains(collider.owner);
        uint8_t rgb[3];
        if (selected) {
            std::copy(COLLISION_SELECTED_COLOR, COLLISION_SELECTED_COLOR + 3, rgb);
        } else if (group.volume) {
            std::copy(group.color, group.color + 3, rgb);
        } else {
            // One colour per object, as Unreal's collision views.
            std::size_t hash = std::hash<std::string>{}(collider.owner);
            float hue = static_cast<float>(hash & 0xFFFF) / 65536.0f * 6.0f;
            float saturation = 0.45f + 0.2f * static_cast<float>((hash >> 16) & 255) / 255.0f;
            float value = 0.72f + 0.2f * static_cast<float>((hash >> 24) & 255) / 255.0f;
            for (int c = 0; c < 3; c++) {
                float k = std::fmod(static_cast<float>(5 - 2 * c) + hue, 6.0f);
                float channel = value * (1.0f - saturation * std::clamp(std::min(k, 4.0f - k), 0.0f, 1.0f));
                rgb[c] = static_cast<uint8_t>(channel * 255.0f + 0.5f);
            }
        }
        uint32_t alpha = group.volume ? COLLISION_VOLUME_ALPHA * (selected ? 2u : 1u) : 255u;
        ViewerCollisionInstance instance{};
        ToFloat3x4(collider.world, instance.world);
        instance.color = uint32_t{ rgb[0] } | uint32_t{ rgb[1] } << 8 | uint32_t{ rgb[2] } << 16 | alpha << 24;
        const CollisionGeometry& geometry = sceneCollision.geometries[collider.geometry];
        if (batches.empty() || batches.back().firstIndex != geometry.firstIndex ||
            batches.back().translucent != group.volume) {
            batches.push_back({ geometry.firstIndex, geometry.indexCount, geometry.baseVertex,
                                static_cast<uint32_t>(instances.size()), 0, group.volume });
        }
        batches.back().instanceCount++;
        instances.push_back(instance);
    }
    viewer->SetCollisionInstances(instances, batches);
}

bool NativeViewport::CollidersBounds(const std::set<std::string>& owners, float center[3], float& radius) const {
    float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
    float hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    bool any = false;
    for (const SceneCollider& collider : sceneCollision.colliders) {
        if (!owners.contains(collider.owner)) continue;
        const CollisionGeometry& geometry = sceneCollision.geometries[collider.geometry];
        for (int k = 0; k < 8; k++) {
            float corner[3] = { k & 1 ? geometry.boundsMax[0] : geometry.boundsMin[0],
                                k & 2 ? geometry.boundsMax[1] : geometry.boundsMin[1],
                                k & 4 ? geometry.boundsMax[2] : geometry.boundsMin[2] };
            float world[3];
            TransformPoint(collider.world, corner, world);
            for (int c = 0; c < 3; c++) {
                lo[c] = std::min(lo[c], world[c]);
                hi[c] = std::max(hi[c], world[c]);
            }
        }
        any = true;
    }
    if (!any) return false;
    float extent[3];
    for (int c = 0; c < 3; c++) {
        center[c] = (lo[c] + hi[c]) * 0.5f;
        extent[c] = (hi[c] - lo[c]) * 0.5f;
    }
    radius = std::sqrt(Dot(extent, extent));
    return true;
}

void NativeViewport::ShowLod(int32_t lod) {
    displayedLod = lod;
    UpdateDrawMask();
    std::lock_guard lock(mutex);
    shownLod = lod;
}

static int32_t RoomGroundCell(float v) {
    return static_cast<int32_t>(std::floor(v / ROOM_GROUND_CELL));
}

static uint64_t RoomGroundKey(int32_t x, int32_t z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
}

void NativeViewport::BuildRoomGround() {
    roomGround = {};
    if (instanceRoom.empty()) return;
    std::unordered_map<std::string, int32_t> sceneRoom;
    for (std::size_t i = 0; i < instanceRoom.size() && i < pick.owners.size(); i++) {
        if (instanceRoom[i] < 0) continue;
        const std::string& owner = pick.owners[i];
        sceneRoom[owner.substr(0, owner.find('|'))] = instanceRoom[i];
    }
    for (const SceneCollider& collider : sceneCollision.colliders) {
        if (collider.filter >= sceneCollision.filters.size() ||
            CollisionFilterGroup(sceneCollision.filters[collider.filter]) != "Terrain") {
            continue;
        }
        auto room = sceneRoom.find(collider.owner.substr(0, collider.owner.find('|')));
        if (room == sceneRoom.end() || collider.geometry >= sceneCollision.geometries.size()) continue;
        const CollisionGeometry& geometry = sceneCollision.geometries[collider.geometry];
        uint32_t end = geometry.firstIndex + geometry.indexCount;
        for (uint32_t i = geometry.firstIndex; i + 2 < end; i += 3) {
            float v[9];
            for (int k = 0; k < 3; k++) {
                std::size_t vertex = static_cast<std::size_t>(geometry.baseVertex) + sceneCollision.indices[i + k];
                TransformPoint(collider.world, &sceneCollision.positions[vertex * 3], v + k * 3);
            }
            uint32_t triangle = static_cast<uint32_t>(roomGround.rooms.size());
            roomGround.triangles.insert(roomGround.triangles.end(), v, v + 9);
            roomGround.rooms.push_back(room->second);
            int32_t x0 = RoomGroundCell(std::min({ v[0], v[3], v[6] }));
            int32_t x1 = RoomGroundCell(std::max({ v[0], v[3], v[6] }));
            int32_t z0 = RoomGroundCell(std::min({ v[2], v[5], v[8] }));
            int32_t z1 = RoomGroundCell(std::max({ v[2], v[5], v[8] }));
            for (int32_t x = x0; x <= x1; x++) {
                for (int32_t z = z0; z <= z1; z++) roomGround.cells[RoomGroundKey(x, z)].push_back(triangle);
            }
        }
    }
}

// app.EnvSceneManager.updateCurrentRoom: the room of the ground under the player, kept while
// that ground has no room or its room draws no list.
void NativeViewport::UpdateCurrentRoom(const float eye[3]) {
    if (roomGround.cells.empty()) return;
    float dx = eye[0] - roomProbe[0];
    float dy = eye[1] - roomProbe[1];
    float dz = eye[2] - roomProbe[2];
    if (dx * dx + dy * dy + dz * dz < ROOM_PROBE_STEP * ROOM_PROBE_STEP) return;
    std::copy(eye, eye + 3, roomProbe);

    std::vector<uint8_t> nearNow(sceneLods.groups.size(), 0);
    int32_t cx = RoomGroundCell(eye[0]);
    int32_t cz = RoomGroundCell(eye[2]);
    float groundY = -FLT_MAX;
    int32_t groundRoom = -1;
    for (int32_t x = cx - 1; x <= cx + 1; x++) {
        for (int32_t z = cz - 1; z <= cz + 1; z++) {
            auto cell = roomGround.cells.find(RoomGroundKey(x, z));
            if (cell == roomGround.cells.end()) continue;
            for (uint32_t triangle : cell->second) {
                const float* v = &roomGround.triangles[static_cast<std::size_t>(triangle) * 9];
                int32_t room = roomGround.rooms[triangle];
                if (std::min({ v[1], v[4], v[7] }) <= eye[1] + ROOM_NEAR_ABOVE &&
                    std::max({ v[1], v[4], v[7] }) >= eye[1] - ROOM_NEAR_BELOW) {
                    nearNow[room] = 1;
                }
                if (x != cx || z != cz) continue;
                float area = (v[3] - v[0]) * (v[8] - v[2]) - (v[6] - v[0]) * (v[5] - v[2]);
                if (std::fabs(area) < 1e-6f) continue;
                float b1 = ((eye[0] - v[0]) * (v[8] - v[2]) - (v[6] - v[0]) * (eye[2] - v[2])) / area;
                float b2 = ((v[3] - v[0]) * (eye[2] - v[2]) - (eye[0] - v[0]) * (v[5] - v[2])) / area;
                if (b1 < 0 || b2 < 0 || b1 + b2 > 1) continue;
                float y = v[1] + b1 * (v[4] - v[1]) + b2 * (v[7] - v[1]);
                if (y <= eye[1] && y > groundY) {
                    groundY = y;
                    groundRoom = room;
                }
            }
        }
    }
    if (groundRoom >= 0 && sceneLods.roomDraws.count(static_cast<uint32_t>(groundRoom))) currentRoom = groundRoom;
    // Rooms that can never be current are far stand-ins (CourtyardLow...) and stay listed only.
    for (std::size_t g = 0; g < nearNow.size(); g++) {
        if (nearNow[g] && !sceneLods.roomDraws.count(static_cast<uint32_t>(g))) nearNow[g] = 0;
    }
    roomsNear = std::move(nearNow);
}

void NativeViewport::UpdateSceneLods(const float eye[3]) {
    if (sceneLods.groups.empty()) return;
    UpdateCurrentRoom(eye);
    bool changed = lodNear.size() != sceneLods.groups.size();
    lodNear.resize(sceneLods.groups.size(), 0);
    for (std::size_t g = 0; g < sceneLods.groups.size(); g++) {
        uint8_t isNear = SceneLodNear(sceneLods, static_cast<uint32_t>(g), eye, currentRoom, lodNear[g] != 0) ||
                         (g < roomsNear.size() && roomsNear[g]);
        changed |= isNear != lodNear[g];
        lodNear[g] = isNear;
    }
    if (!changed) return;
    lodHiddenInstances.assign(pick.worlds.size(), 0);
    for (const SceneLodMember& member : sceneLods.members) {
        if (member.instance < lodHiddenInstances.size() && member.low == (lodNear[member.group] != 0)) {
            lodHiddenInstances[member.instance] = 1;
        }
    }
    UpdateDrawMask();
}

// via::render::decideLodLevel: depth along the view over P00 times the mesh radius.
void NativeViewport::UpdateMeshLods(const float eye[3], const float target[3]) {
    if (meshLods.empty()) return;
    float forward[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
    float length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (length < 1e-6f) return;
    for (float& f : forward) f /= length;
    float aspect = static_cast<float>(viewer->GetWidth()) / static_cast<float>(std::max(1u, viewer->GetHeight()));
    float p00 = 1.0f / (std::tan(camera.GetFov() * 0.5f) * aspect);
    bool changed = false;
    for (const SceneMeshLod& lod : meshLods) {
        float depth = (lod.center[0] - eye[0]) * forward[0] + (lod.center[1] - eye[1]) * forward[1] +
                      (lod.center[2] - eye[2]) * forward[2];
        float v = depth / (p00 * lod.radius);
        int level = 0;
        for (uint32_t i = 0; i + 1 < lod.lodCount; i++) level += v > lod.thresholds[i];
        level = std::clamp(level + MESH_LOD_OFFSET, 0, static_cast<int>(lod.lodCount) - 1);
        if (lod.instance < instanceLod.size() && instanceLod[lod.instance] != level) {
            instanceLod[lod.instance] = static_cast<uint8_t>(level);
            changed = true;
        }
    }
    static uint32_t diagThrottle = 0;
    if (changed && (diagThrottle++ % 90) == 0) {
        uint32_t histogram[8] = {};
        for (const SceneMeshLod& lod : meshLods) {
            uint8_t level = lod.instance < instanceLod.size() ? instanceLod[lod.instance] : 0;
            if (level < 8) histogram[level]++;
        }
        LogInfo("mesh LOD levels: L0=%u L1=%u L2=%u L3=%u L4=%u L5=%u L6=%u L7=%u (of %zu with LOD data)",
                histogram[0], histogram[1], histogram[2], histogram[3], histogram[4], histogram[5], histogram[6],
                histogram[7], meshLods.size());
    }
    if (changed) UpdateDrawMask();
}

void NativeViewport::UpdateDrawMask() {
    std::vector<uint8_t> mask(pick.draws.size(), 1);
    uint32_t count = 0;
    bool filtered = false;
    for (std::size_t id = 0; id < pick.draws.size(); id++) {
        uint32_t instance = pick.draws[id].instanceIndex;
        int32_t wantLod = meshAsset && instance == mainInstance ? displayedLod
                        : instance < instanceLod.size() ? instanceLod[instance] : 0;
        if (int32_t forced = meshAsset ? -1 : ForcedInstanceLod(instance); forced >= 0) wantLod = forced;
        bool shown = static_cast<int32_t>(pick.tags[id].lod) == wantLod &&
                     !(instance < hiddenInstances.size() && hiddenInstances[instance]) &&
                     !(!appliedStreamAll && instance < lodHiddenInstances.size() && lodHiddenInstances[instance]) &&
                     !(instance < zoneHiddenInstances.size() && zoneHiddenInstances[instance]) &&
                     (meshAsset || DrawAllowed(id));
        mask[id] = shown;
        count += shown;
        filtered |= !shown;
    }
    drawShown = filtered ? std::move(mask) : std::vector<uint8_t>();
    viewer->SetDrawMask(drawShown);
    std::lock_guard lock(mutex);
    stats.draws = count;
}

std::vector<uint32_t> NativeViewport::SubmeshDraws(int32_t submesh) const {
    std::vector<uint32_t> ids;
    if (!meshAsset || submesh < 0) return ids;
    for (std::size_t id = 0; id < pick.draws.size(); id++) {
        if (pick.draws[id].instanceIndex == mainInstance && static_cast<int32_t>(pick.tags[id].lod) == displayedLod &&
            static_cast<int32_t>(pick.tags[id].submesh) == submesh) {
            ids.push_back(static_cast<uint32_t>(id));
        }
    }
    return ids;
}

std::vector<uint32_t> NativeViewport::ObjectDraws(const std::vector<std::string>& keys) const {
    std::vector<uint8_t> chosen(pick.worlds.size());
    bool any = false;
    for (const std::string& key : keys) {
        auto it = pick.instancesByOwner.find(key);
        if (it == pick.instancesByOwner.end()) continue;
        for (uint32_t instance : it->second) chosen[instance] = 1;
        any = true;
    }
    std::vector<uint32_t> ids;
    if (!any) return ids;
    for (std::size_t id = 0; id < pick.draws.size(); id++) {
        if (pick.draws[id].instanceIndex < chosen.size() && chosen[pick.draws[id].instanceIndex]) {
            ids.push_back(static_cast<uint32_t>(id));
        }
    }
    return ids;
}

void NativeViewport::SetSelection(const std::vector<uint32_t>& ids, int32_t submesh, std::string object) {
    selectedSubmesh = ids.empty() ? -1 : submesh;
    hasSelectionBounds = DrawsBounds(ids, selectionCenter, selectionRadius);
    viewer->SetSelection(ids);
    std::lock_guard lock(mutex);
    shownSubmesh = selectedSubmesh;
    shownObject = std::move(object);
}

bool NativeViewport::DrawsBounds(const std::vector<uint32_t>& ids, float center[3], float& radius) const {
    const float* positions = reinterpret_cast<const float*>(pick.positions.data());
    const uint16_t* indices = reinterpret_cast<const uint16_t*>(pick.indices.data());
    int64_t vertexCount = static_cast<int64_t>(pick.positions.size() / 12);
    std::size_t indexCount = pick.indices.size() / 2;
    float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
    float hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    bool any = false;
    for (uint32_t id : ids) {
        const ViewerMeshDraw& draw = pick.draws[id];
        if (draw.instanceIndex >= pick.worlds.size()) continue;
        const float* m = pick.worlds[draw.instanceIndex].m;
        std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(draw.startIndex) + draw.indexCount, indexCount);
        for (std::size_t i = draw.startIndex; i < end; i++) {
            int64_t v = draw.baseVertex + indices[i];
            if (v < 0 || v >= vertexCount) continue;
            const float* p = positions + v * 3;
            for (int r = 0; r < 3; r++) {
                float w = m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3];
                lo[r] = w < lo[r] ? w : lo[r];
                hi[r] = w > hi[r] ? w : hi[r];
            }
            any = true;
        }
    }
    if (!any) return false;
    float extent[3];
    for (int r = 0; r < 3; r++) {
        center[r] = (lo[r] + hi[r]) * 0.5f;
        extent[r] = (hi[r] - lo[r]) * 0.5f;
    }
    radius = std::sqrt(Dot(extent, extent));
    return true;
}

void NativeViewport::RenderLoop() {
    // Movies decode with Media Foundation on this thread.
    bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    ResetEmptyCamera();
    try {
        CreateBlankViewer();
    } catch (const std::exception& e) {
        LogError("viewport: %s", e.what());
        deviceLost = true;
    }

    using Clock = std::chrono::steady_clock;
    Clock::time_point last = Clock::now();
    Clock::time_point statsStart = last;
    int statsFrames = 0;
    bool statsChanged = false;
    double statsRenderMs = 0;
    ViewerFrameTimings statsTimings;

    while (running.load()) {
        std::unique_ptr<Job> job;
        uint32_t width;
        uint32_t height;
        CameraInput frameInput;
        {
            std::lock_guard lock(mutex);
            job = std::move(pendingJob);
            width = pendingWidth;
            height = pendingHeight;
            frameInput = input;
            frameInput.leftMouse = leftMouse;
            frameInput.rightMouse = rightMouse;
            input = CameraInput{};
        }

        if (job) {
            RunJob(*job);
            last = Clock::now();
            continue;
        }

        if (!viewer) {
            std::unique_lock lock(mutex);
            stats.deviceLost = deviceLost;
            wake.wait_for(lock, std::chrono::milliseconds(100));
            continue;
        }

        if (!IsWindowVisible(hwnd)) {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(50));
            last = Clock::now();
            continue;
        }

        // Taken only now so requests made during a load or while hidden are kept.
        bool pickNow = false;
        int pickAtX = 0;
        int pickAtY = 0;
        bool selectNow = false;
        int32_t selectLod = -1;
        int32_t selectSubmesh = -1;
        bool objectsNow = false;
        std::vector<std::string> selectObjects;
        std::vector<std::pair<std::string, std::vector<float>>> params;
        bool hiddenNow = false;
        std::vector<std::string> hidden;
        bool collisionGroupsNow = false;
        std::vector<std::string> collisionGroups;
        bool aiMapGroupsNow = false;
        std::vector<std::string> aiMapNames;
        std::shared_ptr<const TextureData> newSky;
        RaeViewportBackground frameBackground;
        float skyBase = 1;
        float panX = 0;
        float panY = 0;
        int cursorX = 0;
        int cursorY = 0;
        uint64_t captureTarget = 0;
        uint64_t targetsTarget = 0;
        {
            std::lock_guard lock(mutex);
            captureTarget = captureRequested > captureDone ? captureRequested : 0;
            targetsTarget = targetsRequested > targetsDone ? targetsRequested : 0;
            if (skyChanged && !image) {
                newSky = sky;
                skyChanged = false;
            }
            frameBackground = background;
            skyBase = skyIntensity;
            panX = panInputX;
            panY = panInputY;
            panInputX = 0;
            panInputY = 0;
            float scale = renderScale.load();
            cursorX = static_cast<int>(static_cast<float>(wheelX) * scale);
            cursorY = static_cast<int>(static_cast<float>(wheelY) * scale);
            std::swap(pickNow, pickRequested);
            pickAtX = static_cast<int>(static_cast<float>(pickX) * scale);
            pickAtY = static_cast<int>(static_cast<float>(pickY) * scale);
            std::swap(selectNow, selectRequested);
            selectLod = requestedLod;
            selectSubmesh = requestedSubmesh;
            std::swap(objectsNow, objectsRequested);
            selectObjects.swap(requestedObjects);
            params.swap(pendingParams);
            std::swap(hiddenNow, hiddenRequested);
            hidden.swap(requestedHidden);
            std::swap(collisionGroupsNow, collisionGroupsRequested);
            if (collisionGroupsNow) collisionGroups = requestedCollisionGroups;
            std::swap(aiMapGroupsNow, aiMapGroupsRequested);
            if (aiMapGroupsNow) aiMapNames = requestedAiMapGroups;
        }

        Clock::time_point now = Clock::now();
        float dt = std::min(std::chrono::duration<float>(now - last).count(), 0.1f);
        last = now;
        try {
            if (newSky) viewer->LoadSky(ToGameTexture(*newSky));
            if (!image) {
                RaeViewportBackground b = frameBackground;
                // Without the sky the default grey gradient shows.
                if (b.mode == 0 && hasScene && (showMask.load() & SHOW_SKY) == 0) b = RaeViewportBackground{ 2, { 0.23f, 0.23f, 0.23f }, { 0.09f, 0.09f, 0.1f }, 1, 0, 0 };
                viewer->SetBackground(b.mode != 0 ? b.color : nullptr, b.mode == 2 ? b.bottom : nullptr);
                viewer->SetSkyIntensity(skyBase * std::max(b.skyIntensity, 0.0f));
                viewer->SetSkyView(b.skyRotation / 360.0f, std::clamp(b.skyBlur, 0.0f, 1.0f));
            }
            {
                float scale = std::clamp(renderScale.load(), 0.25f, 2.0f);
                viewer->Resize(std::max(16u, static_cast<uint32_t>(static_cast<float>(width) * scale)),
                               std::max(16u, static_cast<uint32_t>(static_cast<float>(height) * scale)));
            }
            ApplyViewerSettings();
            if (image) {
                StepImage(frameInput, panX, panY, cursorX, cursorY, dt, selectNow, selectLod, selectSubmesh,
                          pickNow, pickAtX, pickAtY);
            }
            if (resetCamera.exchange(false)) {
                float aspect = static_cast<float>(viewer->GetWidth()) / static_cast<float>(viewer->GetHeight());
                bool selection = hasScene && hasSelectionBounds;
                const float* center = selection ? selectionCenter : framing.center;
                float radius = selection ? selectionRadius : framing.radius;
                if (viewKind == ViewportView::Perspective) {
                    camera.FocusOn(center, radius, aspect);
                } else {
                    std::copy(center, center + 3, orthoCenter);
                    orthoHalfHeight = std::max(radius * 1.2f, 0.05f);
                }
            }
            {
                std::lock_guard lock(mutex);
                if (poseRequested) {
                    EnterView(ViewportView::Perspective);
                    camera.SetPose(requestedPose);
                }
                poseRequested = false;
            }
            if (int32_t requested = viewRequest.exchange(-1); requested >= 0 && requested <= 6) {
                EnterView(static_cast<ViewportView>(requested));
            }
            if (float speed = speedRequest.exchange(-1); speed > 0) camera.SetSpeed(speed);
            if (float fov = fovRequest.exchange(-1); fov > 0 && viewKind == ViewportView::Perspective) camera.SetFov(fov);
            ApplyBookmarks();
            if (viewKind == ViewportView::Perspective) {
                camera.Update(frameInput, dt);
            } else {
                UpdateViewCamera(frameInput, panX, panY, dt);
            }
            speedShown = camera.GetSpeed();
            {
                std::lock_guard lock(mutex);
                shownPose = camera.GetPose();
            }
            viewer->SetFov(camera.GetFov());
            {
                std::lock_guard lock(mutex);
                viewer->SetProjectionJitter(jitter[0], jitter[1]);
            }
            float eye[3];
            float target[3];
            float up[3];
            camera.GetEye(eye);
            camera.GetTarget(target);
            camera.GetUp(up);
            viewer->SetCamera(eye, target, up);
            // Orthographic views stand far back: zones and LODs follow the point they look at.
            const float* focus = viewKind == ViewportView::Perspective ? eye : orthoCenter;
            if (hasScene) {
                if (postProcessZones) postProcessZones->Update(*viewer, focus, dt);
                if (probeNetworks) probeNetworks->Update(*viewer, focus);
                UpdateSceneLods(focus);
                UpdateMeshLods(eye, target);
            }
            bool effectsShown = (showMask.load() & SHOW_EFFECTS) != 0 || effect;
            if (effect) StepEffect(dt, eye);
            else if (!sceneEffects.empty() && effectsShown) StepSceneEffects(dt, eye);
            else if (!sceneEffects.empty()) viewer->SetParticles({});
            ViewportShading mode = shading.load();
            bool collisionView = mode == ViewportShading::Collision && hasScene && !meshAsset && !materialPreview &&
                                 !effect && !image && !collision;
            bool lit = mode == ViewportShading::Lit || mode == ViewportShading::LightingOnly;
            viewer->SetUnlit(!lit);
            viewer->SetWireframe(mode == ViewportShading::Wireframe);
            viewer->SetGBufferView(mode == ViewportShading::Normals     ? GBufferView::Normals
                                   : mode == ViewportShading::Roughness ? GBufferView::Roughness
                                   : mode == ViewportShading::Metallic  ? GBufferView::Metallic
                                   : mode == ViewportShading::Occlusion ? GBufferView::Occlusion
                                   : mode == ViewportShading::BaseColor ? GBufferView::BaseColor
                                   : mode == ViewportShading::Emissive  ? GBufferView::Emissive
                                   : mode == ViewportShading::Depth     ? GBufferView::Depth
                                   : mode == ViewportShading::Velocity  ? GBufferView::Velocity
                                                                        : GBufferView::None);
            viewer->SetDebugView(mode == ViewportShading::LightingOnly      ? ViewerDebugView::LightingOnly
                                 : mode == ViewportShading::LodColoration   ? ViewerDebugView::LodColoration
                                 : mode == ViewportShading::LightComplexity ? ViewerDebugView::LightComplexity
                                 : mode == ViewportShading::Overdraw        ? ViewerDebugView::Overdraw
                                                                            : ViewerDebugView::None);
            if (mode == ViewportShading::LightComplexity) {
                std::vector<float> spheres;
                spheres.reserve(sceneLights.size() * 4);
                for (std::size_t i = 0; i < sceneLights.size(); i++) {
                    if (i < lightHidden.size() && lightHidden[i]) continue;
                    const GameLightParam& light = sceneLights[i];
                    spheres.insert(spheres.end(), { light.position[0], light.position[1], light.position[2], light.boundingRadius });
                }
                viewer->SetLightSpheres(spheres);
            }
            viewer->SetCollisionView(collisionView);
            if (collisionGroupsNow) {
                allCollisionGroups = false;
                shownCollisionGroups = std::set<std::string>(collisionGroups.begin(), collisionGroups.end());
                collisionDirty = true;
            }
            if (aiMapGroupsNow) {
                allAiMaps = false;
                shownAiMaps = std::set<std::string>(aiMapNames.begin(), aiMapNames.end());
                aiMapDirty = true;
            }
            bool aiMapView = hasScene && !sceneAiMaps.empty() && (aiMapsAlways || mode == ViewportShading::AiMap);
            if (aiMapView != aiMapOverlayOn) {
                aiMapOverlayOn = aiMapView;
                aiMapDirty = true;
            }
            uint32_t shownGizmos = gizmoMask.load();
            bool gizmosOn = (shownGizmos & GIZMOS_ENABLED) != 0;
            viewer->SetGrid(grid.load() && gizmosOn);
            viewer->SetSelectionOutline(gizmosOn && (shownGizmos & GIZMO_SELECTION) != 0);
            {
                std::lock_guard lock(mutex);
                viewer->SetEnvironment(environment);
            }
            if (hasScene) {
                if (selectNow) {
                    if (meshAsset && selectLod >= 0 && selectLod < static_cast<int32_t>(lodCount) &&
                        selectLod != displayedLod) {
                        ShowLod(selectLod);
                    }
                    SetSelection(SubmeshDraws(selectSubmesh), selectSubmesh, {});
                }
                if (hiddenNow && effect) {
                    std::vector<uint8_t> hiddenEmitters(effect->Data().emitters.size(), 0);
                    for (const std::string& key : hidden) {
                        std::size_t index = 0;
                        if (key.starts_with("emitter:") && std::sscanf(key.c_str() + 8, "%zu", &index) == 1 &&
                            index < hiddenEmitters.size()) {
                            hiddenEmitters[index] = 1;
                        }
                    }
                    effect->SetHidden(std::move(hiddenEmitters));
                } else if (hiddenNow) {
                    if (collision) {
                        hiddenLayers.clear();
                        for (const std::string& key : hidden) {
                            int layer = 0;
                            if (key.starts_with("layer:") && std::sscanf(key.c_str() + 6, "%d", &layer) == 1) hiddenLayers.insert(layer);
                        }
                        float lo[3];
                        float hi[3];
                        UpdateCollisionOverlay(lo, hi);
                    }
                    if (aiMap) hiddenAiMapParts = std::set<std::string>(hidden.begin(), hidden.end());
                    hiddenOwners = std::set<std::string>(hidden.begin(), hidden.end());
                    UpdateLightVisibility();
                    skeletonsDirty = true;
                    collisionDirty = true;
                    aiMapDirty = true;
                    hiddenInstances.assign(pick.worlds.size(), 0);
                    for (const std::string& key : hidden) {
                        auto it = pick.instancesByOwner.find(key);
                        if (it == pick.instancesByOwner.end()) continue;
                        for (uint32_t instance : it->second) hiddenInstances[instance] = 1;
                    }
                    UpdateDrawMask();
                }
                for (const auto& [key, values] : params) {
                    std::size_t emitter = 0;
                    std::string field;
                    if (std::string objectKey; ParseTransformParamKey(key, objectKey, field)) {
                        SetObjectTransformField(objectKey, field, values);
                        continue;
                    }
                    if (effect && ParseEffectParamKey(key, effectPath, emitter, field)) {
                        if (emitter < effect->Data().emitters.size() &&
                            SetEffectParam(effect->Data().emitters[emitter], field, values) &&
                            field.starts_with("transform.")) {
                            effect->RefreshTransforms();
                        }
                        continue;
                    }
                    auto [first, last] = pick.params.equal_range(key);
                    for (auto it = first; it != last; ++it) {
                        std::size_t count = std::min<std::size_t>(values.size(), it->second.count);
                        viewer->UpdateMaterialParams(it->second.materialIndex, it->second.offset,
                                                     std::span(reinterpret_cast<const uint8_t*>(values.data()), count * 4));
                    }
                }
                if (objectsNow) {
                    SetSelection(ObjectDraws(selectObjects), -1, selectObjects.empty() ? std::string() : selectObjects[0]);
                    selectedOwners = std::set<std::string>(selectObjects.begin(), selectObjects.end());
                    activeObject = selectObjects.empty() ? std::string() : selectObjects[0];
                    if (!hasSelectionBounds) hasSelectionBounds = CollidersBounds(selectedOwners, selectionCenter, selectionRadius);
                    if (!hasSelectionBounds) hasSelectionBounds = LightsBounds(selectedOwners, selectionCenter, selectionRadius);
                    skeletonsDirty = true;
                    collisionDirty = true;
                }
                if (pickNow && !materialPreview) {
                    if (meshAsset) {
                        int32_t id = PickDraw(pickAtX, pickAtY);
                        bool mainMesh = id >= 0 && pick.draws[id].instanceIndex == mainInstance;
                        int32_t submesh = mainMesh ? static_cast<int32_t>(pick.tags[id].submesh) : -1;
                        SetSelection(SubmeshDraws(submesh), submesh, {});
                    } else {
                        std::string owner;
                        if (int32_t light = lightIconMask != 0 ? PickLight(pickAtX, pickAtY, !collisionView) : -1; light >= 0) {
                            owner = lightMarkers[light].owner;
                        } else if (collisionView) {
                            owner = PickCollider(pickAtX, pickAtY);
                        } else if (int32_t id = PickDraw(pickAtX, pickAtY); id >= 0) {
                            const ViewerMeshDraw& draw = pick.draws[id];
                            owner = pick.owners[draw.instanceIndex];
                            std::string material;
                            if (draw.instanceIndex < pick.instanceMaterials.size()) {
                                const std::vector<uint32_t>& slots = pick.instanceMaterials[draw.instanceIndex];
                                if (draw.materialSlot < slots.size() && slots[draw.materialSlot] < pick.materialNames.size()) {
                                    material = pick.materialNames[slots[draw.materialSlot]];
                                }
                            }
                            std::string text = owner + '\t' +
                                (draw.instanceIndex < pick.meshes.size() ? pick.meshes[draw.instanceIndex] : std::string()) +
                                '\t' + material + '\t' + std::to_string(draw.pipelineIndex) + '\t' + std::to_string(id);
                            std::lock_guard lock(mutex);
                            pickedDraw = std::move(text);
                        }
                        SetSelection(ObjectDraws({ owner }), -1, owner);
                        selectedOwners.clear();
                        if (!owner.empty()) selectedOwners.insert(owner);
                        activeObject = owner;
                        if (!hasSelectionBounds) hasSelectionBounds = CollidersBounds(selectedOwners, selectionCenter, selectionRadius);
                        if (!hasSelectionBounds) hasSelectionBounds = LightsBounds(selectedOwners, selectionCenter, selectionRadius);
                        skeletonsDirty = true;
                        collisionDirty = true;
                    }
                    std::lock_guard lock(mutex);
                    selectionSerial++;
                }
            }
            if (followJoint >= 0 && frameInput.rightMouse) followCharacter = false;
            if (hasScene && !objects.empty()) {
                for (uint32_t n = undoRequests.exchange(0); n > 0; n--) ApplyUndo(false);
                for (uint32_t n = redoRequests.exchange(0); n > 0; n--) ApplyUndo(true);
                if (floorRequests.exchange(0) > 0) SnapToFloor();
            }
            UpdateTransformGizmo();
            UpdateMeasure();
            if (!poses.empty()) {
                StepMotion(dt);
                FollowCharacter(dt);
            }
            float overlayEye[3];
            camera.GetEye(overlayEye);
            bool eyeMoved = skeletonSolid && !std::equal(overlayEye, overlayEye + 3, skeletonEye);
            if (skeletonsDirty.exchange(false) || eyeMoved) UpdateSkeletonOverlay(overlayEye);
            if (collisionDirty) {
                if (!sceneCollision.colliders.empty()) UpdateCollisionInstances();
                collisionDirty = false;
            }
            if (aiMapDirty) {
                aiMapDirty = false;
                if (aiMap || !sceneAiMaps.empty()) {
                    float lo[3];
                    float hi[3];
                    UpdateAiMapOverlay(lo, hi);
                }
            }
            bool gizmosShown = hasScene && !meshAsset && !materialPreview && !effect && !image && !lightMarkers.empty();
            uint32_t mask = gizmoMask.load();
            UpdateLightGizmos(gizmosShown && (mask & GIZMOS_ENABLED) != 0 ? mask & 7u : 0u, overlayEye);
            // Realtime off: a frame only after input, a request or a camera move, and a slow heartbeat.
            if (!realtime.load() && !captureTarget && !targetsTarget) {
                CameraPose pose = camera.GetPose();
                bool moved = std::memcmp(&pose, &renderedPose, sizeof(pose)) != 0;
                uint32_t serial = inputSerial.load();
                bool busy = pickNow || selectNow || objectsNow || !params.empty() || hiddenNow || gizmo.Dragging() != GizmoHandle::None;
                if (!moved && serial == renderedInputSerial && !busy && Clock::now() - lastRendered < IDLE_REDRAW_INTERVAL) {
                    std::unique_lock lock(mutex);
                    wake.wait_for(lock, std::chrono::milliseconds(15));
                    continue;
                }
            }
            renderedPose = camera.GetPose();
            renderedInputSerial = inputSerial.load();
            lastRendered = Clock::now();
            Clock::time_point renderStart = Clock::now();
            std::vector<uint8_t> frameCapture;
            if (captureTarget) viewer->RequestCapture(&frameCapture);
            ViewerTargetCapture frameTargets;
            if (targetsTarget) viewer->RequestTargetCapture(&frameTargets);
            viewer->RenderFrame();
            if (targetsTarget) {
                std::lock_guard lock(mutex);
                targetCapture = std::move(frameTargets);
                targetsDone = targetsTarget;
                captured.notify_all();
            }
            if (captureTarget) {
                std::lock_guard lock(mutex);
                capturePixels = std::move(frameCapture);
                captureWidth = viewer->GetWidth();
                captureHeight = viewer->GetHeight();
                captureDone = captureTarget;
                captured.notify_all();
            }
            statsRenderMs += std::chrono::duration<double, std::milli>(Clock::now() - renderStart).count();
            statsFrames++;
            const ViewerFrameTimings& t = viewer->GetFrameTimings();
            statsTimings.cullMs += t.cullMs;
            statsTimings.recordMs += t.recordMs;
            statsTimings.presentMs += t.presentMs;
            statsTimings.waitMs += t.waitMs;
            statsTimings.gpuPrepassMs += t.gpuPrepassMs;
            statsTimings.gpuGBufferMs += t.gpuGBufferMs;
            statsTimings.gpuRestMs += t.gpuRestMs;
            static uint32_t gpuDiagThrottle = 0;
            if ((gpuDiagThrottle++ % 90) == 0) {
                LogInfo("GPU ms: prepass=%.2f gbuffer=%.2f shadow=%.2f cacao=%.2f gi=%.2f lighting=%.2f forward+fog=%.2f rest=%.2f | shadowDraws=%u shadowPsoSwitches=%u | cullMs=%.2f recordMs=%.2f presentMs=%.2f waitMs=%.2f",
                        t.gpuPrepassMs, t.gpuGBufferMs, t.gpuShadowMs, t.gpuCacaoMs, t.gpuGiMs, t.gpuLightingMs,
                        t.gpuForwardFogMs, t.gpuRestMs, t.shadowDraws, t.shadowPipelineSwitches,
                        t.cullMs, t.recordMs, t.presentMs, t.waitMs);
            }
        } catch (const std::exception& e) {
            LogError("viewport: rendering stopped: %s", e.what());
            viewer.reset();
            hasScene = false;
            deviceLost = true;
            std::lock_guard lock(mutex);
            stats.hasScene = 0;
            stats.deviceLost = 1;
            continue;
        }

        double elapsed = std::chrono::duration<double>(Clock::now() - statsStart).count();
        if (elapsed >= 0.5) {
            std::lock_guard lock(mutex);
            stats.fps = static_cast<float>(statsFrames / elapsed);
            stats.frameMs = statsFrames > 0 ? static_cast<float>(statsRenderMs / statsFrames) : 0.0f;
            float frames = static_cast<float>(std::max(statsFrames, 1));
            stats.cpuCullMs = statsTimings.cullMs / frames;
            stats.cpuRecordMs = statsTimings.recordMs / frames;
            stats.presentMs = statsTimings.presentMs / frames;
            stats.gpuWaitMs = statsTimings.waitMs / frames;
            stats.gpuPrepassMs = statsTimings.gpuPrepassMs / frames;
            stats.gpuGBufferMs = statsTimings.gpuGBufferMs / frames;
            stats.gpuRestMs = statsTimings.gpuRestMs / frames;
            stats.drawn = viewer->GetFrameTimings().drawn;
            stats.triangles = viewer->GetFrameTimings().triangles;
            statsTimings = {};
            stats.width = viewer->GetWidth();
            stats.height = viewer->GetHeight();
            stats.deviceLost = deviceLost;
            statsStart = Clock::now();
            statsFrames = 0;
            statsRenderMs = 0;
            statsChanged = true;
        }
        if (viewer) UpdateStatsOverlay(statsChanged);
        statsChanged = false;

        if (!hasScene && !viewer->HasSky()) {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(50));
        }
    }

    {
        std::lock_guard lock(mutex);
        if (pendingJob) {
            pendingJob->done.set_exception(std::make_exception_ptr(std::runtime_error("viewport closed")));
            pendingJob.reset();
        }
    }
    movie.reset();
    viewer.reset();
    if (com) CoUninitialize();
    finished = true;
}
