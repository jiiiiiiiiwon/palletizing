#ifndef _VISUALIZATION_UTILS
#define _VISUALIZATION_UTILS

#include <iostream>
#include <filesystem>
#include <vector>
#include <exception>
#include <string>

#include <gif_lib.h>
#include <opencv2/opencv.hpp>
#include <gnuplot-iostream.h>

// Visualization utilities
class VisualizationUtils {
public:
    static void create_gif(const std::vector<std::string>& filenames, const std::string& output_filename)
    {
        if (filenames.empty())
        {
            std::cerr << "No input files provided for GIF creation." << std::endl;
            return;
        }

        std::cout << "Starting GIF creation process..." << std::endl;
        std::vector<cv::Mat> valid_images;
        
        for (const auto& filename : filenames)
        {
            if (!std::filesystem::exists(filename))
                continue;
            cv::Mat test_image = cv::imread(filename);
            if (!test_image.empty())
            {
                valid_images.push_back(test_image);
            }
        }

        if (valid_images.empty())
        {
            std::cerr << "No valid images found!" << std::endl;
            return;
        }

        try {
            int error_code;
            GifFileType* gif = EGifOpenFileName(output_filename.c_str(), false, &error_code);
            if (!gif)
                return;

            ColorMapObject* color_map = GifMakeMapObject(256, NULL);
            if (!color_map)
            {
                EGifCloseFile(gif, &error_code);
                return;
            }

            for (int i = 0; i < 256; i++)
            {
                color_map->Colors[i].Red = ((i >> 5) & 0x07) * 255 / 7;
                color_map->Colors[i].Green = ((i >> 2) & 0x07) * 255 / 7;
                color_map->Colors[i].Blue = (i & 0x03) * 255 / 3;
            }

            if (EGifPutScreenDesc(gif, valid_images[0].cols, valid_images[0].rows, 8, 0, color_map) == GIF_ERROR)
            {
                GifFreeMapObject(color_map);
                EGifCloseFile(gif, &error_code);
                return;
            }

            std::vector<GifByteType> buffer(valid_images[0].cols * valid_images[0].rows);
            for (const auto& image : valid_images)
            {
                for (int y = 0; y < image.rows; y++)
                {
                    for (int x = 0; x < image.cols; x++)
                    {
                        cv::Vec3b pixel = image.at<cv::Vec3b>(y, x);
                        buffer[y * image.cols + x] = static_cast<GifByteType>(
                            ((pixel[2] * 7 / 255) << 5) | 
                            ((pixel[1] * 7 / 255) << 2) | 
                            (pixel[0] * 3 / 255)
                        );
                    }
                }

                if (EGifPutImageDesc(gif, 0, 0, image.cols, image.rows, false, nullptr) == GIF_ERROR)
                {
                    continue;
                }

                for (int y = 0; y < image.rows; y++)
                {
                    if (EGifPutLine(gif, &buffer[y * image.cols], image.cols) == GIF_ERROR)
                    {
                        break;
                    }
                }
            }

            GifFreeMapObject(color_map);
            EGifCloseFile(gif, &error_code);

        } catch (const std::exception& e) {
            std::cerr << "Exception occurred: " << e.what() << std::endl;
        }
    }

    static void plot_3d_box(Gnuplot& gp, const std::vector<std::vector<double>>& corners, 
                           double z, double height, const std::string& color, double opacity = 0.5)
    {
        // 바닥면
        gp << "set object polygon from ";
        for (const auto& corner : corners)
        {
            gp << corner[0] << "," << corner[1] << "," << z << " to ";
        }
        gp << corners[0][0] << "," << corners[0][1] << "," << z << " fc rgb '" << color << "' fs transparent solid " << opacity << " border rgb 'black'\n";

        // 윗면
        gp << "set object polygon from ";
        for (const auto& corner : corners)
        {
            gp << corner[0] << "," << corner[1] << "," << (z + height) << " to ";
        }
        gp << corners[0][0] << "," << corners[0][1] << "," << (z + height) << " fc rgb '" << color << "' fs transparent solid " << opacity << " border rgb 'black'\n";

        // 옆면
        for (size_t i = 0; i < corners.size(); ++i)
        {
            size_t j = (i + 1) % corners.size();
            gp << "set object polygon from "
               << corners[i][0] << "," << corners[i][1] << "," << z << " to "
               << corners[j][0] << "," << corners[j][1] << "," << z << " to "
               << corners[j][0] << "," << corners[j][1] << "," << (z + height) << " to "
               << corners[i][0] << "," << corners[i][1] << "," << (z + height) << " to "
               << corners[i][0] << "," << corners[i][1] << "," << z
               << " fc rgb '" << color << "' fs transparent solid " << opacity << " border rgb 'black'\n";
        }
    }
};

#endif