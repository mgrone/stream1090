/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#pragma once

#include <airspy.h>
#include "devices/InputDeviceBase.hpp"
#include "IniConfig.hpp"

class AirspyDevice : public InputDeviceBase<uint16_t> {
public:
    AirspyDevice(SampleRate sampleRate, IAsyncWriter<uint16_t>& bufferWriter)
        : InputDeviceBase<uint16_t>(sampleRate, bufferWriter) {}

    bool open_with_serial(uint64_t serial = 0) override;
    bool open() override { return open_with_serial(); }
    bool start() override;
    void stop() override;
    void close() override;

    // Live‑mutable controls
    bool setFrequency(uint32_t hz);
    bool setLinearityGain(int value);
    bool setSensitivityGain(int value);
    bool setLnaGain(int value);
    bool setMixerGain(int value);
    bool setVgaGain(int value);
    bool setBiasTee(bool enabled);

    bool applySetting(const std::string& key, const std::string& value) override;

    // Reload hook
    void applyReloadedConfig(const IniConfig::Section& cfg) override;

private:
    struct ShadowState {
        uint32_t frequency = 1090000000;
        int linearity_gain = 5;
        int sensitivity_gain = 5;
        int lna_gain = 5;
        int mixer_gain = 5;
        int vga_gain = 5;
        bool bias_tee = false;
    };

    ShadowState m_state;
    airspy_device* m_dev = nullptr;
};
