/*
 * Hardware profile for the Settings menu and the built-in app list.
 *
 * Entries that do not apply to a platform stay in the source and are compiled out
 * here instead of being deleted. Set a macro to 1 (or define it on the command line)
 * to bring an entry back, e.g. for another platform.
 *
 * Build with APPLAUNCH_HW=pizero2w in the environment to select the profile of our boards
 * (Raspberry Pi boards and PCs; see main/SConstruct, every bundle sets it). Without it the
 * default profile applies (the SDL simulator): the menu the code was inherited with.
 */
#pragma once

#if defined(APPLAUNCH_HW_PIZERO2W)

// Raspberry Pi Zero 2W cyberdeck (and the other boards of this port). Speaker, Battery and Screen > Brightness are not
// switched here: they follow what the board has, detected each time Settings opens (report 030, decision C1:
// cp0_capabilities.h, settings_capabilities.hpp). No wired Ethernet row (C10: later).
#ifndef APPLAUNCH_SETTINGS_ETHERNET
#define APPLAUNCH_SETTINGS_ETHERNET 0
#endif
// The launcher's own interface sounds (startup, home switch / enter, the legacy lock screen) stay off on our boards
// (decision C12: silent today, a board that gains sound must not start beeping). The volume page's preview still
// plays: it answers the user's own volume change.
#ifndef APPLAUNCH_SYSTEM_SOUNDS
#define APPLAUNCH_SYSTEM_SOUNDS 0
#endif
#ifndef APPLAUNCH_SETTINGS_SHUTDOWN
#define APPLAUNCH_SETTINGS_SHUTDOWN 1
#endif
// No hardware RTC and a PolicyKit rule for timedate1: set network time / the clock over D-Bus
// (no sudo password, no `hwclock -w`). A manual time lasts until the next boot.
#ifndef APPLAUNCH_SETTINGS_TIME_NO_SUDO
#define APPLAUNCH_SETTINGS_TIME_NO_SUDO 1
#endif
// A standard keyboard has no Fn key: pages without text entry open their help with plain H, and the
// SSH page (typing fields) uses F1.
#ifndef APPLAUNCH_HELP_PLAIN_H
#define APPLAUNCH_HELP_PLAIN_H 1
#endif
// Touch panel: Settings > Touch picks the swipe/tap behaviour per app.
#ifndef APPLAUNCH_SETTINGS_TOUCH
#define APPLAUNCH_SETTINGS_TOUCH 1
#endif
// Settings > Apps: install / remove apps from GitHub-hosted sources (needs the Store's backend).
#ifndef APPLAUNCH_SETTINGS_APPS
#define APPLAUNCH_SETTINGS_APPS 1
#endif
// Settings > System > Quit to desktop: shown at run time only to a launcher started by applaunch-desktop.service on a
// desktop board (APPLAUNCH_SESSION=desktop, settings_session_model.hpp); the appliance boards never see it.
#ifndef APPLAUNCH_SETTINGS_QUIT_TO_DESKTOP
#define APPLAUNCH_SETTINGS_QUIT_TO_DESKTOP 1
#endif
#define APPLAUNCH_TXT_HELP_HINT "H:Help"
#define APPLAUNCH_TXT_HELP_CLOSE "H: close"
#define APPLAUNCH_TXT_SSH_HELP_HINT "F1:Help"


// User-facing wording for Date & Time: a board without a hardware RTC just "saves" the time.
#define APPLAUNCH_TXT_RTC_WRITE_PROMPT "Save?"
#define APPLAUNCH_TXT_RTC_INVALID_VALUE "Invalid time value"
#define APPLAUNCH_TXT_RTC_READING_STATUS "Reading time status"
#define APPLAUNCH_TXT_RTC_OP_PENDING "Time operation is pending"
#define APPLAUNCH_TXT_RTC_REFRESH_UNAVAILABLE "Time refresh unavailable"
#define APPLAUNCH_TXT_RTC_TIME_UNAVAILABLE "Time unavailable"
#define APPLAUNCH_TXT_RTC_READ_STATUS_FAILED "Unable to read time status"
#define APPLAUNCH_TXT_RTC_OP_FAILED "Time operation failed"
#define APPLAUNCH_TXT_RTC_WRITE_IN_PROGRESS "Saving the time"
#define APPLAUNCH_TXT_RTC_DISABLE_NTP_FIRST "Turn off Network Time first"
#define APPLAUNCH_TXT_RTC_WRITE_ALREADY_PENDING "A save is already in progress"
#define APPLAUNCH_TXT_RTC_WRITE_START_FAILED "Unable to start the save"
#define APPLAUNCH_TXT_RTC_AUTH_FAILED "Authentication failed"
#define APPLAUNCH_TXT_RTC_WRITE_CANCELLED "Save cancelled"
#define APPLAUNCH_TXT_RTC_WRITE_TIMED_OUT "Save timed out"
#define APPLAUNCH_TXT_RTC_WRITE_FAILED "Could not save the time"
#define APPLAUNCH_TXT_RTC_OP_IN_PROGRESS "Time operation in progress"

#else // default profile (no APPLAUNCH_HW)

#ifndef APPLAUNCH_SETTINGS_SPEAKER
#define APPLAUNCH_SETTINGS_SPEAKER 1
#endif
#ifndef APPLAUNCH_SETTINGS_ETHERNET
#define APPLAUNCH_SETTINGS_ETHERNET 1
#endif
#ifndef APPLAUNCH_SETTINGS_BATTERY
#define APPLAUNCH_SETTINGS_BATTERY 1
#endif
#ifndef APPLAUNCH_SYSTEM_SOUNDS
#define APPLAUNCH_SYSTEM_SOUNDS 1 // default: the startup, switch and lock-screen sounds play
#endif
#ifndef APPLAUNCH_SETTINGS_SHUTDOWN
#define APPLAUNCH_SETTINGS_SHUTDOWN 0 // default: Reboot only
#endif
#ifndef APPLAUNCH_SETTINGS_TIME_NO_SUDO
#define APPLAUNCH_SETTINGS_TIME_NO_SUDO 0 // default: sudo coordinator + `hwclock -w`
#endif
#ifndef APPLAUNCH_HELP_PLAIN_H
#define APPLAUNCH_HELP_PLAIN_H 0 // default: Fn+H (a key code of the legacy matrix keyboard)
#endif
#ifndef APPLAUNCH_SETTINGS_TOUCH
#define APPLAUNCH_SETTINGS_TOUCH 0 // default: no touch panel
#endif
#ifndef APPLAUNCH_SETTINGS_APPS
#define APPLAUNCH_SETTINGS_APPS 0 // default: apps are managed in the Store
#endif
#ifndef APPLAUNCH_SETTINGS_QUIT_TO_DESKTOP
#define APPLAUNCH_SETTINGS_QUIT_TO_DESKTOP 0 // default: the launcher is the device's only screen
#endif
#define APPLAUNCH_TXT_HELP_HINT "Fn+H:Help"
#define APPLAUNCH_TXT_HELP_CLOSE "Fn+H: close"
#define APPLAUNCH_TXT_SSH_HELP_HINT "KEY_HELP:Help"

// User-facing wording for Date & Time (default profile: a hardware RTC).
#define APPLAUNCH_TXT_RTC_WRITE_PROMPT "Write RTC?"
#define APPLAUNCH_TXT_RTC_INVALID_VALUE "Invalid RTC value"
#define APPLAUNCH_TXT_RTC_READING_STATUS "Reading RTC status"
#define APPLAUNCH_TXT_RTC_OP_PENDING "RTC operation is pending"
#define APPLAUNCH_TXT_RTC_REFRESH_UNAVAILABLE "RTC refresh unavailable"
#define APPLAUNCH_TXT_RTC_TIME_UNAVAILABLE "RTC time unavailable"
#define APPLAUNCH_TXT_RTC_READ_STATUS_FAILED "Unable to read RTC status"
#define APPLAUNCH_TXT_RTC_OP_FAILED "RTC operation failed"
#define APPLAUNCH_TXT_RTC_WRITE_IN_PROGRESS "RTC write in progress"
#define APPLAUNCH_TXT_RTC_DISABLE_NTP_FIRST "Disable NTP before writing RTC"
#define APPLAUNCH_TXT_RTC_WRITE_ALREADY_PENDING "RTC write is already pending"
#define APPLAUNCH_TXT_RTC_WRITE_START_FAILED "Unable to start RTC write"
#define APPLAUNCH_TXT_RTC_AUTH_FAILED "RTC authentication failed"
#define APPLAUNCH_TXT_RTC_WRITE_CANCELLED "RTC write cancelled"
#define APPLAUNCH_TXT_RTC_WRITE_TIMED_OUT "RTC write timed out"
#define APPLAUNCH_TXT_RTC_WRITE_FAILED "RTC write failed"
#define APPLAUNCH_TXT_RTC_OP_IN_PROGRESS "RTC operation in progress"

#endif
