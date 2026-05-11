#include "fft.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(__riscv) && defined(__riscv_vector)
#include <riscv_vector.h>
#endif

namespace fft {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

bool IsPowerOfTwo(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

void BitReversePermute(std::vector<Complex>& data) {
    const std::size_t n = data.size();
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
}

void BitReversePermute(std::vector<double>& real, std::vector<double>& imag) {
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

void ScalarFftInPlace(std::vector<Complex>& data) {
    const std::size_t n = data.size();
    if (!IsPowerOfTwo(n)) {
        throw std::invalid_argument("FFT input size must be a power of two");
    }

    BitReversePermute(data);

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const Complex w_len(std::cos(angle), std::sin(angle));

        for (std::size_t i = 0; i < n; i += len) {
            Complex w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const Complex u = data[i + j];
                const Complex v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= w_len;
            }
        }
    }
}

#if defined(__riscv) && defined(__riscv_vector)
std::vector<Complex> RvvFft(const std::vector<double>& samples) {
    const std::size_t n = NextPowerOfTwo(samples.size());
    std::vector<double> real(n, 0.0);
    std::vector<double> imag(n, 0.0);
    std::copy(samples.begin(), samples.end(), real.begin());

    BitReversePermute(real, imag);

    std::vector<double> twiddle_real(n / 2);
    std::vector<double> twiddle_imag(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) {
        const double angle = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(n);
        twiddle_real[k] = std::cos(angle);
        twiddle_imag[k] = std::sin(angle);
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half = len / 2;
        const std::size_t step = n / len;
        const ptrdiff_t twiddle_stride = static_cast<ptrdiff_t>(step * sizeof(double));

        for (std::size_t i = 0; i < n; i += len) {
            for (std::size_t j = 0; j < half;) {
                const std::size_t vl = vsetvl_e64m1(half - j);

                vfloat64m1_t upper_real = vle64_v_f64m1(real.data() + i + j, vl);
                vfloat64m1_t upper_imag = vle64_v_f64m1(imag.data() + i + j, vl);
                vfloat64m1_t lower_real = vle64_v_f64m1(real.data() + i + j + half, vl);
                vfloat64m1_t lower_imag = vle64_v_f64m1(imag.data() + i + j + half, vl);
                vfloat64m1_t wr = vlse64_v_f64m1(twiddle_real.data() + j * step, twiddle_stride, vl);
                vfloat64m1_t wi = vlse64_v_f64m1(twiddle_imag.data() + j * step, twiddle_stride, vl);

                vfloat64m1_t temp_real = vfsub_vv_f64m1(
                    vfmul_vv_f64m1(lower_real, wr, vl),
                    vfmul_vv_f64m1(lower_imag, wi, vl),
                    vl);
                vfloat64m1_t temp_imag = vfadd_vv_f64m1(
                    vfmul_vv_f64m1(lower_real, wi, vl),
                    vfmul_vv_f64m1(lower_imag, wr, vl),
                    vl);

                vse64_v_f64m1(real.data() + i + j, vfadd_vv_f64m1(upper_real, temp_real, vl), vl);
                vse64_v_f64m1(imag.data() + i + j, vfadd_vv_f64m1(upper_imag, temp_imag, vl), vl);
                vse64_v_f64m1(real.data() + i + j + half, vfsub_vv_f64m1(upper_real, temp_real, vl), vl);
                vse64_v_f64m1(imag.data() + i + j + half, vfsub_vv_f64m1(upper_imag, temp_imag, vl), vl);

                j += vl;
            }
        }
    }

    std::vector<Complex> spectrum(n);
    for (std::size_t i = 0; i < n; ++i) {
        spectrum[i] = Complex(real[i], imag[i]);
    }
    return spectrum;
}
#endif

}  // namespace

bool IsRiscVTarget() {
#if defined(__riscv)
    return true;
#else
    return false;
#endif
}

std::size_t NextPowerOfTwo(std::size_t value) {
    if (value <= 1) {
        return 1;
    }
    --value;
    for (std::size_t shift = 1; shift < sizeof(std::size_t) * 8; shift <<= 1) {
        value |= value >> shift;
    }
    return value + 1;
}

std::vector<Complex> ComputeFft(const std::vector<double>& samples) {
    if (samples.empty()) {
        throw std::invalid_argument("Cannot compute FFT for empty audio");
    }

#if defined(__riscv) && defined(__riscv_vector)
    return RvvFft(samples);
#else
    std::vector<Complex> data(NextPowerOfTwo(samples.size()), Complex(0.0, 0.0));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        data[i] = Complex(samples[i], 0.0);
    }

    ScalarFftInPlace(data);

    return data;
#endif
}

double MagnitudeAtFrequency(const std::vector<Complex>& spectrum,
                            double sample_rate,
                            double target_frequency,
                            std::size_t* selected_bin,
                            double* selected_frequency) {
    if (spectrum.empty() || sample_rate <= 0.0 || target_frequency < 0.0) {
        throw std::invalid_argument("Invalid spectrum or frequency parameters");
    }

    const std::size_t n = spectrum.size();
    const std::size_t nyquist_bin = n / 2;
    std::size_t bin = static_cast<std::size_t>(
        std::llround(target_frequency * static_cast<double>(n) / sample_rate));
    bin = std::min(bin, nyquist_bin);

    if (selected_bin != nullptr) {
        *selected_bin = bin;
    }
    if (selected_frequency != nullptr) {
        *selected_frequency = static_cast<double>(bin) * sample_rate / static_cast<double>(n);
    }

    return std::abs(spectrum[bin]);
}

}  // namespace fft
