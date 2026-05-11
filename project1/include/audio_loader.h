#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct AudioData {
    unsigned sample_rate = 0;
    unsigned channels = 0;
    std::vector<double> mono_samples;
};

bool IsSupportedAudioExtension(const std::filesystem::path& path);
AudioData LoadAudioFile(const std::filesystem::path& path);
std::string SupportedAudioDescription();
