#include <argparse/argparse.hpp>
#include <ctime>
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "globals.hpp"

// Map darkness score (0=bright, 1=dark)
// bucket 0-5 (0=darkest, 5=brightest)
int getDarknessBucket(double score)
{
    if (score > 0.9) return 0; // very dark
    if (score > 0.8) return 1; // dark
    if (score > 0.6) return 2; // mid-dark
    if (score > 0.4) return 3; // mid-bright
    if (score > 0.2) return 4; // bright
    if (score > 0.0) return 5; // very bright
    return 5;
}

int getTargetBucketForHour(int hour)
{
    if (hour >= 20) return 0; // very dark
    if (hour >= 19) return 1; // dark
    if (hour >= 18) return 2; // mid-dark
    if (hour >= 17) return 3; // mid-bright
    if (hour >= 16) return 4; // bright
    if (hour >= 12) return 2; // very bright
    if (hour >= 9) return 2; // bright
    if (hour >= 7) return 2; // mid-dark
    if (hour >= 5) return 1; // dark
    if (hour >= 0) return 0; // very dark
    return 0;
}

struct DarkScoreResult {
    std::string filePath;
    double score;
};

std::vector<std::string> split(const std::string& line, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream ss(line);
    while (getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("darkscore-select", VERSION);
    program.add_description("select wallpaper from csv file based on time of day and darkness score (night = dark, day = bright)");
    program.add_argument("-i", "--input")
        .required()
        .help("csv file that was made by bgcpl-darkscore")
        .metavar("file.csv");

    program.add_argument("-e", "--exec")
        .help("pass image to a command and execute (e.g. plasma-apply-wallpaperimage <image>) (this calls system so make sure you pass valid command)")
        .metavar("file.csv")
        .default_value("");

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
        std::cout << err.what() << std::endl;
        std::cout << program;
        return 1;
    }

    std::string inputPath = program.get<std::string>("input");

    std::ifstream file(inputPath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << inputPath << std::endl;
        return 1;
    }

    std::vector<std::vector<DarkScoreResult>> buckets(6);

    // parse images from csv
    std::vector<DarkScoreResult> images;
    std::string line;
    if (getline(file, line)) {} // skip header
    while (getline(file, line)) {
        std::vector<std::string> fields = split(line, CSV_DELIM);
        if (fields.size() < 2) continue;

        DarkScoreResult image;
        image.filePath = fields[0];
        try {
            image.score = std::stod(fields[1]);
        }
        catch (...) {
            continue;
        }

        int b = getDarknessBucket(image.score);
        buckets[b].push_back(image);
    }

    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    int hour = local->tm_hour;
    int targetBucket = getTargetBucketForHour(hour);

    // fallback: look for nearest non-empty bucket
    int chosenBucket = targetBucket;
    int offset = 0;
    while (buckets[chosenBucket].empty() && offset < 6) {
        offset++;
        int up = targetBucket + offset;
        int down = targetBucket - offset;
        if (up < 6 && !buckets[up].empty()) {
            chosenBucket = up;
            break;
        }
        if (down >= 0 && !buckets[down].empty()) {
            chosenBucket = down;
            break;
        }
    }

    if (buckets[chosenBucket].empty()) {
        std::cerr << "No wallpapers available in any brightness bucket!" << std::endl;
        return 1;
    }

    std::cout << "Map darkness score (0=bright, 1=dark) → bucket 0-5 (0=darkest, 5=brightest)" << std::endl;

    for (size_t i = 0; i < buckets.size(); i++) {
        std::cout << "bucket " << i << " has " << buckets[i].size() << " images" << std::endl;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(buckets[chosenBucket].size()) - 1);
    const auto& chosen = buckets[chosenBucket][dist(gen)];

    std::cout << "Current hour: " << hour << std::endl;
    std::cout << "Target bucket: " << targetBucket << " (used " << chosenBucket << ")\n";
    std::cout << "Selected wallpaper: " << chosen.filePath << "\n";
    std::cout << "Darkness score: " << chosen.score << std::endl;

    std::string execStr = program.get<std::string>("exec");
    if (!execStr.empty()) {
        system(std::string(execStr + " " + chosen.filePath).c_str());
    }

    return 0;
}
