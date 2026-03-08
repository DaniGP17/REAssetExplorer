#ifndef REASSETEXPLORER_SDFDATA_H
#define REASSETEXPLORER_SDFDATA_H
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct SdfConstantVariable {
    std::string name;
    uint32_t nameHash;
    uint32_t data;
};

struct SdfConstantBuffer {
    std::string name;
    uint32_t nameHash;
    uint32_t shaderId;
    bool sceneDependent;
    uint32_t size;
    uint64_t slotRaw;
    std::vector<SdfConstantVariable> variables;
};

struct SdfResource {
    std::string name;
    uint32_t nameHash;
    uint32_t shaderId;
    bool sceneDependent;
    uint64_t slotRaw;
};

struct SdfInputElement {
    uint8_t semantic;
    uint8_t format;
    uint8_t inputSlot;
    uint8_t semanticIndex;
    uint32_t offset;
    bool instanceData;
};

struct SdfProgram {
    std::string name;
    std::vector<uint8_t> vs, ps, cs;  // stages 0, 4 and 5; the others are not used
    std::vector<SdfConstantBuffer> constantBuffers;
    std::vector<SdfResource> srvs;
    std::vector<SdfResource> samplers;
    std::vector<SdfResource> uavs;
    std::vector<SdfInputElement> inputLayout;
    std::array<uint8_t, 16> rasterizerState{};
    std::array<uint8_t, 36> blendState{};
    std::array<uint8_t, 8> depthStencilState{};
};

struct SdfData {
    uint16_t variantCount;
    uint16_t programCount;
    std::vector<SdfProgram> programs;

    const SdfProgram* FindProgram(std::string_view name) const {
        for (const SdfProgram& program : programs) {
            if (program.name == name) return &program;
        }
        return nullptr;
    }
};

#endif
