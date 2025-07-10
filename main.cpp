#include <iostream>
#include <filesystem>
#include <vector>
#include <exception>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include "stacking_algorithm.hpp"
#include "stackingVisualizer.hpp"

int main()
{
    // 데이터 디렉토리 설정
    std::filesystem::path data_dir("sample_json");
    try {
        std::filesystem::create_directories(data_dir);
        std::cout << "Created data directory: " << std::filesystem::absolute(data_dir) << std::endl;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error creating directory: " << e.what() << std::endl;
        return 1;
    }
/*
    // 랜덤 박스 생성
    int num_small_boxes = 75, num_large_boxes = 5;
    auto boxes_small = BoxGenerator::small_generate_boxes(num_small_boxes, {100, 500}, {100, 500}, {100, 500});
    auto boxes_large = BoxGenerator::large_generate_boxes(num_large_boxes, {500, 700}, {500, 700}, {500, 700});
    
    std::vector<nlohmann::json> boxes;
    boxes.insert(boxes.end(), boxes_small.begin(), boxes_small.end());
    boxes.insert(boxes.end(), boxes_large.begin(), boxes_large.end());
    
    // JSON 파일 저장 및 로드
    auto boxes_path = data_dir / "random_boxes.json";
    JsonUtils::save_to_json(boxes, boxes_path.string());
*/
    auto boxes_path = data_dir / "random_boxes.json";
    auto loaded_boxes = JsonUtils::load_from_json(boxes_path.string());

    // 적재 공간 크기 설정
    std::vector<double> cubic_range = {1100, 1100, 1800};

    // 박스 데이터 형식 변환
    std::vector<std::unordered_map<std::string, std::string>> boxesMap;
    for (const auto& box : loaded_boxes)
    {
        std::unordered_map<std::string, std::string> boxMap;
        boxMap["box_id"] = std::to_string(box["box_id"].get<int>());
        boxMap["box_size"] = box["box_size"].dump();
        boxesMap.push_back(boxMap);
    }

    // 여러 적재 방식 테스트를 위한 함수
    auto optmz_test_stacking_method = [&](StackingMethod method, const std::string& method_name) {
        std::cout << "\nTesting " << method_name << "..." << std::endl;
        
        // 새로운 알고리즘 인스턴스 생성하여 독립성 보장
        StackingAlgorithm fresh_algorithm(boxesMap, {
            static_cast<int>(std::round(cubic_range[0])),
            static_cast<int>(std::round(cubic_range[1])),
            static_cast<int>(std::round(cubic_range[2]))
        });
        
        // 적재 수행
        std::vector<nlohmann::json> placements;
        for (const auto& result : fresh_algorithm.Stack(method))
        {
            nlohmann::json placement;
            placement["box_id"] = std::stoi(result.box_id);
            placement["box_loc"] = {
                std::get<0>(result.box_loc), 
                std::get<1>(result.box_loc), 
                std::get<2>(result.box_loc)
            };
            placement["box_rot"] = result.box_rot;
            placement["pallet_id"] = result.pallet_id;
            placements.push_back(placement);
        }

        // 결과 저장
        auto result_path = data_dir / (method_name + "_result.json");
        JsonUtils::save_to_json(placements, result_path.string());

        // 결과 시각화 디렉토리 생성
        std::filesystem::path result_folder("results_" + method_name);
        try {
            if (std::filesystem::exists(result_folder))
            {
                std::filesystem::remove_all(result_folder);
            }
            std::filesystem::create_directories(result_folder);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating result directory: " << e.what() << std::endl;
            return;
        }

        std::filesystem::path result_folder2("2results_" + method_name);
        try {
            if (std::filesystem::exists(result_folder2))
            {
                std::filesystem::remove_all(result_folder2);
            }
            std::filesystem::create_directories(result_folder2);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating result directory: " << e.what() << std::endl;
            return;
        }

        std::filesystem::path result_folder_1600("1600_results_" + method_name);
        try {
            if (std::filesystem::exists(result_folder_1600))
            {
                std::filesystem::remove_all(result_folder_1600);
            }
            std::filesystem::create_directories(result_folder_1600);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating result directory: " << e.what() << std::endl;
            return;
        }

        // 결과 시각화 및 검증
        std::cout << "Starting visualization process for " << method_name << "..." << std::endl;
        auto [stacking_rate, stacking_number] = StackingVisualizer::optimized_stack_check_and_visualize(
            placements, 
            loaded_boxes, 
            cubic_range, 
            result_folder.string(), 
            method_name + "_animation"
        );

        StackingVisualizer::optimized_stack_check_and_visualize_1600(
            placements, 
            loaded_boxes, 
            cubic_range, 
            result_folder_1600.string(), 
            method_name + "_animation"
        );

        StackingVisualizer::optimized_stack_check_and_visualize_xyz(
            placements, 
            loaded_boxes, 
            cubic_range, 
            result_folder2.string(), 
            method_name + "_animation"
        );

        // 통계 계산 및 출력
        int main_count = 0, buffer_count = 0;
        for (const auto& placement : placements) {
            if (placement["pallet_id"].get<int>() == 1)
            {
                main_count++;
            }
            else if (placement["pallet_id"].get<int>() == 2)
            {
                buffer_count++;
            }
        }

        // 결과 출력
        std::cout << "\n" << method_name << " Results:" << std::endl;
        std::cout << "--------------------" << std::endl;
        std::cout << "Main Pallet Boxes: " << main_count << std::endl;
        std::cout << "Buffer Pallet Boxes: " << buffer_count << std::endl;
        std::cout << "Stacking rate: " << stacking_rate << "%" << std::endl;
        std::cout << "Number of stacked boxes: " << stacking_number << std::endl;
        std::cout << "--------------------" << std::endl;
    };

    auto live_test_stacking_method = [&](StackingMethod method, const std::string& method_name) {
        std::cout << "\nTesting " << method_name << "..." << std::endl;
        
        // 새로운 알고리즘 인스턴스 생성하여 독립성 보장
        StackingAlgorithm fresh_algorithm(boxesMap, {
            static_cast<int>(std::round(cubic_range[0])),
            static_cast<int>(std::round(cubic_range[1])),
            static_cast<int>(std::round(cubic_range[2]))
        });
        
        // 적재 수행
        std::vector<nlohmann::json> placements;
        for (const auto& result : fresh_algorithm.Stack(method))
        {
            nlohmann::json placement;
            placement["box_id"] = std::stoi(result.box_id);
            placement["box_loc"] = {
                std::get<0>(result.box_loc), 
                std::get<1>(result.box_loc), 
                std::get<2>(result.box_loc)
            };
            placement["box_rot"] = result.box_rot;
            placement["pallet_id"] = result.pallet_id;
            placements.push_back(placement);
        }

        // 결과 저장
        auto result_path = data_dir / (method_name + "_result.json");
        JsonUtils::save_to_json(placements, result_path.string());

        // 결과 시각화 디렉토리 생성
        std::filesystem::path result_folder("results_" + method_name);
        try {
            if (std::filesystem::exists(result_folder))
            {
                std::filesystem::remove_all(result_folder);
            }
            std::filesystem::create_directories(result_folder);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating result directory: " << e.what() << std::endl;
            return;
        }

        std::filesystem::path result_folder2("2results_" + method_name);
        try {
            if (std::filesystem::exists(result_folder2))
            {
                std::filesystem::remove_all(result_folder2);
            }
            std::filesystem::create_directories(result_folder2);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating result directory: " << e.what() << std::endl;
            return;
        }

        std::filesystem::path result_folder_1600("1600_results_" + method_name);
        try {
            if (std::filesystem::exists(result_folder_1600))
            {
                std::filesystem::remove_all(result_folder_1600);
            }
            std::filesystem::create_directories(result_folder_1600);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating result directory: " << e.what() << std::endl;
            return;
        }

        // 결과 시각화 및 검증
        std::cout << "Starting visualization process for " << method_name << "..." << std::endl;
        auto [stacking_rate, stacking_number] = StackingVisualizer::stack_all_box_check_and_visualize(
            placements, 
            loaded_boxes, 
            cubic_range, 
            result_folder.string(), 
            method_name + "_animation"
        );

        StackingVisualizer::stack_all_box_check_and_visualize_1600(
            placements, 
            loaded_boxes, 
            cubic_range, 
            result_folder_1600.string(), 
            method_name + "_animation"
        );

        StackingVisualizer::stack_all_box_check_and_visualize_xyz(
            placements, 
            loaded_boxes, 
            cubic_range, 
            result_folder2.string(), 
            method_name + "_animation"
        );

        // 통계 계산 및 출력
        int main_count = 0, buffer_count = 0;
        for (const auto& placement : placements)
        {
            if (placement["pallet_id"].get<int>() == 1)
            {
                main_count++;
            }
            else if (placement["pallet_id"].get<int>() == 2)
            {
                buffer_count++;
            }
        }

        // 결과 출력
        std::cout << "\n" << method_name << " Results:" << std::endl;
        std::cout << "--------------------" << std::endl;
        std::cout << "Main Pallet Boxes: " << main_count << std::endl;
        std::cout << "Buffer Pallet Boxes: " << buffer_count << std::endl;
        std::cout << "Stacking rate: " << stacking_rate << "%" << std::endl;
        std::cout << "Number of stacked boxes: " << stacking_number << std::endl;
        std::cout << "--------------------" << std::endl;
    };

    auto buffer_test_stacking_method = [&](StackingMethod method, const std::string& method_name) {
        std::cout << "\nTesting " << method_name << "..." << std::endl;
        
        // 새로운 알고리즘 인스턴스 생성하여 독립성 보장
        StackingAlgorithm fresh_algorithm(boxesMap, {
            static_cast<int>(std::round(cubic_range[0])),
            static_cast<int>(std::round(cubic_range[1])),
            static_cast<int>(std::round(cubic_range[2]))
        });
        
        // 적재 수행
        std::vector<nlohmann::json> placements;
        for (const auto& result : fresh_algorithm.Stack(method))
        {
            nlohmann::json placement;
            placement["box_id"] = std::stoi(result.box_id);
            placement["box_loc"] = {
                std::get<0>(result.box_loc), 
                std::get<1>(result.box_loc), 
                std::get<2>(result.box_loc)
            };
            placement["box_rot"] = result.box_rot;
            placement["pallet_id"] = result.pallet_id;
            placements.push_back(placement);
        }

        // 결과 저장
        auto result_path = data_dir / (method_name + "_result.json");
        JsonUtils::save_to_json(placements, result_path.string());

        // 결과 시각화 디렉토리 생성
        std::filesystem::path result_folder("results_" + method_name);
        try {
            if (std::filesystem::exists(result_folder))
            {
                std::filesystem::remove_all(result_folder);
            }
            std::filesystem::create_directories(result_folder);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating result directory: " << e.what() << std::endl;
            return;
        }

        std::filesystem::path result_folder2("2results_" + method_name);
        try {
            if (std::filesystem::exists(result_folder2))
            {
                std::filesystem::remove_all(result_folder2);
            }
            std::filesystem::create_directories(result_folder2);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating result directory: " << e.what() << std::endl;
            return;
        }

        // 결과 시각화 및 검증
        std::cout << "Starting visualization process for " << method_name << "..." << std::endl;
        auto [stacking_rate, stacking_number] = StackingVisualizer::stack_with_buffer_check_and_visualize(
            placements, 
            loaded_boxes, 
            cubic_range, 
            result_folder.string(), 
            method_name + "_animation"
        );

        StackingVisualizer::stack_with_buffer_check_and_visualize_xyz(
            placements, 
            loaded_boxes, 
            cubic_range, 
            result_folder2.string(), 
            method_name + "_animation"
        );

        // 통계 계산 및 출력
        int main_count = 0, buffer_count = 0;
        for (const auto& placement : placements) {
            if (placement["pallet_id"].get<int>() == 1)
            {
                main_count++;
            } else if (placement["pallet_id"].get<int>() == 2)
            {
                buffer_count++;
            }
        }

        // 결과 출력
        std::cout << "\n" << method_name << " Results:" << std::endl;
        std::cout << "--------------------" << std::endl;
        std::cout << "Main Pallet Boxes: " << main_count << std::endl;
        std::cout << "Buffer Pallet Boxes: " << buffer_count << std::endl;
        std::cout << "Stacking rate: " << stacking_rate << "%" << std::endl;
        std::cout << "Number of stacked boxes: " << stacking_number << std::endl;
        std::cout << "--------------------" << std::endl;
    };

    // 여러 적재 방식 테스트
    optmz_test_stacking_method(StackingMethod::OPTIMIZED_STACK, "optimized_stack");
    buffer_test_stacking_method(StackingMethod::STACK_WITH_BUFFER, "stack_with_buffer");
    live_test_stacking_method(StackingMethod::PALLET_STACK_ALL, "stack_all_boxes");

    return 0;
}
