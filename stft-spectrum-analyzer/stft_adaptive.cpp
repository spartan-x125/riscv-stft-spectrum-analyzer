#include "stft_adaptive.h"

#if defined(USE_RVV_STFT) && defined(__riscv_vector)
#include <riscv_vector.h>
#if defined(__riscv_v_intrinsic)
#define RVV_VSETVL_E32M1 __riscv_vsetvl_e32m1
#else
#define RVV_VSETVL_E32M1 vsetvl_e32m1
#endif
#endif

StftVectorizationMode SelectStftVectorizationMode(std::size_t frame_size,
                                                  std::size_t frame_count) {
    /*
     * 功能：选择 STFT 执行模式。
     * 核心：运行时用 vsetvl 探测 e32m1 最大 lane 数；帧数充足时优先跨帧并行。
     * 参数：frame_size 为单帧点数；frame_count 为待处理帧数。
     * 返回：Scalar、IntraFrame 或 MultiFrame。
     * 平台：无 RVV 时固定返回 Scalar。
     */
#if defined(USE_RVV_STFT) && defined(__riscv_vector)
    const std::size_t floats_per_reg = RVV_VSETVL_E32M1(65536);
    if (frame_count >= floats_per_reg && frame_size >= 256) {
        return StftVectorizationMode::MultiFrame;
    }
    if (frame_size >= 64) {
        return StftVectorizationMode::IntraFrame;
    }
#else
    (void)frame_size;
    (void)frame_count;
#endif
    return StftVectorizationMode::Scalar;
}

const char* StftVectorizationModeName(StftVectorizationMode mode) {
    // 功能：把执行模式转换为日志文本；参数：执行模式枚举；返回：静态字符串。
    switch (mode) {
        case StftVectorizationMode::Scalar:
            return "Scalar";
        case StftVectorizationMode::IntraFrame:
            return "IntraFrame";
        case StftVectorizationMode::MultiFrame:
            return "MultiFrame";
    }
    return "Unknown";
}
