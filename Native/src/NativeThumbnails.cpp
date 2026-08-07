#include "NativeThumbnails.h"

#include <algorithm>
#include <cmath>

#include "Core/Assets/Readers/TexReader.h"
#include "Explorer/Log.h"
#include "Explorer/SceneBuilder.h"
#include "Explorer/SceneUpload.h"
#include "Renderer/Viewer.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

constexpr float BACKGROUND[3] = { 0.20f, 0.20f, 0.21f };
constexpr float THUMBNAIL_FOV = 0.6f;
constexpr float SPHERE_RADIUS = 0.5f;  // AppendMaterialSphere

float Dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void Normalize(float v[3]) {
    float len = std::sqrt(Dot(v, v));
    if (len > 0) for (int i = 0; i < 3; i++) v[i] /= len;
}

bool HasExtension(const std::string& path, const char* extension) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower.find(extension) != std::string::npos;
}

// Rendered at twice the size and box-filtered down, for cheap antialiasing.
std::vector<uint8_t> Downsample(const std::vector<uint8_t>& source, uint32_t size) {
    uint32_t sourceSize = size * 2;
    std::vector<uint8_t> out(static_cast<std::size_t>(size) * size * 4);
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            for (int c = 0; c < 3; c++) {
                uint32_t sum = 0;
                for (uint32_t dy = 0; dy < 2; dy++) {
                    for (uint32_t dx = 0; dx < 2; dx++) {
                        sum += source[((y * 2 + dy) * sourceSize + x * 2 + dx) * 4 + c];
                    }
                }
                out[(y * size + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
            }
            out[(y * size + x) * 4 + 3] = 255;
        }
    }
    return out;
}

}

ThumbnailRenderer::ThumbnailRenderer() {
    thread = std::thread(&ThumbnailRenderer::Loop, this);
}

ThumbnailRenderer::~ThumbnailRenderer() {
    {
        std::lock_guard lock(mutex);
        running = false;
    }
    wake.notify_all();
    thread.join();
}

std::vector<uint8_t> ThumbnailRenderer::Render(const LoadedGame& game, const std::string& pakPath, uint32_t size) {
    auto job = std::make_unique<Job>();
    job->game = &game;
    job->path = pakPath;
    job->size = size;
    std::future<std::vector<uint8_t>> done = job->done.get_future();
    {
        std::lock_guard lock(mutex);
        if (!running) throw std::runtime_error("thumbnails stopped");
        jobs.push_back(std::move(job));
    }
    wake.notify_all();
    return done.get();
}

void ThumbnailRenderer::Loop() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(&__ImageBase);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = L"REAssetNativeThumbnails";
    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 64, 64,
                           nullptr, nullptr, instance, nullptr);
    MuteLogOnThisThread(true);

    while (true) {
        std::unique_ptr<Job> job;
        {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(100), [this] { return !running || !jobs.empty(); });
            if (!running) break;
            if (!jobs.empty()) {
                job = std::move(jobs.front());
                jobs.pop_front();
            }
        }
        MSG msg;
        while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        if (!job) continue;
        try {
            job->done.set_value(RenderJob(*job));
        } catch (...) {
            job->done.set_exception(std::current_exception());
        }
    }

    std::lock_guard lock(mutex);
    for (std::unique_ptr<Job>& job : jobs) {
        job->done.set_exception(std::make_exception_ptr(std::runtime_error("thumbnails stopped")));
    }
    jobs.clear();
    if (hwnd) DestroyWindow(hwnd);
}

std::vector<uint8_t> ThumbnailRenderer::RenderJob(const Job& job) {
    if (hwnd == nullptr) throw std::runtime_error("no thumbnail window");
    uint32_t pixels = job.size * 2;
    std::string path = job.path;
    std::string element;
    if (std::size_t bar = path.find('|'); bar != std::string::npos) {
        element = path.substr(bar + 1);
        path.resize(bar);
    }
    std::vector<uint8_t> frame;
    if (HasExtension(path, ".mesh.")) frame = RenderMesh(*job.game, path, pixels);
    else if (HasExtension(path, ".tex.")) frame = RenderTexture(*job.game, path, pixels);
    else if (HasExtension(path, ".mdf2.")) frame = RenderMaterial(*job.game, path, element, pixels);
    else throw std::runtime_error("no thumbnail for this kind of file");
    if (frame.size() != static_cast<std::size_t>(pixels) * pixels * 4) throw std::runtime_error("thumbnail capture failed");
    return Downsample(frame, job.size);
}

std::vector<uint8_t> ThumbnailRenderer::RenderMesh(const LoadedGame& game, const std::string& path, uint32_t pixels) {
    SceneBuild build;
    TextureCache textures;
    SceneMeshAsset asset = AppendSceneMesh(game, build, textures, path, FindMeshMaterial(game, path));
    if (!asset.valid) throw std::runtime_error("mesh not loaded");
    AddSceneInstance(build, asset, Identity());

    float center[3];
    float extent[3];
    for (int i = 0; i < 3; i++) {
        center[i] = (build.aabbMin[i] + build.aabbMax[i]) * 0.5f;
        extent[i] = (build.aabbMax[i] - build.aabbMin[i]) * 0.5f;
    }
    float radius = std::max(std::sqrt(Dot(extent, extent)), 0.01f);
    const float dir[3] = { 0.55f, 0.35f, -1.0f };
    return RenderBuild(game, build, textures, pixels, FrameSphere(center, radius, dir));
}

ThumbnailRenderer::PreviewCamera ThumbnailRenderer::FrameSphere(const float center[3], float radius,
                                                                const float viewDir[3]) {
    PreviewCamera camera{};
    float dir[3] = { viewDir[0], viewDir[1], viewDir[2] };
    Normalize(dir);
    camera.radius = radius;
    camera.distance = radius / std::sin(THUMBNAIL_FOV * 0.5f);
    for (int i = 0; i < 3; i++) {
        camera.target[i] = center[i];
        camera.eye[i] = center[i] + dir[i] * camera.distance;
    }
    return camera;
}

std::vector<uint8_t> ThumbnailRenderer::RenderBuild(const LoadedGame& game, SceneBuild& build,
                                                    const TextureCache& textures, uint32_t pixels,
                                                    const PreviewCamera& camera) {
    if (build.draws.empty() || build.masters.prepass == nullptr) throw std::runtime_error("nothing to draw");

    // Game PSOs are created once per Viewer, so each preview gets a fresh one.
    Viewer viewer(hwnd, pixels, pixels);
    SceneLook look;
    look.skyPath.clear();
    look.unlit = true;
    UploadScene(viewer, game, build, textures, look);
    viewer.SetGrid(false);
    viewer.SetBackground(BACKGROUND);
    viewer.SetClearColor(BACKGROUND[0], BACKGROUND[1], BACKGROUND[2]);
    viewer.SetFov(THUMBNAIL_FOV);
    viewer.SetClipPlanes(std::max(camera.distance - camera.radius * 1.5f, camera.distance * 0.01f),
                         camera.distance + camera.radius * 1.5f);
    viewer.SetCamera(camera.eye, camera.target);

    std::vector<uint8_t> frame;
    viewer.RequestCapture(&frame);
    viewer.RenderFrame();
    return frame;
}

std::vector<uint8_t> ThumbnailRenderer::RenderTexture(const LoadedGame& game, const std::string& path, uint32_t pixels) {
    TextureData tex = game.Readers().Get<TexReader>()->Read(game.ExtractFile(path));
    GameTextureDesc desc = ToGameTexture(tex);
    if (desc.arraySize <= 1) {
        std::size_t first = 0;
        uint32_t w = desc.width;
        uint32_t h = desc.height;
        while (first + 1 < desc.mips.size() && std::max(w, h) > pixels * 2) {
            first++;
            w = std::max(w / 2, 1u);
            h = std::max(h / 2, 1u);
        }
        desc.mips.erase(desc.mips.begin(), desc.mips.begin() + static_cast<std::ptrdiff_t>(first));
        desc.width = w;
        desc.height = h;
    }

    Viewer viewer(hwnd, pixels, pixels);
    viewer.SetClearColor(BACKGROUND[0], BACKGROUND[1], BACKGROUND[2]);
    viewer.SetBackground(BACKGROUND);
    viewer.SetPreviewTexture(desc);
    std::vector<uint8_t> frame;
    viewer.RequestCapture(&frame);
    viewer.RenderFrame();
    return frame;
}

std::vector<uint8_t> ThumbnailRenderer::RenderMaterial(const LoadedGame& game, const std::string& path,
                                                       const std::string& material, uint32_t pixels) {
    SceneBuild build;
    TextureCache textures;
    SceneMeshAsset asset = AppendMaterialSphere(game, build, textures, path, material);
    if (!asset.valid) throw std::runtime_error("material not loaded");
    AddSceneInstance(build, asset, Identity());
    const float center[3] = { 0, 0, 0 };
    const float dir[3] = { 0.0f, 0.15f, -1.0f };
    PreviewCamera camera = FrameSphere(center, SPHERE_RADIUS * 1.08f, dir);
    std::vector<uint8_t> frame = RenderBuild(game, build, textures, pixels, camera);
    ShadeSphere(frame, pixels, camera, SPHERE_RADIUS);
    return frame;
}

// Game lighting expects probes and shadow maps a preview lacks, so the sphere renders unlit
// and is shaded here, with normals from the view ray against the analytic sphere.
void ThumbnailRenderer::ShadeSphere(std::vector<uint8_t>& frame, uint32_t pixels, const PreviewCamera& camera,
                                    float radius) {
    float forward[3] = { camera.target[0] - camera.eye[0], camera.target[1] - camera.eye[1],
                         camera.target[2] - camera.eye[2] };
    Normalize(forward);
    float right[3] = { -forward[2], 0, forward[0] };
    Normalize(right);
    float up[3] = { right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                    right[0] * forward[1] - right[1] * forward[0] };
    float light[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; i++) light[i] = -right[i] * 0.6f + up[i] * 0.7f - forward[i] * 0.55f;
    Normalize(light);

    const uint8_t background[3] = { static_cast<uint8_t>(std::lround(BACKGROUND[0] * 255)),
                                    static_cast<uint8_t>(std::lround(BACKGROUND[1] * 255)),
                                    static_cast<uint8_t>(std::lround(BACKGROUND[2] * 255)) };
    float tanHalf = std::tan(THUMBNAIL_FOV * 0.5f);
    float oc[3] = { camera.eye[0] - camera.target[0], camera.eye[1] - camera.target[1], camera.eye[2] - camera.target[2] };
    for (uint32_t y = 0; y < pixels; y++) {
        for (uint32_t x = 0; x < pixels; x++) {
            uint8_t* px = frame.data() + (static_cast<std::size_t>(y) * pixels + x) * 4;
            if (std::abs(px[0] - background[0]) <= 1 && std::abs(px[1] - background[1]) <= 1 &&
                std::abs(px[2] - background[2]) <= 1) {
                continue;
            }
            float nx = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(pixels) - 1.0f) * tanHalf;
            float ny = (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(pixels)) * tanHalf;
            float dir[3];
            for (int i = 0; i < 3; i++) dir[i] = forward[i] + right[i] * nx + up[i] * ny;
            Normalize(dir);
            float b = Dot(oc, dir);
            float h = b * b - (Dot(oc, oc) - radius * radius);
            float normal[3];
            if (h >= 0) {
                float t = -b - std::sqrt(h);
                for (int i = 0; i < 3; i++) normal[i] = (oc[i] + dir[i] * t) / radius;
            } else {
                // Silhouette pixels just outside the analytic sphere: use its rim.
                float along = -b;
                for (int i = 0; i < 3; i++) normal[i] = oc[i] + dir[i] * along;
                Normalize(normal);
            }
            float diffuse = std::max(Dot(normal, light), 0.0f);
            float ambient = 0.16f + 0.14f * (normal[1] * 0.5f + 0.5f);
            float halfway[3] = { light[0] - dir[0], light[1] - dir[1], light[2] - dir[2] };
            Normalize(halfway);
            float specular = std::pow(std::max(Dot(normal, halfway), 0.0f), 40.0f) * 0.22f;
            for (int c = 0; c < 3; c++) {
                float base = std::pow(px[c] / 255.0f, 2.2f);
                float lit = std::min(base * (ambient + 0.95f * diffuse) + specular, 1.0f);
                px[c] = static_cast<uint8_t>(std::lround(std::pow(lit, 1.0f / 2.2f) * 255.0f));
            }
        }
    }
}
