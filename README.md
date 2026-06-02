# riscv-stft-spectrum-analyzer

面向 RISC-V/RVV 的跨平台音频频谱分析仓库，包含一个单频点 FFT 幅度提取器和一个 STFT 频谱分析可视化工具。

## 子项目概览

- `audio-fft-extractor`：对音频执行 FFT，提取目标频率 bin 的幅度，并输出文本结果和 SVG 频谱图。适合快速验证单频点幅度。
- `stft-spectrum-analyzer`：对音频执行 STFT，生成频谱图 PNG、梅尔频谱图 PNG、目标频率幅度曲线 SVG 和时间序列 TXT。适合做完整的时频分析和可视化。

## 目录结构

```text
.
├── audio-fft-extractor/
│   ├── CMakeLists.txt
│   ├── include/
│   ├── src/
│   └── result/
├── dataset/
├── stft-spectrum-analyzer/
│   ├── CMakeLists.txt
│   ├── include/
│   ├── src/
│   ├── result/
│   ├── README.md
│   ├── 用户手册.md
│   └── 代码导读.md
├── changes.md
├── workflow.md
├── workflow2.md
├── name.txt
└── README.md
```

## 快速开始

克隆仓库并进入目录：

```bash
git clone <your-repository-url> riscv-stft-spectrum-analyzer
cd riscv-stft-spectrum-analyzer
```

准备音频：

```bash
mkdir -p dataset
cp your_audio.wav dataset/
```

构建并运行 STFT 分析器：

```bash
cmake -S stft-spectrum-analyzer -B stft-spectrum-analyzer/build -DCMAKE_BUILD_TYPE=Release
cmake --build stft-spectrum-analyzer/build
./stft-spectrum-analyzer/build/stft-spectrum-analyzer
```

Windows Visual Studio 生成器：

```powershell
cmake -S stft-spectrum-analyzer -B stft-spectrum-analyzer/build
cmake --build stft-spectrum-analyzer/build --config Release
.\stft-spectrum-analyzer\build\Release\stft-spectrum-analyzer.exe
```

构建并运行单频点 FFT 提取器：

```bash
cmake -S audio-fft-extractor -B audio-fft-extractor/build -DCMAKE_BUILD_TYPE=Release
cmake --build audio-fft-extractor/build
./audio-fft-extractor/build/audio-fft-extractor
```

## RISC-V RVV 加速

在 RISC-V 平台上，使用支持 RVV 的 GCC/Clang 工具链，并传入芯片匹配的 `-march` 和 `-mabi`：

```bash
cmake -S stft-spectrum-analyzer -B stft-spectrum-analyzer/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -march=rv64gcv -mabi=lp64d"
cmake --build stft-spectrum-analyzer/build
./stft-spectrum-analyzer/build/stft-spectrum-analyzer
```

运行日志会显示：

```text
Platform: RISC-V
RVV STFT acceleration: Enabled
```

如果显示 `Disabled`，通常说明编译器没有定义 `__riscv_vector`，需要检查工具链版本、`-march` 和 ABI 设置。

## 平台支持矩阵

| 功能 | x86_64 | ARM64 | RISC-V 标量 | RISC-V RVV |
|------|--------|-------|-------------|------------|
| FFT/STFT | 支持 | 支持 | 支持 | 支持 |
| 帧内向量化 | 不适用 | 不适用 | 不适用 | 支持 |
| 多帧向量化 | 不适用 | 不适用 | 不适用 | 支持 |
| 频谱图绘制 | 支持 | 支持 | 支持 | 支持 |
| RVV 加速绘制 | 不适用 | 不适用 | 不适用 | 支持 |

## 依赖

必需：

- CMake 3.16+
- 支持 C++17 的 C++ 编译器

可选：

- FFmpeg 开发库：用于通过 `libavformat/libavcodec` 解码常见音频格式。
- 系统 `ffmpeg` 命令：未链接 FFmpeg 库时，用作 MP3/FLAC 等格式的回退解码方式。
- PortAudio：启用 UAC 实时输入接口时需要。

## 常见问题

如果找不到音频文件，请确认音频在仓库根目录的 `dataset/` 下。

如果 MP3 无法解码，请确认系统能直接运行 `ffmpeg -version`，或安装 FFmpeg 开发库后重新 CMake。

如果 RISC-V 编译报 RVV intrinsic 未声明，请确认工具链支持 RVV 1.0，且 `-march` 包含 `v` 扩展，例如 `rv64gcv`。

如果输出 PNG 很大，这是因为 STFT 每帧都会映射到一列像素；可以在 `stft-spectrum-analyzer/include/settings.h` 中调大 `STFT_HOP_SIZE` 或调小输入音频长度。
