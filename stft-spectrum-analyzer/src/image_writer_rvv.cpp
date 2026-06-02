#include <image_writer.h>

#if defined(USE_RVV_STFT) && defined(__riscv_vector)
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

#include <riscv_vector.h>

#if defined(__riscv_v_intrinsic)
#define RVV_VSETVL_E32M1 __riscv_vsetvl_e32m1
#define RVV_VLE32_V_F32M1 __riscv_vle32_v_f32m1
#define RVV_VSE32_V_F32M1 __riscv_vse32_v_f32m1
#define RVV_VFMUL_VF_F32M1 __riscv_vfmul_vf_f32m1
#define RVV_VFSUB_VF_F32M1 __riscv_vfsub_vf_f32m1
#define RVV_VFADD_VF_F32M1 __riscv_vfadd_vf_f32m1
#define RVV_VFRSUB_VF_F32M1 __riscv_vfrsub_vf_f32m1
#define RVV_VFMIN_VF_F32M1 __riscv_vfmin_vf_f32m1
#define RVV_VFMAX_VF_F32M1 __riscv_vfmax_vf_f32m1
#define RVV_VFABS_V_F32M1 __riscv_vfabs_v_f32m1
#define RVV_VFMV_V_F_F32M1 __riscv_vfmv_v_f_f32m1
#define RVV_VFREDMIN_VS_F32M1 __riscv_vfredmin_vs_f32m1_f32m1
#define RVV_VFREDMAX_VS_F32M1 __riscv_vfredmax_vs_f32m1_f32m1
#define RVV_VFMV_F_S_F32M1 __riscv_vfmv_f_s_f32m1_f32
#else
#define RVV_VSETVL_E32M1 vsetvl_e32m1
#define RVV_VLE32_V_F32M1 vle32_v_f32m1
#define RVV_VSE32_V_F32M1 vse32_v_f32m1
#define RVV_VFMUL_VF_F32M1 vfmul_vf_f32m1
#define RVV_VFSUB_VF_F32M1 vfsub_vf_f32m1
#define RVV_VFADD_VF_F32M1 vfadd_vf_f32m1
#define RVV_VFRSUB_VF_F32M1 vfrsub_vf_f32m1
#define RVV_VFMIN_VF_F32M1 vfmin_vf_f32m1
#define RVV_VFMAX_VF_F32M1 vfmax_vf_f32m1
#define RVV_VFABS_V_F32M1 vfabs_v_f32m1
#define RVV_VFMV_V_F_F32M1 vfmv_v_f_f32m1
#define RVV_VFREDMIN_VS_F32M1 vfredmin_vs_f32m1_f32m1
#define RVV_VFREDMAX_VS_F32M1 vfredmax_vs_f32m1_f32m1
#define RVV_VFMV_F_S_F32M1 vfmv_f_s_f32m1_f32
#endif

namespace {

vfloat32m1_t ClampUnit(vfloat32m1_t values, std::size_t vl) {
    // 功能：把向量限制到 [0,1]；参数：RVV 向量和有效 lane 数；返回：裁剪后的向量；平台：仅 RVV。
    return RVV_VFMIN_VF_F32M1(RVV_VFMAX_VF_F32M1(values, 0.0f, vl), 1.0f, vl);
}

}  // namespace

void WriteSpectrogramPngRvv(const std::filesystem::path& path,
                            const std::vector<std::vector<float>>& matrix_db) {
    /*
     * 功能：使用 RVV 写出频谱图 PNG。
     * 核心：向量归约 min/max，批量归一化并计算 RGB 分段映射，最后复用 PNG 编码器。
     * 参数：path 为输出路径；matrix_db 按 [frame][bin] 存储。
     * 返回：无，失败时抛出异常。
     * 平台：仅在 USE_RVV_STFT 与 __riscv_vector 同时定义时编译。
     */
    if (matrix_db.empty() || matrix_db.front().empty()) {
        throw std::runtime_error("empty spectrogram matrix");
    }

    const int width = static_cast<int>(matrix_db.size());
    const int height = static_cast<int>(matrix_db.front().size());
    float min_value = 1.0e30f;
    float max_value = -1.0e30f;
    for (const auto& row : matrix_db) {
        for (std::size_t y = 0; y < row.size();) {
            const std::size_t vl = RVV_VSETVL_E32M1(row.size() - y);
            vfloat32m1_t values = RVV_VLE32_V_F32M1(row.data() + y, vl);
            const vfloat32m1_t min_seed = RVV_VFMV_V_F_F32M1(min_value, vl);
            const vfloat32m1_t max_seed = RVV_VFMV_V_F_F32M1(max_value, vl);
            min_value = std::min(min_value, RVV_VFMV_F_S_F32M1(RVV_VFREDMIN_VS_F32M1(values, min_seed, vl)));
            max_value = std::max(max_value, RVV_VFMV_F_S_F32M1(RVV_VFREDMAX_VS_F32M1(values, max_seed, vl)));
            y += vl;
        }
    }
    if (max_value <= min_value) {
        max_value = min_value + 1.0f;
    }

    const float inv_range = 1.0f / (max_value - min_value);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3), 0);
    std::vector<float> normalized(static_cast<std::size_t>(height), 0.0f);
    std::vector<float> red(static_cast<std::size_t>(height), 0.0f);
    std::vector<float> green(static_cast<std::size_t>(height), 0.0f);
    std::vector<float> blue(static_cast<std::size_t>(height), 0.0f);

    for (int x = 0; x < width; ++x) {
        std::size_t y = 0;
        for (; y < static_cast<std::size_t>(height);) {
            const std::size_t vl = RVV_VSETVL_E32M1(static_cast<std::size_t>(height) - y);
            vfloat32m1_t values = RVV_VLE32_V_F32M1(matrix_db[static_cast<std::size_t>(x)].data() + y, vl);
            vfloat32m1_t shifted = RVV_VFSUB_VF_F32M1(values, min_value, vl);
            vfloat32m1_t norm = ClampUnit(RVV_VFMUL_VF_F32M1(shifted, inv_range, vl), vl);
            vfloat32m1_t r = ClampUnit(RVV_VFSUB_VF_F32M1(RVV_VFMUL_VF_F32M1(norm, 1.5f, vl), 0.25f, vl), vl);
            vfloat32m1_t distance = RVV_VFABS_V_F32M1(
                RVV_VFSUB_VF_F32M1(RVV_VFMUL_VF_F32M1(norm, 2.0f, vl), 1.0f, vl),
                vl);
            vfloat32m1_t g = ClampUnit(RVV_VFRSUB_VF_F32M1(RVV_VFMUL_VF_F32M1(distance, 1.5f, vl), 1.5f, vl), vl);
            vfloat32m1_t b = ClampUnit(RVV_VFRSUB_VF_F32M1(RVV_VFMUL_VF_F32M1(norm, 1.5f, vl), 1.25f, vl), vl);
            RVV_VSE32_V_F32M1(normalized.data() + y, norm, vl);
            RVV_VSE32_V_F32M1(red.data() + y, r, vl);
            RVV_VSE32_V_F32M1(green.data() + y, g, vl);
            RVV_VSE32_V_F32M1(blue.data() + y, b, vl);
            y += vl;
        }

        for (int py = 0; py < height; ++py) {
            const int source_y = height - 1 - py;
            const std::size_t offset = static_cast<std::size_t>((py * width + x) * 3);
            pixels[offset + 0] = static_cast<unsigned char>(red[static_cast<std::size_t>(source_y)] * 255.0f);
            pixels[offset + 1] = static_cast<unsigned char>(green[static_cast<std::size_t>(source_y)] * 255.0f);
            pixels[offset + 2] = static_cast<unsigned char>(blue[static_cast<std::size_t>(source_y)] * 255.0f);
        }
    }

    if (!WritePngRgb(path, width, height, pixels)) {
        throw std::runtime_error("failed to write PNG: " + path.string());
    }
}

/*
 * 功能：批量生成 SVG 折线坐标。
 * 核心：向量化时间归一化和 dB 幅度归一化，文本格式化仍由标量流输出。
 * 参数：times 与 values_db 长度必须一致；left/bottom 为 SVG 绘图区坐标基准。
 * 返回：通过 x_coordinates 和 y_coordinates 返回坐标数组。
 * 平台：仅在 USE_RVV_STFT 与 __riscv_vector 同时定义时编译。
 */
void ComputeTargetFrequencyCoordinatesRvv(const std::vector<float>& times,
                                          const std::vector<float>& values_db,
                                          float min_value,
                                          float max_value,
                                          float plot_width,
                                          float plot_height,
                                          float left,
                                          float bottom,
                                          std::vector<float>& x_coordinates,
                                          std::vector<float>& y_coordinates) {
    const float inv_tmax = times.back() > 0.0f ? 1.0f / times.back() : 0.0f;
    const float inv_range = max_value > min_value ? 1.0f / (max_value - min_value) : 1.0f;
    for (std::size_t i = 0; i < values_db.size();) {
        const std::size_t vl = RVV_VSETVL_E32M1(values_db.size() - i);
        vfloat32m1_t time_values = RVV_VLE32_V_F32M1(times.data() + i, vl);
        vfloat32m1_t db_values = RVV_VLE32_V_F32M1(values_db.data() + i, vl);
        vfloat32m1_t x = RVV_VFADD_VF_F32M1(
            RVV_VFMUL_VF_F32M1(time_values, inv_tmax * plot_width, vl),
            left,
            vl);
        vfloat32m1_t normalized = RVV_VFMUL_VF_F32M1(
            RVV_VFSUB_VF_F32M1(db_values, min_value, vl),
            inv_range,
            vl);
        vfloat32m1_t y = RVV_VFRSUB_VF_F32M1(
            RVV_VFMUL_VF_F32M1(normalized, plot_height, vl),
            bottom,
            vl);
        RVV_VSE32_V_F32M1(x_coordinates.data() + i, x, vl);
        RVV_VSE32_V_F32M1(y_coordinates.data() + i, y, vl);
        i += vl;
    }
}

#endif
