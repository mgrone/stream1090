/* SPDX-License-Identifier: GPL-3.0-or-later */
//
// The device path cannot be driven end to end without a dongle, so what is
// tested here is the code that path actually runs: the watchdog check and the
// owner-side completion from TcpOutputSupervision.hpp, against a real
// TcpOutputServer with an injected failure and a fake device.
//
// It covers what the stdin integration test cannot reach: the reader being
// woken, the failure surviving the join, and the device not being closed twice
// because the supervision decided to close it as well.

#include "TcpOutputSupervision.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

// Stands in for InputDeviceBase: the supervision only ever calls
// shutdownWriter() on it, and close() is here to prove it stays untouched.
class FakeDevice {
  public:
    void shutdownWriter() {
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_woken;
        }
        m_readerReleased.notify_all();
    }

    void close() { ++m_closed; }

    int woken() const {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_woken;
    }
    int closed() const { return m_closed; }

    // A reader blocked until the writer is shut down, like the DSP loop
    // blocked on the ring buffer.
    bool waitForWakeUp(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_readerReleased.wait_for(lock, timeout, [this] { return m_woken > 0; });
    }

  private:
    mutable std::mutex m_mutex;
    std::condition_variable m_readerReleased;
    int m_woken = 0;
    std::atomic<int> m_closed{0};
};

std::unique_ptr<TcpOutputServer> startedServer() {
    TcpOutputConfig config;
    config.bindAddress = "127.0.0.1";
    config.avrPort = 0;
    config.enableAvr = true;
    auto server = std::make_unique<TcpOutputServer>(config);
    std::string error;
    if (!server->start(error)) {
        std::fprintf(stderr, "server start failed: %s\n", error.c_str());
        assert(false);
    }
    return server;
}

void waitForFailure(const TcpOutputServer& server) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!server.failed() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(server.failed());
}

void clearShutdownFlag() {
    ProcessSignals::g_shutdownRequested.store(false, std::memory_order_relaxed);
}

// A healthy output, and no output at all, must leave everything alone: this is
// what keeps a normal run from being turned into a failure.
void healthyOutputIsNotSupervisedAway() {
    clearShutdownFlag();
    FakeDevice device;
    auto server = startedServer();
    assert(!superviseTcpOutput(server.get(), device));
    assert(device.woken() == 0);
    assert(device.closed() == 0);
    assert(!ProcessSignals::shutdownRequested());

    assert(!superviseTcpOutput<FakeDevice>(nullptr, device));
    assert(device.woken() == 0);
    assert(!ProcessSignals::shutdownRequested());

    // A normal stop is a successful run.
    assert(finishTcpOutput(server));
    assert(!server->running());
    assert(!server->failed());

    // And so is no server at all.
    std::unique_ptr<TcpOutputServer> none;
    assert(finishTcpOutput(none));
}

// The watchdog path: the failure is noticed at the watchdog's own cadence, the
// blocked reader is released, the shutdown flag is raised, and the device is
// left for the owner's cleanup to close exactly once.
void failedOutputWakesTheReaderAndFailsTheRun() {
    clearShutdownFlag();
    FakeDevice device;
    auto server = startedServer();

    // A reader that only comes back when the writer is shut down. Without the
    // wake-up it would sit here until the test's deadline.
    std::atomic<bool> readerReturned{false};
    std::thread reader([&] {
        readerReturned.store(device.waitForWakeUp(std::chrono::seconds(10)), std::memory_order_release);
    });

    std::thread watchdog([&] {
        using namespace std::chrono_literals;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!ProcessSignals::shutdownRequested() && std::chrono::steady_clock::now() < deadline) {
            if (superviseTcpOutput(server.get(), device))
                break;
            std::this_thread::sleep_for(200ms); // the watchdog's real cadence
        }
    });

    server->injectPollFailureForTest();
    waitForFailure(*server);
    watchdog.join();
    reader.join();

    assert(readerReturned.load(std::memory_order_acquire));
    assert(device.woken() == 1);
    // The supervision must not close the device: the owner's shutdown path
    // does that in every case, and two threads in close() is the bug this
    // guards against.
    assert(device.closed() == 0);
    assert(ProcessSignals::shutdownRequested());

    // The owner joins the worker and only then decides: the run failed.
    assert(!finishTcpOutput(server));
    assert(!server->running());
    assert(server->failed());
    // Reading it again after the stop keeps saying so, and stopping twice is
    // harmless.
    assert(!finishTcpOutput(server));

    // The owner closes the device once, as it does on every path.
    device.close();
    assert(device.closed() == 1);
    clearShutdownFlag();
}

// A failure that lands while the run is already ending still has to be seen.
// Nothing supervises this path: the producer simply reached EOF, and the only
// thing that reports the failure is the state being read after the join.
void failureWithoutAWatchdogIsStillAFailure() {
    clearShutdownFlag();
    auto server = startedServer();
    server->injectPollFailureForTest();
    // Waiting for the flag first keeps the case deterministic; the point of
    // the check is that stop() neither clears it nor is asked about it before
    // the worker has been joined.
    waitForFailure(*server);
    assert(!finishTcpOutput(server));
    assert(server->failed());
    assert(!server->running());
}

} // namespace

int main() {
    healthyOutputIsNotSupervisedAway();
    failedOutputWakesTheReaderAndFailsTheRun();
    failureWithoutAWatchdogIsStillAFailure();
    return 0;
}
