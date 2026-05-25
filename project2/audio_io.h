#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct AudioBuffer {
    unsigned sample_rate = 0;
    unsigned channels = 0;
    std::vector<float> mono_samples;
};

bool IsAudioFile(const std::filesystem::path& path);
AudioBuffer DecodeAudioFile(const std::filesystem::path& path);
std::string AudioBackendDescription();
