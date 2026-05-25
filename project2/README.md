# Project2: 跨平台音频频谱分析与实时可视化程序

Project2 是在 project1 基础上扩展的 C++/CMake 音频分析程序，面向 RISC-V、x86 和 ARM 平台。程序会读取 `dataset` 中的音频文件，执行短时傅里叶变换（STFT），输出频谱图、梅尔频谱图、目标频率幅度曲线和目标频率幅度数据。

## 文件结构

```text
project2/
├── CMakeLists.txt
├── settings.h
├── stft.h
├── stft_generic.cpp
├── stft_rvv.cpp
├── audio_io.cpp
├── audio_io.h
├── image_writer.cpp
├── image_writer.h
├── uac_input.cpp
├── uac_input.h
├── web_spectrogram.cpp
├── web_spectrogram.h
├── main.cpp
├── result/
├── README.md
└── 用户手册.md
```

## 功能

- 支持批量扫描音频文件。
- 使用 FFmpeg 库解码常见音频格式；若构建环境没有 FFmpeg 开发库，则内置 WAV 解码器仍可处理 WAV，非 WAV 会尝试调用系统 `ffmpeg` 命令。
- 将输入音频转换为单声道 float PCM。
- 执行 STFT 并生成频谱图 PNG。
- 生成梅尔频谱图 PNG。
- 提取 `TARGET_FREQUENCY` 对应频点的幅度时间序列。
- 输出目标频率曲线 SVG 和目标频率数据 TXT。
- RISC-V 平台可启用 RVV STFT 源文件和 `USE_RVV_STFT` 宏。
- 预留 UAC 实时输入接口 `IAudioInputStream`。
- 预留网页实时频谱图推送接口。

## 依赖

必须依赖：

- CMake 3.16+
- 支持 C++17 的 C++ 编译器

可选依赖：

- FFmpeg 开发库：`libavformat`、`libavcodec`、`libavutil`、`libswresample`
- PortAudio：启用 `ENABLE_UAC_INPUT=1` 时需要
- 系统 `ffmpeg` 命令：未链接 FFmpeg 库时用于解码 MP3/FLAC 等非 WAV 格式

PNG 输出使用项目内置的轻量 PNG 写入器，不依赖外部图片库。

## 编译

在仓库根目录执行：

```bash
cmake -S project2 -B project2/build -DCMAKE_BUILD_TYPE=Release
cmake --build project2/build
```

Windows Visual Studio 生成器：

```powershell
cmake -S project2 -B project2/build
cmake --build project2/build --config Release
```

RISC-V RVV 编译示例：

```bash
cmake -S project2 -B project2/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -march=rv64gcv -mabi=lp64d"
cmake --build project2/build
```

如果是 rv32 平台，请根据芯片和工具链改为相应的 `-march` 和 `-mabi`。

## 运行

将音频放入仓库根目录的 `dataset` 文件夹，然后运行：

```bash
./project2/build/project2
```

Windows Release：

```powershell
.\project2\build\Release\project2.exe
```

运行时会打印：

```text
Platform: RISC-V / Other
RVV STFT acceleration: Enabled / Disabled
```

## 输出

所有输出保存在：

```text
project2/result/
```

以 `1.wav` 为例：

```text
1-spectrogram.png
1-mel_spectrogram.png
1-target_freq.svg
1-target_freq.txt
```

`1-target_freq.txt` 每行格式为：

```text
时间(秒)    幅度(dB)
```

## settings.h

所有主要参数集中在 `settings.h`：

```cpp
#define TARGET_FREQUENCY     1000.0f
#define DATASET_PATH         "../dataset"
#define ENABLE_UAC_INPUT     0
#define ENABLE_WEB_SPECTROGRAM 0
#define STFT_FRAME_SIZE      1024
#define STFT_HOP_SIZE        512
#define MEL_BAND_COUNT       64
```

修改后需要重新编译。

## UAC 与网页功能

`ENABLE_UAC_INPUT=1` 时会编译 PortAudio 版本的 `UacAudioStream`，用于后续接入 USB Audio Class 设备。启用前请确保系统安装 PortAudio 开发库。

`ENABLE_WEB_SPECTROGRAM=1` 时会编译网页频谱推送接口。目前项目提供接口和占位实现，后续可接入 `libwebsockets` 或支持 WebSocket 的轻量 HTTP 库，在本地 `9002` 端口提供实时频谱页面。
