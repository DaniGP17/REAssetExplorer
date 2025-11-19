#ifndef REASSETEXPLORER_IASSETREADER_H
#define REASSETEXPLORER_IASSETREADER_H
#include <string_view>

class IAssetReader {
public:
    virtual ~IAssetReader() = default;

    virtual bool SupportsPath(std::string_view path) const = 0;
};

#endif
