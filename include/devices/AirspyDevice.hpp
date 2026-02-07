/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include <airspy.h>
#include "devices/InputDeviceBase.hpp"


class AirspyDevice : public InputDeviceBase<uint16_t> {
public:
    
    AirspyDevice(SampleRate sampleRate, IAsyncWriter<uint16_t>& bufferWriter) : InputDeviceBase<uint16_t>(sampleRate, bufferWriter) { }

    bool open_with_serial(uint64_t = 0) override;
    bool open() override { return open_with_serial(); };
    bool start() override;
    void stop() override;
    void close() override;
 
    bool setFrequency(uint32_t hz);
    bool setLinearityGain(int value);
    bool setSensitivityGain(int value);
    bool setLnaGain(int value);
    bool setMixerGain(int value);
    bool setVgaGain(int value);
    bool setAgc(bool enabled);
    bool setBiasTee(bool enabled);

    bool applySetting(const std::string& key, const std::string& value) override;

private:
    uint64_t m_desiredSerial = 0;
    airspy_device* m_dev = nullptr;
};
