#include "NativeAudio.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

// miniaudio (public domain): playback only, WASAPI only.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

struct AudioPlayer::Device {
    ma_device device;
};

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() {
    Stop();
}

void AudioPlayer::Play(DecodedAudio audio) {
    if (audio.channels == 0 || audio.sampleRate == 0) throw std::runtime_error("audio: nothing to play");
    std::lock_guard lock(mutex);
    if (device) {
        // Waits for the callback, so the old buffer can be dropped.
        ma_device_uninit(&device->device);
        device.reset();
    }
    sound = std::move(audio);
    cursor = 0;
    paused = false;
    ready = sound.FrameCount();
    complete = true;
    StartDevice();
}

void AudioPlayer::PlayStream(uint32_t sampleRate, uint32_t channels, uint64_t capacityFrames) {
    if (channels == 0 || sampleRate == 0) throw std::runtime_error("audio: nothing to play");
    std::lock_guard lock(mutex);
    if (device) {
        ma_device_uninit(&device->device);
        device.reset();
    }
    sound.sampleRate = sampleRate;
    sound.channels = channels;
    sound.samples.assign(capacityFrames * channels, 0.0f);
    cursor = 0;
    paused = false;
    ready = 0;
    complete = false;
    StartDevice();
}

void AudioPlayer::Publish(uint64_t frames, bool finished) {
    ready = std::min<uint64_t>(frames, sound.FrameCount());
    if (finished) complete = true;
}

void AudioPlayer::Stop() {
    std::lock_guard lock(mutex);
    playing = false;
    paused = false;
    cursor = 0;
    if (!device) return;
    ma_device_uninit(&device->device);
    device.reset();
}

void AudioPlayer::Seek(double seconds) {
    std::lock_guard lock(mutex);
    if (sound.channels == 0) return;
    uint64_t frame = static_cast<uint64_t>(std::max(seconds, 0.0) * sound.sampleRate);
    cursor = std::min<uint64_t>(frame, sound.FrameCount());
    playing = true;
    if (!device) StartDevice();
}

void AudioPlayer::SetPaused(bool value) {
    paused = value;
}

AudioPlayer::State AudioPlayer::Status(double& position, double& duration) {
    std::lock_guard lock(mutex);
    double rate = sound.sampleRate ? sound.sampleRate : 1;
    position = static_cast<double>(cursor.load()) / rate;
    duration = static_cast<double>(complete.load() ? ready.load() : sound.FrameCount()) / rate;
    if (!playing.load()) return State::Stopped;
    return paused.load() ? State::Paused : State::Playing;
}

uint32_t AudioPlayer::Waveform(float* out, uint32_t columns, uint32_t maxChannels) {
    std::lock_guard lock(mutex);
    uint32_t channels = std::min(sound.channels, maxChannels);
    std::size_t frames = sound.FrameCount();
    if (channels == 0 || columns == 0 || frames == 0) return 0;
    for (uint32_t c = 0; c < channels; c++) {
        for (uint32_t x = 0; x < columns; x++) {
            std::size_t begin = frames * x / columns;
            std::size_t end = std::max(frames * (x + 1) / columns, begin + 1);
            float low = 0;
            float high = 0;
            for (std::size_t f = begin; f < end && f < frames; f++) {
                float v = sound.samples[f * sound.channels + c];
                low = std::min(low, v);
                high = std::max(high, v);
            }
            out[(c * columns + x) * 2] = std::max(low, -1.0f);
            out[(c * columns + x) * 2 + 1] = std::min(high, 1.0f);
        }
    }
    return channels;
}

// Caller holds the mutex.
void AudioPlayer::StartDevice() {
    // miniaudio converts rate and channel count to the device's.
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = sound.channels;
    config.sampleRate = sound.sampleRate;
    config.pUserData = this;
    config.dataCallback = [](ma_device* dev, void* output, const void*, ma_uint32 frames) {
        static_cast<AudioPlayer*>(dev->pUserData)->Mix(static_cast<float*>(output), frames);
    };
    auto created = std::make_unique<Device>();
    if (ma_device_init(nullptr, &config, &created->device) != MA_SUCCESS) {
        throw std::runtime_error("audio: no output device");
    }
    playing = true;
    if (ma_device_start(&created->device) != MA_SUCCESS) {
        playing = false;
        ma_device_uninit(&created->device);
        throw std::runtime_error("audio: cannot start the output device");
    }
    device = std::move(created);
}

// Device thread.
void AudioPlayer::Mix(float* output, uint32_t frames) {
    std::size_t channels = sound.channels;
    if (paused.load() || !playing.load()) {
        std::fill(output, output + frames * channels, 0.0f);
        return;
    }
    uint64_t start = cursor.load();
    uint64_t total = ready.load();
    bool finished = complete.load();
    uint64_t available = start < total ? std::min<uint64_t>(frames, total - start) : 0;
    if (available > 0) {
        std::memcpy(output, sound.samples.data() + start * channels, available * channels * sizeof(float));
    }
    std::fill(output + available * channels, output + frames * channels, 0.0f);
    // A seek from the UI thread may land between the load and this store; the
    // compare keeps the newer position.
    uint64_t expected = start;
    if (cursor.compare_exchange_strong(expected, start + available) && finished && start + available >= total) {
        playing = false;
    }
}
