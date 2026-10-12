# Hosting your own apps for the deck

The deck can install apps from **any public GitHub repository** that follows a simple layout. On the deck, open
**Settings > Apps > Sources > + Add a GitHub source**, type `owner/repo`, and the apps of that repository appear
under **Settings > Apps > Available**, where each one installs with Enter or a tap; installed apps are listed under
**Settings > Apps > Installed**, where each one can be updated or removed.

Settings > Apps is meant for **your own** (sideloaded) sources. The CardputerZero Hub catalogue stays in the
**Store** app and is not shown there.

**The project's apps come pre-added.** A fresh install (`install.sh`) adds `OSRdesign/cyberdeck-zero-apps` (LAN Scan,
viz1090, Wi-Fi Survey, Mesh Hop, ...) once, and only for a user who has no source yet, then syncs it: Settings > Apps >
Available lists its apps without typing anything. It is an ordinary source: switch it off or remove it in Sources, and
the next `install.sh` leaves it that way; a list with sources of your own is never changed. Installed while offline, it
shows after a sync (Sources > Sync my sources). Details: `projects/APPLaunch/pizero2w/README.md`, "Default app source".

The **Store** tile itself needs a keyboard; on a touch-only board hide it in **Settings > Launcher** (Store). That does
not change Settings > Apps, which uses the Store's backend directly.

The complete, step by step guide, the build tools and a working example live in the template repository:

**<https://github.com/OSRdesign/cyberdeck-zero-apps>** (use it as a template or fork it)

## In short

1. Create a **public** GitHub repository (default branch `main`) from the template.
2. Build your app as an **aarch64 Linux executable** drawing a 320x170 picture (copy the
   `apps/lanscan/src` project of the apps repo as a starting point; the launcher shows it scaled 2x), or any
   program that runs on the Pi.
3. Describe it in `apps/<id>/app.json` (name, version, description) and lay its files out in
   `apps/<id>/root/` exactly as they must be on the device:
   ```
   usr/share/APPLaunch/bin/M5CardputerZero-<id>            the executable
   usr/share/APPLaunch/applications/<id>.desktop           the launcher entry
   usr/share/APPLaunch/share/images/<id>.png               the tile icon
   ```
4. Run `python3 tools/make_registry.py`. It builds the `.deb` package (pure Python, no dpkg needed) and writes
   `registry.json` with the download URL, MD5, SHA-256 and size.
5. `git add -A && git commit && git push`. No GitHub Pages and no release are required.
6. On the deck: Settings > Apps > Sources > + Add a GitHub source > `your-name/your-repo`.

To publish an update, raise `version` in `app.json`, run `tools/make_registry.py` again and push; the deck shows
the update after a sync (Settings > Apps > Sources > Sync my sources).

## Format and behaviour

* The registry format is the CardputerZero Store registry (schema 2), so the same repository also works as a
  registry in the Store app. The deck downloads `registry.json`, checks the MD5 of each `.deb` and installs it
  with `dpkg`.
* Installing needs the deck user's **sudo password** (typed with the keyboard) because the package is installed
  as root. Removing an app asks for it too.
* Settings > Apps uses the Store's backend (`/usr/share/APPLaunch/bin/M5CardputerZero-AppStore`), so the Store
  must be installed (it is, with the release bundle built with `--with-store`).
* Accepted source addresses: `owner/repo`, `github.com/owner/repo`, `https://github.com/owner/repo` (optionally
  `/tree/<branch>`), or the direct address of a `registry.json`.

## Using Settings > Apps on the deck

**Levels.** Settings > Apps lists **Installed (N)**, **Available (M)** and **Sources**; the counts follow every
install, removal and sync. **Installed** lists the installed apps; Enter or a tap on one opens its actions (Update
when an update is offered, Remove, Cancel). **Available** lists the apps of your enabled sources that are not
installed, with the version offered; Enter or a tap installs one at once. While a job runs, its app's row shows a
spinner. Empty levels say `No apps installed` or `Nothing to install` (`Loading...` before the first list).

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
| Enter | open a level; an installed app's actions (Installed); install (Available); on/off (Sources); yes in a confirmation |
| `U` | update all (Installed) |
| `S` | sync (any list) |
| `A` | add a source (Sources) |
| `D` or Del | remove the selected app (Installed) or source (Sources) after a confirmation; the built-in source cannot be removed |

The letters are read from the physical key, so `W`, `E`, `R` and `T` never move the selection or change levels. `F`, `X`, `Z` and `C` still act as up, down, left and right.

## Why a source survives opening the Store

The Store keeps its own copy of the source list in the launcher's `config.json` (keys `appstore.registry.*`) and, every
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
(the Esc-hold watchdog still applies). The launcher's top bar (clock, Wi-Fi bars, Bluetooth) is one shared
renderer, `ext_components/cp0_lvgl/{include/cp0_statusbar.h,src/cp0/cp0_statusbar.c}`, so a full-screen app can
draw exactly the same bar into its own frame. An LVGL app that links `cp0_lvgl` gets it ready-made as a widget,
with the launcher's title: `cp0::ui::StatusStrip::add(header)` and `cp0::ui::add_title()` (`cp0_ui_widgets.hpp`,
API in `docs/cp0_lvgl.en.md`); it keeps no copy. The viz1090 package in the apps repository is a complete example
of a non-LVGL app (SDL2 programs run through a small display and input bridge; it also draws a close button).

The panel is not always 640x480 (the Pi 3A+ is 480x320 rotated at 16 bpp, the HackBerry 720x720): a full-screen app
should take its size, rotation, depth and framebuffer from the app contract (next section) instead of hard-coding
them. If it really needs a minimum size, add `X-MinSize=WxH` (logical pixels, for example `X-MinSize=640x480`): on a
smaller screen the launcher hides its tile (and it does not appear in Settings > Apps either). An older launcher
ignores the key, so the app must still start there and say that the screen is too small. New native (LVGL) apps
declare `X-MinSize=640x480` by default (decision of 2026-10-13): they are hidden on the 480x320 Pi 3A+, while
text-mode apps (`Terminal=true`) may still run there.

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
| Backend access (sources, catalogue, install flow) | `projects/APPLaunch/main/ui/settings/apps_backend.{hpp,cpp}` |
| Default source at install (the Store's `registries.json`, once per user) | `projects/APPLaunch/pizero2w/default-sources.sh`, run by `install.sh` |
| The Store's shared source list (mirror) and the default source | `settings_apps_registry_mirror.{hpp,cpp}` (pure), `settings_apps_jobs.cpp` (adapter), test `tests/test_settings_apps_registry_mirror.cpp` |
| Example app | `apps/lanscan/` in the apps repo |
| Switch (off on the original CardputerZero build) | `APPLAUNCH_SETTINGS_APPS` in `settings_hw_profile.hpp` |
