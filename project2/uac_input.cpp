#include "uac_input.h"

#if ENABLE_UAC_INPUT
#if defined(PROJECT2_HAS_PORTAUDIO)
#include <portaudio.h>

UacAudioStream::UacAudioStream() {
    Pa_Initialize();
}

UacAudioStream::~UacAudioStream() {
    stop();
    Pa_Terminate();
}

bool UacAudioStream::start() {
    PaStream* stream = nullptr;
    const PaError err = Pa_OpenDefaultStream(&stream, 1, 0, paFloat32, 44100, 512, nullptr, nullptr);
    if (err != paNoError) {
        return false;
    }
    stream_ = stream;
    return Pa_StartStream(stream) == paNoError;
}

void UacAudioStream::stop() {
    if (stream_ != nullptr) {
        Pa_StopStream(static_cast<PaStream*>(stream_));
        Pa_CloseStream(static_cast<PaStream*>(stream_));
        stream_ = nullptr;
    }
}

int UacAudioStream::read(float* buffer, int frames) {
    if (stream_ == nullptr) {
        return 0;
    }
    return Pa_ReadStream(static_cast<PaStream*>(stream_), buffer, frames) == paNoError ? frames : 0;
}
#else
#error "ENABLE_UAC_INPUT=1 requires PortAudio development files"
#endif
#endif
