/*
 * SPDX-License-Identifier: MIT
 *
 * How this launcher session ends (see settings_session_model.hpp).
 */

#include "settings_session_model.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <thread>

namespace launcher_session {
namespace {

std::atomic<bool> quit_flag{false};
std::atomic<WakeFn> wake_fn{nullptr};

} // namespace

bool is_desktop_session(const char *value) noexcept
{
    return value && std::strcmp(value, "desktop") == 0;
}

bool quit_to_desktop_shown(bool compiled_in, bool native_settings, const char *session_value) noexcept
{
    return compiled_in && native_settings && is_desktop_session(session_value);
}

void set_wake(WakeFn wake) noexcept
{
    wake_fn.store(wake, std::memory_order_release);
}

void request_quit(unsigned deadline_ms) noexcept
{
    if (quit_flag.exchange(true, std::memory_order_acq_rel)) return; /* already on its way out */
    std::fprintf(stderr, "[SESSION] quit requested: the launcher exits (exit status 0)\n");
    std::fflush(stderr);
    if (deadline_ms > 0) {
        try {
            /* the safety net: a teardown that hangs (a worker that does not join) must not keep the panel from the
             * desktop. Detached: when main() returns first, the process ends and this thread with it. */
            std::thread([deadline_ms] {
                std::this_thread::sleep_for(std::chrono::milliseconds(deadline_ms));
                std::fprintf(stderr, "[SESSION] the teardown did not finish within %u ms: exiting now\n", deadline_ms);
                std::fflush(nullptr);
                std::_Exit(0);
            }).detach();
        } catch (const std::exception &) {
            /* no thread: the orderly exit still runs, without the safety net */
        }
    }
    if (WakeFn wake = wake_fn.load(std::memory_order_acquire)) wake();
}

bool quit_requested() noexcept
{
    return quit_flag.load(std::memory_order_acquire);
}

} // namespace launcher_session
