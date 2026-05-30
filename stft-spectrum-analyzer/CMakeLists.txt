cmake_minimum_required(VERSION 3.16)

project(stft-spectrum-analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PROJECT2_SCALAR_SOURCES
    audio_io.cpp
    image_writer_scalar.cpp
    main.cpp
    stft_adaptive.cpp
    stft_scalar.cpp
    uac_input.cpp
    web_spectrogram.cpp
)

set(PROJECT2_RVV_SOURCES
    image_writer_rvv.cpp
    stft_intra_rvv.cpp
    stft_multi_rvv.cpp
)

set(PROJECT2_SOURCES ${PROJECT2_SCALAR_SOURCES})
if (CMAKE_SYSTEM_PROCESSOR MATCHES "riscv|RISC-V")
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #ifndef __riscv_vector
        #error RVV extension is not enabled
        #endif
        #include <riscv_vector.h>
        int main() { return 0; }
    " PROJECT2_COMPILER_HAS_RVV)
    if (PROJECT2_COMPILER_HAS_RVV)
        list(APPEND PROJECT2_SOURCES ${PROJECT2_RVV_SOURCES})
    endif()
endif()

add_executable(stft-spectrum-analyzer ${PROJECT2_SOURCES})
target_include_directories(stft-spectrum-analyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(stft-spectrum-analyzer PRIVATE -Wall -Wextra -Wpedantic)
elseif (MSVC)
    target_compile_options(stft-spectrum-analyzer PRIVATE /utf-8)
endif()

if (CMAKE_SYSTEM_PROCESSOR MATCHES "riscv|RISC-V")
    message(STATUS "Platform detection: RISC-V")
    if (PROJECT2_COMPILER_HAS_RVV)
        target_compile_definitions(stft-spectrum-analyzer PRIVATE USE_RVV_STFT)
        message(STATUS "RVV STFT source: enabled")
    else()
        message(STATUS "RVV STFT source: disabled; pass -march=rv64gcv/-march=rv32gcv when supported")
    endif()
else()
    message(STATUS "Platform detection: Other")
endif()

find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavformat libavcodec libavutil libswresample)
    if (FFMPEG_FOUND)
        target_compile_definitions(stft-spectrum-analyzer PRIVATE PROJECT2_HAS_FFMPEG)
        target_include_directories(stft-spectrum-analyzer PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_directories(stft-spectrum-analyzer PRIVATE ${FFMPEG_LIBRARY_DIRS})
        target_link_libraries(stft-spectrum-analyzer PRIVATE ${FFMPEG_LIBRARIES})
        message(STATUS "FFmpeg libraries: found")
    else()
        message(STATUS "FFmpeg libraries: not found; using native WAV decoder and ffmpeg command fallback")
    endif()

    pkg_check_modules(PORTAUDIO QUIET portaudio-2.0)
    if (PORTAUDIO_FOUND)
        target_compile_definitions(stft-spectrum-analyzer PRIVATE PROJECT2_HAS_PORTAUDIO)
        target_include_directories(stft-spectrum-analyzer PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
        target_link_directories(stft-spectrum-analyzer PRIVATE ${PORTAUDIO_LIBRARY_DIRS})
        target_link_libraries(stft-spectrum-analyzer PRIVATE ${PORTAUDIO_LIBRARIES})
        message(STATUS "PortAudio: found")
    else()
        message(STATUS "PortAudio: not found; ENABLE_UAC_INPUT must remain 0")
    endif()
endif()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/result)
