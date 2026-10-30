/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's SSH indicator (owner decisions 2026-10-30): the Settings choice and the status feed's last sample.
 *
 *   Settings > System > SSH indicator: Off / On (the default); config key "ssh_indicator" (0 Off, 1 On), absent = On,
 *   written only when the user changes it and then with SetManyAndSave (one transaction: when the config store is
 *   full, 64 keys, report 046 section 4.1, the write is refused and the page says so instead of a silent drop).
 *
 *   The status feed's thread (status_feed.cpp) owns the one cp0_ssh_watch (ssh_indicator::Sampler, every 2 s); it
 *   samples only while enabled() and publishes each sample here and as status.state ssh= / ssh_act= for the apps. The
 *   launcher's own strip (native_ui::fill_status) reads snapshot(): the count and the activity flag only, never a peer
 *   address.
 *
 * mode(), set_mode(), install(): LVGL thread. enabled(), publish(), snapshot(): any thread.
 */
#pragma once

#include "model/ssh_indicator_model.hpp"

namespace launcher_ssh {

/* Settings > SSH indicator: ssh_indicator::kOff or kOn (read once from the config, then cached). */
int mode();
/* Persist the choice (nothing is written when it is the current one) and apply it. False when it could not be saved
 * (the cached value stays). */
bool set_mode(int mode);

/* Hand the stored choice to the feed thread once (later calls do nothing). Until then the feed does not sample. */
void install();

/* install() was called and the choice is On. */
bool enabled();

/* The feed thread's last sample ({0, 0} until the first one). */
void publish(const ssh_indicator::Snapshot &sample);
ssh_indicator::Snapshot snapshot();

} // namespace launcher_ssh
