#include "audio_io.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define popen _popen
#define pclose _pclose
#endif

#if defined(PROJECT2_HAS_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}
#endif

namespace {

constexpr unsigned kFallbackSampleRate = 44100;

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

int32_t ReadS24(const unsigned char* data) {
    int32_t value = static_cast<int32_t>(data[0] | (data[1] << 8) | (data[2] << 16));
    if ((value & 0x00800000) != 0) {
        value |= static_cast<int32_t>(0xFF000000);
    }
    return value;
}

float DecodePcmSample(const unsigned char* data, uint16_t audio_format, uint16_t bits_per_sample) {
    if (audio_format == 3) {
        if (bits_per_sample == 32) {
            float value = 0.0f;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }
        if (bits_per_sample == 64) {
            double value = 0.0;
            std::memcpy(&value, data, sizeof(value));
            return static_cast<float>(value);
        }
    }

    if (audio_format != 1 && audio_format != 0xFFFE) {
        throw std::runtime_error("unsupported WAV encoding");
    }

    switch (bits_per_sample) {
        case 8:
            return (static_cast<int>(data[0]) - 128) / 128.0f;
        case 16:
            return static_cast<int16_t>(ReadU16(data)) / 32768.0f;
        case 24:
            return ReadS24(data) / 8388608.0f;
        case 32:
            return static_cast<int32_t>(ReadU32(data)) / 2147483648.0f;
        default:
            throw std::runtime_error("unsupported WAV bit depth");
    }
}

AudioBuffer DecodeWavNative(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open file: " + path.string());
    }

    std::array<unsigned char, 12> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size()) ||
        std::memcmp(header.data(), "RIFF", 4) != 0 ||
        std::memcmp(header.data() + 8, "WAVE", 4) != 0) {
        throw std::runtime_error("invalid WAV file: " + path.string());
    }

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    std::vector<unsigned char> pcm;

    while (input) {
        std::array<unsigned char, 8> chunk_header{};
        input.read(reinterpret_cast<char*>(chunk_header.data()), static_cast<std::streamsize>(chunk_header.size()));
        if (input.gcount() == 0) {
            break;
        }
        if (input.gcount() != static_cast<std::streamsize>(chunk_header.size())) {
            throw std::runtime_error("truncated WAV chunk header");
        }

        const uint32_t chunk_size = ReadU32(chunk_header.data() + 4);
        std::vector<unsigned char> chunk(chunk_size);
        input.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        if (input.gcount() != static_cast<std::streamsize>(chunk.size())) {
            throw std::runtime_error("truncated WAV chunk");
        }
        if ((chunk_size & 1U) != 0) {
            input.ignore(1);
        }

        if (std::memcmp(chunk_header.data(), "fmt ", 4) == 0) {
            audio_format = ReadU16(chunk.data());
            channels = ReadU16(chunk.data() + 2);
            sample_rate = ReadU32(chunk.data() + 4);
            bits_per_sample = ReadU16(chunk.data() + 14);
        } else if (std::memcmp(chunk_header.data(), "data", 4) == 0) {
            pcm = std::move(chunk);
        }
    }

    if (channels == 0 || sample_rate == 0 || bits_per_sample == 0 || pcm.empty()) {
        throw std::runtime_error("missing WAV fmt/data chunk");
    }

    const std::size_t bytes_per_sample = bits_per_sample / 8;
    const std::size_t frame_size = bytes_per_sample * channels;
    if (bytes_per_sample == 0 || frame_size == 0 || pcm.size() % frame_size != 0) {
        throw std::runtime_error("invalid WAV sample layout");
    }

    AudioBuffer audio;
    audio.sample_rate = sample_rate;
    audio.channels = channels;
    audio.mono_samples.reserve(pcm.size() / frame_size);

    for (std::size_t offset = 0; offset + frame_size <= pcm.size(); offset += frame_size) {
        float sum = 0.0f;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            sum += DecodePcmSample(pcm.data() + offset + ch * bytes_per_sample, audio_format, bits_per_sample);
        }
        audio.mono_samples.push_back(sum / static_cast<float>(channels));
    }
    return audio;
}

std::string QuotePath(const std::filesystem::path& path) {
    std::string text = path.string();
    std::string quoted = "\"";
    for (char ch : text) {
        quoted += ch == '"' ? "\\\"" : std::string(1, ch);
    }
    quoted += "\"";
    return quoted;
}

AudioBuffer DecodeWithFfmpegCli(const std::filesystem::path& path) {
    const std::string command =
        "ffmpeg -v error -i " + QuotePath(path) +
        " -f f32le -acodec pcm_f32le -ac 1 -ar " + std::to_string(kFallbackSampleRate) + " -";

    FILE* pipe = popen(command.c_str(), "rb");
    if (pipe == nullptr) {
        throw std::runtime_error("cannot start ffmpeg command");
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
                throw std::runtime_error("failed reading ffmpeg output");
            }
        }
    }

    const int code = pclose(pipe);
    if (code != 0 || bytes.empty()) {
        throw std::runtime_error("ffmpeg command failed");
    }

    AudioBuffer audio;
    audio.sample_rate = kFallbackSampleRate;
    audio.channels = 1;
    audio.mono_samples.resize(bytes.size() / sizeof(float));
    std::memcpy(audio.mono_samples.data(), bytes.data(), audio.mono_samples.size() * sizeof(float));
    return audio;
}

#if defined(PROJECT2_HAS_FFMPEG)
AudioBuffer DecodeWithFfmpegLibrary(const std::filesystem::path& path) {
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path.string().c_str(), nullptr, nullptr) < 0) {
        throw std::runtime_error("avformat_open_input failed");
    }

    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        throw std::runtime_error("avformat_find_stream_info failed");
    }

    const int stream_index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        avformat_close_input(&format);
        throw std::runtime_error("no audio stream found");
    }

    AVCodecParameters* params = format->streams[stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, params);
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format);
        throw std::runtime_error("avcodec_open2 failed");
    }

    SwrContext* swr = swr_alloc();
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, 1);
    av_opt_set_chlayout(swr, "out_chlayout", &out_layout, 0);
    av_opt_set_int(swr, "out_sample_rate", codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
    av_opt_set_chlayout(swr, "in_chlayout", &codec_ctx->ch_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", codec_ctx->sample_fmt, 0);
    swr_init(swr);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AudioBuffer audio;
    audio.sample_rate = static_cast<unsigned>(codec_ctx->sample_rate);
    audio.channels = 1;

    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == stream_index && avcodec_send_packet(codec_ctx, packet) == 0) {
            while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                std::vector<float> converted(frame->nb_samples);
                uint8_t* out_data[] = {reinterpret_cast<uint8_t*>(converted.data())};
                swr_convert(swr, out_data, frame->nb_samples, const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                audio.mono_samples.insert(audio.mono_samples.end(), converted.begin(), converted.end());
            }
        }
        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swr);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format);
    return audio;
}
#endif

}  // namespace

bool IsAudioFile(const std::filesystem::path& path) {
    const std::string ext = ToLower(path.extension().string());
    return ext == ".wav" || ext == ".wave" || ext == ".mp3" || ext == ".flac" ||
           ext == ".ogg" || ext == ".m4a" || ext == ".aac" || ext == ".wma";
}

AudioBuffer DecodeAudioFile(const std::filesystem::path& path) {
#if defined(PROJECT2_HAS_FFMPEG)
    return DecodeWithFfmpegLibrary(path);
#else
    const std::string ext = ToLower(path.extension().string());
    if (ext == ".wav" || ext == ".wave") {
        return DecodeWavNative(path);
    }
    return DecodeWithFfmpegCli(path);
#endif
}

std::string AudioBackendDescription() {
#if defined(PROJECT2_HAS_FFMPEG)
    return "FFmpeg libraries: enabled";
#else
    return "FFmpeg libraries: not found; WAV native decoder plus optional ffmpeg command fallback";
#endif
}
