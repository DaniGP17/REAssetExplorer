#ifndef REASSETEXPLORER_RSZREADER_H
#define REASSETEXPLORER_RSZREADER_H
#include <cstddef>

#include "RszTypeDatabase.h"
#include "RszTypes.h"

class MemoryReader;

class RszReader {
public:
    explicit RszReader(const RszTypeDatabase& database) : db(database) {}

    RszData Read(MemoryReader& r, std::size_t rszStart) const;

private:
    RszValue ReadValue(MemoryReader& r, const RszFieldDef& field) const;
    RszValue ReadScalar(MemoryReader& r, const RszFieldDef& field) const;

    const RszTypeDatabase& db;
};

#endif
