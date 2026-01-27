#ifndef REASSETEXPLORER_FOLIAGEDATA_H
#define REASSETEXPLORER_FOLIAGEDATA_H
#include <cstdint>
#include <string>
#include <vector>

struct FoliageInstance {
    float position[3]{};
    float rotation[4]{ 0, 0, 0, 1 };  // quaternion xyzw
    float scale[3]{ 1, 1, 1 };
};

struct FoliageUnit {
    std::string meshPath;
    std::string materialPath;
    std::vector<FoliageInstance> instances;
};

struct FoliageData {
    uint32_t version = 0;
    std::vector<FoliageUnit> units;
};

#endif
