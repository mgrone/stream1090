#include <array>
#include <cstdint>
#include <iostream>

#include "LowPassFilter.hpp"

namespace {

template<size_t NumTaps>
bool symmetricMatchesPlain(const std::array<int16_t, NumTaps>& taps) {
    constexpr size_t Count = 257;
    std::array<int16_t, Count + NumTaps> inputI{};
    std::array<int16_t, Count + NumTaps> inputQ{};
    std::array<int16_t, Count> plainI{};
    std::array<int16_t, Count> plainQ{};
    std::array<int16_t, Count> symmetricI{};
    std::array<int16_t, Count> symmetricQ{};

    uint32_t state = 0x13579bdu;
    for (size_t i = 0; i < inputI.size(); ++i) {
        state = state * 1664525u + 1013904223u;
        inputI[i] = int16_t((state >> 17) - 16384);
        state = state * 1664525u + 1013904223u;
        inputQ[i] = int16_t((state >> 17) - 16384);
    }

    FirDetail::firBlock<false>(taps.data(), taps.size(), inputI.data(), inputQ.data(),
                                plainI.data(), plainQ.data(), Count);
    FirDetail::firBlock<true>(taps.data(), taps.size(), inputI.data(), inputQ.data(),
                               symmetricI.data(), symmetricQ.data(), Count);
    return plainI == symmetricI && plainQ == symmetricQ;
}

template<SampleRate InputRate, SampleRate OutputRate>
bool builtInSymmetricMatchesPlain() {
    constexpr auto source = LowPassTaps::getCustomTaps<InputRate, OutputRate>();
    constexpr auto taps = [source] {
        std::array<int16_t, source.size()> result{};
        for (size_t i = 0; i < source.size(); ++i)
            result[i] = FirDetail::toQ15(source[i]);
        return result;
    }();
    static_assert(LowPassTaps::areCustomTapsSymmetric<InputRate, OutputRate>());
    return symmetricMatchesPlain(taps);
}

} // namespace

int main() {
    constexpr std::array<int16_t, 15> oddTaps{
        -81, -66, 781, 1019, 1741, 3178, 1865, 15891,
        1865, 3178, 1741, 1019, 781, -66, -81
    };
    constexpr std::array<int16_t, 6> evenTaps{328, 655, 1311, 1311, 655, 328};

    if (!symmetricMatchesPlain(oddTaps) || !symmetricMatchesPlain(evenTaps) ||
        !builtInSymmetricMatchesPlain<Rate_2_4_Mhz, Rate_8_0_Mhz>() ||
        !builtInSymmetricMatchesPlain<Rate_6_0_Mhz, Rate_24_0_Mhz>() ||
        !builtInSymmetricMatchesPlain<Rate_10_0_Mhz, Rate_24_0_Mhz>()) {
        std::cerr << "Symmetric FIR changed fixed-point output\n";
        return 1;
    }
    return 0;
}
