/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

// Turns changes in the sample-clock deficit into distinct drop events.
//
// A callback arriving late creates exactly the same instantaneous deficit as
// a real FIFO/USB loss. The difference is that queued samples subsequently
// catch up. Therefore a threshold crossing is only a candidate: it must remain
// above the pre-event baseline for a recovery window before it is reported.
// After reporting, the gate stays latched until the rolling comparison has
// absorbed the step, so one missing transfer cannot be counted repeatedly.
class SampleDropEventGate {
public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto RecoveryWindow = std::chrono::seconds(1);

  explicit SampleDropEventGate(uint64_t threshold)
      : m_threshold(static_cast<int64_t>(threshold)) {}

  std::optional<int64_t> observe(Clock::time_point now, int64_t deficit,
                                 int64_t pastDeficit) {
    const int64_t rollingGrowth = deficit - pastDeficit;

    switch (m_state) {
    case State::Idle:
      if (rollingGrowth > m_threshold) {
        m_state = State::Pending;
        m_candidateStart = now;
        m_candidateBaseline = pastDeficit;
        m_candidateMinimumGrowth = rollingGrowth;
      }
      break;

    case State::Pending: {
      const int64_t residualGrowth = deficit - m_candidateBaseline;
      if (residualGrowth <= m_threshold) {
        // The callback queue caught up: no samples were lost.
        m_state = State::Idle;
        break;
      }

      m_candidateMinimumGrowth =
          std::min(m_candidateMinimumGrowth, residualGrowth);
      if (now - m_candidateStart >= RecoveryWindow) {
        m_state = State::Latched;
        return m_candidateMinimumGrowth;
      }
      break;
    }

    case State::Latched:
      // Wait until the pre-event value has left the rolling comparison
      // window. This makes a persistent step one event, not one event
      // per cooldown period.
      if (rollingGrowth <= m_threshold)
        m_state = State::Idle;
      break;
    }

    return std::nullopt;
  }

  void reset() { m_state = State::Idle; }

private:
  enum class State { Idle, Pending, Latched };

  const int64_t m_threshold;
  State m_state = State::Idle;
  Clock::time_point m_candidateStart{};
  int64_t m_candidateBaseline = 0;
  int64_t m_candidateMinimumGrowth = 0;
};
