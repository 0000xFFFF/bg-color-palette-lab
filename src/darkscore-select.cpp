#include <argparse/argparse.hpp>
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "globals.hpp"

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
    program.add_description("select wallpaper from csv file based on time of day");
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
    std::vector<std::string> images;

    std::ifstream file(inputPath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << inputPath << std::endl;
        return 1;
    }

    std::string line;
    if (getline(file, line)) {} // skip header

    while (getline(file, line)) {
        std::vector<std::string> fields = split(line, CSV_DELIM);

        // Print parsed fields
        for (size_t i = 0; i < fields.size(); ++i) {
            std::cout << "[" << fields[i] << "]";
            if (i < fields.size() - 1) std::cout << " ";
        }
        std::cout << std::endl;
    }

    return 0;
}
