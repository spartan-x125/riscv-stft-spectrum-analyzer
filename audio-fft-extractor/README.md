# audio-fft-extractor

C++/CMake 单频点 FFT 幅度提取器，用于在 RISC-V 或普通桌面平台上批量分析音频文件。

## 功能

- 扫描 `dataset` 目录中的音频文件。
- 对每个音频执行 FFT。
- 提取最接近 `TARGETFREQUENCY` 的频点幅值。
- 输出同名 `.txt` 结果文件。
- 输出同名 `_spectrum.svg` 频谱图。
- WAV 原生解码；MP3/FLAC/OGG/M4A/AAC/WMA 可通过系统 `ffmpeg` 命令解码。

## 编译

```bash
cmake -S audio-fft-extractor -B audio-fft-extractor/build -DCMAKE_BUILD_TYPE=Release
cmake --build audio-fft-extractor/build
```

Windows：

```powershell
cmake -S audio-fft-extractor -B audio-fft-extractor/build
cmake --build audio-fft-extractor/build --config Release
```

## 运行

```bash
./audio-fft-extractor/build/audio-fft-extractor
```

Windows：

```powershell
.\audio-fft-extractor\build\Release\audio-fft-extractor.exe
```

输出在：

```text
audio-fft-extractor/result/
```

## RISC-V RVV

使用支持 RVV 的 RISC-V 工具链时，传入芯片匹配的 `-march` 和 `-mabi`，例如：

```bash
cmake -S audio-fft-extractor -B audio-fft-extractor/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -march=rv64gcv -mabi=lp64d"
cmake --build audio-fft-extractor/build
```

RVV 相关代码位于 `src/fft.cpp`，由 `__riscv` 和 `__riscv_vector` 宏保护。
