# 项目优化需求（完整技术规格）

> 本文档是对 `d:\code\risc-v` 仓库的完整优化需求说明，面向 AI Agent 自动化处理。所有技术细节均已展开，确保 Agent 在无需人工追问的情况下即可准确执行。

---

# 一、project2 STFT 单帧逻辑完善

## 1.1 修复 ComputeStftRvv 的返回值问题

### 问题描述
当前 `stft_rvv.cpp` 中 `ComputeStftRvv` 函数在第79行直接返回 `ComputeStftGeneric(mono_samples, sample_rate, config)` 的调用结果。也就是说，虽然前面第58-68行使用 RVV 向量指令完成了加窗（windowing）操作（`windowed_samples` 已正确计算），但后续的 **FFT 变换**、**幅度谱/功率谱计算**、**梅尔频谱生成** 等全部流程又回退到了标量实现 `ComputeStftGeneric`。这导致 RVV 加速的加窗结果被丢弃，RV V 加速形同虚设。

### 修改要求
1. 删除第79行的 `return ComputeStftGeneric(...)` 调用。
2. 将加窗结果 `windowed_samples` 重新组织为 `std::vector<std::vector<float>> frames`（即 `frame_count` 行、`frame_size` 列的二维帧矩阵），每一行是一帧加窗后的数据。
3. 将 `frames` 传入 `ComputeFromWindowedFrames`（该函数定义在 `stft_generic.cpp` 匿名命名空间中，需要将其声明提升到 `stft.h` 中作为内部公共接口，或将其逻辑内联到 RVV 版本中）。
4. 在 `ComputeStftRvv` 内部，将 FFT、幅度谱计算改为使用 RVV 向量化版本（见下文 1.2 和 1.3）。
5. 返回完整的 `StftResult`。

### 关键代码上下文
- `stft.h` 第33-35行：`ComputeStftGeneric` 声明
- `stft_rvv.cpp` 第41-80行：当前 `ComputeStftRvv` 实现
- `stft_generic.cpp` 第122-171行：`ComputeFromWindowedFrames` 匿名命名空间函数

---

## 1.2 编写 RISC-V RVV 加速的 FFT（快速傅里叶变换）

### 技术规格

#### 1.2.1 FFT 算法选择
- 采用 **Cooley-Tukey 基-2 按时间抽取（DIT）** 算法，与现有标量版本 `FftInPlace`（`stft_generic.cpp` 第25-58行）保持一致的结构。
- 输入/输出格式：`std::complex<float>` 数组（即 `std::vector<std::complex<float>>`），复数在内存中按 `[real, imag, real, imag, ...]` 交替存储（C++ 标准布局）。
- 帧大小 `frame_size` 保证为2的幂，无需在函数内再次校验。

#### 1.2.2 位逆序置换（Bit-Reversal Permutation）的 RVV 向量化
- 标量版本（`stft_generic.cpp` 第31-42行）使用逐元素的 swap 循环。
- RVV 优化策略：位逆序置换的访存模式不规则，难以直接向量化。**保留标量位逆序置换逻辑**，但可使用 RVV 的 `vrgather` 指令加速——预计算逆序索引表，然后使用 `vrgather.vv` 一次性搬移一个向量寄存器组的数据。
- 若预计算索引表，索引表只需在 STFT 初始化时计算一次（帧大小不变）。

#### 1.2.3 蝶形运算（Butterfly）的 RVV 向量化
标量版本的核心循环（`stft_generic.cpp` 第44-57行）：

```
for len = 2, 4, 8, ..., n:
    w_len = exp(-2πi / len)
    for offset = 0, len, 2*len, ...:
        w = 1
        for k = 0 to len/2 - 1:
            upper = data[offset + k]
            lower = data[offset + k + len/2] * w
            data[offset + k] = upper + lower
            data[offset + k + len/2] = upper - lower
            w *= w_len
```

RVV 向量化策略：
- 对最内层 `k` 循环进行向量化。
- 每个向量寄存器一次处理 `vl = vsetvlmax_e32m1()` 个蝶形对（实际 `vl` 由 VLEN/32 决定，例如 VLEN=128 时 `vl=4`，VLEN=256 时 `vl=8`）。
- **复数运算的内存布局处理**：由于 `std::complex<float>` 是 interleaved 布局（实部虚部交替），而 RVV 的浮点运算指令操作的是连续 `float`，因此有两种方案：
  - **方案A（推荐）**：在进入 FFT 前将 `std::complex<float>` 数组拆分为两个独立的 `float` 数组 `real[]` 和 `imag[]`，这样可以用 `vle32` 直接加载连续实部/虚部进行向量运算，FFT 完成后再合并回 interleaved 格式。
  - **方案B**：使用带 stride 的 `vlse32` / `vsse32` 指令，stride=2 来加载/存储实部和虚部。
- **推荐采用方案A**：拆分后蝶形运算中的复数乘法 `(a+bi)(c+di) = (ac-bd) + (ad+bc)i` 可以用 RVV 的 `vfmul`、`vfadd`、`vfsub` 等指令高效完成。
- **旋转因子预计算**：外层 `len` 循环中，预先用标量计算 `cos(angle)` 和 `sin(angle)` 值填充一个长度为 `len/2` 的旋转因子数组（pair 存储或分开存储），在向量化内层循环时通过 `vle32` 加载。

#### 1.2.4 RVV 内联函数 API 兼容性
- 当前代码（`stft_rvv.cpp` 第7-20行）通过宏兼容 RVV 0.7（不带 `__riscv_` 前缀）和 RVV 1.0（带 `__riscv_` 前缀）两种 intrinsic 命名风格。**新的 RVV FFT 代码必须沿用此兼容策略**。
- 需要使用的 RVV intrinsic 包括但不限于：
  - `__riscv_vsetvl_e32m1` / `vsetvl_e32m1`：设置向量长度
  - `__riscv_vle32_v_f32m1` / `vle32_v_f32m1`：加载 float32 向量
  - `__riscv_vse32_v_f32m1` / `vse32_v_f32m1`：存储 float32 向量
  - `__riscv_vfmul_vv_f32m1` / `vfmul_vv_f32m1`：向量浮点乘法
  - `__riscv_vfadd_vv_f32m1` / `vfadd_vv_f32m1`：向量浮点加法
  - `__riscv_vfsub_vv_f32m1` / `vfsub_vv_f32m1`：向量浮点减法
  - `__riscv_vfmacc_vv_f32m1` / `vfmacc_vv_f32m1`：向量浮点乘加
  - `__riscv_vfnmsac_vv_f32m1` / `vfnmsac_vv_f32m1`：向量浮点负乘减

#### 1.2.5 跨平台兼容
- 新的 RVV FFT 函数必须用 `#if defined(USE_RVV_STFT) && defined(__riscv_vector)` 条件编译包裹。
- 确保在 x86 和 ARM 平台上编译时不包含 RVV FFT 代码，回退到标量 FFT（`FftInPlace`）。
- 在 `CMakeLists.txt` 中，RVV FFT 的新 `.cpp` 文件也应仅在 `CMAKE_SYSTEM_PROCESSOR MATCHES "riscv|RISC-V"` 时加入编译。

### 函数签名建议
```cpp
// 在 stft.h 中声明（条件编译保护）
#if defined(USE_RVV_STFT) && defined(__riscv_vector)
void FftInPlaceRvv(std::vector<std::complex<float>>& data);
#endif
```

---

## 1.3 使用 RVV 加速幅度谱和功率谱计算

### 技术规格
标量版本在 `stft_generic.cpp` 第154-158行：
```cpp
for (std::size_t bin = 0; bin < freq_bins; ++bin) {
    const float magnitude = std::abs(fft_buffer[bin]) / static_cast<float>(config.frame_size);
    row[bin] = 20.0f * std::log10(std::max(magnitude, 1.0e-6f));
}
```

RVV 优化策略：

#### 1.3.1 幅度计算向量化
- 对于每个 FFT bin，`std::complex<float>` 包含实部 `re` 和虚部 `im`。
- 幅度 `mag = sqrt(re² + im²) / frame_size`。
- 若已采用方案A将实部/虚部分离为两个数组（见 1.2.3），则：
  - 用 `vle32` 加载实部向量 `vre`
  - 用 `vle32` 加载虚部向量 `vim`
  - `vre² = vfmul(vre, vre)`，`vim² = vfmul(vim, vim)`
  - `vsum = vfadd(vre², vim²)`
  - `vmag = vfsqrt(vsum)`  —— 注意：RVV 1.0 提供 `vfsqrt`（`__riscv_vfsqrt_v_f32m1` / `vfsqrt_v_f32m1`）
  - 除以 `frame_size`：预计算 `inv_frame_size = 1.0f / frame_size`，用 `vfmul(vmag, inv_frame_size)` 完成。
- 若保持 interleaved 布局，需要用 stride-2 的 `vlse32` 分别加载实部和虚部。

#### 1.3.2 dB 转换的向量化
- `dB = 20 * log10(max(mag, 1e-6))`。
- RVV 不直接提供 `log10` 向量指令。有以下处理方式：
  - **方案A（推荐）**：幅度归一化后的 `vmag` 先做 clamp 下限（`vfmax` 与 `1e-6` 常量向量比较），然后回退到标量循环计算 `log10` 和 dB 值。log10 的计算占比相对较小，标量处理不会成为瓶颈。
  - **方案B**：使用快速近似多项式 `log2` + 换底公式。`log10(x) = log2(x) / log2(10)`。RVV 不直接提供 `vlog2`，但可以用 frexp + 多项式近似实现。此方案实现复杂，不推荐首版采用。
- **推荐方案A**：幅度平方和开根号用 RVV，dB 转换保留标量循环。

#### 1.3.3 梅尔频谱的向量化
- 梅尔频谱计算（`stft_generic.cpp` 第72-120行 `BuildMelSpectrogram`）涉及三角形滤波器组加权求和。
- 内层对 `freq_bins` 的循环（第99-112行）可以部分向量化：权重为 `0` 的 bin 可以用 `vfmacc`（乘加）跳过，但整体访存模式不规则，收益有限。
- **首版优化建议**：梅尔频谱的内层加权求和可以尝试用 RVV 的 `vfmacc` 指令加速，将连续 bin 的 `spectrogram_db[frame][bin] * weight` 乘积累加到向量寄存器中，最后用 `vfredsum`（向量归约求和）得到 `weighted_sum`。

---

# 二、增加多帧 STFT 的 RVV 并行计算适应

## 2.1 多帧向量化方案的技术原理

### 核心思想
- **帧内向量化（Intra-frame）**：将单帧内的数据并行处理。例如一帧1024个样本，用 VLEN=256 的 RVV 每次处理 8 个 float，128 次迭代完成。
- **多帧向量化（Multi-frame / Cross-frame）**：将多帧的同一位置索引的数据并行处理。例如同时有 8 帧，将 8 帧的第 i 个样本加载到一个向量寄存器中，一次性完成 8 帧的乘法/加法操作。

### 2.2 多帧向量化的具体实现
对于加窗操作 `windowed[i] = sample[frame*hop + i] * window[i]`：
- **帧内方案**：一帧一帧处理，每帧内部使用 RVV 向量化 i 循环。
- **多帧方案**：选取 `M` 帧（M = VLEN/32，例如 VLEN=256 时 M=8），同时处理这 M 帧的第 i 个样本：
  - 加载 M 帧的第 i 个样本到向量 `vs`（需要跨帧 gather 加载或重组内存布局）
  - 广播 `window[i]` 到向量 `vw`（`vfmv.v.f` + 广播 或 `vle32` 重复加载）
  - `vout = vfmul(vs, vw)`
  - 存回 M 帧的第 i 个位置

### 2.3 内存布局策略
- **推荐做法**：在进行多帧向量化前，将帧数据转置存储。即：
  - 原始布局：`frames[frame_index][sample_index]`（行主序）
  - 转置布局：`frames_t[sample_index][frame_index]`（列主序），使得连续 `M` 帧的同一采样点位于连续内存地址，便于 `vle32` 连续加载。
- 转置操作本身也可以用 RVV 加速（使用 strided load/store）。

### 2.4 FFT 的多帧向量化
- 对 M 帧同时执行相同阶段的蝶形运算。每个向量寄存器槽位对应不同帧的同一 FFT bin。
- 旋转因子在各帧间相同，可以广播复用。
- 这种方法特别适合帧数较多（>32帧）且帧大小适中（≤2048）的场景。

### 2.5 多帧向量化的跨平台条件编译
- 多帧 RVV 代码同样使用 `#if defined(USE_RVV_STFT) && defined(__riscv_vector)` 保护。
- 在 x86/ARM 上编译时应使用 SSE/AVX/NEON 等效实现（或回退标量），但本次优化以 RVV 为优先目标。

## 2.6 混合自适应方案（Hybrid Adaptive Scheme）

### 决策因素
根据以下运行时参数决定采用帧内向量化还是多帧向量化：

| 参数 | 含义 | 来源 |
|------|------|------|
| `frame_size` | 帧大小（如1024） | `StftConfig::frame_size` |
| `frame_count` | 总帧数 | 由音频长度和 hop_size 计算 |
| `VLEN` | RVV 向量寄存器位宽（bits） | 编译时宏 `__riscv_vlen` 或运行时通过 `vsetvl` 探测 |
| `LMUL` | 向量寄存器分组因子 | 固定为1（`e32m1`），可考虑 LMUL=2/4 提高并行度 |

### 决策算法
```
如果 frame_count >= VLEN/32（即帧数 ≥ 一个向量寄存器能容纳的 float32 数量）：
    且 frame_size >= 256：
        使用多帧向量化方案（帧间并行优先，因为可利用的并行帧数多）
    否则：
        使用帧内向量化方案
否则：
    使用帧内向量化方案（帧数太少，跨帧收益不足）
```

### 实现位置
- 决策逻辑定义在独立的 `stft_adaptive.h` 和 `stft_adaptive.cpp` 中（见第四部分文件结构）。
- `ComputeStft`（`stft_generic.cpp` 第227-235行）在 RISC-V 平台运行时，先调用决策函数判断使用哪种方案，再分发到对应的实现。

### 决策函数签名建议
```cpp
enum class StftVectorizationMode {
    Scalar,       // 标量实现（回退方案）
    IntraFrame,   // 帧内向量化
    MultiFrame,   // 多帧向量化
};

StftVectorizationMode SelectStftVectorizationMode(std::size_t frame_size,
                                                   std::size_t frame_count);
```

---

# 三、图像绘制与输出的 RVV 加速

## 3.1 频谱图/梅尔频谱图 PNG 绘制的 RVV 加速

### 当前标量实现
`image_writer.cpp` 中 `WriteSpectrogramPng` 函数（第145-180行）的关键可向量化部分：

1. **min/max 值搜索**（第153-160行）：
   ```cpp
   for (const auto& row : matrix_db) {
       for (float value : row) {
           min_value = std::min(min_value, value);
           max_value = std::max(max_value, value);
       }
   }
   ```
   - **RVV 优化**：用 `vle32` 加载一行中的连续值，使用 `vfmin` / `vfmax`（RVV 提供 `__riscv_vfmin_vv_f32m1` 和 `__riscv_vfmax_vv_f32m1`）进行逐元素比较，再用 `vfredmin` / `vfredmax` 进行向量归约得到本批次的 min/max。每行处理完毕后与全局 min/max 比较更新。

2. **颜色映射和像素填充**（第166-175行）：
   ```cpp
   for (int x = 0; x < width; ++x) {
       for (int y = 0; y < height; ++y) {
           const float value = matrix_db[x][height - 1 - y];
           const auto color = ColorMap((value - min_value) / (max_value - min_value));
           pixels[offset + 0] = color[0];
           pixels[offset + 1] = color[1];
           pixels[offset + 2] = color[2];
       }
   }
   ```
   - **RVV 优化**：
     - 预计算 `inv_range = 1.0f / (max_value - min_value)` 和 `min_value`，广播为向量。
     - 用 `vle32` 加载一列（height 方向）的 dB 值，用 `vfsub` + `vfmul` 计算归一化值 `normalized = (value - min_value) * inv_range`。
     - 颜色映射 `ColorMap` 是一个简单的分段线性函数（`image_writer.cpp` 第93-103行），可以用 RVV 的 `vfmax`/`vfmin`（clamp）、`vfmul`、`vfadd`、`vfsub` 等逐元素计算 R、G、B 通道值。
     - 将 R、G、B 三通道结果分别存储到像素缓冲区对应位置（stride=3 的 `vsse32` 或分别用 offset 写入）。

### 修改策略
- 在 RISC-V 平台编译时，条件编译选择 RVV 加速版的 `WriteSpectrogramPng` 函数。
- 在 x86/ARM 平台保留原标量实现。

## 3.2 目标频率曲线 SVG 和 TXT 的 RVV 加速

### SVG 生成
`WriteTargetFrequencySvg`（`image_writer.cpp` 第182-230行）的 polyline 点坐标计算（第222-228行）：
```cpp
for (std::size_t i = 0; i < values_db.size(); ++i) {
    const float time_norm = times.back() > 0.0f ? times[i] / times.back() : 0.0f;
    const float value_norm = (values_db[i] - min_value) / (max_value - min_value);
    const float x = left + time_norm * plot_width;
    const float y = top + plot_height - value_norm * plot_height;
    out << x << ',' << y << ' ';
}
```
- **RVV 优化**：可以预先用 RVV 批量计算所有 `(x, y)` 坐标对，存入 `std::vector<float>`，然后一次性输出。RVV 加速部分主要是除法和乘法——`times[i] / times.back()` 可预计算 `inv_tmax`，然后用 `vfmul` 向量化；`(values_db[i] - min_value) / (max_value - min_value)` 同理。

### TXT 生成
- `WriteTargetFrequencyTxt`（`main.cpp` 第67-78行）主要是格式化输出，属于 I/O 密集型操作，RVV 加速空间有限。可保持标量实现，也可以预格式化字符串缓冲区后用 RVV 辅助 memcpy。

---

# 四、project2 内部结构的模块化拆分

## 4.1 STFT 实现文件拆分

将当前混合在一起的 STFT 实现拆分为以下文件：

| 文件 | 内容 | 条件编译 |
|------|------|----------|
| `stft_scalar.cpp` | 标量 STFT 实现：`ComputeStftGeneric`、`FftInPlace`、`BuildHannWindow`、`BuildMelSpectrogram`、`ComputeFromWindowedFrames` | 始终编译 |
| `stft_intra_rvv.cpp` | 帧内向量化 RVV STFT：`ComputeStftIntraRvv`、`FftInPlaceRvv`、RV V 加窗、RVV 幅度谱计算 | `USE_RVV_STFT && __riscv_vector` |
| `stft_multi_rvv.cpp` | 多帧向量化 RVV STFT：`ComputeStftMultiRvv`、多帧 FFT、多帧幅度谱 | `USE_RVV_STFT && __riscv_vector` |

### 拆分细则
- `stft_scalar.cpp` 将原来 `stft_generic.cpp` 的内容移入，并保持匿名命名空间中的辅助函数不变。
- `stft_intra_rvv.cpp` 包含**帧内 RVV 加速**的完整 STFT 流水线（加窗、FFT、幅度谱、梅尔频谱），不调用任何标量 FFT。
- `stft_multi_rvv.cpp` 包含**多帧 RVV 加速**的完整 STFT 流水线，从内存转置开始到最终结果组装。

### 声明统一
- 所有上述函数均在 `stft.h` 中声明（条件编译保护），确保外部调用者只需包含 `stft.h` 一个头文件。

## 4.2 混合自适应决策模块

| 文件 | 内容 |
|------|------|
| `stft_adaptive.h` | `StftVectorizationMode` 枚举、`SelectStftVectorizationMode` 函数声明 |
| `stft_adaptive.cpp` | 决策函数实现，包含 `vsetvl` 探测 VLEN 的逻辑 |

### 决策实现细节
```cpp
// stft_adaptive.cpp 核心逻辑伪代码
#include "stft_adaptive.h"
#if defined(__riscv_vector)
#include <riscv_vector.h>
#endif

StftVectorizationMode SelectStftVectorizationMode(std::size_t frame_size,
                                                   std::size_t frame_count) {
#if defined(USE_RVV_STFT) && defined(__riscv_vector)
    // 获取 VLEN（向量寄存器位宽）
    // 方法：调用 vsetvl_e32m1(1) 得到 vl=1，但 VLEN 需要间接获取
    // 更好的方法：使用编译时宏 __riscv_vlen（如果可用）或运行时探测
    const std::size_t vlen = /* 探测逻辑 */;
    const std::size_t floats_per_reg = vlen / 32;  // LMUL=1, SEW=32

    if (frame_count >= floats_per_reg && frame_size >= 256) {
        return StftVectorizationMode::MultiFrame;
    } else if (frame_size >= 64) {
        return StftVectorizationMode::IntraFrame;
    }
#endif
    return StftVectorizationMode::Scalar;
}
```

- 如何获取 VLEN：优先使用编译器预定义宏（如 GCC 的 `__riscv_vlen`，需查阅具体工具链文档）。若不可用，可通过运行 `vsetvl_e32m1(65536)` 得到最大 `vl`，而 `VLEN = vl * 32`（当 LMUL=1, SEW=32时，`vl_max = VLEN/32`）。

## 4.3 图像绘制模块拆分

| 文件 | 内容 | 条件编译 |
|------|------|----------|
| `image_writer_scalar.cpp` | 标量 PNG/SVG 写入：`WritePngRgb`、`WriteSpectrogramPng`、`WriteTargetFrequencySvg` | 始终编译 |
| `image_writer_rvv.cpp` | RVV 加速的频谱图绘制：`WriteSpectrogramPngRvv`、辅助的 min/max 搜索和颜色映射 RVV 版本 | `USE_RVV_STFT && __riscv_vector` |

- 对外接口保持一致：`image_writer.h` 中声明的函数签名不变，在内部通过 `ComputeStft` 层面的分发逻辑或条件编译选择标量/RVV 版本的绘制函数。

## 4.4 CMakeLists.txt 更新

```cmake
set(PROJECT2_SCALAR_SOURCES
    audio_io.cpp
    image_writer_scalar.cpp
    main.cpp
    stft_scalar.cpp
    stft_adaptive.cpp
    uac_input.cpp
    web_spectrogram.cpp
)

set(PROJECT2_RVV_SOURCES
    stft_intra_rvv.cpp
    stft_multi_rvv.cpp
    image_writer_rvv.cpp
)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "riscv|RISC-V")
    list(APPEND PROJECT2_SOURCES ${PROJECT2_SCALAR_SOURCES} ${PROJECT2_RVV_SOURCES})
else()
    list(APPEND PROJECT2_SOURCES ${PROJECT2_SCALAR_SOURCES})
endif()
```

---

# 五、代码可读性优化：中文注释

## 5.1 注释规范

### 每个函数的注释必须包含：
1. **功能简述**：一句话说明函数做什么。
2. **计算核心**：解释核心算法逻辑（如 "Cooley-Tukey 基-2 FFT，蝶形运算采用按时间抽取"）。
3. **参数说明**：每个参数的含义和约束（如 "frame_size 必须为2的幂"）。
4. **返回值说明**：返回值的结构和含义。
5. **平台/条件编译说明**（如适用）：如 "仅在 `__riscv_vector` 宏定义时编译"。

### 函数内关键代码块的注释：
- 加窗循环：说明窗函数类型（如 "Hann 窗：w[n] = 0.5 - 0.5*cos(2πn/(N-1))"）。
- FFT 蝶形运算：标注每个循环层级的含义（外层：FFT 级数；中层：蝶形组；内层：蝶形对）。
- RVV intrinsic 调用：用注释说明每个 `vle32`/`vfmul`/`vse32` 的用途。
- 向量长度设置：注释说明 `vl = vsetvl_e32m1(N)` 中 `vl` 的含义（如 "本次迭代实际处理的元素数，由 VLEN 硬件决定"）。

## 5.2 生成代码导读文档

在 `project2/` 目录下生成 `代码导读.md`，内容包括：

1. **整体架构概述**：程序从音频输入到最终输出的完整数据流图（文字描述或 ASCII art）。
2. **文件职责表**：列出每个 `.cpp`/`.h` 文件的职责和对外接口。
3. **核心算法讲解**：
   - STFT 的数学原理（加窗 → FFT → 幅度谱 → dB 转换）
   - 梅尔频谱的三角形滤波器组原理
   - 帧内向量化 vs 多帧向量化的并行策略图解
4. **关键函数调用链**：从 `main()` 开始到各子函数的调用关系。
5. **RISC-V RVV 加速详解**：
   - RVV 向量化在哪些环节加速
   - 混合自适应方案的选择逻辑
   - 编译配置和宏定义说明
6. **构建与运行指南**（可引用 `README.md` 和 `用户手册.md`）。

---

# 六、整体项目架构修改

## 6.1 文件夹重命名

### project1
- 当前功能：对音频文件计算 FFT，提取目标频率 bin 的幅度，输出 `.txt` 数据文件。
- 本质：**单频点 FFT 幅度提取器**。
- 建议命名：`fft-amplitude-extractor` 或 `audio-fft-extractor`（请 Agent 根据功能自行判断最合适的名称）。

### project2
- 当前功能：STFT 短时傅里叶变换，生成频谱图 PNG、梅尔频谱图 PNG、目标频率曲线 SVG 和数据 TXT。
- 本质：**STFT 频谱分析与可视化工具**。
- 建议命名：`stft-spectrum-analyzer`（请 Agent 根据功能自行判断最合适的名称）。

### 修改要求
- 重命名实际文件夹后，需同步更新：
  - 所有 `CMakeLists.txt` 中的 `project()` 名称和路径引用
  - `README.md` 中的路径引用
  - `用户手册.md` 中的路径引用
  - `main.cpp` 中的 `DATASET_PATH` 相对路径可能需要调整

## 6.2 README.md 重写

新的 `README.md` 应包含：

1. **项目名称和一句话描述**（位于仓库根目录）。
2. **子项目概述**：分别简述两个子项目的功能和适用场景。
3. **目录结构**：以 ASCII 树状图展示完整文件结构。
4. **快速开始**：从克隆仓库到运行的完整步骤。
5. **RISC-V RVV 加速**：专门的章节说明如何启用 RVV 加速、所需的工具链和硬件要求。
6. **平台支持矩阵**：

| 功能 | x86_64 | ARM64 | RISC-V (标量) | RISC-V (RVV) |
|------|--------|-------|---------------|--------------|
| FFT/STFT | ✅ | ✅ | ✅ | ✅ |
| 帧内向量化 | ❌ | ❌ | ❌ | ✅ |
| 多帧向量化 | ❌ | ❌ | ❌ | ✅ |
| 频谱图绘制 | ✅ | ✅ | ✅ | ✅ |
| RVV 加速绘制 | ❌ | ❌ | ❌ | ✅ |

7. **依赖列表**。
8. **常见问题**。

---

# 七、GitHub 仓库重命名

将项目的新名称保存在仓库根目录的 `name.txt` 文件中。

### 命名考量因素
- 项目核心功能：基于 STFT 的音频频谱分析，特别强调 RISC-V RVV 向量加速。
- 关键词组合建议：`riscv`、`stft`、`spectrum`、`rvv`、`audio`、`analyzer`。
- 格式要求：全小写，单词间用连字符分隔，简洁且具有辨识度。

### 命名示例（供参考，Agent 可自由发挥）
- `riscv-stft-analyzer`
- `rvv-audio-spectrum`
- `stft-spectrum-rvv`

最终名称由 Agent 自行决定，写入 `name.txt`。

---

# 附录A：现有代码的关键问题速查

| 文件 | 行号 | 问题 | 修复方向 |
|------|------|------|----------|
| `stft_rvv.cpp` | 79 | `return ComputeStftGeneric(...)` 丢弃了 RVV 加窗结果 | 完成完整 RVV STFT 流水线 |
| `stft_rvv.cpp` | 58-68 | 加窗已用 RVV，但 `windowed_samples` 未被后续使用 | 传递给新的 RVV FFT |
| `stft_generic.cpp` | 25-58 | `FftInPlace` 为标量实现 | 编写 `FftInPlaceRvv` |
| `stft_generic.cpp` | 122-171 | `ComputeFromWindowedFrames` 在匿名命名空间，外部无法调用 | 提升声明到 `stft.h` 或内联到 RVV 版本 |
| `image_writer.cpp` | 145-180 | `WriteSpectrogramPng` 全标量 | 编写 RVV 加速版本 |
| `CMakeLists.txt` | 18-20 | 仅 `stft_rvv.cpp` 条件编译 | 更新为新文件结构 |

---

# 附录B：RVV Intrinsic 速查表

> 以下列出本次优化中常用的 RVV intrinsic（以 RVV 1.0 命名风格为准）。RVV 0.7 风格去掉 `__riscv_` 前缀。代码中通过宏兼容两种风格。

| 操作 | RVV 1.0 Intrinsic | 说明 |
|------|-------------------|------|
| 设置向量长度 | `size_t vl = __riscv_vsetvl_e32m1(size_t n)` | 返回实际处理元素数 |
| 加载 f32 | `vfloat32m1_t v = __riscv_vle32_v_f32m1(const float* ptr, size_t vl)` | 连续加载 |
| 存储 f32 | `__riscv_vse32_v_f32m1(float* ptr, vfloat32m1_t v, size_t vl)` | 连续存储 |
| 向量乘法 | `vfloat32m1_t r = __riscv_vfmul_vv_f32m1(vfloat32m1_t a, vfloat32m1_t b, size_t vl)` | 逐元素乘法 |
| 向量加法 | `vfloat32m1_t r = __riscv_vfadd_vv_f32m1(vfloat32m1_t a, vfloat32m1_t b, size_t vl)` | 逐元素加法 |
| 向量减法 | `vfloat32m1_t r = __riscv_vfsub_vv_f32m1(vfloat32m1_t a, vfloat32m1_t b, size_t vl)` | 逐元素减法 |
| 向量除法 | `vfloat32m1_t r = __riscv_vfdiv_vv_f32m1(vfloat32m1_t a, vfloat32m1_t b, size_t vl)` | 逐元素除法 |
| 平方根 | `vfloat32m1_t r = __riscv_vfsqrt_v_f32m1(vfloat32m1_t v, size_t vl)` | 逐元素 sqrt |
| 乘加 | `vfloat32m1_t r = __riscv_vfmacc_vv_f32m1(vfloat32m1_t acc, vfloat32m1_t a, vfloat32m1_t b, size_t vl)` | `acc + a*b` |
| 取最大值 | `vfloat32m1_t r = __riscv_vfmax_vv_f32m1(vfloat32m1_t a, vfloat32m1_t b, size_t vl)` | 逐元素 max |
| 取最小值 | `vfloat32m1_t r = __riscv_vfmin_vv_f32m1(vfloat32m1_t a, vfloat32m1_t b, size_t vl)` | 逐元素 min |
| 归约求和 | `vfloat32m1_t r = __riscv_vfredusum_vs_f32m1_f32m1(...)` | 向量元素求和归约 |
| 归约最大 | `vfloat32m1_t r = __riscv_vfredmax_vs_f32m1_f32m1(...)` | 向量元素最大值归约 |

---

# 附录C：性能预期参考

| 优化项 | 预期加速比（vs 标量） | 前提条件 |
|--------|----------------------|----------|
| RVV 加窗 | 2-4x | VLEN ≥ 128 |
| RVV FFT（帧内） | 2-8x | frame_size 越大加速越明显，受 VLEN 和 FFT 级数影响 |
| RVV 幅度谱 | 3-6x | 使用 split real/imag 布局 |
| 多帧 FFT | 4-16x | frame_count ≥ VLEN/32，帧数越多越好 |
| RVV 频谱图绘制 | 2-5x | 宽度和高度方向均有并行度 |