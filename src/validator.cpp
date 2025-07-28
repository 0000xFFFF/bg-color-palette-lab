#include <opencv2/opencv.hpp>
#include <iostream>
#include <filesystem>
#include <vector>
#include <fstream>

namespace fs = std::filesystem;

class ImageValidator {
private:
    std::vector<std::string> supportedExtensions = {
        ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif", ".webp", ".gif"
    };
    
    std::vector<std::string> corruptedFiles;
    std::vector<std::string> validFiles;
    
    bool isSupportedFormat(const std::string& filename) {
        std::string extension = filename.substr(filename.find_last_of("."));
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        return std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) != supportedExtensions.end();
    }
    
public:
    void scanFolder(const std::string& folderPath) {
        std::cout << "Scanning folder: " << folderPath << std::endl;
        
        int totalFiles = 0;
        int processedFiles = 0;
        
        try {
            // First pass: count total files
            for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
                if (entry.is_regular_file() && isSupportedFormat(entry.path().filename().string())) {
                    totalFiles++;
                }
            }
            
            std::cout << "Found " << totalFiles << " image files to validate." << std::endl << std::endl;
            
            // Second pass: validate each image
            for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
                if (entry.is_regular_file() && isSupportedFormat(entry.path().filename().string())) {
                    processedFiles++;
                    std::string imagePath = entry.path().string();
                    std::string filename = entry.path().filename().string();
                    
                    std::cout << "[" << processedFiles << "/" << totalFiles << "] Checking: " << filename;
                    
                    // Try to load the image
                    cv::Mat image = cv::imread(imagePath);
                    
                    if (image.empty()) {
                        std::cout << " - FAILED TO LOAD" << std::endl;
                        corruptedFiles.push_back(imagePath);
                    } else {
                        std::cout << " - OK (" << image.cols << "x" << image.rows << ")" << std::endl;
                        validFiles.push_back(imagePath);
                    }
                }
            }
            
        } catch (const fs::filesystem_error& ex) {
            std::cerr << "Error scanning folder: " << ex.what() << std::endl;
            return;
        }
    }
    
    void printSummary() {
        std::cout << "\n=== VALIDATION SUMMARY ===" << std::endl;
        std::cout << "Total files processed: " << (validFiles.size() + corruptedFiles.size()) << std::endl;
        std::cout << "Valid images: " << validFiles.size() << std::endl;
        std::cout << "Corrupted/unreadable images: " << corruptedFiles.size() << std::endl;
        
        if (!corruptedFiles.empty()) {
            std::cout << "\nCorrupted files:" << std::endl;
            for (const auto& file : corruptedFiles) {
                std::cout << "  " << file << std::endl;
            }
        }
    }
    
    void saveReport(const std::string& reportPath = "corrupted_images.txt") {
        if (corruptedFiles.empty()) {
            std::cout << "\nNo corrupted images found - no report needed!" << std::endl;
            return;
        }
        
        std::ofstream report(reportPath);
        report << "CORRUPTED IMAGES REPORT\n";
        report << "======================\n\n";
        report << "Total corrupted files: " << corruptedFiles.size() << "\n\n";
        
        for (const auto& file : corruptedFiles) {
            report << file << "\n";
        }
        
        report.close();
        std::cout << "\nCorrupted files list saved to: " << reportPath << std::endl;
    }
    
    void deleteCorruptedFiles() {
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
            for (const auto& file : corruptedFiles) {
                try {
                    if (fs::remove(file)) {
                        std::cout << "Deleted: " << file << std::endl;
                        deletedCount++;
                    } else {
                        std::cout << "Failed to delete: " << file << std::endl;
                    }
                } catch (const fs::filesystem_error& ex) {
                    std::cout << "Error deleting " << file << ": " << ex.what() << std::endl;
                }
            }
            std::cout << "\nDeleted " << deletedCount << " corrupted files." << std::endl;
        } else {
            std::cout << "Deletion cancelled." << std::endl;
        }
    }
    
    void moveCorruptedFiles(const std::string& quarantineFolder = "corrupted_images") {
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
                } catch (const fs::filesystem_error& ex) {
                    std::cout << "Error moving " << file << ": " << ex.what() << std::endl;
                }
            }
            std::cout << "\nMoved " << movedCount << " corrupted files to '" << quarantineFolder << "' folder." << std::endl;
            
        } catch (const fs::filesystem_error& ex) {
            std::cerr << "Error creating quarantine folder: " << ex.what() << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    std::string inputFolder = "bg";
    
    if (argc >= 2) {
        inputFolder = argv[1];
    } else {
        std::cout << "Usage: " << argv[0] << " <folder_path>" << std::endl;
        std::cout << "Using default folder: " << inputFolder << std::endl;
    }
    
    std::cout << "Image Validator - Corrupted Image Detector" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    ImageValidator validator;
    
    // Scan and validate all images
    validator.scanFolder(inputFolder);
    
    // Show summary
    validator.printSummary();
    
    // Save report of corrupted files
    validator.saveReport();
    
    // Ask user what to do with corrupted files
    std::cout << "\nWhat would you like to do with corrupted files?" << std::endl;
    std::cout << "1. Delete them permanently" << std::endl;
    std::cout << "2. Move them to 'corrupted_images' folder" << std::endl;
    std::cout << "3. Do nothing (just keep the report)" << std::endl;
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
            std::cout << "No action taken. Check 'corrupted_images.txt' for the list." << std::endl;
            break;
        default:
            std::cout << "Invalid choice. No action taken." << std::endl;
            break;
    }
    
    return 0;
}
