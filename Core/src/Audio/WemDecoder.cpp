#include "Core/Audio/WemDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

DecodedAudio DecodeWem(std::span<const uint8_t> wem, const WwiseCodebooks& codebooks) {
    uint32_t expected = 0;
    std::vector<uint8_t> ogg = WemToOgg(wem, codebooks, &expected);
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(ogg.data(), static_cast<int>(ogg.size()), &error, nullptr);
    if (vorbis == nullptr) throw std::runtime_error("vorbis: cannot open the rebuilt stream (" + std::to_string(error) + ")");

    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    DecodedAudio audio;
    audio.sampleRate = info.sample_rate;
    audio.channels = static_cast<uint32_t>(info.channels);
    std::vector<float> chunk(4096 * audio.channels);
    while (true) {
        int frames = stb_vorbis_get_samples_float_interleaved(vorbis, info.channels, chunk.data(),
                                                              static_cast<int>(chunk.size()));
        if (frames <= 0) break;
        audio.samples.insert(audio.samples.end(), chunk.begin(), chunk.begin() + frames * info.channels);
    }
    stb_vorbis_close(vorbis);

    if (expected != 0 && audio.FrameCount() > expected) audio.samples.resize(static_cast<std::size_t>(expected) * audio.channels);
    return audio;
}

std::vector<uint8_t> EncodeWav(const DecodedAudio& audio) {
    uint32_t dataBytes = static_cast<uint32_t>(audio.samples.size() * 2);
    std::vector<uint8_t> wav(44 + dataBytes);
    auto put16 = [&wav](std::size_t at, uint16_t v) { std::memcpy(wav.data() + at, &v, 2); };
    auto put32 = [&wav](std::size_t at, uint32_t v) { std::memcpy(wav.data() + at, &v, 4); };
    std::memcpy(wav.data(), "RIFF", 4);
    put32(4, 36 + dataBytes);
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, static_cast<uint16_t>(audio.channels));
    put32(24, audio.sampleRate);
    put32(28, audio.sampleRate * audio.channels * 2);
    put16(32, static_cast<uint16_t>(audio.channels * 2));
    put16(34, 16);
    std::memcpy(wav.data() + 36, "data", 4);
    put32(40, dataBytes);
    for (std::size_t i = 0; i < audio.samples.size(); i++) {
        float clamped = std::clamp(audio.samples[i], -1.0f, 1.0f);
        put16(44 + i * 2, static_cast<uint16_t>(static_cast<int16_t>(std::lround(clamped * 32767.0f))));
    }
    return wav;
}
