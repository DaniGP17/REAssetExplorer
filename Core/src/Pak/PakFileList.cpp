#include "Core/PakFileList.h"

#include <fstream>
#include <stdexcept>
#include <vector>

void PakFileList::SetupFromFile(std::string filePath) {
    std::string content = ReadWholeFile(filePath);
    std::vector<std::string_view> lines = SplitLines(content);

    hashDict.clear();

    for (std::string_view& line : lines) {
        auto entry = CreateHashListEntry(line);
        hashDict[entry.lowerCaseHash] = entry;
    }
}

std::string PakFileList::ReadWholeFile(const std::string &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Couldn't open: " + path);

    std::size_t size = file.tellg();
    file.seekg(0);

    std::string buffer(size, '\0');
    file.read(buffer.data(), size);

    return buffer;
}

std::vector<std::string_view> PakFileList::SplitLines(std::string_view content) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;

    while (start < content.size()) {
        std::size_t end = content.find('\n', start);
        if (end == std::string_view::npos) end = content.size();

        std::size_t len = end - start;
        if (len > 0 && content[start + len - 1] == '\r') len--;

        lines.emplace_back(content.data() + start, len);
        start = end + 1;
    }

    return lines;
}