#ifndef REASSETEXPLORER_NATIVEMOVIE_H
#define REASSETEXPLORER_NATIVEMOVIE_H
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include "Core/LoadedGame.h"
#include "NativeAudio.h"

// RE7 and RE8 movies are ASF with VC-1/WMV video and WMA audio.
struct MovieInfo {
    std::string source;       // pak path read: the streaming copy when there is one
    uint64_t bytes = 0;
    double duration = 0;
    std::string videoCodec;
    uint32_t width = 0;
    uint32_t height = 0;
    double frameRate = 0;
    std::string audioCodec;   // empty without an audio stream
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
};

MovieInfo ReadMovieInfo(const LoadedGame& game, const std::string& pakPath);

// Audio is decoded ahead by a worker thread; video on demand to match the audio clock (wall
// time without audio). Not thread-safe; one caller drives it.
class MoviePlayer {
public:
    MoviePlayer(const LoadedGame& game, const std::string& pakPath);
    ~MoviePlayer();

    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;

    const MovieInfo& Info() const { return info; }
    // True when Frame() changed.
    bool Update(float dt);
    // Top-down BGRX rows of Info().width * 4 bytes.
    const std::vector<uint8_t>& Frame() const { return frame; }
    void SetPaused(bool paused);
    bool Paused() const { return paused; }
    void Seek(double seconds);
    double Position() const;

private:
    void DecodeAudio(Microsoft::WRL::ComPtr<IMFSourceReader> reader, uint64_t capacity);
    bool NextVideoSample();
    void TakeFrame(IMFSample* sample, int64_t time);

    MovieInfo info;
    Microsoft::WRL::ComPtr<IMFSourceReader> video;
    Microsoft::WRL::ComPtr<IMFSample> pending;
    int64_t pendingTime = 0;
    int64_t frameTime = -1;   // 100 ns units; -1 until a frame is shown after a seek
    bool videoEnded = false;
    int32_t stride = 0;
    std::vector<uint8_t> frame;

    std::unique_ptr<AudioPlayer> audio;
    std::thread audioThread;
    std::atomic<bool> stopAudio{ false };
    double clock = 0;
    bool paused = false;
};

#endif
