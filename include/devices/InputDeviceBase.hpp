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
#include <atomic>

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

    virtual bool open_with_serial(uint64_t = 0) { return open(); }
    virtual bool open() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void close() = 0;

    // Live‑mutable settings (gain, bias‑tee, etc.)
    virtual bool applySetting(const std::string&, const std::string&) {
        return false;
    }

    // Called by the watchdog after SIGHUP
    virtual void applyReloadedConfig(const IniConfig::Section&) {
        // we do not do anything as default        
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
