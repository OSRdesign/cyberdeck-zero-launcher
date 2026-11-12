# 047 - Updates: the launcher updates itself (with release notes), and system packages (apt) with protection

Status: study written 2026-10-30 from the owner's request "offer the launcher an update feature where it can check on
the repo if a newer version is available, propose to update, show release notes. The update section of the launcher
also looks for apt update and manages, upon user request, apt upgrade, while taking care not to update packages that
could break the launcher's dependencies." Launcher read at `main` bcfccdb7 (release v0.8.12). Reading only: no product
code changed, nothing committed, no board touched (another worker was installing v0.8.12 on the boards). Report 045 is
the CloudpilotEmu study (apps repo tag `archive/cloudpilot-study`), 046 the home grid order, so this is 047.

Outcome (2026-11-12): implemented. P1 and P2 (check, release notes, helper with backup and rollback) in v0.8.13, hardened in v0.9.0; P3 and P4 (apt Packages page, anchor package cyberdeckzero-runtime, upgrade with cancel) in v0.9.2; P5 docs done (README, Pi README, release checklist). Known limits at the time: the apt upgrade was run on a Pi 4B only; self-update works on the release channel only.

Also read, outside the tree: the v0.8.12 GitHub release (`gh release view`, read-only), the local release bundle
`C:\CLAUDE\zero7\rel0812-out\cyberdeckzero-launcher-0.8.12-arm64.tar.gz` (ELF dependencies of its binaries), and the
upstream updater that report 029 removed (`git show b9967208^:...`).

Markers: **S** = read in the source, the docs or the release (file:line); **I** = inferred, needs a spike or a board.
Cost unit: one worker session (ws). Paths without a folder are in `projects/APPLaunch/main/ui/settings/`.

## 1. Recommendation

**Option C, built in phases, with the system part limited at first: ship the launcher update (check, notes, install with
automatic rollback) and a read-only list of system updates first (A + B), then system upgrades of a "safe set" only.
The boot chain (kernel, firmware, bootloader) is never upgraded from the launcher.**

Five design choices carry the whole feature:

1. **The check runs as the user, with tools the board already has.** `curl` (installed by `install.sh` on every board)
   asks the GitHub API for the releases; the bodies are the release notes. No root, no new library in the launcher;
   JSON through the SDK's `cjson` component (I: build check on amd64).
2. **Every root step runs in a transient system unit, never as a child of the launcher.** The shared sudo prompt (the
   one Settings > Apps uses, with the handwriting band in PDA mode since v0.8.10) runs one short command:
   `systemd-run --unit=cyberdeckzero-update ... /usr/share/APPLaunch/bin/cyberdeckzero-update apply ...`. The work then
   survives the launcher's own restart, a crash, the hold-Esc watchdog (exit 75) or a `needrestart`. Today `sudo` runs
   inside the launcher's cgroup: a launcher restart would kill a running `install.sh` or `dpkg` half way (section 5.5).
3. **The install is today's `install.sh`, replayed with the options of the first install, with a safety net.**
   - The helper backs up the binaries and units (about 30 MB).
   - It runs the new bundle's `install.sh` with the recorded options.
   - It waits for the new launcher to report its version in `board.state`. If that does not happen, it restores the
     backup and restarts the old launcher.
   - Re-running `install.sh` without the original options resets the Bluetooth keyboard name and turns a generated PDA
     portrait profile back to landscape. So the options must be recorded (section 5.8).
4. **System updates are planned by simulation, then classified against the launcher's real dependencies.**
   - `apt-get -s` is the plan. Its packages are checked against a protected set computed on the board itself: the ELF
     closure of the binaries, the owners of the tools the launcher calls, and the boot chain.
   - Only the confirmed list is installed, with `--no-remove`.
   - A small anchor package (`cyberdeckzero-runtime`, built by `install.sh` with `dpkg-deb`) tells apt what the launcher
     needs. `apt autoremove`, from the launcher or from a terminal, then never takes those packages away.
   - No `apt-mark hold`, no pinning: both silently block security updates and outlive the launcher (section 7.4).
5. **Nothing happens without the user.** The page checks and lists only:
   - no automatic download of system packages;
   - no automatic upgrade or reboot;
   - no `dist-upgrade`, no removals, no change to the apt sources.

   apt has no rollback, and the page says so before a system upgrade starts.

**Cost.**

| Path | Phases | Cost (ws) |
| --- | --- | --- |
| A + B: launcher update + list-only apt | P1 + P2 + P3 + P5 | 4.75-6.75 |
| System upgrade of the safe set | P4 | +2-2.75 |
| Launcher update alone (Option A) | P1 + P2 + P5 | 3.5-5 |

**Premises of the brief, checked.**
- "The sha256 asset exists": yes. Each tarball has a `.sha256` asset (108 bytes, `<hash> <file>` with one space). The
  API also gives a `digest: sha256:...` per asset (v0.8.12, S). Both come from the same GitHub account (section 5.3).
- "The tarball is about 57 MB": yes. arm64 is 57,332,103 bytes, amd64 57,311,828. It unpacks to 96 MB, 66 MB of which
  are fonts (S).
- "System service vs user service": three boards run the launcher as the **user** service `cyberdeckzero.service`: the
  deck, the Pi 3A+ and the Pi 4 B. The Pi 5, the HackBerry and the tablet run the **system** unit
  `cyberdeckzero-desktop.service`, started from the desktop menu (`tools/boards/boards.json` `kind`, S). Both restart on
  exit status 76.
- "An existing sudo prompt": yes. The shared asynchronous sudo service (`cp0_sudo_*`) has a top-layer password prompt;
  v0.8.10 gave it the handwriting band in PDA mode (S, section 3.4).
- **Prior art.** Upstream had an updater that report 029 removed. It used a fixed system unit `applaunch-updater.service`
  that "owns download, verification, rollback and install", started with `systemctl start` under a polkit rule (S:
  `git show b9967208^:ext_components/cp0_lvgl/src/cp0_launcher_updater.hpp:136-155`; `reports/029-upstream-decoupling.md:81`).
  It installed upstream's `.deb`, so it could not work for our tarballs. This study keeps its unit idea (a transient
  unit instead of a fixed one, section 5.5).

## 2. Decisions for the owner

| # | Question | Options | Recommendation |
| --- | --- | --- | --- |
| D1 | Scope | A launcher only / B A + apt list / C B + apt upgrade | **C in phases: A + B first (P1-P3), then P4 after the board checks of section 12** |
| D2 | Where the page lives | top-level **Updates** row after Apps / System > Updates / inside Settings > Apps | **top-level row after Apps**, its value showing the state ("0.8.13 available", "Up to date"); System is already 9-10 rows long |
| D3 | When it checks | on opening the page only / also in the background (at most once a day, Wi-Fi up, clock synced) with one toast per new version / never automatic | **page open + once a day in the background, one toast per new launcher version; no badge on the home grid** (a badge is a visual decision, D17) |
| D4 | Which builds may self-update | release channel only / every build | **check and notes on every build; Install only on the `release` channel.** A development build (`0.0.0-dev`, `development` or `prerelease` channel, a `-suffix` test version) says why |
| D5 | Integrity | sha256 only (API `digest` + `.sha256` asset) / plus a detached OpenPGP signature checked with `sqv`/`gpgv` and a key shipped in the launcher | **sha256 now** (catches truncated and corrupt downloads, honestly not a compromised account); a signature later if wanted (a release step and a key to keep) |
| D6 | Privilege | the shared sudo password prompt + `systemd-run` / a polkit rule letting the user start a fixed update unit without a password / `pkexec` | **the sudo prompt**, the same gate as installing an app. On a board whose user has NOPASSWD sudo it is a confirmation only (section 12 checks) |
| D7 | Replaying the install options | `install.sh` records them in a new `/etc/applaunch/install.conf` / the updater infers them (keyboard name from `99-bt-keyboard.rules`, `--board` from `board.conf`) | **record, with inference as the fallback for boards installed before the change** |
| D8 | Backup and rollback | binaries + units (~30 MB), restored automatically when the new launcher does not come up / also keep the previous tarball (57 MB) for a manual "Go back to 0.8.12" row | **automatic restore of binaries + units now**; the manual row later if wanted |
| D9 | How the notes look | plain text in the existing `TextBlock` / styled (bold runs, headings in the accent colour) with spans | **plain text with version headings in P1** (a visual decision: the owner may want styling) |
| D10 | Which part of the notes | the whole file / stop before the Install and Upgrading paragraphs | **stop at a marker** `<!-- launcher-notes-end -->` that the release notes gain from now on; without it, stop at the line `**Install**` |
| D11 | First start after an update | a toast "Updated to 0.8.13" with a "What's new" action / open the notes page at once / nothing | **the toast once**; the notes also ship in the bundle, so a manual `install.sh` update gets it too |
| D12 | Refreshing the apt lists | on demand with the password / a daily list refresh by `apt-daily.timer` (`APT::Periodic::Update-Package-Lists "1"` written by `install.sh`) | **on demand only**: no background apt load on the Zero 2 W (414 MB); the page shows the lists' age |
| D13 | Protection mechanism | simulation + classification + anchor package / `apt-mark hold` / apt pinning | **simulation + classification + anchor package**; no holds, no pins (section 7.4.2) |
| D14 | The launcher's runtime libraries (libc6, libstdc++6, libinput10, libglib2.0, libfreetype6 ...) | in the default set / listed, unchecked, included after a warning / terminal only | **listed, unchecked, one switch "Include launcher libraries" with a warning**: Debian stable updates keep their ABI and carry security fixes |
| D15 | Kernel, firmware, bootloader, initramfs | terminal only / after a second confirmation, on mains power | **terminal only**: the page shows the command (section 7.4.3) |
| D16 | Power | refuse on a battery board under 50 % and not charging; on a board without a battery, a "keep it plugged in" line in the confirmation | **yes** |
| D17 | Visual choices (row values, colours, the progress page, a badge) | owner / orchestrator brief | **settled in each phase's brief**, reusing the Apps page's tokens (amber "update offered", green "current") |
| D18 | unattended-upgrades and desktop updaters (PackageKit) | detect and show their state / also configure them (blacklist the protected set) | **detect and show only**; the launcher never changes their settings |
| D19 | App updates on the Updates page | a row "App updates (N)" that opens Settings > Apps > Installed / nothing | **the link row**: installing apps stays Settings > Apps' job |

## 3. How it works today

### 3.1 The bundle and the installer

- **The bundle** comes from `projects/APPLaunch/pizero2w/build.sh`:
  - name `cyberdeckzero-launcher-<version>-<arch>.tar.gz`, with a top folder `bundle/` (S: `build.sh:40-47,185`);
  - `payload/bin` holds the launcher, the Store, the console helper and `cyberdeckzero-ctl`; `payload/lib` the vfb shim;
    `payload/share` the images, fonts and audio; `payload/etc` the units and rules; `payload/doc` the licences
    (`build.sh:123-177`);
  - next to them: `install.sh`, `uninstall.sh`, `default-sources.sh`, `README.md` (`179`).
  - Channel `release` unless `APPLAUNCH_CHANNEL` says otherwise (`92-94`). A local scons build is `0.0.0-dev` on the
    `development` channel (S: `main/SConstruct:72-90`); CI builds are `prerelease` (`docs/dev/checklists/release.md`).
- **The release** (checklist steps 5-7) is `gh release create vX.Y.Z <tarballs> --notes-file docs/dev/release-notes-X.Y.Z.md`.
  - The v0.8.12 release has 4 assets: two tarballs and two `.sha256`, each with an API `digest` (S, `gh release view`).
  - The body is the notes file, served with CRLF line ends (S, `gh api .../releases/latest`).
  - 20 releases exist; v0.8.0 to v0.8.12 came out in 10 days (S, `gh release list`), so skipping several versions will
    be common.
- **Install and upgrade are the same command.** The 0.8.12 notes say: "Upgrading from 0.8.11: run the same command;
  settings, app sources and installed apps carry over" (S: `docs/dev/release-notes-0.8.12.md`). What `install.sh` does
  matters for an unattended re-run:

| Step | What it does | Matters for self-update because |
| --- | --- | --- |
| becomes root | `exec sudo -E sh "$0"` (`install.sh:58-62`) | the helper is root already |
| glibc floor | refuses below 2.38 (`134-154`) | none (the board passed once) |
| board detection | the device tree, DRM, framebuffers; DMI on a PC (`156-360`) | `pi3a-wsdsi43-portrait` (PDA) is **never detected**: without `--board` a re-run rewrites a generated portrait profile to landscape (`pizero2w/README.md:244-246`) |
| session | appliance or desktop, from the display manager (`390-460`) | a board that gained or lost a desktop **switches variant**: not for an unattended run |
| board profile | a checksummed `board.conf`; hand-edited = kept unless `--force` (`495-503`, `704-743`) | **the updater must never pass `--force`**; a hand-edited `board.conf` is then kept (S; the tablet's was kept on 2026-10-27) |
| arch check | the bundle's ELF `e_machine` must match `uname -m`, else "use ...-amd64.tar.gz" (`1004-1013`, `1158-1168`) | a second guard behind the updater's own asset choice |
| cgroup guards | refuses the migration and the switch to desktop when run from inside the launcher's service (`1169-1182`) | proof that the problem of design point 2 is known |
| packages | `apt-get update` + `apt-get install` of missing ones only: Pi `libinput10 libxkbcommon0 libfreetype6 curl device-tree-compiler`, PC adds `polkitd bluez alsa-utils` (+`kbd` on desktops) (`337-341`, `1184-1202`) | a new release can add a package: the update may need apt (network, dpkg lock) |
| files | `cp -a payload/share` over `/usr/share/APPLaunch` (never deletes: installed apps live there), `install -m 755` of the binaries (`1218-1250`) | `install` unlinks before writing, so a running launcher is not disturbed; this is how every manual upgrade works today |
| keyboard rule | writes `99-bt-keyboard.rules` with `--keyboard-name`, **default "M4 Keyboard"** (`43`, `1264-1273`) | a re-run without the option renames the keyboard link: **record the option** |
| user | `--user`, else `SUDO_USER` (`1018-1019`) | under `systemd-run` there is no `SUDO_USER`: **pass `--user`** |
| boot config | appliance Pi boards: the deck's PWM overlay and two cmdline arguments, unless `--no-boot-config` (`1394-1433`) | a re-run without `--no-boot-config` would add them: **record the option** |
| start | appliance: `systemctl --user restart cyberdeckzero.service` (`1482-1492`); desktop: not restarted, "quit it and start it again" (`1473-1480`) | the two variants restart differently (section 5.6) |

### 3.2 The services per board

| Board | Variant | Unit | Restart behaviour (S) |
| --- | --- | --- | --- |
| deck (Zero 2 W), Pi 3A+, Pi 4 B | appliance | user `cyberdeckzero.service` (lingering) | `Restart=on-failure`, `RestartForceExitStatus=76` (`cyberdeckzero.service:33-37`); systemd's default start limit then gives up after a crash loop (I: 5 starts in 10 s) |
| Pi 5, HackBerry CM5, FZ-M1 tablet | desktop | system `cyberdeckzero-desktop.service`, `User=`, `Conflicts=` the display manager | `Restart=no` except exit 76; `ExecStopPost=` gives the desktop back unless the exit status was 76 (`cyberdeckzero-desktop.service:19,44-57`) |

The launcher asks for its own restart with exit status 76 (`launcher_session::kRestartExitStatus`,
`settings_session_model.hpp:49`; `main/src/main.cpp:59-70`). PDA mode uses it today after "Restart the launcher? This
restarts the launcher and closes open apps." (`settings_pda_page.cpp:81`). The self-update reuses both.

### 3.3 Settings: the framework the page would use

- **The tree.** `SettingEntry` (`settings_tree_types.hpp:176-205`) has these parts:
  - a label;
  - a page factory or an API;
  - an optional `activation_gate` (refuse with a reason);
  - an optional `SettingActionConfirm` (a row that asks first: title, text, Cancel / confirm, `run`;
    `settings_tree_types.hpp:165-175`; Settings > Launcher "Reset app order" uses it, `settings_adapter.cpp:468`).
- **Where the rows are.**
  - The root list: Screen, Speaker, Wi-Fi, Bluetooth, Battery, Launcher, Touch, Apps, User, Date & Time, System
    (`settings_page.cpp:757-898`).
  - System: Storage, Licenses, About, Pointer navigation, SSH indicator, the keyboard rows, Quit to desktop (desktop
    session), Reboot, Shutdown (`898-923`).
- **Native pages.** The host classifies each node (`SettingsNodeKind`, `settings_page.hpp:47-58`). A whole page is a
  `SettingsNativePage` value (`64-80`: Wifi, Apps, About, TimeZone, StrokesTraining ...). It is built by a panel class
  on `cp0::ui::Roller`, `CompactList`, `Dialog`, `TextBlock` (recipe: `docs/dev/native-settings-page-howto.md`
  sections 2-3). `settings_native_row_look()` lets a root row show a value and a chevron (Time zone does).
- **Version display.** About shows version, build, channel and commit; System's first line is the version
  (`settings_static_info_page.cpp:27-46`, `settings_system_page.cpp:30-46,390`), from the `LAUNCHER_VERSION_RAW` and
  `LAUNCHER_CHANNEL_RAW` defines (`main/SConstruct:101-104`). `board.state` publishes `launcher=<version>` with the
  launcher's `pid` (`docs/APP-CONTRACT.md:268-306`), in `$XDG_RUNTIME_DIR/applaunch/` (`:33`).
- **Background jobs, two patterns.**
  - Settings > Apps keeps a `Jobs::Shared` state behind a mutex. Detached worker threads run the steps; the page polls
    `take()` on a timer for status, a stage line with elapsed seconds, failures and "sudo pending"
    (`settings_apps_jobs.cpp:34-131,597-636`). Workers outlive the page.
  - Wi-Fi uses `run_async_task` with a generation counter (howto step 4).

  The update jobs fit the Apps pattern: long, may outlive the page, hand over to the sudo prompt.
- **The Store and app updates.**
  - Settings > Apps already has "Update all (N)" and per-app Update (`settings_native_apps_page.hpp:8-27`).
  - An app install: free-space check (`statvfs`, `settings_apps_jobs.cpp:145-150`), `--prepare-package` as the user,
    then the sudo prompt with `--package-helper` (auth timeout 60 s, run timeout 15 min, `638-669`), then
    `--finalize-package`.
  - The Store's helper runs `dpkg --install`, or `apt-get -y install <deb>` when dependencies are missing
    (`projects/AppStore/main/backend/appstore_native.cpp:1791-1835`).
  - **App updates are the Store's job**; the Updates page only links to them (D19).
- **Network, HTTP, TLS, JSON in the launcher today.**
  - The launcher binary links no HTTP or TLS library (section 7.4.1). It runs `curl` as a subprocess: the source
    probe `curl -sS -L -m 10 -w %{http_code}` (`settings_apps_jobs.cpp:152-161`), through `apps_backend::run_program`
    (process group, timeout, then SIGTERM and SIGKILL, `apps_backend.cpp:79-146`).
  - "Network up" = a default route in `/proc/net/route` (`settings_apps_jobs.cpp:137-143`).
  - The Store has libhv + OpenSSL 3 + nlohmann/json, falls back to `curl`, and honours `http(s)_proxy`
    (`appstore_native.cpp:470-521`).
  - JSON in the launcher: only `cp0_config_json.h`, a flat config reader that skips arrays. The SDK has a `cjson`
    component (`SDK/components/cjson`, licence already in `LICENSES/cJSON-MIT.txt`). nlohmann/json exists only in the
    arm64 cross sysroot.
- **The sudo service** (`ext_components/cp0_lvgl/src/cp0_sudo_*`):
  - it runs `sudo -S -v` then `sudo -S -- argv` as the launcher's user (`cp0_sudo_command.hpp:14-37`), with a password
    of at most 128 bytes and 64 KiB of captured output streamed to a callback (`cp0_sudo_async.cpp:56-57,600-612`);
  - its prompt is the shared top-layer dialog: the band in PDA mode (v0.8.10, goldens `480x800/zone_sudo_*.png`), the
    fallback keyboard elsewhere;
  - a fixed set of **admin commands** with validated arguments sits on top (`NtpSet`, `TimezoneSet`, `TimeSet`,
    `cp0_sudo_async.cpp:944-992`): the pattern for an `UpdateApply` command whose argv is built inside the toolkit.
- **The lock screen** can be held off while a long job runs: `ui_screensaver_set_foreground(0)` is what an app launch
  uses (`ui_screensaver.h:22`). A "keep awake while installing" flag would be new (I).

### 3.4 Status feed

`status.state` and `board.state` (APP-CONTRACT sections 5b, 5c) need nothing new. `board.state`'s `launcher=` key plus a
live `pid` is exactly the health signal the helper needs after a restart (section 5.7). An "update available" key for
apps is not proposed (no app needs it).

## 4. Where the Updates page fits, and how it looks

### 4.1 Placement

A top-level row **Updates** after Apps (D2). Its value says the state, with a chevron:
- "0.8.13 available" in amber;
- "37 system updates";
- "Up to date" in grey;
- "Development build" in grey.

The row is the only discoverable place for a feature the user rarely visits. System > Updates would be the 11th row of
an already long section; inside Settings > Apps it would mix apps with the system.

### 4.2 Levels

| Level | Rows (affordance: `>` opens, dot = action, grey = info) |
| --- | --- |
| Updates | `Launcher` (value: installed version, or "0.8.13 available" amber) `>`; `System packages` (value: "37 updates", "lists 9 days old", "not checked") `>`; `App updates` (value: N from the Store's list) `>`; `Check now` (dot); `Last checked` (grey info) |
| Updates > Launcher | `What's new in 0.8.13` `>`; `Install 0.8.13` (dot, asks first; absent on a development build, where an Info row says why); `Check again` (dot); after a failed update: `Last update failed: <reason>` (grey) and `Show log` `>` |
| What's new | a `TextBlock`: the notes of every version newer than the installed one, newest first (section 6) |
| Updates > System packages | `Refresh lists` (dot; password; "lists from today 09:12"); `Install 31 updates (41 MB)` (dot, asks first); the list (`CompactList`, sections: Will be installed / Launcher libraries, kept / Terminal only / Held by you / Kept back by apt); `Reboot` (dot) when a reboot is advised |
| progress | full page while a root job runs (sections 5.6 and 7.2): steps, a bar, "Do not switch off"; Esc only in a cancellable phase |

### 4.3 Mockups at 640x480 (deck, roller look; not to scale, one character is about 10 px)

Settings root, the new row highlighted:

```
+--------------------------------------------------------------+
| Settings            ( Esc )            >_  *  .ill  (20:30)  |
|                                                              |
|    Touch                                                     |
|    Apps                                                      |
|##############################################################|
|#   Updates                          0.8.13 available   >    #|
|##############################################################|
|    User                                                      |
|    Date & Time                                               |
|                              v                               |
+--------------------------------------------------------------+
```

Updates, then Updates > Launcher (the Wi-Fi / Apps action-list pattern):

```
+--------------------------------------------------------------+   +--------------------------------------------------------------+
| Updates             ( Esc )                         (20:30)  |   | Launcher            ( Esc )                         (20:30)  |
|                                                              |   |                                                              |
|##############################################################|   |##############################################################|
|#   Launcher 0.8.12                 0.8.13 available    >    #|   |#   What's new in 0.8.13                                >    #|
|##############################################################|   |##############################################################|
|    System packages                  37 updates          >    |   |    Install 0.8.13  (57 MB)                              o    |
|    App updates                      2                   >    |   |    Check again                                          o    |
|    Check now                                            o    |   |                                                              |
|    Last checked                     today 10:42              |   |                                                              |
|  Checked 10:42 over Wi-Fi                     v              |   |  0.8.12 -> 0.8.13, release, arm64                            |
+--------------------------------------------------------------+   +--------------------------------------------------------------+
```

"Install 0.8.13" asks first (a `Dialog`), then the shared sudo prompt:

```
+--------------------------------------------------------------+
| Launcher            ( Esc )                         (20:30)  |
|      +--------------------------------------------------+    |
|      | Install 0.8.13?                                  |    |
|      | Downloaded and checked (57 MB). The launcher     |    |
|      | restarts and open apps close. Settings, apps     |    |
|      | and board.conf are kept. If 0.8.13 does not      |    |
|      | start, 0.8.12 comes back by itself.              |    |
|      |                                                  |    |
|      |        ( Cancel )          ( Install )           |    |
|      +--------------------------------------------------+    |
+--------------------------------------------------------------+
```

What's new (a `TextBlock`, drag or Up / Down, Page Up / Down):

```
+--------------------------------------------------------------+
| What's new          ( Esc )                         (20:30)  |
| 0.8.13 - 2026-11-04                                          |
| New since 0.8.12                                             |
| - Updates in Settings. The launcher checks GitHub for a      |
|   new release, shows its notes and installs it; a failed     |
|   start brings the old version back.                         |
| - ...                                                        |
|                                                              |
| 0.8.12 is installed. Older notes: Settings > System > About. |
+--------------------------------------------------------------+
```

Progress while the root helper runs (the old launcher draws it; on an appliance board this last frame stays on the
panel while the unit restarts, I: fbdev keeps the last buffer):

```
+--------------------------------------------------------------+
| Updating            (     )                         (20:31)  |
|                                                              |
|     Updating the launcher to 0.8.13                          |
|                                                              |
|     [x] Download checked (sha256)                            |
|     [x] Backup of 0.8.12                                     |
|     [>] Installing                       ======-----  45 s   |
|     [ ] Restart and check                                    |
|                                                              |
|     Do not switch off. The launcher restarts by itself.      |
+--------------------------------------------------------------+
```

System packages (a `CompactList`, Compact density: about 7 rows on the deck):

```
+--------------------------------------------------------------+
| System packages     ( Esc )                         (20:30)  |
|  Refresh lists                    lists today 09:12      o   |
|  Install 31 updates               41 MB, about 12 min    o   |
| -- Will be installed (31) ---------------------------------- |
|  curl                       8.14.1-2 > 8.14.1-2+deb13u1      |
|  openssh-server             1:10.0p1-7 > 1:10.0p1-7+deb13u1  |
| -- Launcher libraries, kept (4) ---------------------------- |
|  libc6                      2.41-12 > 2.41-12+deb13u1        |
| -- Terminal only (2) --------------------------------------- |
|  linux-image-rpi-v8         kernel: sudo apt full-upgrade    |
+--------------------------------------------------------------+
```

### 4.4 Mockups at 480x800 (PDA portrait; 480x640 is the same with fewer rows)

```
+------------------------------------------------+   +------------------------------------------------+
| Updates       ( Esc )        >_ * .ill (20:30) |   | What's new    ( Esc )                  (20:30) |
|                                                |   | 0.8.13 - 2026-11-04                            |
|                                                |   | New since 0.8.12                               |
|                                                |   | - Updates in Settings. The launcher            |
|                                                |   |   checks GitHub for a new release,             |
|                                                |   |   shows its notes and installs it; a           |
|################################################|   |   failed start brings the old version          |
|# Launcher 0.8.12   0.8.13 available   >       #|   |   back.                                        |
|################################################|   | - System packages: the list of updates,        |
|  System packages   37 updates         >        |   |   what is kept and why.                        |
|  App updates       2                  >        |   |                                                |
|  Check now                            o        |   | 0.8.12 is installed.                           |
|  Last checked      today 10:42                 |   |                                                |
|                                                |   |                                                |
|                                                |   |                                                |
|                                                |   |                                                |
|                       v                        |   |                                                |
+------------------------------------------------+   +------------------------------------------------+
```

- **The writing band** shows only with a text field (on demand, report 041). The page has none. The sudo prompt brings
  its own band (v0.8.10).
- **Line length.** At 480 px wide the body text holds about 35-40 characters per line (I: Montserrat or DejaVu at the
  BODY size), at 640 px about 50. The notes must be unwrapped and re-wrapped by LVGL, never shown with their 120-column
  hard wraps (section 6.2).

## 5. Launcher self-update

### 5.1 Version source

| Item | Proposal |
| --- | --- |
| Request | one call: `GET https://api.github.com/repos/OSRdesign/cyberdeck-zero-launcher/releases?per_page=10`, with `curl -fsSL --max-time 20 -A cyberdeckzero-launcher/<version> -H "Accept: application/vnd.github+json"` and `If-None-Match: <etag>` |
| Why the list, not `/releases/latest` | the list also carries the **notes of every skipped version** (`body`); `latest` gives one |
| Fields used | `tag_name` (`vX.Y.Z`), `draft`, `prerelease`, `published_at`, `body`, and per asset `name`, `size`, `digest`, `browser_download_url` |
| Asset | `cyberdeckzero-launcher-<X.Y.Z>-<arch>.tar.gz` and its `.sha256`; `<arch>` from `apps_backend::system_arch()` (`apps_backend.cpp:287-293`: arm64, amd64; `APPLAUNCH_ARCH` test hook). No asset for this arch: "no build for amd64 in 0.8.13" |
| Rate limit | unauthenticated: 60 requests an hour per public address; a `304 Not Modified` answer to `If-None-Match` does not count (GitHub's documented rule). Six boards checking daily use six requests |
| No JSON fallback | the web redirect `.../releases/latest` gives the tag without the API, and the asset URLs are predictable; the notes would then come from `raw.githubusercontent.com/.../vX.Y.Z/docs/dev/release-notes-X.Y.Z.md`. This works without a JSON parser but cannot list the skipped versions: kept only as a fallback idea |
| Offline | no default route (the Apps rule): "No network", the last result and its age stay shown. curl errors are mapped: 6 (DNS), 7 (connect), 28 (timeout), 22 + HTTP 403 with a zero `X-RateLimit-Remaining` ("GitHub limit, try in an hour"), 60 (certificate) |
| Clock | the Pi boards have no RTC: before network time a certificate can look "not yet valid". Check only when timedated says `NTPSynchronized=yes` (the launcher already talks to timedated, `cp0_timedate_client`); else "Waiting for network time" |
| Proxy | curl honours `https_proxy` from its environment. The launcher's unit has none: a board behind a proxy would set it in `board.conf` (EnvironmentFile). Rare; a README line |
| Where the state lives | a small file `~/.local/state/cyberdeckzero/update.state` (last check, ETag, latest seen, last result, version last shown in "What's new"), **not** `config.json`: that store is capped at 64 keys and close to full (report 046 section 4.1) |
| Trigger | page open + at most once a day in the background (D3), only with Wi-Fi or Ethernet up and the clock synced; never while an app is in front (the check is cheap, but a toast over an app is not wanted) |

### 5.2 Channel and version order

- **Versions are semantic** (`main/SConstruct:74-75` enforces `X.Y.Z[-suffix]`). A semver compare is needed:
  `0.8.13-rc.1 < 0.8.13`. The Debian order that `apps_backend::compare_versions` implements would rank `0.8.9-pda.1`
  **above** `0.8.9`, because `-` is Debian's revision separator.
- **Candidates:** releases that are not drafts and not pre-releases, with a tag newer than the running version.
- **On a development build** (channel `development` / `prerelease`, `0.0.0-dev`, or any `-suffix`) the page shows the
  latest release and its notes, and says "This is a development build: install releases by hand" (D4). Without that
  rule, every local build (`0.0.0-dev`) would be offered every release.
- **No downgrade** from this page. Going back is the rollback's job (section 5.7).

### 5.3 Integrity

- **What exists.** The `.sha256` asset and the API `digest` give the same hash. Checking it catches a truncated
  download, a broken cache or a captive portal's HTML page saved as the tarball. It does **not** protect against someone
  who controls the GitHub account or the release: the hash comes from the same place as the file.
- **Realistic next step (D5).** A detached OpenPGP signature `...tar.gz.sig` made on the owner's PC, checked on the
  board with `sqv` or `gpgv` against a public key shipped in the launcher bundle (`/usr/share/APPLaunch/keys/`):
  - Debian 13's apt verifies its own archives with Sequoia's `sqv`, so a verifier is on every board without a new
    library (I: confirm `sqv` / `gpgv` presence, section 12);
  - cost: a key pair to keep safe, one signing step in the release checklist, and the key's own update story.
- **The root helper re-checks the hash** on its own copy (section 5.6), so the file the user process downloaded cannot
  change between the check and the install.

### 5.4 Download

- **As the user, with `curl`.** Into `~/.cache/cyberdeckzero/update/` with `-C -` (resume) and `--max-time 900` (the
  Store's values, `appstore_native.cpp:474`). Progress comes from the file size against the asset `size`. Esc / a
  Cancel row stops it: no root has been involved yet. Done before the confirmation, so the dialog can say
  "downloaded and checked".
- **Not in `/tmp`.** Debian 13 mounts `/tmp` as a RAM disk by default (I: confirm on Raspberry Pi OS, section 12). On
  the Zero 2 W, 57 + 96 MB there would not fit in RAM.
- **Space.** 57 MB (tarball) + 96 MB (unpacked, root's copy) + about 30 MB (backup) + margin: **refuse below 300 MB free
  on `/`**, with the Apps page's wording (`apps_status::space_problem`, `apps_status_model.hpp:113`). Afterwards only
  the backup stays.
- **Time on the Zero 2 W** (I, to measure in P2): download 30 s to 2 min on 2.4 GHz Wi-Fi; sha256 a few seconds; unpack
  30-60 s; `install.sh` under a minute without new packages.

### 5.5 Privilege model

| Option | How | For | Against |
| --- | --- | --- | --- |
| **sudo prompt + `systemd-run` (recommended)** | the shared prompt runs `sudo -S -- /usr/bin/systemd-run --unit=cyberdeckzero-update --collect --quiet /usr/share/APPLaunch/bin/cyberdeckzero-update apply --version X --sha256 H --from <file> --user <name>`; returns at once; the helper runs as a **transient system service** | the user types the password, as for an app install; no new rule or sudoers entry; the work is outside the launcher's cgroup | one password per update; on NOPASSWD boards only the dialog guards it |
| sudo, the helper as a direct child | `sudo -S -- cyberdeckzero-update apply ...` | simplest | **wrong**: the helper and `install.sh` stay in the launcher's cgroup. `install.sh`'s own `systemctl --user restart` (`install.sh:1491`) kills them both; so do a crash, the watchdog, a desktop unit stop, or `needrestart` restarting the desktop unit |
| polkit + a fixed unit | `cyberdeckzero-update.service` + a rule letting the user `start` it, as `52-cyberdeckzero-desktop.rules` does for the desktop unit; arguments through a file | no password (upstream's way) | any process of the user (any app) can then start an update of the launcher, bounded to a verified release; arguments by file are clumsy |
| `pkexec` | polkit's authentication agent | - | no agent runs in our sessions |

- **Validation.** The helper's argv is built inside the toolkit, like the `TimezoneSet` admin command
  (`cp0_sudo_async.cpp:957-964`): the version matches `X.Y.Z`, the hash is 64 hex digits, the path is in the user's
  cache. The helper checks them again, and that `--user` exists and owns the file.
- **The helper itself.** `cyberdeckzero-update` is a POSIX shell script installed by `install.sh` in
  `/usr/share/APPLaunch/bin` (root-owned).
  - It copies itself to `/run/cyberdeckzero-update/` and runs from there. The `install.sh` it starts replaces the file
    it was started from, and `sh` reads its script while it runs.
  - It refuses to start when its unit is already active (`systemctl is-active cyberdeckzero-update`).

### 5.6 The install run, step by step

| # | Who | Step | The screen |
| --- | --- | --- | --- |
| 1 | launcher (user) | check, download, sha256, confirmation dialog (open apps close; space; "comes back by itself") | Updates > Launcher, then the dialog |
| 2 | launcher | refuse while a package job runs (Settings > Apps, a system upgrade) or the Store tile is open | "Another installation is running" |
| 3 | sudo prompt | `systemd-run ... cyberdeckzero-update apply` | the shared prompt |
| 4 | helper (root) | copy the tarball to `/var/cache/cyberdeckzero-update/`, re-check sha256, unpack, check `bundle/payload/bin/cyberdeckzero`'s arch | progress page (the launcher polls `/run/cyberdeckzero-update/status`, world-readable, one `key=value` per line) |
| 5 | helper | `install.sh --dry-run` with the recorded options; **stop** if it reports a variant switch, a migration, an unknown board or a profile `replace` | "This update needs install.sh by hand: <reason>" |
| 6 | helper | backup to `/var/cache/cyberdeckzero-update/backup-<old>/`: `bin/`, `lib/`, the user unit or the desktop unit, the console unit, the polkit and udev rules it installs (about 30 MB) | "Backup of 0.8.12" |
| 7 | helper | `sh bundle/install.sh --user <u> <recorded options>` (no `--force`), output to `/var/log/cyberdeckzero/update-<date>.log`, under `systemd-inhibit --what=shutdown:sleep:handle-power-key` | "Installing" |
| 8a | appliance | `install.sh` ends with `systemctl --user restart cyberdeckzero.service`: the old launcher is stopped, the new one started | the old launcher's last frame, then the new launcher |
| 8b | desktop | `install.sh` does not restart (`install.sh:1476-1477`). The helper writes `status=installed`; the old launcher sees it and exits with 76; the unit restarts it without giving the desktop back | the same |
| 9 | helper | **health check**: up to 90 s for `$XDG_RUNTIME_DIR/applaunch/board.state` with `launcher=<new>` and a live `pid` that stays alive 20 s | - |
| 10 | new launcher | reads the status file: "Updated to 0.8.13" toast with "What's new" (D11) | home grid or Settings |

### 5.7 Rollback and backup

- **When `install.sh` fails** (non-zero exit) or **the health check fails**:
  1. the helper restores the backup files;
  2. `daemon-reload`, then restarts the old launcher: appliance `systemctl --user restart` as the user (the `as_user`
     way of `install.sh:1026`), desktop `systemctl restart cyberdeckzero-desktop.service`;
  3. writes `status=rolled-back` with the reason.

  The restored launcher then says "Update to 0.8.13 failed, 0.8.12 restored" and offers the log.
- **What a rollback does not undo:**
  - packages `install.sh` added with apt;
  - new files in `share/` (fonts, images: harmless to an older binary, I);
  - a `board.conf` rewritten by a generated-profile update: it is kept as `board.conf.bak` by `install.sh` itself
    (`install.sh:738`), and the helper restores it too.
- **A desktop board never ends without a screen.** A new launcher that crashes stops its unit (`Restart=no`), and
  `ExecStopPost=` brings the desktop back. The helper then restores the files; the user starts the launcher again from
  the menu.
- **An appliance board** without the health check would loop until systemd's start limit and stay black: the health
  check is what makes self-update safe there.
- **Interrupted helper** (power cut): the status file says `running` and no unit is active. The next launcher start
  offers "Update interrupted: repair" (it re-runs the helper's `resume`: restore or finish). `install.sh` is idempotent,
  so finishing is a re-run.
- **Keep** the last backup until the next update; the full previous tarball only if D8 says so.

### 5.8 Install options and `board.conf`

- **A new `/etc/applaunch/install.conf`**, written by `install.sh` on each run (D7): `keyboard_name=`, `board=` (only
  when given with `--board`), `boot_config=0|1`, `pwm_backlight=0|1`, `default_source=0|1`.
- **The helper replays it.** A board installed before that file existed gets inferred values:
  - the name from the `ATTRS{name}==` of `/etc/udev/rules.d/99-bt-keyboard.rules`;
  - `--board` from `APPLAUNCH_BOARD=` in `board.conf`, when it is one of `install.sh`'s Pi names (a `pc-*` profile is
    detected, not passed);
  - `--no-boot-config` when the cmdline arguments are absent on an appliance Pi.
- **`board.conf`.** `install.sh` keeps a hand-edited file and rewrites only its own unedited ones (checksum line,
  `install.sh:696-743`). The helper never passes `--force`. A dry run that plans `replace` stops the automatic update
  (step 5), so a profile never changes behind the user's back.
- **Apps need no change.** `install.sh` never deletes in `/usr/share/APPLaunch` (installed apps' `.desktop` files live
  there, `install.sh:1220`). The app contract stays 1. The Store binary is part of the bundle and is updated with it.

### 5.9 First start after an update ("What's new")

- **`build.sh` copies `docs/dev/release-notes-<version>.md` into `payload/doc/`.** `install.sh` installs `payload/doc`
  as a whole into `/usr/share/doc/cyberdeckzero-launcher/` (`install.sh:1252-1262`). The notes of the running version
  are then always on the board, offline, however it was installed.
- **On start**, the launcher compares its version with `last_shown` in `update.state`. Newer: one toast
  "Updated to 0.8.13" with a What's new action (D11), the notes from the downloaded cache (skipped versions included)
  or, after a manual install, from `/usr/share/doc`. A first install (no state file) shows nothing.

## 6. Release notes display

### 6.1 The Markdown actually used (13 files, 2.2-8.1 KB, 49 KB in all; S: `docs/dev/release-notes-*.md`)

| Construct | Where | Handling |
| --- | --- | --- |
| `**bold**` | every file (labels of bullets, "New since") | drop the markers (or a bold span, D9) |
| `- ` bullets, continuation lines indented 2 spaces | every file | join continuations; bullet glyph + hanging indent |
| nested `  - ` | 0.8.0 (6 lines) | indent one more step |
| `` `code` `` | every file | drop the backticks |
| fenced code (```` ``` ````) | every file (the install commands) | after the end marker (D10); otherwise keep the lines, monospace if the face is loaded, no re-wrap |
| `<!-- branding-allow-begin/end -->` | every file | strip all comments |
| `## heading` | 0.8.8, 0.8.9 (the "in pictures" sections) | a heading line |
| tables of images `| ![alt](path) |` | 0.8.8, 0.8.9 | drop the table, keep one line "(pictures in the online notes)" |
| links `[text](url)` | 0.8.8 | keep the text |
| `<placeholder>` in code | every file | literal text (nothing is HTML) |
| CRLF | the API body | strip `\r` |
| non-ASCII | none today | UTF-8 through FreeType anyway |

### 6.2 Proposed renderer: Markdown to plain text, a pure function

`notes_text(markdown, width_hint)` returns a list of blocks (heading, paragraph, bullet with level, code line):
- a model in the howto's sense, no LVGL;
- unit-tested on **all 13 existing files** with expected outputs;
- hard-wrapped lines joined into paragraphs;
- everything after `<!-- launcher-notes-end -->` dropped (fallback: the line `**Install**`, D10).

The page joins the blocks into one string for the existing `cp0::ui::TextBlock`:
- the widget the Licenses page uses for long text (`settings_native_info_page.cpp:97`);
- drag, Up / Down by a row, Page Up / Down, Home / End, Esc / Left back (`cp0_ui_widgets.hpp:48-49,345-366`);
- touch, keys and the pointer work as there, so there is nothing new to build for navigation.

### 6.3 Styling options (D9)

- **Plain (recommended for P1).**
  - The face: one FreeType DejaVu Sans at the BODY size. Its `•` exists; the built-in Montserrat covers only ASCII,
    so plain `-` there.
  - Version headings as `0.8.13 - 2026-11-04` lines with a blank line before.
  - The bundle already ships `DejaVuSans.ttf`, `DejaVuSansMono.ttf`, `Montserrat-Bold.ttf` and `JetBrainsMono-*.ttf`
    (`APPLaunch/share/font`).
- **Styled.** LVGL 9.5 has spans (`LV_USE_SPAN=y`) and label recolor (`lv_label_set_recolor`, `lv_label.h:159`):
  - spans could draw bold runs with `Montserrat-Bold.ttf` and headings in the accent colour;
  - recolor uses `#` as its marker, and the notes are full of `#111`-style PR numbers, so every `#` would need escaping
    (I: the escape rule in 9.5);
  - a span group of a few hundred spans in a scrolling box on the Zero 2 W is untested (I).

  To decide after seeing P1's plain page.

### 6.4 Length and several versions

- **Newest first.** One heading per version.
- **Capped at 5 versions or about 24 KB**, then "Older notes: github.com/OSRdesign/cyberdeck-zero-launcher/releases".
  One label of 24 KB lays out once when the page opens (I: time it on the Zero 2 W in P1; the Licenses text is the
  reference).
- **The cache.** The bodies of the versions on offer are saved in `~/.cache/cyberdeckzero/update/notes/<version>.md`
  at check time, so What's new works offline after the update.

## 7. System updates (apt)

### 7.1 Detecting updates

| Command | Root | Network | What it gives | Use |
| --- | --- | --- | --- | --- |
| `apt-get update` | yes | yes | fresh lists | **Refresh lists** row: sudo prompt + `systemd-run --unit=cyberdeckzero-apt` (as section 5.5); progress from `-o APT::Status-Fd=3` |
| `apt-get -s upgrade` | **no** (a simulation by a user turns locking off: apt-get(8)) | no | `Inst pkg [old] (new suite [arch])`, `Conf`, kept-back list, counts | the plan of the safe path (no new packages, no removals) |
| `apt-get -s dist-upgrade` | no | no | also `Remv` and new packages | **only to show** what the safe path leaves behind ("Kept back by apt") |
| `apt-get -qq --print-uris upgrade` | no (I: as a user; else take sizes from `apt-cache show`) | no | URIs with sizes | the download size in the confirmation |
| `apt list --upgradable` | no | no | the same list, with a "no stable CLI" warning | not used |
| lists' age | no | no | mtime of `/var/lib/apt/lists/*InRelease` (or `/var/lib/apt/periodic/update-success-stamp`) | "lists 9 days old": a list from old lists says little (D12) |

Without fresh lists the list-only page can mislead. It shows the age, and offers Refresh (one password), before any
count.

### 7.2 Running an upgrade (P4)

- **One transient unit,** started through the sudo prompt with the confirmed package list as arguments (validated: names
  only).
  1. Re-simulate the exact command: `apt-get -s install --only-upgrade --no-remove <list>`.
  2. **Stop if the plan differs from the confirmed one** (a new package, a removal, a protected package pulled in as a
     dependency).
  3. **Download phase, cancellable:** `apt-get -d install --only-upgrade --no-remove -y <list>`. Cancel: the launcher
     writes a flag the helper reads in this phase only (no second password).
  4. **Install phase, not cancellable:** `apt-get install --only-upgrade --no-remove -y <list>`. Environment and
     options:
     - `DEBIAN_FRONTEND=noninteractive`;
     - `-o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold` (local config files kept, never a
       prompt);
     - `-o DPkg::Lock::Timeout=120`;
     - `NEEDRESTART_MODE=l` (list, never restart services; otherwise `needrestart` may restart the desktop unit and end
       the launcher);
     - under `systemd-inhibit --what=shutdown:sleep:handle-power-key:handle-lid-switch`.
  5. Afterwards, `dpkg --audit` must be empty.
- **Progress:** `APT::Status-Fd` lines (`dlstatus:` and `pmstatus:pkg:percent:text`) go to the status file; the page
  shows the bar, the current package and the elapsed time (the Apps stage line).
- **Cancel, honestly.** During the download: yes. During the install: no. Stopping dpkg half way is how systems break.
  The dialog says it before the start.
- **Logs.**
  - apt's own `/var/log/apt/history.log` and `term.log`;
  - the helper's `/var/log/cyberdeckzero/apt-<date>.log` and the unit's journal (`journalctl -u cyberdeckzero-apt`);
  - the page shows the result and a "Show log" `TextBlock` with the tail.
- **The dpkg lock.**
  - Inside the launcher, one process-wide "package manager busy" flag covers Settings > Apps jobs, the launcher update
    and apt.
  - Against others (the Store tile, PackageKit, unattended-upgrades, a terminal), the lock timeout applies, then
    "Another program is installing packages (<name> holds the lock)".
- **Reboot.**
  - Debian creates `/run/reboot-required` only through some packages; Ubuntu's `update-notifier-common` always does.
  - So the launcher also decides by class: libc6, systemd, dbus, or a kernel (terminal) mean "Restart advised".
  - A `Reboot` row (the existing System > Reboot path). **Never an automatic reboot.**
- **After an interrupted run** (power cut): the next start sees the status `running` with no active unit, or
  `dpkg --audit` not empty. It offers "Repair: finish the interrupted installation" = `dpkg --configure -a` then
  `apt-get -f install --no-remove -s` shown first.
- **Screen.** The page keeps the lock screen away while a root job runs (D17 look; I: a hold flag next to
  `ui_screensaver_set_foreground`). Dimming is harmless to apt; the user simply loses the progress view.

### 7.3 Who else installs packages

| Actor | Where | Interaction |
| --- | --- | --- |
| Settings > Apps / the Store helper | every board | `dpkg --install`, or `apt-get -y install <deb>` that pulls dependencies (`appstore_native.cpp:1814-1828`): it can **upgrade a system library** an app needs, outside the protection. Residual risk; the Store is the unchanged upstream submodule |
| app `.deb`s | every board | installed from files, no repository: `apt-get upgrade` never offers them (no candidate). Their dependencies are protected by their own `Depends` |
| `install.sh` | every update | `apt-get install` of a missing package, on purpose |
| unattended-upgrades | I: Raspberry Pi OS Lite does not install it by default; Armbian on Ubuntu 26.04 may (section 12) | honours `apt-mark hold` and its own `Package-Blacklist`, knows nothing of the launcher. Shown as "Automatic updates: on" (D18) |
| PackageKit and the desktop's updater | I: Raspberry Pi OS desktop (Pi 5, HackBerry) ships a taskbar updater; Ubuntu XFCE may have `update-notifier` | holds the dpkg lock at times; the desktop user can upgrade from there; shown when present |
| `apt-daily.timer` / `apt-daily-upgrade.timer` | every Debian | do nothing unless `APT::Periodic::*` is set (I: check per board) |
| `needrestart` | I: Ubuntu-based images | `NEEDRESTART_MODE=l` in our runs; a terminal run keeps its own setting |

### 7.4 The protection requirement

#### 7.4.1 What the launcher really depends on

| Layer | Packages (Debian 13 names; Ubuntu 26.04 the same t64 names, I) | Source |
| --- | --- | --- |
| ELF: launcher | `libc6` (GLIBC_2.38), `libstdc++6` (GLIBCXX_3.4.30), `libgcc-s1`, `libinput10` (+ `libudev1`, `libevdev2`, `libmtdev1`, `libwacom9`), `libxkbcommon0`, `libglib2.0-0t64` (gio, gobject, glib: D-Bus to BlueZ and timedated), `libfreetype6` (+ `libpng16-16t64`, `zlib1g`, `libbrotli1`, `libbz2-1.0`) | S: DT_NEEDED of `bundle/payload/bin/cyberdeckzero` in the 0.8.12 arm64 bundle; transitive packages I |
| ELF: Store | the same + `libssl3t64` (`libssl.so.3`, `libcrypto.so.3`), GLIBCXX_3.4.32 | S: the Store binary of the same bundle |
| ELF: shim, ctl | `libc6` only (GLIBC_2.34) | S |
| `install.sh` packages | Pi: `libinput10 libxkbcommon0 libfreetype6 curl device-tree-compiler`; PC: + `polkitd bluez alsa-utils`, `kbd` on desktops | S: `install.sh:337-341,1184-1202` |
| tools and services the launcher calls | `curl` (+ `libcurl4t64`, `ca-certificates`), `sudo`, `systemd` (`systemctl reboot/poweroff`, `timedatectl`, `systemd-run`, logind, timedated, udev), `dbus`, `polkitd` (+ our rules), `network-manager` (`nmcli`), `bluez` (D-Bus, `bluetoothd`), `rfkill`, `alsa-utils` (`amixer`), `pulseaudio-utils` / PipeWire's `pactl`, `dpkg` / `apt` (the Store), `kbd` (`chvt`, desktop unit) | S: `cp0_lvgl_process.cpp:146,158`, `cp0_bluez_dbus_client.cpp`, `cp0_timedate_client.cpp`, `pizero2w/*.rules`, `cyberdeckzero-desktop.service:43` |
| display, touch, keys | the kernel's fbdev (`/dev/fb0` legacy firmware DPI on the deck; DRM fbdev emulation `vc4drmfb`, `drm-rp1-dpi`, `i915drmfb` elsewhere), evdev + uinput (`91-applaunch-vkbd.rules`), backlight class, fbcon (`cyberdeckzero-console`), the panel overlays (`waveshare-28dpi*` on the deck, `vc4-kms-dsi-*`, `vc4-kms-dpi-hyperpixel4`) and our compiled `.dtbo`s in `/boot/firmware/overlays` (not owned by a package) | S: `install.sh:211-283,1394-1459`, the board profiles |
| boot chain | Pi: `linux-image-rpi-v8` / `linux-image-rpi-2712` and their versioned kernels, `raspi-firmware`, `rpi-eeprom` (Pi 4, 5, CM5), `initramfs-tools`, `firmware-brcm80211` (Wi-Fi), `bluez-firmware`; PC/Armbian: `linux-image-*-x86`, `armbian-bsp-*`, `armbian-firmware` / `linux-firmware`, `grub-*`, `shim-signed`, `intel-microcode` | I: names to confirm with `dpkg -l` on each board (section 12) |
| desktop boards | the display manager and the desktop's X / Wayland server (`lightdm`, `labwc`, `xserver-xorg-core`): a postinst that starts the display manager would stop the launcher through `Conflicts=` | S: `cyberdeckzero-desktop.service:19`; the postinst behaviour I |

The deck matters most. It runs the panel through the **legacy firmware DPI path** (no KMS, `display_rotate` in
`config.txt`). A firmware or kernel update that changes that path can leave the panel dark on a board whose only screen
it is.

#### 7.4.2 Mechanisms compared

| Mechanism | How | For | Against |
| --- | --- | --- | --- |
| `apt-mark hold` | hold the protected packages | apt and unattended-upgrades honour it everywhere, the terminal too | silently blocks **security updates** of libc, OpenSSL, the kernel for good; `apt list --upgradable` still lists them (confusing); outlives the launcher unless `uninstall.sh` unholds; mixes with the user's own holds |
| apt pinning | a preferences file (`Pin-Priority` per package) | fine-grained | invisible, easy to get wrong, the same staleness; a stateless per-run variant (`-o Dir::Etc::PreferencesParts=`) is clever but hard to explain |
| **simulation + classification (recommended)** | `apt-get -s` of the exact command, every `Inst` / `Remv` classified against the protected set computed on the board; the run installs only the confirmed list with `--no-remove`, after a second simulation | no persistent state; the user's terminal apt is unchanged; the plan shown is the plan run; works the same on Debian and Ubuntu | protects only runs started from the launcher (a terminal `apt upgrade` is the user's choice) |
| **anchor package (recommended, with it)** | `install.sh` builds `cyberdeckzero-runtime` with `dpkg-deb --build` (arch `all`, version = the launcher's, `Depends:` the packages found in 7.4.1 **on this board at install time**, from `dpkg -S` of the libraries and tools); `uninstall.sh` purges it | apt then knows the launcher: `apt autoremove` (launcher or terminal) never removes those packages; any plan that would shows `Remv cyberdeckzero-runtime`, an easy red flag; `--no-remove` refuses it | one more install step; `Depends` must use the names found on the board (t64 names differ between releases) |

#### 7.4.3 Classes

| Class | Contents | From the launcher |
| --- | --- | --- |
| **S** safe | everything not below (tools, Python, apps' libraries, editors ...) | installed by default |
| **R** launcher runtime | the ELF closure and the Store's (7.4.1 rows 1-3), `curl` and `ca-certificates` | listed, unchecked; one switch "Include launcher libraries" with a warning (D14) |
| **N** session | `systemd`, `udev`, `dbus`, `polkitd`, `sudo`, `network-manager`, `wpasupplicant`, `bluez`, the display manager on desktop boards | as R. Upgrading `bluez` restarts `bluetoothd`: the Bluetooth keyboard drops for some seconds mid-run (I) |
| **K** boot chain | kernel, firmware, bootloader, EEPROM, initramfs, GRUB, shim, microcode | **terminal only** (D15): the row shows the command; `/boot/firmware` is FAT, so a power cut there can leave the board unbootable |
| **X** never | any plan with a removal, a new kernel ABI package (a new package needed), a release change, or a protected package pulled in as a dependency of a chosen one | refused; the package moves to "Kept back" with the reason |

On Raspberry Pi OS a kernel update usually needs a **new** versioned package (a new ABI), so `apt-get upgrade` keeps the
kernel back by itself (I: confirm on trixie). The rules above do not rely on it.

#### 7.4.4 Computing the set on each board

- **Dynamic, at check time, as the user** (cached until `/var/lib/dpkg/status` changes; I: a few seconds on the Zero 2 W):
  - `ldd` of the four binaries, resolved to packages with `dpkg-query -S`;
  - `dpkg-query -S` of the tools found with `command -v`;
  - the anchor package's `Depends`.
- **Static patterns** per family. Pi: `linux-image-*`, `raspi-firmware`, `rpi-eeprom*`, `firmware-*`, `initramfs-tools*`.
  PC: `linux-image-*`, `linux-modules-*`, `armbian-*`, `grub-*`, `shim*`, `*-microcode`, `linux-firmware`.
- **The session's display manager** on desktop boards (from `cyberdeckzero-desktop.service`'s `Conflicts=`).
- **User holds** (`apt-mark showhold`) are listed as "Held by you" and never touched.

#### 7.4.5 What apt cannot do

**There is no rollback.**
- A finished upgrade stays.
- An interrupted one leaves half-configured packages that `dpkg --configure -a` usually finishes.
- A broken boot chain needs another computer to repair the card.

The protection lowers the odds; it is not a guarantee, and the confirmation dialog says so in one line. What is
realistic:
- K stays out;
- removals are refused;
- the anchor package keeps `autoremove` honest;
- the health check exists only for the launcher's own update.

### 7.5 Time, disk and power on the Pi Zero 2 W (I, to measure)

- **Time.** `apt-get update` about 1-3 min. A point-release upgrade of 30-100 packages 10-40 min (`man-db` triggers
  alone take a minute or two).
- **RAM.** apt's cache needs some tens of MB next to the launcher's 20 MB of the 414 MB.
- **Disk.** `/var/cache/apt/archives` holds the download (the confirmation shows it); `apt-get clean` afterwards (the
  helper's last step).
- **Power.**
  - No battery on the Pi boards: the dialog says "Keep it plugged in: about 12 minutes".
  - The Pi 3A+ under-voltage case (`get_throttled 0x50005`, 2026-10-29) argues for refusing when
    `/sys/devices/platform/soc/soc:firmware/get_throttled` shows under-voltage now (I: path and bits per board).
  - Battery boards (the tablet, the HackBerry through `board.state battery=1`): refuse under 50 % unless charging (D16).

### 7.6 App updates

Already covered by Settings > Apps (Update, Update all, sources sync). The Updates page shows "App updates (N)" from the
same backend list and opens Settings > Apps > Installed (D19). The launcher update and the Store share the dpkg lock
and the busy flag.

## 8. Security and safety rules

**The launcher must never:**
- run apt or the update helper as root without the user's confirmation and password in that moment;
- download system packages, upgrade or reboot by itself (the background check reads GitHub only);
- run `dist-upgrade` / `full-upgrade`, remove a package, change `/etc/apt/sources*`, the suite or holds the user set;
- pass `--force` to `install.sh` or change `board.conf` itself;
- install a draft, a pre-release, another architecture, or a release whose hash does not match;
- downgrade, except by the rollback;
- start a root job while another package job runs, or (battery boards) on a low battery;
- kill a running `dpkg`.

**Shown before the confirmation:**
- **Launcher:**
  - versions from and to;
  - the size and that it is already checked;
  - "the launcher restarts and open apps close";
  - what is kept (settings, apps, `board.conf`);
  - that a failed start brings the old version back.
- **System:**
  - the number of packages per class and the list (name, old and new version);
  - the download size and the time estimate;
  - what is kept back and why (R, N, K, X, held by you);
  - "not cancellable once installing starts; apt cannot undo an upgrade";
  - the power line;
  - "restart advised afterwards" when R / N packages are included.

**Logged:**
- the helper's own log in `/var/log/cyberdeckzero/` and the transient units' journal;
- apt's own logs;
- `update.state` with the last check and the last result;
- the page's "Show log".

Nothing is sent anywhere except the GitHub request (a fixed User-Agent with the launcher version).

## 9. Options

| Option | What | For | Against | Risk | Cost (ws) |
| --- | --- | --- | --- | --- | --- |
| **A** launcher only | check, notes, What's new, install with backup, health check, rollback | the feature the boards need most (12 releases in 10 days, today installed by hand over ssh); fully reversible | apt untouched | low: the rollback covers the launcher's own failure; desktop boards fall back to the desktop | 3.5-5 |
| **B** A + apt list | B adds Refresh lists (password), counts, the classified list, the kept-back reasons, the terminal command | the user sees what is pending and what the launcher would protect, at no risk | no upgrade from the launcher | very low (the list refresh is `apt-get update` only) | +1.25-1.75 |
| **C** B + apt upgrade with protection | the S set (and R / N on request) in a transient unit, anchor package, repair after an interruption, reboot advice | the owner's full request | apt has no rollback; long runs on battery-less boards; a desktop board's display manager or `bluez` restarts mid-run | medium; contained by K terminal-only, `--no-remove`, the anchor, the second simulation | +2-2.75 |

## 10. Plan

| Phase | Option | Worker tier | Content | Cost (ws) | Owner test |
| --- | --- | --- | --- | --- | --- |
| P0 | all | none (Controller with the owner) | the read-only board checks of section 12 on the six boards; capture `apt-get -s upgrade` outputs as test fixtures | 0.1 | 10 min of reading outputs |
| P1 | A | savvy-medium | `update` model (semver order, release JSON via `cjson`, candidate choice, notes to text, the `update.state` file); curl check on a worker with ETag and the error map; native Updates page (Launcher section, What's new `TextBlock`, Check now, root row value); `build.sh` ships the notes in `payload/doc`; the toast after a version change; unit tests (the 13 notes files, semver, JSON fixtures); scenes at 640x480, 480x800, 480x640, 720x720 | 1.25-1.75 | 5 min: deck online and offline, notes readable by keys and touch; one portrait pass |
| P2 | A | savvy-heavy | `cyberdeckzero-update` helper (copy itself, re-check, `--dry-run` plan check, backup, `install.sh` replay, inhibit, health check through `board.state`, rollback, resume, status file, log); `install.sh` writes `install.conf` and a machine-readable dry-run plan; the launcher job (download with progress and cancel, sudo + `systemd-run`, progress page, exit 76 on desktop boards, failure and rollback screens); `uninstall.sh` cleanup; fake-root tests in the `tests/test-install-board.sh` style; a test hook `APPLAUNCH_UPDATE_URL` for a local fake release server | 2-2.75 | 15 min: deck (user service) update to a test release from the fake server, then a deliberately broken test release (rollback); tablet (desktop unit, amd64) one update |
| P3 | B | savvy-medium | `apt -s` / `--print-uris` parsers, the classifier and the per-board protected set, lists' age, Refresh lists (sudo + `systemd-run apt-get update`, Status-Fd progress), the System packages list (`CompactList`, sections, affordances), detection of unattended-upgrades / PackageKit / holds, App updates link row; tests on the P0 fixtures | 1.25-1.75 | 5 min: deck and tablet: refresh, read the list |
| P4 | C | savvy-heavy | the anchor package (`install.sh` / `uninstall.sh`, `dpkg-deb`); the upgrade helper (second simulation, download phase with cancel, install phase, inhibit, lock timeout, `confold`, `NEEDRESTART_MODE=l`, `apt-get clean`); busy flag shared with Settings > Apps; repair after an interruption; reboot advice; battery and under-voltage guards; logs | 2-2.75 | 20-40 min mostly waiting: one Pi on mains (Pi 4 B), then the tablet |
| P5 | all | docs-writer | README "Updates" section, `pizero2w/README.md` (install.conf, the anchor package), release checklist (notes marker, notes in the bundle, sha256 assets stay, the body is the notes file), ROADMAP | 0.3-0.5 | - |

**Totals:**

| Path | Phases | Cost (ws) |
| --- | --- | --- |
| A | P1 + P2 + P5 | 3.5-5 |
| B | A + P3 | 4.75-6.75 |
| C | B + P4 | 6.75-9.5 |
| P0 | - | negligible |

One release (with notes) between P1 and P2 lets P2 be tested against a real newer release as well as the fake server.

## 11. Tests: PC and boards

**On the PC** (WSL build tree, `tools/dev/check-all.sh`):
- **Unit tests (plain `assert`, `projects/APPLaunch/tests/run_tests.sh`):**
  - semver order;
  - the release JSON (fixtures: real API answers saved from `gh api`, a draft, a pre-release, an amd64-less release,
    a 403 rate-limit body);
  - asset choice;
  - the notes converter on all 13 files plus edge cases (CRLF, comments, tables, nested bullets, the end marker);
  - `update.state` read and write;
  - the apt simulation parser (the P0 captures: kept back, `Remv`, held, a kernel ABI change) and the classifier;
  - the plan check ("simulated differs from confirmed").
- **Render harness scenes** with fake services (`tests/render/src/harness_services.cpp` fixtures):
  - Updates: checking, up to date, available, offline, rate-limited, development build;
  - What's new: one version, three versions;
  - the confirm dialog, the progress page, rollback and failure;
  - System packages: stale lists, the classified list, nothing to do.

  At 640x480, 480x800, 480x640, 720x720 and 1280x800; 480x320 not needed.
- **Helper and installer tests** in a fake root (`INSTALL_FAKE_SYSTEM=1`, stand-ins for `systemctl`, `runuser`, `dpkg`,
  `apt-get`, `systemd-run`):
  - backup;
  - options replay (keyboard name, `--board pi3a-wsdsi43-portrait`, `--no-boot-config`);
  - dry-run refusal on a variant switch;
  - health check passing and timing out, then rollback;
  - the desktop path (exit 76);
  - resume after an interruption;
  - the anchor package's `Depends` from a fake `dpkg -S`.
- **The curl path** against a local `python3 -m http.server` serving a fake API and tarball (the `APPLAUNCH_UPDATE_URL`
  hook).

**Only on boards:**
- real TLS to GitHub with the board's clock;
- the sudo prompt and `systemd-run` from the user service and from the desktop unit;
- the restart of each variant;
- what the panel shows during the restart;
- the times on the Zero 2 W (download, unpack, the layout of a 24 KB notes text);
- real apt lists;
- unattended-upgrades or PackageKit present;
- `bluez` restarting under a Bluetooth keyboard;
- an upgrade's length on battery-less power.

Short protocols (quality bar): the deck for the main path, one pass on the tablet (amd64, desktop unit).

## 12. Read-only board checks (to run later, after the 0.8.12 rollout)

On each board, as the user, no `sudo` needed except where marked:

1. `cat /etc/os-release; uname -m; uname -r` (Debian 13 or Ubuntu 26.04, kernel flavour).
2. `apt-mark showhold` (existing holds).
3. `dpkg -l unattended-upgrades packagekit needrestart update-notifier-common 2>/dev/null | grep ^ii` and
   `systemctl is-enabled apt-daily.timer apt-daily-upgrade.timer unattended-upgrades.service packagekit.service` and
   `apt-config dump | grep -i periodic`.
4. `id -nG` (sudo group), `ls /etc/sudoers.d/`, `sudo -n true && echo NOPASSWD` (non-interactive: fails without
   asking when a password is needed).
5. `command -v curl systemd-run sqv gpgv sha256sum dpkg-deb; curl --version | head -1; dpkg -l ca-certificates | tail -1`.
6. `df -h / /boot/firmware /var/cache; findmnt /tmp` (free space; is `/tmp` a tmpfs).
7. `free -m`.
8. `ls -l --time-style=+%F /var/lib/apt/lists/*InRelease` (lists' age).
9. `apt-get -s upgrade > ~/apt-sim.txt; tail -3 ~/apt-sim.txt` and `apt-get -s dist-upgrade > ~/apt-sim-dist.txt`
   (simulation, no lock, no change: fixtures for P3).
10. `dpkg --audit` (must print nothing).
11. `ls /etc/apt/sources.list.d/; cat /etc/apt/sources.list.d/*.sources` (repositories: Debian, Raspberry Pi,
    Armbian).
12. `dpkg -l | grep -E 'linux-image|raspi-firmware|rpi-eeprom|initramfs|armbian-|grub-|firmware-'` (boot-chain names).
13. `grep -o 'ATTRS{name}=="[^"]*"' /etc/udev/rules.d/99-bt-keyboard.rules; head -3 /etc/applaunch/board.conf`
    (keyboard name; generated or hand-edited profile).
14. `ldd /usr/share/APPLaunch/bin/cyberdeckzero | awk '/=>/{print $3}' | xargs -r dpkg -S 2>/dev/null | cut -d: -f1 | sort -u`
    (the R class as it is on that board).
15. `timedatectl show -p NTPSynchronized` (clock state at a typical moment).
16. Pi only: `grep -E '^(dtoverlay|display_rotate|auto_initramfs)' /boot/firmware/config.txt` (legacy display path,
    initramfs); `vcgencmd get_throttled` (under-voltage bits).

## 13. Risks and interactions

- **`sudo` inside the launcher's cgroup.** Any root job started as a plain child dies with the launcher. This is the
  first rule of P2 and P4 (`systemd-run`).
- **Re-running `install.sh` without the first options** changes the keyboard link name, the PDA profile and the boot
  configuration (section 5.8). `install.conf` plus the dry-run refusal handles it.
- **A variant switch or a migration** planned by `install.sh` must never happen unattended (step 5 refuses).
- **Development builds** (`0.0.0-dev`) would see every release as newer: channel rule D4.
- **Open apps close** on every launcher restart (multitasking residents too): said in the dialog, as PDA mode does.
- **Store jobs and system upgrades share dpkg:** one busy flag in the launcher, the lock timeout outside it. The Store's
  `apt-get install <deb>` can upgrade a protected library (residual).
- **Desktop boards:** a display-manager package's postinst could start the display manager and stop the launcher
  (`Conflicts=`). The run survives in its own unit; the display manager stays in class N.
- **Bluetooth keyboard** drops while `bluez` restarts (class N).
- **Power cut** during dpkg: repair offered on the next start. During a boot-chain write: why K is terminal-only.
- **GitHub changes** (API fields, the `digest` field is recent, rate limits): the `.sha256` asset stays the
  reference, and the parser ignores unknown fields.
- **The config store** is near its 64-key cap: the update state goes to its own file.
- **Release discipline:** the notes need the end marker, and the notes file must be in the bundle. Both are release
  checklist lines (P5).

## 14. Not verified

- **No build, no board run, no measurement.** The times, RAM and the 24 KB `TextBlock` layout on the Zero 2 W are
  estimates.
- **Packages and services on the boards:**
  - whether unattended-upgrades, PackageKit, `needrestart`, `sqv` / `gpgv` are present per board;
  - whether `/tmp` is a tmpfs on Raspberry Pi OS;
  - the exact boot-chain package names;
  - whether the first user has NOPASSWD sudo (section 12).
- **apt details to confirm on trixie:**
  - `--print-uris` as a non-root user;
  - whether `--only-upgrade` can still pull a new dependency (the second simulation catches it either way);
  - the kernel kept back by `apt-get upgrade` on Raspberry Pi OS.
- **The `cjson` component** builds in the amd64 native configuration (only read in `SDK/components`).
- **Restart behaviour:**
  - that the fbdev panel keeps the old launcher's last frame during the restart;
  - systemd's start limit for the user unit (its default, not set in `cyberdeckzero.service`).
- **LVGL 9.5 recolor escaping** of `#`, and span performance (only if D9 asks for styling).
