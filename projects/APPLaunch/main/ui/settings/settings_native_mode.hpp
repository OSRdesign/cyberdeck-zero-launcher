/*
 * SPDX-License-Identifier: MIT
 *
 * Which Settings renderer runs (task 014 P2a, decisions D2 and D3).
 *
 *   APPLAUNCH_SETTINGS_UI=native   the native Settings host (full screen on the native display, touch native,
 *                                  roller look at every screen size; settings_native_host.hpp)
 *   APPLAUNCH_SETTINGS_UI=compat   today's Settings in the 320x170 compat window with the toolbar (the default,
 *   or unset                       on every board, until P3e makes the native one the deck's default)
 *
 * The variable comes from the launcher's environment (board.conf / the service unit). Native needs the dpi-scaled
 * display backend; without it (SDL simulator) the compat renderer runs whatever the variable says.
 *
 * D3: a page the native host has no widget for yet (the manual time "Save?" step; the info pages, Wi-Fi, the Bluetooth
 * pages and Apps are native since task 014 P3a / P3b / P3c / P3d) opens through the compat path from the native host
 * in development builds only. In a release build those entries are hidden while native is selected; native pages show
 * in both. Development = APPLAUNCH_DEV=1 in the environment, or a build whose channel (APPLAUNCH_CHANNEL at build
 * time, LAUNCHER_CHANNEL_RAW) is "development" (the default of a local scons build); APPLAUNCH_DEV=0 forces the
 * release behaviour.
 */
#pragma once

namespace settings_ui {

/* APPLAUNCH_SETTINGS_UI=native and the native display is available. */
bool native_selected();

/* Unmigrated pages may open through the compat path (development build or APPLAUNCH_DEV=1). */
bool dev_pages();

} // namespace settings_ui
