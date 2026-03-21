/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include <rtl-sdr.h>
#include <thread>
#include "devices/InputDeviceBase.hpp"
#include "IniConfig.hpp"

class RtlSdrDevice : public InputDeviceBase<uint8_t> {
public:
    RtlSdrDevice(SampleRate sampleRate, IAsyncWriter<uint8_t>& bufferWriter)
        : InputDeviceBase<uint8_t>(sampleRate, bufferWriter) {}

    bool open_with_serial(uint64_t serial = 0) override;
    bool open() override { return open_with_serial(); }

    bool start() override;
    void stop() override;
    void close() override;

    // Runtime setters (shadow-aware)
    bool setFrequency(uint32_t hz);
    bool setGain(float gainDb);
    bool setAgc(bool enabled);
    bool setBiasTee(bool enabled);
    bool setPpm(int ppm);
    bool setOffsetTuning(bool enabled);
    bool setDirectSampling(int mode);
    bool setTunerBandwidth(uint32_t bw);

    bool applySetting(const std::string& key, const std::string& value) override;

    // Reload hook
    void applyReloadedConfig(const IniConfig::Section& cfg) override;

private:
    int nearestGain(int requested);

    struct ShadowState {
        uint32_t frequency = 1090000000;
        float gain_db = 0.0f;
        bool agc = false;
        bool bias_tee = false;
        int ppm = 0;
        bool offset_tuning = false;
        int direct_sampling = 0;
        uint32_t tuner_bandwidth = 0;
    };

    ShadowState m_state;

    rtlsdr_dev_t* m_dev = nullptr;
    std::thread   m_thread;
    uint64_t m_actualSerial = 0;
};

