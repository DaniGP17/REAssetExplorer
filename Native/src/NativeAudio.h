#ifndef REASSETEXPLORER_NATIVEAUDIO_H
#define REASSETEXPLORER_NATIVEAUDIO_H
#include <atomic>
#include <memory>
#include <mutex>

#include "Core/Audio/WemDecoder.h"

// One sound at a time; it stays loaded after it ends or is stopped, so Seek can start it again.
class AudioPlayer {
public:
    enum class State { Stopped, Playing, Paused };

    AudioPlayer();
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    void Play(DecodedAudio audio);
    // A sound another thread is still decoding into StreamData() (capacity frames, zeroed). The
    // cursor waits at the last published frame until Publish reaches further or finishes the stream.
    void PlayStream(uint32_t sampleRate, uint32_t channels, uint64_t capacityFrames);
    float* StreamData() { return sound.samples.data(); }
    void Publish(uint64_t frames, bool finished);
    void Stop();
    void Seek(double seconds);
    void SetPaused(bool paused);
    State Status(double& position, double& duration);
    uint32_t Waveform(float* out, uint32_t columns, uint32_t maxChannels);

private:
    struct Device;

    void StartDevice();
    void Mix(float* output, uint32_t frames);

    std::mutex mutex;
    std::unique_ptr<Device> device;
    DecodedAudio sound;
    std::atomic<uint64_t> cursor{ 0 };
    std::atomic<bool> playing{ false };
    std::atomic<bool> paused{ false };
    // Frames Mix may read; all of them unless streaming.
    std::atomic<uint64_t> ready{ 0 };
    std::atomic<bool> complete{ true };
};

#endif
