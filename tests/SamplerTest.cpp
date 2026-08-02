#include "Sampler.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

int main() {
    using Sampler = Sampler_2_56_to_8_0_Mhz;

    std::vector<int32_t> input(Sampler::InputBufferSize + Sampler::InputBufferOverlap);
    std::vector<int32_t> output(Sampler::SampleBufferSize);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<int32_t>(i);

    Sampler::sample(input.data(), output.data());

    // A ramp resampled by 8/25 comes back out as the same ramp. The weighted
    // average now divides by 25 in integers, so each output is the exact value
    // with the fraction dropped, which keeps the error below one. A wrong
    // weight or denominator makes the error grow with the index instead, so
    // this still catches the thing the test is here for.
    for (size_t i = 0; i < output.size(); ++i) {
        const double expected = static_cast<double>(i) * 8.0 / 25.0;
        if (std::fabs(static_cast<double>(output[i]) - expected) > 1.0)
            return 1;
    }
    return 0;
}
