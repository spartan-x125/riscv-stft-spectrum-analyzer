#include "audio_io.h"
#include "image_writer.h"
#include "settings.h"
#include "stft.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::filesystem::path ResolveDatasetPath(const char* argv0) {
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::path(DATASET_PATH),
        std::filesystem::current_path() / "dataset",
        std::filesystem::current_path() / DATASET_PATH,
        std::filesystem::current_path() / ".." / DATASET_PATH,
        std::filesystem::current_path() / ".." / ".." / "dataset",
        std::filesystem::absolute(std::filesystem::path(argv0)).parent_path() / DATASET_PATH,
        std::filesystem::absolute(std::filesystem::path(argv0)).parent_path() / ".." / ".." / "dataset",
    };

    for (const auto& candidate : candidates) {
        std::error_code ec;
        const auto normalized = std::filesystem::weakly_canonical(candidate, ec);
        const auto& checked = ec ? candidate : normalized;
        if (std::filesystem::is_directory(checked)) {
            return checked;
        }
    }
    throw std::runtime_error("dataset path not found");
}

std::filesystem::path ResolveResultPath(const char* argv0) {
    const auto exe_dir = std::filesystem::absolute(std::filesystem::path(argv0)).parent_path();
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "project2" / "result",
        exe_dir / ".." / ".." / "result",
        std::filesystem::current_path() / "result",
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        const auto parent = std::filesystem::weakly_canonical(candidate.parent_path(), ec);
        if (!ec && std::filesystem::exists(parent)) {
            std::filesystem::create_directories(candidate);
            return std::filesystem::weakly_canonical(candidate);
        }
    }
    std::filesystem::create_directories("result");
    return std::filesystem::weakly_canonical("result");
}

std::vector<std::filesystem::path> CollectAudioFiles(const std::filesystem::path& dataset_path) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dataset_path)) {
        if (entry.is_regular_file() && IsAudioFile(entry.path())) {
            files.push_back(entry.path());
        }
    }
    return files;
}

void WriteTargetFrequencyTxt(const std::filesystem::path& path,
                             const std::vector<float>& times,
                             const std::vector<float>& values_db) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to write target frequency txt");
    }
    out << std::fixed << std::setprecision(6);
    for (std::size_t i = 0; i < times.size(); ++i) {
        out << times[i] << '\t' << values_db[i] << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const char* argv0 = argc > 0 ? argv[0] : "";
        const auto dataset_path = ResolveDatasetPath(argv0);
        const auto result_path = ResolveResultPath(argv0);

        std::cout << "Platform: " << PlatformName() << '\n';
        std::cout << "RVV STFT acceleration: " << (IsRvvStftEnabled() ? "Enabled" : "Disabled") << '\n';
        std::cout << "Audio backend: " << AudioBackendDescription() << '\n';
        std::cout << "Dataset: " << dataset_path << '\n';
        std::cout << "Result: " << result_path << '\n';

        const auto files = CollectAudioFiles(dataset_path);
        if (files.empty()) {
            std::cout << "No audio files found.\n";
            return 0;
        }

        const StftConfig config{
            static_cast<std::size_t>(STFT_FRAME_SIZE),
            static_cast<std::size_t>(STFT_HOP_SIZE),
            static_cast<std::size_t>(MEL_BAND_COUNT),
            static_cast<float>(TARGET_FREQUENCY),
        };

        std::size_t success = 0;
        for (const auto& file : files) {
            try {
                const auto stem = file.stem().string();
                const auto audio = DecodeAudioFile(file);
                const auto stft = ComputeStft(audio.mono_samples, audio.sample_rate, config);

                const auto spectrogram_path = result_path / (stem + "-spectrogram.png");
                const auto mel_path = result_path / (stem + "-mel_spectrogram.png");
                const auto target_svg_path = result_path / (stem + "-target_freq.svg");
                const auto target_txt_path = result_path / (stem + "-target_freq.txt");

                WriteSpectrogramPng(spectrogram_path, stft.spectrogram_db);
                WriteSpectrogramPng(mel_path, stft.mel_spectrogram_db);
                WriteTargetFrequencySvg(target_svg_path,
                                        stft.times,
                                        stft.target_frequency_db,
                                        static_cast<float>(TARGET_FREQUENCY));
                WriteTargetFrequencyTxt(target_txt_path, stft.times, stft.target_frequency_db);

                ++success;
                std::cout << "Processed: " << file.filename() << '\n';
            } catch (const std::exception& ex) {
                std::cerr << "Failed: " << file << ": " << ex.what() << '\n';
            }
        }

        std::cout << "Done. " << success << " of " << files.size() << " files processed.\n";
        return success == 0 ? 1 : 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
