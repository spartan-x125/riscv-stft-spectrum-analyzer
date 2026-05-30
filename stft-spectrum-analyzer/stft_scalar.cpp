#include "stft.h"
#include "stft_adaptive.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinDb = -120.0f;

bool IsPowerOfTwo(std::size_t value) {
    // 功能：判断 FFT 长度是否合法；参数：待判断整数；返回：为 2 的幂时为 true。
    return value != 0 && (value & (value - 1)) == 0;
}

float HzToMel(float hz) {
    // 功能：把 Hz 映射到 Mel；参数：频率 Hz；返回：Mel 频率。
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float MelToHz(float mel) {
    // 功能：把 Mel 映射回 Hz；参数：Mel 频率；返回：Hz。
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

void FftInPlace(std::vector<std::complex<float>>& data) {
    /*
     * 功能：原地计算单帧复数 FFT。
     * 核心：使用 Cooley-Tukey radix-2 DIT，先位倒序，再逐级执行蝶形运算。
     * 参数：data 长度必须为 2 的幂；输入为时域复数序列，输出为频域复数序列。
     * 返回：无返回值，结果直接写回 data。
     * 平台：始终编译，作为 x86/ARM 和无 RVV 平台的回退实现。
     */
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
    /*
     * 功能：生成 Hann 窗。
     * 核心：w[n] = 0.5 - 0.5*cos(2*pi*n/(N-1))，用于降低频谱泄漏。
     * 参数：frame_size 为窗口长度。
     * 返回：长度为 frame_size 的浮点窗函数数组。
     */
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
    /*
     * 功能：把线性频率谱映射为梅尔频谱。
     * 核心：为每个 Mel band 构造三角形滤波器，并对落入滤波器范围的频点加权平均。
     * 参数：spectrogram_db 按 [frame][bin] 存储；sample_rate 为采样率；frame_size 为 FFT 点数。
     * 返回：按 [frame][mel_band] 存储的梅尔频谱。
     */
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
    /*
     * 功能：从已经加窗的帧矩阵生成完整 STFT 结果。
     * 核心：逐帧 FFT，计算幅度谱和 dB，再生成梅尔频谱与目标频率时间序列。
     * 参数：frames 按 [frame][sample] 存储；config.frame_size 必须为 2 的幂。
     * 返回：包含频率轴、时间轴、频谱图、梅尔频谱图和目标频率曲线的 StftResult。
     */
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

bool IsRiscVPlatform() {
    // 功能：判断编译目标平台；返回：RISC-V 时为 true；平台：由 __riscv 宏决定。
#if defined(__riscv)
    return true;
#else
    return false;
#endif
}

bool IsRvvStftEnabled() {
    // 功能：判断 RVV STFT 是否启用；返回：条件编译宏同时满足时为 true。
#if defined(USE_RVV_STFT) && defined(__riscv_vector)
    return true;
#else
    return false;
#endif
}

std::string PlatformName() {
    // 功能：生成平台日志文本；返回："RISC-V" 或 "Other"。
    return IsRiscVPlatform() ? "RISC-V" : "Other";
}

StftResult ComputeStftGeneric(const std::vector<float>& mono_samples,
                              unsigned sample_rate,
                              const StftConfig& config) {
    /*
     * 功能：执行标量 STFT。
     * 核心：按 hop_size 分帧，逐样本乘 Hann 窗，再进入公共频谱计算函数。
     * 参数：mono_samples 为单声道 PCM；sample_rate 为采样率；config 为 STFT 配置。
     * 返回：完整 StftResult。
     */
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
    /*
     * 功能：STFT 统一入口。
     * 核心：RVV 平台根据帧数和帧长选择帧内或多帧向量化；其它平台回退到标量实现。
     * 参数：与 ComputeStftGeneric 相同。
     * 返回：完整 StftResult，调用方不需要关心平台差异。
     */
#if defined(USE_RVV_STFT) && defined(__riscv_vector)
    const std::size_t frame_count =
        mono_samples.size() <= config.frame_size
            ? 1
            : 1 + (mono_samples.size() - config.frame_size) / config.hop_size;
    switch (SelectStftVectorizationMode(config.frame_size, frame_count)) {
        case StftVectorizationMode::MultiFrame:
            return ComputeStftMultiRvv(mono_samples, sample_rate, config);
        case StftVectorizationMode::IntraFrame:
            return ComputeStftIntraRvv(mono_samples, sample_rate, config);
        case StftVectorizationMode::Scalar:
            break;
    }
#else
    (void)SelectStftVectorizationMode(config.frame_size, 0);
#endif
    return ComputeStftGeneric(mono_samples, sample_rate, config);
}
