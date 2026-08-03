/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <thread>

// A bounded single-producer/single-consumer ring. Each sequence is written by
// only one thread and lives on its own cache line. HistoryBlocks slots stay
// unavailable to the producer so the consumer may safely inspect old samples.
template<typename T, size_t BlockSize, size_t NumBlocks, size_t Delay = 0,
         size_t HistoryBlocks = 0>
class BlockRingAsync {
public:
    static_assert(NumBlocks > HistoryBlocks);
    static_assert(Delay <= BlockSize);

    static constexpr size_t RingSize = BlockSize * NumBlocks;
    static constexpr size_t TotalSize = RingSize + Delay;
    static constexpr size_t Capacity = NumBlocks - HistoryBlocks;

    explicit BlockRingAsync(const T& initValue)
        : m_data(
            new (std::align_val_t(CacheLineSize)) T[TotalSize],
            AlignedDeleter{}
        )
    {
        std::fill(m_data.get(), m_data.get() + TotalSize, initValue);
    }

    // Producer thread. Blocks until one slot is writable.
    T* waitWritePos() noexcept {
        const size_t writeSequence = publishedSequence();
        for (;;) {
            const size_t readSequence = m_readSequence.load(std::memory_order_acquire);
            if (writeSequence - readSequence < Capacity)
                break;
            std::this_thread::sleep_for(RetryDelay);
        }
        return m_data.get() + Delay + (writeSequence % NumBlocks) * BlockSize;
    }

    // Producer thread. Publishes the block returned by waitWritePos().
    void advanceWritePos() noexcept {
        const size_t writeSequence = publishedSequence();
        if constexpr (Delay > 0) {
            if ((writeSequence % NumBlocks) == NumBlocks - 1) {
                std::memcpy(m_data.get(),
                            m_data.get() + RingSize,
                            Delay * sizeof(T));
            }
        }

        m_publishedState.store((writeSequence + 1) << 1, std::memory_order_release);
    }

    // Consumer thread. Returns null after the producer has finished and all
    // published blocks have been consumed.
    const T* waitReadPos() noexcept {
        const size_t readSequence = m_readSequence.load(std::memory_order_relaxed);
        for (;;) {
            const size_t state = m_publishedState.load(std::memory_order_acquire);
            if (readSequence < (state >> 1))
                return m_data.get() + (readSequence % NumBlocks) * BlockSize;
            if (state & FinishedBit)
                return nullptr;
            std::this_thread::sleep_for(RetryDelay);
        }
    }

    // Consumer thread.
    const T* readPos() const noexcept {
        const size_t readSequence = m_readSequence.load(std::memory_order_relaxed);
        return m_data.get() + (readSequence % NumBlocks) * BlockSize;
    }

    // Consumer thread.
    void advanceReadPos() noexcept {
        const size_t readSequence = m_readSequence.load(std::memory_order_relaxed);
        m_readSequence.store(readSequence + 1, std::memory_order_release);
    }

    // Consumer thread.
    const T& lookBack(size_t k, size_t offsetInBlock = 0) const noexcept {
        const size_t readSequence = m_readSequence.load(std::memory_order_relaxed);
        size_t absoluteIndex = (readSequence % NumBlocks) * BlockSize + offsetInBlock;
        if (absoluteIndex >= RingSize)
            absoluteIndex -= RingSize;

        size_t lookBackIndex = absoluteIndex + RingSize - k;
        if (lookBackIndex >= RingSize)
            lookBackIndex -= RingSize;

        return m_data[lookBackIndex];
    }

    // Producer thread.
    void finished() noexcept {
        const size_t writeSequence = publishedSequence();
        m_publishedState.store((writeSequence << 1) | FinishedBit,
                               std::memory_order_release);
    }

private:
    static constexpr size_t CacheLineSize = 64;
    static constexpr size_t FinishedBit = 1;
    static constexpr auto RetryDelay = std::chrono::microseconds(100);

    struct AlignedDeleter {
        void operator()(T* p) const noexcept {
            ::operator delete[](p, std::align_val_t(CacheLineSize));
        }
    };

    size_t publishedSequence() const noexcept {
        return m_publishedState.load(std::memory_order_relaxed) >> 1;
    }

    std::unique_ptr<T[], AlignedDeleter> m_data;
    alignas(CacheLineSize) std::atomic<size_t> m_readSequence{0};
    alignas(CacheLineSize) std::atomic<size_t> m_publishedState{0};
};
