/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#include "devices/AirspyDevice.hpp"
#include "Logger.hpp"
#include <iostream>

namespace {

// libairspy applies these stage values for the combined gain presets and
// gives no way to read them back, so they are mirrored here to keep the
// shadow state honest. They are the table documented in configs/airspy.ini,
// which is also what airspy_rx uses.
struct StageGains {
    int lna;
    int mixer;
    int vga;
};

constexpr int clampPreset(int index) {
    return index < 0 ? 0 : (index > 21 ? 21 : index);
}

constexpr StageGains linearityPreset(int index) {
    constexpr StageGains table[22] = {
        { 0,  0,  4},
        { 0,  0,  5},
        { 0,  1,  6},
        { 0,  1,  7},
        { 0,  1,  8},
        { 0,  1,  9},
        { 0,  2, 10},
        { 1,  2, 10},
        { 3,  0, 10},
        { 5,  0, 10},
        { 6,  1, 10},
        { 8,  0, 10},
        { 9,  0, 10},
        { 8,  5, 10},
        { 9,  6, 10},
        { 9,  6, 11},
        {10,  7, 11},
        {12,  8, 11},
        {13,  9, 11},
        {14, 11, 11},
        {14, 12, 12},
        {14, 12, 13}
    };
    return table[clampPreset(index)];
}

constexpr StageGains sensitivityPreset(int index) {
    constexpr StageGains table[22] = {
        { 0,  0,  4},
        { 1,  0,  4},
        { 2,  0,  4},
        { 3,  0,  4},
        { 5,  1,  4},
        { 6,  2,  4},
        { 7,  2,  4},
        { 8,  3,  4},
        { 9,  4,  4},
        { 9,  4,  5},
        {12,  4,  5},
        {12,  7,  5},
        {13,  8,  5},
        {14,  9,  5},
        {14,  9,  6},
        {14, 10,  7},
        {14, 10,  8},
        {14, 11,  9},
        {14, 12, 10},
        {14, 12, 11},
        {14, 12, 12},
        {14, 12, 13}
    };
    return table[clampPreset(index)];
}

// Guard against a transcription slip in the tables above.
static_assert(linearityPreset(0).lna == 0 && linearityPreset(0).vga == 4);
static_assert(linearityPreset(19).lna == 14 && linearityPreset(19).mixer == 11);
static_assert(linearityPreset(20).lna == 14 && linearityPreset(20).mixer == 12
              && linearityPreset(20).vga == 12);
static_assert(sensitivityPreset(10).lna == 12 && sensitivityPreset(10).vga == 5);
static_assert(sensitivityPreset(21).vga == 13);

} // namespace

int airspy_callback(airspy_transfer_t* transfer) {
    auto* self = reinterpret_cast<AirspyDevice*>(transfer->ctx);

    if (!self->isRunning())
        return 0;

    self->markAsAlive();

    const uint16_t* samples = reinterpret_cast<const uint16_t*>(transfer->samples);
    size_t count = transfer->sample_count;
    self->writeDataToBuffer(samples, count);

    return 0;
}

// ----------------------
// Open / Start / Stop
// ----------------------
bool AirspyDevice::open_with_serial(uint64_t serial) {
    int rc = (serial == 0)
        ? airspy_open(&m_dev)
        : airspy_open_sn(&m_dev, serial);

    if (rc != AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] airspy_open failed.\n";
        return false;
    }

    if (airspy_set_sample_type(m_dev, AIRSPY_SAMPLE_UINT16_REAL) != AIRSPY_SUCCESS)
        return false;

    if (airspy_set_samplerate(m_dev, getSampleRate() * 2) != AIRSPY_SUCCESS)
        return false;

    if (airspy_set_freq(m_dev, m_state.frequency) != AIRSPY_SUCCESS)
        return false;

    if (m_packingEnabled) {
        if (!tryEnablingPacking()) {
            Log::error("AirspyDevice", "Packing has been requested, but is not supported");    
            return false;
        }
        Log::info("AirspyDevice", "Packing is enabled");
    }

    return true;
}

bool AirspyDevice::start() {
    if (!m_dev)
        return false;

    m_running.store(true, std::memory_order_relaxed);
    return airspy_start_rx(m_dev, airspy_callback, this) == AIRSPY_SUCCESS;
}

void AirspyDevice::stop() {
    m_bufferWriter.shutdown();
    m_running.store(false, std::memory_order_relaxed);

    if (m_dev)
        airspy_stop_rx(m_dev);
}

void AirspyDevice::close() {
    stop();
    if (m_dev) {
        airspy_close(m_dev);
        m_dev = nullptr;
    }
}

// ----------------------
// Shadow‑aware setters
// ----------------------
bool AirspyDevice::setFrequency(uint32_t hz) {
    if (m_state.frequency == hz)
        return true;

    if (airspy_set_freq(m_dev, hz) == AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] frequency: "
                  << m_state.frequency << " -> " << hz << std::endl;
        m_state.frequency = hz;
        return true;
    }
    return false;
}

bool AirspyDevice::setLinearityGain(int value) {
    if (m_state.linearity_gain == value)
        return true;

    if (airspy_set_linearity_gain(m_dev, value) == AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] linearity_gain: "
                  << m_state.linearity_gain << " -> " << value << std::endl;
        m_state.linearity_gain = value;
        // The preset moved all three stages, so the shadow copy has to follow
        // or every later per-stage call compares against a stale value.
        adoptStageGains(linearityPreset(value));
        return true;
    }
    return false;
}

bool AirspyDevice::setSensitivityGain(int value) {
    if (m_state.sensitivity_gain == value)
        return true;

    if (airspy_set_sensitivity_gain(m_dev, value) == AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] sensitivity_gain: "
                  << m_state.sensitivity_gain << " -> " << value << std::endl;
        m_state.sensitivity_gain = value;
        // The preset moved all three stages, so the shadow copy has to follow
        // or every later per-stage call compares against a stale value.
        adoptStageGains(sensitivityPreset(value));
        return true;
    }
    return false;
}

bool AirspyDevice::setLnaGain(int value) {
    if (m_state.lna_gain == value)
        return true;

    if (airspy_set_lna_gain(m_dev, value) == AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] lna_gain: "
                  << m_state.lna_gain << " -> " << value << std::endl;
        m_state.lna_gain = value;
        return true;
    }
    return false;
}

bool AirspyDevice::setMixerGain(int value) {
    if (m_state.mixer_gain == value)
        return true;

    if (airspy_set_mixer_gain(m_dev, value) == AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] mixer_gain: "
                  << m_state.mixer_gain << " -> " << value << std::endl;
        m_state.mixer_gain = value;
        return true;
    }
    return false;
}

bool AirspyDevice::setVgaGain(int value) {
    if (m_state.vga_gain == value)
        return true;

    if (airspy_set_vga_gain(m_dev, value) == AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] vga_gain: "
                  << m_state.vga_gain << " -> " << value << std::endl;
        m_state.vga_gain = value;
        return true;
    }
    return false;
}

bool AirspyDevice::setBiasTee(bool enabled) {
    if (m_state.bias_tee == enabled)
        return true;

    if (airspy_set_rf_bias(m_dev, enabled ? 1 : 0) == AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] bias_tee: "
                  << (m_state.bias_tee ? "on" : "off")
                  << " -> " << (enabled ? "on" : "off") << std::endl;
        m_state.bias_tee = enabled;
        return true;
    }
    return false;
}


bool AirspyDevice::tryEnablingPacking() {
    // The device can send its 12-bit samples packed instead of padded
    // into 16-bit words, which is 25% less USB bandwidth. That matters
    // when the bus cannot sustain the unpacked rate: at 6 MSPS unpacked
    // needs 24 MB/s, and on a Raspberry Pi sharing the bus with the rest
    // of a feeder stack this dropped about 8% of the transfers. It is
    // lossless here, since the ADC is 12-bit and the upper bits are
    // padding.
    //
    // libairspy leaves this off by default and older firmware may not
    // support it. We will just report it since this is on by default.
    const int result = airspy_set_packing(m_dev, 1);
    if (result == AIRSPY_ERROR_BUSY) {
        // libairspy reallocates the USB transfers when packing changes,
        // so it refuses while streaming. A reload can therefore reach
        // this, and it is not a firmware limitation.
        std::cerr << "[AirspyDevice] packing can only be changed before "
                        "the device starts streaming; restart to apply"
                    << std::endl;
        return false;
    }
    if (result != AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] packing not supported by this "
                        "device or firmware; continuing unpacked"
                    << std::endl;
        return false;
    }

    return true;
}

// ----------------------
// applySetting()
// ----------------------
bool AirspyDevice::applySetting(const std::string& key, const std::string& value) {
    if (!m_dev)
        return false;

    if (key == "frequency")        return setFrequency(std::stoul(value));
    if (key == "linearity_gain")   return setLinearityGain(std::stoi(value));
    if (key == "sensitivity_gain") return setSensitivityGain(std::stoi(value));
    if (key == "lna_gain")         return setLnaGain(std::stoi(value));
    if (key == "mixer_gain")       return setMixerGain(std::stoi(value));
    if (key == "vga_gain")         return setVgaGain(std::stoi(value));
    if (key == "bias_tee") {
        bool enabled = (value == "1" || value == "true" || value == "on");
        return setBiasTee(enabled);
    }

    return false;
}


void AirspyDevice::applyConfigPreOpen(const IniConfig::Section& cfg) {
    for (auto& [key, value] : cfg) {

        if (key == "serial") {
            try {
                std::size_t pos = 0;
                uint64_t serial = std::stoull(value, &pos, 0);
                if (pos != value.size()) {
                    m_serial = 0;
                }
                m_serial = serial;
            } catch (...) {
                m_serial = 0;
            }   
        }

        if (key == "packing") {
            m_packingEnabled = (value == "1" || value == "true" || value == "on");
        }
    }
}


// ----------------------
// Reload logic
// ----------------------
void AirspyDevice::applyConfigPostOpen(const IniConfig::Section& cfg) {
    for (auto& [key, value] : cfg) {

        if (key == "serial")
            continue; // immutable
        if (key == "packing")
            continue; // immutable

        applySetting(key, value);
    }
}


