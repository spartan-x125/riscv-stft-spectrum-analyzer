#pragma once

#include <filesystem>
#include <vector>

bool WritePngRgb(const std::filesystem::path& path,
                 int width,
                 int height,
                 const std::vector<unsigned char>& rgb);

void WriteSpectrogramPng(const std::filesystem::path& path,
                         const std::vector<std::vector<float>>& matrix_db);

void WriteTargetFrequencySvg(const std::filesystem::path& path,
                             const std::vector<float>& times,
                             const std::vector<float>& values_db,
                             float target_frequency);
