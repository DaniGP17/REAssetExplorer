#include "Core/Assets/Readers/SdfReader.h"

#include <algorithm>

#include "Core/IO/MemoryReader.h"

namespace {
    constexpr std::size_t PROGRAM_ENTRY_SIZE = 0x108;
    constexpr std::size_t PROGRAM_TABLE_OFFSET = 0x10;
    constexpr std::size_t DXIL_VARIANT = 1;

    std::vector<uint8_t> CopyBlob(const MemoryReader& r, uint64_t offset, uint32_t size) {
        if (offset == 0 || size == 0) return {};
        auto bytes = r.BytesAt(offset, size);
        return { bytes.begin(), bytes.end() };
    }

    template <std::size_t N>
    std::array<uint8_t, N> CopyState(const MemoryReader& r, uint64_t offset) {
        std::array<uint8_t, N> state{};
        if (offset == 0) return state;
        auto bytes = r.BytesAt(offset, N);
        std::copy(bytes.begin(), bytes.end(), state.begin());
        return state;
    }
}

SdfData SdfReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);

    if (r.ReadAt<uint32_t>(0x00) != 0x00464453) {
        throw std::runtime_error("sdf: bad magic");
    }

    SdfData sdf;
    sdf.variantCount = r.ReadAt<uint16_t>(0x04);
    sdf.programCount = r.ReadAt<uint16_t>(0x06);

    if (DXIL_VARIANT >= sdf.variantCount) {
        throw std::runtime_error("sdf: variant out of range");
    }

    std::size_t base = PROGRAM_TABLE_OFFSET + DXIL_VARIANT * sdf.programCount * PROGRAM_ENTRY_SIZE;
    for (uint16_t i = 0; i < sdf.programCount; i++) {
        sdf.programs.push_back(ReadProgram(r, base + i * PROGRAM_ENTRY_SIZE));
    }

    return sdf;
}

SdfProgram SdfReader::ReadProgram(const MemoryReader& r, std::size_t at) {
    SdfProgram p;
    p.name = r.ReadCStringAt(r.ReadAt<uint64_t>(at));

    uint64_t shaderPtrs[6];
    for (int i = 0; i < 6; i++) {
        shaderPtrs[i] = r.ReadAt<uint64_t>(at + 16 + i * 8);
    }

    uint64_t inputLayoutPtr = r.ReadAt<uint64_t>(at + 64);
    uint64_t rasterizerPtr = r.ReadAt<uint64_t>(at + 72);
    uint64_t blendPtr = r.ReadAt<uint64_t>(at + 80);
    uint64_t depthStencilPtr = r.ReadAt<uint64_t>(at + 88);

    uint64_t constantSlots = r.ReadAt<uint64_t>(at + 104);
    uint64_t constantInfos = r.ReadAt<uint64_t>(at + 112);
    uint64_t samplerSlots = r.ReadAt<uint64_t>(at + 120);
    uint64_t samplerInfos = r.ReadAt<uint64_t>(at + 128);
    uint64_t srvSlots = r.ReadAt<uint64_t>(at + 136);
    uint64_t srvInfos = r.ReadAt<uint64_t>(at + 144);
    uint64_t uavSlots = r.ReadAt<uint64_t>(at + 152);
    uint64_t uavInfos = r.ReadAt<uint64_t>(at + 160);

    uint32_t shaderSizes[6];
    for (int i = 0; i < 6; i++) {
        shaderSizes[i] = r.ReadAt<uint32_t>(at + 188 + i * 4);
    }

    uint8_t constantCount = r.ReadAt<uint8_t>(at + 246);
    uint8_t samplerCount = r.ReadAt<uint8_t>(at + 247);
    uint8_t srvCount = r.ReadAt<uint8_t>(at + 252);
    uint8_t uavCount = r.ReadAt<uint8_t>(at + 253);

    p.vs = CopyBlob(r, shaderPtrs[0], shaderSizes[0]);
    p.ps = CopyBlob(r, shaderPtrs[4], shaderSizes[4]);
    p.cs = CopyBlob(r, shaderPtrs[5], shaderSizes[5]);

    for (uint8_t i = 0; i < constantCount; i++) {
        std::size_t info = constantInfos + i * 0x20;
        SdfConstantBuffer cb;
        cb.name = r.ReadCStringAt(r.ReadAt<uint64_t>(info));
        cb.nameHash = r.ReadAt<uint32_t>(info + 8);
        uint32_t idAndFlag = r.ReadAt<uint32_t>(info + 12);
        cb.shaderId = idAndFlag & 0x7FFFFFFF;
        cb.sceneDependent = (idAndFlag & 0x80000000) != 0;
        cb.size = r.ReadAt<uint32_t>(info + 16);
        uint32_t variableCount = r.ReadAt<uint32_t>(info + 20);
        uint64_t variablesPtr = r.ReadAt<uint64_t>(info + 24);
        cb.slotRaw = r.ReadAt<uint64_t>(constantSlots + i * 8);

        for (uint32_t j = 0; j < variableCount; j++) {
            std::size_t varOffset = variablesPtr + j * 0x10;
            SdfConstantVariable variable;
            variable.name = r.ReadCStringAt(r.ReadAt<uint64_t>(varOffset));
            variable.nameHash = r.ReadAt<uint32_t>(varOffset + 8);
            variable.data = r.ReadAt<uint32_t>(varOffset + 12);
            cb.variables.push_back(std::move(variable));
        }

        p.constantBuffers.push_back(std::move(cb));
    }

    auto readResources = [&](uint64_t infos, uint64_t slots, uint8_t count, std::vector<SdfResource>& out) {
        for (uint8_t i = 0; i < count; i++) {
            std::size_t info = infos + i * 0x10;
            SdfResource res;
            res.name = r.ReadCStringAt(r.ReadAt<uint64_t>(info));
            res.nameHash = r.ReadAt<uint32_t>(info + 8);
            uint32_t idAndFlag = r.ReadAt<uint32_t>(info + 12);
            res.shaderId = idAndFlag & 0x7FFFFFFF;
            res.sceneDependent = (idAndFlag & 0x80000000) != 0;
            res.slotRaw = r.ReadAt<uint64_t>(slots + i * 8);
            out.push_back(std::move(res));
        }
    };

    readResources(srvInfos, srvSlots, srvCount, p.srvs);
    readResources(samplerInfos, samplerSlots, samplerCount, p.samplers);
    readResources(uavInfos, uavSlots, uavCount, p.uavs);

    if (inputLayoutPtr != 0) {
        uint32_t elementCount = r.ReadAt<uint32_t>(inputLayoutPtr);
        for (uint32_t i = 0; i < elementCount; i++) {
            uint64_t raw = r.ReadAt<uint64_t>(inputLayoutPtr + 8 + i * 8);
            SdfInputElement element;
            element.semantic = static_cast<uint8_t>(raw & 0xFF);
            element.format = static_cast<uint8_t>((raw >> 8) & 0xFF);
            element.inputSlot = static_cast<uint8_t>((raw >> 16) & 0xFF);
            element.semanticIndex = static_cast<uint8_t>((raw >> 24) & 0xFF);
            element.offset = static_cast<uint32_t>((raw >> 32) & 0x7FFFFFFF);
            element.instanceData = ((raw >> 63) & 1) != 0;
            p.inputLayout.push_back(element);
        }
    }

    p.rasterizerState = CopyState<16>(r, rasterizerPtr);
    p.blendState = CopyState<36>(r, blendPtr);
    p.depthStencilState = CopyState<8>(r, depthStencilPtr);

    return p;
}
