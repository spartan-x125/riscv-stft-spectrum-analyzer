#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace fft {

using Complex = std::complex<double>;

bool IsRiscVTarget();
std::size_t NextPowerOfTwo(std::size_t value);
std::vector<Complex> ComputeFft(const std::vector<double>& samples);
double MagnitudeAtFrequency(const std::vector<Complex>& spectrum,
                            double sample_rate,
                            double target_frequency,
                            std::size_t* selected_bin = nullptr,
                            double* selected_frequency = nullptr);

}  // namespace fft
