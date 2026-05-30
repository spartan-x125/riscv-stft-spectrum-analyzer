#include "uac_input.h"

#if ENABLE_UAC_INPUT
#if defined(PROJECT2_HAS_PORTAUDIO)
#include <portaudio.h>

UacAudioStream::UacAudioStream() {
    // 功能：初始化 PortAudio UAC 输入对象；参数：无；返回：构造对象；平台：仅 ENABLE_UAC_INPUT=1。
    Pa_Initialize();
}

UacAudioStream::~UacAudioStream() {
    // 功能：停止流并释放 PortAudio；参数：无；返回：无；平台：仅 ENABLE_UAC_INPUT=1。
    stop();
    Pa_Terminate();
}

bool UacAudioStream::start() {
    // 功能：打开默认单声道输入设备；参数：无；返回：启动是否成功；平台：需要 PortAudio。
    PaStream* stream = nullptr;
    const PaError err = Pa_OpenDefaultStream(&stream, 1, 0, paFloat32, 44100, 512, nullptr, nullptr);
    if (err != paNoError) {
        return false;
    }
    stream_ = stream;
    return Pa_StartStream(stream) == paNoError;
}

void UacAudioStream::stop() {
    // 功能：停止并关闭输入流；参数：无；返回：无；平台：需要 PortAudio。
    if (stream_ != nullptr) {
        Pa_StopStream(static_cast<PaStream*>(stream_));
        Pa_CloseStream(static_cast<PaStream*>(stream_));
        stream_ = nullptr;
    }
}

int UacAudioStream::read(float* buffer, int frames) {
    // 功能：读取实时 PCM；参数：输出缓冲区和帧数；返回：实际读取帧数；平台：需要 PortAudio。
    if (stream_ == nullptr) {
        return 0;
    }
    return Pa_ReadStream(static_cast<PaStream*>(stream_), buffer, frames) == paNoError ? frames : 0;
}
#else
#error "ENABLE_UAC_INPUT=1 requires PortAudio development files"
#endif
#endif
