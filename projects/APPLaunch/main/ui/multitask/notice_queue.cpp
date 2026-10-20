/*
 * SPDX-License-Identifier: MIT
 *
 * Notices on the toast, one at a time, each readable (see notice_queue.hpp).
 */
#include "notice_queue.hpp"

namespace multitask {

void NoticeQueue::push(std::string text)
{
    if (text.empty()) return;
    if (!waiting_.empty() ? waiting_.back() == text : (showing_ && current_ == text)) return;
    if (waiting_.size() >= config_.max_waiting) waiting_.pop_front();
    waiting_.push_back(std::move(text));
}

NoticeQueue::Step NoticeQueue::tick(std::uint64_t now_ms, bool can_show)
{
    Step step;
    if (!can_show) {
        if (!showing_) return step;
        /* covered before it could be read: shown again when the toast is free */
        if (now_ms - shown_at_ < config_.min_ms) waiting_.push_front(current_);
        showing_ = false;
        current_.clear();
        step.kind = Step::Kind::Hide;
        return step;
    }
    if (showing_) {
        const std::uint64_t age = now_ms - shown_at_;
        if (age < config_.min_ms) return step;
        if (waiting_.empty()) {
            if (age < config_.show_ms) return step;
            showing_ = false;
            current_.clear();
            step.kind = Step::Kind::Hide;
            return step;
        }
    }
    if (waiting_.empty()) return step;
    current_ = waiting_.front();
    waiting_.pop_front();
    showing_ = true;
    shown_at_ = now_ms;
    step.kind = Step::Kind::Show;
    step.text = current_;
    return step;
}

} // namespace multitask
