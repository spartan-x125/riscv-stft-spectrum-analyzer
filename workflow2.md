# Project2: 跨平台音频频谱分析与实时可视化程序开发

你需要继续扮演一位资深的音频算法工程师和 C++ 及嵌入式工程师。现在，请在 project1 的基础上新建 `project2` 目录，并完成该项目的全部代码。以下为详细要求。

## 一、核心功能

实现一个可在 **RISC-V 平台**（同时兼容 x86 与 ARM）上运行的音频频谱转换与分析程序，具备以下能力：

1. **文件输入**：支持 `.wav`、`.mp3`、`.flac` 等常见音频格式。使用 **FFmpeg 库（libavformat/libavcodec）** 将任何格式解码为单声道/立体声浮点 PCM。
2. **实时输入**：由于后期有实时输入需求，请保留通过 **UAC（USB Audio Class）协议** 设备实时输入音频数据的接口，底层可使用 PortAudio 或 RtAudio 捕获。
3. **处理流水线**：对音频执行 **短时傅里叶变换（STFT）**，生成频谱图与梅尔频谱图，并提取预设目标频率的幅度时间序列。

## 二、输出规范

所有输出存入 `project2/result/` 目录，命名规则（以输入 `1.wav` 为例）：
- 频谱图：`1-spectrogram.png`
- 梅尔频谱图：`1-mel_spectrogram.png`
- 目标频率幅度曲线图：`1-target_freq.png` （或 `.svg`）
- 目标频率幅度数据：`1-target_freq.txt` （格式：每行 `时间(秒)\t幅度(dB)`）

**图像要求**：
- 频谱图与梅尔频谱图必须保存为 **PNG** 格式，建议使用 `stb_image_write` 单头文件库，保证零外部依赖。
- 目标频率曲线图可保存为 **SVG** 矢量图（用文本绘制折线，跨平台无依赖）或同样用 `stb_image_write` 生成 PNG 折线图。
- 所有图片需能在 Windows、macOS、Linux 上直接打开。

## 三、平台适配与 RVV 加速

- 项目使用 **C++ 和 CMake** 构建。
- **RISC-V 平台**：若编译器支持 **RVV（RISC-V Vector Extension）**，STFT 核心计算必须使用 RVV 向量指令加速。其他平台使用常规标量实现。
- **代码组织**：
  - `stft.h` 提供统一接口。
  - 实现分为 `stft_generic.cpp`（标量）与 `stft_rvv.cpp`（RVV 加速，内部用 `#ifdef __riscv_vector` 保护）。
  - CMake 自动检测平台及 V 扩展，选择对应源文件，并定义宏 `USE_RVV_STFT`。
- **运行时日志** 必须清晰输出：`Platform: RISC-V / Other` 以及 `RVV STFT acceleration: Enabled / Disabled`。

## 四、配置与开关（settings.h）

所有可调参数集中在 `project2/settings.h`，使用宏定义：

```cpp
#define TARGET_FREQUENCY     1000.0f   // 要提取幅度时间序列的目标频率 (Hz)
#define DATASET_PATH         "../dataset" // 测试音频文件夹路径
#define ENABLE_UAC_INPUT     0         // 是否启用 UAC 实时输入 (1=启用)
#define ENABLE_WEB_SPECTROGRAM 0       // 是否启用网页实时频谱推送 (1=启用)
```

## 五、实时 UAC 输入接口设计

- 定义抽象接口 `IAudioInputStream`，含 `start()`, `stop()`, `read(float* buffer, int frames)` 等纯虚函数。
- 当 `ENABLE_UAC_INPUT=1` 时，提供 `UacAudioStream` 实现（基于 PortAudio），自动枚举并打开默认 USB Audio 设备。若宏为 0，相关代码完全不编译。

## 六、网页实时频谱图推送接口

- 当 `ENABLE_WEB_SPECTROGRAM=1` 时：
  - 程序内部启动一个轻量级 WebSocket 服务器（可使用 `libwebsockets` 或 `cpp-httplib` 的 WebSocket 支持），监听本地 `9002` 端口。
  - 每完成一帧 STFT，将频率幅值数组打包为 JSON 推送给所有客户端。
  - 同时提供一个内嵌的 `index.html` 页面（可硬编码为 C++ 字符串），利用 Canvas 实时绘制频谱图。用户手册需说明如何访问该页面（如浏览器打开 `http://localhost:9002`）。

## 七、项目文件结构

```
project2/
├── CMakeLists.txt
├── settings.h
├── stft.h
├── stft_generic.cpp
├── stft_rvv.cpp
├── audio_io.cpp / audio_io.h        # FFmpeg 解码与 WAV 读写
├── uac_input.cpp / uac_input.h      # UAC 输入（受 ENABLE_UAC_INPUT 控制）
├── web_spectrogram.cpp / web_spectrogram.h # 网页推送（受 ENABLE_WEB_SPECTROGRAM 控制）
├── main.cpp
├── result/                          # 运行后自动创建
├── README.md
└── 用户手册.md
```

## 八、文档要求

在 `project2` 目录下生成两份文档：

1. **README.md**：项目总体介绍、文件结构、依赖说明（FFmpeg, PortAudio, stb 等）、如何在 x86/ARM/RISC-V 上通过 CMake 配置与编译、如何修改 `settings.h`、如何开启 UAC 与网页功能。
2. **用户手册.md**：面向用户的操作步骤，包括编译、运行批量处理、查看结果图像、连接 UAC 设备、打开网页实时频谱的详细方法及注意事项。

请严格按照上述所有要求，编写结构清晰、平台自适应、可成功编译运行的完整 C++/CMake 项目。此外，请在生成代码的时候添加详细中文注释，中文注释格式以utf-8格式进行编码。