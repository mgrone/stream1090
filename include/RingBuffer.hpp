/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cstring>
#include <vector>
#include <iostream>


template<typename T, size_t BlockSize, size_t NumBlocks> class RingBufferAsyncReader;
template<typename T, size_t BlockSize, size_t NumBlocks> class RingBufferAsyncWriter;

// template class for a block based ring buffer. 
template<typename T, size_t BlockSize, size_t NumBlocks>
class RingBufferBase {
    public:
    
    // default constructor
    RingBufferBase() = default;
    
    // the number of blocks this buffer holds, each of size blockSize
    static constexpr size_t numBlocks() {
        return NumBlocks;
    }

    // number of elements in a single blocks
    static constexpr size_t blockSize() {
        return BlockSize;
    }

    // number of elements in a single blocks
    static constexpr size_t size() {
        return numBlocks() * blockSize();
    }

    // returns a pointer to the first element of block blockIndex
    // assumes that blockIndex < numBlocks
    inline T* begin(size_t blockIndex) {
        return m_data + (blockIndex * BlockSize);
    }

    // returns a pointer to the first element of block blockIndex
    // assumes that blockIndex < numBlocks
    inline const T* begin(size_t blockIndex) const {
        return m_data + (blockIndex * BlockSize);
    }

    // raw function to write into the ring buffer at element index startIndex < size().
    // Handles the wrap around
    inline void write(size_t startIndex, const T* src, size_t n) {
        // number of elements we can write before wrapping
        size_t first = std::min(n, size() - startIndex);
        
        // write first segment, this might be enough. Maybe not.
        std::memcpy(m_data + startIndex, src, first * sizeof(T));
    
        // check if there are elements left
        if (n > first) {
            // wrap around and copy them to the start of the buffer 
            std::memcpy(m_data, src + first, (n - first) * sizeof(T));
        }
    }

    protected:

    // all data data of the block ring buffer
    alignas(64) T m_data[BlockSize * NumBlocks];
};


template<typename T, size_t _BlockSize, size_t _NumBlocks = 8>
class RingBufferAsync : public RingBufferBase<T, _BlockSize, _NumBlocks> {
public:
    using Reader = RingBufferAsyncReader<T, _BlockSize, _NumBlocks>;
    using Writer = RingBufferAsyncWriter<T, _BlockSize, _NumBlocks>;
    using ValueType = T;
    static constexpr auto BlockSize = _BlockSize;
    static constexpr auto NumBlocks = _NumBlocks;

    RingBufferAsync() : m_numFullBlocks(0), m_shutdown(false) {}

    // signals that there are numNewBlocksWritten new full blocks of data available
    // returns the new number of full blocks
    size_t commitBlocks(size_t numNewBlocksWritten) {
        size_t res = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_numFullBlocks += numNewBlocksWritten;
            res = m_numFullBlocks;
        }
        m_condVar.notify_one();
        return res;
    }

    // signals that someone has read numBlocksRead many blocks which frees those for writing
    // returns the new number of full blocks
    size_t consumeBlocks(size_t numBlocksRead) {
        size_t res = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_numFullBlocks -= numBlocksRead;
            res = m_numFullBlocks;
        }
        m_condVar.notify_one();   // wake writer
        return res;
    }

    // function to signal that no more data will be written (due to an error or some other reason)
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_shutdown = true;
        }
        m_condVar.notify_all();
    }

    // This function blocks until at least one full block of data is ready (returns sth. > 0), 
    // or no blocks are remaining and it has been signaled via shutdown() 
    // that no more data will bee written later. Returns 0 in that case.
    size_t waitForNewBlocks() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condVar.wait(lock, [&]{
            return m_shutdown || m_numFullBlocks > 0;
        });

        // If shutdown and no blocks → EOF
        if (m_shutdown && (m_numFullBlocks == 0))
            return 0;

        // Otherwise: return how many blocks are currently available
        return m_numFullBlocks;
    }

    size_t waitForSpace(size_t desiredBlocks) {
        
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condVar.wait(lock, [&]{
            //std::cerr << "Waiting for desired Blocks " << desiredBlocks << " full "<< m_numFullBlocks << std::endl;
            return m_shutdown || (NumBlocks - m_numFullBlocks) > desiredBlocks;
        });
        return m_numFullBlocks;
    }

    // the number of full blocks containing unread data.
    size_t getNumFullBlocks() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_numFullBlocks;
    }

private:
    // number of unread full blocks
    size_t m_numFullBlocks;
    
    // signals that no more data will be written (error or end of file)        
    bool   m_shutdown;
    
    // mutex to protected the two above variables             
    mutable std::mutex m_mutex;

    // condition variable for signaling when the state changes
    std::condition_variable m_condVar;
};


template<typename T, size_t BlockSize, size_t NumBlocks>
class RingBufferAsyncReader {
    public:
    
    using RingBufferType = RingBufferAsync<T, BlockSize, NumBlocks>;

    RingBufferAsyncReader(RingBufferType& ring) 
        : m_ring(ring), m_numFullBlocks(0), m_readBlockIndex(0) {}

    bool eof() {
        // we first check if the local m_numFullBlocks > 0, that is, there is work to be done.
        // note that this value might not be up-to-date. However, we will first do this instead
        // of syncing with the shared value
        if (m_numFullBlocks > 0) 
            return false;

        // local m_numFullBlocks is 0, let's wait for new blocks to arrive or a shutdown signal
        m_numFullBlocks = m_ring.waitForNewBlocks();
        return m_numFullBlocks == 0;
    }

    template<typename ProcessingFunc>
    void process(ProcessingFunc processingFunc) {
        if (m_numFullBlocks > 0) {
            processingFunc(m_ring.begin(m_readBlockIndex));
            m_readBlockIndex = (m_readBlockIndex + 1) % NumBlocks;
            m_numFullBlocks = m_ring.consumeBlocks(1);
        }
    }
    
    private:

    RingBufferType& m_ring;
    size_t m_numFullBlocks;
    size_t m_readBlockIndex;
};

template<typename T>
class IAsyncWriter {
    public:
    virtual size_t write(const T* newData, size_t n) = 0;
    virtual void shutdown() = 0;
};

template<typename T, size_t BlockSize, size_t NumBlocks>
class RingBufferAsyncWriter : public IAsyncWriter<T> {
public:
    using RingBufferType = RingBufferAsync<T, BlockSize, NumBlocks>;

    RingBufferAsyncWriter(RingBufferType& ring) : m_ring(ring), m_writePos(0), m_numFullBlocks(0)  {}

    size_t write(const T* newData, size_t n) override {
        constexpr size_t bufferSize = BlockSize * NumBlocks;
        size_t remaining = n;

        while (remaining > 0) {

            // Compute used/free based on local state
            size_t usedElems =
                (m_numFullBlocks * BlockSize) +
                (m_writePos % BlockSize);

            size_t freeElems = bufferSize - usedElems;

            // If no space, block until consumer frees some
            if (freeElems == 0) {
                // here we need to be careful when waiting for new free blocks.
                size_t new_numFullBlocks = m_ring.waitForSpace( 1 );//std::min(NumBlocks - 1, remaining / BlockSize));
                if (new_numFullBlocks == NumBlocks) {
                    // there are no new free blocks, the buffer is shutting down
                    // we bail out too
                    remaining = 0;
                } else {
                    m_numFullBlocks = new_numFullBlocks;
                }
                continue;
            }

            // Write as much as possible
            size_t numToWrite = std::min(remaining, freeElems);

            const size_t writeBlockOffset = m_writePos % BlockSize;
            const size_t numNewFullBlocks =
                (writeBlockOffset + numToWrite) / BlockSize;
                
            // Perform the write
            m_ring.write(m_writePos, newData, numToWrite);
            m_writePos = (m_writePos + numToWrite) % bufferSize;

            // Commit full blocks and update local copy
            if (numNewFullBlocks > 0)
                m_numFullBlocks = m_ring.commitBlocks(numNewFullBlocks);

            // Advance input pointer
            newData   += numToWrite;
            remaining -= numToWrite;
        }

        return n;
    }

    size_t finishLastBlock(const T& paddingValue = T{}) {
        const auto numPartial = (m_writePos % BlockSize);
        
        if (numPartial == 0)
            return 0;
        
        const auto numPadding = BlockSize - numPartial;
        

        std::vector<T> padding(numPadding);
        std::fill(padding.begin(), padding.end(), paddingValue);
        return write(padding.data(), numPadding);
    }

    void shutdown() override {
        m_ring.shutdown();
    }


private:
    RingBufferType& m_ring;
    size_t m_writePos;       // element index in [0, NumBlocks*BlockSize)
    size_t m_numFullBlocks;  // local copy of full blocks
};


