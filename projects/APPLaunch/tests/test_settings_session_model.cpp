/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > System > Quit to desktop (desktop boards): who sees the entry, and the quit request with its deadline; the
 * restart request of Settings > Screen > PDA mode (exit status 76, report 043).
 */

#include "settings_session_model.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::atomic<int> wakes{0};
void count_wake()
{
    ++wakes;
}

} // namespace

int main()
{
    using namespace launcher_session;

    // the session marker of applaunch-desktop.service, exactly
    assert(is_desktop_session("desktop"));
    assert(!is_desktop_session(nullptr));
    assert(!is_desktop_session(""));
    assert(!is_desktop_session("Desktop"));
    assert(!is_desktop_session("desktop "));
    assert(!is_desktop_session("appliance"));

    // shown only when compiled in, on the native Settings, in the desktop unit's launcher
    assert(quit_to_desktop_shown(true, true, "desktop"));
    assert(!quit_to_desktop_shown(false, true, "desktop")); // default-profile build
    assert(!quit_to_desktop_shown(true, false, "desktop")); // legacy (compat) Settings
    assert(!quit_to_desktop_shown(true, true, nullptr));    // appliance boards: APPLaunch.service sets no session
    assert(!quit_to_desktop_shown(true, true, "appliance"));

    // the deadline, in a child whose flag is still clear: a teardown that hangs ends the process with exit status 0
    // (the unit then stops and the desktop comes back), after the deadline and not long after
    const auto start = std::chrono::steady_clock::now();
    const pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        request_quit(150);
        if (!quit_requested()) _exit(4);
        for (;;) pause(); /* the "teardown" that never ends */
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    assert(elapsed >= std::chrono::milliseconds(150));
    assert(elapsed < std::chrono::milliseconds(3000));

    // the restart (Settings > Screen > PDA mode, report 043): the same orderly end with exit status 76, on which both
    // units start the launcher again; its deadline exits with 76 too
    static_assert(kRestartExitStatus == 76, "the units' RestartForceExitStatus= and cyberdeckzero-console say 76");
    const auto restart_start = std::chrono::steady_clock::now();
    const pid_t restart_pid = fork();
    assert(restart_pid >= 0);
    if (restart_pid == 0) {
        if (quit_requested() || restart_requested()) _exit(3);
        request_restart(150);
        if (!quit_requested() || !restart_requested()) _exit(4);
        request_quit(0); // a quit after it changes nothing: still a restart
        if (!restart_requested()) _exit(5);
        for (;;) pause();
    }
    assert(waitpid(restart_pid, &status, 0) == restart_pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == kRestartExitStatus);
    assert(std::chrono::steady_clock::now() - restart_start >= std::chrono::milliseconds(150));
    // a restart asked after a quit is a quit (exit status 0)
    const pid_t quit_first_pid = fork();
    assert(quit_first_pid >= 0);
    if (quit_first_pid == 0) {
        request_quit(0);
        request_restart(0);
        _exit(restart_requested() ? 6 : quit_requested() ? 0 : 7);
    }
    assert(waitpid(quit_first_pid, &status, 0) == quit_first_pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    // a restart wakes the main loop once
    const pid_t wake_pid = fork();
    assert(wake_pid >= 0);
    if (wake_pid == 0) {
        set_wake(&count_wake);
        request_restart(0);
        request_restart(0);
        _exit(wakes == 1 && restart_requested() ? 0 : 8);
    }
    assert(waitpid(wake_pid, &status, 0) == wake_pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    // the quit request: clear at start, set once, wakes the main loop once, stays set
    assert(!quit_requested());
    assert(!restart_requested());
    set_wake(&count_wake);
    request_quit(0);
    assert(quit_requested());
    assert(wakes == 1);
    request_quit(0); // idempotent: no second wake
    assert(wakes == 1 && quit_requested());
    return 0;
}
