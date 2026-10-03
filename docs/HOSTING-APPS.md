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
2. Build your app as an **aarch64 Linux executable** drawing a 320x170 picture (copy
   [`projects/LanScan`](../projects/LanScan) as a starting point; the launcher shows it scaled 2x), or any
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

## Trust

An installed package runs as root during installation and the app runs with the deck user's rights. Only add
sources you trust. The MD5 in a registry protects against a damaged download, not against a malicious author.

## Where things are in this repository

| Part | Files |
| --- | --- |
| Settings > Apps page | `projects/APPLaunch/main/ui/settings/settings_apps_page.{hpp,cpp}` |
| Backend access (sources, catalogue, install flow) | `projects/APPLaunch/main/ui/settings/apps_backend.{hpp,cpp}` |
| Example app | `projects/LanScan/` |
| Switch (off on the original CardputerZero build) | `APPLAUNCH_SETTINGS_APPS` in `settings_hw_profile.hpp` |
