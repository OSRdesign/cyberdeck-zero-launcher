/*
 * Hardware profile for the Settings menu and the built-in app list.
 *
 * Entries that do not apply to a platform stay in the source and are compiled out
 * here instead of being deleted. Set a macro to 1 (or define it on the command line)
 * to bring an entry back, e.g. for another platform.
 *
 * Build with APPLAUNCH_HW=pizero2w in the environment to select the Raspberry Pi
 * Zero 2W profile (see main/SConstruct); the default is the stock CardputerZero.
 */
#pragma once

#if defined(APPLAUNCH_HW_PIZERO2W)

// Raspberry Pi Zero 2W cyberdeck: no speaker, no wired Ethernet, no M5IOE1 expander,
// no battery gauge, no ADB gadget mode, and software updates come from elsewhere.
#ifndef APPLAUNCH_SETTINGS_SPEAKER
#define APPLAUNCH_SETTINGS_SPEAKER 0
#endif
#ifndef APPLAUNCH_SETTINGS_ETHERNET
#define APPLAUNCH_SETTINGS_ETHERNET 0
#endif
#ifndef APPLAUNCH_SETTINGS_EXTPORT
#define APPLAUNCH_SETTINGS_EXTPORT 0
#endif
#ifndef APPLAUNCH_SETTINGS_BATTERY
#define APPLAUNCH_SETTINGS_BATTERY 0
#endif
#ifndef APPLAUNCH_SETTINGS_DEVELOPER
#define APPLAUNCH_SETTINGS_DEVELOPER 0
#endif
#ifndef APPLAUNCH_SETTINGS_SOFTWARE_UPDATE
#define APPLAUNCH_SETTINGS_SOFTWARE_UPDATE 0
#endif
#ifndef APPLAUNCH_SETTINGS_SHUTDOWN
#define APPLAUNCH_SETTINGS_SHUTDOWN 1
#endif
// No hardware RTC and a PolicyKit rule for timedate1: set network time / the clock over D-Bus
// (no sudo password, no `hwclock -w`). A manual time lasts until the next boot.
#ifndef APPLAUNCH_SETTINGS_TIME_NO_SUDO
#define APPLAUNCH_SETTINGS_TIME_NO_SUDO 1
#endif
// The stock Calculator is an external program that draws to a 320x170 framebuffer and is not
// installed here: use the built-in native touch calculator page instead.
#ifndef APPLAUNCH_APPS_NATIVE_CALCULATOR
#define APPLAUNCH_APPS_NATIVE_CALCULATOR 1
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

#else // stock CardputerZero

#ifndef APPLAUNCH_SETTINGS_SPEAKER
#define APPLAUNCH_SETTINGS_SPEAKER 1
#endif
#ifndef APPLAUNCH_SETTINGS_ETHERNET
#define APPLAUNCH_SETTINGS_ETHERNET 1
#endif
#ifndef APPLAUNCH_SETTINGS_EXTPORT
#define APPLAUNCH_SETTINGS_EXTPORT 1
#endif
#ifndef APPLAUNCH_SETTINGS_BATTERY
#define APPLAUNCH_SETTINGS_BATTERY 1
#endif
#ifndef APPLAUNCH_SETTINGS_DEVELOPER
#define APPLAUNCH_SETTINGS_DEVELOPER 1
#endif
#ifndef APPLAUNCH_SETTINGS_SOFTWARE_UPDATE
#define APPLAUNCH_SETTINGS_SOFTWARE_UPDATE 1
#endif
#ifndef APPLAUNCH_SETTINGS_SHUTDOWN
#define APPLAUNCH_SETTINGS_SHUTDOWN 0 // the stock menu only offers Reboot
#endif
#ifndef APPLAUNCH_SETTINGS_TIME_NO_SUDO
#define APPLAUNCH_SETTINGS_TIME_NO_SUDO 0 // stock: sudo coordinator + `hwclock -w`
#endif
#ifndef APPLAUNCH_APPS_NATIVE_CALCULATOR
#define APPLAUNCH_APPS_NATIVE_CALCULATOR 0 // stock: external CardputerZero-Calculator
#endif
#ifndef APPLAUNCH_HELP_PLAIN_H
#define APPLAUNCH_HELP_PLAIN_H 0 // stock: Fn+H is a hardware key code on the Cardputer keyboard
#endif
#ifndef APPLAUNCH_SETTINGS_TOUCH
#define APPLAUNCH_SETTINGS_TOUCH 0 // stock: no touch panel
#endif
#ifndef APPLAUNCH_SETTINGS_APPS
#define APPLAUNCH_SETTINGS_APPS 0 // stock: apps are managed in the Store
#endif
#define APPLAUNCH_TXT_HELP_HINT "Fn+H:Help"
#define APPLAUNCH_TXT_HELP_CLOSE "Fn+H: close"
#define APPLAUNCH_TXT_SSH_HELP_HINT "KEY_HELP:Help"

// User-facing wording for Date & Time (stock CardputerZero: hardware RTC).
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
