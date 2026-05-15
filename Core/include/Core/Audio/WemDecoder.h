#ifndef REASSETEXPLORER_WEMDECODER_H
#define REASSETEXPLORER_WEMDECODER_H
#include <cstdint>
#include <span>
#include <vector>

// ww2ogg's packed_codebooks_aoTuV_603.bin (Wwise 2011.2+): setup headers store only codebook ids.
class WwiseCodebooks {
public:
    explicit WwiseCodebooks(std::vector<uint8_t> library);
    std::span<const uint8_t> Get(uint32_t id) const;

private:
    std::vector<uint8_t> data;
    std::vector<uint32_t> offsets;
};

// Only the RE7/RE8 variant: vorb inside a 0x42 fmt, 2-byte packet headers, modified packets,
// external codebooks. Granules are exact, so decoders trim the last frame to sampleCount.
std::vector<uint8_t> WemToOgg(std::span<const uint8_t> wem, const WwiseCodebooks& codebooks,
                              uint32_t* sampleCount = nullptr);

struct DecodedAudio {
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    std::vector<float> samples;  // interleaved

    std::size_t FrameCount() const { return channels ? samples.size() / channels : 0; }
};

DecodedAudio DecodeWem(std::span<const uint8_t> wem, const WwiseCodebooks& codebooks);
// 16-bit PCM RIFF/WAVE.
std::vector<uint8_t> EncodeWav(const DecodedAudio& audio);

#endif
