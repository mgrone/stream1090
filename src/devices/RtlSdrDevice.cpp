/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#include "devices/RtlSdrDevice.hpp"
#include <iostream>


static void rtlsdr_callback(unsigned char* buf, uint32_t len, void* ctx) {
    auto* self = static_cast<RtlSdrDevice*>(ctx);

    if (!self->isRunning())
        return; // ignore late callbacks

    self->writeDataToBuffer(buf, len);
}

int RtlSdrDevice::nearestGain(int requested) {
    if (!m_dev)
        return 0;

    int gains[256];
    int count = rtlsdr_get_tuner_gains(m_dev, gains);

    if (count <= 0)
        return 0;

    int best = gains[0];
    int bestDiff = std::abs(requested - best);

    for (int i = 1; i < count; i++) {
        int diff = std::abs(requested - gains[i]);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = gains[i];
        }
    }

    return best;
}

bool RtlSdrDevice::open_with_serial(uint64_t serial) {
    int deviceCount = rtlsdr_get_device_count();
    if (deviceCount <= 0)
        return false;

    int index = 0;

    if (serial != 0) {
        for (int i = 0; i < deviceCount; i++) {
            char buf[256];
            rtlsdr_get_device_usb_strings(i, nullptr, nullptr, buf);
            uint64_t devSerial = std::strtoull(buf, nullptr, 0);

            if (devSerial == serial) {
                index = i;
                break;
            }
        }
    }

    if (rtlsdr_open(&m_dev, index) != 0)
        return false;

    char buf[256];
    rtlsdr_get_device_usb_strings(index, nullptr, nullptr, buf);
    m_actualSerial = std::strtoull(buf, nullptr, 0);

    if (rtlsdr_set_sample_rate(m_dev, getSampleRate()) != 0)
        return false;

    rtlsdr_set_center_freq(m_dev, 1090000000);
    rtlsdr_set_tuner_gain_mode(m_dev, 0);
    rtlsdr_reset_buffer(m_dev);

    return true;
}


bool RtlSdrDevice::start() {
    if (!m_dev)
        return false;

    m_running.store(true, std::memory_order_relaxed);

    m_thread = std::thread([this]() {
        int rc = rtlsdr_read_async(
            m_dev,
            rtlsdr_callback,
            this,
            0,
            0
        );

        if (rc != 0) {
            std::cerr << "rtlsdr_read_async failed: " << rc << std::endl;
        }

        m_running.store(false, std::memory_order_relaxed);
    });

    return true;
}


void RtlSdrDevice::stop() {
    m_bufferWriter.shutdown();
    if (!m_dev)
        return;

    m_running.store(false, std::memory_order_relaxed);
    
    rtlsdr_cancel_async(m_dev);
    if (m_thread.joinable())
        m_thread.join();
}

void RtlSdrDevice::close() {
    stop();

    if (m_dev) {
        rtlsdr_close(m_dev);
        m_dev = nullptr;
    }
}

bool RtlSdrDevice::setFrequency(uint32_t hz) {
    return rtlsdr_set_center_freq(m_dev, hz) == 0;
}

bool RtlSdrDevice::setGain(float gainDb) {
    if (!m_dev)
        return false;

    // enable manual gain mode
    rtlsdr_set_tuner_gain_mode(m_dev, 1);

    int gainTenthsDb = (int)(gainDb * 10.0f);
    int nearest = nearestGain(gainTenthsDb);

    return rtlsdr_set_tuner_gain(m_dev, nearest) == 0;
}


bool RtlSdrDevice::setAgc(bool enabled) {
    return rtlsdr_set_agc_mode(m_dev, enabled ? 1 : 0) == 0;
}

bool RtlSdrDevice::setBiasTee(bool enabled) {
    return rtlsdr_set_bias_tee(m_dev, enabled ? 1 : 0) == 0;
}

bool RtlSdrDevice::applySetting(const std::string& key, const std::string& value) {
    if (!m_dev)
        return false;

    // Frequency in Hz
    if (key == "frequency") {
        uint32_t hz = std::stoul(value, nullptr, 0);
        return rtlsdr_set_center_freq(m_dev, hz) == 0;
    }

    // Gain in dB (float)
    if (key == "gain") {
        float gainDb = std::stof(value);
        int gainTenths = static_cast<int>(gainDb * 10.0f);
        int nearest = nearestGain(gainTenths);
        return rtlsdr_set_tuner_gain(m_dev, nearest) == 0;
    }

    // Automatic gain control
    if (key == "agc") {
        bool enabled = (value == "1" || value == "true" || value == "on");
        return rtlsdr_set_agc_mode(m_dev, enabled ? 1 : 0) == 0;
    }

    // Bias tee (5V)
    if (key == "bias_tee") {
        bool enabled = (value == "1" || value == "true" || value == "on");
        return rtlsdr_set_bias_tee(m_dev, enabled ? 1 : 0) == 0;
    }

    // Frequency correction (PPM)
    if (key == "ppm") {
        int ppm = std::stoi(value);
        return rtlsdr_set_freq_correction(m_dev, ppm) == 0;
    }

    // Offset tuning
    if (key == "offset_tuning") {
        bool enabled = (value == "1" || value == "true" || value == "on");
        return rtlsdr_set_offset_tuning(m_dev, enabled ? 1 : 0) == 0;
    }

    // Direct sampling (0=off, 1=I, 2=Q)
    if (key == "direct_sampling") {
        int mode = std::stoi(value);
        return rtlsdr_set_direct_sampling(m_dev, mode) == 0;
    }

    // Tuner bandwidth in Hz
    if (key == "tuner_bandwidth") {
        uint32_t bw = std::stoul(value);
        return rtlsdr_set_tuner_bandwidth(m_dev, bw) == 0;
    }

    return false;
}

