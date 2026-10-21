/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > System > Quit to desktop (desktop boards): who sees the entry, and the quit request with its deadline.
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

    // the quit request: clear at start, set once, wakes the main loop once, stays set
    assert(!quit_requested());
    set_wake(&count_wake);
    request_quit(0);
    assert(quit_requested());
    assert(wakes == 1);
    request_quit(0); // idempotent: no second wake
    assert(wakes == 1 && quit_requested());
    return 0;
}
