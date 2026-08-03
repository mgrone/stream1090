/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "BlockRingAsync.hpp"

#include <cstdlib>
#include <thread>

namespace {

void require(bool condition) {
    if (!condition)
        std::abort();
}

void testConcurrentWraparoundAndHistory() {
    constexpr size_t BlockSize = 8;
    constexpr size_t Delay = 2;
    constexpr size_t NumValues = 200000;
    constexpr size_t NumBlocks = NumValues / BlockSize;
    using Ring = BlockRingAsync<int, BlockSize, 4, Delay, 1>;

    Ring ring(-1);

    std::jthread producer([&] {
        for (size_t block = 0; block < NumBlocks; ++block) {
            int* output = ring.waitWritePos();
            for (size_t i = 0; i < BlockSize; ++i)
                output[i] = int(block * BlockSize + i);
            ring.advanceWritePos();
            if ((block & 31) == 0)
                std::this_thread::yield();
        }
        ring.finished();
    });

    size_t block = 0;
    while (const int* input = ring.waitReadPos()) {
        for (size_t i = 0; i < Delay; ++i) {
            const int expected = block == 0
                ? -1
                : int(block * BlockSize - Delay + i);
            require(input[i] == expected);
        }
        for (size_t i = 0; i < BlockSize; ++i)
            require(input[Delay + i] == int(block * BlockSize + i));

        if (block > 0)
            require(ring.lookBack(1, Delay) == int(block * BlockSize - 1));
        if (block > 0 && block % 4 == 0)
            require(ring.lookBack(1, 0) == int(block * BlockSize - Delay - 1));

        ring.advanceReadPos();
        if ((block & 15) == 0)
            std::this_thread::yield();
        ++block;
    }

    require(block == NumBlocks);
}

void testFinishWakesEmptyConsumer() {
    BlockRingAsync<int, 1, 2> ring(0);
    std::jthread producer([&] {
        std::this_thread::yield();
        ring.finished();
    });
    require(ring.waitReadPos() == nullptr);
}

} // namespace

int main() {
    testConcurrentWraparoundAndHistory();
    testFinishWakesEmptyConsumer();
}
