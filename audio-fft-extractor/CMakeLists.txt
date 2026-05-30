cmake_minimum_required(VERSION 3.16)

project(audio-fft-extractor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(audio-fft-extractor
    src/audio_loader.cpp
    src/fft.cpp
    src/main.cpp
)

target_include_directories(audio-fft-extractor PRIVATE include)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(audio-fft-extractor PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (CMAKE_SYSTEM_PROCESSOR MATCHES "riscv|RISC-V")
    message(STATUS "Configuring for RISC-V. Enable RVV intrinsics with -march=rv64gcv or a matching target in your toolchain.")
endif()
