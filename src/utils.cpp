
#include "utils.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

std::vector<std::string> supportedExtensions = {
    ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif", ".webp", ".gif"};

bool isSupportedFormat(const std::string& filename)
{
    std::string extension = filename.substr(filename.find_last_of("."));
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    return std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) != supportedExtensions.end();
}

size_t scanFolder(std::vector<std::string>& imageFiles, const std::string& folderPath)
{
    std::cout << "Scanning folder: " << folderPath << std::endl;

    if (!std::filesystem::exists(folderPath)) {
        std::cerr << "Error scanning folder: " << folderPath << std::endl;
        return 0;
    }

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folderPath)) {
            if (entry.is_regular_file() && isSupportedFormat(entry.path().filename().string())) {
                imageFiles.push_back(entry.path().string());
            }
        }
    }
    catch (const std::filesystem::filesystem_error& ex) {
        std::cerr << "Error scanning folder: " << ex.what() << std::endl;
        exit(1);
        return 0;
    }

    size_t totalCount = imageFiles.size();
    std::cout << "Found " << totalCount << " image files." << std::endl;

    if (totalCount == 0) {
        std::cout << "No images found." << std::endl;
    }

    return totalCount;
}


std::string formatTime(int seconds)
{
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;

    if (hours > 0) {
        return std::to_string(hours) + "h " + std::to_string(minutes) + "m " + std::to_string(secs) + "s";
    }
    else if (minutes > 0) {
        return std::to_string(minutes) + "m " + std::to_string(secs) + "s";
    }
    else {
        return std::to_string(secs) + "s";
    }
}

namespace Cursor {
    void termClear() { std::cout << "\033[2J"; }
    void reset() { std::cout << "\033[H"; }
    void hide() { std::cout << "\033[?25l" << std::flush; }
    void show() { std::cout << "\033[?25h" << std::flush; }
    void cr() { std::cout << "\r" << std::flush; }

}; // namespace Cursor
