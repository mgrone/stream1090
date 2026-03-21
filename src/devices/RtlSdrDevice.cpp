#include "devices/RtlSdrDevice.hpp"
#include <iostream>

// ----------------------
// Callback
// ----------------------
static void rtlsdr_callback(unsigned char* buf, uint32_t len, void* ctx) {
    auto* self = static_cast<RtlSdrDevice*>(ctx);

    if (!self->isRunning())
        return;

    self->markCallback();
    self->writeDataToBuffer(buf, len);
}

// ----------------------
// Open
// ----------------------
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

    rtlsdr_set_sample_rate(m_dev, getSampleRate());
    rtlsdr_set_center_freq(m_dev, m_state.frequency);

    // Apply shadow state
    /*setAgc(m_state.agc);
    setGain(m_state.gain_db);
    setBiasTee(m_state.bias_tee);
    setPpm(m_state.ppm);
    setOffsetTuning(m_state.offset_tuning);
    setDirectSampling(m_state.direct_sampling);
    setTunerBandwidth(m_state.tuner_bandwidth);*/

    rtlsdr_reset_buffer(m_dev);
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
            std::cerr << "rtlsdr_read_async failed: " << rc << std::endl;

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
// Shadow-aware setters
// ----------------------
// ----------------------
// Shadow-aware setters with change logging
// ----------------------

bool RtlSdrDevice::setFrequency(uint32_t hz) {
    if (m_state.frequency == hz)
        return true;

    if (rtlsdr_set_center_freq(m_dev, hz) == 0) {
        std::cerr << "[RtlSdrDevice] frequency: "
                  << m_state.frequency << " -> " << hz << std::endl;
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
        std::cerr << "[RtlSdrDevice] gain: "
                  << m_state.gain_db << " dB -> " << gainDb << " dB"
                  << " (nearest step = " << nearest/10.0f << " dB)"
                  << std::endl;
        m_state.gain_db = gainDb;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setAgc(bool enabled) {
    if (m_state.agc == enabled)
        return true;

    if (rtlsdr_set_agc_mode(m_dev, enabled ? 1 : 0) == 0) {
        std::cerr << "[RtlSdrDevice] agc: "
                  << (m_state.agc ? "on" : "off")
                  << " -> " << (enabled ? "on" : "off") << std::endl;
        m_state.agc = enabled;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setBiasTee(bool enabled) {
    if (m_state.bias_tee == enabled)
        return true;

    if (rtlsdr_set_bias_tee(m_dev, enabled ? 1 : 0) == 0) {
        std::cerr << "[RtlSdrDevice] bias_tee: "
                  << (m_state.bias_tee ? "on" : "off")
                  << " -> " << (enabled ? "on" : "off") << std::endl;
        m_state.bias_tee = enabled;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setPpm(int ppm) {
    if (m_state.ppm == ppm)
        return true;

    if (rtlsdr_set_freq_correction(m_dev, ppm) == 0) {
        std::cerr << "[RtlSdrDevice] ppm: "
                  << m_state.ppm << " -> " << ppm << std::endl;
        m_state.ppm = ppm;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setOffsetTuning(bool enabled) {
    if (m_state.offset_tuning == enabled)
        return true;

    if (rtlsdr_set_offset_tuning(m_dev, enabled ? 1 : 0) == 0) {
        std::cerr << "[RtlSdrDevice] offset_tuning: "
                  << (m_state.offset_tuning ? "on" : "off")
                  << " -> " << (enabled ? "on" : "off") << std::endl;
        m_state.offset_tuning = enabled;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setDirectSampling(int mode) {
    if (m_state.direct_sampling == mode)
        return true;

    if (rtlsdr_set_direct_sampling(m_dev, mode) == 0) {
        std::cerr << "[RtlSdrDevice] direct_sampling: "
                  << m_state.direct_sampling << " -> " << mode << std::endl;
        m_state.direct_sampling = mode;
        return true;
    }
    return false;
}

bool RtlSdrDevice::setTunerBandwidth(uint32_t bw) {
    if (m_state.tuner_bandwidth == bw)
        return true;

    if (rtlsdr_set_tuner_bandwidth(m_dev, bw) == 0) {
        std::cerr << "[RtlSdrDevice] tuner_bandwidth: "
                  << m_state.tuner_bandwidth << " -> " << bw << std::endl;
        m_state.tuner_bandwidth = bw;
        return true;
    }
    return false;
}


// ----------------------
// applySetting()
// ----------------------
bool RtlSdrDevice::applySetting(const std::string& key, const std::string& value) {
    if (!m_dev)
        return false;

    if (key == "frequency")        return setFrequency(std::stoul(value));
    if (key == "gain")             return setGain(std::stof(value));
    if (key == "agc")              return setAgc(value == "1" || value == "true" || value == "on");
    if (key == "bias_tee")         return setBiasTee(value == "1" || value == "true" || value == "on");
    if (key == "ppm")              return setPpm(std::stoi(value));
    if (key == "offset_tuning")    return setOffsetTuning(value == "1" || value == "true" || value == "on");
    if (key == "direct_sampling")  return setDirectSampling(std::stoi(value));
    if (key == "tuner_bandwidth")  return setTunerBandwidth(std::stoul(value));

    return false;
}

// ----------------------
// Reload logic
// ----------------------
void RtlSdrDevice::applyReloadedConfig(const IniConfig::Section& cfg) {
    for (auto& [key, value] : cfg) {

        if (key == "serial")
            continue; // immutable

        applySetting(key, value);
    }
}
