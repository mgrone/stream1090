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
#include <string>
#include <atomic>
#include <chrono>
#include <cstdint>

template<typename T>
class InputDeviceBase {
public:
    using RawType = T;

    InputDeviceBase(SampleRate sampleRate, IAsyncWriter<T>& bufferWriter)
        : m_sampleRate(sampleRate), m_bufferWriter(bufferWriter),
          // a drop event must cost at least 2 ms of stream: far above the
          // +-100 ppm crystal drift, far below one 256 KB USB transfer
          m_dropEventThreshold((uint64_t)sampleRate / 500)
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
    virtual void applyConfigPreOpen(const IniConfig::Section&) {
        // we do not do anything as default
    };

    // Called by the watchdog after SIGHUP
    virtual void applyConfigPostOpen(const IniConfig::Section&) {
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

    // Interleaved IQ: two raw elements per sample. Runs on the device
    // callback thread; the sample-drop accounting happens here because
    // only at arrival time is the wall clock comparable to the delivered
    // count without paying the quantization of the USB transfer queue
    // (one 256 KB transfer = 131072 IQ pairs = ~110 ms at 2.4 Msps).
    void writeDataToBuffer(const T* data, size_t n) {
        const auto now = std::chrono::steady_clock::now();
        const uint64_t pairs = m_iqPairsDelivered.fetch_add(n / 2, std::memory_order_relaxed)
                             + n / 2;

        const bool gap = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - m_lastAccounting).count() > 150;
        if (m_accT0.time_since_epoch().count() == 0) {
            // first data ever: start accounting from here
            m_firstCallback = now;
            m_accT0 = now;
            m_accPairs0 = pairs;
        } else if (gap) {
            // streaming was paused (device startup, config reload on
            // SIGHUP, unplug): re-baseline, the gap is operator- or
            // driver-induced and not congestion.
            m_accT0 = now;
            m_accPairs0 = pairs;
            m_histCount = 0;
            m_dropDeficit.store(0, std::memory_order_relaxed);
        } else {
            const double secs = std::chrono::duration<double>(now - m_accT0).count();
            const int64_t expected = (int64_t)((double)m_sampleRate * secs);
            const int64_t deficit = std::max<int64_t>(
                expected - (int64_t)(pairs - m_accPairs0), 0);
            m_dropDeficit.store(deficit, std::memory_order_relaxed);
            {
                uint64_t prev = m_maxDeficit.load(std::memory_order_relaxed);
                while (deficit > (int64_t)prev &&
                       !m_maxDeficit.compare_exchange_weak(prev, (uint64_t)deficit,
                                                           std::memory_order_relaxed)) {}
            }
            if (now - m_firstCallback < std::chrono::seconds(5)) {
                // startup ramp: transfers hiccup while the queue fills,
                // which is not the congestion the exit policy is for. Keep
                // the baseline rolling so ramp-up losses never count.
                m_accT0 = now;
                m_accPairs0 = pairs;
                m_histCount = 0;
            } else {
                // a lost sample never comes back, so a real drop leaves a
                // deficit that persists and grows. Two confounders to keep
                // out: a transient delivery spike (ring buffer full for a
                // few ms) recovers and collapses the deficit again, and the
                // crystal offset makes the cumulative deficit drift at up
                // to ~150 ppm (under 500 pairs per second, far below the
                // event threshold). Comparing the deficit against ~1 s ago
                // keeps both out: real loss arrives in whole USB transfers
                // (131072 pairs) and dwarfs the drift.
                m_deficitHist[m_histHead] = {now, deficit};
                m_histHead = (m_histHead + 1) % kDeficitHistory;
                if (m_histCount < kDeficitHistory)
                    m_histCount++;

                // find the oldest sample that is at least 900 ms old
                int64_t past = -1;
                for (size_t i = 0; i < m_histCount; i++) {
                    const auto& s = m_deficitHist[(m_histHead + i) % kDeficitHistory];
                    if (now - s.t >= std::chrono::milliseconds(900)) {
                        past = s.deficit;
                    } else {
                        break;
                    }
                }
                if (past >= 0 && deficit - past > (int64_t)m_dropEventThreshold &&
                    now - m_lastEventTime > std::chrono::milliseconds(500)) {
                    m_lastEventGrowth = deficit - past;
                    m_lastEventTime = now;
                    m_dropEvents.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        m_lastAccounting = now;

        m_bufferWriter.write(data, n);
    }

    void shutdownWriter() {
        m_bufferWriter.shutdown();
    }

    SampleRate getSampleRate() const {
        return m_sampleRate;
    }

    // ---------------------
    // Sample-drop monitor interface (polled by the watchdog).
    // The exit policy (more than N drops per minute) lives there; the
    // detection happens here on the callback thread.
    // ---------------------

    // monotonic count of drop events
    uint64_t dropEventCount() const {
        return m_dropEvents.load(std::memory_order_relaxed);
    }

    // pairs lost in the most recent drop event (over its ~1 s window)
    int64_t lastEventGrowth() const {
        return m_lastEventGrowth.load(std::memory_order_relaxed);
    }

    // current cumulative deficit in IQ pairs
    int64_t currentDropDeficit() const {
        return m_dropDeficit.load(std::memory_order_relaxed);
    }

    uint64_t maxDropDeficit() const {
        return m_maxDeficit.load(std::memory_order_relaxed);
    }

    virtual bool isRunning() const {
        return m_running.load(std::memory_order_relaxed);
    }

protected:
    SampleRate m_sampleRate;
    IAsyncWriter<T>& m_bufferWriter;
    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_iqPairsDelivered{0};

    // timestamp when the last time the callback was called.
    std::atomic<std::chrono::steady_clock::time_point> m_lastSignOfLife;

    // ---- sample-drop accounting, callback thread only ----
    // deficit history: 32 samples cover well over 1 s of callbacks (one
    // callback per 256 KB transfer = ~82-110 ms)
    static constexpr size_t kDeficitHistory = 32;
    struct DeficitSample {
        std::chrono::steady_clock::time_point t;
        int64_t deficit;
    };
    DeficitSample m_deficitHist[kDeficitHistory]{};
    size_t m_histHead = 0;
    size_t m_histCount = 0;
    std::chrono::steady_clock::time_point m_lastEventTime{};
    std::chrono::steady_clock::time_point m_lastAccounting{};
    std::chrono::steady_clock::time_point m_firstCallback{};
    std::chrono::steady_clock::time_point m_accT0{};
    uint64_t m_accPairs0 = 0;
    const uint64_t m_dropEventThreshold;
    std::atomic<int64_t> m_dropDeficit{0};
    std::atomic<int64_t> m_lastEventGrowth{0};
    std::atomic<uint64_t> m_dropEvents{0};
    std::atomic<uint64_t> m_maxDeficit{0};
};
