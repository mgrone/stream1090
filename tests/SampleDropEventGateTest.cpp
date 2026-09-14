#include "devices/SampleDropEventGate.hpp"

#include <chrono>
#include <iostream>

namespace {
using namespace std::chrono_literals;
using Clock = SampleDropEventGate::Clock;

bool expectNoEvent(std::optional<int64_t> event, const char *where) {
  if (!event)
    return true;
  std::cerr << where << ": unexpected event of " << *event << " pairs\n";
  return false;
}
} // namespace

int main() {
  constexpr uint64_t threshold = 5120; // 2 ms at 2.56 Msps
  const auto t0 = Clock::time_point{} + 10s;

  // A callback is late by 2.7 ms, then the queued samples catch up. This is
  // the shape from the field report (6854 pairs followed by 226): it must
  // never become a loss event.
  SampleDropEventGate recovered(threshold);
  if (!expectNoEvent(recovered.observe(t0, 6854, 0), "recovered/start"))
    return 1;
  if (!expectNoEvent(recovered.observe(t0 + 100ms, 226, 0),
                     "recovered/catch-up"))
    return 2;
  if (!expectNoEvent(recovered.observe(t0 + 2s, 100, 100), "recovered/settled"))
    return 3;

  // Recovery just before the deadline is still recovery. In particular, the
  // gate must not turn the candidate into an event merely because it was high
  // for most of the confirmation window.
  SampleDropEventGate slowRecovery(threshold);
  if (!expectNoEvent(slowRecovery.observe(t0, 13614, 0), "slow/start"))
    return 14;
  if (!expectNoEvent(slowRecovery.observe(t0 + 800ms, 13000, 0), "slow/wait"))
    return 15;
  if (!expectNoEvent(slowRecovery.observe(t0 + 999ms, 41, 0), "slow/catch-up"))
    return 16;
  if (!expectNoEvent(slowRecovery.observe(t0 + 2s, 100, 100), "slow/settled"))
    return 17;

  // A real loss remains above the pre-event deficit for the entire recovery
  // window and is reported once, using the minimum residual rather than the
  // transient peak.
  SampleDropEventGate persistent(threshold);
  if (!expectNoEvent(persistent.observe(t0, 7000, 0), "persistent/start"))
    return 4;
  if (!expectNoEvent(persistent.observe(t0 + 500ms, 6800, 0),
                     "persistent/wait"))
    return 5;
  const auto first = persistent.observe(t0 + 1s, 6700, 0);
  if (!first || *first != 6700) {
    std::cerr << "persistent/confirm: expected one 6700-pair event\n";
    return 6;
  }

  // The same persistent step must not be emitted again. Once it has moved
  // into both sides of the rolling comparison, a later independent step can
  // arm the gate again.
  if (!expectNoEvent(persistent.observe(t0 + 1500ms, 6700, 0),
                     "persistent/latched"))
    return 7;
  if (!expectNoEvent(persistent.observe(t0 + 2s, 6700, 6700),
                     "persistent/rearm"))
    return 8;
  if (!expectNoEvent(persistent.observe(t0 + 2100ms, 13700, 6700),
                     "second/start"))
    return 9;
  if (!expectNoEvent(persistent.observe(t0 + 2700ms, 13600, 6700),
                     "second/wait"))
    return 10;
  const auto second = persistent.observe(t0 + 3100ms, 13500, 6700);
  if (!second || *second != 6800) {
    std::cerr << "second/confirm: expected one 6800-pair event\n";
    return 11;
  }

  // Resetting after an intentional/driver gap also discards a pending
  // candidate; the post-gap stream starts with a clean detector state.
  SampleDropEventGate reset(threshold);
  if (!expectNoEvent(reset.observe(t0, 9000, 0), "reset/start"))
    return 12;
  reset.reset();
  if (!expectNoEvent(reset.observe(t0 + 2s, 100, 100), "reset/after"))
    return 13;

  return 0;
}
