# Hosting your own apps for the deck

CyberdeckZero Launcher can install apps from **any public GitHub repository** that follows a simple layout. On the deck, open
**Settings > Apps > Sources > + Add a GitHub source**, type `owner/repo`, and the apps of that repository appear
under **Settings > Apps > Available**, where each one installs with Enter or a tap; installed apps are listed under
**Settings > Apps > Installed**, where each one can be updated or removed.

Settings > Apps is meant for **your own** (sideloaded) sources. The CardputerZero Hub catalogue is not shown there
(its apps are 320x170 keyboard apps; the Store screen that lists them is on the `small-screen` branch only).

**The project's apps come pre-added.** A fresh install (`install.sh`) adds `OSRdesign/cyberdeck-zero-apps` (14 apps:
Cassette, Deck Atlas, Home Assistant, Klondike Solitaire, Markdown Notes, Mesh Hop, Mine Field, mmWave Radar, Net Toolbox,
Net Walk, RSS Reader, Serial Console, System Monitor, viz1090) once, and only for a user who has no source yet, then syncs it: Settings > Apps >
Available lists its apps without typing anything. It is an ordinary source: switch it off or remove it in Sources, and
the next `install.sh` leaves it that way; a list with sources of your own is never changed. Installed while offline, it
shows the next time Settings > Apps opens with a network (or after Sources > Sync my sources). Details:
`projects/APPLaunch/pizero2w/README.md`, "Default app source".

The three newest, by their `summary` in the apps repository: **Cassette**, "A tape-deck sound recorder: WAV, FLAC, Opus, USB and
Bluetooth headsets."; **Deck Atlas**, "Offline maps on the deck: your MBTiles, PMTiles and tile folders."; **mmWave Radar**, "See
people move: a viewer for the Hi-Link LD2452 and LD2451 radars."

Since v0.9.0 main has no **Store** tile and no Store switch in Settings > Launcher: the Store program stays in the
bundle as the back end of Settings > Apps, with no screen of its own. The Store screen and the 320x170 apps live on
the `small-screen` branch (README, "Branches").

The complete, step by step guide, the build tools and a working example live in the template repository:

**<https://github.com/OSRdesign/cyberdeck-zero-apps>** (use it as a template or fork it)

## In short

1. Create a **public** GitHub repository (default branch `main`) from the template.
2. Build your app as an **aarch64 Linux executable**. A new app is a native full-screen app built from
   `apps/cyberdeck-app-template` of the apps repo on `cp0_lvgl`, for the screens it supports (it declares its own
   minimum, `X-MinSize`; see "Full-screen apps" below); its `docs/APP-GUIDELINES.md` gives the rules. Any other program that runs on the Pi works too; the older apps draw a
   320x170 picture that the launcher scales into its window.
3. Describe it in `apps/<id>/app.json` (name, version, description) and lay its files out in
   `apps/<id>/root/` exactly as they must be on the device. **The descriptor rule** (every app): a `summary` of one
   line, at most 80 characters, and a `description`; summary + newline + description at most 500 characters (counted
   in characters, not bytes). That is all Settings > Apps shows (the Description row and the page before installing),
   and it cuts a longer text at 500 with dots. The long documentation goes in the app's `README.md` in the repository.
   The apps repo's `tools/check_descriptors.py` checks every `app.json` and registry entry against the rule.
   ```
   usr/share/APPLaunch/bin/<id>                            the executable (named after the app id)
   usr/share/APPLaunch/applications/<id>.desktop           the launcher entry
   usr/share/APPLaunch/share/images/<id>.png               the tile icon
   ```
4. Run `python3 tools/make_registry.py`. It builds the `.deb` package (pure Python, no dpkg needed) and writes
   `registry.json` with the download URL, MD5, SHA-256 and size.
5. `git add -A && git commit && git push`. No GitHub Pages and no release are required.
6. On the deck: Settings > Apps > Sources > + Add a GitHub source > `your-name/your-repo`.

To publish an update, raise `version` in `app.json`, run `tools/make_registry.py` again and push; the deck shows
the update the next time Settings > Apps opens more than 10 minutes after its last check (see "Checking for updates"
below), or at once after Settings > Apps > Sources > Sync my sources (key `S`).

## Format and behaviour

* The registry format is the one of M5Stack's CardputerZero Store (schema 2), so the same repository also works as a
  registry in the Store app (on the `small-screen` branch). The deck downloads `registry.json`, checks the MD5 of each `.deb` and installs it
  with `dpkg`.
* Installing needs the deck user's **sudo password** (typed with the keyboard) because the package is installed
  as root. Removing an app asks for it too.
* Settings > Apps uses the Store's backend (`/usr/share/APPLaunch/bin/M5CardputerZero-AppStore`), so the Store
  must be installed (it is, with the release bundle built with `--with-store`).
* Accepted source addresses: `owner/repo`, `github.com/owner/repo`, `https://github.com/owner/repo` (optionally
  `/tree/<branch>`), or the direct address of a `registry.json`. The `owner/repo` forms name `registry.json` on
  arm64 and `registry-<arch>.json` (`registry-amd64.json`) on any other architecture
  (`apps_backend::normalize_source`, `default-sources.sh`); a full address is never rewritten.

## Using Settings > Apps on the deck

**Levels.** Settings > Apps lists **Installed (N)**, **Available (M)** and **Sources**; the counts follow every
install, removal, sync and check. **Installed** lists the installed apps; Enter or a tap on one opens its actions
(Description when the app has one, Update when an update is offered, Remove, Cancel). **Available** lists the apps of
your enabled sources that are not installed, with the version offered; Enter or a tap opens the app's page
(Description, Install, Cancel), and Install installs it. The **Description** row shows the first line; Enter or a tap
opens the whole text in a read-only page (plain, wrapped, scrollable), cut at 500 characters with dots (see the
descriptor rule above). While a job runs, its app's row shows a spinner. Empty levels say `No apps installed` or `Nothing to install` (`Loading...` before the first list).

**Apps that fit the screen** (v0.9.2). Available (and its count) leaves out an app whose registry entry has a
`min_size` larger than the screen in either dimension; a portrait screen is judged turned (480x800 as 800x480), as a
landscape app runs there, so orientation hides nothing. An entry without `min_size`, or with a value the launcher cannot
read, is listed. Installed and the update count are not filtered. An app's page (Installed or Available) starts with
two read-only rows from its registry entry: **Screen** (`Landscape`, or `Landscape and portrait` when it has a
`portrait_min_size`) and **Minimum size** (the `min_size`). The apps repo's `tools/make_registry.py` copies both values
from the packaged `.desktop` (`X-MinSize`, `X-Portrait`). The filter reads the lists the Store backend cached at the last
sync or check.

**Checking for updates** (the check when the page opens). The lists come from the copy of each source's
`registry.json` the Store backend saved at its last download (`~/.cache/cardputerzero-appstore/registry-<hash>.json`,
its time in the `synced_at` field). Opening Settings > Apps shows that copy at once; then, in the background, the
launcher downloads again every enabled source whose copy is older than **10 minutes** (GitHub serves raw files cached
for 5 minutes, so a shorter time gains nothing), when the board has a network (a default route). The rows change where
they are (the highlight and the scroll stay; a newer version shows as the yellow `0.1.2>0.1.3` marker as usual) and
the status line under the title says what happened:

| Shown | Meaning |
| --- | --- |
| `Checking for updates...` | the download runs (installs and syncs wait for it; moving around the page works) |
| `Up to date (just now)` | the lists were downloaded now |
| `Updated just now (N failed)` | some sources were downloaded now, N of them failed (their saved lists stay; Sources shows them `failed` with the reason) |
| `Updated N min ago` | nothing was older than 10 minutes, or it was tried a moment ago: the lists are that old (the line follows the clock while the page stays open; ` (N failed)` is added while the failed sources of that check have not come back) |
| `Offline: showing the list from <age> ago` | no network, or every download failed: the saved lists stay (no error box) |

The line wraps when it is long; it keeps clear of the list's up arrow (beside it, or under it when it would need more
than three lines there, as on a 320 px wide screen).

It happens at most once per opening, never again within 10 minutes of the last try (a failed source is not retried at
every opening), never when the launcher starts and never on a timer. A source that is switched off is never
contacted. One download may take up to 20 seconds before it counts as failed. A source that failed shows `failed`
in Sources with its reason when highlighted, as after a sync. The Store backend has no conditional requests: a check
downloads each due `registry.json` again in full, and only the icons of new apps.

**The setting.** Sources ends with **Auto-check updates**, `On` (the default) or `Off`; Enter or a tap flips it, also
while a job runs. It is stored in `~/.config/cardputerzero/config.json` as `apps_auto_check` (`0` Off,
`1` On; absent means On, written only when changed) and applies from the next opening. Off: Settings > Apps never
downloads by itself and shows no such line, as in earlier versions; Sync my sources is then the only way to fetch.

**Sync my sources** (the row, key `S`, or a source's own Sync) is the **forced** refresh: it downloads every enabled
source at once, whatever its age, the setting or the last try, and shows each source's progress and errors (below).

**Versions and updates.** Installed shows each app's installed version and, when the sources offer a newer one
(compared the Debian way, as `dpkg --compare-versions` does, not as text), both, for example `0.1.2>0.1.3`, in
yellow; its actions then offer **Update**. When more than one app has an update, an **Update all (N)** row heads
Installed (key `U`); it updates the apps one after the other and asks for the sudo password once per app. An app
that is current shows only its version.

**Tile position.** An upgraded or reinstalled app keeps its place on the home grid. The launcher stores the tile
order as `app_order` (a list of keys made from the `.desktop` file names) in
`~/.config/cardputerzero/config.json`. An app that is new goes last.

**Sync progress.** Syncing (Sync row or key `S`) goes through the sources one by one. Each row shows `waiting`,
`syncing...`, then `done` or a red `failed`; the footer shows `Syncing 3/7 <source>`. Selecting a failed row shows
the reason, followed by `(cached list)` when the apps of the last good sync stay listed:

| Shown | Meaning |
| --- | --- |
| `HTTP <code>` | the server answered with an error, for example `HTTP 404` (file not found) |
| `Connection refused` | nothing listens at that address and port |
| `Timed out` | no answer within the time limit |
| `No network` | the host name cannot be resolved, or there is no route |
| `Bad JSON` | the answer is not a valid registry (an HTML page, for example) |
| `Connection failed` | any other network error (connection reset, empty reply) |

A failed source never hides the apps of the other sources. When every source is finished the footer is green
(`Synced N sources`), amber if some failed (`Synced 2 of 7 sources`) or red if all failed (`Sync failed (5)`).

**Install, update and remove.** The footer shows the current step with a counter and the seconds, for example
`Downloading LAN Scan (1/2) 5s`, then a success line (`Installed ...`, `Updated ... to ...`, `Removed ...`). A
failure opens a panel with a short headline and the reason (a key press or a tap closes it): `Not enough space`,
`Download failed`, `Unfinished install`, `Could not prepare`, a checksum mismatch, or `dpkg error` with the reason
dpkg gave. A wrong sudo password is asked again up to three times, then the line turns red (`Wrong password`);
Esc at the password prompt gives a red `Cancelled`.

**Keys.**

| Key | Action |
| --- | --- |
| Up / Down | move the selection |
| Right | open the selected level (Apps root only) |
| Left / Esc | back one level (Installed / Available / Sources -> Apps -> Settings), or no in a confirmation |
| Enter | open a level; an installed app's actions (Installed); an available app's page (Available); on/off (a source, or Auto-check updates, in Sources); yes in a confirmation |
| `U` | update all (Installed) |
| `S` | sync, the forced refresh (any list) |
| `A` | add a source (Sources) |
| `D` or Del | remove the selected app (Installed) or source (Sources) after a confirmation; the built-in source cannot be removed |

The letters are read from the physical key, so `W`, `E`, `R` and `T` never move the selection or change levels. `F`, `X`, `Z` and `C` still act as up, down, left and right.

## Why a source survives opening the Store

(On main the Store screen cannot be opened since v0.9.0; this matters on the `small-screen` branch, and main keeps
the mirror.) The Store keeps its own copy of the source list in the launcher's `config.json` (keys `appstore.registry.*`) and, every
time it starts, writes that copy over the backend's `registries.json`. A source that exists only in the backend file
would be wiped the next time the Store opens. So Settings > Apps mirrors the backend list into those keys, in the
Store's own format, after every add, remove, on / off and sync, once shortly after the launcher starts (local only: no
network, so a source `install.sh` seeded survives opening the Store first), and again when Settings > Apps opens (the backend
list always wins; an empty list is never written over a non-empty one). A source you add in Settings > Apps is
therefore still there after you open the Store.

**The default source.** The first time Settings > Apps opens, if you have no source of your own, it adds
`OSRdesign/cyberdeck-zero-apps` and syncs it (the footer shows `Adding ...`), then records the decision once as
`apps_default_source` in `config.json`. If you already have a source of your own, only the decision is recorded. A
source you remove later is not added back. Without network the add fails, the footer says why, and the next opening
tries again. `install.sh` still seeds the same source in the backend file (the two are idempotent by URL).

## Full-screen apps (Pi port)

An app can take over the whole 640x480 panel instead of running in the scaled 320x170 window with the toolbar:
add `X-Fullscreen=true` to its `.desktop` file. The launcher then stops drawing and waits for the app to exit
(the Esc-hold watchdog still applies). The launcher's top bar (clock, Wi-Fi bars, Bluetooth, a battery, the SSH
indicator) is one shared renderer, `ext_components/cp0_lvgl/{include/cp0_statusbar.h,src/cp0/cp0_statusbar.c}`, so a
full-screen app can draw exactly the same bar into its own frame; the values come from the launcher's `status.state`
(`docs/APP-CONTRACT.md` section 5b: `ssh=` and `ssh_act=` for the SSH indicator). An LVGL app that links `cp0_lvgl` gets it ready-made as a widget,
with the launcher's title: `cp0::ui::StatusStrip::add(header)` and `cp0::ui::add_title()` (`cp0_ui_widgets.hpp`,
API in `docs/cp0_lvgl.en.md`); it keeps no copy. The viz1090 package in the apps repository is a complete example
of a non-LVGL app (SDL2 programs run through a small display and input bridge; it also draws a close button).

The panel is not always 640x480 (the Pi 3A+ is 800x480 with its DSI panel or 480x320 rotated at 16 bpp with its
SPI panel, the HackBerry 720x720): a full-screen app
should take its size, rotation, depth and framebuffer from the app contract (next section) instead of hard-coding
them. If it really needs a minimum size, add `X-MinSize=WxH` (logical pixels, for example `X-MinSize=640x480`): on a
smaller screen the launcher hides its tile, and Settings > Apps > Available leaves it out through the registry's
`min_size` (above). An older launcher
ignores the key, so the app must still start there and say that the screen is too small. There is no global minimum screen size: each
app declares what its own layout needs (decision of 2026-11-12, apps repo PR #150). Of the 14 apps of the project's registry, five
declare `480x320` (Cassette, Deck Atlas, Home Assistant, mmWave Radar, Net Walk) and the rest `640x480`. A tile is hidden only
where the screen is smaller than the app's own value; text-mode apps (`Terminal=true`) may still run on any screen.

## Responsive apps (the app contract)

Every app the launcher starts is told, through environment variables and two small state files, which mode it runs
in (`compat` or `fullscreen`), where its picture is on the screen, how many pixels it draws (`APPLAUNCH_CANVAS`) and
at what scale, the panel's size, rotation, density (px/mm) and screen class, the launcher's design tokens, and the
on-screen keyboard rule. A compat app still draws 320x170 but can size things in millimetres; a full-screen app can
use the whole panel at any size. The reference, with a copy-ready C reader (no LVGL), is
[`docs/APP-CONTRACT.md`](APP-CONTRACT.md); the test app `tools/dev/app-contract-probe` shows what a board tells its
apps.

## Trust

An installed package runs as root during installation and the app runs with the deck user's rights. Only add
sources you trust. The MD5 in a registry protects against a damaged download, not against a malicious author.

## Where things are in this repository

| Part | Files |
| --- | --- |
| Settings > Apps page | `projects/APPLaunch/main/ui/settings/settings_native_apps_page.{hpp,cpp}` (workers: `settings_apps_jobs.*`, rules: `settings_apps_model.*`) |
| Check for updates when the page opens (TTL, due sources, offline, the lines, the setting) | `settings_apps_autocheck.{hpp,cpp}` (pure), `settings_apps_jobs.cpp` (`task_auto_check`), test `tests/test_settings_apps_autocheck.cpp`, scenes `settings_native_apps_autocheck*` |
| Backend access (sources, catalogue, install flow) | `projects/APPLaunch/main/ui/settings/apps_backend.{hpp,cpp}` |
| Default source at install (the Store's `registries.json`, once per user) | `projects/APPLaunch/pizero2w/default-sources.sh`, run by `install.sh` |
| The Store's shared source list (mirror) and the default source | `settings_apps_registry_mirror.{hpp,cpp}` (pure), `settings_apps_jobs.cpp` (adapter), test `tests/test_settings_apps_registry_mirror.cpp` |
| Example app | `apps/cyberdeck-app-template/` in the apps repo (native); `apps/lanscan/` (320x170 compat) |
| Switch (off in the default profile, a build without `APPLAUNCH_HW=pizero2w`) | `APPLAUNCH_SETTINGS_APPS` in `settings_hw_profile.hpp` |
