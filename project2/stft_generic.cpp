#include "stft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinDb = -120.0f;

bool IsPowerOfTwo(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

float HzToMel(float hz) {
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float MelToHz(float mel) {
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

void FftInPlace(std::vector<std::complex<float>>& data) {
    const std::size_t n = data.size();
    if (!IsPowerOfTwo(n)) {
        throw std::invalid_argument("STFT frame size must be a power of two");
    }

    std::size_t j = 0;
    for (std::size_t i = 1; i < n; ++i) {
        std::size_t bit = n >> 1;
        while ((j & bit) != 0) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const float angle = -2.0f * kPi / static_cast<float>(len);
        const std::complex<float> w_len(std::cos(angle), std::sin(angle));
        for (std::size_t offset = 0; offset < n; offset += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto upper = data[offset + k];
                const auto lower = data[offset + k + len / 2] * w;
                data[offset + k] = upper + lower;
                data[offset + k + len / 2] = upper - lower;
                w *= w_len;
            }
        }
    }
}

std::vector<float> BuildHannWindow(std::size_t frame_size) {
    std::vector<float> window(frame_size, 1.0f);
    if (frame_size <= 1) {
        return window;
    }
    for (std::size_t i = 0; i < frame_size; ++i) {
        window[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) /
                                           static_cast<float>(frame_size - 1));
    }
    return window;
}

std::vector<std::vector<float>> BuildMelSpectrogram(const std::vector<std::vector<float>>& spectrogram_db,
                                                    unsigned sample_rate,
                                                    std::size_t frame_size,
                                                    std::size_t mel_bands) {
    if (spectrogram_db.empty() || mel_bands == 0) {
        return {};
    }

    const std::size_t freq_bins = spectrogram_db.front().size();
    const float min_mel = HzToMel(0.0f);
    const float max_mel = HzToMel(static_cast<float>(sample_rate) * 0.5f);
    std::vector<std::vector<float>> mel(spectrogram_db.size(), std::vector<float>(mel_bands, kMinDb));

    for (std::size_t band = 0; band < mel_bands; ++band) {
        const float left_mel = min_mel + (max_mel - min_mel) * static_cast<float>(band) /
                                           static_cast<float>(mel_bands + 1);
        const float center_mel = min_mel + (max_mel - min_mel) * static_cast<float>(band + 1) /
                                             static_cast<float>(mel_bands + 1);
        const float right_mel = min_mel + (max_mel - min_mel) * static_cast<float>(band + 2) /
                                            static_cast<float>(mel_bands + 1);
        const float left_hz = MelToHz(left_mel);
        const float center_hz = MelToHz(center_mel);
        const float right_hz = MelToHz(right_mel);

        for (std::size_t frame = 0; frame < spectrogram_db.size(); ++frame) {
            float weighted_sum = 0.0f;
            float weight_total = 0.0f;
            for (std::size_t bin = 0; bin < freq_bins; ++bin) {
                const float hz = static_cast<float>(bin) * static_cast<float>(sample_rate) /
                                 static_cast<float>(frame_size);
                float weight = 0.0f;
                if (hz >= left_hz && hz <= center_hz && center_hz > left_hz) {
                    weight = (hz - left_hz) / (center_hz - left_hz);
                } else if (hz > center_hz && hz <= right_hz && right_hz > center_hz) {
                    weight = (right_hz - hz) / (right_hz - center_hz);
                }
                if (weight > 0.0f) {
                    weighted_sum += spectrogram_db[frame][bin] * weight;
                    weight_total += weight;
                }
            }
            if (weight_total > 0.0f) {
                mel[frame][band] = weighted_sum / weight_total;
            }
        }
    }

    return mel;
}

StftResult ComputeFromWindowedFrames(const std::vector<std::vector<float>>& frames,
                                     unsigned sample_rate,
                                     const StftConfig& config) {
    StftResult result;
    result.sample_rate = sample_rate;
    result.frame_size = config.frame_size;
    result.hop_size = config.hop_size;

    const std::size_t freq_bins = config.frame_size / 2 + 1;
    result.frequencies.resize(freq_bins);
    for (std::size_t bin = 0; bin < freq_bins; ++bin) {
        result.frequencies[bin] = static_cast<float>(bin) * static_cast<float>(sample_rate) /
                                  static_cast<float>(config.frame_size);
    }

    const std::size_t target_bin = std::min<std::size_t>(
        freq_bins - 1,
        static_cast<std::size_t>(std::lround(config.target_frequency *
                                            static_cast<float>(config.frame_size) /
                                            static_cast<float>(sample_rate))));

    result.spectrogram_db.reserve(frames.size());
    result.times.reserve(frames.size());
    result.target_frequency_db.reserve(frames.size());

    for (std::size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
        std::vector<std::complex<float>> fft_buffer(config.frame_size);
        for (std::size_t i = 0; i < config.frame_size; ++i) {
            fft_buffer[i] = {frames[frame_index][i], 0.0f};
        }
        FftInPlace(fft_buffer);

        std::vector<float> row(freq_bins);
        for (std::size_t bin = 0; bin < freq_bins; ++bin) {
            const float magnitude = std::abs(fft_buffer[bin]) / static_cast<float>(config.frame_size);
            row[bin] = 20.0f * std::log10(std::max(magnitude, 1.0e-6f));
        }

        result.times.push_back(static_cast<float>(frame_index * config.hop_size) /
                               static_cast<float>(sample_rate));
        result.target_frequency_db.push_back(row[target_bin]);
        result.spectrogram_db.push_back(std::move(row));
    }

    result.mel_spectrogram_db = BuildMelSpectrogram(result.spectrogram_db,
                                                    sample_rate,
                                                    config.frame_size,
                                                    config.mel_bands);
    return result;
}

}  // namespace

bool IsRiscVPlatform() {
#if defined(__riscv)
    return true;
#else
    return false;
#endif
}

bool IsRvvStftEnabled() {
#if defined(USE_RVV_STFT) && defined(__riscv_vector)
    return true;
#else
    return false;
#endif
}

std::string PlatformName() {
    return IsRiscVPlatform() ? "RISC-V" : "Other";
}

StftResult ComputeStftGeneric(const std::vector<float>& mono_samples,
                              unsigned sample_rate,
                              const StftConfig& config) {
    if (mono_samples.empty()) {
        throw std::invalid_argument("audio samples are empty");
    }
    if (sample_rate == 0 || config.frame_size == 0 || config.hop_size == 0) {
        throw std::invalid_argument("invalid STFT parameters");
    }
    if (!IsPowerOfTwo(config.frame_size)) {
        throw std::invalid_argument("STFT_FRAME_SIZE must be a power of two");
    }

    const auto window = BuildHannWindow(config.frame_size);
    const std::size_t frame_count =
        mono_samples.size() <= config.frame_size
            ? 1
            : 1 + (mono_samples.size() - config.frame_size) / config.hop_size;

    std::vector<std::vector<float>> frames(frame_count, std::vector<float>(config.frame_size, 0.0f));
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const std::size_t base = frame * config.hop_size;
        for (std::size_t i = 0; i < config.frame_size; ++i) {
            const std::size_t sample_index = base + i;
            const float sample = sample_index < mono_samples.size() ? mono_samples[sample_index] : 0.0f;
            frames[frame][i] = sample * window[i];
        }
    }

    return ComputeFromWindowedFrames(frames, sample_rate, config);
}

StftResult ComputeStft(const std::vector<float>& mono_samples,
                       unsigned sample_rate,
                       const StftConfig& config) {
#if defined(USE_RVV_STFT) && defined(__riscv_vector)
    return ComputeStftRvv(mono_samples, sample_rate, config);
#else
    return ComputeStftGeneric(mono_samples, sample_rate, config);
#endif
}
