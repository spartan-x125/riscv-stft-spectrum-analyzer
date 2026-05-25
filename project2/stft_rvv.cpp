#include "stft.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(USE_RVV_STFT) && defined(__riscv_vector)
#include <riscv_vector.h>

#if defined(__riscv_v_intrinsic)
#define RVV_VSETVL_E32M1 __riscv_vsetvl_e32m1
#define RVV_VLE32_V_F32M1 __riscv_vle32_v_f32m1
#define RVV_VFMUL_VV_F32M1 __riscv_vfmul_vv_f32m1
#define RVV_VSE32_V_F32M1 __riscv_vse32_v_f32m1
#else
#define RVV_VSETVL_E32M1 vsetvl_e32m1
#define RVV_VLE32_V_F32M1 vle32_v_f32m1
#define RVV_VFMUL_VV_F32M1 vfmul_vv_f32m1
#define RVV_VSE32_V_F32M1 vse32_v_f32m1
#endif

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool IsPowerOfTwo(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

std::vector<float> BuildHannWindow(std::size_t frame_size) {
    std::vector<float> window(frame_size, 1.0f);
    for (std::size_t i = 0; i < frame_size; ++i) {
        window[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) /
                                           static_cast<float>(frame_size - 1));
    }
    return window;
}

}  // namespace

StftResult ComputeStftRvv(const std::vector<float>& mono_samples,
                          unsigned sample_rate,
                          const StftConfig& config) {
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

    std::vector<float> windowed_samples(frame_count * config.frame_size, 0.0f);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const std::size_t base = frame * config.hop_size;
        std::size_t i = 0;
        for (; i < config.frame_size && base + i < mono_samples.size();) {
            const std::size_t vl = RVV_VSETVL_E32M1(config.frame_size - i);
            vfloat32m1_t sample_vec = RVV_VLE32_V_F32M1(mono_samples.data() + base + i, vl);
            vfloat32m1_t window_vec = RVV_VLE32_V_F32M1(window.data() + i, vl);
            vfloat32m1_t out_vec = RVV_VFMUL_VV_F32M1(sample_vec, window_vec, vl);
            RVV_VSE32_V_F32M1(windowed_samples.data() + frame * config.frame_size + i, out_vec, vl);
            i += vl;
        }
    }

    std::vector<float> flattened;
    flattened.reserve(frame_count * config.hop_size + config.frame_size);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        flattened.insert(flattened.end(),
                         windowed_samples.begin() + static_cast<std::ptrdiff_t>(frame * config.frame_size),
                         windowed_samples.begin() + static_cast<std::ptrdiff_t>((frame + 1) * config.frame_size));
    }

    return ComputeStftGeneric(mono_samples, sample_rate, config);
}

#endif
