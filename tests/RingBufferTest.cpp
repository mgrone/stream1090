/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "RingBuffer.hpp"

#include <chrono>
#include <cstdlib>
#include <future>

namespace {

void require(bool condition) {
    if (!condition)
        std::abort();
}

void testWriterResumesAfterOneBlockIsConsumed() {
    using Ring = RingBufferAsync<int, 1, 2>;

    Ring ring;
    Ring::Writer writer(ring);
    Ring::Reader reader(ring);

    const int initial[] = {1, 2};
    writer.write(initial, 2);

    auto pendingWrite = std::async(std::launch::async, [&] {
        const int value = 3;
        writer.write(&value, 1);
    });

    require(!reader.eof());
    reader.process([](const int*) {});

    const auto status = pendingWrite.wait_for(std::chrono::seconds(1));
    if (status != std::future_status::ready) {
        reader.process([](const int*) {});
        pendingWrite.wait();
    }

    require(status == std::future_status::ready);
}

} // namespace

int main() {
    testWriterResumesAfterOneBlockIsConsumed();
}
