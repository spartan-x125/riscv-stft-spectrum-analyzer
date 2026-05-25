cmake_minimum_required(VERSION 3.16)

project(project2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PROJECT2_SOURCES
    audio_io.cpp
    image_writer.cpp
    main.cpp
    stft_generic.cpp
    uac_input.cpp
    web_spectrogram.cpp
)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "riscv|RISC-V")
    list(APPEND PROJECT2_SOURCES stft_rvv.cpp)
endif()

add_executable(project2 ${PROJECT2_SOURCES})
target_include_directories(project2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(project2 PRIVATE -Wall -Wextra -Wpedantic)
elseif (MSVC)
    target_compile_options(project2 PRIVATE /utf-8)
endif()

include(CheckCXXCompilerFlag)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "riscv|RISC-V")
    target_compile_definitions(project2 PRIVATE USE_RVV_STFT)
    message(STATUS "Platform detection: RISC-V")
    message(STATUS "RVV source enabled. Pass -march=rv64gcv/-march=rv32gcv when your toolchain requires it.")
else()
    message(STATUS "Platform detection: Other")
endif()

find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavformat libavcodec libavutil libswresample)
    if (FFMPEG_FOUND)
        target_compile_definitions(project2 PRIVATE PROJECT2_HAS_FFMPEG)
        target_include_directories(project2 PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_directories(project2 PRIVATE ${FFMPEG_LIBRARY_DIRS})
        target_link_libraries(project2 PRIVATE ${FFMPEG_LIBRARIES})
        message(STATUS "FFmpeg libraries: found")
    else()
        message(STATUS "FFmpeg libraries: not found; using native WAV decoder and ffmpeg command fallback")
    endif()

    pkg_check_modules(PORTAUDIO QUIET portaudio-2.0)
    if (PORTAUDIO_FOUND)
        target_compile_definitions(project2 PRIVATE PROJECT2_HAS_PORTAUDIO)
        target_include_directories(project2 PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
        target_link_directories(project2 PRIVATE ${PORTAUDIO_LIBRARY_DIRS})
        target_link_libraries(project2 PRIVATE ${PORTAUDIO_LIBRARIES})
        message(STATUS "PortAudio: found")
    else()
        message(STATUS "PortAudio: not found; ENABLE_UAC_INPUT must remain 0")
    endif()
endif()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/result)
