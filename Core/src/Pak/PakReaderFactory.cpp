#include "Core/PakReaderFactory.h"

#include <stdexcept>

#include "Core/PakReaderV4.h"

std::unique_ptr<IPakReader> CreatePakReader(PakVersions version) {
    switch (version) {
        case PakVersions::V4: return std::make_unique<PakReaderV4>();
    }
    throw std::runtime_error("CreatePakReader: unsupported version");
}
