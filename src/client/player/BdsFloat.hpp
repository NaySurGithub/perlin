#pragma once

#include "client/player/BdsPhysicsNumerics.hpp"

#include <bit>
#include <cstdint>

namespace perlin::physics::bds::detail {
    inline bool IsNaN(float value) {
        return (std::bit_cast<uint32_t>(value) & 0x7fffffffu) > 0x7f800000u;
    }

    // Scalar SSE arithmetic preserves the destination NaN operand's payload, quiets
    // signaling NaNs, and generates negative canonical NaN for invalid finite/
    // infinity combinations. ARM's generated NaN sign differs, so make it explicit.
    template <class Operation> inline float SseBinary(float first, float second, Operation operation) {
        if (IsNaN(first))
            return std::bit_cast<float>(std::bit_cast<uint32_t>(first) | 0x00400000u);
        if (IsNaN(second))
            return std::bit_cast<float>(std::bit_cast<uint32_t>(second) | 0x00400000u);
        const float result = Round(operation(first, second));
        return IsNaN(result) ? std::bit_cast<float>(0xffc00000u) : result;
    }

    inline float SseAdd(float first, float second) {
        return SseBinary(first, second, [](float a, float b) { return a + b; });
    }
    inline float SseSubtract(float first, float second) {
        return SseBinary(first, second, [](float a, float b) { return a - b; });
    }
    inline float SseMultiply(float first, float second) {
        return SseBinary(first, second, [](float a, float b) { return a * b; });
    }
    inline float SseDivide(float first, float second) {
        return SseBinary(first, second, [](float a, float b) { return a / b; });
    }
}
