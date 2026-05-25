#include "web_spectrogram.h"

#if ENABLE_WEB_SPECTROGRAM
#include <iostream>

bool WebSpectrogramServer::start(int port) {
    std::cout << "Web spectrogram placeholder server listening on port " << port << '\n';
    std::cout << "Open http://localhost:" << port << " after linking a WebSocket backend.\n";
    return true;
}

void WebSpectrogramServer::stop() {}

void WebSpectrogramServer::publishFrame(const std::vector<float>&) {}
#endif
