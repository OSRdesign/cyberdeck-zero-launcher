/*
 * SPDX-License-Identifier: MIT
 *
 * Detects that the machine slept (system suspend) from two clocks: CLOCK_BOOTTIME counts the time spent suspended,
 * CLOCK_MONOTONIC does not, so (boottime - monotonic) only grows when the machine sleeps. Wall-clock steps (NTP,
 * RTC, timezone) move neither of them. The caller samples both in milliseconds; this class holds no clock, so it is
 * testable with injected values.
 */
#pragma once

#include <cstdint>

class ResumeDetector {
public:
    /* A jump of the offset smaller than this is read jitter between the two clock reads, not a sleep. */
    static constexpr int64_t kThresholdMs = 2000;

    /* Feed one sample. Returns the milliseconds slept since the previous sample when that is at least kThresholdMs,
     * else 0. The first sample only sets the baseline. A backwards step of the offset re-bases without a report. */
    int64_t sample(int64_t boottime_ms, int64_t monotonic_ms)
    {
        const int64_t offset = boottime_ms - monotonic_ms;
        const bool had_baseline = has_baseline_;
        const int64_t slept = offset - offset_ms_;
        offset_ms_ = offset;
        has_baseline_ = true;
        return had_baseline && slept >= kThresholdMs ? slept : 0;
    }

private:
    bool has_baseline_ = false;
    int64_t offset_ms_ = 0;
};
