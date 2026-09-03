/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include "Sampler.hpp"
#include "RingBuffer.hpp"
#include "IniConfig.hpp"
#include "Logger.hpp"
#include <string>
#include <atomic>
#include <cstdint>
#include <exception>

namespace DeviceSettings {

inline bool parseInt(const std::string& value, int& out) noexcept {
    try {
        std::size_t consumed = 0;
        out = std::stoi(value, &consumed);
        return consumed == value.size();
    } catch (...) {
        return false;
    }
}

inline bool parseUnsigned(const std::string& value, uint32_t& out) noexcept {
    if (value.empty() || value.front() == '-')
        return false;
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoul(value, &consumed);
        if (consumed != value.size() || parsed > UINT32_MAX)
            return false;
        out = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

inline bool parseFloat(const std::string& value, float& out) noexcept {
    bool hasDigit = false;
    for (const char ch : value) {
        if (ch >= '0' && ch <= '9') {
            hasDigit = true;
            continue;
        }
        if (ch != '+' && ch != '-' && ch != '.' && ch != 'e' && ch != 'E')
            return false;
    }
    if (!hasDigit)
        return false;
    try {
        std::size_t consumed = 0;
        out = std::stof(value, &consumed);
        return consumed == value.size();
    } catch (...) {
        return false;
    }
}

} // namespace DeviceSettings

template<typename T>
class InputDeviceBase {
public:
    using RawType = T;

    InputDeviceBase(SampleRate sampleRate, IAsyncWriter<T>& bufferWriter)
        : m_sampleRate(sampleRate), m_bufferWriter(bufferWriter)
    {
        m_lastSignOfLife.store(std::chrono::steady_clock::now(),
                             std::memory_order_relaxed);
    }

    virtual ~InputDeviceBase() = default;
    
    virtual bool open() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void close() = 0;

    // Called before open() is called to parse things like serial, packing etc.
    virtual bool applyConfigPreOpen(const IniConfig::Section&) {
        // we do not do anything as default        
        return true;
    };

    virtual bool applySetting(const std::string& key, const std::string& value) = 0;

    bool applySettingSafely(const std::string& key, const std::string& value) noexcept {
        try {
            return applySetting(key, value);
        } catch (const std::exception& error) {
            Log::error("InputDevice")
                << "invalid value for setting '" << key << "': '" << value
                << "' (" << error.what() << ")";
        } catch (...) {
            Log::error("InputDevice")
                << "invalid value for setting '" << key << "': '" << value
                << "'";
        }
        return false;
    }

    // Called by the watchdog after SIGHUP
    virtual bool validateConfigPostOpen(const IniConfig::Section&) {
        return true;
    }

    virtual bool applyConfigPostOpen(const IniConfig::Section&) {
        // we do not do anything as default        
        return true;
    };

    // Called by device callback threads
    void markAsAlive() {
        m_lastSignOfLife.store(std::chrono::steady_clock::now(),
                             std::memory_order_relaxed);
    }
    
    // Used by watchdog to detect cable pulls
    std::chrono::milliseconds lastSignOfLife() const {
        auto now  = std::chrono::steady_clock::now();
        auto last = m_lastSignOfLife.load(std::memory_order_relaxed);
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - last);
    }

    void writeDataToBuffer(const T* data, size_t n) {
        m_bufferWriter.write(data, n);
    }

    void shutdownWriter() {
        m_bufferWriter.shutdown();
    }

    SampleRate getSampleRate() const {
        return m_sampleRate;
    }

    virtual bool isRunning() const {
        return m_running.load(std::memory_order_relaxed);
    }

protected:
    SampleRate m_sampleRate;
    IAsyncWriter<T>& m_bufferWriter;
    std::atomic<bool> m_running{false};

    // timestamp when the last time the callback was called.
    std::atomic<std::chrono::steady_clock::time_point> m_lastSignOfLife;
};
