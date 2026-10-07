#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

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

// A tap set that does not fit Q15 must keep its shape: {0.25, 1.25, 0.25}
// used to run with the middle tap saturated at 1.0, as {0.25, 1.0, 0.25}.
bool oversizedTapsKeepTheirShape() {
    IQLowPassDynamic<> fir(std::vector<float>{0.25f, 1.25f, 0.25f});
    constexpr size_t N = 16;
    std::array<int16_t, N> I{}, Q{};
    I[0] = 8192;
    fir.applyBlock(I.data(), Q.data(), N);
    int16_t peak = 0, side = 0;
    for (size_t i = 0; i < N; ++i)
        if (I[i] > peak) peak = I[i];
    for (size_t i = 0; i < N; ++i)
        if (I[i] > side && I[i] < peak) side = I[i];
    if (peak <= 0) {
        std::cerr << "oversized taps produced no positive impulse response\n";
        return false;
    }
    const double ratio = double(side) / double(peak);
    if (ratio < 0.195 || ratio > 0.205) {
        std::cerr << "oversized taps changed shape: side/peak = " << ratio << " (want 0.2)\n";
        return false;
    }
    return true;
}

bool identityFallbackPreservesSamples() {
    IQLowPass<Rate_8_0_Mhz, Rate_8_0_Mhz> fir;
    constexpr size_t N = 257;
    std::array<int16_t, N> I{}, Q{};
    for (size_t i = 0; i < N; ++i) {
        I[i] = int16_t(int(i) * 127 - 16384);
        Q[i] = int16_t(16383 - int(i) * 127);
    }
    const auto expectedI = I, expectedQ = Q;
    fir.applyBlock(I.data(), Q.data(), N);
    if (I != expectedI || Q != expectedQ) {
        std::cerr << "identity fallback changed samples\n";
        return false;
    }
    return true;
}

bool negativeLimitPreservesSamples() {
    const std::vector<float> taps{0.25f, -1.0f, 0.25f};
    if (FirDetail::q15Scale(taps) != 1.0f) {
        std::cerr << "representable negative limit was scaled\n";
        return false;
    }
    IQLowPassDynamic<> fir(taps);
    std::array<int16_t, 16> I{}, Q{};
    I[0] = 8192;
    Q[0] = -8192;
    fir.applyBlock(I.data(), Q.data(), I.size());
    std::array<int16_t, 16> expectedI{}, expectedQ{};
    expectedI[1] = 2048;
    expectedI[2] = -8192;
    expectedI[3] = 2048;
    expectedQ[1] = -2048;
    expectedQ[2] = 8192;
    expectedQ[3] = -2048;
    if (I != expectedI || Q != expectedQ) {
        std::cerr << "negative limit changed impulse response\n";
        return false;
    }
    return true;
}

bool oversizedNegativeTapsFitQ15() {
    const std::vector<float> taps{0.25f, -1.25f, 0.25f};
    const float scale = FirDetail::q15Scale(taps);
    if (scale != 0.8f || FirDetail::toQ15(taps[1] * scale) != -32768) {
        std::cerr << "oversized negative taps did not use the full Q15 range\n";
        return false;
    }
    return true;
}

static_assert(FirDetail::tapsFitQ15(LowPassTaps::getCustomTaps<Rate_2_4_Mhz, Rate_8_0_Mhz>()));
static_assert(!FirDetail::tapsFitQ15(std::array<float, 3>{0.25f, 1.25f, 0.25f}));
static_assert(FirDetail::tapsFitQ15(std::array<float, 1>{-1.0f}));
static_assert(!FirDetail::tapsFitQ15(std::array<float, 1>{1.0f}));

} // namespace

int main() {
    if (!oversizedTapsKeepTheirShape() || !identityFallbackPreservesSamples() ||
        !negativeLimitPreservesSamples() || !oversizedNegativeTapsFitQ15())
        return 1;

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
