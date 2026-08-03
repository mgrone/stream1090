#include "Sampler.hpp"

#include <cmath>
#include <vector>

int main() {
    using Sampler = Sampler_2_56_to_8_0_Mhz;

    std::vector<float> input(Sampler::InputBufferSize + Sampler::InputBufferOverlap);
    std::vector<float> output(Sampler::SampleBufferSize);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(i);

    Sampler::sample(input.data(), output.data());

    for (size_t i = 0; i < output.size(); ++i) {
        const float expected = static_cast<float>(i) * 8.0f / 25.0f;
        if (std::fabs(output[i] - expected) > 2e-3f)
            return 1;
    }
    return 0;
}
