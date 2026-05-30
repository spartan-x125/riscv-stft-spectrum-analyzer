#include "audio_loader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define popen _popen
#define pclose _pclose
#endif

namespace {

constexpr unsigned kFfmpegSampleRate = 44100;

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

uint16_t ReadU16(const unsigned char* data) {
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t ReadU32(const unsigned char* data) {
    return static_cast<uint32_t>(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

int32_t ReadSigned24(const unsigned char* data) {
    int32_t value = static_cast<int32_t>(data[0] | (data[1] << 8) | (data[2] << 16));
    if ((value & 0x00800000) != 0) {
        value |= static_cast<int32_t>(0xFF000000);
    }
    return value;
}

double DecodeSample(const unsigned char* data, uint16_t audio_format, uint16_t bits_per_sample) {
    if (audio_format == 3) {
        if (bits_per_sample == 32) {
            float value = 0.0f;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }
        if (bits_per_sample == 64) {
            double value = 0.0;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }
    }

    if (audio_format != 1 && audio_format != 0xFFFE) {
        throw std::runtime_error("Unsupported WAV encoding");
    }

    switch (bits_per_sample) {
        case 8:
            return (static_cast<int>(data[0]) - 128) / 128.0;
        case 16:
            return static_cast<int16_t>(ReadU16(data)) / 32768.0;
        case 24:
            return ReadSigned24(data) / 8388608.0;
        case 32:
            return static_cast<int32_t>(ReadU32(data)) / 2147483648.0;
        default:
            throw std::runtime_error("Unsupported WAV bit depth");
    }
}

AudioData LoadWavFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open audio file: " + path.string());
    }

    std::array<unsigned char, 12> riff_header{};
    input.read(reinterpret_cast<char*>(riff_header.data()), static_cast<std::streamsize>(riff_header.size()));
    if (input.gcount() != static_cast<std::streamsize>(riff_header.size()) ||
        std::memcmp(riff_header.data(), "RIFF", 4) != 0 ||
        std::memcmp(riff_header.data() + 8, "WAVE", 4) != 0) {
        throw std::runtime_error("Invalid WAV file: " + path.string());
    }

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    std::vector<unsigned char> pcm_data;

    while (input) {
        std::array<unsigned char, 8> chunk_header{};
        input.read(reinterpret_cast<char*>(chunk_header.data()), static_cast<std::streamsize>(chunk_header.size()));
        if (input.gcount() == 0) {
            break;
        }
        if (input.gcount() != static_cast<std::streamsize>(chunk_header.size())) {
            throw std::runtime_error("Truncated WAV chunk header: " + path.string());
        }

        const uint32_t chunk_size = ReadU32(chunk_header.data() + 4);
        std::vector<unsigned char> chunk(chunk_size);
        if (chunk_size > 0) {
            input.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
            if (input.gcount() != static_cast<std::streamsize>(chunk.size())) {
                throw std::runtime_error("Truncated WAV chunk: " + path.string());
            }
        }
        if ((chunk_size & 1U) != 0) {
            input.ignore(1);
        }

        if (std::memcmp(chunk_header.data(), "fmt ", 4) == 0) {
            if (chunk.size() < 16) {
                throw std::runtime_error("Invalid WAV fmt chunk: " + path.string());
            }
            audio_format = ReadU16(chunk.data());
            channels = ReadU16(chunk.data() + 2);
            sample_rate = ReadU32(chunk.data() + 4);
            bits_per_sample = ReadU16(chunk.data() + 14);
        } else if (std::memcmp(chunk_header.data(), "data", 4) == 0) {
            pcm_data = std::move(chunk);
        }
    }

    if (channels == 0 || sample_rate == 0 || bits_per_sample == 0 || pcm_data.empty()) {
        throw std::runtime_error("Missing WAV fmt or data chunk: " + path.string());
    }

    const std::size_t bytes_per_sample = bits_per_sample / 8;
    const std::size_t frame_size = bytes_per_sample * channels;
    if (bytes_per_sample == 0 || frame_size == 0 || (pcm_data.size() % frame_size) != 0) {
        throw std::runtime_error("Invalid WAV sample layout: " + path.string());
    }

    AudioData audio;
    audio.sample_rate = sample_rate;
    audio.channels = channels;

    const std::size_t frame_count = pcm_data.size() / frame_size;
    audio.mono_samples.reserve(frame_count);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        double sum = 0.0;
        const unsigned char* frame_ptr = pcm_data.data() + frame * frame_size;
        for (uint16_t channel = 0; channel < channels; ++channel) {
            sum += DecodeSample(frame_ptr + channel * bytes_per_sample, audio_format, bits_per_sample);
        }
        audio.mono_samples.push_back(sum / static_cast<double>(channels));
    }

    return audio;
}

std::string QuoteForShell(const std::filesystem::path& path) {
    std::string value = path.string();
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
}

AudioData LoadWithFfmpeg(const std::filesystem::path& path) {
    const std::string command =
        "ffmpeg -v error -i " + QuoteForShell(path) +
        " -f s16le -acodec pcm_s16le -ac 1 -ar " + std::to_string(kFfmpegSampleRate) + " -";

    FILE* pipe = popen(command.c_str(), "rb");
    if (pipe == nullptr) {
        throw std::runtime_error("Cannot start ffmpeg for: " + path.string());
    }

#ifdef _WIN32
    _setmode(_fileno(pipe), _O_BINARY);
#endif

    std::vector<unsigned char> bytes;
    std::array<unsigned char, 8192> buffer{};
    while (true) {
        const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), pipe);
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read));
        if (read < buffer.size()) {
            if (std::feof(pipe) != 0) {
                break;
            }
            if (std::ferror(pipe) != 0) {
                pclose(pipe);
                throw std::runtime_error("Failed reading ffmpeg output: " + path.string());
            }
        }
    }

    const int exit_code = pclose(pipe);
    if (exit_code != 0 || bytes.empty()) {
        throw std::runtime_error("ffmpeg could not decode audio file: " + path.string());
    }

    AudioData audio;
    audio.sample_rate = kFfmpegSampleRate;
    audio.channels = 1;
    audio.mono_samples.reserve(bytes.size() / 2);
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        audio.mono_samples.push_back(static_cast<int16_t>(ReadU16(bytes.data() + i)) / 32768.0);
    }
    return audio;
}

}  // namespace

bool IsSupportedAudioExtension(const std::filesystem::path& path) {
    const std::string ext = ToLower(path.extension().string());
    return ext == ".wav" || ext == ".wave" || ext == ".mp3" || ext == ".flac" ||
           ext == ".ogg" || ext == ".m4a" || ext == ".aac" || ext == ".wma";
}

AudioData LoadAudioFile(const std::filesystem::path& path) {
    const std::string ext = ToLower(path.extension().string());
    if (ext == ".wav" || ext == ".wave") {
        return LoadWavFile(path);
    }
    return LoadWithFfmpeg(path);
}

std::string SupportedAudioDescription() {
    return "WAV is decoded natively; MP3/FLAC/OGG/M4A/AAC/WMA are decoded through ffmpeg when available.";
}
