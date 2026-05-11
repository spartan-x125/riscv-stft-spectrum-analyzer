# project1

C++/CMake audio FFT extractor for RISC-V deployment experiments.

## Features

- Scans the dataset directory defined by `include/settings.h`.
- Computes an FFT for each supported audio file.
- Extracts the magnitude of the FFT bin nearest to `TARGETFREQUENCY` (default `1000.0` Hz).
- Writes one same-stem `.txt` file per audio file into `dataset/result`.
- Uses a portable scalar FFT on normal platforms and selects the RISC-V build hook when compiled for RISC-V with RVV enabled.
- Decodes WAV natively. Other common formats such as MP3, FLAC, OGG, M4A, AAC, and WMA are decoded through `ffmpeg` when it is installed.

## Build

```bash
cmake -S project1 -B project1/build
cmake --build project1/build
```

## Run

From the workspace root:

```bash
./project1/build/project1
```

On Windows with the default generator:

```powershell
.\project1\build\Debug\project1.exe
```

## RISC-V RVV Build Note

Use a RISC-V compiler and pass the correct architecture and ABI flags for your chip or SDK, for example:

```bash
riscv64-unknown-elf-g++ -O3 -std=c++17 -march=rv64gcv -mabi=lp64d
```

The RVV-specific branch is isolated in `src/fft.cpp` and guarded by `__riscv` and `__riscv_vector`.
