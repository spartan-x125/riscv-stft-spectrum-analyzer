#include <web_spectrogram.h>

#if ENABLE_WEB_SPECTROGRAM
#include <iostream>

bool WebSpectrogramServer::start(int port) {
    // 功能：启动网页频谱接口占位服务；参数：监听端口；返回：启动成功标志。
    std::cout << "Web spectrogram placeholder server listening on port " << port << '\n';
    std::cout << "Open http://localhost:" << port << " after linking a WebSocket backend.\n";
    return true;
}

// 功能：停止网页频谱接口占位服务；参数：无；返回：无。
void WebSpectrogramServer::stop() {}

// 功能：发布一帧频谱占位接口；参数：dB 幅度数组；返回：无。
void WebSpectrogramServer::publishFrame(const std::vector<float>&) {}
#endif
