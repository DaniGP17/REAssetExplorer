#ifndef REASSETEXPLORER_NATIVETHUMBNAILS_H
#define REASSETEXPLORER_NATIVETHUMBNAILS_H
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#include "Core/LoadedGame.h"
#include "Explorer/SceneBuilder.h"

// Renders on its own thread into a hidden window only that thread touches: DXGI talks to a
// swapchain's window on the window's thread.
class ThumbnailRenderer {
public:
    ThumbnailRenderer();
    ~ThumbnailRenderer();

    ThumbnailRenderer(const ThumbnailRenderer&) = delete;
    ThumbnailRenderer& operator=(const ThumbnailRenderer&) = delete;

    // Blocks until done; size x size top-down RGBA8. Throws when the asset has no preview.
    std::vector<uint8_t> Render(const LoadedGame& game, const std::string& pakPath, uint32_t size);

private:
    struct Job {
        const LoadedGame* game;
        std::string path;
        uint32_t size;
        std::promise<std::vector<uint8_t>> done;
    };

    struct PreviewCamera {
        float eye[3];
        float target[3];
        float radius;
        float distance;
    };

    void Loop();
    std::vector<uint8_t> RenderJob(const Job& job);
    std::vector<uint8_t> RenderMesh(const LoadedGame& game, const std::string& path, uint32_t pixels);
    std::vector<uint8_t> RenderTexture(const LoadedGame& game, const std::string& path, uint32_t pixels);
    std::vector<uint8_t> RenderMaterial(const LoadedGame& game, const std::string& path, const std::string& material,
                                        uint32_t pixels);
    std::vector<uint8_t> RenderBuild(const LoadedGame& game, SceneBuild& build, const TextureCache& textures,
                                     uint32_t pixels, const PreviewCamera& camera);
    static PreviewCamera FrameSphere(const float center[3], float radius, const float viewDir[3]);
    static void ShadeSphere(std::vector<uint8_t>& frame, uint32_t pixels, const PreviewCamera& camera, float radius);

    HWND hwnd = nullptr;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::unique_ptr<Job>> jobs;
    bool running = true;
};

#endif
