/*
 * SPDX-License-Identifier: MIT
 *
 * The supervisor's notices on the launcher's toast (task 015, deck findings 2026-10-20): a newer notice must not erase
 * an older one before it could be read. Each notice stays at least `min_ms` (2 s); the next one waits for that; with
 * nothing waiting a notice goes away after `show_ms`. While the toast cannot be used (an app is in front, the
 * Alt+Tab text is up) nothing is shown, and a notice that was interrupted before its minimum is shown again later.
 * Pure: no clock, no LVGL. Unit-tested by tests/test_notice_queue.cpp.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace multitask {

class NoticeQueue {
public:
    struct Config {
        std::uint32_t min_ms = 2000;   /* a notice is never replaced or hidden earlier while the toast is free */
        std::uint32_t show_ms = 3000;  /* ... and goes away after this when nothing waits */
        std::size_t max_waiting = 4;   /* more: the oldest waiting one is dropped */
    };
    struct Step {
        enum class Kind : std::uint8_t { None, Show, Hide } kind = Kind::None;
        std::string text;
    };

    NoticeQueue() : NoticeQueue(Config()) {}
    explicit NoticeQueue(Config config) : config_(config) {}

    /* A new notice (any time). The same text as the last one queued or shown is not repeated. */
    void push(std::string text);
    /* What the toast must do now. can_show: the launcher is in front and nothing else uses the toast. */
    Step tick(std::uint64_t now_ms, bool can_show);
    /* Something is shown or waiting: the caller ticks often (100 ms) while this is true. */
    bool active() const { return showing_ || !waiting_.empty(); }
    std::size_t waiting() const { return waiting_.size(); }

private:
    Config config_;
    std::deque<std::string> waiting_;
    bool showing_ = false;
    std::string current_;
    std::uint64_t shown_at_ = 0;
};

} // namespace multitask
