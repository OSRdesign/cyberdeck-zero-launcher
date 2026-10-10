/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_external_app_runner.hpp"

#include "cp0_esc_exit_policy.hpp"
#include "cp0_esc_key_watch.hpp"
#include "cp0_esc_state.h"
#include "cp0_keyboard_presence.h"
#include "keyboard_input.h"
#include "../cp0_external_process_group.hpp"
#include "cp0_process_commands.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#if !defined(_WIN32)
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

extern "C" void __attribute__((weak)) keyboard_pause(void) {}
extern "C" void __attribute__((weak)) keyboard_resume(void) {}
extern "C" void __attribute__((weak)) ui_external_esc_hint(int visible) { (void)visible; }

namespace cp0_external_app_runner {
namespace {

const char *keyboard_device()
{
    const char *configured = std::getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE");
    return configured ? configured : "/dev/input/by-path/platform-3f804000.i2c-event";
}

std::uint64_t monotonic_ms()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

int run(const char *command, bool keep_root)
{
#if defined(_WIN32)
    (void)command;
    (void)keep_root;
    return -1;
#else
    keyboard_pause();
    const bool subreaper = cp0_process_group::enable_subreaper();

    // The Esc-hold watcher reads the explicit keyboard device and, unless APPLAUNCH_KEYBOARD_SCAN=0, every real
    // keyboard the presence watcher lists (USB, Bluetooth), picking them up or dropping them while the app
    // runs. No EVIOCGRAB: the nodes stay shared with the child. A Bluetooth keyboard may be asleep (its node is
    // gone) when the app starts: that must not prevent the launch, it is attached when it wakes up.
    cp0_kbd_presence_watcher_t *presence =
        cp0_keyboard_get_read_all_keyboards() ? cp0_kbd_presence_shared_acquire() : nullptr;
    cp0_kbd_presence_list_t presence_list;
    presence_list.count = 0;
    unsigned seen_generation = cp0_kbd_presence_watcher_generation(presence);
    cp0_esc_key_watch::Sources esc_sources;
    const auto attach_keyboards = [&]() {
        esc_sources.open(keyboard_device());
        if (presence) {
            seen_generation = cp0_kbd_presence_watcher_snapshot(presence, &presence_list);
            for (unsigned i = 0; i < presence_list.count; ++i) esc_sources.open(presence_list.devices[i].path);
        }
    };
    attach_keyboards();
    if (esc_sources.count() == 0)
        std::printf("[cp0] no keyboard available yet (%s): starting the app anyway\n", keyboard_device());
    std::fflush(stdout);
    std::uint64_t next_reopen_ms = monotonic_ms() + 500;
    const auto release_keyboards = [&]() {
        esc_sources.close_all();
        if (presence) cp0_kbd_presence_shared_release();
        presence = nullptr;
    };

    const pid_t pid = fork();
    if (pid < 0) {
        release_keyboards();
        keyboard_resume();
        return -1;
    }
    if (pid == 0) {
        esc_sources.close_all(); // O_CLOEXEC too; the presence thread does not exist in the child
        setpgid(0, 0);
        if (keep_root)
            execlp("/bin/sh", "sh", "-c", command, static_cast<char *>(nullptr));
        else
            cp0_process_commands::exec_shell_as_configured_user(command);
        _exit(127);
    }

    setpgid(pid, pid);
    std::fprintf(stderr,
                 "[process] external app leader=%d pgid=%d subreaper=%d\n",
                 static_cast<int>(pid),
                 static_cast<int>(pid),
                 subreaper ? 1 : 0);

    cp0_esc_exit_policy::StateMachine esc_policy;
    bool leader_reaped = false;
    int status = 0;

    while (true) {
        cp0_process_group::reap_available(pid, pid, status, leader_reaped);
        if (!cp0_process_group::exists(pid)) break;

        // New keyboards as soon as the presence list changes; every 500 ms the ones not open yet (a sleeping
        // Bluetooth keyboard, udev permissions not set yet).
        if (cp0_kbd_presence_watcher_generation(presence) != seen_generation || monotonic_ms() >= next_reopen_ms) {
            next_reopen_ms = monotonic_ms() + 500;
            attach_keyboards();
        }
        // Esc is held while any keyboard holds it; a keyboard that slept or was unplugged releases its Esc.
        const int esc_change = esc_sources.poll();
        if (esc_change >= 0) cp0_esc_state_write(esc_change);

        const bool esc_now = cp0_esc_state_read() != 0;
        const auto decision = esc_policy.update(monotonic_ms(), esc_now);
        if (decision.show_hint) ui_external_esc_hint(1);
        if (decision.hide_hint) ui_external_esc_hint(0);
        if (decision.send_terminate) {
            std::fprintf(stderr, "[process] ESC timeout: SIGTERM pgid=%d\n", static_cast<int>(pid));
            killpg(pid, SIGTERM);
        }
        if (decision.send_kill) {
            std::fprintf(stderr, "[process] grace timeout: SIGKILL pgid=%d\n", static_cast<int>(pid));
            killpg(pid, SIGKILL);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (esc_policy.finish().hide_hint) ui_external_esc_hint(0);
    cp0_process_group::reap_available(pid, pid, status, leader_reaped);
    std::fprintf(stderr,
                 "[process] external app group drained pgid=%d leader_reaped=%d\n",
                 static_cast<int>(pid),
                 leader_reaped ? 1 : 0);
    release_keyboards();
    keyboard_resume();
    cp0_esc_state_reset();
    std::printf("[cp0] Returned to launcher\n");
    std::fflush(stdout);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

} // namespace cp0_external_app_runner
