#ifndef REASSETEXPLORER_FONTDECODE_H
#define REASSETEXPLORER_FONTDECODE_H
#include <cstdint>
#include <span>
#include <vector>

// .oft: "FBFO" then an OpenType/TrueType font XORed with an 8-byte key, one of
// two byte sequences at a rotation that varies per file. The key is the one
// whose output starts with a valid sfnt table directory. Throws when none does.
std::vector<uint8_t> DecryptFont(std::span<const uint8_t> oft);

#endif
