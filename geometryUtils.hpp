#ifndef _GEOMETRY_UTILS
#define _GEOMETRY_UTILS

#include <vector>
#include <cmath>
#include <algorithm>
#include <utility>
#include <iostream>
#include <tuple>
#include <string>
#include <unordered_map>
#include <set>
#include <filesystem>
#include <fstream>
#include <iomanip>


// Geometric utility functions
class GeometryUtils {
public:
    static bool is_point_in_box(const std::vector<double>& point,
                              const std::vector<std::vector<double>>& box_corners,
                              const std::pair<double, double>& z_range)
    {
        double x = point[0];
        double y = point[1];
        double z = point[2];

        double x_min = std::min(box_corners[0][0], box_corners[1][0]);
        double x_max = std::max(box_corners[0][0], box_corners[1][0]);
        double y_min = std::min(box_corners[0][1], box_corners[1][1]);
        double y_max = std::max(box_corners[0][1], box_corners[1][1]);

        return (x_min <= x && x <= x_max) && (y_min <= y && y <= y_max) && (z_range.first <= z && z <= z_range.second);
    }

    static bool check_overlap_rotation(const std::vector<std::vector<double>>& rotated_corners1,
                                     const std::pair<double, double>& z_range1,
                                     const std::vector<std::vector<double>>& rotated_corners2,
                                     const std::pair<double, double>& z_range2)
    {
        for (const auto& corner : rotated_corners1)
        {
            if (is_point_in_box({corner[0], corner[1], z_range1.first}, rotated_corners2, z_range2) ||
                is_point_in_box({corner[0], corner[1], z_range1.second}, rotated_corners2, z_range2))
            {
                return true;
            }
        }

        for (const auto& corner : rotated_corners2)
        {
            if (is_point_in_box({corner[0], corner[1], z_range2.first}, rotated_corners1, z_range1) ||
                is_point_in_box({corner[0], corner[1], z_range2.second}, rotated_corners1, z_range1))
            {
                return true;
            }
        }
        return false;
    }

    static std::vector<std::vector<double>> rotate_box_corners(double x_center, double y_center, double width, double length, double angle)
    {
        double radians = angle * M_PI / 180.0;
        double cos_angle = cos(radians);
        double sin_angle = sin(radians);

        std::vector<std::vector<double>> corners = {
            {-width / 2, -length / 2},
            {width / 2, -length / 2},
            {width / 2, length / 2},
            {-width / 2, length / 2}
        };

        std::vector<std::vector<double>> rotated_corners(4, std::vector<double>(2));
        for (int i = 0; i < 4; ++i)
        {
            rotated_corners[i][0] = corners[i][0] * cos_angle - corners[i][1] * sin_angle + x_center;
            rotated_corners[i][1] = corners[i][0] * sin_angle + corners[i][1] * cos_angle + y_center;
        }

        return rotated_corners;
    }
};

#endif