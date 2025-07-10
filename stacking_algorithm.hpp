#ifndef _STACKING_ALGORITHM
#define _STACKING_ALGORITHM

#include <iostream>
#include <cmath>
#include <unordered_map>
#include <tuple>
#include <vector>
#include <string>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <set>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <exception>
#include <memory>

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <gnuplot-iostream.h>
#include <gif_lib.h>
#include <Eigen/Dense>

#include "jsonUtils.hpp"
#include "boxGenerator.hpp"
#include "geometryUtils.hpp"
#include "visualizationUtils.hpp"

enum class StackingMethod {
    PALLET_ORIGIN_OUT_OF_BOUND,
    PALLET_STACK_ALL,
    BUFFER,
    STACK_WITH_BUFFER,
    OPTIMIZED_STACK
};

std::vector<int> parseBoxSize(const std::string& sizeStr)
{
    std::vector<int> sizes;
    std::string cleanStr = sizeStr;
    cleanStr.erase(std::remove(cleanStr.begin(), cleanStr.end(), '['), cleanStr.end());
    cleanStr.erase(std::remove(cleanStr.begin(), cleanStr.end(), ']'), cleanStr.end());
    
    std::stringstream ss(cleanStr);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        try {
            sizes.push_back(std::stoi(item));
        } catch (const std::exception& e) {
            std::cerr << "Error parsing size: " << item << std::endl;
        }
    }
    return sizes;
}

class BoxPlacement {
private:
    std::vector<int> pallet_dimensions;
    std::vector<bool> grid_cells;
    const int grid_size;
    
    struct PlacedBox {
        std::tuple<int, int, int> position;
        std::vector<int> size;
        int rotation;
    };
    std::vector<PlacedBox> placed_boxes;

    std::vector<int> getRotatedSize(const std::vector<int>& original_size, int rotation)
    {
        std::vector<int> new_size = original_size;
        if (rotation == 90)
        {
            std::swap(new_size[0], new_size[1]);
        }
        return new_size;
    }

    bool isWithinBounds(const std::tuple<int, int, int>& pos, 
                       const std::vector<int>& size) {
        int x = std::get<0>(pos);
        int y = std::get<1>(pos);
        int z = std::get<2>(pos);

        return (x >= 0 && x + size[0] <= pallet_dimensions[0] &&
                y >= 0 && y + size[1] <= pallet_dimensions[1] &&
                z >= 0 && z + size[2] <= pallet_dimensions[2]);
    }

    bool hasOverlap(const std::tuple<int, int, int>& pos, const std::vector<int>& size)
    {
        int x1 = std::get<0>(pos);
        int y1 = std::get<1>(pos);
        int z1 = std::get<2>(pos);

        for (int z = z1/grid_size; z <= (z1 + size[2])/grid_size; z++)
        {
            for (int y = y1/grid_size; y <= (y1 + size[1])/grid_size; y++)
            {
                for (int x = x1/grid_size; x <= (x1 + size[0])/grid_size; x++)
                {
                    int idx = (z * (pallet_dimensions[0]/grid_size) * 
                              (pallet_dimensions[1]/grid_size)) +
                             (y * (pallet_dimensions[0]/grid_size)) + x;
                    
                    if (grid_cells[idx])
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void markGridCells(const std::tuple<int, int, int>& pos, const std::vector<int>& size, bool value)
    {
        int x1 = std::get<0>(pos);
        int y1 = std::get<1>(pos);
        int z1 = std::get<2>(pos);

        for (int z = z1/grid_size; z <= (z1 + size[2])/grid_size; z++)
        {
            for (int y = y1/grid_size; y <= (y1 + size[1])/grid_size; y++)
            {
                for (int x = x1/grid_size; x <= (x1 + size[0])/grid_size; x++)
                {
                    int idx = (z * (pallet_dimensions[0]/grid_size) * 
                              (pallet_dimensions[1]/grid_size)) +
                             (y * (pallet_dimensions[0]/grid_size)) + x;
                    grid_cells[idx] = value;
                }
            }
        }
    }

public:
    BoxPlacement(const std::vector<int>& pallet_dims) 
        : pallet_dimensions(pallet_dims), grid_size(5) {
        grid_cells.resize((pallet_dims[0]/grid_size) * 
                         (pallet_dims[1]/grid_size) * 
                         (pallet_dims[2]/grid_size), false);
    }

    bool canPlaceBox(const std::vector<int>& box_size, 
                     const std::tuple<int, int, int>& position,
                     int rotation) {
        auto rotated_size = getRotatedSize(box_size, rotation);
        
        if (!isWithinBounds(position, rotated_size))
        {
            return false;
        }

        return !hasOverlap(position, rotated_size);
    }

    void placeBox(const std::vector<int>& box_size,
                  const std::tuple<int, int, int>& position,
                  int rotation) {
        auto rotated_size = getRotatedSize(box_size, rotation);
        markGridCells(position, rotated_size, true);
        placed_boxes.push_back({position, rotated_size, rotation});
    }
};

class StackingAlgorithm {
private:
    std::vector<std::unordered_map<std::string, std::string>> boxes;
    std::vector<int> pallet_size;
    int stacking_interval;
    std::unique_ptr<BoxPlacement> placement_manager;

    struct StackResult {
        std::string box_id;
        std::tuple<int, int, int> box_loc;
        int box_rot;
        int pallet_id;
    };

    std::vector<StackResult> final_placements;
    std::vector<std::tuple<int, int, int, int, int, int>> main_placements;
    std::vector<std::tuple<int, int, int, int, int, int>> buffer_placements;
    std::set<std::string> used_boxes;
    int buffer_count = 0;
    const int MAX_BUFFER_COUNT = 100;

    bool is_overlap(const std::tuple<int, int, int, int, int, int>& new_box,
                    const std::vector<std::tuple<int, int, int, int, int, int>>& placements)
    {
        int bx, by, bz, bwidth, blength, bheight;
        std::tie(bx, by, bz, bwidth, blength, bheight) = new_box;

        for (const auto& placement : placements)
        {
            int px, py, pz, pwidth, plength, pheight;
            std::tie(px, py, pz, pwidth, plength, pheight) = placement;

            if (!(bx + bwidth <= px || bx >= px + pwidth ||
                  by + blength <= py || by >= py + plength ||
                  bz + bheight <= pz || bz >= pz + pheight))
            {
                return true;    // 겹침 발견
            }
        }
        return false;
    }

    bool try_place_in_buffer(const std::unordered_map<std::string, std::string>& box)
    {
        std::vector<int> box_sizes = parseBoxSize(box.at("box_size"));
        if (box_sizes.size() < 3)
        {
            std::cerr << "Invalid box size for box ID: " << box.at("box_id") << std::endl;
            return false;
        }

        for (int y = 0; y <= pallet_size[1] - box_sizes[1]; y += stacking_interval)
        {
            for (int x = 0; x <= pallet_size[0] - box_sizes[0]; x += stacking_interval)
            {
                if (!is_overlap(std::make_tuple(x, y, 0, box_sizes[0], box_sizes[1], box_sizes[2]), buffer_placements))
                {
                    buffer_placements.push_back(std::make_tuple(
                        x, y, 0,
                        box_sizes[0] + stacking_interval,
                        box_sizes[1] + stacking_interval,
                        box_sizes[2] + stacking_interval
                    ));

                    final_placements.push_back({
                        box.at("box_id"),
                        std::make_tuple(x + std::ceil(box_sizes[0] / 2.0), 
                                      y + std::ceil(box_sizes[1] / 2.0), 0),
                        0,
                        2
                    });

                    buffer_count++;
                    used_boxes.insert(box.at("box_id"));
                    return true;
                }
            }
        }
        return false;
    }

    bool try_place_in_main(const std::unordered_map<std::string, std::string>& box)
    {
        std::vector<int> box_sizes = parseBoxSize(box.at("box_size"));
        if (box_sizes.size() < 3)
        {
            std::cerr << "Invalid box size for box ID: " << box.at("box_id") << std::endl;
            return false;
        }

        for (int z = 0; z <= pallet_size[2] - box_sizes[2]; z += stacking_interval)
        {
            for (int y = 0; y <= pallet_size[1] - box_sizes[1]; y += stacking_interval)
            {
                for (int x = 0; x <= pallet_size[0] - box_sizes[0]; x += stacking_interval)
                {
                    if (!is_overlap(std::make_tuple(x, y, z, box_sizes[0], box_sizes[1], box_sizes[2]), main_placements))
                    {
                        main_placements.push_back(std::make_tuple(
                            x, y, z,
                            box_sizes[0] + stacking_interval,
                            box_sizes[1] + stacking_interval,
                            box_sizes[2] + stacking_interval
                        ));

                        final_placements.push_back({
                            box.at("box_id"),
                            std::make_tuple(x + std::ceil(box_sizes[0] / 2.0), 
                                          y + std::ceil(box_sizes[1] / 2.0), z),
                            0,
                            1
                        });

                        used_boxes.insert(box.at("box_id"));
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool tryPlaceBox(const std::vector<int>& box_size, std::tuple<int, int, int>& position, int& rotation)
    {
        std::vector<int> rotations = {0, 90};  // 가능한 회전 각도들
        
        for (int z = 0; z <= pallet_size[2] - box_size[2]; z += stacking_interval)
        {
            for (int y = 0; y <= pallet_size[1] - box_size[1]; y += stacking_interval)
            {
                for (int x = 0; x <= pallet_size[0] - box_size[0]; x += stacking_interval)
                {
                    for (int rot : rotations)
                    {
                        auto pos = std::make_tuple(x, y, z);
                        if (placement_manager->canPlaceBox(box_size, pos, rot))
                        {
                            position = pos;
                            rotation = rot;
                            placement_manager->placeBox(box_size, pos, rot);
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    public:
    StackingAlgorithm(const std::vector<std::unordered_map<std::string, std::string>>& boxes, const std::vector<int>& pallet_size, int box_gap = 5)
        : boxes(boxes), pallet_size(pallet_size), stacking_interval(box_gap)
    {}

    ~StackingAlgorithm()
    {
        std::cout << "Object Destroyed" << std::endl;
    }

    std::vector<StackResult> stack_pallet_origin_out_of_bound()
    {
        std::vector<StackResult> result;
        int pallet_id = 1;
        const auto& box = boxes[0];
        result.push_back({
            box.at("box_id"),
            std::make_tuple(0, 0, 0),
            0,
            pallet_id 
        });
        return result;
    }

    std::tuple<std::string, std::vector<int>, std::tuple<int, int, int>> find_best_fit_from_buffer()
    {
        std::string best_box_id;
        std::vector<int> best_box_size;
        std::tuple<int, int, int> best_location;
        int best_fit_score = 0;

        for (const auto& placement : final_placements)
        {
            if (placement.pallet_id == 2)
            {
                auto box_it = std::find_if(boxes.begin(), boxes.end(),
                    [&](const auto& b) { return b.at("box_id") == placement.box_id; });
                if (box_it != boxes.end())
                {
                    auto curr_sizes = parseBoxSize(box_it->at("box_size"));
                    
                    for (int z = 0; z <= pallet_size[2] - curr_sizes[2]; z += stacking_interval)
                    {
                        for (int y = 0; y <= pallet_size[1] - curr_sizes[1]; y += stacking_interval)
                        {
                            for (int x = 0; x <= pallet_size[0] - curr_sizes[0]; x += stacking_interval)
                            {
                                if (!is_overlap(std::make_tuple(x, y, z, curr_sizes[0], curr_sizes[1], curr_sizes[2]), main_placements))
                                {
                                    // 적합도 점수 계산 (여기서는 간단히 부피로 계산)
                                    int fit_score = curr_sizes[0] * curr_sizes[1] * curr_sizes[2];
                                    if (fit_score > best_fit_score)
                                    {
                                        best_fit_score = fit_score;
                                        best_box_id = placement.box_id;
                                        best_box_size = curr_sizes;
                                        best_location = std::make_tuple(x, y, z);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        return {best_box_id, best_box_size, best_location};
    }

    bool move_best_fit_from_buffer_to_main()
    {
        auto [best_box_id, best_box_size, best_location] = find_best_fit_from_buffer();

        if (!best_box_id.empty())
        {
            auto it = std::find_if(final_placements.begin(), final_placements.end(),
                [&](const StackResult& result) {
                    return result.pallet_id == 2 && result.box_id == best_box_id;
                });

            if (it != final_placements.end())
            {
                final_placements.erase(it);
                buffer_count--;
                std::cout << "Moved box " << best_box_id << " from buffer to main" << std::endl;

                // 버퍼 팔레트 업데이트
                buffer_placements.clear();
                for (const auto& p : final_placements)
                {
                    if (p.pallet_id == 2)
                    {
                        auto box_info = std::find_if(boxes.begin(), boxes.end(),
                            [&](const auto& b) { return b.at("box_id") == p.box_id; });

                        if (box_info != boxes.end())
                        {
                            auto sizes = parseBoxSize(box_info->at("box_size"));
                            auto [x, y, z] = p.box_loc;
                            buffer_placements.push_back(std::make_tuple(
                                x - std::ceil(sizes[0] / 2.0),
                                y - std::ceil(sizes[1] / 2.0),
                                z,
                                sizes[0] + stacking_interval,
                                sizes[1] + stacking_interval,
                                sizes[2] + stacking_interval
                            ));
                        }
                    }
                }

                // 메인 팔레트에 박스 추가
                auto [x, y, z] = best_location;
                main_placements.push_back(std::make_tuple(
                    x, y, z,
                    best_box_size[0] + stacking_interval,
                    best_box_size[1] + stacking_interval,
                    best_box_size[2] + stacking_interval
                ));

                final_placements.push_back({
                    best_box_id,
                    std::make_tuple(x + std::ceil(best_box_size[0] / 2.0),
                                  y + std::ceil(best_box_size[1] / 2.0),
                                  z),
                    0,
                    1
                });

                return true;
            }
        }
        return false;
    }

    std::vector<StackResult> stack_all_boxes()
    {
        std::vector<std::tuple<int, int, int, int, int, int>> placements;
        std::vector<StackResult> out_placements;

        int pallet_width = pallet_size[0];
        int pallet_length = pallet_size[1];
        int pallet_height = pallet_size[2];

        for (const auto& box : boxes)
        {
            std::vector<int> box_sizes = parseBoxSize(box.at("box_size"));
            if (box_sizes.size() < 3)
            {
                std::cerr << "Invalid box size for box ID: " << box.at("box_id") << std::endl;
                continue;
            }
            int width = box_sizes[0];
            int length = box_sizes[1];
            int height = box_sizes[2];
            bool placed = false;

            for (int z = 0; z <= pallet_height - height; z += stacking_interval)
            {
                if (placed) break;
                
                for (int y = 0; y <= pallet_length - length; y += stacking_interval)
                {
                    if (placed) break;
                    
                    for (int x = 0; x <= pallet_width - width; x += stacking_interval)
                    {
                        if (!is_overlap(std::make_tuple(x, y, z, width, length, height), placements))
                        {
                            placements.push_back(std::make_tuple(
                                x, y, z,
                                width + stacking_interval,
                                length + stacking_interval,
                                height + stacking_interval
                            ));

                            int b_x = x + std::ceil(width/2.0);
                            int b_y = y + std::ceil(length/2.0);
                            int b_z = z;
                            out_placements.push_back({
                                box.at("box_id"),
                                std::make_tuple(b_x, b_y, b_z),
                                0,
                                1
                            });

                            placed = true;
                            break;
                        }
                    }
                }
            }

            if (!placed)
            {
                for (int z = 0; z <= pallet_height - height; z += stacking_interval)
                {
                    if (!is_overlap(std::make_tuple(0, 0, z, width, length, height), placements))
                    {
                        placements.push_back(std::make_tuple(
                            0, 0, z,
                            width + stacking_interval,
                            length + stacking_interval,
                            height + stacking_interval
                        ));

                        int b_x = std::ceil(width/2.0);
                        int b_y = std::ceil(length/2.0);
                        int b_z = z;
                        out_placements.push_back({
                            box.at("box_id"),
                            std::make_tuple(b_x, b_y, b_z),
                            0,
                            1
                        });

                        placed = true;
                        break;
                    }
                }
            }
        }
        return out_placements;
    }

    std::vector<StackResult> stack_buffer()
    {
        std::vector<StackResult> out_placements;
        int pallet_width = pallet_size[0];
        int pallet_length = pallet_size[1];

        auto is_position_occupied = [&](int x, int y, int width, int length)
        {
            for (const auto& p : buffer_placements)
            {
                int px = std::get<0>(p);
                int py = std::get<1>(p);
                int pwidth = std::get<3>(p);
                int plength = std::get<4>(p);

                if (!(x + width <= px || px + pwidth <= x || y + length <= py || py + plength <= y))
                {
                    return true;
                }
            }
            return false;
        };

        for (const auto& box : boxes)
        {
            std::vector<int> box_sizes = parseBoxSize(box.at("box_size"));
            if (box_sizes.size() < 3) continue;

            int width = box_sizes[0];
            int length = box_sizes[1];
            bool placed = false;
            const int z = 0;

            for (int y = 0; y <= pallet_length - length; y += stacking_interval)
            {
                if (placed)
                    break;
                
                for (int x = 0; x <= pallet_width - width; x += stacking_interval)
                {
                    if (!is_position_occupied(x, y, width + stacking_interval, length + stacking_interval))
                    {
                        buffer_placements.push_back(std::make_tuple(
                            x, y, z,
                            width + stacking_interval,
                            length + stacking_interval,
                            box_sizes[2] + stacking_interval
                        ));

                        out_placements.push_back({
                            box.at("box_id"),
                            std::make_tuple(x + std::ceil(width/2.0), y + std::ceil(length/2.0), z),
                            0,
                            1
                        });

                        placed = true;
                        break;
                    }
                }
            }
        }
        return out_placements;
    }

    std::vector<StackResult> stack_with_buffer()
    {
        // 먼저 버퍼 팔레트에 최대한 많이 배치
        for (const auto& box : boxes)
        {
            if (used_boxes.find(box.at("box_id")) != used_boxes.end())
                continue;

            if (buffer_count < MAX_BUFFER_COUNT && try_place_in_buffer(box))
                continue;
                
            try_place_in_main(box);
        }

        // 버퍼에서 메인으로 이동 가능한 박스들 이동
        while (move_best_fit_from_buffer_to_main())
        {}

        return final_placements;
    }

    std::vector<StackResult> optimized_stack()
    {
        std::vector<StackResult> results;
        
        // 부피 기준 정렬
        std::vector<std::pair<std::string, std::vector<int>>> sorted_boxes;
        for (const auto& box : boxes)
        {
            auto size = parseBoxSize(box.at("box_size"));
            sorted_boxes.push_back({box.at("box_id"), size});
        }
        
        std::sort(sorted_boxes.begin(), sorted_boxes.end(),
            [](const auto& a, const auto& b) {
                auto vol_a = a.second[0] * a.second[1] * a.second[2];
                auto vol_b = b.second[0] * b.second[1] * b.second[2];
                return vol_a > vol_b;
            });

        placement_manager = std::make_unique<BoxPlacement>(pallet_size);
        
        for (const auto& [box_id, box_size] : sorted_boxes)
        {
            std::tuple<int, int, int> position;
            int rotation;
            
            if (tryPlaceBox(box_size, position, rotation))
            {
                results.push_back({
                    box_id,
                    std::make_tuple(
                        std::get<0>(position) + std::ceil(box_size[0]/2.0),
                        std::get<1>(position) + std::ceil(box_size[1]/2.0),
                        std::get<2>(position)
                    ),
                    rotation,
                    1
                });
            }
        }

        return results;
    }

    std::vector<StackResult> Stack(StackingMethod stacking_method)
    {
        switch (stacking_method)
        {
            case StackingMethod::PALLET_ORIGIN_OUT_OF_BOUND:
                return stack_pallet_origin_out_of_bound();
            case StackingMethod::PALLET_STACK_ALL:
                return stack_all_boxes();
            case StackingMethod::BUFFER:
                return stack_buffer();
            case StackingMethod::STACK_WITH_BUFFER:
                return stack_with_buffer();
            case StackingMethod::OPTIMIZED_STACK:
                return optimized_stack();
            default:
                throw std::invalid_argument("Invalid stacking method");
        }
    }
};

#endif
