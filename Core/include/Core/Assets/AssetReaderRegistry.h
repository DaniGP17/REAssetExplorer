#ifndef REASSETEXPLORER_ASSETREADERREGISTRY_H
#define REASSETEXPLORER_ASSETREADERREGISTRY_H
#include <memory>
#include <vector>

#include "IAssetReader.h"

class AssetReaderRegistry {
public:
    void Register(std::unique_ptr<IAssetReader> reader) {
        readers.push_back(std::move(reader));
    }

    template <typename T>
    const T* Get() const {
        for (const auto& reader : readers) {
            if (auto* typed = dynamic_cast<const T*>(reader.get())) {
                return typed;
            }
        }
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<IAssetReader>> readers;
};

#endif
