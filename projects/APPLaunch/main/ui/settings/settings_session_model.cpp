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

/* how the session ends: 0 running, kQuit, kRestart (the first request counts) */
constexpr int kQuit = 1;
constexpr int kRestart = 2;
std::atomic<int> end_state{0};
std::atomic<WakeFn> wake_fn{nullptr};

/* the orderly end of the main loop, then the exit status (0: quit, kRestartExitStatus: restart) */
void request_end(unsigned deadline_ms, int how) noexcept
{
    int expected = 0;
    if (!end_state.compare_exchange_strong(expected, how, std::memory_order_acq_rel)) return; /* on its way out */
    const int exit_status = how == kRestart ? kRestartExitStatus : 0;
    if (how == kRestart)
        std::fprintf(stderr, "[SESSION] restart requested: the launcher exits (exit status %d) and its unit starts it "
                             "again\n", exit_status);
    else
        std::fprintf(stderr, "[SESSION] quit requested: the launcher exits (exit status 0)\n");
    std::fflush(stderr);
    if (deadline_ms > 0) {
        try {
            /* the safety net: a teardown that hangs (a worker that does not join) must not keep the panel from the
             * desktop, nor the launcher from starting again. Detached: when main() returns first, the process ends
             * and this thread with it. */
            std::thread([deadline_ms, exit_status] {
                std::this_thread::sleep_for(std::chrono::milliseconds(deadline_ms));
                std::fprintf(stderr, "[SESSION] the teardown did not finish within %u ms: exiting now\n", deadline_ms);
                std::fflush(nullptr);
                std::_Exit(exit_status);
            }).detach();
        } catch (const std::exception &) {
            /* no thread: the orderly exit still runs, without the safety net */
        }
    }
    if (WakeFn wake = wake_fn.load(std::memory_order_acquire)) wake();
}

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
    request_end(deadline_ms, kQuit);
}

void request_restart(unsigned deadline_ms) noexcept
{
    request_end(deadline_ms, kRestart);
}

bool quit_requested() noexcept
{
    return end_state.load(std::memory_order_acquire) != 0;
}

bool restart_requested() noexcept
{
    return end_state.load(std::memory_order_acquire) == kRestart;
}

} // namespace launcher_session
