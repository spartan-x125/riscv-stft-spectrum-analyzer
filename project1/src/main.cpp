#include "audio_loader.h"
#include "fft.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
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
        std::filesystem::path(TARGET_DIRECTORY),
        std::filesystem::current_path() / TARGET_DIRECTORY,
        std::filesystem::current_path() / ".." / TARGET_DIRECTORY,
        std::filesystem::current_path() / ".." / ".." / "dataset",
        std::filesystem::absolute(std::filesystem::path(argv0)).parent_path() / TARGET_DIRECTORY,
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

    throw std::runtime_error("Dataset directory not found. Check TARGET_DIRECTORY in settings.h.");
}

std::vector<std::filesystem::path> CollectAudioFiles(const std::filesystem::path& dataset_path,
                                                     const std::filesystem::path& result_path) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dataset_path)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto parent = entry.path().parent_path();
        std::error_code ec;
        if (std::filesystem::equivalent(parent, result_path, ec)) {
            continue;
        }

        if (IsSupportedAudioExtension(entry.path())) {
            files.push_back(entry.path());
        }
    }
    return files;
}

std::filesystem::path ResultPathFor(const std::filesystem::path& result_dir,
                                    const std::filesystem::path& audio_file) {
    return result_dir / (audio_file.stem().string() + ".txt");
}

std::filesystem::path SpectrumPathFor(const std::filesystem::path& result_dir,
                                      const std::filesystem::path& audio_file) {
    return result_dir / (audio_file.stem().string() + "_spectrum.svg");
}

void WriteResult(const std::filesystem::path& output_path,
                 const std::filesystem::path& audio_path,
                 const AudioData& audio,
                 std::size_t fft_size,
                 std::size_t selected_bin,
                 double selected_frequency,
                 double magnitude) {
    std::ofstream output(output_path);
    if (!output) {
        throw std::runtime_error("Cannot write result file: " + output_path.string());
    }

    output << std::fixed << std::setprecision(10);
    output << "audio_file=" << audio_path.string() << '\n';
    output << "sample_rate=" << audio.sample_rate << '\n';
    output << "channels=" << audio.channels << '\n';
    output << "sample_count=" << audio.mono_samples.size() << '\n';
    output << "fft_size=" << fft_size << '\n';
    output << "target_frequency_hz=" << static_cast<double>(TARGETFREQUENCY) << '\n';
    output << "selected_bin=" << selected_bin << '\n';
    output << "selected_frequency_hz=" << selected_frequency << '\n';
    output << "magnitude=" << magnitude << '\n';
}

void WriteSpectrumSvg(const std::filesystem::path& output_path,
                      const std::vector<fft::Complex>& spectrum,
                      double sample_rate) {
    constexpr int width = 1200;
    constexpr int height = 700;
    constexpr int left = 80;
    constexpr int right = 30;
    constexpr int top = 30;
    constexpr int bottom = 70;

    if (spectrum.empty() || sample_rate <= 0.0) {
        throw std::runtime_error("Cannot draw empty spectrum");
    }

    const std::size_t max_bin = spectrum.size() / 2;
    const std::size_t plot_points = std::min<std::size_t>(max_bin + 1, static_cast<std::size_t>(width - left - right));
    std::vector<double> magnitudes(plot_points, 0.0);

    double max_magnitude = 0.0;
    for (std::size_t x = 0; x < plot_points; ++x) {
        const std::size_t begin = x * (max_bin + 1) / plot_points;
        const std::size_t end = std::max(begin + 1, (x + 1) * (max_bin + 1) / plot_points);
        double value = 0.0;
        for (std::size_t bin = begin; bin < end && bin < spectrum.size(); ++bin) {
            value = std::max(value, std::abs(spectrum[bin]));
        }
        magnitudes[x] = std::log1p(value);
        max_magnitude = std::max(max_magnitude, magnitudes[x]);
    }

    if (max_magnitude <= 0.0) {
        max_magnitude = 1.0;
    }

    std::ofstream output(output_path);
    if (!output) {
        throw std::runtime_error("Cannot write spectrum file: " + output_path.string());
    }

    const int plot_width = width - left - right;
    const int plot_height = height - top - bottom;
    output << std::fixed << std::setprecision(2);
    output << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
           << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' ' << height << "\">\n";
    output << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    output << "<line x1=\"" << left << "\" y1=\"" << top << "\" x2=\"" << left
           << "\" y2=\"" << top + plot_height << "\" stroke=\"black\"/>\n";
    output << "<line x1=\"" << left << "\" y1=\"" << top + plot_height << "\" x2=\""
           << left + plot_width << "\" y2=\"" << top + plot_height << "\" stroke=\"black\"/>\n";
    output << "<text x=\"" << width / 2 << "\" y=\"" << height - 20
           << "\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"18\">Frequency (Hz)</text>\n";
    output << "<text x=\"20\" y=\"" << height / 2
           << "\" text-anchor=\"middle\" transform=\"rotate(-90 20 " << height / 2
           << ")\" font-family=\"Arial\" font-size=\"18\">Magnitude (log scale)</text>\n";
    output << "<text x=\"" << left << "\" y=\"" << height - 45
           << "\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"14\">0</text>\n";
    output << "<text x=\"" << left + plot_width << "\" y=\"" << height - 45
           << "\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"14\">"
           << sample_rate / 2.0 << "</text>\n";
    output << "<polyline fill=\"none\" stroke=\"#1f77b4\" stroke-width=\"1.5\" points=\"";
    for (std::size_t i = 0; i < magnitudes.size(); ++i) {
        const double x = left + (magnitudes.size() == 1 ? 0.0 : static_cast<double>(i) * plot_width / (magnitudes.size() - 1));
        const double y = top + plot_height - magnitudes[i] * plot_height / max_magnitude;
        output << x << ',' << y << ' ';
    }
    output << "\"/>\n</svg>\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path dataset_path = ResolveDatasetPath(argc > 0 ? argv[0] : "");
        const std::filesystem::path result_path = dataset_path / "result";
        std::filesystem::create_directories(result_path);

        std::cout << "Dataset: " << dataset_path << '\n';
        std::cout << "Result: " << result_path << '\n';
        std::cout << "Target frequency: " << TARGETFREQUENCY << " Hz\n";
        std::cout << "CPU path: " << (fft::IsRiscVTarget() ? "RISC-V/RVV-capable build path" : "portable scalar FFT") << '\n';
        std::cout << SupportedAudioDescription() << '\n';

        const auto audio_files = CollectAudioFiles(dataset_path, result_path);
        if (audio_files.empty()) {
            std::cout << "No supported audio files found.\n";
            return 0;
        }

        std::size_t success_count = 0;
        for (const auto& audio_file : audio_files) {
            try {
                const AudioData audio = LoadAudioFile(audio_file);
                const auto spectrum = fft::ComputeFft(audio.mono_samples);
                std::size_t selected_bin = 0;
                double selected_frequency = 0.0;
                const double magnitude = fft::MagnitudeAtFrequency(
                    spectrum,
                    static_cast<double>(audio.sample_rate),
                    static_cast<double>(TARGETFREQUENCY),
                    &selected_bin,
                    &selected_frequency);

                const auto output_path = ResultPathFor(result_path, audio_file);
                const auto spectrum_path = SpectrumPathFor(result_path, audio_file);
                WriteResult(output_path,
                            audio_file,
                            audio,
                            spectrum.size(),
                            selected_bin,
                            selected_frequency,
                            magnitude);
                WriteSpectrumSvg(spectrum_path, spectrum, static_cast<double>(audio.sample_rate));
                ++success_count;
                std::cout << "Processed " << audio_file.filename() << " -> " << output_path.filename() << '\n';
            } catch (const std::exception& ex) {
                std::cerr << "Failed " << audio_file << ": " << ex.what() << '\n';
            }
        }

        std::cout << "Done. " << success_count << " of " << audio_files.size() << " files processed.\n";
        return success_count == 0 ? 1 : 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
