# App packaging checklist (`apps-dev`)

1. Source in `apps/<id>/src/` (or `build/` when it wraps upstream software), a build script that works from a
   fresh checkout, and nothing app-specific added to the launcher repo.
2. `apps/<id>/app.json` complete: `share_code`, `package`, `version` (raised for every release), `title`, `summary`,
   `description`, `categories`, `author`, `license`, `source_repo`, `depends` (Debian packages the app needs),
   `permissions`.
3. `apps/<id>/root/` laid out as it must land: `usr/share/APPLaunch/bin/M5CardputerZero-<id>`,
   `usr/share/APPLaunch/applications/<share_code>.desktop` (absolute `Exec`, `Icon=share/images/<id>.png`; add
   `X-Fullscreen=true` only for a full-screen app; `X-MinSize=WxH` only when it cannot work on a smaller screen),
   `usr/share/APPLaunch/share/images/<id>.png`; data under `usr/share/APPLaunch/share/<id>/` or `/opt/<id>/`.
   Screen size, rotation, depth, framebuffer, density and the keyboard rule come from the app contract
   (`docs/APP-CONTRACT.md`, reader `cp0_app_contract.{h,c}`), never hard-coded 640x480 or `/dev/fb0`; without the
   contract (an older launcher) the app falls back to its own fbdev probe and still starts.
4. Licence notice `usr/share/doc/<id>/copyright` when anything third-party is bundled.
5. `python tools/make_registry.py --owner OSRdesign --repo cyberdeck-zero-apps`; inspect the `.deb`
   (`dpkg-deb -I` / `-c` in WSL).
6. Hand off to `deck-verifier` with the test plan. Do not commit or push.
