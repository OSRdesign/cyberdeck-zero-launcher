# App packaging checklist (`apps-dev`)

1. Source in `apps/<id>/src/` (or `build/` when it wraps upstream software), a build script that works from a
   fresh checkout, and nothing app-specific added to the launcher repo.
2. `apps/<id>/app.json` complete: `share_code`, `package`, `version` (raised for every release), `title`, `summary`,
   `description`, `categories`, `author`, `license`, `source_repo`, `depends` (Debian packages the app needs),
   `permissions`. Descriptor rule (every app): `summary` one line, at most 80 characters; summary + newline +
   `description` at most 500 characters (characters, not bytes). Settings > Apps shows only that and cuts at 500 with
   dots; the long documentation goes in `apps/<id>/README.md`. `python3 tools/check_descriptors.py` exits 0.
3. `apps/<id>/root/` laid out as it must land: `usr/share/APPLaunch/bin/<id>` (the binary is named after the app id),
   `usr/share/APPLaunch/applications/<share_code>.desktop` (absolute `Exec`, `Icon=share/images/<id>.png`; add
   `X-Fullscreen=true` only for a full-screen app; a new native (LVGL) app declares `X-MinSize=640x480` by default,
   decision of 2026-10-13; any other app adds `X-MinSize=WxH` only when it cannot work on a smaller screen, and
   text-mode apps may still run at 480x320),
   `usr/share/APPLaunch/share/images/<id>.png` (100x100 square transparent PNG, apps repo `docs/ICON-PALETTE.md`); data under `usr/share/APPLaunch/share/<id>/` or `/opt/<id>/`.
   Screen size, rotation, depth, framebuffer, density and the keyboard rule come from the app contract
   (`docs/APP-CONTRACT.md`, reader `cp0_app_contract.{h,c}`), never hard-coded 640x480 or `/dev/fb0`; without the
   contract (an older launcher) the app falls back to its own fbdev probe and still starts.
   A native app's header uses the shared status strip and title from `cp0_lvgl` (`cp0::ui::StatusStrip`,
   `cp0::ui::add_title`): no copy of the strip or of `cp0_statusbar.[ch]` in an LVGL app.
4. Licence notice `usr/share/doc/<id>/copyright` when anything third-party is bundled.
5. `python tools/make_registry.py --owner OSRdesign --repo cyberdeck-zero-apps`; inspect the `.deb`
   (`dpkg-deb -I` / `-c` in WSL). A draft or a test package: `--only <id> --out <folder outside packages/>`. To
   publish, the tested `.deb` goes into `packages/` and the registry is written with `--no-build`; only that app's
   entry changes.
6. Hand off to `deck-verifier` with the test plan. Do not commit or push.
