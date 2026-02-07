/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include "Sampler.hpp"
#include "RingBuffer.hpp"
#include <atomic>

template<typename T>
class InputDeviceBase {
public: 
    using RawType = T;

    InputDeviceBase(SampleRate sampleRate, IAsyncWriter<T>& bufferWriter) : m_sampleRate(sampleRate), m_bufferWriter(bufferWriter) {}

    virtual bool open_with_serial(uint64_t = 0) { return open(); };
    virtual bool open() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void close() = 0;

    virtual bool applySetting(const std::string&, const std::string&) {
        return false;
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
};