#include <stft.h>

#if defined(USE_RVV_STFT) && defined(__riscv_vector)
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include <riscv_vector.h>

#if defined(__riscv_v_intrinsic)
#define RVV_VSETVL_E32M1 __riscv_vsetvl_e32m1
#define RVV_VLE32_V_F32M1 __riscv_vle32_v_f32m1
#define RVV_VSE32_V_F32M1 __riscv_vse32_v_f32m1
#define RVV_VFMUL_VF_F32M1 __riscv_vfmul_vf_f32m1
#define RVV_VFMUL_VV_F32M1 __riscv_vfmul_vv_f32m1
#define RVV_VFADD_VV_F32M1 __riscv_vfadd_vv_f32m1
#define RVV_VFSUB_VV_F32M1 __riscv_vfsub_vv_f32m1
#define RVV_VFSQRT_V_F32M1 __riscv_vfsqrt_v_f32m1
#else
#define RVV_VSETVL_E32M1 vsetvl_e32m1
#define RVV_VLE32_V_F32M1 vle32_v_f32m1
#define RVV_VSE32_V_F32M1 vse32_v_f32m1
#define RVV_VFMUL_VF_F32M1 vfmul_vf_f32m1
#define RVV_VFMUL_VV_F32M1 vfmul_vv_f32m1
#define RVV_VFADD_VV_F32M1 vfadd_vv_f32m1
#define RVV_VFSUB_VV_F32M1 vfsub_vv_f32m1
#define RVV_VFSQRT_V_F32M1 vfsqrt_v_f32m1
#endif

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::size_t ReverseBits(std::size_t value, unsigned bits) {
    // 功能：反转索引低位；参数：原索引和有效位数；返回：位倒序索引。
    std::size_t reversed = 0;
    for (unsigned i = 0; i < bits; ++i) {
        reversed = (reversed << 1) | (value & 1U);
        value >>= 1;
    }
    return reversed;
}

unsigned CountBits(std::size_t value) {
    // 功能：计算 FFT 索引位数；参数：帧长；返回：位数。
    unsigned bits = 0;
    while (value > 1) {
        value >>= 1;
        ++bits;
    }
    return bits;
}

void SwapColumnsRvv(std::vector<float>& values,
                    std::size_t left,
                    std::size_t right,
                    std::size_t frame_count) {
    // 功能：交换转置帧矩阵的两列；核心：RVV 批量交换多帧 lane；参数：矩阵、列索引和帧数；返回：无。
    for (std::size_t frame = 0; frame < frame_count;) {
        const std::size_t vl = RVV_VSETVL_E32M1(frame_count - frame);
        vfloat32m1_t left_values = RVV_VLE32_V_F32M1(values.data() + left * frame_count + frame, vl);
        vfloat32m1_t right_values = RVV_VLE32_V_F32M1(values.data() + right * frame_count + frame, vl);
        RVV_VSE32_V_F32M1(values.data() + left * frame_count + frame, right_values, vl);
        RVV_VSE32_V_F32M1(values.data() + right * frame_count + frame, left_values, vl);
        frame += vl;
    }
}

}  // namespace

/*
 * 功能：使用跨帧 RVV 并行完成 STFT。
 * 核心：数据按 [sample_index][frame_index] 转置存储，使连续 lane 对应不同音频帧。
 * 参数：mono_samples 为单声道 PCM；sample_rate 为采样率；config.frame_size 必须为 2 的幂。
 * 返回：包含频谱图、梅尔频谱图和目标频率曲线的完整 StftResult。
 * 平台：仅在 USE_RVV_STFT 与 __riscv_vector 同时定义时编译。
 */
StftResult ComputeStftMultiRvv(const std::vector<float>& mono_samples,
                               unsigned sample_rate,
                               const StftConfig& config) {
    if (mono_samples.empty() || sample_rate == 0 || config.frame_size == 0 || config.hop_size == 0) {
        throw std::invalid_argument("invalid audio or STFT parameters");
    }

    const auto window = BuildHannWindow(config.frame_size);
    const std::size_t frame_count =
        mono_samples.size() <= config.frame_size
            ? 1
            : 1 + (mono_samples.size() - config.frame_size) / config.hop_size;
    const std::size_t freq_bins = config.frame_size / 2 + 1;

    // 转置布局：[sample][frame]。相邻 lane 读取不同帧的同一采样点，适合多帧并行。
    std::vector<float> real(config.frame_size * frame_count, 0.0f);
    std::vector<float> imag(config.frame_size * frame_count, 0.0f);
    std::vector<float> column(frame_count, 0.0f);
    for (std::size_t sample = 0; sample < config.frame_size; ++sample) {
        for (std::size_t frame = 0; frame < frame_count; ++frame) {
            const std::size_t source = frame * config.hop_size + sample;
            column[frame] = source < mono_samples.size() ? mono_samples[source] : 0.0f;
        }
        for (std::size_t frame = 0; frame < frame_count;) {
            // vl 是本轮实际处理帧数，由硬件 VLEN 决定。
            const std::size_t vl = RVV_VSETVL_E32M1(frame_count - frame);
            vfloat32m1_t samples = RVV_VLE32_V_F32M1(column.data() + frame, vl);
            vfloat32m1_t windowed = RVV_VFMUL_VF_F32M1(samples, window[sample], vl);
            RVV_VSE32_V_F32M1(real.data() + sample * frame_count + frame, windowed, vl);
            frame += vl;
        }
    }

    // 位倒序仍按 FFT bin 计算索引，但交换整列帧数据，交换本身由 RVV 完成。
    const unsigned bits = CountBits(config.frame_size);
    for (std::size_t sample = 0; sample < config.frame_size; ++sample) {
        const std::size_t reversed = ReverseBits(sample, bits);
        if (reversed > sample) {
            SwapColumnsRvv(real, sample, reversed, frame_count);
            SwapColumnsRvv(imag, sample, reversed, frame_count);
        }
    }

    // Cooley-Tukey radix-2 DIT FFT。每个 lane 对应一帧，旋转因子在帧之间复用。
    for (std::size_t len = 2; len <= config.frame_size; len <<= 1) {
        const std::size_t half = len / 2;
        for (std::size_t offset = 0; offset < config.frame_size; offset += len) {
            for (std::size_t k = 0; k < half; ++k) {
                const float angle = -2.0f * kPi * static_cast<float>(k) / static_cast<float>(len);
                const float wr = std::cos(angle);
                const float wi = std::sin(angle);
                const std::size_t upper_index = (offset + k) * frame_count;
                const std::size_t lower_index = (offset + k + half) * frame_count;

                for (std::size_t frame = 0; frame < frame_count;) {
                    const std::size_t vl = RVV_VSETVL_E32M1(frame_count - frame);
                    vfloat32m1_t upper_real = RVV_VLE32_V_F32M1(real.data() + upper_index + frame, vl);
                    vfloat32m1_t upper_imag = RVV_VLE32_V_F32M1(imag.data() + upper_index + frame, vl);
                    vfloat32m1_t lower_real = RVV_VLE32_V_F32M1(real.data() + lower_index + frame, vl);
                    vfloat32m1_t lower_imag = RVV_VLE32_V_F32M1(imag.data() + lower_index + frame, vl);
                    vfloat32m1_t temp_real = RVV_VFSUB_VV_F32M1(
                        RVV_VFMUL_VF_F32M1(lower_real, wr, vl),
                        RVV_VFMUL_VF_F32M1(lower_imag, wi, vl),
                        vl);
                    vfloat32m1_t temp_imag = RVV_VFADD_VV_F32M1(
                        RVV_VFMUL_VF_F32M1(lower_real, wi, vl),
                        RVV_VFMUL_VF_F32M1(lower_imag, wr, vl),
                        vl);
                    RVV_VSE32_V_F32M1(real.data() + upper_index + frame,
                                      RVV_VFADD_VV_F32M1(upper_real, temp_real, vl),
                                      vl);
                    RVV_VSE32_V_F32M1(imag.data() + upper_index + frame,
                                      RVV_VFADD_VV_F32M1(upper_imag, temp_imag, vl),
                                      vl);
                    RVV_VSE32_V_F32M1(real.data() + lower_index + frame,
                                      RVV_VFSUB_VV_F32M1(upper_real, temp_real, vl),
                                      vl);
                    RVV_VSE32_V_F32M1(imag.data() + lower_index + frame,
                                      RVV_VFSUB_VV_F32M1(upper_imag, temp_imag, vl),
                                      vl);
                    frame += vl;
                }
            }
        }
    }

    StftResult result;
    result.sample_rate = sample_rate;
    result.frame_size = config.frame_size;
    result.hop_size = config.hop_size;
    result.times.resize(frame_count);
    result.frequencies.resize(freq_bins);
    result.spectrogram_db.assign(frame_count, std::vector<float>(freq_bins, 0.0f));
    const float inv_frame_size = 1.0f / static_cast<float>(config.frame_size);

    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        result.times[frame] = static_cast<float>(frame * config.hop_size) / static_cast<float>(sample_rate);
    }
    for (std::size_t bin = 0; bin < freq_bins; ++bin) {
        result.frequencies[bin] = static_cast<float>(bin) * static_cast<float>(sample_rate) /
                                  static_cast<float>(config.frame_size);
        for (std::size_t frame = 0; frame < frame_count;) {
            const std::size_t vl = RVV_VSETVL_E32M1(frame_count - frame);
            vfloat32m1_t re = RVV_VLE32_V_F32M1(real.data() + bin * frame_count + frame, vl);
            vfloat32m1_t im = RVV_VLE32_V_F32M1(imag.data() + bin * frame_count + frame, vl);
            vfloat32m1_t sum = RVV_VFADD_VV_F32M1(
                RVV_VFMUL_VV_F32M1(re, re, vl),
                RVV_VFMUL_VV_F32M1(im, im, vl),
                vl);
            vfloat32m1_t magnitude = RVV_VFMUL_VF_F32M1(RVV_VFSQRT_V_F32M1(sum, vl), inv_frame_size, vl);
            RVV_VSE32_V_F32M1(column.data() + frame, magnitude, vl);
            frame += vl;
        }
        for (std::size_t frame = 0; frame < frame_count; ++frame) {
            result.spectrogram_db[frame][bin] = 20.0f * std::log10(std::max(column[frame], 1.0e-6f));
        }
    }

    const std::size_t target_bin = std::min<std::size_t>(
        freq_bins - 1,
        static_cast<std::size_t>(std::lround(config.target_frequency *
                                            static_cast<float>(config.frame_size) /
                                            static_cast<float>(sample_rate))));
    result.target_frequency_db.resize(frame_count);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        result.target_frequency_db[frame] = result.spectrogram_db[frame][target_bin];
    }
    result.mel_spectrogram_db = BuildMelSpectrogramRvv(result.spectrogram_db,
                                                       sample_rate,
                                                       config.frame_size,
                                                       config.mel_bands);
    return result;
}

#endif
