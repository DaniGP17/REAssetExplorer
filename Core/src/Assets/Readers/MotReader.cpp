#include "Core/Assets/Readers/MotReader.h"

#include <cmath>
#include <string>
#include <unordered_map>

#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t MOT_MAGIC = 0x20746F6D;
constexpr uint32_t MOT_VERSION_RE3 = 78;
constexpr uint32_t MOT_VERSION_MHR_DEMO = 456;
constexpr uint32_t MOT_VERSION_RE8 = 458;
constexpr uint32_t MOT_VERSION_MHWILDS = 932;
constexpr std::size_t BONE_STRIDE = 80;
constexpr std::size_t BONE_CLIP_STRIDE = 12;
constexpr std::size_t TRACK_HEADER_SIZE = 20;
constexpr uint32_t MAX_KEYS = 1000000;
constexpr uint64_t MAX_BONES = 8192;

// Packed keys of 3, 5, 6 and 7 bytes are assembled big-endian (first byte most
// significant), so the first component sits in the low bits of the value.
class KeyCursor {
public:
    KeyCursor(const MemoryReader& reader, std::size_t offset) : r(reader), pos(offset) {}

    template <typename T>
    T Next() {
        T value = r.ReadAt<T>(pos);
        pos += sizeof(T);
        return value;
    }

    uint64_t NextBigEndian(int bytes) {
        uint64_t value = 0;
        for (int i = 0; i < bytes; i++) value = (value << 8) | r.ReadAt<uint8_t>(pos + i);
        pos += bytes;
        return value;
    }

private:
    const MemoryReader& r;
    std::size_t pos;
};

float Unit(uint64_t value, int bits) {
    uint64_t mask = (1ull << bits) - 1;
    return static_cast<float>(value & mask) / static_cast<float>(mask);
}

float& Axis(MotVec3& v, int axis) {
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

// Compression ids follow REE-Lib's RE3+ table.
MotVec3 DecodeVector3(uint32_t compression, KeyCursor& c, const float u[8]) {
    auto triple = [u](float qx, float qy, float qz) {
        return MotVec3{ u[0] * qx + u[3], u[1] * qy + u[4], u[2] * qz + u[5] };
    };
    auto tripleBits = [&](uint64_t v, int bits) {
        return triple(Unit(v, bits), Unit(v >> bits, bits), Unit(v >> (2 * bits), bits));
    };
    auto single = [u](int axis, float q) {
        MotVec3 out{ u[1], u[2], u[3] };
        Axis(out, axis) = u[0] * q + u[1 + axis];
        return out;
    };
    auto pair = [u](int axis0, int axis1, float q0, float q1) {
        MotVec3 out{ u[2], u[3], u[4] };
        Axis(out, axis0) = u[0] * q0 + u[2 + axis0];
        Axis(out, axis1) = u[1] * q1 + u[2 + axis1];
        return out;
    };
    auto pairBits = [&](int axis0, int axis1, uint64_t v, int bits) {
        return pair(axis0, axis1, Unit(v, bits), Unit(v >> bits, bits));
    };
    auto pairOf = [&](int axis0, int axis1, int bits) {
        float q0 = bits == 8 ? Unit(c.Next<uint8_t>(), 8) : Unit(c.Next<uint16_t>(), 16);
        float q1 = bits == 8 ? Unit(c.Next<uint8_t>(), 8) : Unit(c.Next<uint16_t>(), 16);
        return pair(axis0, axis1, q0, q1);
    };
    auto rawAxis = [&](int axis) {
        MotVec3 out{ u[0], u[1], u[2] };
        Axis(out, axis) = c.Next<float>();
        return out;
    };

    switch (compression) {
        case 0x00000: {
            MotVec3 v;
            v.x = c.Next<float>();
            v.y = c.Next<float>();
            v.z = c.Next<float>();
            return v;
        }
        case 0x20000: return tripleBits(c.Next<uint16_t>(), 5);
        case 0x21000: return single(0, Unit(c.Next<uint16_t>(), 16));
        case 0x22000: return single(1, Unit(c.Next<uint16_t>(), 16));
        case 0x23000: return single(2, Unit(c.Next<uint16_t>(), 16));
        case 0x24000: {
            float v = u[0] * Unit(c.Next<uint16_t>(), 16) + u[1];
            return MotVec3{ v, v, v };
        }
        case 0x25000: return pairOf(0, 1, 8);
        case 0x26000: return pairOf(0, 2, 8);
        case 0x27000: return pairOf(1, 2, 8);
        case 0x30000: {
            float qx = Unit(c.Next<uint8_t>(), 8);
            float qy = Unit(c.Next<uint8_t>(), 8);
            float qz = Unit(c.Next<uint8_t>(), 8);
            return triple(qx, qy, qz);
        }
        case 0x31000: return single(0, Unit(c.NextBigEndian(3), 24));
        case 0x32000: return single(1, Unit(c.NextBigEndian(3), 24));
        case 0x33000: return single(2, Unit(c.NextBigEndian(3), 24));
        case 0x35000: return pairBits(1, 2, c.NextBigEndian(3), 12);
        case 0x36000: return pairBits(0, 2, c.NextBigEndian(3), 12);
        case 0x37000: return pairBits(0, 1, c.NextBigEndian(3), 12);
        case 0x40000: return tripleBits(c.Next<uint32_t>(), 10);
        case 0x41000: return rawAxis(0);
        case 0x42000: return rawAxis(1);
        case 0x43000: return rawAxis(2);
        case 0x44000: {
            float v = c.Next<float>();
            return MotVec3{ v, v, v };
        }
        case 0x45000: return pairOf(0, 1, 16);
        case 0x46000: return pairOf(0, 2, 16);
        case 0x47000: return pairOf(1, 2, 16);
        case 0x50000: return tripleBits(c.NextBigEndian(5), 13);
        case 0x55000: return pairBits(0, 1, c.NextBigEndian(5), 20);
        case 0x56000: return pairBits(1, 2, c.NextBigEndian(5), 20);
        case 0x57000: return pairBits(2, 0, c.NextBigEndian(5), 20);
        case 0x60000: return tripleBits(c.NextBigEndian(6), 16);
        case 0x65000: return pairBits(0, 1, c.NextBigEndian(6), 24);
        case 0x66000: return pairBits(1, 2, c.NextBigEndian(6), 24);
        case 0x67000: return pairBits(2, 0, c.NextBigEndian(6), 24);
        case 0x70000: return tripleBits(c.NextBigEndian(7), 18);
        case 0x75000: return pairBits(0, 1, c.NextBigEndian(7), 28);
        case 0x76000: return pairBits(1, 2, c.NextBigEndian(7), 28);
        case 0x77000: return pairBits(2, 0, c.NextBigEndian(7), 28);
        case 0x80000: return tripleBits(c.Next<uint64_t>(), 21);
        case 0x85000: {
            MotVec3 v{ 0, 0, u[2] };
            v.x = c.Next<float>();
            v.y = c.Next<float>();
            return v;
        }
        case 0x86000: {
            MotVec3 v{ u[0], 0, 0 };
            v.y = c.Next<float>();
            v.z = c.Next<float>();
            return v;
        }
        case 0x87000: {
            MotVec3 v{ 0, u[1], 0 };
            v.z = c.Next<float>();
            v.x = c.Next<float>();
            return v;
        }
        default: break;
    }
    throw std::runtime_error("mot: unknown vector3 compression " + std::to_string(compression >> 12));
}

MotQuat DecodeQuaternion(uint32_t compression, uint32_t version, KeyCursor& c, const float u[8]) {
    float q[3] = { 0, 0, 0 };
    auto triple = [&](float a, float b, float d) {
        q[0] = u[0] * a + u[4];
        q[1] = u[1] * b + u[5];
        q[2] = u[2] * d + u[6];
    };
    auto tripleBits = [&](uint64_t v, int bits) {
        triple(Unit(v, bits), Unit(v >> bits, bits), Unit(v >> (2 * bits), bits));
    };
    auto single = [&](int axis, float v) { q[axis] = u[0] * v + u[1]; };

    switch (compression) {
        case 0x00000: {
            MotQuat full;
            full.x = c.Next<float>();
            full.y = c.Next<float>();
            full.z = c.Next<float>();
            full.w = c.Next<float>();
            return full;
        }
        case 0x20000: tripleBits(c.Next<uint16_t>(), 5); break;
        case 0x21000: single(0, Unit(c.Next<uint16_t>(), 16)); break;
        case 0x22000: single(1, Unit(c.Next<uint16_t>(), 16)); break;
        case 0x23000: single(2, Unit(c.Next<uint16_t>(), 16)); break;
        case 0x30000: {
            float a = Unit(c.Next<uint8_t>(), 8);
            float b = Unit(c.Next<uint8_t>(), 8);
            float d = Unit(c.Next<uint8_t>(), 8);
            triple(a, b, d);
            break;
        }
        case 0x31000: single(0, Unit(c.NextBigEndian(3), 24)); break;
        case 0x32000: single(1, Unit(c.NextBigEndian(3), 24)); break;
        case 0x33000: single(2, Unit(c.NextBigEndian(3), 24)); break;
        case 0x40000: tripleBits(c.Next<uint32_t>(), 10); break;
        case 0x41000: q[0] = c.Next<float>(); break;
        case 0x42000: q[1] = c.Next<float>(); break;
        case 0x43000: q[2] = c.Next<float>(); break;
        case 0x50000: tripleBits(c.NextBigEndian(5), 13); break;
        case 0x60000:
            // Three little-endian u16 until MH Wilds packed them like the other widths.
            if (version >= MOT_VERSION_MHWILDS) {
                tripleBits(c.NextBigEndian(6), 16);
            } else {
                float a = Unit(c.Next<uint16_t>(), 16);
                float b = Unit(c.Next<uint16_t>(), 16);
                float d = Unit(c.Next<uint16_t>(), 16);
                triple(a, b, d);
            }
            break;
        case 0x70000: tripleBits(c.NextBigEndian(7), 18); break;
        case 0x80000: tripleBits(c.Next<uint64_t>(), 21); break;
        case 0xC0000:
            q[0] = c.Next<float>();
            q[1] = c.Next<float>();
            q[2] = c.Next<float>();
            break;
        default:
            throw std::runtime_error("mot: unknown quaternion compression " + std::to_string(compression >> 12));
    }
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
    return MotQuat{ q[0], q[1], q[2], n < 1.0f ? std::sqrt(1.0f - n) : 0.0f };
}

MotTrack ReadTrack(const MemoryReader& r, std::size_t headerPos, bool rotation, uint32_t version) {
    MotTrack track;
    track.flags = r.ReadAt<uint32_t>(headerPos);
    uint32_t keyCount = r.ReadAt<uint32_t>(headerPos + 4);
    uint32_t frameIndexOffset = r.ReadAt<uint32_t>(headerPos + 8);
    uint32_t frameDataOffset = r.ReadAt<uint32_t>(headerPos + 12);
    uint32_t unpackOffset = r.ReadAt<uint32_t>(headerPos + 16);
    if (keyCount > MAX_KEYS) throw std::runtime_error("mot: key count " + std::to_string(keyCount));

    if (frameIndexOffset != 0) {
        uint32_t width = track.flags >> 20;
        track.frames.reserve(keyCount);
        for (uint32_t k = 0; k < keyCount; k++) {
            switch (width) {
                case 2: track.frames.push_back(r.ReadAt<uint8_t>(frameIndexOffset + k)); break;
                case 4: track.frames.push_back(r.ReadAt<uint16_t>(frameIndexOffset + k * 2)); break;
                case 5: track.frames.push_back(r.ReadAt<uint32_t>(frameIndexOffset + k * 4)); break;
                default: throw std::runtime_error("mot: unknown frame index width " + std::to_string(width));
            }
        }
    }

    // Always 8 floats in the engine's layout, but formats that need fewer may
    // sit at the end of the file.
    float unpack[8]{};
    if (unpackOffset != 0) {
        for (std::size_t i = 0; i < 8 && unpackOffset + (i + 1) * 4 <= r.Size(); i++) {
            unpack[i] = r.ReadAt<float>(unpackOffset + i * 4);
        }
    }

    KeyCursor cursor(r, frameDataOffset);
    if (rotation) {
        track.rotations.reserve(keyCount);
        for (uint32_t k = 0; k < keyCount; k++) {
            track.rotations.push_back(DecodeQuaternion(track.Compression(), version, cursor, unpack));
        }
    } else {
        track.vectors.reserve(keyCount);
        for (uint32_t k = 0; k < keyCount; k++) {
            track.vectors.push_back(DecodeVector3(track.Compression(), cursor, unpack));
        }
    }
    return track;
}

std::vector<MotBone> ReadBones(const MemoryReader& r, uint64_t listOffset) {
    uint64_t tableOffset = r.ReadAt<uint64_t>(listOffset);
    uint64_t count = r.ReadAt<uint64_t>(listOffset + 8);
    if (count > MAX_BONES) throw std::runtime_error("mot: bone count " + std::to_string(count));

    std::vector<MotBone> bones;
    bones.reserve(count);
    for (uint64_t i = 0; i < count; i++) {
        std::size_t pos = tableOffset + i * BONE_STRIDE;
        MotBone bone;
        uint64_t nameOffset = r.ReadAt<uint64_t>(pos);
        if (nameOffset != 0) bone.name = r.ReadWStringAt(nameOffset);
        uint64_t parentOffset = r.ReadAt<uint64_t>(pos + 8);
        if (parentOffset >= tableOffset && parentOffset < tableOffset + count * BONE_STRIDE) {
            bone.parent = static_cast<int32_t>((parentOffset - tableOffset) / BONE_STRIDE);
        }
        bone.translation = { r.ReadAt<float>(pos + 32), r.ReadAt<float>(pos + 36), r.ReadAt<float>(pos + 40) };
        bone.rotation = { r.ReadAt<float>(pos + 48), r.ReadAt<float>(pos + 52),
                          r.ReadAt<float>(pos + 56), r.ReadAt<float>(pos + 60) };
        bone.index = r.ReadAt<int32_t>(pos + 64);
        bone.hash = r.ReadAt<uint32_t>(pos + 68);
        bones.push_back(std::move(bone));
    }
    return bones;
}

// The offsets block grows one u64 at 456 (end clip frame values), the counts block one u16 at 458.
MotData ParseMot(std::span<const uint8_t> data, bool embedded, std::shared_ptr<const std::vector<MotBone>> sharedBones) {
    MemoryReader r(data);
    if (r.ReadAt<uint32_t>(4) != MOT_MAGIC) throw std::runtime_error("mot: bad magic");

    MotData mot;
    mot.version = r.ReadAt<uint32_t>(0);
    if (mot.version < MOT_VERSION_RE3) {
        throw std::runtime_error("mot: version " + std::to_string(mot.version) + " predates the supported layout");
    }
    uint32_t motSize = r.ReadAt<uint32_t>(12);
    uint64_t boneListOffset = r.ReadAt<uint64_t>(16);
    uint64_t boneClipOffset = r.ReadAt<uint64_t>(24);
    uint64_t dataOffset = r.ReadAt<uint64_t>(56);

    r.Seek(72);
    if (mot.version >= MOT_VERSION_MHR_DEMO) r.Read<uint64_t>();
    r.Read<uint64_t>();
    uint64_t nameOffset = r.Read<uint64_t>();
    mot.frameCount = r.Read<float>();
    mot.blending = r.Read<float>();
    mot.startFrame = r.Read<float>();
    mot.endFrame = r.Read<float>();
    r.Read<uint16_t>();
    uint16_t boneClipCount = r.Read<uint16_t>();
    r.Read<uint16_t>();
    if (mot.version >= MOT_VERSION_RE8) r.Read<uint16_t>();
    mot.frameRate = r.Read<uint16_t>();
    r.Read<uint16_t>();
    uint16_t attributes = r.Read<uint16_t>();

    if (nameOffset != 0) mot.name = r.ReadWStringAt(nameOffset);
    // Attribute bit 0 turns the data block into GPU vertex animation instead of a joint map path.
    if (dataOffset != 0 && (attributes & 1) == 0) mot.jointMapPath = r.ReadWStringAt(dataOffset);

    // In a motlist only the first mot writes its bone list (after its data,
    // with motSize 0); later mots point at or past their own end.
    bool ownBones = boneListOffset != 0 &&
                    (!embedded || motSize == 0 || boneListOffset < motSize);
    if (ownBones) {
        mot.bones = std::make_shared<const std::vector<MotBone>>(ReadBones(r, boneListOffset));
    } else {
        mot.bones = std::move(sharedBones);
    }

    std::unordered_map<uint32_t, const std::string*> namesByHash;
    if (mot.bones) {
        for (const MotBone& bone : *mot.bones) namesByHash.emplace(bone.hash, &bone.name);
    }

    mot.boneClips.reserve(boneClipCount);
    for (uint16_t i = 0; i < boneClipCount; i++) {
        std::size_t pos = boneClipOffset + i * BONE_CLIP_STRIDE;
        MotBoneClip clip;
        clip.boneIndex = r.ReadAt<uint16_t>(pos);
        uint8_t trackFlags = r.ReadAt<uint8_t>(pos + 2);
        clip.boneHash = r.ReadAt<uint32_t>(pos + 4);
        std::size_t trackPos = r.ReadAt<uint32_t>(pos + 8);

        auto name = namesByHash.find(clip.boneHash);
        if (name != namesByHash.end()) clip.boneName = *name->second;

        if (trackFlags & 1) {
            clip.translation = ReadTrack(r, trackPos, false, mot.version);
            trackPos += TRACK_HEADER_SIZE;
        }
        if (trackFlags & 2) {
            clip.rotation = ReadTrack(r, trackPos, true, mot.version);
            trackPos += TRACK_HEADER_SIZE;
        }
        if (trackFlags & 4) {
            clip.scale = ReadTrack(r, trackPos, false, mot.version);
        }
        mot.boneClips.push_back(std::move(clip));
    }
    return mot;
}

}

MotData MotReader::Read(std::span<const uint8_t> data) const {
    return ParseMot(data, false, nullptr);
}

MotData MotReader::ReadEmbedded(std::span<const uint8_t> data, std::shared_ptr<const std::vector<MotBone>> sharedBones) {
    return ParseMot(data, true, std::move(sharedBones));
}
