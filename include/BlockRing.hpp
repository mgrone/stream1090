/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <memory>
#include <algorithm>

template<typename T, size_t BlockSize, size_t NumBlocks, size_t Delay = 0>
class BlockRing {
public:
    static constexpr size_t TotalSize = BlockSize * NumBlocks + Delay;

    BlockRing(const T& initValue)
        : m_data(
            new (std::align_val_t(CacheLineSize)) T[TotalSize],   // aligned allocation
            AlignedDeleter{}                           // matching deleter
        ),
          m_readPos(0),
          m_writePos(Delay),
          m_fullBlocks(0)
    {
        std::fill(m_data.get(), m_data.get() + TotalSize, initValue);
    }

    T* writePos() noexcept {
        return m_data.get() + m_writePos;
    }

    void advanceWritePos() noexcept {
        m_writePos += BlockSize;

        if (m_writePos + BlockSize > TotalSize) {
            if constexpr (Delay > 0) {
                std::memcpy(
                    m_data.get(),
                    m_data.get() + (TotalSize - Delay),
                    Delay * sizeof(T)
                );
            }
            m_writePos = Delay;
        }

        m_fullBlocks++;
    }

    const T* readPos() const noexcept {
        return m_data.get() + m_readPos;
    }

    void advanceReadPos() noexcept {
        m_readPos += BlockSize;

        if (m_readPos >= BlockSize * NumBlocks) {
            m_readPos = 0;
        }

        m_fullBlocks--;
    }

    bool isReadable() const noexcept {
        return m_fullBlocks > 0;
    }

    bool isWritable() const noexcept {
        return m_fullBlocks < NumBlocks;
    }

    const T& lookBack(size_t k, size_t offsetInBlock = 0) const noexcept {
        size_t absoluteIndex = m_readPos + offsetInBlock;
        if (absoluteIndex >= TotalSize)
            absoluteIndex -= TotalSize;

        size_t lookBackIndex = absoluteIndex + TotalSize - k;
        if (lookBackIndex >= TotalSize)
            lookBackIndex -= TotalSize;

        return m_data[lookBackIndex];
    }

private:

    static constexpr size_t CacheLineSize = 64;
    struct AlignedDeleter {
        void operator()(T* p) const noexcept {
            ::operator delete[](p, std::align_val_t(CacheLineSize));
        }
    };

    std::unique_ptr<T[], AlignedDeleter> m_data;

    size_t m_readPos;
    size_t m_writePos;
    size_t m_fullBlocks;
};
