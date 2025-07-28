#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <queue>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

struct ValidationResult {
    std::string filePath;
    std::string filename;
    bool isValid;
    int width;
    int height;
};

class ThreadSafeQueue {

  public:
    void push(const std::string& item)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(item);
        m_condition.notify_one();
    }

    bool pop(std::string& item, std::chrono::milliseconds timeout = std::chrono::milliseconds(100))
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_condition.wait_for(lock, timeout, [this] { return !m_queue.empty(); })) {
            item = m_queue.front();
            m_queue.pop();
            return true;
        }
        return false;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

  private:
    std::queue<std::string> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
};

class ImageValidator {
  private:
    std::vector<std::string> m_supportedExtensions = {
        ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif", ".webp", ".gif"};

    std::vector<ValidationResult> m_Results;
    std::mutex m_ResultsMutex;
    std::atomic<int> m_ProcessedCount{0};
    std::atomic<int> m_TotalCount{0};

    int m_ValidCount = 0;
    int m_CorruptedCount = 0;

    const unsigned int m_NumThreads;

    bool isSupportedFormat(const std::string& filename)
    {
        std::string extension = filename.substr(filename.find_last_of("."));
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        return std::find(m_supportedExtensions.begin(), m_supportedExtensions.end(), extension) != m_supportedExtensions.end();
    }

    ValidationResult validateImage(const std::string& imagePath)
    {
        ValidationResult result;
        result.filePath = imagePath;
        result.filename = fs::path(imagePath).filename().string();
        result.isValid = false;
        result.width = 0;
        result.height = 0;

        try {
            cv::Mat image = cv::imread(imagePath);
            if (!image.empty()) {
                result.isValid = true;
                result.width = image.cols;
                result.height = image.rows;
            }
        }
        catch (const cv::Exception& e) {
            // OpenCV exception - image is corrupted
            result.isValid = false;
        }
        catch (...) {
            // Any other exception
            result.isValid = false;
        }

        return result;
    }

    void workerThread(ThreadSafeQueue& workQueue)
    {
        std::string imagePath;
        while (workQueue.pop(imagePath)) {
            ValidationResult result = validateImage(imagePath);

            // Thread-safe result storage
            {
                std::lock_guard<std::mutex> lock(m_ResultsMutex);
                m_Results.push_back(result);
            }

            int current = ++m_ProcessedCount;

            // Thread-safe progress output (less frequent to avoid spam)
            if (current % 10 == 0 || current == m_TotalCount) {
                std::cout << "\rProgress: " << current << "/" << m_TotalCount
                          << " images processed ("
                          << std::fixed << std::setprecision(1)
                          << (100.0 * current / m_TotalCount) << "%)" << std::flush;
            }
        }
    }

  public:
    ImageValidator() : m_NumThreads(std::thread::hardware_concurrency())
    {
        std::cout << "Using " << m_NumThreads << " threads for validation." << std::endl;
    }

    explicit ImageValidator(unsigned int threads) : m_NumThreads(threads)
    {
        std::cout << "Using " << threads << " threads for validation." << std::endl;
    }

    void scanFolder(const std::string& folderPath)
    {
        std::cout << "Scanning folder: " << folderPath << std::endl;

        // Collect all image files first
        std::vector<std::string> imageFiles;

        try {
            for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
                if (entry.is_regular_file() && isSupportedFormat(entry.path().filename().string())) {
                    imageFiles.push_back(entry.path().string());
                }
            }
        }
        catch (const fs::filesystem_error& ex) {
            std::cerr << "Error scanning folder: " << ex.what() << std::endl;
            exit(1);
            return;
        }

        m_TotalCount = imageFiles.size();
        std::cout << "Found " << m_TotalCount << " image files to validate." << std::endl;

        if (m_TotalCount == 0) {
            std::cout << "No images found to validate." << std::endl;
            return;
        }

        // Create work queue and populate it
        ThreadSafeQueue workQueue;
        for (const auto& imagePath : imageFiles) {
            workQueue.push(imagePath);
        }

        // Start worker threads
        std::vector<std::thread> workers;
        workers.reserve(m_NumThreads);

        auto startTime = std::chrono::high_resolution_clock::now();

        for (unsigned int i = 0; i < m_NumThreads; ++i) {
            workers.emplace_back(&ImageValidator::workerThread, this, std::ref(workQueue));
        }

        // Wait for all threads to complete
        for (auto& worker : workers) {
            worker.join();
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        std::cout << "\nValidation completed in " << duration.count() << "ms" << std::endl;
        std::cout << "Average: " << std::fixed << std::setprecision(2)
                  << (double)duration.count() / m_TotalCount << "ms per image" << std::endl;
    }

    void calcAndPrintSummary()
    {
        for (const auto& result : m_Results) {
            if (result.isValid) {
                m_ValidCount++;
            }
            else {
                m_CorruptedCount++;
            }
        }

        std::cout << "\n=== VALIDATION SUMMARY ===" << std::endl;
        std::cout << "Total files processed: " << m_Results.size() << std::endl;
        std::cout << "Valid images: " << m_ValidCount << std::endl;
        std::cout << "Corrupted/unreadable images: " << m_CorruptedCount << std::endl;

        if (m_CorruptedCount > 0) {
            std::cout << "\nCorrupted files:" << std::endl;
            for (const auto& result : m_Results) {
                if (!result.isValid) {
                    std::cout << "  " << result.filePath << std::endl;
                }
            }
        }
    }

    bool foundCorruptedFiles()
    {
        return m_CorruptedCount > 0;
    }

    void printDetailedResults()
    {
        std::cout << "\n=== DETAILED RESULTS ===" << std::endl;

        // Sort results by filename for better readability
        std::vector<ValidationResult> sortedResults = m_Results;
        std::sort(sortedResults.begin(), sortedResults.end(),
                  [](const ValidationResult& a, const ValidationResult& b) {
                      return a.filename < b.filename;
                  });

        for (const auto& result : sortedResults) {
            std::cout << result.filename;
            if (result.isValid) {
                std::cout << " - OK (" << result.width << "x" << result.height << ")";
            }
            else {
                std::cout << " - FAILED TO LOAD";
            }
            std::cout << std::endl;
        }
    }

    void deleteCorruptedFiles()
    {
        std::vector<std::string> corruptedFiles;
        for (const auto& result : m_Results) {
            if (!result.isValid) {
                corruptedFiles.push_back(result.filePath);
            }
        }

        if (corruptedFiles.empty()) {
            std::cout << "No corrupted files to delete." << std::endl;
            return;
        }

        char response;
        std::cout << "\nDo you want to DELETE all " << corruptedFiles.size()
                  << " corrupted files? (y/N): ";
        std::cin >> response;

        if (response == 'y' || response == 'Y') {
            int deletedCount = 0;
            int errorCount = 0;
            for (const auto& file : corruptedFiles) {
                try {
                    if (fs::remove(file)) {
                        std::cout << "Deleted: " << file << std::endl;
                        deletedCount++;
                    }
                    else {
                        std::cout << "Failed to delete: " << file << std::endl;
                    }
                }
                catch (const fs::filesystem_error& ex) {
                    std::cout << "Error deleting " << file << ": " << ex.what() << std::endl;
                    errorCount++;
                }
            }
            std::cout << "\nDeleted " << deletedCount << " corrupted files." << std::endl;
            std::cout << "\nErrors: " << errorCount << std::endl;
        }
        else {
            std::cout << "Deletion cancelled." << std::endl;
        }
    }

    void moveCorruptedFiles(const std::string& quarantineFolder = "corrupted_images")
    {
        std::vector<std::string> corruptedFiles;
        for (const auto& result : m_Results) {
            if (!result.isValid) {
                corruptedFiles.push_back(result.filePath);
            }
        }

        if (corruptedFiles.empty()) {
            std::cout << "No corrupted files to move." << std::endl;
            return;
        }

        try {
            fs::create_directories(quarantineFolder);

            int movedCount = 0;
            for (const auto& file : corruptedFiles) {
                fs::path sourcePath(file);
                fs::path destPath = fs::path(quarantineFolder) / sourcePath.filename();

                // Handle filename conflicts
                int counter = 1;
                while (fs::exists(destPath)) {
                    std::string stem = sourcePath.stem().string();
                    std::string extension = sourcePath.extension().string();
                    destPath = fs::path(quarantineFolder) / (stem + "_" + std::to_string(counter) + extension);
                    counter++;
                }

                try {
                    fs::rename(file, destPath);
                    std::cout << "Moved: " << sourcePath.filename() << " -> " << destPath << std::endl;
                    movedCount++;
                }
                catch (const fs::filesystem_error& ex) {
                    std::cout << "Error moving " << file << ": " << ex.what() << std::endl;
                }
            }
            std::cout << "\nMoved " << movedCount << " corrupted files to '" << quarantineFolder << "' folder." << std::endl;
        }
        catch (const fs::filesystem_error& ex) {
            std::cerr << "Error creating quarantine folder: " << ex.what() << std::endl;
        }
    }
};

int main(int argc, char* argv[])
{
    std::string inputFolder = "bg";

    if (argc >= 2) {
        inputFolder = argv[1];
    }
    else {
        std::cout << "Usage: " << argv[0] << " <folder_path>" << std::endl;
        std::cout << "Using default folder: " << inputFolder << std::endl;
    }

    std::cout << "Image Validator - Corrupted Image Detector" << std::endl;
    std::cout << "=========================================" << std::endl;

    ImageValidator validator;

    // Scan and validate all images
    validator.scanFolder(inputFolder);

    // Show summary
    validator.calcAndPrintSummary();

    if (validator.foundCorruptedFiles()) {

        // Ask user what to do with corrupted files
        std::cout << "\nWhat would you like to do with corrupted files?" << std::endl;
        std::cout << "1. Delete them permanently" << std::endl;
        std::cout << "2. Move them to 'corrupted_images' folder" << std::endl;
        std::cout << "3. Do nothing" << std::endl;
        std::cout << "Choice (1/2/3): ";

        int choice;
        std::cin >> choice;

        switch (choice) {
            case 1:
                validator.deleteCorruptedFiles();
                break;
            case 2:
                validator.moveCorruptedFiles();
                break;
            case 3:
                std::cout << "No action taken." << std::endl;
                break;
            default:
                std::cout << "Invalid choice. No action taken." << std::endl;
                break;
        }
    }

    return 0;
}
