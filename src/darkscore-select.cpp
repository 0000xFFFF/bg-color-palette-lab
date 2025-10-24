#include "globals.hpp"
#include <argparse/argparse.hpp>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <random>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <thread>
#include <chrono>

void daemonize()
{
    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "Fork failed\n";
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        // Parent exits
        exit(EXIT_SUCCESS);
    }

    // Child continues
    if (setsid() < 0) {
        exit(EXIT_FAILURE);
    }

    // Catch, ignore, or handle signals here if needed
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    // Fork again to prevent reacquiring a terminal
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    // Set new file permissions
    umask(0);

    // Change working directory to root
    chdir("/");

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect stdout/stderr to a log file
    std::ofstream log("/tmp/darkscore-select.log", std::ios::app);
    if (log.is_open()) {
        std::cout.rdbuf(log.rdbuf());
        std::cerr.rdbuf(log.rdbuf());
    }
}

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
    if (hour >= 9) return 2;  // bright
    if (hour >= 7) return 2;  // mid-dark
    if (hour >= 5) return 1;  // dark
    if (hour >= 0) return 0;  // very dark
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

std::vector<std::vector<DarkScoreResult>> loadBuckets(const std::string& inputPath)
{
    std::vector<std::vector<DarkScoreResult>> buckets(6);
    
    std::ifstream file(inputPath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << inputPath << std::endl;
        return buckets;
    }

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
    
    return buckets;
}

DarkScoreResult selectWallpaper(const std::vector<std::vector<DarkScoreResult>>& buckets, int hour)
{
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
        throw std::runtime_error("No wallpapers available in any brightness bucket!");
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(buckets[chosenBucket].size()) - 1);
    
    return buckets[chosenBucket][dist(gen)];
}

void printBucketInfo(const std::vector<std::vector<DarkScoreResult>>& buckets)
{
    std::cout << "Map darkness score (0=bright, 1=dark) → bucket 0-5 (0=darkest, 5=brightest)" << std::endl;
    for (size_t i = 0; i < buckets.size(); i++) {
        std::cout << "bucket " << i << " has " << buckets[i].size() << " images" << std::endl;
    }
}

std::string trim(const std::string& str) {
    const std::string whitespace = " \n\r\t\f\v";
    const auto first = str.find_first_not_of(whitespace);
    if (first == std::string::npos) return "";
    const auto last = str.find_last_not_of(whitespace);
    return str.substr(first, (last - first + 1));
}

void executeWallpaperChange(const std::string& execStr, const DarkScoreResult& chosen, int hour)
{
    std::time_t now = std::time(nullptr);
    std::cout << "[" << trim(std::string(std::ctime(&now))) << "] ";
    std::cout << "Hour: " << hour 
              << " | Selected: " << chosen.filePath 
              << " | Score: " << chosen.score << std::endl;

    if (!execStr.empty()) {
        std::string command = execStr + " " + chosen.filePath;
        int result = system(command.c_str());
        if (result != 0) {
            std::cerr << "Warning: Command execution returned non-zero status: " << result << std::endl;
        }
    }
}

constexpr int LOOP_SLEEP_MS = 1000 * 60 * 1; // 1 min

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("darkscore-select", VERSION);
    program.add_description("select wallpaper from csv file based on time of day and darkness score (night = dark, day = bright)");
    program.add_argument("-i", "--input")
        .required()
        .help("csv file that was made by bgcpl-darkscore")
        .metavar("file.csv");

    program.add_argument("-e", "--exec")
        .help("pass image to a command and execute (e.g. plasma-apply-wallpaperimage <image>)")
        .metavar("command")
        .default_value("");

    program.add_argument("-d", "--daemon")
        .default_value(false)
        .implicit_value(true)
        .help("run daemon in the background");

    program.add_argument("-l", "--loop")
        .default_value(false)
        .implicit_value(true)
        .help("loop logic for setting wallpapers");

    program.add_argument("-s", "--sleep")
        .help("sleep ms for loop")
        .metavar("sleep_ms")
        .default_value(LOOP_SLEEP_MS)
        .scan<'i', int>();

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
        std::cout << err.what() << std::endl;
        std::cout << program;
        return 1;
    }

    std::string inputPath = program.get<std::string>("input");
    std::string execStr = program.get<std::string>("exec");
    bool isDaemon = program.get<bool>("daemon");
    bool isLoop = program.get<bool>("loop");
    int sleepMs = program.get<int>("sleep");

    // Daemonize if requested
    if (isDaemon) {
        daemonize();
    }

    // Load buckets once
    auto buckets = loadBuckets(inputPath);
    
    // Check if any buckets have images
    bool hasImages = false;
    for (const auto& bucket : buckets) {
        if (!bucket.empty()) {
            hasImages = true;
            break;
        }
    }
    
    if (!hasImages) {
        std::cerr << "Error: No valid images found in CSV file!" << std::endl;
        return 1;
    }

    if (!isDaemon && !isLoop) {
        printBucketInfo(buckets);
    }

    // Main execution logic
    if (isLoop || isDaemon) {
        // Loop mode: continuously select and change wallpaper
        while (true) {
            try {
                std::time_t now = std::time(nullptr);
                std::tm* local = std::localtime(&now);
                int hour = local->tm_hour;

                auto chosen = selectWallpaper(buckets, hour);
                executeWallpaperChange(execStr, chosen, hour);

                // Sleep for specified duration
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            }
            catch (const std::exception& e) {
                std::cerr << "Error in loop: " << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(60)); // Wait before retrying
            }
        }
    }
    else {
        // Single execution mode
        std::time_t now = std::time(nullptr);
        std::tm* local = std::localtime(&now);
        int hour = local->tm_hour;

        try {
            auto chosen = selectWallpaper(buckets, hour);
            
            int targetBucket = getTargetBucketForHour(hour);
            int chosenBucket = getDarknessBucket(chosen.score);
            
            std::cout << "Current hour: " << hour << std::endl;
            std::cout << "Target bucket: " << targetBucket << " (used " << chosenBucket << ")\n";
            std::cout << "Selected wallpaper: " << chosen.filePath << "\n";
            std::cout << "Darkness score: " << chosen.score << std::endl;

            if (!execStr.empty()) {
                std::string command = execStr + " \"" + chosen.filePath + "\"";
                system(command.c_str());
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }

    return 0;
}
