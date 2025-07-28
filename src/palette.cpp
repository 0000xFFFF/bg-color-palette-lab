#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <algorithm>
#include <map>
#include <string>

struct ColorInfo {
    cv::Vec3b color;
    int count;
    double saturation;
    double brightness;
    double hue;
};

struct PaletteGroup {
    std::vector<ColorInfo> colors;
    std::string name;
};

class ColorPaletteExtractor {
private:
    cv::Mat image;
    std::vector<ColorInfo> palette;
    
    // Convert BGR to HSV and calculate color properties
    void calculateColorProperties(ColorInfo& colorInfo) {
        cv::Mat bgrPixel(1, 1, CV_8UC3, cv::Scalar(colorInfo.color[0], colorInfo.color[1], colorInfo.color[2]));
        cv::Mat hsvPixel;
        cv::cvtColor(bgrPixel, hsvPixel, cv::COLOR_BGR2HSV);
        
        cv::Vec3b hsv = hsvPixel.at<cv::Vec3b>(0, 0);
        colorInfo.hue = hsv[0] * 2.0; // OpenCV hue is 0-179, convert to 0-359
        colorInfo.saturation = hsv[1] / 255.0;
        colorInfo.brightness = hsv[2] / 255.0;
    }
    
    // Extract dominant colors using K-means clustering
    void extractPalette(int k = 8) {
        if (image.empty()) return;
        
        // Reshape image to a 2D array of pixels
        cv::Mat data = image.reshape(1, image.rows * image.cols);
        data.convertTo(data, CV_32F);
        
        // Apply K-means clustering
        cv::Mat labels, centers;
        cv::kmeans(data, k, labels, 
                  cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 20, 1.0),
                  3, cv::KMEANS_PP_CENTERS, centers);
        
        // Count occurrences of each cluster
        std::vector<int> counts(k, 0);
        for (int i = 0; i < labels.rows; i++) {
            counts[labels.at<int>(i)]++;
        }
        
        // Convert centers to color info
        palette.clear();
        for (int i = 0; i < k; i++) {
            ColorInfo colorInfo;
            colorInfo.color = cv::Vec3b(
                static_cast<uchar>(centers.at<float>(i, 0)),
                static_cast<uchar>(centers.at<float>(i, 1)),
                static_cast<uchar>(centers.at<float>(i, 2))
            );
            colorInfo.count = counts[i];
            calculateColorProperties(colorInfo);
            palette.push_back(colorInfo);
        }
        
        // Sort by count (most dominant first)
        std::sort(palette.begin(), palette.end(), 
                 [](const ColorInfo& a, const ColorInfo& b) {
                     return a.count > b.count;
                 });
    }
    
    // Group colors by characteristics
    std::map<std::string, PaletteGroup> groupColors() {
        std::map<std::string, PaletteGroup> groups;
        
        for (const auto& color : palette) {
            // Vibrant colors: high saturation and brightness
            if (color.saturation > 0.6 && color.brightness > 0.6) {
                groups["Vibrant"].colors.push_back(color);
                groups["Vibrant"].name = "Vibrant";
            }
            // Dark colors: low brightness
            else if (color.brightness < 0.3) {
                groups["Dark"].colors.push_back(color);
                groups["Dark"].name = "Dark";
            }
            // Light colors: high brightness, low saturation
            else if (color.brightness > 0.8 && color.saturation < 0.3) {
                groups["Light"].colors.push_back(color);
                groups["Light"].name = "Light";
            }
            // Muted colors: medium brightness, low saturation
            else if (color.saturation < 0.4) {
                groups["Muted"].colors.push_back(color);
                groups["Muted"].name = "Muted";
            }
            // Medium colors: everything else
            else {
                groups["Medium"].colors.push_back(color);
                groups["Medium"].name = "Medium";
            }
        }
        
        return groups;
    }
    
    // Create a visualization of the palette
    cv::Mat createPaletteVisualization(const std::map<std::string, PaletteGroup>& groups) {
        int swatchSize = 80;
        int padding = 10;
        int textHeight = 30;
        
        // Calculate total height needed
        int totalHeight = 0;
        for (const auto& group : groups) {
            if (!group.second.colors.empty()) {
                totalHeight += textHeight + swatchSize + padding * 2;
            }
        }
        
        int width = 600;
        cv::Mat visualization(totalHeight, width, CV_8UC3, cv::Scalar(255, 255, 255));
        
        int currentY = padding;
        
        for (const auto& group : groups) {
            if (group.second.colors.empty()) continue;
            
            // Draw group label
            cv::putText(visualization, group.first + " Colors:", 
                       cv::Point(padding, currentY + 20), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 0), 2);
            currentY += textHeight;
            
            // Draw color swatches
            int swatchX = padding;
            for (size_t i = 0; i < group.second.colors.size(); i++) {
                if (swatchX + swatchSize > width - padding) {
                    currentY += swatchSize + padding;
                    swatchX = padding;
                }
                
                cv::Rect swatchRect(swatchX, currentY, swatchSize, swatchSize);
                cv::rectangle(visualization, swatchRect, 
                             cv::Scalar(group.second.colors[i].color[0], 
                                       group.second.colors[i].color[1], 
                                       group.second.colors[i].color[2]), -1);
                
                // Add border
                cv::rectangle(visualization, swatchRect, cv::Scalar(0, 0, 0), 1);
                
                // Add percentage text
                double percentage = (double)group.second.colors[i].count / (image.rows * image.cols) * 100;
                std::string percentText = std::to_string((int)percentage) + "%";
                cv::putText(visualization, percentText, 
                           cv::Point(swatchX + 5, currentY + swatchSize - 10), 
                           cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255, 255, 255), 1);
                
                swatchX += swatchSize + padding;
            }
            currentY += swatchSize + padding * 2;
        }
        
        return visualization;
    }
    
public:
    bool loadImage(const std::string& imagePath) {
        image = cv::imread(imagePath);
        if (image.empty()) {
            std::cerr << "Error: Could not load image " << imagePath << std::endl;
            return false;
        }
        return true;
    }
    
    void processImage(int numColors = 8) {
        if (image.empty()) {
            std::cerr << "Error: No image loaded" << std::endl;
            return;
        }
        
        std::cout << "Extracting color palette..." << std::endl;
        extractPalette(numColors);
        
        auto groups = groupColors();
        
        // Print results
        std::cout << "\n=== COLOR PALETTE ANALYSIS ===" << std::endl;
        std::cout << "Image size: " << image.cols << "x" << image.rows << " pixels\n" << std::endl;
        
        for (const auto& group : groups) {
            if (group.second.colors.empty()) continue;
            
            std::cout << group.first << " Colors (" << group.second.colors.size() << "):" << std::endl;
            for (const auto& color : group.second.colors) {
                double percentage = (double)color.count / (image.rows * image.cols) * 100;
                std::cout << "  RGB(" << (int)color.color[2] << ", " << (int)color.color[1] 
                         << ", " << (int)color.color[0] << ") - " 
                         << std::fixed << std::setprecision(1) << percentage << "% "
                         << "(H:" << (int)color.hue << "° S:" << (int)(color.saturation*100) 
                         << "% B:" << (int)(color.brightness*100) << "%)" << std::endl;
            }
            std::cout << std::endl;
        }
        
        // Create and show visualization
        cv::Mat paletteViz = createPaletteVisualization(groups);
        
        // Resize original image for display
        cv::Mat displayImage;
        double scale = std::min(400.0 / image.cols, 400.0 / image.rows);
        cv::resize(image, displayImage, cv::Size(), scale, scale);
        
        // Show results
        cv::imshow("Original Image", displayImage);
        cv::imshow("Color Palette Groups", paletteViz);
        
        std::cout << "Press any key to exit..." << std::endl;
        cv::waitKey(0);
        cv::destroyAllWindows();
        
        // Save results
        cv::imwrite("palette_visualization.png", paletteViz);
        std::cout << "Palette visualization saved as 'palette_visualization.png'" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    std::string imagePath;
    int numColors = 8;
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <image_path> [num_colors]" << std::endl;
        std::cout << "Enter image path: ";
        std::getline(std::cin, imagePath);
    } else {
        imagePath = argv[1];
        if (argc >= 3) {
            numColors = std::atoi(argv[2]);
        }
    }
    
    ColorPaletteExtractor extractor;
    
    if (!extractor.loadImage(imagePath)) {
        return -1;
    }
    
    extractor.processImage(numColors);
    
    return 0;
}
