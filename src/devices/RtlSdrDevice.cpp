/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#include "devices/RtlSdrDevice.hpp"
#include "devices/RtlSdrSerial.hpp"
#include "Logger.hpp"
#include <iostream>
#include <string>
#include <vector>

static void rtlsdr_callback(unsigned char* buf, uint32_t len, void* ctx) {
    auto* self = static_cast<RtlSdrDevice*>(ctx);

    if (!self->isRunning())
        return;

    self->markAsAlive();
    self->writeDataToBuffer(buf, len);
}

// ----------------------
// Open
// ----------------------
bool RtlSdrDevice::open_with_serial(const std::string& serial) {
    if (serial.empty()) {
        return open_with_serial(static_cast<uint64_t>(0));
    }

    const int deviceCount = static_cast<int>(rtlsdr_get_device_count());
    std::vector<std::string> available;
    available.reserve(deviceCount > 0 ? static_cast<std::size_t>(deviceCount) : 0);
    for (int i = 0; i < deviceCount; ++i) {
        char deviceSerial[256]{};
        if (rtlsdr_get_device_usb_strings(i, nullptr, nullptr, deviceSerial) == 0)
            available.emplace_back(deviceSerial);
        else
            available.emplace_back();
    }

    const int index = RtlSdrSerial::resolveIndex(serial, available);
    if (index < 0) {
        Log::error("RtlSdrDevice") << "No RTL-SDR device found with serial '"
                  << serial << "'";
        return false;
    }

    if (rtlsdr_open(&m_dev, index) != 0)
        return false;

    char buf[256];
    rtlsdr_get_device_usb_strings(index, nullptr, nullptr, buf);
    m_actualSerial = std::strtoull(buf, nullptr, 0);

    auto check = [&](const char* name, int rc) {
        if (rc != 0) {
            Log::error("RtlSdrDevice") << "ERROR: " << name
                    << " failed with code " << rc;
            return false;
        }
        return true;
    };

    // Set the frequency before the sample rate: R820T bandwidth setup retunes
    // the current frequency, and immediately after open() that value is zero.
    if (!check("rtlsdr_set_center_freq",
            rtlsdr_set_center_freq(m_dev, 1090000000)))
        return false;

    if (!check("rtlsdr_set_sample_rate",
            rtlsdr_set_sample_rate(m_dev, getSampleRate())))
        return false;

    if (!check("rtlsdr_reset_buffer",
            rtlsdr_reset_buffer(m_dev)))
        return false;
    return true;
}

bool RtlSdrDevice::open_with_serial(uint64_t serial) {
    int deviceCount = rtlsdr_get_device_count();
    if (deviceCount <= 0)
        return false;

    int index = 0;

    if (serial != 0) {
        bool found = false;
        for (int i = 0; i < deviceCount; i++) {
            char buf[256];
            rtlsdr_get_device_usb_strings(i, nullptr, nullptr, buf);
            uint64_t devSerial = std::strtoull(buf, nullptr, 0);

            if (devSerial == serial) {
                index = i;
                found = true;
                break;
            }
        }

        if (!found)
            return false;
    }

    if (rtlsdr_open(&m_dev, index) != 0)
        return false;

    char buf[256];
    rtlsdr_get_device_usb_strings(index, nullptr, nullptr, buf);
    m_actualSerial = std::strtoull(buf, nullptr, 0);
    
    
    auto check = [&](const char* name, int rc) {
        if (rc != 0) {
            Log::error("RtlSdrDevice") << "ERROR: " << name
                    << " failed with code " << rc;
            return false;
        }
        return true;
    };

    // Set the frequency before the sample rate: R820T bandwidth setup retunes
    // the current frequency, and immediately after open() that value is zero.
    if (!check("rtlsdr_set_center_freq",
            rtlsdr_set_center_freq(m_dev, 1090000000)))
        return false;

    if (!check("rtlsdr_set_sample_rate",
            rtlsdr_set_sample_rate(m_dev, getSampleRate())))
        return false;

    if (!check("rtlsdr_reset_buffer",
            rtlsdr_reset_buffer(m_dev)))
        return false;
    return true;
}

bool RtlSdrDevice::open() {
    if (!open_with_serial(m_serialString))
        return false;

    const char* tunerName = "unknown";
    switch (rtlsdr_get_tuner_type(m_dev)) {
        case RTLSDR_TUNER_R820T:  tunerName = "R820T/R820T2"; break;
        case RTLSDR_TUNER_R828D:  tunerName = "R828D"; break;
        case RTLSDR_TUNER_E4000:  tunerName = "E4000"; break;
        case RTLSDR_TUNER_FC0012: tunerName = "FC0012"; break;
        case RTLSDR_TUNER_FC0013: tunerName = "FC0013"; break;
        case RTLSDR_TUNER_FC2580: tunerName = "FC2580"; break;
        default: break;
    }
    std::cerr << "[RtlSdrDevice] Tuner: " << tunerName << std::endl;
    return true;
}

// ----------------------
// Start / Stop / Close
// ----------------------
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

        if (rc != 0)
            Log::error("RtlSdrDevice") << "rtlsdr_read_async failed: " << rc;

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


// ----------------------
// Shadow-aware setters with change logging
// ----------------------

bool RtlSdrDevice::setFrequency(uint32_t hz) {
    if (m_state.frequency == hz)
        return true;

    if (rtlsdr_set_center_freq(m_dev, hz) == 0) {
        Log::info("RtlSdrDevice") << "frequency: "
                  << m_state.frequency << " -> " << hz;
        m_state.frequency = hz;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setGain(float gainDb) {
    // m_state.gain_db starts at 0 dB, so a configured "gain = 0" must still
    // switch the tuner into manual mode the first time around.
    if (m_gainApplied && m_state.gain_db == gainDb)
        return true;

    rtlsdr_set_tuner_gain_mode(m_dev, 1);

    int gainTenths = static_cast<int>(gainDb * 10.0f);
    int nearest = nearestGain(gainTenths);

    if (rtlsdr_set_tuner_gain(m_dev, nearest) == 0) {
        Log::info("RtlSdrDevice") << "gain: "
                  << m_state.gain_db << " dB -> " << gainDb << " dB"
                  << " (nearest step = " << nearest/10.0f << " dB)";
        m_state.gain_db = gainDb;
        m_gainApplied = true;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setAgc(bool enabled) {
    if (m_state.agc == enabled)
        return true;

    if (rtlsdr_set_agc_mode(m_dev, enabled ? 1 : 0) == 0) {
        Log::info("RtlSdrDevice") << "agc: "
                  << (m_state.agc ? "on" : "off")
                  << " -> " << (enabled ? "on" : "off");
        if (enabled) {
            Log::warn("RtlSdrDevice")
                << "agc=true enables the RTL2832U digital AGC, not the tuner AGC. "
                   "It lifts the noise floor between pulses and usually lowers the "
                   "Mode-S message rate; prefer agc=false with an explicit gain.";
        }
        m_state.agc = enabled;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setBiasTee(bool enabled) {
    if (m_state.bias_tee == enabled)
        return true;

    if (rtlsdr_set_bias_tee(m_dev, enabled ? 1 : 0) == 0) {
        Log::info("RtlSdrDevice") << "bias_tee: "
                  << (m_state.bias_tee ? "on" : "off")
                  << " -> " << (enabled ? "on" : "off");
        m_state.bias_tee = enabled;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setPpm(int ppm) {
    if (m_state.ppm == ppm)
        return true;

    if (rtlsdr_set_freq_correction(m_dev, ppm) == 0) {
        Log::info("RtlSdrDevice") << "ppm: "
                  << m_state.ppm << " -> " << ppm;
        m_state.ppm = ppm;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setOffsetTuning(bool enabled) {
    if (m_state.offset_tuning == enabled)
        return true;

    if (rtlsdr_set_offset_tuning(m_dev, enabled ? 1 : 0) == 0) {
        Log::info("RtlSdrDevice") << "offset_tuning: "
                  << (m_state.offset_tuning ? "on" : "off")
                  << " -> " << (enabled ? "on" : "off");
        m_state.offset_tuning = enabled;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setTunerBandwidth(uint32_t bw) {
    if (m_state.tuner_bandwidth == bw)
        return true;

    if (rtlsdr_set_tuner_bandwidth(m_dev, bw) == 0) {
        Log::info("RtlSdrDevice") << "tuner_bandwidth: "
                  << m_state.tuner_bandwidth << " -> " << bw;
        m_state.tuner_bandwidth = bw;
        return true;
    }
    return false;
}

#ifdef STREAM1090_HAVE_RTLSDR_BLOG
bool RtlSdrDevice::setLnaGain(int gain) {
    if (!m_dev)
        return false;

    // Shadow awareness
    if (m_state.lna_gain == gain)
        return true;

    if (rtlsdr_r82xx_set_lna_gain(m_dev, gain) != 0)
        return false;

    Log::info("RtlSdrDevice") << "LNA gain: "
              << m_state.lna_gain << " -> " << gain;

    m_state.lna_gain = gain;
    return true;
}

bool RtlSdrDevice::setMixerGain(int gain) {
    if (!m_dev)
        return false;

    // Shadow awareness
    if (m_state.mixer_gain == gain)
        return true;

    if (rtlsdr_r82xx_set_mixer_gain(m_dev, gain) != 0)
        return false;

    Log::info("RtlSdrDevice") << "Mixer gain: "
              << m_state.mixer_gain << " -> " << gain;

    m_state.mixer_gain = gain;
    return true;
}

bool RtlSdrDevice::setVgaGain(int gain) {
    if (!m_dev)
        return false;

    // Shadow awareness
    if (m_state.vga_gain == gain)
        return true;

    if (rtlsdr_r82xx_set_vga_gain(m_dev, gain) != 0)
        return false;

    Log::info("RtlSdrDevice") << "VGA gain: "
              << m_state.vga_gain << " -> " << gain;

    m_state.vga_gain = gain;
    return true;
}

#else
bool RtlSdrDevice::setLnaGain(int) { return false; }
bool RtlSdrDevice::setMixerGain(int) { return false; }
bool RtlSdrDevice::setVgaGain(int) { return false; }
#endif





// ----------------------
// applySetting()
// ----------------------
bool RtlSdrDevice::applySetting(const std::string& key, const std::string& value) {
    if (!m_dev)
        return false;

    // Core controls
    if (key == "frequency")        return setFrequency(std::stoul(value));
    if (key == "gain")             return setGain(std::stof(value));
    if (key == "agc")              return setAgc(value == "1" || value == "true" || value == "on");
    if (key == "bias_tee")         return setBiasTee(value == "1" || value == "true" || value == "on");
    if (key == "ppm")              return setPpm(std::stoi(value));
    if (key == "offset_tuning")    return setOffsetTuning(value == "1" || value == "true" || value == "on");
    if (key == "tuner_bandwidth")  return setTunerBandwidth(std::stoul(value));

    // Advanced per‑stage gain controls (R820T manual mode)
    if (key == "lna_gain")         return setLnaGain(std::stoi(value));
    if (key == "mixer_gain")       return setMixerGain(std::stoi(value));
    if (key == "vga_gain")         return setVgaGain(std::stoi(value));

    return false;
}


void RtlSdrDevice::applyConfigPreOpen(const IniConfig::Section& cfg) {
    for (auto& [key, value] : cfg) {

        if (key == "serial")
            m_serialString = value;
    }
}

// ----------------------
// Reload logic
// ----------------------
void RtlSdrDevice::applyConfigPostOpen(const IniConfig::Section& cfg) {
    if (!m_initialConfigApplied) {
        m_initialConfigApplied = true;

        if (!cfg.count("tuner_bandwidth")
                && rtlsdr_get_tuner_type(m_dev) == RTLSDR_TUNER_R820T) {
            Log::warn("RtlSdrDevice")
                << "No tuner_bandwidth configured for this R820T/R820T2 tuner; "
                   "automatic IF filter selection depends on the sample rate and "
                   "librtlsdr implementation. Set it explicitly (for example, "
                   "3000000 at 2.4 or 2.56 Msps) to make the tuner state reproducible.";
        }

        if (!cfg.count("gain")) {
            // Without an explicit gain the tuner stays in whatever mode
            // librtlsdr left after open (observed: a low fixed gain on an
            // R828D, ~5x fewer messages). Ask for hardware automatic gain so
            // the default behaves like other Mode-S receivers' "gain auto".
            if (rtlsdr_set_tuner_gain_mode(m_dev, 0) == 0) {
                Log::info("RtlSdrDevice")
                    << "gain: not configured, tuner set to automatic gain "
                       "(set gain=<dB> in the ini for a fixed value)";
            } else {
                Log::warn("RtlSdrDevice")
                    << "gain: not configured and switching the tuner to automatic "
                       "gain failed; the tuner is in an undefined gain state";
            }
        }
    }

    for (auto& [key, value] : cfg) {

        if (key == "serial")
            continue; // immutable

        applySetting(key, value);
    }

    // Report the bandwidth setting once. librtlsdr has no read-back API for
    // the effective bandwidth it derives when tuner_bandwidth is omitted.
    if (!m_stateReported) {
        m_stateReported = true;
        std::cerr << "[RtlSdrDevice] Tuner bandwidth setting: "
                  << (m_state.tuner_bandwidth
                          ? std::to_string(m_state.tuner_bandwidth) + " Hz (explicit)"
                          : std::string("auto (derived by librtlsdr from sample rate)"))
                  << std::endl;
    }
}
