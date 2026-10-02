#include "ModeSFrame.hpp"
#include "ModeSFrameEncoder.hpp"
#include "SpscFrameQueue.hpp"
#include "AVRWriter.hpp"

#include <array>
#include <atomic>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
std::string asString(const EncodedModeSFrame& encoded) {
    return {reinterpret_cast<const char*>(encoded.bytes.data()), encoded.size};
}

void queueBasics() {
    static_assert(std::is_trivially_copyable_v<ModeSFrame>);
    SpscFrameQueue<4> queue;
    ModeSFrame out{};
    assert(!queue.tryPop(out));

    const auto a = ModeSFrame::shortFrame(1, 0x11223344556677ULL, 0x31, true);
    const auto b = ModeSFrame::longFrame(2, Bits128(0x0000AABBCCDDEEFFULL, 0x1122334455667788ULL), 0x32, true);
    const auto c = ModeSFrame::shortFrame(3, 0x01020304050607ULL, 0x33, true);
    const auto d = ModeSFrame::shortFrame(4, 0x11121314151617ULL, 0x34, true);

    assert(queue.tryPush(a));
    assert(queue.tryPush(b));
    assert(queue.tryPush(c));
    assert(queue.tryPush(d));
    assert(!queue.tryPush(a));
    assert(queue.tryPop(out) && out == a);
    assert(queue.tryPop(out) && out == b);
    assert(queue.tryPush(a)); // wrap around
    assert(queue.tryPop(out) && out == c);
    assert(queue.tryPop(out) && out == d);
    assert(queue.tryPop(out) && out == a);
    assert(!queue.tryPop(out));
}

void queueConcurrency() {
    constexpr uint64_t Count = 200000;
    SpscFrameQueue<1024> queue;
    std::thread producer([&] {
        for (uint64_t i = 0; i < Count; ++i) {
            const auto frame = ModeSFrame::shortFrame(i, i, uint8_t(i), true);
            while (!queue.tryPush(frame))
                std::this_thread::yield();
        }
    });

    for (uint64_t i = 0; i < Count; ++i) {
        ModeSFrame frame{};
        while (!queue.tryPop(frame))
            std::this_thread::yield();
        assert(frame.mlatTimestamp == i);
        assert(frame.low == i);
        assert(frame.signalLevel == uint8_t(i));
    }
    producer.join();
}

void avrEncoding() {
    auto shortNoRssi = ModeSFrame::shortFrame(0x123456789ABCULL, 0x8D40621D58C382ULL, 0x55, false);
    assert(asString(encodeAvr(shortNoRssi)) == "@123456789ABC8D40621D58C382;\n");

    auto shortRssi = ModeSFrame::shortFrame(0x123456789ABCULL, 0x8D40621D58C382ULL, 0x5A, true);
    assert(asString(encodeAvr(shortRssi)) == "<123456789ABC5A8D40621D58C382;\n");

    auto longNoRssi = ModeSFrame::longFrame(0x123456789ABCULL,
        Bits128(0x00008D40621D58C3ULL, 0x82D690C8AC2863A7ULL), 0, false);
    assert(asString(encodeAvr(longNoRssi)) == "@123456789ABC8D40621D58C382D690C8AC2863A7;\n");

    auto longRssi = longNoRssi;
    longRssi.signalLevel = 0x0F;
    longRssi.signalAvailable = true;
    assert(asString(encodeAvr(longRssi)) == "<123456789ABC0F8D40621D58C382D690C8AC2863A7;\n");

    shortNoRssi.mlatTimestamp = 0xFFFF123456789ABCULL;
    assert(asString(encodeAvr(shortNoRssi)).starts_with("@123456789ABC"));
}

void timestampAndPayloadMasks() {
    // Overlong timestamp and payload must be truncated to the protocol widths,
    // not leaked into the output.
    const auto shortFrame = ModeSFrame::shortFrame(0xFFFF123456789ABCULL, 0xFFFFFFFFFFFFFFFFULL, 0x00, false);
    assert(asString(encodeAvr(shortFrame)) == "@123456789ABCFFFFFFFFFFFFFF;\n");

    const auto longFrame = ModeSFrame::longFrame(0xFFFF123456789ABCULL,
        Bits128(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL), 0x00, false);
    assert(asString(encodeAvr(longFrame)) ==
           "@123456789ABCFFFFFFFFFFFFFFFFFFFFFFFFFFFF;\n");
}

void rssiPresenceAndZero() {
    const auto absent = ModeSFrame::shortFrame(0x000000000001ULL, 0x8D40621D58C382ULL, 0x7F, false);
    const auto zero = ModeSFrame::shortFrame(0x000000000001ULL, 0x8D40621D58C382ULL, 0x00, true);

    // RSSI absent and RSSI present-but-zero must stay distinguishable in AVR.
    assert(asString(encodeAvr(absent)) == "@0000000000018D40621D58C382;\n");
    assert(asString(encodeAvr(zero)) == "<000000000001008D40621D58C382;\n");
    assert(encodeAvr(absent).size == 29);
    assert(encodeAvr(zero).size == 31);
}

void avrMatchesUpstreamWriter() {
    // The upstream writer is the source of truth for the legacy stdout format.
    // Encode the same frames through both paths and compare the bytes.
    const std::vector<ModeSFrame> frames {
        ModeSFrame::shortFrame(0x123456789ABCULL, 0x8D40621D58C382ULL, 0x5A, true),
        ModeSFrame::shortFrame(0x000000000001ULL, 0x11223344556677ULL, 0x00, false),
        ModeSFrame::longFrame(0x123456789ABCULL,
            Bits128(0x00008D40621D58C3ULL, 0x82D690C8AC2863A7ULL), 0x0F, true),
        ModeSFrame::longFrame(0x000000000002ULL,
            Bits128(0x0000112233445566ULL, 0x778899AABBCCDDEEULL), 0x00, false),
    };

    std::ostringstream stream;
    AVRWriter writer(stream);
    for (const auto& frame : frames) {
        if (frame.length == ModeSFrameLength::Short) {
            if (frame.signalAvailable)
                writer.write_short_MLAT_RSSI(frame.mlatTimestamp, frame.low, frame.signalLevel);
            else
                writer.write_short_MLAT(frame.mlatTimestamp, frame.low);
        } else {
            const Bits128 payload(frame.high, frame.low);
            if (frame.signalAvailable)
                writer.write_long_MLAT_RSSI(frame.mlatTimestamp, payload, frame.signalLevel);
            else
                writer.write_long_MLAT(frame.mlatTimestamp, payload);
        }
    }

    std::string expected;
    for (const auto& frame : frames)
        expected.append(asString(encodeAvr(frame)));
    assert(stream.str() == expected);
}

void producerAtomicsAreLockFree() {
    // The SPSC producer runs on the DSP thread; the index atomics must be
    // lock-free on the platforms this backport targets. A compile-time check
    // also avoids a libatomic dependency in Clang's Linux test link.
    static_assert(std::atomic<size_t>::is_always_lock_free);
}
} // namespace

int main() {
    producerAtomicsAreLockFree();
    queueBasics();
    queueConcurrency();
    timestampAndPayloadMasks();
    avrEncoding();
    rssiPresenceAndZero();
    avrMatchesUpstreamWriter();
}
