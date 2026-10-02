/* SPDX-License-Identifier: GPL-3.0-or-later */
//
// Wiring test for the optional TCP outputs.
//
// The server's own test exercises TcpOutputServer through its public API. That
// leaves the join between the DSP side and the server untested: whether the
// message handlers actually publish what they write, whether the bytes on the
// socket are the same bytes stdout would have received, and whether
// --no-stdout really silences stdout instead of only claiming to. A handler
// constructed with a null server, or one that published a frame stdout never
// saw, would pass every other test in the suite.

#include "MessageHandler.hpp"
#include "TcpOutputServer.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <arpa/inet.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

// 8 streams is a real configuration (2.4 Msps in, 8 MHz out) and, unlike 12,
// its MLAT conversion is not the identity, so a handler that forgot to convert
// the sample index would show up here.
struct FakeSampler {
    static constexpr int NumStreams = 8;
};

struct FakeRssi {
    uint8_t shortRssi = 0;
    uint8_t longRssi = 0;
    uint8_t getRSSIShort() const { return shortRssi; }
    uint8_t getRSSILong() const { return longRssi; }
};

int connectLoopback(uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    return fd;
}

void waitForClients(const TcpOutputServer& server, size_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (server.clientCount() != expected && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(server.clientCount() == expected);
}

std::string readAtLeast(int fd, size_t wanted) {
    std::string got;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (got.size() < wanted && std::chrono::steady_clock::now() < deadline) {
        pollfd event{fd, POLLIN, 0};
        if (::poll(&event, 1, 100) <= 0)
            continue;
        std::array<char, 1024> chunk{};
        const ssize_t n = ::recv(fd, chunk.data(), chunk.size(), 0);
        if (n <= 0)
            break;
        got.append(chunk.data(), size_t(n));
    }
    return got;
}

// Redirects std::cout for as long as it is alive. AVRWriter holds a reference
// to std::cout itself, so swapping the stream buffer captures it.
class CoutCapture {
  public:
    CoutCapture() : m_saved(std::cout.rdbuf(m_buffer.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(m_saved); }
    std::string str() {
        std::cout.flush();
        return m_buffer.str();
    }

  private:
    std::ostringstream m_buffer;
    std::streambuf* m_saved;
};

TcpOutputConfig avrConfig() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    return config;
}

const uint64_t kShortSampleIndex = 0x0000000000112233ULL;
const uint64_t kLongSampleIndex = 0x0000000000445566ULL;
const uint64_t kShortPayload = 0x00112233445566ULL;
const Bits128 kLongPayload{0x0000AABBCCDDEEULL, 0x0102030405060708ULL};

// What the legacy stdout writer produces for the same input. The TCP output has
// to match it byte for byte, otherwise the two paths have drifted apart.
std::string expectedAvrText(bool withRssi, uint8_t shortRssi, uint8_t longRssi) {
    std::ostringstream out;
    AVRWriter writer(out);
    const uint64_t shortTs = MLAT::sampleIndexToMlatTime<FakeSampler::NumStreams>(kShortSampleIndex);
    const uint64_t longTs = MLAT::sampleIndexToMlatTime<FakeSampler::NumStreams>(kLongSampleIndex);
    if (withRssi) {
        writer.write_short_MLAT_RSSI(shortTs, kShortPayload, shortRssi);
        writer.write_long_MLAT_RSSI(longTs, kLongPayload, longRssi);
    } else {
        writer.write_short_MLAT(shortTs, kShortPayload);
        writer.write_long_MLAT(longTs, kLongPayload);
    }
    return out.str();
}

// The plain handler must publish to TCP exactly what it writes to stdout.
void plainHandlerPublishesToTcpAndStdout() {
    TcpOutputServer server(avrConfig());
    std::string error;
    assert(server.start(error));
    const int client = connectLoopback(server.avrPort());
    waitForClients(server, 1);

    const std::string expected = expectedAvrText(false, 0, 0);
    std::string stdoutText;
    {
        CoutCapture capture;
        StdOutMessageHandler<FakeSampler> handler(true, &server);
        handler.handleShort(kShortSampleIndex, kShortPayload);
        handler.handleLong(kLongSampleIndex, kLongPayload);
        stdoutText = capture.str();
    }

    assert(stdoutText == expected);
    assert(readAtLeast(client, expected.size()) == expected);
    assert(server.avrFramesEncoded() == 2);
    ::close(client);
    server.stop();
}

// --no-stdout must silence stdout without costing a single TCP frame. This is
// the combination the packaged service and the README examples rely on.
void noStdoutStillFeedsTcp() {
    TcpOutputServer server(avrConfig());
    std::string error;
    assert(server.start(error));
    const int client = connectLoopback(server.avrPort());
    waitForClients(server, 1);

    const std::string expected = expectedAvrText(false, 0, 0);
    std::string stdoutText;
    {
        CoutCapture capture;
        StdOutMessageHandler<FakeSampler> handler(false, &server);
        handler.handleShort(kShortSampleIndex, kShortPayload);
        handler.handleLong(kLongSampleIndex, kLongPayload);
        stdoutText = capture.str();
    }

    assert(stdoutText.empty());
    assert(readAtLeast(client, expected.size()) == expected);
    ::close(client);
    server.stop();
}

// The RSSI handler carries the signal level onto both outputs, and the AVR
// frames switch to the '<' prefix exactly as the stdout writer does.
void rssiHandlerCarriesSignalLevel() {
    TcpOutputConfig config = avrConfig();
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const int avrClient = connectLoopback(server.avrPort());
    waitForClients(server, 1);

    FakeRssi rssi{0x7F, 0xC3};
    const std::string expected = expectedAvrText(true, rssi.shortRssi, rssi.longRssi);
    assert(expected.front() == '<');

    std::string stdoutText;
    {
        CoutCapture capture;
        RssiStdOutMessageHandler<FakeSampler, FakeRssi> handler(rssi, true, &server);
        handler.handleShort(kShortSampleIndex, kShortPayload);
        handler.handleLong(kLongSampleIndex, kLongPayload);
        stdoutText = capture.str();
    }
    assert(stdoutText == expected);
    assert(readAtLeast(avrClient, expected.size()) == expected);

    assert(server.avrFramesEncoded() == 2);
    ::close(avrClient);
    server.stop();
}

// A handler built without a server is the stdout-only configuration: it must
// keep writing and must not reach for the null pointer.
void nullServerKeepsStdoutOnly() {
    const std::string expected = expectedAvrText(false, 0, 0);
    std::string stdoutText;
    {
        CoutCapture capture;
        StdOutMessageHandler<FakeSampler> handler;
        handler.handleShort(kShortSampleIndex, kShortPayload);
        handler.handleLong(kLongSampleIndex, kLongPayload);
        stdoutText = capture.str();
    }
    assert(stdoutText == expected);

    FakeRssi rssi{0x11, 0x22};
    const std::string expectedRssi = expectedAvrText(true, rssi.shortRssi, rssi.longRssi);
    {
        CoutCapture capture;
        RssiStdOutMessageHandler<FakeSampler, FakeRssi> handler(rssi);
        handler.handleShort(kShortSampleIndex, kShortPayload);
        handler.handleLong(kLongSampleIndex, kLongPayload);
        stdoutText = capture.str();
    }
    assert(stdoutText == expectedRssi);
}

// Publishing with no client attached is a documented drop, not a stall: the
// stdout side has to stay complete and the ring must not fill up.
void publishingWithoutClientsNeverBlocks() {
    TcpOutputServer server(avrConfig());
    std::string error;
    assert(server.start(error));

    std::string stdoutText;
    {
        CoutCapture capture;
        StdOutMessageHandler<FakeSampler> handler(true, &server);
        for (int i = 0; i < 20000; ++i)
            handler.handleShort(uint64_t(i), kShortPayload);
        stdoutText = capture.str();
    }
    // 20000 short AVR frames, each '@' + 12 + 14 hex + ';' + '\n'.
    assert(stdoutText.size() == 20000u * 29u);
    assert(server.droppedFrames() < 20000);
    server.stop();
}

} // namespace

int main() {
    plainHandlerPublishesToTcpAndStdout();
    noStdoutStillFeedsTcp();
    rssiHandlerCarriesSignalLevel();
    nullServerKeepsStdoutOnly();
    publishingWithoutClientsNeverBlocks();
}
