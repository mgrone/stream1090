/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "ModeSFrame.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// Configuration for the optional AVR TCP output.
//
// A port of 0 asks the kernel for an ephemeral port. The command line refuses
// 0; the test API uses it. An empty bind address, a zero client buffer limit
// and a zero client limit are rejected at start().
struct TcpOutputConfig {
    std::string bindAddress = "127.0.0.1";
    uint16_t avrPort = 0;
    bool enableAvr = false;
    size_t clientBufferLimit = 256 * 1024;
    size_t maxClients = 32;
};

// Asynchronous fan-out of decoded Mode-S frames to AVR TCP clients.
//
// Lifecycle contract:
//   * tryPublish() has exactly one producer: the DSP/handler thread.
//   * stop() is called by the owning thread only after that producer has
//     quiesced; it is idempotent and safe after a failed or partial start.
//   * stop() is best effort: it drains the bounded queue for at most 250 ms
//     and sends whatever the sockets accept. A successful send() does not
//     prove that a remote peer received or decoded the bytes, and frames
//     published while no client is connected are dropped by design.
//   * A client is released as soon as its peer closes: end of stream on the
//     socket is a disconnect. Waiting for a failing send() instead would hold
//     the slot for as long as no frame is published, and a table filled with
//     dead peers refuses the live ones.
//   * Restarting the same instance is not supported. After stop(), create a
//     new instance to listen again on the same port.
//   * failed() latches a permanent worker failure (a poll() that cannot be
//     retried). It is set before the worker declares itself finished, stop()
//     does not clear it, and the owner turns it into a failed run. A slow
//     client, a full queue or a transient accept() error is not a failure of
//     the server and never sets it.
////
// A frame carries a signal level only when the build has RSSI enabled. Without
// it the AVR output uses the '@' prefix (no signal field).
class TcpOutputServer {
  public:
    explicit TcpOutputServer(TcpOutputConfig config);
    ~TcpOutputServer();

    TcpOutputServer(const TcpOutputServer&) = delete;
    TcpOutputServer& operator=(const TcpOutputServer&) = delete;

    bool start(std::string& error);
    void stop() noexcept;
    bool tryPublish(const ModeSFrame& frame) noexcept;

    bool running() const noexcept;
    // True once the worker has hit a permanent failure. Latched: it survives
    // stop() and the worker publishes it before it stops running.
    bool failed() const noexcept;
    uint16_t avrPort() const noexcept;
    // The endpoint the listener actually bound, formatted for logging
    // ("127.0.0.1:30006", "[::1]:30006"). A bind address that resolves to
    // several families resolves to exactly one of them, so this is what a
    // consumer must connect to -- not necessarily what was asked for.
    std::string avrEndpoint() const;
    size_t clientCount() const noexcept;
    uint64_t droppedFrames() const noexcept;
    uint64_t slowClientDisconnects() const noexcept;
    uint64_t rejectedClients() const noexcept;
    uint64_t avrFramesEncoded() const noexcept;

#ifdef STREAM1090_TCP_TEST_SEAM
    // Test-only seam, compiled only into the TCP server test target. It lets
    // the queue-overflow test suspend the worker's frame consumption so the
    // bounded SPSC ring can be saturated deterministically. It does not exist
    // in production builds and adds no public tuning option.
    void setConsumptionSuspendedForTest(bool suspended) noexcept;

    // Test-only fault injection. The next poll() the worker would issue fails
    // with a code that is not retryable, so the test travels the same handler
    // as a real permanent failure instead of setting the flag behind its back.
    // Compiled only into the test targets.
    void injectPollFailureForTest() noexcept;
#endif

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
