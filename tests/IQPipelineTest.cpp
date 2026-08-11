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
    float i = 8.0f;
    float q = -4.0f;

    stage.apply(i, q);
    return i == 8.0f && q == -4.0f;
}

bool enabledStageTracksAndRemovesTheAverage() {
    DCRemoval stage(0.5f);
    float i = 8.0f;
    float q = -4.0f;

    stage.apply(i, q);
    if (i != 8.0f || q != -4.0f)
        return false;

    i = 8.0f;
    q = -4.0f;
    stage.apply(i, q);
    return i == 4.0f && q == -2.0f;
}

bool rtlFilteredPipelinesHonorDcRemoval() {
    using FileSelector = IQPipelineSelector<Rate_2_4_Mhz, Rate_8_0_Mhz,
        IQPipelineOptions::IQ_FIR_RTL_SDR_FILE>;

    auto disabled = FileSelector::make(std::vector<float>{ 1.0f }, false);
    auto enabled = FileSelector::make(std::vector<float>{ 1.0f }, true);

    float disabledMagnitude = 0.0f;
    float enabledMagnitude = 0.0f;
    for (int sample = 0; sample < 2000; ++sample) {
        disabledMagnitude = disabled.process(8.0f, -4.0f);
        enabledMagnitude = enabled.process(8.0f, -4.0f);
    }

    using BuiltInSelector = IQPipelineSelector<Rate_2_4_Mhz, Rate_8_0_Mhz,
        IQPipelineOptions::IQ_FIR_RTL_SDR>;
    const auto builtIn = BuiltInSelector::make({}, true);

    return enabledMagnitude < disabledMagnitude * 0.001f
        && builtIn.toString().find("[DCRemoval] alpha: 0.005")
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
