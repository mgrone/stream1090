#include "ModeSFrameEncoder.hpp"
#include "TcpOutputServer.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <algorithm>
#include <arpa/inet.h>
#include <fcntl.h>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <vector>

namespace {
int connectLoopback(uint16_t port, int receiveBuffer = 0) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    if (receiveBuffer > 0) {
        // Set before connect so it caps the advertised window. Without it the
        // loopback buffers can absorb the whole test frame burst and the
        // server never sees a slow client, which is what made this test flaky.
        int value = receiveBuffer;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value));
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    return fd;
}

std::vector<uint8_t> receiveExact(int fd, size_t count) {
    std::vector<uint8_t> bytes(count);
    size_t used = 0;
    while (used != count) {
        pollfd event{fd, POLLIN, 0};
        assert(::poll(&event, 1, 2000) == 1);
        const ssize_t n = ::recv(fd, bytes.data() + used, count - used, 0);
        assert(n > 0);
        used += size_t(n);
    }
    return bytes;
}

void waitForClients(TcpOutputServer& server, size_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (server.clientCount() != expected && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(server.clientCount() == expected);
}

// Closes a client so the server sees a reset rather than a FIN. A FIN says
// only that the peer stopped writing, which a half-closed peer also does, so a
// test that wants an immediate disconnect with no traffic has to reset.
void resetClose(int fd) {
    linger reset{};
    reset.l_onoff = 1;
    reset.l_linger = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset));
    ::close(fd);
}

void waitForFailure(TcpOutputServer& server) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!server.failed() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(server.failed());
}

std::vector<uint8_t> encodedBytes(const ModeSFrame& frame) {
    const auto encoded = encodeAvr(frame);
    return std::vector<uint8_t>(encoded.bytes.begin(), encoded.bytes.begin() + encoded.size);
}

void serverFanout() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.avrPort = 0;   // ephemeral ports are available to the test API
    config.enableAvr = true;

    TcpOutputServer server(config);
    std::string error;
    if (!server.start(error)) {
        std::fprintf(stderr, "server start failed: %s\n", error.c_str());
        assert(false);
    }
    assert(server.avrPort() != 0);

    const int avr1 = connectLoopback(server.avrPort());
    const int avr2 = connectLoopback(server.avrPort());
    waitForClients(server, 2);

    const auto frame = ModeSFrame::shortFrame(0x010203040506ULL, 0x11223344556677ULL, 0x55, true);
    const auto avrExpected = encodeAvr(frame);
    assert(server.tryPublish(frame));

    assert(receiveExact(avr1, avrExpected.size) ==
           std::vector<uint8_t>(avrExpected.bytes.begin(), avrExpected.bytes.begin() + avrExpected.size));
    assert(receiveExact(avr2, avrExpected.size) ==
           std::vector<uint8_t>(avrExpected.bytes.begin(), avrExpected.bytes.begin() + avrExpected.size));

    ::close(avr1);
    ::close(avr2);
    server.stop();
    assert(!server.running());
}

void orderedDeliveryAndReconnect() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));

    int client = connectLoopback(server.avrPort());
    waitForClients(server, 1);
    const auto first = ModeSFrame::shortFrame(1, 0x01020304050607ULL, 1, false);
    const auto second = ModeSFrame::shortFrame(2, 0x11121314151617ULL, 2, false);
    const auto firstBytes = encodeAvr(first);
    const auto secondBytes = encodeAvr(second);
    assert(server.tryPublish(first));
    assert(server.tryPublish(second));
    auto received = receiveExact(client, firstBytes.size + secondBytes.size);
    std::vector<uint8_t> expected(firstBytes.bytes.begin(), firstBytes.bytes.begin() + firstBytes.size);
    expected.insert(expected.end(), secondBytes.bytes.begin(), secondBytes.bytes.begin() + secondBytes.size);
    assert(received == expected);

    // A reset, not a plain close: a FIN alone leaves a client that may still be
    // reading, and this step wants the socket gone before the reconnect.
    resetClose(client);
    waitForClients(server, 0);
    client = connectLoopback(server.avrPort());
    waitForClients(server, 1);
    assert(server.tryPublish(second));
    assert(receiveExact(client, secondBytes.size) ==
           std::vector<uint8_t>(secondBytes.bytes.begin(), secondBytes.bytes.begin() + secondBytes.size));
    ::close(client);
    server.stop();
}

void slowClientDoesNotBlockHealthyClient() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    // One network-thread drain appends the whole ring batch to every client
    // before any flush, and the ring holds at most 4096 frames (about 118 KB),
    // so the limit has to sit well above that or a busy drain disconnects the
    // healthy client too. The slow client, once its socket stops accepting,
    // keeps accumulating across drains and crosses the same limit.
    config.clientBufferLimit = 1024 * 1024;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));

    // The slow client gets a tiny receive window so the server's send() blocks
    // after a few KB instead of the kernel quietly buffering the whole burst.
    const int slow = connectLoopback(server.avrPort(), 4 * 1024);
    const int healthy = connectLoopback(server.avrPort());
    waitForClients(server, 2);

    // The burst has to clear the socket buffers on both sides plus a full
    // clientBufferLimit (1 MB) of pending bytes before the slow client is
    // dropped. The server caps its own send buffer at 64 KB, which is what
    // keeps this number the same everywhere: left to autotune, Linux would
    // grow it to 4 MB and swallow the whole burst without the userspace
    // backlog ever building. 80k frames is about 2.3 MB.
    constexpr size_t Count = 80000;
    // Progressive frames: the healthy reader verifies the full ordered
    // sequence, not just a byte total, so duplicates or reordering fail.
    const auto frameFor = [](size_t index) {
        return ModeSFrame::shortFrame(index, index, uint8_t(index), true);
    };
    std::atomic<size_t> received{0};
    std::thread reader([&] {
        std::string buffer;
        size_t next = 0;
        std::array<char, 16384> bytes{};
        while (next < Count) {
            pollfd event{healthy, POLLIN, 0};
            assert(::poll(&event, 1, 5000) == 1);
            const ssize_t n = ::recv(healthy, bytes.data(), bytes.size(), 0);
            assert(n > 0);
            buffer.append(bytes.data(), size_t(n));
            size_t newline;
            while ((newline = buffer.find('\n')) != std::string::npos) {
                const std::string line = buffer.substr(0, newline);
                buffer.erase(0, newline + 1);
                const uint64_t timestamp = std::strtoull(line.substr(1, 12).c_str(), nullptr, 16);
                assert(timestamp == next);
                ++next;
            }
            received.store(next, std::memory_order_relaxed);
        }
    });

    for (size_t i = 0; i < Count; ++i) {
        while (!server.tryPublish(frameFor(i)))
            std::this_thread::yield();
        // Pace the producer so one network-thread drain stays well under the
        // client buffer limit: the whole ring batch is appended to every
        // client before any of them is flushed, so a large batch would
        // disconnect the healthy client too.
        if ((i & 63) == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    reader.join();
    assert(received.load() == Count);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (server.slowClientDisconnects() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(server.slowClientDisconnects() == 1);
    assert(server.clientCount() == 1);
    // A slow client is not a failure of the server.
    assert(!server.failed());

    ::close(slow);
    ::close(healthy);
    server.stop();
}

void promptStopAndInvalidBind() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const uint16_t port = server.avrPort();

    auto conflictingConfig = config;
    conflictingConfig.avrPort = port;
    TcpOutputServer conflicting(conflictingConfig);
    assert(!conflicting.start(error));
    assert(!error.empty());

    // The shutdown drain budget is 250 ms; the margin here is for thread
    // teardown on a loaded machine, not for the budget itself.
    const auto before = std::chrono::steady_clock::now();
    server.stop();
    assert(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(1000));

    TcpOutputServer restarted(conflictingConfig);
    assert(restarted.start(error));
    assert(restarted.avrPort() == port);
    restarted.stop();

    config.bindAddress = "not-an-address.invalid";
    TcpOutputServer invalid(config);
    assert(!invalid.start(error));
    assert(!error.empty());
}

void fragmentedReads() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const int client = connectLoopback(server.avrPort());
    waitForClients(server, 1);

    const auto frame = ModeSFrame::shortFrame(0x010203040506ULL, 0x11223344556677ULL, 0x55, true);
    const auto expected = encodeAvr(frame);
    assert(server.tryPublish(frame));

    // Read a single byte at a time: the wire format must not depend on how the
    // kernel happens to split the stream.
    std::vector<uint8_t> got;
    for (size_t i = 0; i < expected.size; ++i) {
        pollfd event{client, POLLIN, 0};
        assert(::poll(&event, 1, 2000) == 1);
        uint8_t byte = 0;
        assert(::recv(client, &byte, 1, 0) == 1);
        got.push_back(byte);
    }
    assert(got == std::vector<uint8_t>(expected.bytes.begin(), expected.bytes.begin() + expected.size));
    ::close(client);
    server.stop();
}

void clientClosesImmediately() {
    // A peer that goes away before reading must not raise SIGPIPE or crash the
    // server, even when the next sends hit a reset socket.
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));

    for (int i = 0; i < 20; ++i) {
        const int fd = connectLoopback(server.avrPort());
        linger reset{};
        reset.l_onoff = 1;
        reset.l_linger = 0;
        ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset));
        ::close(fd);
    }
    const auto frame = ModeSFrame::shortFrame(1, 2, 3, false);
    for (int i = 0; i < 2000; ++i)
        server.tryPublish(frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(server.running());
    server.stop();
    assert(!server.running());
}

void controlBytesAreIgnored() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const int client = connectLoopback(server.avrPort());
    waitForClients(server, 1);

    const char junk[64] = {'x'};
    assert(::send(client, junk, sizeof(junk), 0) == ssize_t(sizeof(junk)));
    const auto frame = ModeSFrame::shortFrame(0x11, 0x22, 0x33, false);
    const auto expected = encodeAvr(frame);
    assert(server.tryPublish(frame));
    assert(receiveExact(client, expected.size) ==
           std::vector<uint8_t>(expected.bytes.begin(), expected.bytes.begin() + expected.size));
    ::close(client);
    server.stop();
}

void clientLimitIsEnforced() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    config.maxClients = 2;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const int a = connectLoopback(server.avrPort());
    const int b = connectLoopback(server.avrPort());
    waitForClients(server, 2);
    const int c = connectLoopback(server.avrPort());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (server.rejectedClients() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(server.rejectedClients() >= 1);
    assert(server.clientCount() == 2);
    // Refusing a client over the limit is not a failure of the server either.
    assert(!server.failed());
    ::close(a);
    ::close(b);
    ::close(c);
    server.stop();
}

void stopWithoutBacklogIsPrompt() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const auto before = std::chrono::steady_clock::now();
    server.stop();
    assert(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(250));
}

void stopAfterPublishDeliversBurst() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const int client = connectLoopback(server.avrPort());
    waitForClients(server, 1);

    constexpr size_t Count = 64;
    std::vector<uint8_t> expected;
    for (size_t i = 0; i < Count; ++i) {
        const auto frame = ModeSFrame::shortFrame(i, i, uint8_t(i), false);
        assert(server.tryPublish(frame));
        const auto encoded = encodeAvr(frame);
        expected.insert(expected.end(), encoded.bytes.begin(), encoded.bytes.begin() + encoded.size);
    }
    server.stop();

    // Read until the server closes: every accepted frame must arrive first.
    std::vector<uint8_t> got;
    std::array<uint8_t, 4096> chunk{};
    for (;;) {
        pollfd event{client, POLLIN, 0};
        const int ready = ::poll(&event, 1, 3000);
        if (ready <= 0)
            break;
        const ssize_t n = ::recv(client, chunk.data(), chunk.size(), 0);
        if (n <= 0)
            break;
        got.insert(got.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(got == expected);
    ::close(client);
}

void stopWithStalledReaderRespectsDeadline() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    config.clientBufferLimit = 1024 * 1024;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const int stalled = connectLoopback(server.avrPort(), 4 * 1024);
    waitForClients(server, 1);

    const auto frame = ModeSFrame::shortFrame(0x010203040506ULL, 0x11223344556677ULL, 0x55, true);
    for (size_t i = 0; i < 20000; ++i)
        server.tryPublish(frame);

    // A reader that never drains must not be able to hold stop() past the
    // budget. What it did not take is lost by design.
    const auto before = std::chrono::steady_clock::now();
    server.stop();
    assert(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(1000));
    assert(server.slowClientDisconnects() == 0);
    ::close(stalled);
}

void doubleStopAndReusePort() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const uint16_t port = server.avrPort();

    server.stop();
    server.stop(); // idempotent
    assert(!server.running());

    // A fresh instance must be able to take the port the previous one released.
    TcpOutputConfig again = config;
    again.avrPort = port;
    TcpOutputServer restarted(again);
    assert(restarted.start(error));
    assert(restarted.avrPort() == port);
    restarted.stop();
}

void queueOverflowCounter() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));

    // Test seam: stop the worker from draining so the bounded ring saturates
    // deterministically instead of relying on a timing race.
    server.setConsumptionSuspendedForTest(true);
    const auto frame = ModeSFrame::shortFrame(1, 2, 3, false);
    uint64_t rejected = 0;
    for (size_t i = 0; i < 200000; ++i) {
        if (!server.tryPublish(frame))
            ++rejected;
    }
    assert(rejected > 0);
    assert(server.droppedFrames() == rejected);
    // A full queue is a dropped frame, not a failed server.
    assert(!server.failed());

    server.setConsumptionSuspendedForTest(false);
    const auto before = std::chrono::steady_clock::now();
    server.stop();
    assert(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(1000));
}
// A bind address that resolves to several families lands on exactly one of
// them, so the reported endpoint has to be the socket's own name rather than
// an echo of the configured string: a consumer pointed at the other family
// gets a silent connection refused.
void endpointReportsTheBoundAddress() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));

    assert(server.avrEndpoint() == "127.0.0.1:" + std::to_string(server.avrPort()));

    // Every ephemeral port has to be resolved in the string too, never a 0.
    assert(server.avrEndpoint().find(":0") == std::string::npos);
    server.stop();

    // The case the endpoint exists for: a name with both an A and a AAAA
    // record binds one family, and which one is not fixed. Echoing the
    // configured string back would tell a consumer nothing, so the endpoint
    // has to be the numeric address of the socket itself.
    TcpOutputConfig named = config;
    named.bindAddress = "localhost";
    TcpOutputServer namedServer(named);
    std::string namedError;
    assert(namedServer.start(namedError));
    const std::string endpoint = namedServer.avrEndpoint();
    assert(endpoint.find("localhost") == std::string::npos);
    assert(endpoint == "127.0.0.1:" + std::to_string(namedServer.avrPort()) ||
           endpoint == "[::1]:" + std::to_string(namedServer.avrPort()));
    // And it must name the family that is really listening.
    const bool boundV6 = endpoint.front() == '[';
    const int probe = ::socket(boundV6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    assert(probe >= 0);
    if (boundV6) {
        sockaddr_in6 target{};
        target.sin6_family = AF_INET6;
        target.sin6_port = htons(namedServer.avrPort());
        target.sin6_addr = in6addr_loopback;
        assert(::connect(probe, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0);
    } else {
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(namedServer.avrPort());
        target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(::connect(probe, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0);
    }
    waitForClients(namedServer, 1);
    ::close(probe);
    namedServer.stop();

    // A numeric IPv6 literal must come back bracketed so the ':' of the port
    // stays unambiguous. Loopback v6 is not available everywhere, so a failed
    // bind is accepted; a successful one is checked.
    TcpOutputConfig v6 = config;
    v6.bindAddress = "::1";
    TcpOutputServer sixServer(v6);
    std::string v6Error;
    if (sixServer.start(v6Error)) {
        assert(sixServer.avrEndpoint() == "[::1]:" + std::to_string(sixServer.avrPort()));
        sixServer.stop();
    }
}

// A client that connects and disconnects in a loop must not be able to make
// the worker spin or lose track of the live client set.
void repeatedChurnKeepsClientAccounting() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));

    for (int i = 0; i < 40; ++i) {
        const int client = connectLoopback(server.avrPort());
        waitForClients(server, 1);
        resetClose(client);
        waitForClients(server, 0);
    }

    // The server still serves a normal client after the churn.
    const int client = connectLoopback(server.avrPort());
    waitForClients(server, 1);
    const auto frame = ModeSFrame::shortFrame(0x010203040506ULL, 0x11223344556677ULL, 0x42, true);
    assert(server.tryPublish(frame));
    const auto encoded = encodeAvr(frame);
    std::vector<uint8_t> expected(encoded.bytes.begin(), encoded.bytes.begin() + encoded.size);
    std::vector<uint8_t> got;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (got.size() < expected.size() && std::chrono::steady_clock::now() < deadline) {
        std::array<uint8_t, 256> chunk{};
        pollfd event{client, POLLIN, 0};
        if (::poll(&event, 1, 100) <= 0)
            continue;
        const ssize_t n = ::recv(client, chunk.data(), chunk.size(), 0);
        if (n <= 0)
            break;
        got.insert(got.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(got == expected);
    ::close(client);
    server.stop();
}
// A client of an output port has nothing to say, but it can still send. The
// worker has to consume that instead of sampling 64 bytes per poll pass, which
// left poll() reporting the socket readable again immediately and burned a
// core on a peer sending a few MB/s. The CPU property itself is not asserted
// here -- the test process cannot separate its own sender's cost from the
// worker's -- but the drain path, its EOF branch and continued delivery are.
void chattyClientIsDrainedAndStillServed() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const int client = connectLoopback(server.avrPort());
    waitForClients(server, 1);

    // Push far more than the socket buffers can hold, on a non-blocking socket
    // with a deadline. This is the discriminating part: a server that stops
    // consuming lets the buffers fill, every send() then returns EAGAIN for
    // good, and the transfer misses the deadline instead of hanging the suite.
    const int flags = ::fcntl(client, F_GETFL, 0);
    assert(flags >= 0 && ::fcntl(client, F_SETFL, flags | O_NONBLOCK) == 0);
    const std::vector<char> junk(256 * 1024, 'x');
    const size_t wanted = junk.size() * 8;
    size_t sentTotal = 0;
    const auto sendDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (sentTotal < wanted && std::chrono::steady_clock::now() < sendDeadline) {
        const size_t chunk = std::min(junk.size(), wanted - sentTotal);
        const ssize_t n = ::send(client, junk.data(), chunk, 0);
        if (n > 0) {
            sentTotal += size_t(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        break;
    }
    assert(sentTotal == wanted);
    assert(::fcntl(client, F_SETFL, flags) == 0);

    // The client must still be connected and must still receive its frames:
    // consuming the noise cannot cost the connection or the output.
    assert(server.clientCount() == 1);
    const auto frame = ModeSFrame::shortFrame(0x0A0B0C0D0E0FULL, 0x00FFEEDDCCBBAAULL, 0x31, true);
    assert(server.tryPublish(frame));
    const auto encoded = encodeAvr(frame);
    std::vector<uint8_t> expected(encoded.bytes.begin(), encoded.bytes.begin() + encoded.size);
    std::vector<uint8_t> got;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (got.size() < expected.size() && std::chrono::steady_clock::now() < deadline) {
        std::array<uint8_t, 256> chunk{};
        pollfd event{client, POLLIN, 0};
        if (::poll(&event, 1, 100) <= 0)
            continue;
        const ssize_t n = ::recv(client, chunk.data(), chunk.size(), 0);
        if (n <= 0)
            break;
        got.insert(got.end(), chunk.begin(), chunk.begin() + n);
    }
    assert(got == expected);

    // The noise is consumed without the drain loop mistaking it for an end of
    // stream; when the real one arrives, the client goes. A graceful close is
    // used on purpose: after a bulk transfer an abortive close is not reliably
    // reported to the peer by poll() on Darwin, so resetClose() here made the
    // test flaky without exercising any server behaviour.
    ::close(client);
    waitForClients(server, 0);
    server.stop();
}

// A peer that disconnects must free its slot straight away, with no frame
// published. Holding it until a send() fails looks harmless until the table is
// full of dead peers: the next real consumer is then refused on a server that
// still considers itself healthy.
void disconnectedClientIsReapedWithoutTraffic() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    config.maxClients = 2;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));

    for (int i = 0; i < 2; ++i) {
        const int fd = connectLoopback(server.avrPort());
        waitForClients(server, 1);
        ::close(fd);
        waitForClients(server, 0);
    }
    // A peer that closes only its write side reaches the same end of stream,
    // and is released the same way.
    const int halfClosed = connectLoopback(server.avrPort());
    waitForClients(server, 1);
    ::shutdown(halfClosed, SHUT_WR);
    waitForClients(server, 0);
    ::close(halfClosed);

    // The table is free, so a live consumer is served rather than refused.
    const int live = connectLoopback(server.avrPort());
    waitForClients(server, 1);
    assert(server.rejectedClients() == 0);
    const auto frame = ModeSFrame::shortFrame(0x010203040506ULL, 0x11223344556677ULL, 0x55, true);
    const auto expected = encodedBytes(frame);
    assert(server.tryPublish(frame));
    assert(receiveExact(live, expected.size()) == expected);
    assert(!server.failed());
    ::close(live);
    server.stop();
}

// A permanent poll() failure is injected at the real call site, so it travels
// the same handler as a genuine one. The worker has to latch it, stop, and
// leave the owner able to see it after the join.
void permanentPollFailureIsLatchedAndStopsTheWorker() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.enableAvr = true;
    config.avrPort = 0;
    TcpOutputServer server(config);
    std::string error;
    assert(server.start(error));
    const uint16_t port = server.avrPort();
    const int client = connectLoopback(server.avrPort());
    waitForClients(server, 1);
    assert(!server.failed());

    server.injectPollFailureForTest();
    waitForFailure(server);

    const auto runningDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (server.running() && std::chrono::steady_clock::now() < runningDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(!server.running());
    // Publishing after the failure is refused rather than silently queued.
    assert(!server.tryPublish(ModeSFrame::shortFrame(1, 2, 3, false)));

    const auto before = std::chrono::steady_clock::now();
    server.stop();
    assert(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(1000));
    server.stop(); // idempotent
    // stop() must not clear the failure: it is what the owner reads afterwards.
    assert(server.failed());
    assert(!server.running());
    ::close(client);

    // Cleanup released the listener as well: the port can be taken again.
    TcpOutputConfig again = config;
    again.avrPort = port;
    TcpOutputServer restarted(again);
    assert(restarted.start(error));
    assert(restarted.avrPort() == port);
    restarted.stop();
    // And a normal stop is never reported as a failure.
    assert(!restarted.failed());
    assert(!restarted.running());
}
} // namespace

int main() {
    serverFanout();
    orderedDeliveryAndReconnect();
    slowClientDoesNotBlockHealthyClient();
    promptStopAndInvalidBind();
    fragmentedReads();
    clientClosesImmediately();
    controlBytesAreIgnored();
    clientLimitIsEnforced();
    stopWithoutBacklogIsPrompt();
    stopAfterPublishDeliversBurst();
    stopWithStalledReaderRespectsDeadline();
    doubleStopAndReusePort();
    queueOverflowCounter();
    endpointReportsTheBoundAddress();
    repeatedChurnKeepsClientAccounting();
    chattyClientIsDrainedAndStillServed();
    disconnectedClientIsReapedWithoutTraffic();
    permanentPollFailureIsLatchedAndStopsTheWorker();
}
