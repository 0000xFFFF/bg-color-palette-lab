#pragma once
#include <string>
#include <vector>

extern std::vector<std::string> supportedExtensions;
bool isSupportedFormat(const std::string& filename);
size_t scanFolder(std::vector<std::string>& imageFiles, const std::string& folderPath);
std::string formatTime(int seconds);
size_t getImages(std::vector<std::string>& images, const std::string& inputPath);

namespace Cursor {
    void termClear();
    void reset();
    void hide();
    void show();
    void cr();
}; // namespace Cursor
