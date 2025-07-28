#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <thread>
#include <mutex>

namespace fs = std::filesystem;

struct ColorInfo {
    cv::Vec3b color;
    double weight;
    double saturation;
    double brightness;
    double hue;
};

struct ImageInfo {
    std::string path;
    std::string filename;
    std::vector<ColorInfo> dominantColors;
    std::string assignedGroup;
    double groupScore;
};

class WallpaperGrouper {
  private:
    std::vector<ImageInfo> images;
    std::vector<std::string> supportedExtensions = {".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".webp"};

    // Predefined color groups with representative colors (HSV ranges)
    struct ColorGroup {
        std::string name;
        float hueMin, hueMax;
        float satMin, satMax;
        float brightMin, brightMax;
        cv::Vec3b representativeColor;
    };

    std::vector<ColorGroup> colorGroups = {
        {"Blue_Cool", 200, 260, 0.3f, 1.0f, 0.3f, 1.0f, cv::Vec3b(255, 100, 50)},
        {"Red_Warm", 340, 20, 0.3f, 1.0f, 0.3f, 1.0f, cv::Vec3b(50, 50, 255)},
        {"Green_Nature", 80, 140, 0.3f, 1.0f, 0.3f, 1.0f, cv::Vec3b(50, 255, 100)},
        {"Orange_Sunset", 20, 50, 0.4f, 1.0f, 0.4f, 1.0f, cv::Vec3b(50, 165, 255)},
        {"Purple_Mystical", 260, 300, 0.3f, 1.0f, 0.3f, 1.0f, cv::Vec3b(255, 50, 200)},
        {"Yellow_Bright", 50, 80, 0.4f, 1.0f, 0.5f, 1.0f, cv::Vec3b(50, 255, 255)},
        {"Pink_Soft", 300, 340, 0.3f, 1.0f, 0.4f, 1.0f, cv::Vec3b(200, 100, 255)},
        {"Cyan_Tech", 160, 200, 0.4f, 1.0f, 0.4f, 1.0f, cv::Vec3b(255, 200, 100)},
        {"Dark_Moody", 0, 360, 0.0f, 1.0f, 0.0f, 0.25f, cv::Vec3b(40, 40, 40)},
        {"Light_Minimal", 0, 360, 0.0f, 0.3f, 0.8f, 1.0f, cv::Vec3b(240, 240, 240)},
        {"Monochrome", 0, 360, 0.0f, 0.15f, 0.25f, 0.8f, cv::Vec3b(128, 128, 128)},
        {"Earth_Tones", 25, 45, 0.2f, 0.7f, 0.3f, 0.7f, cv::Vec3b(100, 150, 200)}};

    bool isSupportedFormat(const std::string& filename)
    {
        std::string extension = filename.substr(filename.find_last_of("."));
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        return std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) != supportedExtensions.end();
    }

    void calculateColorProperties(ColorInfo& colorInfo)
    {
        cv::Mat bgrPixel(1, 1, CV_8UC3, cv::Scalar(colorInfo.color[0], colorInfo.color[1], colorInfo.color[2]));
        cv::Mat hsvPixel;
        cv::cvtColor(bgrPixel, hsvPixel, cv::COLOR_BGR2HSV);

        cv::Vec3b hsv = hsvPixel.at<cv::Vec3b>(0, 0);
        colorInfo.hue = hsv[0] * 2.0;
        colorInfo.saturation = hsv[1] / 255.0;
        colorInfo.brightness = hsv[2] / 255.0;
    }

    std::vector<ColorInfo> extractDominantColors(const cv::Mat& image, int k = 5)
    {
        cv::Mat data = image.reshape(1, image.rows * image.cols);
        data.convertTo(data, CV_32F);

        cv::Mat labels, centers;
        cv::kmeans(data, k, labels,
                   cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 20, 1.0),
                   3, cv::KMEANS_PP_CENTERS, centers);

        std::vector<int> counts(k, 0);
        for (int i = 0; i < labels.rows; i++) {
            counts[labels.at<int>(i)]++;
        }

        std::vector<ColorInfo> colors;
        int totalPixels = image.rows * image.cols;

        for (int i = 0; i < k; i++) {
            ColorInfo colorInfo;
            colorInfo.color = cv::Vec3b(
                static_cast<uchar>(centers.at<float>(i, 0)),
                static_cast<uchar>(centers.at<float>(i, 1)),
                static_cast<uchar>(centers.at<float>(i, 2)));
            colorInfo.weight = (double)counts[i] / totalPixels;
            calculateColorProperties(colorInfo);
            colors.push_back(colorInfo);
        }

        std::sort(colors.begin(), colors.end(),
                  [](const ColorInfo& a, const ColorInfo& b) {
                      return a.weight > b.weight;
                  });

        return colors;
    }

    double calculateGroupScore(const std::vector<ColorInfo>& colors, const ColorGroup& group)
    {
        double score = 0.0;
        double totalWeight = 0.0;

        for (const auto& color : colors) {
            double colorScore = 0.0;

            // Check hue match (handle wraparound for red)
            bool hueMatch = false;
            if (group.hueMin > group.hueMax) { // wraparound case (red)
                hueMatch = (color.hue >= group.hueMin || color.hue <= group.hueMax);
            }
            else {
                hueMatch = (color.hue >= group.hueMin && color.hue <= group.hueMax);
            }

            if (hueMatch &&
                color.saturation >= group.satMin && color.saturation <= group.satMax &&
                color.brightness >= group.brightMin && color.brightness <= group.brightMax) {
                colorScore = 1.0;
            }
            else {
                // Partial scoring for near matches
                double hueDist = 0.0;
                if (group.hueMin > group.hueMax) {
                    hueDist = std::min({std::abs(color.hue - group.hueMin),
                                        std::abs(color.hue - group.hueMax),
                                        std::abs(color.hue - (group.hueMin - 360)),
                                        std::abs(color.hue - (group.hueMax + 360))}) /
                              180.0;
                }
                else {
                    hueDist = std::min(std::abs(color.hue - group.hueMin),
                                       std::abs(color.hue - group.hueMax)) /
                              180.0;
                }

                double satDist = std::max(0.0, std::max(group.satMin - color.saturation,
                                                        color.saturation - group.satMax));
                double brightDist = std::max(0.0, std::max(group.brightMin - color.brightness,
                                                           color.brightness - group.brightMax));

                colorScore = std::max(0.0, 1.0 - (hueDist + satDist + brightDist) / 3.0);
            }

            score += colorScore * color.weight;
            totalWeight += color.weight;
        }

        return totalWeight > 0 ? score / totalWeight : 0.0;
    }

    void assignImageToGroup(ImageInfo& imageInfo)
    {
        double bestScore = 0.0;
        std::string bestGroup = "Miscellaneous";

        for (const auto& group : colorGroups) {
            double score = calculateGroupScore(imageInfo.dominantColors, group);
            if (score > bestScore) {
                bestScore = score;
                bestGroup = group.name;
            }
        }

        imageInfo.assignedGroup = bestGroup;
        imageInfo.groupScore = bestScore;

        // If no group has a good score, assign to miscellaneous
        if (bestScore < 0.3) {
            imageInfo.assignedGroup = "Miscellaneous";
        }
    }

  public:
    void scanFolder(const std::string& folderPath)
    {
        std::cout << "Scanning folder: " << folderPath << std::endl;

        try {
            for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
                if (entry.is_regular_file() && isSupportedFormat(entry.path().filename().string())) {
                    ImageInfo imgInfo;
                    imgInfo.path = entry.path().string();
                    imgInfo.filename = entry.path().filename().string();
                    images.push_back(imgInfo);
                }
            }
        }
        catch (const fs::filesystem_error& ex) {
            std::cerr << "Error scanning folder: " << ex.what() << std::endl;
            return;
        }

        std::cout << "Found " << images.size() << " images to process." << std::endl;
    }

    void processImages()
    {
        int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4; // Fallback in case detection fails

        std::cout << "Using " << numThreads << " threads for processing." << std::endl;

        std::vector<std::thread> threads;
        std::mutex coutMutex;

        auto processChunk = [this, &coutMutex](int start, int end, int threadId) {
            for (int i = start; i < end; ++i) {
                auto& imageInfo = images[i];

                {
                    std::lock_guard<std::mutex> lock(coutMutex);
                    std::cout << "[Thread " << threadId << "] Processing: " << imageInfo.filename << std::endl;
                }

                cv::Mat image = cv::imread(imageInfo.path);
                if (image.empty()) {
                    std::lock_guard<std::mutex> lock(coutMutex);
                    std::cerr << "[Thread " << threadId << "] Could not load: " << imageInfo.path << std::endl;
                    continue;
                }

                if (image.cols > 800 || image.rows > 600) {
                    double scale = std::min(800.0 / image.cols, 600.0 / image.rows);
                    cv::resize(image, image, cv::Size(), scale, scale);
                }

                imageInfo.dominantColors = extractDominantColors(image);
                assignImageToGroup(imageInfo);
            }
        };

        int totalImages = images.size();
        int chunkSize = (totalImages + numThreads - 1) / numThreads;

        for (int t = 0; t < numThreads; ++t) {
            int start = t * chunkSize;
            int end = std::min(start + chunkSize, totalImages);
            if (start >= totalImages) break;
            threads.emplace_back(processChunk, start, end, t);
        }

        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        std::cout << "All threads finished processing." << std::endl;
    }

    void createGroupFolders(const std::string& outputPath = "grouped_wallpapers")
    {
        try {
            fs::create_directories(outputPath);

            std::map<std::string, std::vector<ImageInfo*>> groupedImages;

            // Group images by assigned category
            for (auto& image : images) {
                if (!image.assignedGroup.empty()) {
                    groupedImages[image.assignedGroup].push_back(&image);
                }
            }

            // Create folders and copy/move images
            for (const auto& group : groupedImages) {
                std::string groupPath = outputPath + "/" + group.first;
                fs::create_directories(groupPath);

                std::cout << "\n"
                          << group.first << " (" << group.second.size() << " images):" << std::endl;

                for (const auto& image : group.second) {
                    std::string destPath = groupPath + "/" + image->filename;

                    try {
                        fs::copy_file(image->path, destPath, fs::copy_options::overwrite_existing);
                        std::cout << "  Copied: " << image->filename << " (score: "
                                  << std::fixed << std::setprecision(2) << image->groupScore << ")" << std::endl;
                    }
                    catch (const fs::filesystem_error& ex) {
                        std::cerr << "  Error copying " << image->filename << ": " << ex.what() << std::endl;
                    }
                }
            }
        }
        catch (const fs::filesystem_error& ex) {
            std::cerr << "Error creating output folders: " << ex.what() << std::endl;
        }
    }

    void generateReport(const std::string& reportPath = "grouping_report.txt")
    {
        std::ofstream report(reportPath);

        std::map<std::string, std::vector<ImageInfo*>> groupedImages;
        for (auto& image : images) {
            if (!image.assignedGroup.empty()) {
                groupedImages[image.assignedGroup].push_back(&image);
            }
        }

        report << "WALLPAPER GROUPING REPORT\n";
        report << "=========================\n\n";
        report << "Total images processed: " << images.size() << "\n\n";

        for (const auto& group : groupedImages) {
            report << group.first << " (" << group.second.size() << " images)\n";
            report << std::string(group.first.length() + 20, '-') << "\n";

            for (const auto& image : group.second) {
                report << "  " << image->filename << " (confidence: "
                       << std::fixed << std::setprecision(2) << image->groupScore << ")\n";
            }
            report << "\n";
        }

        report.close();
        std::cout << "Report saved to: " << reportPath << std::endl;
    }

    void printSummary()
    {
        std::map<std::string, int> groupCounts;
        for (const auto& image : images) {
            if (!image.assignedGroup.empty()) {
                groupCounts[image.assignedGroup]++;
            }
        }

        std::cout << "\n=== GROUPING SUMMARY ===" << std::endl;
        std::cout << "Total images: " << images.size() << std::endl;

        for (const auto& group : groupCounts) {
            double percentage = (double)group.second / images.size() * 100;
            std::cout << group.first << ": " << group.second << " images ("
                      << std::fixed << std::setprecision(1) << percentage << "%)" << std::endl;
        }
    }
};

int main(int argc, char* argv[])
{
    std::string inputFolder = "bg";
    std::string outputFolder = "grouped_wallpapers";

    if (argc >= 2) {
        inputFolder = argv[1];
    }
    if (argc >= 3) {
        outputFolder = argv[2];
    }

    std::cout << "Wallpaper Color Grouping System" << std::endl;
    std::cout << "===============================" << std::endl;
    std::cout << "Input folder: " << inputFolder << std::endl;
    std::cout << "Output folder: " << outputFolder << std::endl
              << std::endl;

    WallpaperGrouper grouper;

    // Scan for images
    grouper.scanFolder(inputFolder);

    // Process all images
    grouper.processImages();

    // Show summary
    grouper.printSummary();

    // Create grouped folders
    grouper.createGroupFolders(outputFolder);

    // Generate detailed report
    grouper.generateReport("wallpaper_grouping_report.txt");

    std::cout << "\nGrouping complete! Check the '" << outputFolder << "' folder for results." << std::endl;

    return 0;
}
