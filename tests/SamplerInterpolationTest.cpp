/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "SamplerFunc.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

int main() {
    constexpr size_t RatioInput = 1;
    constexpr size_t RatioOutput = 5;
    constexpr size_t NumBlocks = 16;
    static_assert(SamplerFunc<1, 5, NumBlocks>::UseSharpeningCubic);
    static_assert(SamplerFunc<8, 25, NumBlocks>::UseSharpeningCubic);
    static_assert(SamplerFunc<16, 75, NumBlocks>::UseSharpeningCubic);
    static_assert(!SamplerFunc<2, 5, NumBlocks>::UseSharpeningCubic);
    std::array<int32_t, RatioInput * NumBlocks + 1> input{};
    std::array<int32_t, RatioOutput * NumBlocks> output{};

    for (size_t i = 0; i < input.size(); ++i)
        input[i] = int32_t(i * 1000);

    SamplerFunc<RatioInput, RatioOutput, NumBlocks>::sample(input.data(), output.data());

    bool differsFromLinear = false;
    for (size_t i = 0; i < output.size(); ++i) {
        const double expected = double(i) * double(RatioInput) * 1000.0
                              / double(RatioOutput);
        differsFromLinear |= std::fabs(double(output[i]) - expected) > 4.0;
    }

    if (!differsFromLinear) {
        std::cerr << "selected kernel produced the linear result\n";
        return 1;
    }

    input.fill(100000);
    output.fill(0);
    SamplerFunc<RatioInput, RatioOutput, NumBlocks>::sample(input.data(), output.data());
    for (size_t i = 0; i < output.size(); ++i) {
        if (std::abs(output[i] - 100000) > 8) {
            std::cerr << "constant sample " << i << ": got " << output[i] << '\n';
            return 1;
        }
    }
    return 0;
}
