#pragma once

#include <cstddef>
#include <complex>
#include <string>
#include <vector>

struct StftConfig {
    std::size_t frame_size = 1024;
    std::size_t hop_size = 512;
    std::size_t mel_bands = 64;
    float target_frequency = 1000.0f;
};

struct StftResult {
    unsigned sample_rate = 0;
    std::size_t frame_size = 0;
    std::size_t hop_size = 0;
    std::vector<float> times;
    std::vector<float> frequencies;
    std::vector<std::vector<float>> spectrogram_db;
    std::vector<std::vector<float>> mel_spectrogram_db;
    std::vector<float> target_frequency_db;
};

bool IsRiscVPlatform();
bool IsRvvStftEnabled();
std::string PlatformName();

StftResult ComputeStft(const std::vector<float>& mono_samples,
                       unsigned sample_rate,
                       const StftConfig& config);

StftResult ComputeStftGeneric(const std::vector<float>& mono_samples,
                              unsigned sample_rate,
                              const StftConfig& config);

std::vector<float> BuildHannWindow(std::size_t frame_size);

std::vector<std::vector<float>> BuildMelSpectrogram(const std::vector<std::vector<float>>& spectrogram_db,
                                                    unsigned sample_rate,
                                                    std::size_t frame_size,
                                                    std::size_t mel_bands);

StftResult ComputeFromWindowedFrames(const std::vector<std::vector<float>>& frames,
                                     unsigned sample_rate,
                                     const StftConfig& config);

#if defined(USE_RVV_STFT) && defined(__riscv_vector)
void FftInPlaceRvv(std::vector<std::complex<float>>& data);
std::vector<std::vector<float>> BuildMelSpectrogramRvv(
    const std::vector<std::vector<float>>& spectrogram_db,
    unsigned sample_rate,
    std::size_t frame_size,
    std::size_t mel_bands);
StftResult ComputeStftIntraRvv(const std::vector<float>& mono_samples,
                               unsigned sample_rate,
                               const StftConfig& config);
StftResult ComputeStftMultiRvv(const std::vector<float>& mono_samples,
                               unsigned sample_rate,
                               const StftConfig& config);
StftResult ComputeFromWindowedFramesRvv(const std::vector<std::vector<float>>& frames,
                                        unsigned sample_rate,
                                        const StftConfig& config);
#endif
