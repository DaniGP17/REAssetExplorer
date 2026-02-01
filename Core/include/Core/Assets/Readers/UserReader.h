#ifndef REASSETEXPLORER_USERREADER_H
#define REASSETEXPLORER_USERREADER_H
#include <span>
#include <string>
#include <vector>

#include "Core/Assets/IAssetReader.h"
#include "Core/Rsz/RszTypeDatabase.h"
#include "Core/Rsz/RszTypes.h"

// userData: the other .user files this one references.
struct UserFileData {
    std::vector<std::string> resources;
    std::vector<RszUserDataRef> userData;
    RszData rsz;

    int32_t RootInstance() const {
        if (rsz.objectTable.empty()) return -1;
        int32_t index = rsz.objectTable.front();
        return index > 0 && index < static_cast<int32_t>(rsz.instances.size()) ? index : -1;
    }
};

class UserReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".user.") != std::string_view::npos; }

    UserFileData Read(std::span<const uint8_t> data, const RszTypeDatabase& db) const;
};

#endif
