/*
 * SPDX-License-Identifier: MIT
 *
 * The `screen off` / `screen on` state (see screen_gate.hpp).
 */
#include "screen_gate.hpp"

namespace app_request {

ScreenGate::Result ScreenGate::off(int pid)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (off_.load()) return owner_ == pid ? Result::Ok : Result::Busy;
    off_.store(true); /* before the write: nothing else may raise the light in between */
    const int raw = hw_.suspend();
    if (raw <= 0) {
        off_.store(false);
        return Result::Failed;
    }
    owner_ = pid;
    raw_ = raw;
    return Result::Ok;
}

ScreenGate::Result ScreenGate::on(int pid)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!off_.load()) return Result::Ok;
    if (owner_ != pid) return Result::Busy;
    restore_locked();
    return Result::Ok;
}

bool ScreenGate::release()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!off_.load()) return false;
    restore_locked();
    return true;
}

bool ScreenGate::poll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!off_.load() || hw_.alive(owner_)) return false;
    restore_locked();
    return true;
}

int ScreenGate::owner() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return off_.load() ? owner_ : 0;
}

void ScreenGate::restore_locked()
{
    const int raw = raw_;
    owner_ = 0;
    raw_ = -1;
    hw_.restore(raw);
    off_.store(false);
}

} // namespace app_request
