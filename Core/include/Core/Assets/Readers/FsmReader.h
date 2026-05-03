#ifndef REASSETEXPLORER_FSMREADER_H
#define REASSETEXPLORER_FSMREADER_H
#include <span>

#include "Core/Assets/FsmData.h"
#include "Core/Assets/IAssetReader.h"
#include "Core/Rsz/RszTypeDatabase.h"

class FsmReader : public IAssetReader {
public:
    explicit FsmReader(uint32_t fsmv2TreeVersion) : fsmv2TreeVersion(fsmv2TreeVersion) {}

    bool SupportsPath(std::string_view path) const override;

    FsmData Read(std::span<const uint8_t> data, const RszTypeDatabase& db) const;

private:
    uint32_t fsmv2TreeVersion;
};

#endif
