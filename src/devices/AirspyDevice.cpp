/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#include "devices/AirspyDevice.hpp"
#include <iostream>

// ----------------------
// Callback
// ----------------------
int airspy_callback(airspy_transfer_t* transfer) {
    auto* self = reinterpret_cast<AirspyDevice*>(transfer->ctx);

    if (!self->isRunning())
        return 0;

    self->markCallback();

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

    // Apply initial shadow state
    setFrequency(m_state.frequency);
    setLnaGain(m_state.lna_gain);
    setMixerGain(m_state.mixer_gain);
    setVgaGain(m_state.vga_gain);
    setBiasTee(m_state.bias_tee);

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

// ----------------------
// Reload logic
// ----------------------
void AirspyDevice::applyReloadedConfig(const IniConfig::Section& cfg) {
    for (auto& [key, value] : cfg) {

        if (key == "serial")
            continue; // immutable

        applySetting(key, value);
    }
}


