#ifndef _BOX_GENERATOR
#define _BOX_GENERATOR

#include <iostream>
#include <random>
#include <vector>
#include <tuple>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>

#include <nlohmann/json.hpp>

// Box generation utility
class BoxGenerator {
public:
    static std::vector<nlohmann::json> small_generate_boxes(
        int num_boxes,
        const std::pair<int, int>& x_range,
        const std::pair<int, int>& y_range,
        const std::pair<int, int>& z_range,
        const std::vector<std::tuple<int, int, int>>& sizes = {}) 
    {
        std::vector<nlohmann::json> boxes;
        std::mt19937 rng(std::random_device{}());

        if (sizes.empty())
        {
            for (int i = 0; i < num_boxes; ++i)
            {
                int x_size = std::uniform_int_distribution<int>(x_range.first, x_range.second)(rng);
                int y_size = std::uniform_int_distribution<int>(y_range.first, y_range.second)(rng);
                int z_size = std::uniform_int_distribution<int>(z_range.first, z_range.second)(rng);
                if (x_size < y_size)
                    std::swap(x_size, y_size);
                nlohmann::json box;
                box["box_id"] = i;
                box["box_size"] = {x_size, y_size, z_size};
                boxes.push_back(box);
            }
        }
        else
        {
            if (static_cast<int>(sizes.size()) != num_boxes)
            {
                throw std::invalid_argument("Length of sizes must match num_boxes");
            }
            for (int i = 0; i < num_boxes; ++i)
            {
                nlohmann::json box;
                box["box_id"] = i;
                box["box_size"] = {std::get<0>(sizes[i]), std::get<1>(sizes[i]), std::get<2>(sizes[i])};
                boxes.push_back(box);
            }
        }
        return boxes;
    }

    static std::vector<nlohmann::json> large_generate_boxes(
        int num_boxes,
        const std::pair<int, int>& x_range,
        const std::pair<int, int>& y_range,
        const std::pair<int, int>& z_range,
        const std::vector<std::tuple<int, int, int>>& sizes = {}) 
    {
        std::vector<nlohmann::json> boxes;
        std::mt19937 rng(std::random_device{}());

        if (sizes.empty())
        {
            for (int i = 0; i < num_boxes; ++i)
            {
                int x_size = std::uniform_int_distribution<int>(x_range.first, x_range.second)(rng);
                int y_size = std::uniform_int_distribution<int>(y_range.first, y_range.second)(rng);
                int z_size = std::uniform_int_distribution<int>(z_range.first, z_range.second)(rng);
                if (x_size < y_size)
                    std::swap(x_size, y_size);
                nlohmann::json box;
                box["box_id"] = i + 75;
                box["box_size"] = {x_size, y_size, z_size};
                boxes.push_back(box);
            }
        }
        else
        {
            if (static_cast<int>(sizes.size()) != num_boxes)
            {
                throw std::invalid_argument("Length of sizes must match num_boxes");
            }
            for (int i = 0; i < num_boxes; ++i)
            {
                nlohmann::json box;
                box["box_id"] = i + 75;
                box["box_size"] = {std::get<0>(sizes[i]), std::get<1>(sizes[i]), std::get<2>(sizes[i])};
                boxes.push_back(box);
            }
        }
        return boxes;
    }
};


#endif