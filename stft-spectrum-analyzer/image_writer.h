#pragma once

#include <filesystem>
#include <vector>

bool WritePngRgb(const std::filesystem::path& path,
                 int width,
                 int height,
                 const std::vector<unsigned char>& rgb);

void WriteSpectrogramPng(const std::filesystem::path& path,
                         const std::vector<std::vector<float>>& matrix_db);

#if defined(USE_RVV_STFT) && defined(__riscv_vector)
void WriteSpectrogramPngRvv(const std::filesystem::path& path,
                            const std::vector<std::vector<float>>& matrix_db);

void ComputeTargetFrequencyCoordinatesRvv(const std::vector<float>& times,
                                          const std::vector<float>& values_db,
                                          float min_value,
                                          float max_value,
                                          float plot_width,
                                          float plot_height,
                                          float left,
                                          float bottom,
                                          std::vector<float>& x_coordinates,
                                          std::vector<float>& y_coordinates);
#endif

void WriteTargetFrequencySvg(const std::filesystem::path& path,
                             const std::vector<float>& times,
                             const std::vector<float>& values_db,
                             float target_frequency);
