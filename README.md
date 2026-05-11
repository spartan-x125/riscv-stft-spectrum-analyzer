# riscv-audio-fft-extractor

一个面向 RISC-V 平台的音频 FFT 频点提取项目。项目会扫描指定数据集目录中的音频文件，对每个音频执行 FFT，提取目标频率附近的频谱幅值，并为每个音频生成对应的结果文本和频谱图。

## 功能特性

- 扫描 `dataset` 目录中的音频文件。
- 对每个音频执行 FFT，得到频谱数据。
- 提取 `TARGETFREQUENCY` 对应的最近频点幅值，默认目标频率为 `1000Hz`。
- 在 `dataset/result` 中输出每个音频对应的 `.txt` 结果文件。
- 在 `dataset/result` 中输出每个音频对应的 `_spectrum.svg` 频谱图。
- WAV 文件可直接解码。
- MP3、FLAC、OGG、M4A、AAC、WMA 等格式在系统安装 `ffmpeg` 后可解码。
- 在 RISC-V 且启用 RVV 的编译环境下，使用 RVV 分支执行 FFT 蝶形计算；其它平台使用普通标量 FFT。

## 项目结构

```text
.
├── dataset/                  # 输入音频目录
│   └── result/               # 输出结果目录
├── project1/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── audio_loader.h
│   │   ├── fft.h
│   │   └── settings.h
│   └── src/
│       ├── audio_loader.cpp
│       ├── fft.cpp
│       └── main.cpp
└── README.md
```

## 环境要求

- CMake 3.16 或更高版本
- 支持 C++17 的 C++ 编译器
- 可选：`ffmpeg`，用于解码 WAV 以外的音频格式

Windows 下可使用 Visual Studio 2022 的 C++ 工具链；Linux 或 RISC-V 交叉编译环境下可使用 GCC/Clang。

## 构建方法

在仓库根目录执行：

```powershell
cmake -S project1 -B project1/build
cmake --build project1/build --config Release
```

Linux 或 Makefile/Ninja 环境下通常可以执行：

```bash
cmake -S project1 -B project1/build
cmake --build project1/build
```

## 运行方法

1. 将待处理音频放入仓库根目录下的 `dataset` 文件夹。
2. 运行可执行文件。

Windows Release 构建：

```powershell
.\project1\build\Release\project1.exe
```

Linux 或单配置生成器：

```bash
./project1/build/project1
```

运行完成后，结果会输出到：

```text
dataset/result
```

## 输出文件

对于输入音频：

```text
dataset/example.wav
```

程序会生成：

```text
dataset/result/example.txt
dataset/result/example_spectrum.svg
```

`.txt` 文件中包含：

- 输入音频路径
- 采样率
- 声道数
- 样本数量
- FFT 点数
- 目标频率
- 实际选中的频点 bin
- 实际频点频率
- 该频点幅值

`_spectrum.svg` 文件是对应音频的频谱图，可直接用浏览器打开查看。

## 配置项

目标频率和输入目录在以下文件中配置：

```text
project1/include/settings.h
```

默认配置：

```cpp
#define TARGETFREQUENCY 1000.0
#define TARGET_DIRECTORY "dataset"
```

修改目标频率后，需要重新编译项目。

## RISC-V / RVV 说明

项目在代码中通过编译宏判断当前是否为 RISC-V 平台：

- 定义 `__riscv` 且定义 `__riscv_vector` 时，启用 RVV FFT 分支。
- 其它平台使用普通标量 FFT 分支。

RISC-V RVV 编译示例：

```bash
riscv64-unknown-elf-g++ -O3 -std=c++17 -march=rv64gcv -mabi=lp64d
```

实际编译参数应根据芯片架构、ABI、SDK 和工具链版本调整。

## 注意事项

- 当前项目会将多声道音频混合为单声道后进行 FFT。
- FFT 输入长度会补零到最近的 2 的幂。
- 目标频率不一定刚好落在 FFT 频点上，程序会选择距离目标频率最近的 bin。
- 如果处理 MP3 等非 WAV 格式，请确保 `ffmpeg` 已安装并能在命令行中直接调用。
