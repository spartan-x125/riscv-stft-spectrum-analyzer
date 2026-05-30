#include "stft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>

#if defined(USE_RVV_STFT) && defined(__riscv_vector)
#include <riscv_vector.h>

#if defined(__riscv_v_intrinsic)
#define RVV_VSETVL_E32M1 __riscv_vsetvl_e32m1
#define RVV_VLE32_V_F32M1 __riscv_vle32_v_f32m1
#define RVV_VSE32_V_F32M1 __riscv_vse32_v_f32m1
#define RVV_VFMUL_VV_F32M1 __riscv_vfmul_vv_f32m1
#define RVV_VFMUL_VF_F32M1 __riscv_vfmul_vf_f32m1
#define RVV_VFADD_VV_F32M1 __riscv_vfadd_vv_f32m1
#define RVV_VFSUB_VV_F32M1 __riscv_vfsub_vv_f32m1
#define RVV_VFSQRT_V_F32M1 __riscv_vfsqrt_v_f32m1
#define RVV_VFMACC_VV_F32M1 __riscv_vfmacc_vv_f32m1
#define RVV_VFMV_V_F_F32M1 __riscv_vfmv_v_f_f32m1
#define RVV_VFREDUSUM_VS_F32M1 __riscv_vfredusum_vs_f32m1_f32m1
#define RVV_VFMV_F_S_F32M1 __riscv_vfmv_f_s_f32m1_f32
#else
#define RVV_VSETVL_E32M1 vsetvl_e32m1
#define RVV_VLE32_V_F32M1 vle32_v_f32m1
#define RVV_VSE32_V_F32M1 vse32_v_f32m1
#define RVV_VFMUL_VV_F32M1 vfmul_vv_f32m1
#define RVV_VFMUL_VF_F32M1 vfmul_vf_f32m1
#define RVV_VFADD_VV_F32M1 vfadd_vv_f32m1
#define RVV_VFSUB_VV_F32M1 vfsub_vv_f32m1
#define RVV_VFSQRT_V_F32M1 vfsqrt_v_f32m1
#define RVV_VFMACC_VV_F32M1 vfmacc_vv_f32m1
#define RVV_VFMV_V_F_F32M1 vfmv_v_f_f32m1
#define RVV_VFREDUSUM_VS_F32M1 vfredusum_vs_f32m1_f32m1
#define RVV_VFMV_F_S_F32M1 vfmv_f_s_f32m1_f32
#endif

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinDb = -120.0f;

float HzToMel(float hz) {
    // 功能：把 Hz 映射到 Mel；参数：Hz；返回：Mel。
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float MelToHz(float mel) {
    // 功能：把 Mel 映射回 Hz；参数：Mel；返回：Hz。
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

bool IsPowerOfTwo(std::size_t value) {
    // 功能：判断帧长是否为 2 的幂；参数：帧长；返回：布尔值。
    return value != 0 && (value & (value - 1)) == 0;
}

void BitReversePermute(std::vector<float>& real, std::vector<float>& imag) {
    // 功能：执行 FFT 位倒序；核心：交换拆分后的实部/虚部；参数：等长数组；返回：无。
    const std::size_t n = real.size();
    std::size_t j = 0;
    for (std::size_t i = 1; i < n; ++i) {
        std::size_t bit = n >> 1;
        while ((j & bit) != 0) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }
}

std::vector<float> MagnitudeDbRvv(const std::vector<float>& real,
                                  const std::vector<float>& imag,
                                  std::size_t freq_bins,
                                  std::size_t frame_size) {
    /*
     * 功能：使用 RVV 计算单帧幅度谱并转换为 dB。
     * 核心：向量化 sqrt(re*re + im*im) 与帧长归一化，log10 保留标量计算。
     * 参数：real/imag 为拆分频域数组；freq_bins 为正频率 bin 数；frame_size 为 FFT 点数。
     * 返回：长度为 freq_bins 的 dB 幅度谱。
     * 平台：仅在 USE_RVV_STFT 与 __riscv_vector 同时定义时编译。
     */
    std::vector<float> magnitude(freq_bins, 0.0f);
    std::vector<float> row(freq_bins, 0.0f);
    const float inv_frame_size = 1.0f / static_cast<float>(frame_size);

    for (std::size_t i = 0; i < freq_bins;) {
        const std::size_t vl = RVV_VSETVL_E32M1(freq_bins - i);
        vfloat32m1_t re = RVV_VLE32_V_F32M1(real.data() + i, vl);
        vfloat32m1_t im = RVV_VLE32_V_F32M1(imag.data() + i, vl);
        vfloat32m1_t re2 = RVV_VFMUL_VV_F32M1(re, re, vl);
        vfloat32m1_t im2 = RVV_VFMUL_VV_F32M1(im, im, vl);
        vfloat32m1_t sum = RVV_VFADD_VV_F32M1(re2, im2, vl);
        vfloat32m1_t mag = RVV_VFMUL_VF_F32M1(RVV_VFSQRT_V_F32M1(sum, vl), inv_frame_size, vl);
        RVV_VSE32_V_F32M1(magnitude.data() + i, mag, vl);
        i += vl;
    }

    for (std::size_t i = 0; i < freq_bins; ++i) {
        row[i] = 20.0f * std::log10(std::max(magnitude[i], 1.0e-6f));
    }
    return row;
}

}  // namespace

/*
 * 功能：使用 RVV 计算梅尔滤波器组加权平均。
 * 核心：三角滤波器权重连续存储，vfmacc 完成乘加，vfredusum 完成向量归约。
 * 参数：spectrogram_db 按 [frame][bin] 存储；frame_size 为 FFT 点数；mel_bands 为输出频带数。
 * 返回：按 [frame][mel_band] 存储的梅尔频谱。
 * 平台：仅在 USE_RVV_STFT 与 __riscv_vector 同时定义时编译。
 */
std::vector<std::vector<float>> BuildMelSpectrogramRvv(
    const std::vector<std::vector<float>>& spectrogram_db,
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
    std::vector<float> weights(freq_bins, 0.0f);

    for (std::size_t band = 0; band < mel_bands; ++band) {
        const float left_hz = MelToHz(min_mel + (max_mel - min_mel) * static_cast<float>(band) /
                                                   static_cast<float>(mel_bands + 1));
        const float center_hz = MelToHz(min_mel + (max_mel - min_mel) * static_cast<float>(band + 1) /
                                                     static_cast<float>(mel_bands + 1));
        const float right_hz = MelToHz(min_mel + (max_mel - min_mel) * static_cast<float>(band + 2) /
                                                    static_cast<float>(mel_bands + 1));
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
            weights[bin] = weight;
            weight_total += weight;
        }

        if (weight_total <= 0.0f) {
            continue;
        }
        for (std::size_t frame = 0; frame < spectrogram_db.size(); ++frame) {
            float weighted_sum = 0.0f;
            for (std::size_t bin = 0; bin < freq_bins;) {
                const std::size_t vl = RVV_VSETVL_E32M1(freq_bins - bin);
                vfloat32m1_t values = RVV_VLE32_V_F32M1(spectrogram_db[frame].data() + bin, vl);
                vfloat32m1_t weight_values = RVV_VLE32_V_F32M1(weights.data() + bin, vl);
                vfloat32m1_t acc = RVV_VFMV_V_F_F32M1(0.0f, vl);
                acc = RVV_VFMACC_VV_F32M1(acc, values, weight_values, vl);
                const vfloat32m1_t seed = RVV_VFMV_V_F_F32M1(0.0f, vl);
                const vfloat32m1_t reduced = RVV_VFREDUSUM_VS_F32M1(acc, seed, vl);
                weighted_sum += RVV_VFMV_F_S_F32M1(reduced);
                bin += vl;
            }
            mel[frame][band] = weighted_sum / weight_total;
        }
    }
    return mel;
}

StftResult ComputeFromWindowedFramesRvv(const std::vector<std::vector<float>>& frames,
                                        unsigned sample_rate,
                                        const StftConfig& config) {
    /*
     * 功能：从 RVV 加窗帧生成完整 STFT 结果。
     * 核心：逐帧调用 RVV FFT 和 RVV 幅度谱，再调用 RVV 梅尔滤波器组。
     * 参数：frames 按 [frame][sample] 存储；sample_rate 为采样率；config 为 STFT 配置。
     * 返回：完整 StftResult。
     * 平台：仅在 USE_RVV_STFT 与 __riscv_vector 同时定义时编译。
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
        FftInPlaceRvv(fft_buffer);

        std::vector<float> real(config.frame_size);
        std::vector<float> imag(config.frame_size);
        for (std::size_t i = 0; i < config.frame_size; ++i) {
            real[i] = fft_buffer[i].real();
            imag[i] = fft_buffer[i].imag();
        }
        auto row = MagnitudeDbRvv(real, imag, freq_bins, config.frame_size);
        result.times.push_back(static_cast<float>(frame_index * config.hop_size) /
                               static_cast<float>(sample_rate));
        result.target_frequency_db.push_back(row[target_bin]);
        result.spectrogram_db.push_back(std::move(row));
    }

    result.mel_spectrogram_db = BuildMelSpectrogramRvv(result.spectrogram_db,
                                                       sample_rate,
                                                       config.frame_size,
                                                       config.mel_bands);
    return result;
}

void FftInPlaceRvv(std::vector<std::complex<float>>& data) {
    /*
     * 功能：使用 RVV 原地计算单帧 FFT。
     * 核心：把交错复数拆为 real/imag 数组，保留标量位倒序，向量化最内层蝶形循环。
     * 参数：data 长度必须为 2 的幂。
     * 返回：无返回值，频域结果写回 data。
     * 平台：仅在 USE_RVV_STFT 与 __riscv_vector 同时定义时编译。
     */
    const std::size_t n = data.size();
    std::vector<float> real(n, 0.0f);
    std::vector<float> imag(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        real[i] = data[i].real();
        imag[i] = data[i].imag();
    }

    BitReversePermute(real, imag);

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half = len / 2;
        std::vector<float> twiddle_real(half);
        std::vector<float> twiddle_imag(half);
        for (std::size_t k = 0; k < half; ++k) {
            const float angle = -2.0f * kPi * static_cast<float>(k) / static_cast<float>(len);
            twiddle_real[k] = std::cos(angle);
            twiddle_imag[k] = std::sin(angle);
        }

        for (std::size_t offset = 0; offset < n; offset += len) {
            for (std::size_t k = 0; k < half;) {
                const std::size_t vl = RVV_VSETVL_E32M1(half - k);
                vfloat32m1_t upper_real = RVV_VLE32_V_F32M1(real.data() + offset + k, vl);
                vfloat32m1_t upper_imag = RVV_VLE32_V_F32M1(imag.data() + offset + k, vl);
                vfloat32m1_t lower_real = RVV_VLE32_V_F32M1(real.data() + offset + k + half, vl);
                vfloat32m1_t lower_imag = RVV_VLE32_V_F32M1(imag.data() + offset + k + half, vl);
                vfloat32m1_t wr = RVV_VLE32_V_F32M1(twiddle_real.data() + k, vl);
                vfloat32m1_t wi = RVV_VLE32_V_F32M1(twiddle_imag.data() + k, vl);

                vfloat32m1_t temp_real = RVV_VFSUB_VV_F32M1(
                    RVV_VFMUL_VV_F32M1(lower_real, wr, vl),
                    RVV_VFMUL_VV_F32M1(lower_imag, wi, vl),
                    vl);
                vfloat32m1_t temp_imag = RVV_VFADD_VV_F32M1(
                    RVV_VFMUL_VV_F32M1(lower_real, wi, vl),
                    RVV_VFMUL_VV_F32M1(lower_imag, wr, vl),
                    vl);

                RVV_VSE32_V_F32M1(real.data() + offset + k,
                                  RVV_VFADD_VV_F32M1(upper_real, temp_real, vl),
                                  vl);
                RVV_VSE32_V_F32M1(imag.data() + offset + k,
                                  RVV_VFADD_VV_F32M1(upper_imag, temp_imag, vl),
                                  vl);
                RVV_VSE32_V_F32M1(real.data() + offset + k + half,
                                  RVV_VFSUB_VV_F32M1(upper_real, temp_real, vl),
                                  vl);
                RVV_VSE32_V_F32M1(imag.data() + offset + k + half,
                                  RVV_VFSUB_VV_F32M1(upper_imag, temp_imag, vl),
                                  vl);
                k += vl;
            }
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        data[i] = {real[i], imag[i]};
    }
}

StftResult ComputeStftIntraRvv(const std::vector<float>& mono_samples,
                               unsigned sample_rate,
                               const StftConfig& config) {
    /*
     * 功能：使用帧内 RVV 并行完成 STFT。
     * 核心：每帧内部向量化 Hann 加窗、FFT 蝶形和幅度谱平方和开方。
     * 参数：mono_samples 为单声道 PCM；config.frame_size 必须为 2 的幂。
     * 返回：完整 StftResult。
     */
    if (mono_samples.empty()) {
        throw std::invalid_argument("audio samples are empty");
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
        std::size_t i = 0;
        for (; i < config.frame_size && base + i < mono_samples.size();) {
            const std::size_t vl = RVV_VSETVL_E32M1(config.frame_size - i);
            vfloat32m1_t sample_vec = RVV_VLE32_V_F32M1(mono_samples.data() + base + i, vl);
            vfloat32m1_t window_vec = RVV_VLE32_V_F32M1(window.data() + i, vl);
            vfloat32m1_t out_vec = RVV_VFMUL_VV_F32M1(sample_vec, window_vec, vl);
            RVV_VSE32_V_F32M1(frames[frame].data() + i, out_vec, vl);
            i += vl;
        }
    }

    return ComputeFromWindowedFramesRvv(frames, sample_rate, config);
}

#endif
