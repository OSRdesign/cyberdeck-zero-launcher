# 014 - Responsive shell, native Settings, on-screen keyboard (brief)

Status: decisions approved 2026-10-10. Study and evidence: `reports/022-responsive-shell-study.md` (read it before any phase; it holds the code inventory, the framework design, the render harness design, the phase table and effort). Builds on 012 (F1 keyboard, F2 multi-board) and 013 (Pi 3A+ bring-up, T1-T4 done, phase 0 keyboards done).

## Decisions (user)
- **Option B:** one code base, a layout layer places and sizes elements for the logical screen (480x320 up to 1280x720 and square 720x720). Landscape logical canvases only; **portrait not supported (D12)**, the display backend rotates portrait-native panels.
- **D1 Settings look:** keep today's centre-highlight roller look; on wide screens a **narrow centred roller** (width capped near today's), with margins. Settings leaves the 320x170 compat window and fills the screen (touch-native + keyboard).
- **D2:** the deck keeps compat Settings during P2-P3 behind a runtime switch (`APPLAUNCH_SETTINGS_UI=native|compat`), then one deliberate change to native at P3e, then the legacy renderer is deleted. **D9:** delete the legacy roller renderer and the ~7,440 lines of dead LVGL examples at the end.
- **D3:** an unmigrated page may open from the native Settings in dev builds only; a release goes native on a board only when every page visible on that board is migrated.
- **D4:** `APPLAUNCH_PANEL_MM=WxH` in `board.conf` (install.sh writes it for known panels), then fbdev mm, then 11.3 px/mm. **D5:** rows 7 mm, buttons and toolbar 9 mm, floors 40 px / 48 px.
- **D6:** the fallback on-screen keyboard is built on the responsive shell (P2b), not on the old compat window. Rules in 012 F1 still hold (Auto/Off setting, only when no physical keyboard, asleep BT keyboard counts as present for N minutes, no hide key, on-screen Esc always sends its release).
- **D7:** migrate the SSH form with the Apps/sudo phase.
- **D8:** stock-app toolbar stays a **bottom bar** on every screen class (scaled window above it).
- **D10:** export size/tokens internally now; publish the app contract (`cp0_ui_metrics.h`, VFB2, `.desktop` keys, docs) in P5 after Settings proves it. **D11:** the shared status bar may scale above 100 % on Large; update the apps' copies in the same change.
- **Out of scope for now:** the M5Stack Store, stock CardputerZero apps and the other native apps (Snake, Tank, IP Panel, CLI, Python): they keep running as a scaled fixed canvas window. User's published apps become responsive later (P5 contract).
- Transitional rule (report 022 section 6): anything not migrated goes through the unchanged compat path.

## Order of work
| Phase | Content | Tier |
|---|---|---|
| Pre | Commit/PR T1-T4 (013) and F1 phase 0 so the harness goldens have a baseline; deck and Pi 3A+ tests pass | Controller |
| P1a | Headless PC render harness (PNG at 480x320, 640x480, 800x480, 720x720, 1280x720; goldens and pixel diff) | savvy-heavy |
| P1b | Layout service `cp0_ui_metrics`, panel mm density, pinned presets (deck and 3A+ pixel-identical) | savvy-heavy |
| P1c | Home grid, status bar, bottom toolbar on tokens; Controller design for Wide/Square/Large | savvy-careful |
| P2a | Widget library, Settings scaffold and generic tree renderer (roller look), ChoiceBinding extraction | savvy-heavy |
| P2b | On-screen keyboard widget + TextFieldRow + inset/scroll, press+release injector, text hint, Auto/Off | savvy-heavy |
| P3a-d | Info pages; Wi-Fi; Bluetooth (PIN/passkey on the number pad); Apps + sudo + SSH form | medium / heavy |
| P3e | Native Settings default on the deck, delete legacy renderer | savvy-light/medium |
| P4 | Calculator, screensaver, toast, media OSD, loading, Esc ribbon on tokens | savvy-careful |
| P5 | App contract published | savvy-medium |

Controller owns: all visual design (Settings on wide/large classes, keyboard look, Wide/Square/Large home), acceptance, deploy to the Pi 3A+ (and the deck), screenshots. The user runs physical tests per phase (listed in report 022 section 5).

## Rolling blockers
- Merge T1-T4 + phase 0 first (baseline).
- Deck capture of `/proc/bus/input/devices` with the RTL-SDR dongle (IR node), still wanted before the keyboard phase.
- Hold-Esc exit with a USB-only keyboard (small follow-up of phase 0: `cp0_external_app_runner` watches only the old device).
- Exact panel mm for HyperPixel 4", HackBerry 720x720, uConsole 5" (ruler measurement).

## Home grid design rule (user feedback 2026-10-10, from the HackBerry 720x720 run)
Tiles must stay close to SQUARE (aspect about 1:1 to 1.2:1) on every screen; the grid dimensions (columns x rows) follow from the available area, they are not fixed at 3x2. Today's 640x480 grid stretched onto 720x720 gives tall 216x305 tiles with dead space: wrong. Rule for P1c (Controller design, to be confirmed on harness mockups):
- target tile = about 17-24 mm per side (deck today about 17 mm, Pi 3A+ today about 24 mm), minimum 9 mm touch target is already met; columns = round(available width / (target tile + gap)), rows = floor(available height / tile), the leftover is spread as even gaps/margins; the icon scales to about 50 % of the tile, the label sits under it; more apps than cells scroll vertically as today.
- the deck 640x480 (3x2, 192x188) and the Pi 3A+ 480x320 (3x2, 146x125) stay PINNED as they are.
- indicative results (available area under the status bar): 800x480 -> 4x2 (about 185x190), 720x720 -> 3x3 (about 218x200), 1280x720 -> 6x3 (about 195x195) or 5x3.

## Toolbar rule (user feedback 2026-10-10, HackBerry 720x720: the toolbar took 370 of 720 px)
The bottom toolbar has a FIXED physical height from the tokens, never "the rest of the screen": buttons 9 mm (about 90-100 px on the boards seen), plus margins, so about 100-110 px on every screen class (deck 140 and Pi 3A+ 100 stay PINNED). The stock-app window (integer scale of 320x170) is centred in the space above the toolbar, black margins around it; on a square or large screen the leftover room stays black (decision D8: always a bottom bar) until a later design uses it. A native, responsive page (Settings after P2) does not use this toolbar at all: it fills the screen and takes touch directly.
