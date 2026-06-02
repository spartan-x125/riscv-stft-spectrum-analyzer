#pragma once

#include <settings.h>

#include <vector>

#if ENABLE_WEB_SPECTROGRAM
class WebSpectrogramServer {
public:
    bool start(int port = 9002);
    void stop();
    void publishFrame(const std::vector<float>& magnitudes_db);
};
#endif
