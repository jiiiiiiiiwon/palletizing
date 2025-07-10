#ifndef _JSON_UTILS
#define _JSON_UTILS

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>

#include <nlohmann/json.hpp>

// JSON utility functions
class JsonUtils {
public:
    static void save_to_json(const nlohmann::json& data, const std::string& filename) {
        std::ofstream file(filename);
        if (file.is_open()) {
            file << std::setw(4) << data << std::endl;
            file.close();
        } else {
            std::cerr << "Can't open: " << filename << std::endl;
        }
    }

    static nlohmann::json load_from_json(const std::string& filename) {
        nlohmann::json data;
        std::ifstream file(filename);
        if (file.is_open()) {
            file >> data;
            file.close();
        } else {
            std::cerr << "Can't open: " << filename << std::endl;
        }
        return data;
    }
};

#endif