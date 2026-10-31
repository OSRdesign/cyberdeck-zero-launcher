/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates outside Settings (report 047 P1):
 *
 *   D11  at the launcher's first home screen: after a version change, one toast "Updated to <version>" pointing at
 *        Settings > Updates, where What's new shows the notes (the skipped versions' from the cached answer, else the
 *        notes the bundle installs). A first start (no state file) and a downgrade show nothing.
 *   D3   once a day in the background (2 min after the start, then every 10 min the timer looks whether a check is due):
 *        only with a default route, the clock synchronised (the check's own rule), the home grid in front (never over
 *        an app or the lock screen); a newer release found is announced by one toast per version. No badge on the
 *        home grid. The check reads GitHub only: nothing is downloaded or installed.
 *
 *   P2   the root update helper's status file (/var/lib/cyberdeckzero-update/status), every 2 s on the same timer: on a
 *        desktop board the helper asks the running launcher to exit with status 76 once the new files are in place
 *        (restart=launcher naming this version and this pid: its unit starts the new launcher without giving the
 *        desktop back; report 047 5.6 step 8b). After a rollback, one toast "Update to 0.8.13 failed / Rolled back to
 *        0.8.12" (once per helper run, update.state rollback_seen). After an update done to this version, its
 *        downloaded bundle is deleted.
 *
 * install() once, from native_ui::show_home() once the home grid is built (the bookkeeping, the timers);
 * show_start_notice() at the end of show_home() (the toast, once). LVGL thread.
 */
#pragma once

namespace launcher_update {

void install();
void show_start_notice();

} // namespace launcher_update
