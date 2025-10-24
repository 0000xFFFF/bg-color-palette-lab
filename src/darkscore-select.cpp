#include "globals.hpp"
#include <algorithm>
#include <argparse/argparse.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <random>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "utils.hpp"

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

    // Close stdin
    close(STDIN_FILENO);

    // Redirect stdout and stderr to log file using file descriptors
    int logfd = open("/tmp/darkscore-select.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logfd != -1) {
        dup2(logfd, STDOUT_FILENO);
        dup2(logfd, STDERR_FILENO);
        if (logfd > 2) {
            close(logfd);
        }
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
        std::vector<std::string> fields = csv_split(line, CSV_DELIM);
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

// State tracker for sequential iteration through buckets
struct BucketIterator {
    std::vector<std::vector<DarkScoreResult>> shuffledBuckets;
    std::vector<size_t> currentIndices; // Current position in each bucket
    int lastUsedBucket;
    std::mt19937 rng;

    BucketIterator(const std::vector<std::vector<DarkScoreResult>>& buckets)
        : shuffledBuckets(buckets), currentIndices(6, 0), lastUsedBucket(-1)
    {
        std::random_device rd;
        rng.seed(rd());

        // Shuffle all buckets initially
        for (auto& bucket : shuffledBuckets) {
            std::shuffle(bucket.begin(), bucket.end(), rng);
        }
    }

    DarkScoreResult getNext(int targetBucket)
    {
        // Find the actual bucket to use (with fallback logic)
        int chosenBucket = targetBucket;
        int offset = 0;
        while (shuffledBuckets[chosenBucket].empty() && offset < 6) {
            offset++;
            int up = targetBucket + offset;
            int down = targetBucket - offset;
            if (up < 6 && !shuffledBuckets[up].empty()) {
                chosenBucket = up;
                break;
            }
            if (down >= 0 && !shuffledBuckets[down].empty()) {
                chosenBucket = down;
                break;
            }
        }

        if (shuffledBuckets[chosenBucket].empty()) {
            throw std::runtime_error("No wallpapers available in any brightness bucket!");
        }

        // If bucket changed, reset and reshuffle the new bucket
        if (chosenBucket != lastUsedBucket) {
            std::cout << "Bucket changed from " << lastUsedBucket
                      << " to " << chosenBucket
                      << ", reshuffling..." << std::endl;
            currentIndices[chosenBucket] = 0;
            std::shuffle(shuffledBuckets[chosenBucket].begin(),
                         shuffledBuckets[chosenBucket].end(),
                         rng);
            lastUsedBucket = chosenBucket;
        }

        // Get current wallpaper from bucket
        size_t& currentIdx = currentIndices[chosenBucket];
        const auto& result = shuffledBuckets[chosenBucket][currentIdx];

        // Advance index, wrap around and reshuffle if we've gone through all
        currentIdx++;
        if (currentIdx >= shuffledBuckets[chosenBucket].size()) {
            std::cout << "Reached end of bucket " << chosenBucket
                      << ", reshuffling..." << std::endl;
            currentIdx = 0;
            std::shuffle(shuffledBuckets[chosenBucket].begin(),
                         shuffledBuckets[chosenBucket].end(),
                         rng);
        }

        return result;
    }
};

void printBucketInfo(const std::vector<std::vector<DarkScoreResult>>& buckets)
{
    std::cout << "Map darkness score (0=bright, 1=dark) → bucket 0-5 (0=darkest, 5=brightest)" << std::endl;
    for (size_t i = 0; i < buckets.size(); i++) {
        std::cout << "bucket " << i << " has " << buckets[i].size() << " images" << std::endl;
    }
}

void executeWallpaperChange(const std::string& execStr, const DarkScoreResult& chosen, int hour)
{
    std::time_t now = std::time(nullptr);
    std::cout << "[" << trim(std::string(std::ctime(&now))) << "] ";
    std::cout << "Hour: " << hour
              << " | Selected: " << chosen.filePath
              << " | Score: " << chosen.score << std::endl;

    if (!execStr.empty()) {
        executeCommand(execStr, chosen.filePath);
    }
}

// Check if user pressed a key
bool checkKeyPress()
{
    char c;
    int n = read(STDIN_FILENO, &c, 1);
    return n > 0;
}

// Sleep with ability to skip on keypress
void interruptibleSleep(int sleepMs)
{
    const int checkIntervalMs = 100; // Check for input every 100ms
    int elapsed = 0;

    std::cout << "Sleeping for " << (sleepMs / 1000) << "s (press any key to skip)..." << std::endl;

    while (elapsed < sleepMs) {
        if (checkKeyPress()) {
            std::cout << "Sleep interrupted by user!" << std::endl;
            // Clear any remaining input
            char c;
            while (read(STDIN_FILENO, &c, 1) > 0);
            return;
        }

        int sleepTime = std::min(checkIntervalMs, sleepMs - elapsed);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
        elapsed += sleepTime;
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
        .help("pass image to a command and execute (e.g. plasma-apply-wallpaperimage)")
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

    try {
        inputPath = std::filesystem::canonical(inputPath).string();
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: Could not resolve path: " << inputPath << " - " << e.what() << std::endl;
        return 1;
    }

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

    if (!isDaemon) {
        printBucketInfo(buckets);
    }

    // Main execution logic
    if (isLoop || isDaemon) {
        // Create bucket iterator for sequential iteration
        BucketIterator iterator(buckets);

        // Enable non-blocking input for loop mode (but not daemon mode)
        if (isLoop && !isDaemon) {
            setNonBlockingInput(true);
        }

        // Loop mode: continuously select and change wallpaper
        while (true) {
            try {
                std::time_t now = std::time(nullptr);
                std::tm* local = std::localtime(&now);
                int hour = local->tm_hour;
                int targetBucket = getTargetBucketForHour(hour);

                auto chosen = iterator.getNext(targetBucket);
                executeWallpaperChange(execStr, chosen, hour);

                // Sleep with ability to skip (only in loop mode, not daemon)
                if (isLoop && !isDaemon) {
                    interruptibleSleep(sleepMs);
                }
                else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Error in loop: " << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(60)); // Wait before retrying
            }
        }

        // Restore terminal settings (won't reach here normally)
        if (isLoop && !isDaemon) {
            setNonBlockingInput(false);
        }
    }
    else {
        // Single execution mode - just pick randomly for one-time use
        std::time_t now = std::time(nullptr);
        std::tm* local = std::localtime(&now);
        int hour = local->tm_hour;

        try {
            int targetBucket = getTargetBucketForHour(hour);

            // Find the actual bucket to use (with fallback logic)
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

            // Random selection for single execution
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dist(0, static_cast<int>(buckets[chosenBucket].size()) - 1);
            const auto& chosen = buckets[chosenBucket][dist(gen)];

            std::cout << "Current hour: " << hour << std::endl;
            std::cout << "Target bucket: " << targetBucket << " (used " << chosenBucket << ")\n";
            std::cout << "Selected wallpaper: " << chosen.filePath << "\n";
            std::cout << "Darkness score: " << chosen.score << std::endl;

            executeWallpaperChange(execStr, chosen, hour);
        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }

    return 0;
}
