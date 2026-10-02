/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "ModeSFrame.hpp"

#include <array>
#include <atomic>
#include <cstddef>

template <size_t Capacity> class SpscFrameQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "SPSC queue capacity must be a power of two");

  public:
    bool tryPush(const ModeSFrame& frame) noexcept {
        const size_t write = m_write.load(std::memory_order_relaxed);
        if (write - m_read.load(std::memory_order_acquire) >= Capacity)
            return false;
        m_frames[write & (Capacity - 1)] = frame;
        m_write.store(write + 1, std::memory_order_release);
        return true;
    }

    bool tryPop(ModeSFrame& frame) noexcept {
        const size_t read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire))
            return false;
        frame = m_frames[read & (Capacity - 1)];
        m_read.store(read + 1, std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        return m_read.load(std::memory_order_acquire) == m_write.load(std::memory_order_acquire);
    }

  private:
    std::array<ModeSFrame, Capacity> m_frames{};
    alignas(64) std::atomic<size_t> m_write{0};
    alignas(64) std::atomic<size_t> m_read{0};
};
