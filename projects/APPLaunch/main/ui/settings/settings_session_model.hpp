/*
 * SPDX-License-Identifier: MIT
 *
 * How this launcher session ends (desktop boards, pizero2w/README.md "Desktop boards").
 *
 * On a board that boots to a desktop, the launcher runs in the system unit applaunch-desktop.service, started from the
 * applications menu: starting it stops the desktop, and when the launcher exits (any exit code) the unit's
 * ExecStopPost= gives the desktop back, unless the system is shutting down. That unit sets APPLAUNCH_SESSION=desktop.
 * Settings > System > Quit to desktop is offered only then (and only in a build whose hardware profile has it, on the
 * native Settings): on an appliance board (the user service APPLaunch.service) a quit would leave the panel dark.
 *
 * The quit is the runner's orderly exit: request_quit() makes the main loop's should_quit() true (main.cpp), so
 * cp0_lvgl_run() tears the launcher down and main() returns 0. A teardown that does not finish within the deadline
 * ends the process with exit status 0 anyway: the unit stops and the desktop comes back in every case.
 *
 * A restart (Settings > Screen > PDA mode, report 043: the canvas is chosen when the launcher starts) is the same
 * orderly exit with exit status kRestartExitStatus (76; 75 is the hold-Esc watchdog's): both units start the launcher
 * again on it (RestartForceExitStatus=76; the desktop unit's ExecStopPost= then leaves the desktop stopped), and the
 * new launcher reads the stored choice. Open apps end with the old launcher (the unit's processes are stopped).
 *
 * Pure logic and the process-wide quit flag only (no LVGL, no settings tree): unit tested on the PC.
 */
#pragma once

namespace launcher_session {

/* APPLAUNCH_SESSION's value names a desktop session: exactly "desktop" (case sensitive; null or anything else: no). */
bool is_desktop_session(const char *value) noexcept;

/* Settings > System > Quit to desktop is shown: compiled in (APPLAUNCH_SETTINGS_QUIT_TO_DESKTOP), the native Settings
 * host runs, and the launcher was started by the desktop unit (APPLAUNCH_SESSION=desktop). */
bool quit_to_desktop_shown(bool compiled_in, bool native_settings, const char *session_value) noexcept;

/* Called by request_quit() to wake the main loop (main.cpp passes cp0_lvgl_wake); null = no wake. */
using WakeFn = void (*)();
void set_wake(WakeFn wake) noexcept;

/* Milliseconds a quit may take before the process ends with exit status 0 regardless. */
inline constexpr unsigned kQuitDeadlineMs = 5000;

/* Ask the main loop to end (any thread; idempotent: the first call counts). deadline_ms 0 = no deadline. */
void request_quit(unsigned deadline_ms = kQuitDeadlineMs) noexcept;

/* The main loop's should_quit(). */
bool quit_requested() noexcept;

/* The exit status that makes the launcher's units start it again (cyberdeckzero.service, cyberdeckzero-desktop.service:
 * RestartForceExitStatus=; cyberdeckzero-console restore-desktop leaves the desktop stopped on it). */
inline constexpr int kRestartExitStatus = 76;

/* request_quit() that ends with kRestartExitStatus (the deadline's exit too). Idempotent: the first request counts
 * (a restart asked after a quit is a quit). */
void request_restart(unsigned deadline_ms = kQuitDeadlineMs) noexcept;

/* A restart was requested: main() returns kRestartExitStatus. */
bool restart_requested() noexcept;

} // namespace launcher_session
