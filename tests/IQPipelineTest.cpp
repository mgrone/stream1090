/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Presets.hpp"

namespace {

bool disabledStageLeavesSamplesUnchanged() {
    DCRemoval stage(0.5f, false);
    int16_t i = 8;
    int16_t q = -4;

    stage.apply(i, q);
    return i == 8 && q == -4;
}

bool enabledStageTracksAndRemovesTheAverage() {
    DCRemoval stage(0.5f);
    int16_t i = 8;
    int16_t q = -4;

    stage.apply(i, q);
    if (i != 8 || q != -4)
        return false;

    i = 8;
    q = -4;
    stage.apply(i, q);
    return i == 4 && q == -2;
}

bool rtlFilteredPipelinesHonorDcRemoval() {
    using FileSelector = IQPipelineSelector<Rate_2_4_Mhz, Rate_8_0_Mhz,
        IQPipelineOptions::IQ_FIR_RTL_SDR_FILE>;

    auto disabled = FileSelector::make(std::vector<float>{ 1.0f }, false);
    auto enabled = FileSelector::make(std::vector<float>{ 1.0f }, true);

    int32_t disabledMagnitude = 0;
    int32_t enabledMagnitude = 0;
    for (int sample = 0; sample < 20000; ++sample) {
        disabledMagnitude = disabled.process(8000, -4000);
        enabledMagnitude = enabled.process(8000, -4000);
    }

    using BuiltInSelector = IQPipelineSelector<Rate_2_4_Mhz, Rate_8_0_Mhz,
        IQPipelineOptions::IQ_FIR_RTL_SDR>;
    const auto builtIn = BuiltInSelector::make({}, true);

    return enabledMagnitude < disabledMagnitude / 1000
        && builtIn.toString().find("[DCRemoval] alpha: 1/2048")
            != std::string::npos;
}

} // namespace

int main() {
    return disabledStageLeavesSamplesUnchanged()
            && enabledStageTracksAndRemovesTheAverage()
            && rtlFilteredPipelinesHonorDcRemoval()
        ? 0
        : 1;
}
