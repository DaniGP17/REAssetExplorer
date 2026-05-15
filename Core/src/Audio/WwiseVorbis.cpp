// Wwise Vorbis to Ogg Vorbis. Ported from ww2ogg (https://github.com/hcs64/ww2ogg)
// and reduced to the variant RE7/RE8 ship; see Assets/Wwise/LICENSE-ww2ogg.txt.
//
// Copyright (c) 2002, Xiph.org Foundation
// Copyright (c) 2009-2016, Adam Gashlin
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// - Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of the Xiph.org Foundation nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
// OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "Core/Audio/WemDecoder.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

uint32_t Read32(std::span<const uint8_t> data, std::size_t pos) {
    if (pos + 4 > data.size()) throw std::runtime_error("wem: truncated");
    return data[pos] | data[pos + 1] << 8 | data[pos + 2] << 16 | static_cast<uint32_t>(data[pos + 3]) << 24;
}

uint16_t Read16(std::span<const uint8_t> data, std::size_t pos) {
    if (pos + 2 > data.size()) throw std::runtime_error("wem: truncated");
    return static_cast<uint16_t>(data[pos] | data[pos + 1] << 8);
}

// Vorbis bit order: least significant bit first.
class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> bytes) : data(bytes) {}

    uint32_t Read(unsigned bits) {
        uint32_t value = 0;
        for (unsigned i = 0; i < bits; i++, pos++) {
            if (pos >= data.size() * 8) throw std::runtime_error("wem: read past the end of a packet");
            if ((data[pos >> 3] >> (pos & 7)) & 1) value |= 1u << i;
        }
        return value;
    }

    std::size_t BitsRead() const { return pos; }

private:
    std::span<const uint8_t> data;
    std::size_t pos = 0;
};

class BitWriter {
public:
    void Write(uint32_t value, unsigned bits) {
        for (unsigned i = 0; i < bits; i++, count++) {
            if ((count & 7) == 0) bytes.push_back(0);
            if ((value >> i) & 1) bytes.back() |= static_cast<uint8_t>(1u << (count & 7));
        }
    }

    void WriteHeaderStart(uint8_t type) {
        Write(type, 8);
        for (char c : std::string_view("vorbis")) Write(static_cast<uint8_t>(c), 8);
    }

    std::vector<uint8_t> Take() {
        count = 0;
        return std::move(bytes);
    }

private:
    std::vector<uint8_t> bytes;
    std::size_t count = 0;
};

// From Tremor (lowmem).
unsigned Ilog(unsigned v) {
    unsigned ret = 0;
    while (v) {
        ret++;
        v >>= 1;
    }
    return ret;
}

unsigned MapType1QuantVals(unsigned entries, unsigned dimensions) {
    unsigned bits = Ilog(entries);
    unsigned vals = entries >> ((bits - 1) * (dimensions - 1) / dimensions);
    while (true) {
        unsigned long acc = 1;
        unsigned long acc1 = 1;
        for (unsigned i = 0; i < dimensions; i++) {
            acc *= vals;
            acc1 *= vals + 1;
        }
        if (acc <= entries && acc1 > entries) return vals;
        if (acc > entries) vals--;
        else vals++;
    }
}

// Packed codebook to the full Vorbis form: lengths and lookup type are stored
// with fewer bits, the sync pattern and the dimension/entry widths are implied.
void RebuildCodebook(std::span<const uint8_t> packed, BitWriter& out) {
    BitReader in(packed);
    uint32_t dimensions = in.Read(4);
    uint32_t entries = in.Read(14);
    out.Write(0x564342, 24);
    out.Write(dimensions, 16);
    out.Write(entries, 24);

    uint32_t ordered = in.Read(1);
    out.Write(ordered, 1);
    if (ordered) {
        out.Write(in.Read(5), 5);
        uint32_t current = 0;
        while (current < entries) {
            unsigned bits = Ilog(entries - current);
            uint32_t number = in.Read(bits);
            out.Write(number, bits);
            current += number;
        }
        if (current > entries) throw std::runtime_error("wem: codebook entry count overflow");
    } else {
        uint32_t lengthBits = in.Read(3);
        uint32_t sparse = in.Read(1);
        if (lengthBits == 0 || lengthBits > 5) throw std::runtime_error("wem: bad codeword length size");
        out.Write(sparse, 1);
        for (uint32_t i = 0; i < entries; i++) {
            bool present = true;
            if (sparse) {
                uint32_t flag = in.Read(1);
                out.Write(flag, 1);
                present = flag != 0;
            }
            if (present) out.Write(in.Read(lengthBits), 5);
        }
    }

    uint32_t lookupType = in.Read(1);
    out.Write(lookupType, 4);
    if (lookupType == 1) {
        uint32_t minimum = in.Read(32);
        uint32_t maximum = in.Read(32);
        uint32_t valueLength = in.Read(4);
        uint32_t sequence = in.Read(1);
        out.Write(minimum, 32);
        out.Write(maximum, 32);
        out.Write(valueLength, 4);
        out.Write(sequence, 1);
        unsigned quantVals = MapType1QuantVals(entries, dimensions);
        for (unsigned i = 0; i < quantVals; i++) out.Write(in.Read(valueLength + 1), valueLength + 1);
    }
    // The packed form ends with at most one byte of padding.
    if (in.BitsRead() / 8 + 1 != packed.size()) throw std::runtime_error("wem: codebook size mismatch");
}

struct SetupInfo {
    std::vector<bool> modeBlockFlags;
    unsigned modeBits = 0;
};

// Setup header without codebooks, time domain transforms, floor and residue
// types, mapping types and mode window/transform types; all are implied.
SetupInfo RebuildSetup(std::span<const uint8_t> packet, const WwiseCodebooks& codebooks, unsigned channels,
                       BitWriter& out) {
    BitReader in(packet);
    out.WriteHeaderStart(5);

    uint32_t codebookCountLess1 = in.Read(8);
    uint32_t codebookCount = codebookCountLess1 + 1;
    out.Write(codebookCountLess1, 8);
    for (uint32_t i = 0; i < codebookCount; i++) {
        uint32_t id = in.Read(10);
        std::span<const uint8_t> packed = codebooks.Get(id);
        if (packed.empty()) throw std::runtime_error("wem: unknown codebook id " + std::to_string(id));
        RebuildCodebook(packed, out);
    }

    out.Write(0, 6);   // time domain count - 1
    out.Write(0, 16);  // the single placeholder transform

    uint32_t floorCountLess1 = in.Read(6);
    uint32_t floorCount = floorCountLess1 + 1;
    out.Write(floorCountLess1, 6);
    for (uint32_t i = 0; i < floorCount; i++) {
        out.Write(1, 16);  // floor type 1
        uint32_t partitions = in.Read(5);
        out.Write(partitions, 5);
        std::vector<uint32_t> partitionClasses(partitions);
        uint32_t maximumClass = 0;
        for (uint32_t j = 0; j < partitions; j++) {
            partitionClasses[j] = in.Read(4);
            out.Write(partitionClasses[j], 4);
            maximumClass = std::max(maximumClass, partitionClasses[j]);
        }
        std::vector<uint32_t> classDimensions(maximumClass + 1);
        for (uint32_t j = 0; j <= maximumClass; j++) {
            uint32_t dimensionsLess1 = in.Read(3);
            out.Write(dimensionsLess1, 3);
            classDimensions[j] = dimensionsLess1 + 1;
            uint32_t subclasses = in.Read(2);
            out.Write(subclasses, 2);
            if (subclasses != 0) {
                uint32_t masterbook = in.Read(8);
                out.Write(masterbook, 8);
                if (masterbook >= codebookCount) throw std::runtime_error("wem: bad floor masterbook");
            }
            for (uint32_t k = 0; k < (1u << subclasses); k++) {
                uint32_t bookPlus1 = in.Read(8);
                out.Write(bookPlus1, 8);
                if (bookPlus1 > codebookCount) throw std::runtime_error("wem: bad floor subclass book");
            }
        }
        out.Write(in.Read(2), 2);  // multiplier - 1
        uint32_t rangeBits = in.Read(4);
        out.Write(rangeBits, 4);
        for (uint32_t j = 0; j < partitions; j++) {
            for (uint32_t k = 0; k < classDimensions[partitionClasses[j]]; k++) out.Write(in.Read(rangeBits), rangeBits);
        }
    }

    uint32_t residueCountLess1 = in.Read(6);
    uint32_t residueCount = residueCountLess1 + 1;
    out.Write(residueCountLess1, 6);
    for (uint32_t i = 0; i < residueCount; i++) {
        uint32_t type = in.Read(2);
        out.Write(type, 16);
        if (type > 2) throw std::runtime_error("wem: bad residue type");
        uint32_t begin = in.Read(24);
        uint32_t end = in.Read(24);
        uint32_t partitionSizeLess1 = in.Read(24);
        uint32_t classificationsLess1 = in.Read(6);
        uint32_t classbook = in.Read(8);
        out.Write(begin, 24);
        out.Write(end, 24);
        out.Write(partitionSizeLess1, 24);
        out.Write(classificationsLess1, 6);
        out.Write(classbook, 8);
        if (classbook >= codebookCount) throw std::runtime_error("wem: bad residue classbook");
        std::vector<uint32_t> cascade(classificationsLess1 + 1);
        for (uint32_t& bits : cascade) {
            uint32_t low = in.Read(3);
            out.Write(low, 3);
            uint32_t flag = in.Read(1);
            out.Write(flag, 1);
            uint32_t high = 0;
            if (flag) {
                high = in.Read(5);
                out.Write(high, 5);
            }
            bits = high * 8 + low;
        }
        for (uint32_t bits : cascade) {
            for (unsigned k = 0; k < 8; k++) {
                if ((bits & (1u << k)) == 0) continue;
                uint32_t book = in.Read(8);
                out.Write(book, 8);
                if (book >= codebookCount) throw std::runtime_error("wem: bad residue book");
            }
        }
    }

    uint32_t mappingCountLess1 = in.Read(6);
    uint32_t mappingCount = mappingCountLess1 + 1;
    out.Write(mappingCountLess1, 6);
    for (uint32_t i = 0; i < mappingCount; i++) {
        out.Write(0, 16);  // mapping type 0
        uint32_t submapsFlag = in.Read(1);
        out.Write(submapsFlag, 1);
        uint32_t submaps = 1;
        if (submapsFlag) {
            uint32_t submapsLess1 = in.Read(4);
            out.Write(submapsLess1, 4);
            submaps = submapsLess1 + 1;
        }
        uint32_t squarePolar = in.Read(1);
        out.Write(squarePolar, 1);
        if (squarePolar) {
            uint32_t stepsLess1 = in.Read(8);
            out.Write(stepsLess1, 8);
            unsigned bits = Ilog(channels - 1);
            for (uint32_t j = 0; j <= stepsLess1; j++) {
                uint32_t magnitude = in.Read(bits);
                uint32_t angle = in.Read(bits);
                out.Write(magnitude, bits);
                out.Write(angle, bits);
                if (magnitude == angle || magnitude >= channels || angle >= channels) {
                    throw std::runtime_error("wem: bad channel coupling");
                }
            }
        }
        uint32_t reserved = in.Read(2);
        out.Write(reserved, 2);
        if (reserved != 0) throw std::runtime_error("wem: mapping reserved field set");
        if (submaps > 1) {
            for (unsigned j = 0; j < channels; j++) out.Write(in.Read(4), 4);
        }
        for (uint32_t j = 0; j < submaps; j++) {
            out.Write(in.Read(8), 8);  // time config
            uint32_t floor = in.Read(8);
            uint32_t residue = in.Read(8);
            out.Write(floor, 8);
            out.Write(residue, 8);
            if (floor >= floorCount || residue >= residueCount) throw std::runtime_error("wem: bad submap");
        }
    }

    uint32_t modeCountLess1 = in.Read(6);
    uint32_t modeCount = modeCountLess1 + 1;
    out.Write(modeCountLess1, 6);
    SetupInfo setup;
    for (uint32_t i = 0; i < modeCount; i++) {
        uint32_t blockFlag = in.Read(1);
        out.Write(blockFlag, 1);
        setup.modeBlockFlags.push_back(blockFlag != 0);
        out.Write(0, 16);  // window type
        out.Write(0, 16);  // transform type
        uint32_t mapping = in.Read(8);
        out.Write(mapping, 8);
        if (mapping >= mappingCount) throw std::runtime_error("wem: bad mode mapping");
    }
    setup.modeBits = Ilog(modeCount - 1);
    out.Write(1, 1);  // framing

    if ((in.BitsRead() + 7) / 8 != packet.size()) throw std::runtime_error("wem: setup packet size mismatch");
    return setup;
}

uint32_t OggCrc(const uint8_t* data, std::size_t size) {
    static const auto TABLE = [] {
        std::vector<uint32_t> table(256);
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t r = i << 24;
            for (int k = 0; k < 8; k++) r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : r << 1;
            table[i] = r;
        }
        return table;
    }();
    uint32_t crc = 0;
    for (std::size_t i = 0; i < size; i++) crc = (crc << 8) ^ TABLE[((crc >> 24) & 0xFF) ^ data[i]];
    return crc;
}

class OggWriter {
public:
    // One packet per page, spilling into continuation pages past 255 segments.
    void Packet(const std::vector<uint8_t>& packet, uint64_t granule, bool last) {
        // Lacing: 255 per full segment, then the remainder (0 included).
        std::vector<uint8_t> lacing(packet.size() / 255, 255);
        lacing.push_back(static_cast<uint8_t>(packet.size() % 255));
        std::size_t segment = 0;
        std::size_t byte = 0;
        while (segment < lacing.size()) {
            std::size_t count = std::min<std::size_t>(lacing.size() - segment, 255);
            std::size_t bodySize = 0;
            for (std::size_t i = segment; i < segment + count; i++) bodySize += lacing[i];
            bool ends = segment + count == lacing.size();
            uint8_t flags = (segment > 0 ? 0x01 : 0) | (sequence == 0 ? 0x02 : 0) | (last && ends ? 0x04 : 0);
            // A page where no packet completes carries granule -1.
            WritePage(flags, ends ? granule : ~0ull,
                      std::span(lacing).subspan(segment, count), std::span(packet).subspan(byte, bodySize));
            segment += count;
            byte += bodySize;
        }
    }

    std::vector<uint8_t> Take() { return std::move(bytes); }

private:
    void WritePage(uint8_t flags, uint64_t granule, std::span<const uint8_t> lacing, std::span<const uint8_t> body) {
        std::size_t start = bytes.size();
        const uint8_t magic[4] = { 'O', 'g', 'g', 'S' };
        bytes.insert(bytes.end(), magic, magic + 4);
        bytes.push_back(0);
        bytes.push_back(flags);
        for (int i = 0; i < 8; i++) bytes.push_back(static_cast<uint8_t>(granule >> (i * 8)));
        for (int i = 0; i < 4; i++) bytes.push_back(static_cast<uint8_t>(SERIAL >> (i * 8)));
        for (int i = 0; i < 4; i++) bytes.push_back(static_cast<uint8_t>(sequence >> (i * 8)));
        bytes.insert(bytes.end(), 4, 0);  // crc, filled below
        bytes.push_back(static_cast<uint8_t>(lacing.size()));
        bytes.insert(bytes.end(), lacing.begin(), lacing.end());
        bytes.insert(bytes.end(), body.begin(), body.end());
        uint32_t crc = OggCrc(bytes.data() + start, bytes.size() - start);
        for (int i = 0; i < 4; i++) bytes[start + 22 + i] = static_cast<uint8_t>(crc >> (i * 8));
        sequence++;
    }

    static constexpr uint32_t SERIAL = 1;
    std::vector<uint8_t> bytes;
    uint32_t sequence = 0;
};

}

WwiseCodebooks::WwiseCodebooks(std::vector<uint8_t> library) : data(std::move(library)) {
    // Codebook blobs, then one u32 offset per codebook; the last u32 of the
    // file is where that offset table starts.
    if (data.size() < 4) throw std::runtime_error("codebooks: empty library");
    uint32_t tableOffset = Read32(data, data.size() - 4);
    if (tableOffset > data.size()) throw std::runtime_error("codebooks: bad offset table");
    for (std::size_t pos = tableOffset; pos + 4 <= data.size(); pos += 4) offsets.push_back(Read32(data, pos));
}

std::span<const uint8_t> WwiseCodebooks::Get(uint32_t id) const {
    if (id + 1 >= offsets.size()) return {};
    uint32_t begin = offsets[id];
    uint32_t end = offsets[id + 1];
    if (begin >= end || end > data.size()) return {};
    return std::span(data).subspan(begin, end - begin);
}

std::vector<uint8_t> WemToOgg(std::span<const uint8_t> wem, const WwiseCodebooks& codebooks, uint32_t* sampleCountOut) {
    if (wem.size() < 12 || std::memcmp(wem.data(), "RIFF", 4) != 0) throw std::runtime_error("wem: not a little-endian RIFF");

    std::size_t fmt = 0;
    uint32_t fmtSize = 0;
    std::size_t vorb = 0;
    std::size_t dataStart = 0;
    uint32_t dataSize = 0;
    for (std::size_t pos = 12; pos + 8 <= wem.size();) {
        uint32_t size = Read32(wem, pos + 4);
        if (std::memcmp(wem.data() + pos, "fmt ", 4) == 0) {
            fmt = pos + 8;
            fmtSize = size;
        } else if (std::memcmp(wem.data() + pos, "vorb", 4) == 0 && size == 0x2A) {
            vorb = pos + 8;
        } else if (std::memcmp(wem.data() + pos, "data", 4) == 0) {
            dataStart = pos + 8;
            dataSize = size;
        }
        pos += 8 + size + (size & 1);
    }
    if (fmt == 0 || dataStart == 0) throw std::runtime_error("wem: missing fmt or data");
    if (Read16(wem, fmt) != 0xFFFF) throw std::runtime_error("wem: not Wwise Vorbis");
    if (vorb == 0 && fmtSize == 0x42) vorb = fmt + 0x18;
    if (vorb == 0) throw std::runtime_error("wem: unsupported Wwise Vorbis layout");
    if (dataStart + dataSize > wem.size()) throw std::runtime_error("wem: truncated data");

    uint16_t channels = Read16(wem, fmt + 2);
    uint32_t sampleRate = Read32(wem, fmt + 4);
    uint32_t averageBytes = Read32(wem, fmt + 8);
    uint32_t sampleCount = Read32(wem, vorb);
    if (sampleCountOut != nullptr) *sampleCountOut = sampleCount;
    uint32_t modSignal = Read32(wem, vorb + 4);
    uint32_t setupOffset = Read32(wem, vorb + 0x10);
    uint32_t audioOffset = Read32(wem, vorb + 0x14);
    uint8_t blockSize0 = wem[vorb + 0x28];
    uint8_t blockSize1 = wem[vorb + 0x29];
    // Signals seen on files whose packets keep the standard first byte.
    bool modPackets = modSignal != 0x4A && modSignal != 0x4B && modSignal != 0x69 && modSignal != 0x70;
    std::span<const uint8_t> audio = wem.subspan(dataStart, dataSize);

    OggWriter ogg;
    BitWriter w;

    w.WriteHeaderStart(1);
    w.Write(0, 32);
    w.Write(channels, 8);
    w.Write(sampleRate, 32);
    w.Write(0, 32);
    w.Write(averageBytes * 8, 32);
    w.Write(0, 32);
    w.Write(blockSize0, 4);
    w.Write(blockSize1, 4);
    w.Write(1, 1);
    ogg.Packet(w.Take(), 0, false);

    static constexpr std::string_view VENDOR = "REAssetExplorer (from Wwise Vorbis, after ww2ogg)";
    w.WriteHeaderStart(3);
    w.Write(static_cast<uint32_t>(VENDOR.size()), 32);
    for (char c : VENDOR) w.Write(static_cast<uint8_t>(c), 8);
    w.Write(0, 32);
    w.Write(1, 1);
    ogg.Packet(w.Take(), 0, false);

    uint16_t setupSize = Read16(audio, setupOffset);
    if (setupOffset + 2 + setupSize > audio.size()) throw std::runtime_error("wem: truncated setup packet");
    SetupInfo setup = RebuildSetup(audio.subspan(setupOffset + 2, setupSize), codebooks, channels, w);
    ogg.Packet(w.Take(), 0, false);
    if (setupOffset + 2 + setupSize != audioOffset) throw std::runtime_error("wem: audio does not follow the setup");

    auto modeFlag = [&setup](uint32_t mode) {
        if (mode >= setup.modeBlockFlags.size()) throw std::runtime_error("wem: bad mode number");
        return setup.modeBlockFlags[mode];
    };

    bool previousLong = false;
    uint32_t previousBlock = 0;
    uint64_t granule = 0;
    std::size_t offset = audioOffset;
    while (offset + 2 <= audio.size()) {
        uint16_t size = Read16(audio, offset);
        std::size_t next = offset + 2 + size;
        if (next > audio.size()) throw std::runtime_error("wem: truncated audio packet");
        if (size == 0) {
            offset = next;
            continue;
        }
        std::span<const uint8_t> packet = audio.subspan(offset + 2, size);
        bool isLong = false;
        {
            BitReader in(packet);
            if (modPackets) {
                // Wwise drops the packet type bit and, for long blocks, the
                // previous/next window flags; rebuild them.
                w.Write(0, 1);
                uint32_t mode = in.Read(setup.modeBits);
                w.Write(mode, setup.modeBits);
                uint32_t remainder = in.Read(8 - setup.modeBits);
                isLong = modeFlag(mode);
                if (isLong) {
                    bool nextLong = false;
                    if (next + 2 <= audio.size()) {
                        uint16_t nextSize = Read16(audio, next);
                        if (nextSize > 0 && next + 2 + nextSize <= audio.size()) {
                            BitReader peek(audio.subspan(next + 2, nextSize));
                            nextLong = modeFlag(peek.Read(setup.modeBits));
                        }
                    }
                    w.Write(previousLong, 1);
                    w.Write(nextLong, 1);
                }
                w.Write(remainder, 8 - setup.modeBits);
            } else {
                in.Read(1);
                isLong = modeFlag(in.Read(setup.modeBits));
                w.Write(packet[0], 8);
            }
            for (std::size_t i = 1; i < packet.size(); i++) w.Write(packet[i], 8);
        }

        // Each packet after the first completes (previous + current) / 4 samples.
        uint32_t block = 1u << (isLong ? blockSize1 : blockSize0);
        if (previousBlock != 0) granule += previousBlock / 4 + block / 4;
        previousBlock = block;
        previousLong = isLong;

        bool last = next + 2 > audio.size();
        uint64_t pageGranule = last && sampleCount != 0 && sampleCount < granule ? sampleCount : granule;
        ogg.Packet(w.Take(), pageGranule, last);
        offset = next;
    }
    return ogg.Take();
}
