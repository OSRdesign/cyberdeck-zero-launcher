/*
 * SPDX-License-Identifier: MIT
 *
 * Which Settings renderer runs (task 014 P2a, decisions D2 and D3; the default flipped in P3e).
 *
 * The native Settings host (full screen on the native display, touch native, roller look at every screen size;
 * settings_native_host.hpp) runs whenever the dpi-scaled display backend exists (native_ui::enabled(): the Pi boards).
 * There is no switch and no compat escape hatch. Without that backend (the SDL simulator) the legacy roller pages in the 320x170 window are the only Settings.
 *
 * Every page of the Pi boards' tree is native. A page the native host has no widget for (a legacy "Custom" page of
 * another board: Speaker, Ethernet, Battery, ...) is hidden. Development-only pages (System > Keyboard test) show only
 * in a development build: APPLAUNCH_DEV=1 in the environment, or a build whose channel (APPLAUNCH_CHANNEL at build
 * time, LAUNCHER_CHANNEL_RAW) is "development" (the default of a local scons build); APPLAUNCH_DEV=0 forces the
 * release behaviour. Bundles built by pizero2w/build.sh are release builds unless the caller sets the channel.
 */
#pragma once

namespace settings_ui {

/* The native display (dpi-scaled backend) is available: the native Settings host runs. */
bool native_selected();

/* Development-only pages show (development build or APPLAUNCH_DEV=1). */
bool dev_pages();

} // namespace settings_ui
