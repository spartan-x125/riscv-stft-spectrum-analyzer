#include <audio_io.h>

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
    // 功能：统一扩展名大小写；参数：输入文本；返回：小写文本。
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

uint16_t ReadU16(const unsigned char* data) {
    // 功能：读取小端 16 位整数；参数：至少 2 字节缓冲区；返回：解码后的整数。
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t ReadU32(const unsigned char* data) {
    // 功能：读取小端 32 位整数；参数：至少 4 字节缓冲区；返回：解码后的整数。
    return static_cast<uint32_t>(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

int32_t ReadS24(const unsigned char* data) {
    // 功能：读取带符号小端 24 位整数；参数：至少 3 字节缓冲区；返回：符号扩展后的整数。
    int32_t value = static_cast<int32_t>(data[0] | (data[1] << 8) | (data[2] << 16));
    if ((value & 0x00800000) != 0) {
        value |= static_cast<int32_t>(0xFF000000);
    }
    return value;
}

float DecodePcmSample(const unsigned char* data, uint16_t audio_format, uint16_t bits_per_sample) {
    // 功能：把一个 WAV PCM/float 样本归一化为 float；参数：样本地址、编码和位深；返回：[-1,1] 附近浮点值。
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
    // 功能：原生读取 WAV；核心：解析 RIFF 块并保留每个声道；参数：文件路径；返回：AudioBuffer。
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
    audio.channel_samples.resize(channels);
    for (auto& samples : audio.channel_samples) {
        samples.reserve(pcm.size() / frame_size);
    }

    for (std::size_t offset = 0; offset + frame_size <= pcm.size(); offset += frame_size) {
        for (uint16_t ch = 0; ch < channels; ++ch) {
            audio.channel_samples[ch].push_back(
                DecodePcmSample(pcm.data() + offset + ch * bytes_per_sample, audio_format, bits_per_sample));
        }
    }
    return audio;
}

std::string QuotePath(const std::filesystem::path& path) {
    // 功能：为命令行路径添加引号；参数：文件路径；返回：可传给 shell 的文本。
    std::string text = path.string();
    std::string quoted = "\"";
    for (char ch : text) {
        quoted += ch == '"' ? "\\\"" : std::string(1, ch);
    }
    quoted += "\"";
    return quoted;
}

unsigned ProbeChannelCount(const std::filesystem::path& path) {
    const std::string command =
        "ffprobe -v error -select_streams a:0 -show_entries stream=channels "
        "-of default=noprint_wrappers=1:nokey=1 " +
        QuotePath(path);

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error("cannot start ffprobe command");
    }

    std::array<char, 64> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    if (pclose(pipe) != 0) {
        throw std::runtime_error("ffprobe command failed");
    }

    const unsigned channels = static_cast<unsigned>(std::stoul(output));
    if (channels == 0) {
        throw std::runtime_error("ffprobe returned an invalid channel count");
    }
    return channels;
}

AudioBuffer DecodeInterleavedFloatPcm(
    const std::vector<unsigned char>& bytes,
    unsigned sample_rate,
    unsigned channels) {
    const std::size_t frame_size = sizeof(float) * channels;
    if (bytes.empty() || channels == 0 || bytes.size() % frame_size != 0) {
        throw std::runtime_error("invalid interleaved float PCM layout");
    }

    AudioBuffer audio;
    audio.sample_rate = sample_rate;
    audio.channels = channels;
    audio.channel_samples.resize(channels);

    const std::size_t frame_count = bytes.size() / frame_size;
    for (auto& samples : audio.channel_samples) {
        samples.reserve(frame_count);
    }

    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (unsigned ch = 0; ch < channels; ++ch) {
            float sample = 0.0f;
            std::memcpy(&sample, bytes.data() + frame * frame_size + ch * sizeof(float), sizeof(float));
            audio.channel_samples[ch].push_back(sample);
        }
    }
    return audio;
}

AudioBuffer DecodeWithFfmpegCli(const std::filesystem::path& path) {
    // 功能：调用 ffmpeg 命令解码；核心：读取 stdout 的多声道交错 f32le PCM；参数：文件路径；返回：AudioBuffer。
    const unsigned channels = ProbeChannelCount(path);
    const std::string command =
        "ffmpeg -v error -i " + QuotePath(path) +
        " -map 0:a:0 -f f32le -acodec pcm_f32le -ac " + std::to_string(channels) +
        " -ar " + std::to_string(kFallbackSampleRate) + " -";

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

    return DecodeInterleavedFloatPcm(bytes, kFallbackSampleRate, channels);
}

#if defined(PROJECT2_HAS_FFMPEG)
AudioBuffer DecodeWithFfmpegLibrary(const std::filesystem::path& path) {
    // 功能：使用 FFmpeg 库解码；核心：libavcodec 解码后由 swresample 转多声道 float；参数：路径；返回：AudioBuffer。
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

    const int output_channels = codec_ctx->ch_layout.nb_channels;
    if (output_channels <= 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format);
        throw std::runtime_error("invalid decoded channel count");
    }

    SwrContext* swr = swr_alloc();
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, output_channels);
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
    audio.channels = static_cast<unsigned>(output_channels);
    audio.channel_samples.resize(audio.channels);

    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == stream_index && avcodec_send_packet(codec_ctx, packet) == 0) {
            while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                std::vector<float> converted(static_cast<std::size_t>(frame->nb_samples) * audio.channels);
                uint8_t* out_data[] = {reinterpret_cast<uint8_t*>(converted.data())};
                const int converted_samples =
                    swr_convert(swr, out_data, frame->nb_samples, const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                if (converted_samples < 0) {
                    throw std::runtime_error("swr_convert failed");
                }
                for (int sample = 0; sample < converted_samples; ++sample) {
                    for (unsigned ch = 0; ch < audio.channels; ++ch) {
                        audio.channel_samples[ch].push_back(converted[static_cast<std::size_t>(sample) * audio.channels + ch]);
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swr);
    av_channel_layout_uninit(&out_layout);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format);
    return audio;
}
#endif

}  // namespace

bool IsAudioFile(const std::filesystem::path& path) {
    // 功能：判断扩展名是否受支持；参数：文件路径；返回：支持时为 true。
    const std::string ext = ToLower(path.extension().string());
    return ext == ".wav" || ext == ".wave" || ext == ".mp3" || ext == ".flac" ||
           ext == ".ogg" || ext == ".m4a" || ext == ".aac" || ext == ".wma";
}

AudioBuffer DecodeAudioFile(const std::filesystem::path& path) {
    // 功能：选择音频解码后端；参数：文件路径；返回：按声道保存的 float PCM；平台：根据 FFmpeg 库可用性条件编译。
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
    // 功能：描述当前音频后端；返回：用于运行日志的文本；平台：根据 FFmpeg 库可用性条件编译。
#if defined(PROJECT2_HAS_FFMPEG)
    return "FFmpeg libraries: enabled";
#else
    return "FFmpeg libraries: not found; WAV native decoder plus optional ffmpeg command fallback";
#endif
}
