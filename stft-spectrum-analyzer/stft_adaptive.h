#pragma once

#include <cstddef>

enum class StftVectorizationMode {
    Scalar,
    IntraFrame,
    MultiFrame,
};

StftVectorizationMode SelectStftVectorizationMode(std::size_t frame_size,
                                                  std::size_t frame_count);

const char* StftVectorizationModeName(StftVectorizationMode mode);
