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
#include <iterator>
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
    return open_with_serial(m_serialString);
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
    if (m_state.gain_db == gainDb)
        return true;

    rtlsdr_set_tuner_gain_mode(m_dev, 1);

    int gainTenths = static_cast<int>(gainDb * 10.0f);
    int nearest = nearestGain(gainTenths);

    if (rtlsdr_set_tuner_gain(m_dev, nearest) == 0) {
        Log::info("RtlSdrDevice") << "gain: "
                  << m_state.gain_db << " dB -> " << gainDb << " dB"
                  << " (nearest step = " << nearest/10.0f << " dB)";
        m_state.gain_db = gainDb;
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

bool RtlSdrDevice::validateSetting(const std::string& key,
                                  const std::string& value) const {
    int integer = 0;
    uint32_t unsignedValue = 0;
    float gain = 0.0f;
    if (key == "frequency" || key == "tuner_bandwidth")
        return DeviceSettings::parseUnsigned(value, unsignedValue);
    if (key == "gain")
        return DeviceSettings::parseFloat(value, gain);
    if (key == "ppm" || key == "lna_gain" || key == "mixer_gain"
            || key == "vga_gain")
        return DeviceSettings::parseInt(value, integer);
    return key == "agc" || key == "bias_tee" || key == "offset_tuning";
}


// ----------------------
// Configuration validation
// ----------------------
//
// The rule: a configuration the device would not apply in full is an error,
// not a warning. Whoever wrote it believes they are measuring one thing while
// they are measuring another, and the wrong measurement is indistinguishable
// from the right one.
//
// A note on ordering. Settings are applied by iterating over
// IniConfig::Section, which is a std::map, so the order is ALPHABETICAL and
// not the order of the file. "gain" therefore always comes before lna_gain,
// mixer_gain and vga_gain, and the per-stage values overwrite the combined one
// however the user wrote them. Measured on the bench: "gain = 49.6" on its own
// leaves the signal at 5.88 LSB RMS; with "vga_gain = 0" next to it the signal
// drops to 0.46, whichever of the two is written first.

namespace {

bool hasKey(const IniConfig::Section& cfg, const char* key) {
    return cfg.find(key) != cfg.end();
}

std::string joinQuoted(const std::vector<std::string>& keys) {
    std::string out;
    for (std::size_t i = 0; i < keys.size(); i++) {
        if (i)
            out += (i + 1 == keys.size()) ? " and " : ", ";
        out += "'" + keys[i] + "'";
    }
    return out;
}

const char* verb(const std::vector<std::string>& keys, const char* one,
                 const char* many) {
    return keys.size() == 1 ? one : many;
}

constexpr const char* kPerStageKeys[] = { "lna_gain", "mixer_gain", "vga_gain" };

} // namespace

bool RtlSdrDevice::validateCombinations(const IniConfig::Section& cfg) {
    std::vector<std::string> perStage;
    for (const char* key : kPerStageKeys) {
        if (hasKey(cfg, key))
            perStage.push_back(key);
    }

    if (perStage.empty())
        return true;

    const std::string names = joinQuoted(perStage);

#if !defined(STREAM1090_HAVE_RTLSDR_BLOG)
    Log::error("RtlSdrDevice")
        << "configuration rejected: " << names << " "
        << verb(perStage, "requires", "require")
        << " the vendored rtl-sdr-blog library, and this binary was built "
           "without it.";
    Log::error("RtlSdrDevice")
        << "  The R820T per-stage gains do not exist in the system "
           "librtlsdr, so nothing written here would ever reach the tuner.";
    Log::error("RtlSdrDevice")
        << "  Rebuild with -DENABLE_RTLSDR_BLOG=ON, or use 'gain = <dB>', "
           "which goes through the standard gain table.";
    return false;
#else
    if (hasKey(cfg, "gain")) {
        Log::error("RtlSdrDevice")
            << "configuration rejected: 'gain' cannot coexist with "
            << names << ".";
        Log::error("RtlSdrDevice")
            << "  They are two alternative ways of driving the same tuner. "
               "'gain' walks the R820T LNA and mixer step tables until it "
               "reaches the requested figure and pins the VGA to index 8; the "
               "per-stage keys write the three indices directly.";
        Log::error("RtlSdrDevice")
            << "  Used together the per-stage keys always win, silently, and "
               "whatever their order in the file, because settings are "
               "applied in alphabetical order.";
        Log::error("RtlSdrDevice")
            << "  Pick one of the two forms: 'gain = <dB>', or the three "
               "per-stage indices.";
        return false;
    }

    // The indices are 0-15 on all three stages: r82xx_set_lna_gain,
    // r82xx_set_mixer_gain and r82xx_set_vga_gain_new reject anything else
    // with -1, and without this check that reaches the user as a generic
    // "Device configuration failed".
    for (const char* key : kPerStageKeys) {
        auto it = cfg.find(key);
        if (it == cfg.end())
            continue;

        // The empty string has to be tested for on its own: std::stoi throws
        // on it, and the "consumed != size" test below would then compare 0
        // against 0 and let it through as if it were index 0.
        int value = 0;
        std::size_t consumed = 0;
        try {
            value = std::stoi(it->second, &consumed, 10);
        } catch (...) {
            consumed = 0;
        }

        if (it->second.empty() || consumed != it->second.size()
                || value < 0 || value > 15) {
            Log::error("RtlSdrDevice")
                << "configuration rejected: '" << key << " = " << it->second
                << "' is not a valid index.";
            Log::error("RtlSdrDevice")
                << "  The per-stage gains are integer indices from 0 to 15, "
                   "not decibels: the value in dB is the one 'gain' takes.";
            return false;
        }
    }

    // All three or none. Naming only one leaves the other two on hardware AGC:
    // the configuration is applied, but the overall gain is not the one the
    // written values suggest, and it is not reproducible because two stages
    // are chasing the signal on their own.
    if (perStage.size() != std::size(kPerStageKeys)) {
        std::vector<std::string> missing;
        for (const char* key : kPerStageKeys) {
            if (!hasKey(cfg, key))
                missing.push_back(key);
        }

        Log::error("RtlSdrDevice")
            << "configuration rejected: all three per-stage gains must be set, "
               "and " << joinQuoted(missing) << " "
            << verb(missing, "is", "are") << " missing here.";
        Log::error("RtlSdrDevice")
            << "  The stages that are not named stay on hardware AGC, so the "
               "overall gain is not the one the written values suggest and it "
               "drifts on its own with the signal.";
        Log::error("RtlSdrDevice")
            << "  Add the missing keys, or use 'gain = <dB>' to drive the "
               "three stages together.";
        return false;
    }

    return true;
#endif
}

bool RtlSdrDevice::validateAgainstTuner(const IniConfig::Section& cfg) {
    auto it = cfg.find("offset_tuning");
    if (it == cfg.end())
        return true;

    const std::string& value = it->second;
    if (!(value == "1" || value == "true" || value == "on"))
        return true;

    if (!m_dev)
        return true;

    const enum rtlsdr_tuner tuner = rtlsdr_get_tuner_type(m_dev);
    if (tuner != RTLSDR_TUNER_R820T && tuner != RTLSDR_TUNER_R828D)
        return true;

    // On R820T/R828D rtlsdr_set_offset_tuning() does not do offset tuning. It
    // returns -2 without tuning anything, so the key never had an effect on
    // any dongle carrying one of these two tuners, which is all the common
    // ADS-B ones.
    Log::error("RtlSdrDevice")
        << "configuration rejected: 'offset_tuning' is not supported by the "
           "R820T/R828D tuner fitted to this dongle.";
#if defined(STREAM1090_HAVE_RTLSDR_BLOG)
    // The vendored rtl-sdr-blog fork goes further than returning -2: it
    // deliberately reuses this call as a bias tee switch, for programs that
    // have no dedicated command for one. Whoever writes offset_tuning = true
    // against that library ends up with 4.5 V on the antenna cable and the
    // program dead on the generic error that follows. Rejecting the key before
    // it is applied is what keeps that from happening. The system librtlsdr
    // does not have the shortcut, so this warning would be wrong there.
    Log::error("RtlSdrDevice")
        << "  The vendored rtl-sdr-blog library reuses that call to switch the "
           "BIAS TEE on, that is to put 4.5 V on the antenna connector, and "
           "then reports an error anyway.";
#endif
    Log::error("RtlSdrDevice")
        << "  If you wanted the bias tee, ask for it explicitly with "
           "'bias_tee = true'. If you wanted offset tuning, this tuner does "
           "not have it: remove the key.";
    return false;
}

bool RtlSdrDevice::applyConfigPreOpen(const IniConfig::Section& cfg) {
    // Before touching the device: if the configuration is not applicable in
    // full, failing now beats failing halfway through with the tuner already
    // half reprogrammed.
    if (!validateCombinations(cfg))
        return false;

    for (auto& [key, value] : cfg) {

        if (key == "serial")
            m_serialString = value;
    }
    return true;
}

// ----------------------
// Reload logic
// ----------------------
bool RtlSdrDevice::validateConfigPostOpen(const IniConfig::Section& cfg) {
    // Repeated here because this is also the SIGHUP reload hook, which does
    // not go through applyConfigPreOpen again.
    if (!validateCombinations(cfg))
        return false;

    if (!validateAgainstTuner(cfg))
        return false;

    // Validate the complete reload before touching hardware. In particular,
    // an invalid value late in the map cannot leave earlier keys half-applied.
    for (const auto& [key, value] : cfg) {
        if (key == "serial")
            continue;
        if (!validateSetting(key, value)) {
            Log::error("RtlSdrDevice") << "invalid setting '" << key
                                       << " = " << value << "'";
            return false;
        }
    }
    return true;
}

bool RtlSdrDevice::applyConfigPostOpen(const IniConfig::Section& cfg) {
    if (!validateConfigPostOpen(cfg))
        return false;
    for (auto& [key, value] : cfg) {

        if (key == "serial")
            continue; // immutable

        if (!applySettingSafely(key, value))
            return false;
    }
    return true;
}
