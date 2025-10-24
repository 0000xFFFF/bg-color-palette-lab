#include <argparse/argparse.hpp>
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "globals.hpp"

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

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
        std::cout << err.what() << std::endl;
        std::cout << program;
        return 1;
    }

    std::string inputPath = program.get<std::string>("--input");

    std::ifstream file(inputPath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << inputPath << std::endl;
        return 1;
    }

    // parse images from csv
    std::vector<DarkScoreResult> images;
    std::string line;
    if (getline(file, line)) {} // skip header
    while (getline(file, line)) {
        std::vector<std::string> fields = split(line, CSV_DELIM);

        DarkScoreResult image;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (i == 0) { image.filePath = fields[0]; }
            if (i == 1) { image.score = std::stod(fields[1]); }
        }
        std::cout << std::endl;
    }



    return 0;
}
