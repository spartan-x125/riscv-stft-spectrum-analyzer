#include <image_writer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>

namespace {

uint32_t Crc32(const unsigned char* data, std::size_t size) {
    // 功能：计算 PNG 块 CRC32；参数：字节数组和长度；返回：校验值。
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & static_cast<uint32_t>(-(static_cast<int>(crc & 1u))));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t Adler32(const std::vector<unsigned char>& data) {
    // 功能：计算 zlib Adler32；参数：原始字节；返回：校验值。
    constexpr uint32_t mod = 65521u;
    uint32_t a = 1;
    uint32_t b = 0;
    for (unsigned char byte : data) {
        a = (a + byte) % mod;
        b = (b + a) % mod;
    }
    return (b << 16) | a;
}

void WriteU32(std::ostream& out, uint32_t value) {
    // 功能：向流写入大端 32 位整数；参数：输出流和值；返回：无。
    const unsigned char bytes[] = {
        static_cast<unsigned char>((value >> 24) & 0xFF),
        static_cast<unsigned char>((value >> 16) & 0xFF),
        static_cast<unsigned char>((value >> 8) & 0xFF),
        static_cast<unsigned char>(value & 0xFF),
    };
    out.write(reinterpret_cast<const char*>(bytes), 4);
}

void AppendU32(std::vector<unsigned char>& data, uint32_t value) {
    // 功能：向缓冲区追加大端整数；参数：目标数组和值；返回：无。
    data.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
    data.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
    data.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    data.push_back(static_cast<unsigned char>(value & 0xFF));
}

void WriteChunk(std::ostream& out, const char type[4], const std::vector<unsigned char>& payload) {
    // 功能：写出 PNG 数据块；核心：写长度、类型、负载和 CRC；参数：输出流、块类型和负载；返回：无。
    WriteU32(out, static_cast<uint32_t>(payload.size()));
    out.write(type, 4);
    if (!payload.empty()) {
        out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    }

    std::vector<unsigned char> crc_input(4 + payload.size());
    std::copy(type, type + 4, crc_input.begin());
    std::copy(payload.begin(), payload.end(), crc_input.begin() + 4);
    WriteU32(out, Crc32(crc_input.data(), crc_input.size()));
}

std::vector<unsigned char> BuildZlibStoredStream(const std::vector<unsigned char>& raw) {
    // 功能：构造无压缩 zlib 流；参数：原始扫描线；返回：可写入 IDAT 的字节流。
    std::vector<unsigned char> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);

    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t remaining = raw.size() - offset;
        const uint16_t block_size = static_cast<uint16_t>(std::min<std::size_t>(remaining, 65535));
        const bool final_block = offset + block_size == raw.size();
        zlib.push_back(final_block ? 0x01 : 0x00);
        zlib.push_back(static_cast<unsigned char>(block_size & 0xFF));
        zlib.push_back(static_cast<unsigned char>((block_size >> 8) & 0xFF));
        const uint16_t nlen = static_cast<uint16_t>(~block_size);
        zlib.push_back(static_cast<unsigned char>(nlen & 0xFF));
        zlib.push_back(static_cast<unsigned char>((nlen >> 8) & 0xFF));
        zlib.insert(zlib.end(),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + block_size));
        offset += block_size;
    }

    AppendU32(zlib, Adler32(raw));
    return zlib;
}

std::array<unsigned char, 3> ColorMap(float normalized) {
    // 功能：把归一化幅度映射为 RGB；参数：[0,1] 附近数值；返回：三通道颜色。
    const float v = std::clamp(normalized, 0.0f, 1.0f);
    const float r = std::clamp(1.5f * v - 0.25f, 0.0f, 1.0f);
    const float g = std::clamp(1.5f - std::abs(2.0f * v - 1.0f) * 1.5f, 0.0f, 1.0f);
    const float b = std::clamp(1.25f - 1.5f * v, 0.0f, 1.0f);
    return {
        static_cast<unsigned char>(r * 255.0f),
        static_cast<unsigned char>(g * 255.0f),
        static_cast<unsigned char>(b * 255.0f),
    };
}

}  // namespace

bool WritePngRgb(const std::filesystem::path& path,
                 int width,
                 int height,
                 const std::vector<unsigned char>& rgb) {
    // 功能：写出 RGB PNG；核心：构造 IHDR、IDAT、IEND；参数：路径、尺寸和 RGB 缓冲区；返回：成功标志。
    if (width <= 0 || height <= 0 || rgb.size() != static_cast<std::size_t>(width * height * 3)) {
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    const unsigned char signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    out.write(reinterpret_cast<const char*>(signature), sizeof(signature));

    std::vector<unsigned char> ihdr;
    AppendU32(ihdr, static_cast<uint32_t>(width));
    AppendU32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    WriteChunk(out, "IHDR", ihdr);

    std::vector<unsigned char> raw;
    raw.reserve(static_cast<std::size_t>((width * 3 + 1) * height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto begin = rgb.begin() + static_cast<std::ptrdiff_t>(y * width * 3);
        raw.insert(raw.end(), begin, begin + width * 3);
    }
    WriteChunk(out, "IDAT", BuildZlibStoredStream(raw));
    WriteChunk(out, "IEND", {});
    return true;
}

void WriteSpectrogramPng(const std::filesystem::path& path,
                         const std::vector<std::vector<float>>& matrix_db) {
    // 功能：写出频谱图 PNG；核心：归一化 dB 矩阵并映射 RGB；参数：路径和 [frame][bin] 矩阵；返回：无。
#if defined(USE_RVV_STFT) && defined(__riscv_vector)
    WriteSpectrogramPngRvv(path, matrix_db);
    return;
#endif

    if (matrix_db.empty() || matrix_db.front().empty()) {
        throw std::runtime_error("empty spectrogram matrix");
    }

    const int width = static_cast<int>(matrix_db.size());
    const int height = static_cast<int>(matrix_db.front().size());
    float min_value = 1.0e30f;
    float max_value = -1.0e30f;
    for (const auto& row : matrix_db) {
        for (float value : row) {
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
        }
    }
    if (max_value <= min_value) {
        max_value = min_value + 1.0f;
    }

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3), 0);
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            const float value = matrix_db[static_cast<std::size_t>(x)][static_cast<std::size_t>(height - 1 - y)];
            const auto color = ColorMap((value - min_value) / (max_value - min_value));
            const std::size_t offset = static_cast<std::size_t>((y * width + x) * 3);
            pixels[offset + 0] = color[0];
            pixels[offset + 1] = color[1];
            pixels[offset + 2] = color[2];
        }
    }

    if (!WritePngRgb(path, width, height, pixels)) {
        throw std::runtime_error("failed to write PNG: " + path.string());
    }
}

void WriteTargetFrequencySvg(const std::filesystem::path& path,
                             const std::vector<float>& times,
                             const std::vector<float>& values_db,
                             float target_frequency) {
    // 功能：写出目标频率 SVG 曲线；参数：路径、时间轴、dB 幅度和目标 Hz；返回：无；RVV 平台批量计算坐标。
    if (times.empty() || values_db.empty() || times.size() != values_db.size()) {
        throw std::runtime_error("invalid target frequency curve");
    }

    constexpr int width = 1000;
    constexpr int height = 420;
    constexpr int left = 70;
    constexpr int right = 30;
    constexpr int top = 30;
    constexpr int bottom = 55;
    const int plot_width = width - left - right;
    const int plot_height = height - top - bottom;

    const auto [min_it, max_it] = std::minmax_element(values_db.begin(), values_db.end());
    float min_value = *min_it;
    float max_value = *max_it;
    if (max_value <= min_value) {
        max_value = min_value + 1.0f;
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to write SVG: " + path.string());
    }

    out << std::fixed << std::setprecision(2);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' ' << height << "\">\n";
    out << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    out << "<text x=\"" << width / 2 << "\" y=\"22\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"16\">"
        << target_frequency << " Hz target frequency amplitude</text>\n";
    out << "<line x1=\"" << left << "\" y1=\"" << top << "\" x2=\"" << left
        << "\" y2=\"" << top + plot_height << "\" stroke=\"black\"/>\n";
    out << "<line x1=\"" << left << "\" y1=\"" << top + plot_height << "\" x2=\""
        << left + plot_width << "\" y2=\"" << top + plot_height << "\" stroke=\"black\"/>\n";
    out << "<polyline fill=\"none\" stroke=\"#1f77b4\" stroke-width=\"1.5\" points=\"";
    std::vector<float> x_coordinates(values_db.size(), 0.0f);
    std::vector<float> y_coordinates(values_db.size(), 0.0f);
#if defined(USE_RVV_STFT) && defined(__riscv_vector)
    ComputeTargetFrequencyCoordinatesRvv(times,
                                         values_db,
                                         min_value,
                                         max_value,
                                         static_cast<float>(plot_width),
                                         static_cast<float>(plot_height),
                                         static_cast<float>(left),
                                         static_cast<float>(top + plot_height),
                                         x_coordinates,
                                         y_coordinates);
#else
    for (std::size_t i = 0; i < values_db.size(); ++i) {
        const float time_norm = times.back() > 0.0f ? times[i] / times.back() : 0.0f;
        const float value_norm = (values_db[i] - min_value) / (max_value - min_value);
        x_coordinates[i] = static_cast<float>(left) + time_norm * static_cast<float>(plot_width);
        y_coordinates[i] = static_cast<float>(top + plot_height) - value_norm * static_cast<float>(plot_height);
    }
#endif
    for (std::size_t i = 0; i < values_db.size(); ++i) {
        out << x_coordinates[i] << ',' << y_coordinates[i] << ' ';
    }
    out << "\"/>\n</svg>\n";
}
