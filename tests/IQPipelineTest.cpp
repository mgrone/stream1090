/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <sstream>
#include <string>

#include "IQPipeline.hpp"

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

} // namespace

int main() {
    return disabledStageLeavesSamplesUnchanged()
            && enabledStageTracksAndRemovesTheAverage()
        ? 0
        : 1;
}
