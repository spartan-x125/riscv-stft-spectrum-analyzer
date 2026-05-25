#pragma once

#include "settings.h"

class IAudioInputStream {
public:
    virtual ~IAudioInputStream() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual int read(float* buffer, int frames) = 0;
};

#if ENABLE_UAC_INPUT
class UacAudioStream final : public IAudioInputStream {
public:
    UacAudioStream();
    ~UacAudioStream() override;
    bool start() override;
    void stop() override;
    int read(float* buffer, int frames) override;

private:
    void* stream_ = nullptr;
};
#endif
