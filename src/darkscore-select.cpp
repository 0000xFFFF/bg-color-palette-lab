#include "globals.hpp"
#include <algorithm>
#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <random>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

std::atomic<bool> skipSleep(false);

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

std::string trim(const std::string& str)
{
    const std::string whitespace = " \n\r\t\f\v";
    const auto first = str.find_first_not_of(whitespace);
    if (first == std::string::npos) return "";
    const auto last = str.find_last_not_of(whitespace);
    return str.substr(first, (last - first + 1));
}

// Execute command directly without shell to avoid escaping issues
bool executeCommand(const std::string& program, const std::string& filePath)
{
    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "Fork failed" << std::endl;
        return false;
    }

    if (pid == 0) {
        // Child process
        // Redirect stdout/stderr to /dev/null in child to avoid issues
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        // Execute the command directly
        execlp(program.c_str(), program.c_str(), filePath.c_str(), nullptr);

        // If execlp returns, it failed
        exit(EXIT_FAILURE);
    }
    else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            int exitCode = WEXITSTATUS(status);
            if (exitCode != 0) {
                std::cerr << "Warning: Command exited with status: " << exitCode << std::endl;
                return false;
            }
            return true;
        }
        else {
            std::cerr << "Warning: Command did not exit normally" << std::endl;
            return false;
        }
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

// Set terminal to non-blocking mode
void setNonBlockingInput(bool enable)
{
    static struct termios old_tio, new_tio;
    static bool initialized = false;

    if (enable) {
        if (!initialized) {
            tcgetattr(STDIN_FILENO, &old_tio);
            new_tio = old_tio;
            new_tio.c_lflag &= ~(ICANON | ECHO);
            initialized = true;
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

        // Set stdin to non-blocking
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
    else {
        if (initialized) {
            tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

            // Restore blocking mode
            int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
            fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
        }
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
