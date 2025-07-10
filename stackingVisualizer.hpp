#ifndef _STACKING_VISUALIZER
#define _STACKING_VISUALIZER

#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>

#include <nlohmann/json.hpp>
#include <gnuplot-iostream.h>
#include <opencv2/opencv.hpp>
#include <gif_lib.h>

#include "geometryUtils.hpp"
#include "visualizationUtils.hpp"

class StackingVisualizer {
public:
    static std::pair<double, int> optimized_stack_check_and_visualize(
        const std::vector<nlohmann::json>& placements,
        const std::vector<nlohmann::json>& boxes,
        const std::vector<double>& cubic_range,
        const std::string& result_folder_name = "./results",
        const std::string& result_file_name = "stacking_animation")
    {
        // 결과 디렉토리 초기화
        std::filesystem::path result_path(result_folder_name);
        try {
            if (std::filesystem::exists(result_path))
            {
                std::filesystem::remove_all(result_path);
            }
            std::filesystem::create_directories(result_path);
            std::cout << "Created result directory: " << std::filesystem::absolute(result_path) << std::endl;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error handling result directory: " << e.what() << std::endl;
            return {0.0, 0};
        }

        double total_volume = 0;
        int stacking_number = 0;
        std::vector<double> stacking_rates;
        std::vector<std::string> frame_filenames;
        bool is_valid = true;

        // 각 프레임별 상태를 추적하기 위한 맵
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> main_states;
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> buffer_states;

        // Initialize Gnuplot
        Gnuplot gp;
        gp << "set terminal pngcairo size 1600,800 enhanced font 'Verdana,10'\n";
        
        // 먼저 모든 placement를 순서대로 처리하여 상태 맵 구성
        for (const auto& place_box : placements)
        {
            int box_id = place_box["box_id"].get<int>();
            int pallet_id = place_box["pallet_id"].get<int>();
            
            auto box_it = std::find_if(boxes.begin(), boxes.end(),
                          [box_id](const nlohmann::json& box) {
                return box["box_id"].get<int>() == box_id;
            });
            
            if (box_it == boxes.end())
            {
                std::cerr << "Box with ID " << box_id << " not found." << std::endl;
                is_valid = false;
                continue;
            }

            double width = (*box_it)["box_size"][0].get<double>();
            double length = (*box_it)["box_size"][1].get<double>();
            double height = (*box_it)["box_size"][2].get<double>();
            double angle = place_box["box_rot"].get<double>();
            double x_center = place_box["box_loc"][0].get<double>();
            double y_center = place_box["box_loc"][1].get<double>();
            double z_center = place_box["box_loc"][2].get<double>();

            auto rotated_corners = GeometryUtils::rotate_box_corners(x_center, y_center, width, length, angle);
            auto placement_tuple = std::make_tuple(rotated_corners, z_center, height);

            // 현재 프레임까지의 모든 상태 복사
            if (!main_states.empty())
            {
                main_states[stacking_number + 1] = main_states[stacking_number];
            }
            if (!buffer_states.empty())
            {
                buffer_states[stacking_number + 1] = buffer_states[stacking_number];
            }

            if (pallet_id == 1)
            {
                // 메인 팔렛트에 배치
                if (main_states.find(stacking_number + 1) == main_states.end())
                {
                    main_states[stacking_number + 1] = {};
                }
                main_states[stacking_number + 1].push_back(placement_tuple);
                total_volume += width * length * height;

                // 버퍼에서 이동한 경우, 버퍼에서 제거
                if (!buffer_states.empty())
                {
                    auto& last_buffer_state = buffer_states[stacking_number];
                    auto it = std::find_if(last_buffer_state.begin(), last_buffer_state.end(),
                        [&](const auto& state) {
                            const auto& corners = std::get<0>(state);
                            const double& state_z = std::get<1>(state);
                            const double& state_height = std::get<2>(state);
                            
                            // 모든 모서리 좌표, z 위치, 높이를 비교
                            bool corners_match = true;
                            for (size_t i = 0; i < corners.size(); ++i)
                            {
                                if (std::abs(corners[i][0] - rotated_corners[i][0]) > 1e-6 || std::abs(corners[i][1] - rotated_corners[i][1]) > 1e-6)
                                {
                                    corners_match = false;
                                    break;
                                }
                            }
                            return corners_match && std::abs(state_z - z_center) < 1e-6 && std::abs(state_height - height) < 1e-6;
                        });

                    if (it != last_buffer_state.end())
                    {
                        last_buffer_state.erase(it);
                        if (buffer_states.find(stacking_number + 1) != buffer_states.end())
                        {
                            buffer_states[stacking_number + 1] = last_buffer_state;
                        }
                    }
                }
                double stacking_rate = (total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2])) * 100;
                stacking_rates.push_back(stacking_rate);
            }
            else if (pallet_id == 2)
            {
                // 버퍼 팔렛트에 배치
                if (buffer_states.find(stacking_number + 1) == buffer_states.end())
                {
                    buffer_states[stacking_number + 1] = {};
                }
                buffer_states[stacking_number + 1].push_back(placement_tuple);
            }

            stacking_number++;
            if (!is_valid)
                break;
        }

        // 프레임 생성
        for (int frame = 1; frame <= stacking_number; frame++)
        {
            std::string frame_filename;

            if (frame < 10)
            {
                frame_filename = (result_path / ("frame_0" + std::to_string(frame) + ".png")).string();
            }
            else
            {
                frame_filename = (result_path / ("frame_" + std::to_string(frame) + ".png")).string();
            }
            
            // Set output for this frame
            gp << "set output '" << frame_filename << "'\n";
            
            // Setup multiplot
            gp << "set multiplot layout 2,2 spacing 0.1\n";

            // Current states
            const auto& current_main = main_states[frame];

            // Generate all views
            generate_view_800(gp, current_main, cubic_range, "Stack optimized: Main Pallet 60, 30", 60, 30, 0.0, 0);
            generate_view_800(gp, current_main, cubic_range, "Stack Optimized: Main Pallet 30, 60", 30, 60, 0.5, 0);

            gp << "unset multiplot\n";
            gp.flush();

            // Wait for frame to be created
            if (wait_for_frame(frame_filename))
            {
                frame_filenames.push_back(frame_filename);
                std::cout << "Created frame " << frame << "/" << stacking_number << std::endl;
            }
        }

        // Create GIF if we have frames
        if (!frame_filenames.empty())
        {
            std::string gif_filename = (result_path / (result_file_name + ".gif")).string();
            std::cout << "\nCreating GIF: " << gif_filename << std::endl;
            VisualizationUtils::create_gif(frame_filenames, gif_filename);
            std::cout << "GIF creation completed" << std::endl;
        }
        else
        {
            std::cerr << "No frames were generated for GIF creation" << std::endl;
        }

        // stacking rate graph
        std::string graph_file = (result_path / "stacking_rate_graph_optmz.png").string();
        
        gp << "set terminal pngcairo size 800, 800 enhanced font 'Verdana, 12'\n";
        gp << "set output '" << graph_file << "'\n";
        gp << "set title 'Stacking Rate Over Frames'\n";
        gp << "set xlabel 'Frame'\n";
        gp << "set xrange [0:" << stacking_number << "]\n";
        gp << "set ylabel 'Stacking Rate (%)'\n";
        gp << "set yrange [0:100]\n";
        gp << "set grid\n";
        gp << "plot '-' with linespoints title 'Stacking Rate (%)' lc rgb 'blue'\n";

        for (size_t i = 0; i < stacking_rates.size(); ++i)
        {
            gp << i + 1 << " " << stacking_rates[i] << "\n";
        }
        gp << "e\n";
        gp.flush();

        std::cout << "Stacking rate graph saved to: " << graph_file << std::endl;

        return {total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2]) * 100, stacking_number};
    }

    static std::pair<double, int> optimized_stack_check_and_visualize_1600(
        const std::vector<nlohmann::json>& placements,
        const std::vector<nlohmann::json>& boxes,
        const std::vector<double>& cubic_range,
        const std::string& result_folder_name = "./results",
        const std::string& result_file_name = "stacking_animation")
    {
        // 결과 디렉토리 초기화
        std::filesystem::path result_path(result_folder_name);
        try {
            if (std::filesystem::exists(result_path))
            {
                std::filesystem::remove_all(result_path);
            }
            std::filesystem::create_directories(result_path);
            std::cout << "Created result directory: " << std::filesystem::absolute(result_path) << std::endl;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error handling result directory: " << e.what() << std::endl;
            return {0.0, 0};
        }

        double total_volume = 0;
        int stacking_number = 0;
        std::vector<double> stacking_rates;
        std::vector<std::string> frame_filenames;
        bool is_valid = true;

        // 각 프레임별 상태를 추적하기 위한 맵
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> main_states;
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> buffer_states;

        // Initialize Gnuplot
        Gnuplot gp;
        gp << "set terminal pngcairo size 1600,1600 enhanced font 'Verdana,10'\n";
        
        // 먼저 모든 placement를 순서대로 처리하여 상태 맵 구성
        for (const auto& place_box : placements)
        {
            int box_id = place_box["box_id"].get<int>();
            int pallet_id = place_box["pallet_id"].get<int>();
            
            auto box_it = std::find_if(boxes.begin(), boxes.end(),
                          [box_id](const nlohmann::json& box) {
                return box["box_id"].get<int>() == box_id;
            });
            
            if (box_it == boxes.end())
            {
                std::cerr << "Box with ID " << box_id << " not found." << std::endl;
                is_valid = false;
                continue;
            }

            double width = (*box_it)["box_size"][0].get<double>();
            double length = (*box_it)["box_size"][1].get<double>();
            double height = (*box_it)["box_size"][2].get<double>();
            double angle = place_box["box_rot"].get<double>();
            double x_center = place_box["box_loc"][0].get<double>();
            double y_center = place_box["box_loc"][1].get<double>();
            double z_center = place_box["box_loc"][2].get<double>();

            auto rotated_corners = GeometryUtils::rotate_box_corners(x_center, y_center, width, length, angle);
            auto placement_tuple = std::make_tuple(rotated_corners, z_center, height);

            // 현재 프레임까지의 모든 상태 복사
            if (!main_states.empty())
            {
                main_states[stacking_number + 1] = main_states[stacking_number];
            }
            if (!buffer_states.empty())
            {
                buffer_states[stacking_number + 1] = buffer_states[stacking_number];
            }

            if (pallet_id == 1)
            {
                // 메인 팔렛트에 배치
                if (main_states.find(stacking_number + 1) == main_states.end())
                {
                    main_states[stacking_number + 1] = {};
                }
                main_states[stacking_number + 1].push_back(placement_tuple);
                total_volume += width * length * height;

                // 버퍼에서 이동한 경우, 버퍼에서 제거
                if (!buffer_states.empty())
                {
                    auto& last_buffer_state = buffer_states[stacking_number];
                    auto it = std::find_if(last_buffer_state.begin(), last_buffer_state.end(),
                        [&](const auto& state) {
                            const auto& corners = std::get<0>(state);
                            const double& state_z = std::get<1>(state);
                            const double& state_height = std::get<2>(state);
                            
                            // 모든 모서리 좌표, z 위치, 높이를 비교
                            bool corners_match = true;
                            for (size_t i = 0; i < corners.size(); ++i)
                            {
                                if (std::abs(corners[i][0] - rotated_corners[i][0]) > 1e-6 ||
                                    std::abs(corners[i][1] - rotated_corners[i][1]) > 1e-6) {
                                    corners_match = false;
                                    break;
                                }
                            }
                            return corners_match && 
                                std::abs(state_z - z_center) < 1e-6 && 
                                std::abs(state_height - height) < 1e-6;
                        });

                    if (it != last_buffer_state.end())
                    {
                        last_buffer_state.erase(it);
                        if (buffer_states.find(stacking_number + 1) != buffer_states.end())
                        {
                            buffer_states[stacking_number + 1] = last_buffer_state;
                        }
                    }
                }
                double stacking_rate = (total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2])) * 100;
                stacking_rates.push_back(stacking_rate);
            }
            else if (pallet_id == 2)
            {
                // 버퍼 팔렛트에 배치
                if (buffer_states.find(stacking_number + 1) == buffer_states.end())
                {
                    buffer_states[stacking_number + 1] = {};
                }
                buffer_states[stacking_number + 1].push_back(placement_tuple);
            }

            stacking_number++;
            if (!is_valid)
                break;
        }

        // 프레임 생성
        for (int frame = 1; frame <= stacking_number; frame++)
        {
            std::string frame_filename;

            if (frame < 10)
            {
                frame_filename = (result_path / ("frame_0" + std::to_string(frame) + ".png")).string();
            }
            else
            {
                frame_filename = (result_path / ("frame_" + std::to_string(frame) + ".png")).string();
            }
            
            // Set output for this frame
            gp << "set output '" << frame_filename << "'\n";
            
            // Setup multiplot
            gp << "set multiplot layout 2,2 spacing 0.1\n";

            // Current states
            const auto& current_main = main_states[frame];
            const auto& current_buffer = buffer_states[frame];

            // Generate all views
            generate_view_1600(gp, current_buffer, cubic_range, "Stack optimized: Buffer Pallet 60, 30", 60, 30, 0.0, 0);
            generate_view_1600(gp, current_buffer, cubic_range, "Stack Optimized: Buffer Pallet 30, 60", 30, 60, 0.5, 0);
            generate_view_1600(gp, current_main, cubic_range, "Stack optimized: Main Pallet 60, 30", 60, 30, 0.0, 0.5);
            generate_view_1600(gp, current_main, cubic_range, "Stack Optimized: Main Pallet 30, 60", 30, 60, 0.5, 0.5);

            gp << "unset multiplot\n";
            gp.flush();

            // Wait for frame to be created
            if (wait_for_frame(frame_filename))
            {
                frame_filenames.push_back(frame_filename);
                std::cout << "Created frame " << frame << "/" << stacking_number << std::endl;
            }
        }

        // Create GIF if we have frames
        if (!frame_filenames.empty())
        {
            std::string gif_filename = (result_path / (result_file_name + ".gif")).string();
            std::cout << "\nCreating GIF: " << gif_filename << std::endl;
            VisualizationUtils::create_gif(frame_filenames, gif_filename);
            std::cout << "GIF creation completed" << std::endl;
        }
        else
        {
            std::cerr << "No frames were generated for GIF creation" << std::endl;
        }

        return {total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2]) * 100, stacking_number};
    }

    static std::pair<double, int> optimized_stack_check_and_visualize_xyz(
        const std::vector<nlohmann::json>& placements,
        const std::vector<nlohmann::json>& boxes,
        const std::vector<double>& cubic_range,
        const std::string& result_folder_name = "./results",
        const std::string& result_file_name = "stacking_animation")
    {
        // 결과 디렉토리 초기화
        std::filesystem::path result_path(result_folder_name);
        try {
            if (std::filesystem::exists(result_path))
            {
                std::filesystem::remove_all(result_path);
            }
            std::filesystem::create_directories(result_path);
            std::cout << "Created result directory: " << std::filesystem::absolute(result_path) << std::endl;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error handling result directory: " << e.what() << std::endl;
            return {0.0, 0};
        }

        double total_volume = 0;
        int stacking_number = 0;
        std::vector<double> stacking_rates;
        std::vector<std::string> frame_filenames;
        bool is_valid = true;

        // 각 프레임별 상태를 추적하기 위한 맵
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> main_states;
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> buffer_states;

        // Initialize Gnuplot
        Gnuplot gp;
        gp << "set terminal pngcairo size 1600,800 enhanced font 'Verdana,10'\n";
        
        // 먼저 모든 placement를 순서대로 처리하여 상태 맵 구성
        for (const auto& place_box : placements)
        {
            int box_id = place_box["box_id"].get<int>();
            int pallet_id = place_box["pallet_id"].get<int>();
            
            auto box_it = std::find_if(boxes.begin(), boxes.end(),
                          [box_id](const nlohmann::json& box) {
                return box["box_id"].get<int>() == box_id;
            });
            
            if (box_it == boxes.end())
            {
                std::cerr << "Box with ID " << box_id << " not found." << std::endl;
                is_valid = false;
                continue;
            }

            double width = (*box_it)["box_size"][0].get<double>();
            double length = (*box_it)["box_size"][1].get<double>();
            double height = (*box_it)["box_size"][2].get<double>();
            double angle = place_box["box_rot"].get<double>();
            double x_center = place_box["box_loc"][0].get<double>();
            double y_center = place_box["box_loc"][1].get<double>();
            double z_center = place_box["box_loc"][2].get<double>();

            auto rotated_corners = GeometryUtils::rotate_box_corners(x_center, y_center, width, length, angle);
            auto placement_tuple = std::make_tuple(rotated_corners, z_center, height);

            // 현재 프레임까지의 모든 상태 복사
            if (!main_states.empty())
            {
                main_states[stacking_number + 1] = main_states[stacking_number];
            }
            if (!buffer_states.empty())
            {
                buffer_states[stacking_number + 1] = buffer_states[stacking_number];
            }

            if (pallet_id == 1)
            {
                // 메인 팔렛트에 배치
                if (main_states.find(stacking_number + 1) == main_states.end())
                {
                    main_states[stacking_number + 1] = {};
                }
                main_states[stacking_number + 1].push_back(placement_tuple);
                total_volume += width * length * height;

                // 버퍼에서 이동한 경우, 버퍼에서 제거
                if (!buffer_states.empty())
                {
                    auto& last_buffer_state = buffer_states[stacking_number];
                    auto it = std::find_if(last_buffer_state.begin(), last_buffer_state.end(),
                        [&](const auto& state) {
                            const auto& corners = std::get<0>(state);
                            return std::abs(corners[0][0] - rotated_corners[0][0]) < 1e-6 &&
                                   std::abs(corners[0][1] - rotated_corners[0][1]) < 1e-6;
                        });
                    if (it != last_buffer_state.end())
                    {
                        last_buffer_state.erase(it);
                        if (buffer_states.find(stacking_number + 1) != buffer_states.end())
                        {
                            buffer_states[stacking_number + 1] = last_buffer_state;
                        }
                    }
                }
                double stacking_rate = (total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2])) * 100;
                stacking_rates.push_back(stacking_rate);
            }
            else if (pallet_id == 2)
            {
                // 버퍼 팔렛트에 배치
                if (buffer_states.find(stacking_number + 1) == buffer_states.end())
                {
                    buffer_states[stacking_number + 1] = {};
                }
                buffer_states[stacking_number + 1].push_back(placement_tuple);
            }

            stacking_number++;
            if (!is_valid)
                break;
        }

        // 프레임 생성
        for (int frame = 1; frame <= stacking_number; frame++)
        {
            std::string frame_filename;

            if (frame < 10)
            {
                frame_filename = (result_path / ("frame_0" + std::to_string(frame) + ".png")).string();
            }
            else
            {
                frame_filename = (result_path / ("frame_" + std::to_string(frame) + ".png")).string();
            }
            
            // Set output for this frame
            gp << "set output '" << frame_filename << "'\n";
            
            // Setup multiplot
            gp << "set multiplot layout 2,2 spacing 0.1\n";

            // Current states
            const auto& current_main = main_states[frame];

            // Generate all views
            generate_view_800(gp, current_main, cubic_range, "Stack optimized: Main Pallet x, z", 90, 0, 0.0, 0);
            generate_view_800(gp, current_main, cubic_range, "Stack Optimized: Main Pallet x, y", 0, 90, 0.5, 0);

            gp << "unset multiplot\n";
            gp.flush();

            // Wait for frame to be created
            if (wait_for_frame(frame_filename))
            {
                frame_filenames.push_back(frame_filename);
                std::cout << "Created frame " << frame << "/" << stacking_number << std::endl;
            }
        }

        // Create GIF if we have frames
        if (!frame_filenames.empty())
        {
            std::string gif_filename = (result_path / (result_file_name + ".gif")).string();
            std::cout << "\nCreating GIF: " << gif_filename << std::endl;
            VisualizationUtils::create_gif(frame_filenames, gif_filename);
            std::cout << "GIF creation completed" << std::endl;
        }
        else
        {
            std::cerr << "No frames were generated for GIF creation" << std::endl;
        }

        return {total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2]) * 100, stacking_number};
    }

    static std::pair<double, int> stack_all_box_check_and_visualize(
        const std::vector<nlohmann::json>& placements,
        const std::vector<nlohmann::json>& boxes,
        const std::vector<double>& cubic_range,
        const std::string& result_folder_name = "./results",
        const std::string& result_file_name = "stacking_animation")
    {
        // 결과 디렉토리 초기화
        std::filesystem::path result_path(result_folder_name);
        try {
            if (std::filesystem::exists(result_path))
            {
                std::filesystem::remove_all(result_path);
            }
            std::filesystem::create_directories(result_path);
            std::cout << "Created result directory: " << std::filesystem::absolute(result_path) << std::endl;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error handling result directory: " << e.what() << std::endl;
            return {0.0, 0};
        }

        double total_volume = 0;
        int stacking_number = 0;
        std::vector<double> stacking_rates;
        std::vector<std::string> frame_filenames;
        bool is_valid = true;

        // 각 프레임별 상태를 추적하기 위한 맵
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> main_states;
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> buffer_states;

        // Initialize Gnuplot
        Gnuplot gp;
        gp << "set terminal pngcairo size 1600,800 enhanced font 'Verdana,10'\n";
        
        // 먼저 모든 placement를 순서대로 처리하여 상태 맵 구성
        for (const auto& place_box : placements)
        {
            int box_id = place_box["box_id"].get<int>();
            int pallet_id = place_box["pallet_id"].get<int>();
            
            auto box_it = std::find_if(boxes.begin(), boxes.end(),
                          [box_id](const nlohmann::json& box) {
                return box["box_id"].get<int>() == box_id;
            });
            
            if (box_it == boxes.end())
            {
                std::cerr << "Box with ID " << box_id << " not found." << std::endl;
                is_valid = false;
                continue;
            }

            double width = (*box_it)["box_size"][0].get<double>();
            double length = (*box_it)["box_size"][1].get<double>();
            double height = (*box_it)["box_size"][2].get<double>();
            double angle = place_box["box_rot"].get<double>();
            double x_center = place_box["box_loc"][0].get<double>();
            double y_center = place_box["box_loc"][1].get<double>();
            double z_center = place_box["box_loc"][2].get<double>();

            auto rotated_corners = GeometryUtils::rotate_box_corners(x_center, y_center, width, length, angle);
            auto placement_tuple = std::make_tuple(rotated_corners, z_center, height);

            // 현재 프레임까지의 모든 상태 복사
            if (!main_states.empty())
            {
                main_states[stacking_number + 1] = main_states[stacking_number];
            }
            if (!buffer_states.empty())
            {
                buffer_states[stacking_number + 1] = buffer_states[stacking_number];
            }

            if (pallet_id == 1)
            {
                // 메인 팔렛트에 배치
                if (main_states.find(stacking_number + 1) == main_states.end()) {
                    main_states[stacking_number + 1] = {};
                }
                main_states[stacking_number + 1].push_back(placement_tuple);
                total_volume += width * length * height;

                // 버퍼에서 이동한 경우, 버퍼에서 제거
                if (!buffer_states.empty())
                {
                    auto& last_buffer_state = buffer_states[stacking_number];
                    auto it = std::find_if(last_buffer_state.begin(), last_buffer_state.end(),
                        [&](const auto& state) {
                            const auto& corners = std::get<0>(state);
                            const double& state_z = std::get<1>(state);
                            const double& state_height = std::get<2>(state);
                            
                            // 모든 모서리 좌표, z 위치, 높이를 비교
                            bool corners_match = true;
                            for (size_t i = 0; i < corners.size(); ++i)
                            {
                                if (std::abs(corners[i][0] - rotated_corners[i][0]) > 1e-6 || std::abs(corners[i][1] - rotated_corners[i][1]) > 1e-6)
                                {
                                    corners_match = false;
                                    break;
                                }
                            }
                            return corners_match && std::abs(state_z - z_center) < 1e-6 && std::abs(state_height - height) < 1e-6;
                        });

                    if (it != last_buffer_state.end())
                    {
                        last_buffer_state.erase(it);
                        if (buffer_states.find(stacking_number + 1) != buffer_states.end())
                        {
                            buffer_states[stacking_number + 1] = last_buffer_state;
                        }
                    }
                }
                double stacking_rate = (total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2])) * 100;
                stacking_rates.push_back(stacking_rate);
            }
            else if (pallet_id == 2)
            {
                // 버퍼 팔렛트에 배치
                if (buffer_states.find(stacking_number + 1) == buffer_states.end())
                {
                    buffer_states[stacking_number + 1] = {};
                }
                buffer_states[stacking_number + 1].push_back(placement_tuple);
            }

            stacking_number++;
            if (!is_valid)
                break;
        }

        // 프레임 생성
        for (int frame = 1; frame <= stacking_number; frame++)
        {
            std::string frame_filename;
            
            if (frame < 10)
            {
                frame_filename = (result_path / ("frame_0" + std::to_string(frame) + ".png")).string();
            }
            else
            {
                frame_filename = (result_path / ("frame_" + std::to_string(frame) + ".png")).string();
            }
            
            // Set output for this frame
            gp << "set output '" << frame_filename << "'\n";
            
            // Setup multiplot
            gp << "set multiplot layout 2,2 spacing 0.1\n";

            // Current states
            const auto& current_main = main_states[frame];

            // Generate all views
            generate_view_800(gp, current_main, cubic_range, "Stack all Boxes: Main Pallet 60, 30", 60, 30, 0.0, 0);
            generate_view_800(gp, current_main, cubic_range, "Stack all Boxes: Main Pallet 30, 60", 30, 60, 0.5, 0);

            gp << "unset multiplot\n";
            gp.flush();

            // Wait for frame to be created
            if (wait_for_frame(frame_filename))
            {
                frame_filenames.push_back(frame_filename);
                std::cout << "Created frame " << frame << "/" << stacking_number << std::endl;
            }
        }

        // Create GIF if we have frames
        if (!frame_filenames.empty())
        {
            std::string gif_filename = (result_path / (result_file_name + ".gif")).string();
            std::cout << "\nCreating GIF: " << gif_filename << std::endl;
            VisualizationUtils::create_gif(frame_filenames, gif_filename);
            std::cout << "GIF creation completed" << std::endl;
        }
        else
        {
            std::cerr << "No frames were generated for GIF creation" << std::endl;
        }

        // stacking rate graph
        std::string graph_file = (result_path / "stacking_rate_graph_live.png").string();
        
        gp << "set terminal pngcairo size 800, 800 enhanced font 'Verdana, 12'\n";
        gp << "set output '" << graph_file << "'\n";
        gp << "set title 'Stacking Rate Over Frames'\n";
        gp << "set xlabel 'Frame'\n";
        gp << "set xrange [0:" << stacking_number << "]\n";
        gp << "set ylabel 'Stacking Rate (%)'\n";
        gp << "set yrange [0:100]\n";
        gp << "set grid\n";
        gp << "plot '-' with linespoints title 'Stacking Rate (%)' lc rgb 'blue'\n";

        for (size_t i = 0; i < stacking_rates.size(); ++i)
        {
            gp << i + 1 << " " << stacking_rates[i] << "\n";
        }
        gp << "e\n";
        gp.flush();

        std::cout << "Stacking rate graph saved to: " << graph_file << std::endl;

        return {total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2]) * 100, stacking_number};
    }

    static std::pair<double, int> stack_all_box_check_and_visualize_1600(
        const std::vector<nlohmann::json>& placements,
        const std::vector<nlohmann::json>& boxes,
        const std::vector<double>& cubic_range,
        const std::string& result_folder_name = "./results",
        const std::string& result_file_name = "stacking_animation")
    {
        // 결과 디렉토리 초기화
        std::filesystem::path result_path(result_folder_name);
        try {
            if (std::filesystem::exists(result_path))
            {
                std::filesystem::remove_all(result_path);
            }
            std::filesystem::create_directories(result_path);
            std::cout << "Created result directory: " << std::filesystem::absolute(result_path) << std::endl;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error handling result directory: " << e.what() << std::endl;
            return {0.0, 0};
        }

        double total_volume = 0;
        int stacking_number = 0;
        std::vector<double> stacking_rates;
        std::vector<std::string> frame_filenames;
        bool is_valid = true;

        // 각 프레임별 상태를 추적하기 위한 맵
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> main_states;
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> buffer_states;

        // Initialize Gnuplot
        Gnuplot gp;
        gp << "set terminal pngcairo size 1600,1600 enhanced font 'Verdana,10'\n";
        
        // 먼저 모든 placement를 순서대로 처리하여 상태 맵 구성
        for (const auto& place_box : placements)
        {
            int box_id = place_box["box_id"].get<int>();
            int pallet_id = place_box["pallet_id"].get<int>();
            
            auto box_it = std::find_if(boxes.begin(), boxes.end(),
                          [box_id](const nlohmann::json& box) {
                return box["box_id"].get<int>() == box_id;
            });
            
            if (box_it == boxes.end())
            {
                std::cerr << "Box with ID " << box_id << " not found." << std::endl;
                is_valid = false;
                continue;
            }

            double width = (*box_it)["box_size"][0].get<double>();
            double length = (*box_it)["box_size"][1].get<double>();
            double height = (*box_it)["box_size"][2].get<double>();
            double angle = place_box["box_rot"].get<double>();
            double x_center = place_box["box_loc"][0].get<double>();
            double y_center = place_box["box_loc"][1].get<double>();
            double z_center = place_box["box_loc"][2].get<double>();

            auto rotated_corners = GeometryUtils::rotate_box_corners(x_center, y_center, width, length, angle);
            auto placement_tuple = std::make_tuple(rotated_corners, z_center, height);

            // 현재 프레임까지의 모든 상태 복사
            if (!main_states.empty())
            {
                main_states[stacking_number + 1] = main_states[stacking_number];
            }
            if (!buffer_states.empty())
            {
                buffer_states[stacking_number + 1] = buffer_states[stacking_number];
            }

            if (pallet_id == 1)
            {
                // 메인 팔렛트에 배치
                if (main_states.find(stacking_number + 1) == main_states.end())
                {
                    main_states[stacking_number + 1] = {};
                }
                main_states[stacking_number + 1].push_back(placement_tuple);
                total_volume += width * length * height;

                // 버퍼에서 이동한 경우, 버퍼에서 제거
                if (!buffer_states.empty())
                {
                    auto& last_buffer_state = buffer_states[stacking_number];
                    auto it = std::find_if(last_buffer_state.begin(), last_buffer_state.end(),
                        [&](const auto& state) {
                            const auto& corners = std::get<0>(state);
                            const double& state_z = std::get<1>(state);
                            const double& state_height = std::get<2>(state);
                            
                            // 모든 모서리 좌표, z 위치, 높이를 비교
                            bool corners_match = true;
                            for (size_t i = 0; i < corners.size(); ++i)
                            {
                                if (std::abs(corners[i][0] - rotated_corners[i][0]) > 1e-6 || std::abs(corners[i][1] - rotated_corners[i][1]) > 1e-6)
                                {
                                    corners_match = false;
                                    break;
                                }
                            }
                            return corners_match && std::abs(state_z - z_center) < 1e-6 && std::abs(state_height - height) < 1e-6;
                        });

                    if (it != last_buffer_state.end())
                    {
                        last_buffer_state.erase(it);
                        if (buffer_states.find(stacking_number + 1) != buffer_states.end())
                        {
                            buffer_states[stacking_number + 1] = last_buffer_state;
                        }
                    }
                }
                double stacking_rate = (total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2])) * 100;
                stacking_rates.push_back(stacking_rate);
            }
            else if (pallet_id == 2)
            {
                // 버퍼 팔렛트에 배치
                if (buffer_states.find(stacking_number + 1) == buffer_states.end())
                {
                    buffer_states[stacking_number + 1] = {};
                }
                buffer_states[stacking_number + 1].push_back(placement_tuple);
            }

            stacking_number++;
            if (!is_valid)
                break;
        }

        // 프레임 생성
        for (int frame = 1; frame <= stacking_number; frame++)
        {
            std::string frame_filename;
            
            if (frame < 10)
            {
                frame_filename = (result_path / ("frame_0" + std::to_string(frame) + ".png")).string();
            }
            else
            {
                frame_filename = (result_path / ("frame_" + std::to_string(frame) + ".png")).string();
            }
            
            // Set output for this frame
            gp << "set output '" << frame_filename << "'\n";
            
            // Setup multiplot
            gp << "set multiplot layout 2,2 spacing 0.1\n";

            // Current states
            const auto& current_main = main_states[frame];
            const auto& current_buffer = buffer_states[frame];

            // Generate all views
            generate_view_1600(gp, current_buffer, cubic_range, "Stack all Boxes: Buffer Pallet 60, 30", 60, 30, 0.0, 0);
            generate_view_1600(gp, current_buffer, cubic_range, "Stack all Boxes: Buffer Pallet 30, 60", 30, 60, 0.5, 0);
            generate_view_1600(gp, current_main, cubic_range, "Stack all Boxes: Main Pallet 60, 30", 60, 30, 0.0, 0.5);
            generate_view_1600(gp, current_main, cubic_range, "Stack all Boxes: Main Pallet 30, 60", 30, 60, 0.5, 0.5);

            gp << "unset multiplot\n";
            gp.flush();

            // Wait for frame to be created
            if (wait_for_frame(frame_filename))
            {
                frame_filenames.push_back(frame_filename);
                std::cout << "Created frame " << frame << "/" << stacking_number << std::endl;
            }
        }

        // Create GIF if we have frames
        if (!frame_filenames.empty())
        {
            std::string gif_filename = (result_path / (result_file_name + ".gif")).string();
            std::cout << "\nCreating GIF: " << gif_filename << std::endl;
            VisualizationUtils::create_gif(frame_filenames, gif_filename);
            std::cout << "GIF creation completed" << std::endl;
        }
        else
        {
            std::cerr << "No frames were generated for GIF creation" << std::endl;
        }

        return {total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2]) * 100, stacking_number};
    }

    static std::pair<double, int> stack_all_box_check_and_visualize_xyz(
        const std::vector<nlohmann::json>& placements,
        const std::vector<nlohmann::json>& boxes,
        const std::vector<double>& cubic_range,
        const std::string& result_folder_name = "./results",
        const std::string& result_file_name = "stacking_animation")
    {
        // 결과 디렉토리 초기화
        std::filesystem::path result_path(result_folder_name);
        try {
            if (std::filesystem::exists(result_path))
            {
                std::filesystem::remove_all(result_path);
            }
            std::filesystem::create_directories(result_path);
            std::cout << "Created result directory: " << std::filesystem::absolute(result_path) << std::endl;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error handling result directory: " << e.what() << std::endl;
            return {0.0, 0};
        }

        double total_volume = 0;
        int stacking_number = 0;
        std::vector<double> stacking_rates;
        std::vector<std::string> frame_filenames;
        bool is_valid = true;

        // 각 프레임별 상태를 추적하기 위한 맵
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> main_states;
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> buffer_states;

        // Initialize Gnuplot
        Gnuplot gp;
        gp << "set terminal pngcairo size 1600,800 enhanced font 'Verdana,10'\n";
        
        // 먼저 모든 placement를 순서대로 처리하여 상태 맵 구성
        for (const auto& place_box : placements)
        {
            int box_id = place_box["box_id"].get<int>();
            int pallet_id = place_box["pallet_id"].get<int>();
            
            auto box_it = std::find_if(boxes.begin(), boxes.end(),
                          [box_id](const nlohmann::json& box) {
                return box["box_id"].get<int>() == box_id;
            });
            
            if (box_it == boxes.end())
            {
                std::cerr << "Box with ID " << box_id << " not found." << std::endl;
                is_valid = false;
                continue;
            }

            double width = (*box_it)["box_size"][0].get<double>();
            double length = (*box_it)["box_size"][1].get<double>();
            double height = (*box_it)["box_size"][2].get<double>();
            double angle = place_box["box_rot"].get<double>();
            double x_center = place_box["box_loc"][0].get<double>();
            double y_center = place_box["box_loc"][1].get<double>();
            double z_center = place_box["box_loc"][2].get<double>();

            auto rotated_corners = GeometryUtils::rotate_box_corners(x_center, y_center, width, length, angle);
            auto placement_tuple = std::make_tuple(rotated_corners, z_center, height);

            // 현재 프레임까지의 모든 상태 복사
            if (!main_states.empty())
            {
                main_states[stacking_number + 1] = main_states[stacking_number];
            }
            if (!buffer_states.empty())
            {
                buffer_states[stacking_number + 1] = buffer_states[stacking_number];
            }

            if (pallet_id == 1)
            {
                // 메인 팔렛트에 배치
                if (main_states.find(stacking_number + 1) == main_states.end())
                {
                    main_states[stacking_number + 1] = {};
                }
                main_states[stacking_number + 1].push_back(placement_tuple);
                total_volume += width * length * height;

                // 버퍼에서 이동한 경우, 버퍼에서 제거
                if (!buffer_states.empty())
                {
                    auto& last_buffer_state = buffer_states[stacking_number];
                    auto it = std::find_if(last_buffer_state.begin(), last_buffer_state.end(),
                        [&](const auto& state) {
                            const auto& corners = std::get<0>(state);
                            return std::abs(corners[0][0] - rotated_corners[0][0]) < 1e-6 &&
                                   std::abs(corners[0][1] - rotated_corners[0][1]) < 1e-6;
                        });
                    if (it != last_buffer_state.end())
                    {
                        last_buffer_state.erase(it);
                        if (buffer_states.find(stacking_number + 1) != buffer_states.end())
                        {
                            buffer_states[stacking_number + 1] = last_buffer_state;
                        }
                    }
                }
                double stacking_rate = (total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2])) * 100;
                stacking_rates.push_back(stacking_rate);
            }
            else if (pallet_id == 2)
            {
                // 버퍼 팔렛트에 배치
                if (buffer_states.find(stacking_number + 1) == buffer_states.end())
                {
                    buffer_states[stacking_number + 1] = {};
                }
                buffer_states[stacking_number + 1].push_back(placement_tuple);
            }

            stacking_number++;
            if (!is_valid) break;
        }

        // 프레임 생성
        for (int frame = 1; frame <= stacking_number; frame++) 
        {
            std::string frame_filename;
            
            if (frame < 10)
            {
                frame_filename = (result_path / ("frame_0" + std::to_string(frame) + ".png")).string();
            }
            else
            {
                frame_filename = (result_path / ("frame_" + std::to_string(frame) + ".png")).string();
            }
            
            // Set output for this frame
            gp << "set output '" << frame_filename << "'\n";
            
            // Setup multiplot
            gp << "set multiplot layout 2,2 spacing 0.1\n";

            // Current states
            const auto& current_main = main_states[frame];

            // Generate all views
            generate_view_800(gp, current_main, cubic_range, "Stack all Boxes: Main Pallet x, z", 90, 0, 0.0, 0);
            generate_view_800(gp, current_main, cubic_range, "Stack all Boxes: Main Pallet x, y", 0, 90, 0.5, 0);

            gp << "unset multiplot\n";
            gp.flush();

            // Wait for frame to be created
            if (wait_for_frame(frame_filename))
            {
                frame_filenames.push_back(frame_filename);
                std::cout << "Created frame " << frame << "/" << stacking_number << std::endl;
            }
        }

        // Create GIF if we have frames
        if (!frame_filenames.empty())
        {
            std::string gif_filename = (result_path / (result_file_name + ".gif")).string();
            std::cout << "\nCreating GIF: " << gif_filename << std::endl;
            VisualizationUtils::create_gif(frame_filenames, gif_filename);
            std::cout << "GIF creation completed" << std::endl;
        }
        else
        {
            std::cerr << "No frames were generated for GIF creation" << std::endl;
        }

        return {total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2]) * 100, stacking_number};
    }

    static std::pair<double, int> stack_with_buffer_check_and_visualize(
        const std::vector<nlohmann::json>& placements,
        const std::vector<nlohmann::json>& boxes,
        const std::vector<double>& cubic_range,
        const std::string& result_folder_name = "./results",
        const std::string& result_file_name = "stacking_animation")
    {
        // Initialize result directory
        std::filesystem::path result_path(result_folder_name);
        try {
            if (std::filesystem::exists(result_path))
            {
                std::filesystem::remove_all(result_path);
            }
            std::filesystem::create_directories(result_path);
            std::cout << "Created result directory: " << std::filesystem::absolute(result_path) << std::endl;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error handling result directory: " << e.what() << std::endl;
            return {0.0, 0};
        }

        // stack_with_buffer_check_and_visualize 메소드 내의 메인 처리 부분을 다음과 같이 수정

        double total_volume = 0;
        int frame_number = 0;
        std::vector<double> stacking_rates;
        std::vector<std::string> frame_filenames;
        bool is_valid = true;

        std::vector<std::tuple<std::vector<std::vector<double>>, double, double>> main_state;
        std::vector<std::tuple<std::vector<std::vector<double>>, double, double>> buffer_state;

        // Initialize Gnuplot
        Gnuplot gp;
        gp << "set terminal pngcairo size 1600,1600 enhanced font 'Verdana,10'\n";

        auto create_frame = [&](const std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>& current_main,
                            const std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>& current_buffer) {
            frame_number++;
            std::string frame_filename = (frame_number < 10) ?
                (result_path / ("frame_0" + std::to_string(frame_number) + ".png")).string()
                : (result_path / ("frame_" + std::to_string(frame_number) + ".png")).string();
            
            gp << "set output '" << frame_filename << "'\n";
            gp << "set multiplot layout 2,2 spacing 0.1\n";

            generate_view_1600(gp, current_buffer, cubic_range, "Stack with Buffer: Buffer Pallet 60, 30", 60, 30, 0.0, 0);
            generate_view_1600(gp, current_buffer, cubic_range, "Stack with Buffer: Buffer Pallet 30, 60", 30, 60, 0.5, 0);
            generate_view_1600(gp, current_main, cubic_range, "Stack with Buffer: Main Pallet 60, 30", 60, 30, 0.0, 0.5);
            generate_view_1600(gp, current_main, cubic_range, "Stack with Buffer: Main Pallet 30, 60", 30, 60, 0.5, 0.5);

            gp << "unset multiplot\n";
            gp.flush();

            if (wait_for_frame(frame_filename))
            {
                frame_filenames.push_back(frame_filename);
                std::cout << "Created frame " << frame_number << std::endl;
            }
        };

        // Process placements and create frames
        for (const auto& place_box : placements)
        {
            int box_id = place_box["box_id"].get<int>();
            int pallet_id = place_box["pallet_id"].get<int>();
            
            auto box_it = std::find_if(boxes.begin(), boxes.end(),
                [box_id](const nlohmann::json& box) {
                    return box["box_id"].get<int>() == box_id;
                });
            
            if (box_it == boxes.end())
            {
                std::cerr << "Box with ID " << box_id << " not found." << std::endl;
                is_valid = false;
                continue;
            }

            double width = (*box_it)["box_size"][0].get<double>();
            double length = (*box_it)["box_size"][1].get<double>();
            double height = (*box_it)["box_size"][2].get<double>();
            double angle = place_box["box_rot"].get<double>();
            double x_center = place_box["box_loc"][0].get<double>();
            double y_center = place_box["box_loc"][1].get<double>();
            double z_center = place_box["box_loc"][2].get<double>();

            auto rotated_corners = GeometryUtils::rotate_box_corners(x_center, y_center, width, length, angle);
            auto placement_tuple = std::make_tuple(rotated_corners, z_center, height);

            if (pallet_id == 1)
            {
                // Check if this box is coming from buffer
                auto it = std::find_if(buffer_state.begin(), buffer_state.end(),
                    [&](const auto& state) {
                        const auto& corners = std::get<0>(state);
                        bool corners_match = true;
                        for (size_t i = 0; i < corners.size(); ++i)
                        {
                            if (std::abs(corners[i][0] - rotated_corners[i][0]) > 1e-6 || std::abs(corners[i][1] - rotated_corners[i][1]) > 1e-6)
                            {
                                corners_match = false;
                                break;
                            }
                        }
                        return corners_match;
                    });

                if (it != buffer_state.end())
                {
                    std::cout << "Moved box " << box_id << " from buffer to main" << std::endl;
                    
                    // Create frame showing box still in buffer
                    create_frame(main_state, buffer_state);
                    
                    // Update states and create frame showing the moved box
                    main_state.push_back(placement_tuple);
                    buffer_state.erase(it);
                    create_frame(main_state, buffer_state);
                }
                else
                {
                    // For boxes directly placed in main pallet, create only one frame
                    main_state.push_back(placement_tuple);
                    create_frame(main_state, buffer_state);
                }
                
                total_volume += width * length * height;
                double stacking_rate = (total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2])) * 100;
                stacking_rates.push_back(stacking_rate);
            }
            else if (pallet_id == 2)
            {
                // For boxes placed in buffer, create only one frame
                buffer_state.push_back(placement_tuple);
                create_frame(main_state, buffer_state);
            }

            if (!is_valid) break;
        }

        // Create GIF if we have frames
        if (!frame_filenames.empty())
        {
            std::string gif_filename = (result_path / (result_file_name + ".gif")).string();
            std::cout << "\nCreating GIF: " << gif_filename << std::endl;
            VisualizationUtils::create_gif(frame_filenames, gif_filename);
            std::cout << "GIF creation completed" << std::endl;
        }
        else
        {
            std::cerr << "No frames were generated for GIF creation" << std::endl;
        }

        // stacking rate graph
        std::string graph_file = (result_path / "stacking_rate_graph_buf.png").string();
        
        gp << "set terminal pngcairo size 800, 800 enhanced font 'Verdana, 12'\n";
        gp << "set output '" << graph_file << "'\n";
        gp << "set title 'Stacking Rate Over Frames'\n";
        gp << "set xlabel 'Frame'\n";
        gp << "set xrange [0:" << frame_number << "]\n";
        gp << "set ylabel 'Stacking Rate (%)'\n";
        gp << "set yrange [0:100]\n";
        gp << "set grid\n";
        gp << "plot '-' with linespoints title 'Stacking Rate (%)' lc rgb 'blue'\n";

        for (size_t i = 0; i < stacking_rates.size(); ++i)
        {
            gp << i + 1 << " " << stacking_rates[i] << "\n";
        }
        gp << "e\n";
        gp.flush();

        std::cout << "Stacking rate graph saved to: " << graph_file << std::endl;

        return {total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2]) * 100, frame_number};
    }

    static std::pair<double, int> stack_with_buffer_check_and_visualize_xyz(
        const std::vector<nlohmann::json>& placements,
        const std::vector<nlohmann::json>& boxes,
        const std::vector<double>& cubic_range,
        const std::string& result_folder_name = "./results",
        const std::string& result_file_name = "stacking_animation")
    {
        // 결과 디렉토리 초기화
        std::filesystem::path result_path(result_folder_name);
        try {
            if (std::filesystem::exists(result_path))
            {
                std::filesystem::remove_all(result_path);
            }
            std::filesystem::create_directories(result_path);
            std::cout << "Created result directory: " << std::filesystem::absolute(result_path) << std::endl;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error handling result directory: " << e.what() << std::endl;
            return {0.0, 0};
        }

        double total_volume = 0;
        int stacking_number = 0;
        std::vector<double> stacking_rates;
        std::vector<std::string> frame_filenames;
        bool is_valid = true;

        // 각 프레임별 상태를 추적하기 위한 맵
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> main_states;
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>> buffer_states;

        // Initialize Gnuplot
        Gnuplot gp;
        gp << "set terminal pngcairo size 1600,800 enhanced font 'Verdana,10'\n";
        
        // 먼저 모든 placement를 순서대로 처리하여 상태 맵 구성
        for (const auto& place_box : placements)
        {
            int box_id = place_box["box_id"].get<int>();
            int pallet_id = place_box["pallet_id"].get<int>();
            
            auto box_it = std::find_if(boxes.begin(), boxes.end(),
                          [box_id](const nlohmann::json& box) {
                return box["box_id"].get<int>() == box_id;
            });
            
            if (box_it == boxes.end())
            {
                std::cerr << "Box with ID " << box_id << " not found." << std::endl;
                is_valid = false;
                continue;
            }

            double width = (*box_it)["box_size"][0].get<double>();
            double length = (*box_it)["box_size"][1].get<double>();
            double height = (*box_it)["box_size"][2].get<double>();
            double angle = place_box["box_rot"].get<double>();
            double x_center = place_box["box_loc"][0].get<double>();
            double y_center = place_box["box_loc"][1].get<double>();
            double z_center = place_box["box_loc"][2].get<double>();

            auto rotated_corners = GeometryUtils::rotate_box_corners(x_center, y_center, width, length, angle);
            auto placement_tuple = std::make_tuple(rotated_corners, z_center, height);

            // 현재 프레임까지의 모든 상태 복사
            if (!main_states.empty())
            {
                main_states[stacking_number + 1] = main_states[stacking_number];
            }
            if (!buffer_states.empty())
            {
                buffer_states[stacking_number + 1] = buffer_states[stacking_number];
            }

            if (pallet_id == 1) 
            {
                // 메인 팔렛트에 배치
                if (main_states.find(stacking_number + 1) == main_states.end())
                {
                    main_states[stacking_number + 1] = {};
                }
                main_states[stacking_number + 1].push_back(placement_tuple);
                total_volume += width * length * height;

                // 버퍼에서 이동한 경우, 버퍼에서 제거
                if (!buffer_states.empty())
                {
                    auto& last_buffer_state = buffer_states[stacking_number];
                    auto it = std::find_if(last_buffer_state.begin(), last_buffer_state.end(),
                        [&](const auto& state) {
                            const auto& corners = std::get<0>(state);
                            return std::abs(corners[0][0] - rotated_corners[0][0]) < 1e-6 &&
                                   std::abs(corners[0][1] - rotated_corners[0][1]) < 1e-6;
                        });
                    if (it != last_buffer_state.end())
                    {
                        last_buffer_state.erase(it);
                        if (buffer_states.find(stacking_number + 1) != buffer_states.end())
                        {
                            buffer_states[stacking_number + 1] = last_buffer_state;
                        }
                    }
                }
                double stacking_rate = (total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2])) * 100;
                stacking_rates.push_back(stacking_rate);
            }
            else if (pallet_id == 2)
            {
                // 버퍼 팔렛트에 배치
                if (buffer_states.find(stacking_number + 1) == buffer_states.end())
                {
                    buffer_states[stacking_number + 1] = {};
                }
                buffer_states[stacking_number + 1].push_back(placement_tuple);
            }

            stacking_number++;
            if (!is_valid)
                break;
        }

        // 프레임 생성
        for (int frame = 1; frame <= stacking_number; frame++)
        {
            std::string frame_filename;

            if (frame < 10)
            {
                frame_filename = (result_path / ("frame_0" + std::to_string(frame) + ".png")).string();
            }
            else
            {
                frame_filename = (result_path / ("frame_" + std::to_string(frame) + ".png")).string();
            }
            
            // Set output for this frame
            gp << "set output '" << frame_filename << "'\n";
            
            // Setup multiplot
            gp << "set multiplot layout 2,2 spacing 0.1\n";

            // Current states
            const auto& current_main = main_states[frame];

            // Generate all views
            generate_view_800(gp, current_main, cubic_range, "Stacking with Buffer: Main Pallet x, z", 90, 0, 0.0, 0);
            generate_view_800(gp, current_main, cubic_range, "Stacking with Buffer: Main Pallet x, y", 0, 90, 0.5, 0);

            gp << "unset multiplot\n";
            gp.flush();

            // Wait for frame to be created
            if (wait_for_frame(frame_filename))
            {
                frame_filenames.push_back(frame_filename);
                std::cout << "Created frame " << frame << "/" << stacking_number << std::endl;
            }
        }

        // Create GIF if we have frames
        if (!frame_filenames.empty())
        {
            std::string gif_filename = (result_path / (result_file_name + ".gif")).string();
            std::cout << "\nCreating GIF: " << gif_filename << std::endl;
            VisualizationUtils::create_gif(frame_filenames, gif_filename);
            std::cout << "GIF creation completed" << std::endl;
        }
        else
        {
            std::cerr << "No frames were generated for GIF creation" << std::endl;
        }

        return {total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2]) * 100, stacking_number};
    }

private:
    static void process_box_movement(
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>>& main_states,
        std::map<int, std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>>& buffer_states,
        int& current_frame,
        const std::tuple<std::vector<std::vector<double>>, double, double>& placement_tuple,
        double& total_volume,
        const std::vector<double>& cubic_range,
        std::vector<double>& stacking_rates,
        const double box_volume,
        bool is_buffer_to_main
    )
    {
        if (is_buffer_to_main)
        {
            // Frame 1: Box still in buffer, not yet in main
            if (main_states.find(current_frame) == main_states.end())
            {
                main_states[current_frame] = main_states[current_frame - 1];
            }
            if (buffer_states.find(current_frame) == buffer_states.end())
            {
                buffer_states[current_frame] = buffer_states[current_frame - 1];
            }

            // Frame 2: Box moved to main, removed from buffer
            current_frame++;
            main_states[current_frame] = main_states[current_frame - 1];
            main_states[current_frame].push_back(placement_tuple);
            
            buffer_states[current_frame] = buffer_states[current_frame - 1];
            auto& current_buffer_state = buffer_states[current_frame];
            auto it = std::find_if(current_buffer_state.begin(), current_buffer_state.end(),
                [&](const auto& state) {
                    const auto& corners = std::get<0>(state);
                    const double& state_z = std::get<1>(state);
                    const double& state_height = std::get<2>(state);
                    const auto& new_corners = std::get<0>(placement_tuple);
                    
                    bool corners_match = true;
                    for (size_t i = 0; i < corners.size(); ++i) {
                        if (std::abs(corners[i][0] - new_corners[i][0]) > 1e-6 ||
                            std::abs(corners[i][1] - new_corners[i][1]) > 1e-6) {
                            corners_match = false;
                            break;
                        }
                    }
                    return corners_match && 
                           std::abs(state_z - std::get<1>(placement_tuple)) < 1e-6 && 
                           std::abs(state_height - std::get<2>(placement_tuple)) < 1e-6;
                });
            
            if (it != current_buffer_state.end())
            {
                current_buffer_state.erase(it);
            }

            total_volume += box_volume;
            double stacking_rate = (total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2])) * 100;
            stacking_rates.push_back(stacking_rate);
            
            std::cout << "Created transition frames for box movement from buffer to main" << std::endl;
        }
        else
        {
            if (main_states.find(current_frame) == main_states.end())
            {
                if (current_frame > 0)
                {
                    main_states[current_frame] = main_states[current_frame - 1];
                }
                else
                {
                    main_states[current_frame] = {};
                }
            }
            main_states[current_frame].push_back(placement_tuple);
            total_volume += box_volume;
            
            double stacking_rate = (total_volume / (cubic_range[0] * cubic_range[1] * cubic_range[2])) * 100;
            stacking_rates.push_back(stacking_rate);
        }
    }
    
    static void generate_view_800(Gnuplot& gp, 
                            const std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>& placements,
                            const std::vector<double>& cubic_range,
                            const std::string& title,
                            int view1,
                            int view2,
                            double origin_x,
                            double origin_y) 
    {
        gp << "reset\n";
        gp << "set size 0.5,1\n";
        gp << "set origin " << origin_x << "," << origin_y << "\n";
        gp << "set title '" << title << "'\n";
        gp << "set view " << view1 << "," << view2 << "\n";
        gp << "set xrange [0:" << cubic_range[0] << "]\n";
        gp << "set yrange [0:" << cubic_range[1] << "]\n";
        gp << "set zrange [0:" << cubic_range[2] << "]\n";
        gp << "set ticslevel 0\n";
        gp << "set grid\n";
        
        for (const auto& placement : placements)
        {
            std::string color = (title.find("Buffer Pallet") != std::string::npos) ? "0xFFCCCC" : "0xFFFFCC";
            VisualizationUtils::plot_3d_box(gp, std::get<0>(placement), std::get<1>(placement), 
                                          std::get<2>(placement), color, 0.5f);
        }
        gp << "splot NaN notitle\n";
    }

    static void generate_view_1600(Gnuplot& gp, 
                            const std::vector<std::tuple<std::vector<std::vector<double>>, double, double>>& placements,
                            const std::vector<double>& cubic_range,
                            const std::string& title,
                            int view1,
                            int view2,
                            double origin_x,
                            double origin_y) 
    {
        gp << "reset\n";
        gp << "set size 0.5,0.5\n";
        gp << "set origin " << origin_x << "," << origin_y << "\n";
        gp << "set title '" << title << "'\n";
        gp << "set view " << view1 << "," << view2 << "\n";
        gp << "set xrange [0:" << cubic_range[0] << "]\n";
        gp << "set yrange [0:" << cubic_range[1] << "]\n";
        gp << "set zrange [0:" << cubic_range[2] << "]\n";
        gp << "set ticslevel 0\n";
        gp << "set grid\n";
        
        for (const auto& placement : placements)
        {
            std::string color = (title.find("Buffer Pallet") != std::string::npos) ? "0xFFCCCC" : "0xFFFFCC";
            VisualizationUtils::plot_3d_box(gp, std::get<0>(placement), std::get<1>(placement), 
                                          std::get<2>(placement), color, 0.5f);
        }
        gp << "splot NaN notitle\n";
    }

    static bool wait_for_frame(const std::string& frame_filename)
    {
        int retry_count = 0;
        const int max_retries = 20;
        const int wait_time = 100;
        
        while (retry_count < max_retries)
        {
            std::error_code ec;
            if (std::filesystem::exists(frame_filename))
            {
                uintmax_t size = std::filesystem::file_size(frame_filename, ec);
                if (!ec && size > 0)
                {
                    #ifdef _WIN32
                        Sleep(wait_time);
                    #else
                        usleep(wait_time * 1000);
                    #endif
                    return true;
                }
            }
            retry_count++;
            #ifdef _WIN32
                Sleep(wait_time);
            #else
                usleep(wait_time * 1000);
            #endif
        }
        
        std::cerr << "Failed to create frame: " << frame_filename << std::endl;
        return false;
    }
};

#endif