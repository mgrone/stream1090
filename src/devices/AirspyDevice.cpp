/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#include "devices/AirspyDevice.hpp"


int airspy_callback(airspy_transfer_t* transfer) {
    auto* self = reinterpret_cast<AirspyDevice*>(transfer->ctx);

    if (!self->isRunning())
        return 0; // ignore late callbacks

    const uint16_t* samples = reinterpret_cast<const uint16_t*>(transfer->samples);
    size_t count = transfer->sample_count;
    self->writeDataToBuffer(samples, count);

    return 0;
}

bool AirspyDevice::open_with_serial(uint64_t serial) {
    int rc;

    if (serial  == 0) {
        rc = airspy_open(&m_dev);
    } else {
        rc = airspy_open_sn(&m_dev, serial);
    }

    if (rc != AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] airspy_open failed." << std::endl;
        return false;
    }

    // Configure device
    if (airspy_set_sample_type(m_dev, AIRSPY_SAMPLE_UINT16_REAL) != AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] airspy_set_sample_type failed." << std::endl;
        return false;
    }
        
    // Hardware rate = 2 × IQ rate
    if (airspy_set_samplerate(m_dev, getSampleRate() * 2) != AIRSPY_SUCCESS){
        std::cerr << "[AirspyDevice] airspy_set_samplerate failed." << getSampleRate() * 2 << std::endl;
        return false;
    }

    // Default frequency for ADS-B
    if (airspy_set_freq(m_dev, 1090000000) != AIRSPY_SUCCESS) {
        std::cerr << "[AirspyDevice] airspy_set_freq failed." << std::endl;
        return false;
    }
    // Default gains from airspy_rx
    if (!setVgaGain(5)) {
        std::cerr << "[AirspyDevice] set initial VGA gain failed." << std::endl;
        return false;
    }

    if (!setLnaGain(5)) {
        std::cerr << "[AirspyDevice] set initial LNA gain failed." << std::endl;
        return false;
    }
    
    if (!setMixerGain(5)) {
        std::cerr << "[AirspyDevice] set initial Mixer gain failed." << std::endl;
        return false;
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

bool AirspyDevice::setFrequency(uint32_t hz) {
    return airspy_set_freq(m_dev, hz) == AIRSPY_SUCCESS;
}

bool AirspyDevice::setLinearityGain(int value) {
    return airspy_set_linearity_gain(m_dev, value) == AIRSPY_SUCCESS;
}

bool AirspyDevice::setSensitivityGain(int value) {
    return airspy_set_sensitivity_gain(m_dev, value) == AIRSPY_SUCCESS;
}

bool AirspyDevice::setLnaGain(int value) {
    return airspy_set_lna_gain(m_dev, value) == AIRSPY_SUCCESS;
}

bool AirspyDevice::setMixerGain(int value) {
    return airspy_set_mixer_gain(m_dev, value) == AIRSPY_SUCCESS;
}

bool AirspyDevice::setVgaGain(int value) {
    return airspy_set_vga_gain(m_dev, value) == AIRSPY_SUCCESS;
}

bool AirspyDevice::setBiasTee(bool enabled) {
    return airspy_set_rf_bias(m_dev, enabled ? 1 : 0) == AIRSPY_SUCCESS;
}

bool AirspyDevice::applySetting(const std::string& key, const std::string& value) {
    if (!m_dev)
        return false;

    if (key == "frequency") {
        return airspy_set_freq(m_dev, std::stoul(value)) == AIRSPY_SUCCESS;
    }

    if (key == "linearity_gain") {
        return airspy_set_linearity_gain(m_dev, std::stoi(value)) == AIRSPY_SUCCESS;
    }

    if (key == "sensitivity_gain") {
        return airspy_set_sensitivity_gain(m_dev, std::stoi(value)) == AIRSPY_SUCCESS;
    }

    if (key == "lna_gain") {
        return airspy_set_lna_gain(m_dev, std::stoi(value)) == AIRSPY_SUCCESS;
    }

    if (key == "mixer_gain") {
        return airspy_set_mixer_gain(m_dev, std::stoi(value)) == AIRSPY_SUCCESS;
    }

    if (key == "vga_gain") {
        return airspy_set_vga_gain(m_dev, std::stoi(value)) == AIRSPY_SUCCESS;
    }

    if (key == "bias_tee") {
        bool enabled = (value == "1" || value == "true" || value == "on");
        return airspy_set_rf_bias(m_dev, enabled ? 1 : 0) == AIRSPY_SUCCESS;
    }

    // Unknown key
    return false;
}
