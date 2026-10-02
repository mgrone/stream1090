/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "TcpOutputServer.hpp"

#include "ModeSFrameEncoder.hpp"
#include "SpscFrameQueue.hpp"
#include "Logger.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
// One poll pass accepts at most this many connections per listener, rejections
// included, so a connection storm cannot starve the frame fan-out.
constexpr int kMaxAcceptAttempts = 32;

// One drain pass moves at most this many frames out of the SPSC ring, so a busy
// producer cannot keep the network thread inside the loop indefinitely.
constexpr size_t kMaxFramesPerBatch = 256;

constexpr int kPollTimeoutMs = 10;

// Best-effort shutdown budget: the worker keeps trying to hand pending bytes to
// the sockets for this long and then gives up. No client can make stop() hang.
constexpr auto kShutdownDrainBudget = std::chrono::milliseconds(250);

// Aggregated diagnostics are emitted at most once per interval.
constexpr auto kDiagnosticsInterval = std::chrono::seconds(5);

// accept() can fail for reasons that persist and leave the listener readable:
// a process- or system-wide descriptor limit, or exhausted kernel memory.
// Returning straight away would spin at 100% CPU on a readable listener, so the
// listeners are taken out of the poll set for this long instead.
constexpr auto kAcceptBackoff = std::chrono::milliseconds(500);

// Per-client connect/disconnect lines are useful when a feed is set up and pure
// noise when a peer reconnects in a loop, so at most this many are logged per
// diagnostics interval. The totals are always reported by the periodic summary.
constexpr uint32_t kLifecycleLogsPerInterval = 8;

// A client buffer that grew past this keeps its capacity only until the backlog
// clears; after that the memory goes back instead of staying resident for the
// lifetime of the connection.
constexpr size_t kPendingRetainBytes = 64 * 1024;

// A client of an output port has nothing to say, but nothing stops it from
// sending anyway. Whatever arrives has to be drained per poll pass rather than
// sampled, or poll() reports the socket readable again immediately and a
// chatty peer turns the worker into a busy loop. The cap keeps one noisy
// client from holding the pass while the others wait.
constexpr size_t kClientDrainChunk = 4096;
constexpr int kMaxClientDrainReads = 16;

// Cap the kernel send buffer of an accepted client.
//
// Left to autotune, Linux grows it to net.ipv4.tcp_wmem's maximum, 4 MB on a
// stock kernel. Two things follow, and neither is wanted here. The per-client
// limit a user configures stops meaning what it says: a slow client is only
// disconnected once the kernel buffer is full too, so the real bound is the
// limit plus several megabytes. And a real-time feed ends up with megabytes of
// stale frames queued ahead of the live ones -- at ADS-B rates 4 MB is well
// over a hundred thousand frames, none of which a consumer still wants by the
// time it reads them.
//
// This is large enough that a healthy consumer never notices (a busy site
// produces a few kB per second, so it holds seconds of traffic) and small
// enough that the backlog a client can hold stays dominated by the configured
// limit, on every platform. Linux doubles the value for its own bookkeeping;
// that is fine, the point is the order of magnitude.
constexpr int kClientSendBufferBytes = 64 * 1024;

struct Client {
    int fd = -1;
    std::vector<uint8_t> pending;
    size_t offset = 0;
};

bool setNonBlockingCloseOnExec(int fd) {
    const int status = ::fcntl(fd, F_GETFL, 0);
    const int descriptor = ::fcntl(fd, F_GETFD, 0);
    return status >= 0 && descriptor >= 0 && ::fcntl(fd, F_SETFL, status | O_NONBLOCK) == 0 &&
           ::fcntl(fd, F_SETFD, descriptor | FD_CLOEXEC) == 0;
}

void closeFd(int& fd) noexcept {
    if (fd >= 0)
        ::close(fd);
    fd = -1;
}

// True for the accept() failures that persist while the listener stays
// readable. EAGAIN (no pending connection) and EINTR are handled separately.
bool isAcceptResourceError(int code) noexcept {
    return code == EMFILE || code == ENFILE || code == ENOBUFS || code == ENOMEM;
}

std::string formatEndpoint(const std::string& host, uint16_t port) {
    const bool numericV6 = host.find(':') != std::string::npos;
    return (numericV6 ? "[" + host + "]" : host) + ":" + std::to_string(port);
}

int sendFlags() noexcept {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}
} // namespace

struct TcpOutputServer::Impl {
    explicit Impl(TcpOutputConfig value) : config(std::move(value)) {}

    TcpOutputConfig config;
    SpscFrameQueue<4096> frames;
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};
    // Latched permanent failure of the worker, distinct from a normal stop.
    // The worker publishes it before it clears running, stop() never clears it
    // and the owner reads it to fail the whole run.
    std::atomic<bool> failed{false};
    std::atomic<size_t> connectedClients{0};
    std::atomic<uint64_t> dropped{0};
    std::atomic<uint64_t> slowDisconnects{0};
    std::atomic<uint64_t> rejectedClients{0};
    std::atomic<uint64_t> avrEncoded{0};
    bool startAttempted = false;
#ifdef STREAM1090_TCP_TEST_SEAM
    std::atomic<bool> consumptionSuspended{false};
    std::atomic<bool> injectPollFailure{false};
#endif
    int controlRead = -1;
    int controlWrite = -1;
    int avrListener = -1;
    uint16_t actualAvrPort = 0;
    // Set once during start() on the owner thread, read afterwards. A bind
    // address that resolves to several families lands on exactly one of them,
    // so this is the endpoint a consumer actually has to connect to.
    std::string actualAvrEndpoint;
    std::thread worker;
    std::vector<Client> clients;
    std::vector<pollfd> pollEvents;
    // Worker-thread-only bookkeeping.
    bool listenerPolled = false;
    std::chrono::steady_clock::time_point acceptBackoffUntil{};
    uint64_t connects = 0;
    uint64_t disconnects = 0;
    uint64_t lastLoggedDropped = 0;
    uint64_t lastLoggedSlow = 0;
    uint64_t lastLoggedRejected = 0;
    uint64_t lastLoggedConnects = 0;
    uint64_t lastLoggedDisconnects = 0;
    uint32_t lifecycleLogBudget = kLifecycleLogsPerInterval;
    std::chrono::steady_clock::time_point lifecycleWindowStart{};
    std::chrono::steady_clock::time_point lastDiagnostics{};
    std::chrono::steady_clock::time_point lastAcceptFailureLog{};

    // Token bucket for the per-client connect/disconnect lines, refilled once
    // per diagnostics interval. Worker thread only.
    bool allowLifecycleLog(std::chrono::steady_clock::time_point now) {
        if (lifecycleWindowStart == std::chrono::steady_clock::time_point{} ||
            now - lifecycleWindowStart >= kDiagnosticsInterval) {
            lifecycleWindowStart = now;
            lifecycleLogBudget = kLifecycleLogsPerInterval;
        }
        if (lifecycleLogBudget == 0)
            return false;
        --lifecycleLogBudget;
        return true;
    }

    int createListener(const std::string& address, uint16_t requestedPort, uint16_t& actualPort,
                       std::string& actualEndpoint, std::string& error) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        const std::string port = std::to_string(requestedPort);
        addrinfo* results = nullptr;
        const int lookup = ::getaddrinfo(address.empty() ? nullptr : address.c_str(), port.c_str(), &hints, &results);
        if (lookup != 0) {
            error = "cannot resolve bind address '" + address + "': " + ::gai_strerror(lookup);
            return -1;
        }

        int listener = -1;
        int lastError = 0;
        for (addrinfo* candidate = results; candidate; candidate = candidate->ai_next) {
            listener = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
            if (listener < 0) {
                lastError = errno;
                continue;
            }
            const int yes = 1;
            ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            if (!setNonBlockingCloseOnExec(listener) ||
                ::bind(listener, candidate->ai_addr, candidate->ai_addrlen) != 0 || ::listen(listener, 8) != 0) {
                lastError = errno;
                closeFd(listener);
                continue;
            }
            break;
        }
        ::freeaddrinfo(results);

        if (listener < 0) {
            error = "cannot listen on " + address + ":" + port + ": " + std::strerror(lastError);
            return -1;
        }

        sockaddr_storage bound{};
        socklen_t boundLength = sizeof(bound);
        if (::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &boundLength) != 0) {
            error = "cannot inspect listening socket: " + std::string(std::strerror(errno));
            closeFd(listener);
            return -1;
        }
        if (bound.ss_family == AF_INET)
            actualPort = ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
        else
            actualPort = ntohs(reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port);

        // Report the address that was really bound, not the one that was asked
        // for: 'localhost' and any other name with both an A and a AAAA record
        // resolves to a single family here, and a consumer pointed at the other
        // one would get a connection refused with nothing to show for it.
        char host[NI_MAXHOST] = {};
        if (::getnameinfo(reinterpret_cast<sockaddr*>(&bound), boundLength, host, sizeof(host), nullptr, 0,
                          NI_NUMERICHOST) == 0) {
            actualEndpoint = formatEndpoint(host, actualPort);
            // The listening line is INFO and therefore invisible at the default
            // log level, so the one case a user has to know about gets a
            // warning of its own: the address that was asked for is not the
            // address that is listening, and a consumer aimed at the other one
            // would only ever see a connection refused.
            if (address != host)
                Log::warn("TCP") << "bind address '" << address << "' resolved to " << actualEndpoint
                                 << "; point the consumer at that address.";
        } else {
            actualEndpoint = formatEndpoint(address, actualPort);
        }
        return listener;
    }

    // Owner thread only, before the worker exists. Closes every acquired
    // resource so a partial startup leaves no listener or pipe behind.
    void closeAll() noexcept {
        for (auto& client : clients)
            closeFd(client.fd);
        clients.clear();
        connectedClients.store(0, std::memory_order_relaxed);
        closeFd(avrListener);
        closeFd(controlRead);
        closeFd(controlWrite);
    }

    bool initialize(std::string& error) {
        if (!config.enableAvr) {
            error = "no AVR TCP output enabled";
            return false;
        }
        if (config.bindAddress.empty()) {
            error = "TCP bind address must not be empty";
            return false;
        }
        if (config.maxClients == 0) {
            error = "TCP maxClients must be greater than zero";
            return false;
        }
        if (config.clientBufferLimit == 0) {
            error = "TCP client buffer limit must be greater than zero";
            return false;
        }
        int pipeFds[2];
        if (::pipe(pipeFds) != 0) {
            error = "cannot create TCP control pipe: " + std::string(std::strerror(errno));
            return false;
        }
        controlRead = pipeFds[0];
        controlWrite = pipeFds[1];
        if (!setNonBlockingCloseOnExec(controlRead) || !setNonBlockingCloseOnExec(controlWrite)) {
            error = "cannot configure TCP control pipe: " + std::string(std::strerror(errno));
            closeAll();
            return false;
        }
        avrListener = createListener(config.bindAddress, config.avrPort, actualAvrPort, actualAvrEndpoint, error);
        if (avrListener < 0) {
            closeAll();
            return false;
        }
        pollEvents.reserve(2 + config.maxClients);
        return true;
    }

    void acceptClients(int listener) {
        for (int attempt = 0; attempt < kMaxAcceptAttempts; ++attempt) {
            const int fd = ::accept(listener, nullptr, nullptr);
            if (fd < 0) {
                const int code = errno;
                if (code == EINTR)
                    continue;
                if (code == EAGAIN || code == EWOULDBLOCK)
                    return; // Nothing more pending: the normal way out.
                if (code == ECONNABORTED)
                    continue; // The peer went away between the SYN and here.
                // Anything else is assumed to persist while the listener stays
                // readable (a descriptor limit above all). Without the backoff
                // poll() would report the listener ready on every pass and the
                // worker would spin at 100% CPU until a descriptor frees up.
                const auto now = std::chrono::steady_clock::now();
                acceptBackoffUntil = now + kAcceptBackoff;
                // A failure that persists is retried once per backoff, so the
                // warning needs its own throttle or it becomes the flood.
                if (lastAcceptFailureLog == std::chrono::steady_clock::time_point{} ||
                    now - lastAcceptFailureLog >= kDiagnosticsInterval) {
                    lastAcceptFailureLog = now;
                    Log::warn("TCP") << "accept() failed: " << std::strerror(code) << "; not accepting for "
                                     << kAcceptBackoff.count() << " ms."
                                     << (isAcceptResourceError(code) ? " Out of file descriptors?" : "");
                }
                return;
            }
            if (clients.size() >= config.maxClients) {
                rejectedClients.fetch_add(1, std::memory_order_relaxed);
                ::close(fd);
                continue;
            }
            if (!setNonBlockingCloseOnExec(fd)) {
                ::close(fd);
                continue;
            }
            const int yes = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
            const int sendBuffer = kClientSendBufferBytes;
            ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendBuffer, sizeof(sendBuffer));
#ifdef SO_NOSIGPIPE
            ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
            clients.push_back({fd, {}, 0});
            connectedClients.store(clients.size(), std::memory_order_relaxed);
            ++connects;
            if (allowLifecycleLog(std::chrono::steady_clock::now()))
                Log::info("TCP") << "client connected (avr), " << clients.size() << " total";
        }
    }

    // Reads and throws away whatever a client sent, and notices the end of the
    // stream. Anything a client writes to an output port is noise; the point of
    // reading it is to keep the socket from staying readable forever.
    void drainClientInput(Client& client) {
        std::array<char, kClientDrainChunk> discard;
        for (int attempt = 0; attempt < kMaxClientDrainReads; ++attempt) {
            const ssize_t received = ::recv(client.fd, discard.data(), discard.size(), 0);
            if (received > 0)
                continue;
            if (received == 0) {
                // End of stream: the peer is gone. This is what frees the
                // slot, and it is the only signal that does so without
                // traffic -- a peer that merely closed raises no POLLHUP on
                // either Linux or Darwin.
                dropClient(client, "disconnected");
                return;
            }
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            dropClient(client, "disconnected");
            return;
        }
    }

    // Closes a client socket and accounts for it once, so the lifecycle totals
    // cannot drift from the number of sockets that were actually dropped.
    void dropClient(Client& client, const char* reason) {
        if (client.fd < 0)
            return;
        closeFd(client.fd);
        ++disconnects;
        if (allowLifecycleLog(std::chrono::steady_clock::now()))
            Log::info("TCP") << "client " << reason << " (avr)";
    }

    // Appends an already encoded frame, or drops the client when the unsent
    // bytes would cross the per-client limit. The check is a subtraction so the
    // sum cannot overflow.
    void appendFrame(Client& client, const EncodedModeSFrame& encoded) {
        if (client.fd < 0)
            return;
        const size_t remaining = client.pending.size() - client.offset;
        if (remaining > config.clientBufferLimit || encoded.size > config.clientBufferLimit - remaining) {
            slowDisconnects.fetch_add(1, std::memory_order_relaxed);
            dropClient(client, "too slow, disconnecting");
            return;
        }
        if (client.offset != 0 && (client.offset == client.pending.size() || client.offset > 4096)) {
            client.pending.erase(client.pending.begin(), client.pending.begin() + std::ptrdiff_t(client.offset));
            client.offset = 0;
        }
        client.pending.insert(client.pending.end(), encoded.bytes.begin(), encoded.bytes.begin() + encoded.size);
    }

    void distributeFrames(size_t maxFrames) {
        const bool haveClients = std::any_of(clients.begin(), clients.end(),
                                            [](const Client& client) { return client.fd >= 0; });
        if (!haveClients) {
            // Nothing to serve: drain the ring so the producer never blocks on
            // a queue nobody reads. Frames published with no client attached
            // are dropped by design.
            ModeSFrame discarded;
            for (size_t i = 0; i < maxFrames && frames.tryPop(discarded); ++i) {}
            return;
        }

        ModeSFrame frame;
        size_t processed = 0;
        while (processed < maxFrames && frames.tryPop(frame)) {
            ++processed;
            const auto avr = encodeAvr(frame);
            avrEncoded.fetch_add(1, std::memory_order_relaxed);
            for (auto& client : clients)
                appendFrame(client, avr);
        }
    }

    void flushClient(Client& client) {
        while (client.fd >= 0 && client.offset < client.pending.size()) {
            const ssize_t sent = ::send(client.fd, client.pending.data() + client.offset,
                                        client.pending.size() - client.offset, sendFlags());
            if (sent > 0) {
                client.offset += size_t(sent);
                continue;
            }
            if (sent < 0 && errno == EINTR)
                continue;
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return;
            dropClient(client, "send failed, disconnecting");
        }
        if (client.fd >= 0 && client.offset == client.pending.size()) {
            client.pending.clear();
            client.offset = 0;
            // A buffer that grew during a backlog would otherwise stay resident
            // for the life of the connection: maxClients of them adds up.
            if (client.pending.capacity() > kPendingRetainBytes)
                std::vector<uint8_t>().swap(client.pending);
        }
    }

    void flushAllClients() {
        for (auto& client : clients)
            flushClient(client);
    }

    void removeClosedClients() {
        clients.erase(std::remove_if(clients.begin(), clients.end(), [](const Client& client) {
                          return client.fd < 0;
                      }), clients.end());
        connectedClients.store(clients.size(), std::memory_order_relaxed);
    }

    void readControl() {
        std::array<char, 64> discard;
        while (::read(controlRead, discard.data(), discard.size()) > 0) {}
    }

    // The listener slot is the only variable part of the pollfd layout, so
    // the decision is taken here and recorded in listenerPolled; servicePollEvents
    // reads it back instead of recomputing it. Passing it as a parameter to both
    // is what would let the two disagree and shift every client index by one.
    void buildPollEvents(bool includeListener) {
        listenerPolled = includeListener && std::chrono::steady_clock::now() >= acceptBackoffUntil;
        pollEvents.clear();
        pollEvents.push_back({controlRead, POLLIN, 0});
        if (listenerPolled) {
            if (avrListener >= 0)
                pollEvents.push_back({avrListener, POLLIN, 0});
        }
        for (const auto& client : clients) {
            short requested = includeListener ? POLLIN : 0;
            if (client.offset < client.pending.size())
                requested |= POLLOUT;
            pollEvents.push_back({client.fd, requested, 0});
        }
    }

    void servicePollEvents() {
        size_t index = 0;
        if (pollEvents[index++].revents & POLLIN)
            readControl();
        if (listenerPolled) {
            if (avrListener >= 0 && (pollEvents[index++].revents & POLLIN))
                acceptClients(avrListener);
        }

        const size_t clientsAtPoll = pollEvents.size() - index;
        for (size_t i = 0; i < clientsAtPoll && i < clients.size(); ++i) {
            Client& client = clients[i];
            const short occurred = pollEvents[index + i].revents;
            if (occurred & (POLLERR | POLLHUP | POLLNVAL)) {
                dropClient(client, "disconnected");
                continue;
            }
            if (occurred & POLLIN)
                drainClientInput(client);
        }
    }

    // The one place the worker loop calls poll(). Wrapping it keeps the test
    // seam at the real call site, so an injected failure travels the same
    // handler as a genuine one instead of short-circuiting it.
    int pollWorkerFds(int timeoutMs) {
#ifdef STREAM1090_TCP_TEST_SEAM
        if (injectPollFailure.load(std::memory_order_relaxed)) {
            errno = EBADF; // not EINTR: a failure that cannot be retried
            return -1;
        }
#endif
        return ::poll(pollEvents.data(), pollEvents.size(), timeoutMs);
    }

    // Returns false only on a permanent poll() failure, which ends the worker
    // loop; the final summary then reports what the outputs managed to send.
    bool pollOnce(int timeoutMs) {
        buildPollEvents(true);
        int ready;
        do {
            ready = pollWorkerFds(timeoutMs);
        } while (ready < 0 && errno == EINTR && !stopRequested.load(std::memory_order_acquire));
        if (ready < 0) {
            if (errno == EINTR)
                return true;
            Log::error("TCP") << "poll() failed: " << std::strerror(errno)
                              << "; the TCP output cannot continue and the run will stop.";
            return false;
        }
        return true;
    }

    void drainUntil(std::chrono::steady_clock::time_point deadline) {
        for (;;) {
            if (!frames.empty())
                distributeFrames(kMaxFramesPerBatch);
            flushAllClients();
            removeClosedClients();

            bool pending = false;
            for (const auto& client : clients) {
                if (client.fd >= 0 && client.offset < client.pending.size()) {
                    pending = true;
                    break;
                }
            }
            if (frames.empty() && !pending)
                return;
            if (std::chrono::steady_clock::now() >= deadline)
                return;

            // Wait briefly for the sockets to become writable without spinning.
            buildPollEvents(false);
            int ready;
            do {
                ready = ::poll(pollEvents.data(), pollEvents.size(), 1);
            } while (ready < 0 && errno == EINTR);
            if (ready < 0)
                return;
            if (ready > 0)
                servicePollEvents();
        }
    }

    void maybeLogDiagnostics() {
        const uint64_t currentDropped = dropped.load(std::memory_order_relaxed);
        const uint64_t currentSlow = slowDisconnects.load(std::memory_order_relaxed);
        const uint64_t currentRejected = rejectedClients.load(std::memory_order_relaxed);
        if (currentDropped == lastLoggedDropped && currentSlow == lastLoggedSlow &&
            currentRejected == lastLoggedRejected && connects == lastLoggedConnects &&
            disconnects == lastLoggedDisconnects)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (lastDiagnostics != std::chrono::steady_clock::time_point{} &&
            now - lastDiagnostics < kDiagnosticsInterval)
            return;
        const bool lost = currentDropped != lastLoggedDropped || currentSlow != lastLoggedSlow ||
                          currentRejected != lastLoggedRejected;
        lastDiagnostics = now;
        lastLoggedDropped = currentDropped;
        lastLoggedSlow = currentSlow;
        lastLoggedRejected = currentRejected;
        lastLoggedConnects = connects;
        lastLoggedDisconnects = disconnects;
        std::ostringstream line;
        line << "output diagnostics: " << currentDropped << " frame(s) dropped (queue full), " << currentSlow
             << " slow client(s) disconnected, " << currentRejected << " client(s) refused, " << connects
             << " connect(s) / " << disconnects << " disconnect(s).";
        // Pure client churn is normal for a decoder that reconnects; only
        // actual loss deserves a warning.
        if (lost)
            Log::warn("TCP", line.str());
        else
            Log::info("TCP", line.str());
    }

    void logFinalSummary() {
        const uint64_t currentDropped = dropped.load(std::memory_order_relaxed);
        const uint64_t currentSlow = slowDisconnects.load(std::memory_order_relaxed);
        const uint64_t currentRejected = rejectedClients.load(std::memory_order_relaxed);
        const uint64_t avr = avrEncoded.load(std::memory_order_relaxed);
        std::ostringstream summary;
        summary << "TCP outputs stopped: " << avr << " AVR frame(s) encoded, "
                << currentDropped << " frame(s) dropped (queue full), " << currentSlow
                << " slow client(s) disconnected, " << currentRejected << " client(s) refused, " << connects
                << " connect(s) / " << disconnects << " disconnect(s). The shutdown drain is best effort: a"
                << " successful send() is not proof of remote receipt.";
        const bool lost = currentDropped != 0 || currentSlow != 0 || currentRejected != 0;
        if (lost)
            Log::warn("TCP", summary.str());
        else
            Log::info("TCP", summary.str());
    }

    void run() {
        bool fatalPollError = false;
        for (;;) {
            if (stopRequested.load(std::memory_order_acquire))
                break;
            const int timeout = frames.empty() ? kPollTimeoutMs : 0;
            if (!pollOnce(timeout)) {
                fatalPollError = true;
                // Publish the failure before anything else: the owner polls
                // this to end the run, and it must not be able to observe a
                // worker that is no longer running but not marked as failed.
                failed.store(true, std::memory_order_release);
                break;
            }
            servicePollEvents();
#ifdef STREAM1090_TCP_TEST_SEAM
            if (!consumptionSuspended.load(std::memory_order_relaxed))
                distributeFrames(kMaxFramesPerBatch);
#else
            distributeFrames(kMaxFramesPerBatch);
#endif
            flushAllClients();
            removeClosedClients();
            maybeLogDiagnostics();
        }
        if (!fatalPollError)
            drainUntil(std::chrono::steady_clock::now() + kShutdownDrainBudget);

        // Only the owner closes the control pipe and the listeners; the worker
        // just releases its client sockets and marks the loop as finished.
        for (auto& client : clients)
            closeFd(client.fd);
        clients.clear();
        connectedClients.store(0, std::memory_order_relaxed);
        logFinalSummary();
        running.store(false, std::memory_order_release);
    }
};

TcpOutputServer::TcpOutputServer(TcpOutputConfig config) : m_impl(std::make_unique<Impl>(std::move(config))) {}
TcpOutputServer::~TcpOutputServer() { stop(); }

bool TcpOutputServer::start(std::string& error) {
    if (m_impl->startAttempted) {
        error = "TcpOutputServer cannot be restarted; create a new instance";
        return false;
    }
    m_impl->startAttempted = true;
    if (!m_impl->initialize(error))
        return false;

    m_impl->running.store(true, std::memory_order_release);
    try {
        m_impl->worker = std::thread([impl = m_impl.get()] { impl->run(); });
    } catch (const std::system_error& exception) {
        error = std::string("cannot start TCP worker thread: ") + exception.what();
        m_impl->running.store(false, std::memory_order_release);
        m_impl->closeAll();
        return false;
    }
    return true;
}

void TcpOutputServer::stop() noexcept {
    if (m_impl->running.load(std::memory_order_acquire)) {
        m_impl->stopRequested.store(true, std::memory_order_release);
        if (m_impl->controlWrite >= 0) {
            const char wake = 1;
            // The control pipe carries a single byte, so a short write or
            // EAGAIN means the read end is gone. Retry on EINTR and keep the
            // result in a variable: glibc marks write() warn_unused_result.
            ssize_t written;
            do {
                written = ::write(m_impl->controlWrite, &wake, 1);
            } while (written < 0 && errno == EINTR);
            (void)written;
        }
    }
    if (m_impl->worker.joinable()) {
        // stop() is noexcept and runs from a destructor: a join() that threw
        // would take the process down instead of the run down.
        try {
            m_impl->worker.join();
        } catch (const std::system_error& exception) {
            Log::error("TCP") << "cannot join the TCP worker thread: " << exception.what();
            return; // The thread still owns its fds; leaking them beats a double close.
        }
    }
    m_impl->closeAll();
}

bool TcpOutputServer::tryPublish(const ModeSFrame& frame) noexcept {
    // Single producer contract: only the DSP/handler thread calls this, and it
    // stops before stop() is invoked. No logging and no waiting here.
    if (!m_impl->running.load(std::memory_order_acquire) ||
        m_impl->stopRequested.load(std::memory_order_relaxed))
        return false;
    if (m_impl->frames.tryPush(frame))
        return true;
    m_impl->dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool TcpOutputServer::running() const noexcept { return m_impl->running.load(std::memory_order_acquire); }
bool TcpOutputServer::failed() const noexcept { return m_impl->failed.load(std::memory_order_acquire); }
uint16_t TcpOutputServer::avrPort() const noexcept { return m_impl->actualAvrPort; }
std::string TcpOutputServer::avrEndpoint() const { return m_impl->actualAvrEndpoint; }
size_t TcpOutputServer::clientCount() const noexcept { return m_impl->connectedClients.load(std::memory_order_relaxed); }
uint64_t TcpOutputServer::droppedFrames() const noexcept { return m_impl->dropped.load(std::memory_order_relaxed); }
uint64_t TcpOutputServer::slowClientDisconnects() const noexcept {
    return m_impl->slowDisconnects.load(std::memory_order_relaxed);
}
uint64_t TcpOutputServer::rejectedClients() const noexcept {
    return m_impl->rejectedClients.load(std::memory_order_relaxed);
}
uint64_t TcpOutputServer::avrFramesEncoded() const noexcept {
    return m_impl->avrEncoded.load(std::memory_order_relaxed);
}
#ifdef STREAM1090_TCP_TEST_SEAM
void TcpOutputServer::setConsumptionSuspendedForTest(bool suspended) noexcept {
    m_impl->consumptionSuspended.store(suspended, std::memory_order_relaxed);
}

void TcpOutputServer::injectPollFailureForTest() noexcept {
    m_impl->injectPollFailure.store(true, std::memory_order_relaxed);
}

#endif
