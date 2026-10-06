# Hosting your own apps for the deck

The deck can install apps from **any public GitHub repository** that follows a simple layout. On the deck, open
**Settings > Apps > Sources > + Add a GitHub source**, type `owner/repo`, and the apps of that repository appear
in the **Apps** tab, where each one can be installed or removed.

Settings > Apps is meant for **your own** (sideloaded) sources. The CardputerZero Hub catalogue stays in the
**Store** app and is not shown there.

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

**Versions and updates.** The Apps tab shows each app's installed version and the version the sources offer. When
the offered one is newer (compared the Debian way, as `dpkg --compare-versions` does, not as text) the row shows
both, for example `0.1.2>0.1.3`, in yellow, and Enter runs **Update**. When more than one app has an update, an
**Update all (N)** row appears (key `U`); it updates the apps one after the other and asks for the sudo
password once per app. An app that is current shows only its version.

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
| Left / Right | Apps tab / Sources tab |
| Enter | install or update (Apps); on/off (Sources); yes in a confirmation |
| Esc | back, or no in a confirmation |
| `U` | update all (Apps tab) |
| `S` | sync (either tab) |
| `A` | add a source (Sources tab) |
| `D` or Del | remove the selected app (Apps tab) or source (Sources tab) after a confirmation; the built-in source cannot be removed. A tap on an up-to-date app does nothing |

The letters are read from the physical key, so `W`, `E`, `R` and `T` no longer move the selection or switch tabs,
as they did before. `F`, `X`, `Z` and `C` still act as up, down, left and right.

## Full-screen apps (Pi port)

An app can take over the whole 640x480 panel instead of running in the scaled 320x170 window with the toolbar:
add `X-Fullscreen=true` to its `.desktop` file. The launcher then stops drawing and waits for the app to exit
(the Esc-hold watchdog still applies). The launcher's top bar (clock, Wi-Fi bars, Bluetooth) is one shared
renderer, `ext_components/cp0_lvgl/{include/cp0_statusbar.h,src/cp0/cp0_statusbar.c}`, so a full-screen app can
draw exactly the same bar into its own frame. The viz1090 package in the apps repository is a complete
example (SDL2 programs run through a small display and input bridge; it also draws a close button).

## Trust

An installed package runs as root during installation and the app runs with the deck user's rights. Only add
sources you trust. The MD5 in a registry protects against a damaged download, not against a malicious author.

## Where things are in this repository

| Part | Files |
| --- | --- |
| Settings > Apps page | `projects/APPLaunch/main/ui/settings/settings_apps_page.{hpp,cpp}` |
| Backend access (sources, catalogue, install flow) | `projects/APPLaunch/main/ui/settings/apps_backend.{hpp,cpp}` |
| Example app | `apps/lanscan/` in the apps repo |
| Switch (off on the original CardputerZero build) | `APPLAUNCH_SETTINGS_APPS` in `settings_hw_profile.hpp` |
