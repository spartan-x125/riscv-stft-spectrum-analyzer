# stft-spectrum-analyzer

跨平台音频 STFT 频谱分析与可视化程序，支持 x86、ARM 和 RISC-V；在 RISC-V RVV 编译环境下会启用向量化 STFT 路径。

## 文件结构

```text
stft-spectrum-analyzer/
├── CMakeLists.txt
├── settings.h
├── audio_io.cpp / audio_io.h
├── image_writer.h
├── image_writer_scalar.cpp
├── image_writer_rvv.cpp
├── stft.h
├── stft_adaptive.cpp / stft_adaptive.h
├── stft_scalar.cpp
├── stft_intra_rvv.cpp
├── stft_multi_rvv.cpp
├── uac_input.cpp / uac_input.h
├── web_spectrogram.cpp / web_spectrogram.h
├── main.cpp
├── result/
├── README.md
├── 用户手册.md
└── 代码导读.md
```

## 功能

- 扫描仓库根目录 `dataset/` 下的音频文件。
- 解码为单声道 float PCM。
- 计算 STFT、频谱图和梅尔频谱图。
- 提取 `TARGET_FREQUENCY` 对应频点的幅度时间序列。
- 输出 PNG、SVG 和 TXT。
- RISC-V RVV 平台支持帧内向量化、多帧向量化和频谱图绘制加速。
- 预留 UAC 输入接口和网页实时频谱接口。

## 依赖

必需：

- CMake 3.16+
- C++17 编译器

可选：

- FFmpeg 开发库：`libavformat`、`libavcodec`、`libavutil`、`libswresample`
- 系统 `ffmpeg` 命令：未链接 FFmpeg 库时用于解码非 WAV 格式
- PortAudio：启用 `ENABLE_UAC_INPUT=1` 时需要

## 编译

```bash
cmake -S stft-spectrum-analyzer -B stft-spectrum-analyzer/build -DCMAKE_BUILD_TYPE=Release
cmake --build stft-spectrum-analyzer/build
```

Windows：

```powershell
cmake -S stft-spectrum-analyzer -B stft-spectrum-analyzer/build
cmake --build stft-spectrum-analyzer/build --config Release
```

RISC-V RVV：

```bash
cmake -S stft-spectrum-analyzer -B stft-spectrum-analyzer/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -march=rv64gcv -mabi=lp64d"
cmake --build stft-spectrum-analyzer/build
```

## 运行

```bash
./stft-spectrum-analyzer/build/stft-spectrum-analyzer
```

Windows：

```powershell
.\stft-spectrum-analyzer\build\Release\stft-spectrum-analyzer.exe
```

输出目录：

```text
stft-spectrum-analyzer/result/
```

以 `1.wav` 为例，输出：

```text
1-spectrogram.png
1-mel_spectrogram.png
1-target_freq.svg
1-target_freq.txt
```

## 配置

编辑 `settings.h`：

```cpp
#define TARGET_FREQUENCY       1000.0f
#define DATASET_PATH           "../dataset"
#define ENABLE_UAC_INPUT       0
#define ENABLE_WEB_SPECTROGRAM 0
#define STFT_FRAME_SIZE        1024
#define STFT_HOP_SIZE          512
#define MEL_BAND_COUNT         64
```

修改后需要重新编译。
