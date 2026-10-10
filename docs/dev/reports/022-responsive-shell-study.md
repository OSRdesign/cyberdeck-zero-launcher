# 022 - Responsive launcher shell (option B): architecture study

Date 2026-10-10. Read-only study: code reading plus one read-only toolchain query in WSL. No product code changed, no
build, no ssh, no commit. Baseline: launcher `main` @ `29157ab` **plus the uncommitted task 013 work (T1-T4) in the main
working tree** (`git status`, V). Paths are relative to `launcher/`.
Marks: **V** = verified (file:line, or a command run today). **I** = inferred (reasoning, a spec value, or a guess that a
spike or a board must confirm).

## Executive summary

1. Feasible without touching the Store or stock apps: the display manager already hands the UI a logical landscape canvas of any size, rotation done in the blit (V), so the shell needs a layout layer, not a new display path.
2. The native shell is small and already half-parametrised: `native_ui.cpp` has a 25-field `Layout` with a deck and a compact preset (V :67-147); Calculator and screensaver use the same two-preset trick (V). About 90 geometry sites in total.
3. Settings is the big part: about 15k lines in about 15 deck-visible page classes, all absolute-positioned on a 320x150 content area through about 20 `LayoutMetric` enums, keys only; touch reaches it only as keys made by the display layer's LIST mode (V).
4. Settings is data-driven (`Tree<SettingEntry>` of toggles, value lists and page factories, V `settings_page.cpp:625-806`): one generic native renderer covers the root, every section, all toggles and all value pages; only 6 custom page groups need hand migration (Wi-Fi, Bluetooth lists + pairing dialog, BT alias, Apps, info pages, sudo prompt).
5. Fonts cost nothing new: Montserrat 8-48 (every even size) and FreeType are already compiled in (V config), and Settings already draws FreeType text at any pixel size (V `settings_fonts.hpp`).
6. Framework: `cp0_ui_metrics` (pure C, unit-testable, shareable with apps) = screen class from the logical size + density from the panel size in mm, tokens in px with mm floors (rows 7 mm, primary targets 9 mm), text styles snapped to the builtin sizes; the deck and the T3 compact presets are pinned so 640x480 and 480x320 stay pixel-identical.
7. Shared widgets (page scaffold with back/title/status icons, list/toggle/choice/slider/text-field/info rows, dialog, toast, progress, segmented strip, on-screen keyboard) so pages carry no geometry; a page declares its structure once and the scaffold picks single- or two-pane by class.
8. The on-screen keyboard (study 020, F1) becomes a shell widget: key size from the mm token, height a capped share of the screen per class, number pad, echo line; native pages shrink their body by the keyboard inset and scroll the focused field above it, which replaces the occlusion rect for launcher pages (the rect survives only for compat apps).
9. A headless render harness on the PC is feasible: a host LVGL build already exists (`tests/low_battery_ui`, V); compile the real display manager over a memory framebuffer with stubbed services. WSL has g++ 13.3, FreeType and libpng but no SDL2, libinput or D-Bus dev files (V today), so it must be headless, not SDL. PNGs at 480x320, 640x480, 800x480, 720x720 and 1280x720 plus golden diffs. savvy-heavy, 1-2 runs.
10. Plan: P1 harness + metrics + home/toolbar (deck identical), P2 widgets + native Settings tree renderer + keyboard, P3 custom Settings pages in 4 groups, P4 Calculator/screensaver/toasts/OSD, P5 app contract (env + `screen.state` + VFB2 + `.desktop` keys). About 4.5-7M tokens.
11. Stock apps keep their 320x170 framebuffer at an integer scale; only the placement rule moves into the layout service (largest integer scale that still leaves a toolbar of at least 9 mm: 1280x720 gets 3x, not today's 4x with a 40 px toolbar).
12. Biggest risks: keyboard parity of touch-native widgets (the BT keyboard stays the primary input), async lifetime rules buried in the Wi-Fi/BT/Apps view classes, CPU cost of full-panel native Settings on the Zero 2 W, and two Settings renderers alive during the migration.
13. User decisions: the Settings look per class (Controller design), one deliberate Settings change on the deck (recommended, at the end of P3), F1 phase 1 re-sequenced onto the responsive shell (recommended), the density source in `board.conf`, SSH form native or not.
14. Blockers: Controller visual design for the 800x480, 720x720 and 1280x720 classes and the Settings pattern; T1-T4 merged first (they are the golden baseline); F1 phase 0 (other worker) before the keyboard; panel mm for HyperPixel, HackBerry and uConsole are inferred.
15. Boards beyond the Zero 2 W and the 3A+ are later releases (012 F2): until then the 800x480, 720x720 and 1280x720 classes are checked by harness PNGs only.

---

## 1. Inventory of hard-coded geometry

### 1.1 Native shell (post-T3)

| File | Lines | Geometry sites | What is hard-coded | Evidence |
|---|---|---|---|---|
| `main/ui/native_ui.cpp` | 808 | ~35 (25 `Layout` fields x 2 presets, + 10 inline) | `kCols 3`, `kRows 2` (:45-46); deck preset bar 56, pad 16, title (20,10) font 32, tile radius 28, border 3/5, icon 128 at top 14, label font 24, label bottom 12 / inset 16, toolbar pad 10/10, gap 8, radius 18, fonts 24/32 (:96-117); compact preset 40/10/24/20/2/4/88/6/18/5/12/6/5/6/14/18/24 (:118-139); preset switch `kCompactBelowH 400` (:86); status strip canvas `320 * pct / 100` (:143); 6 toolbar keys Esc/arrows/Enter (:451-458); toolbar starts at `wy + wh + wy` (:510); Esc ribbon rect in compat coords (20,4,280,22) (:623); colours `kTileBg`, `kAccent`, `kGold` (:47-52) | V |
| `main/ui/page_app/ui_app_calculator.cpp` | 447 | ~20 | `compact = height < 400` (:206); pad 6/10, top bar 36/44, display 64/104, gap 4/6, 6x4 grid (:207-212); title font 20/28 at (10,8)/(16,6) (:217-219); key radius 8/14 (:274); key fonts Montserrat 20/28/32, DejaVu FreeType for x and / (:146-152, :285-287) | V |
| `main/ui/native_screensaver.cpp` | 254 | ~8 | FreeType clock 180 px, offsets -30/+90 (:28-30), design 640x480 scaled down by the tighter ratio below 400 px height (:31-53), Montserrat 28/18/48 (:47, :53, :121) | V |
| `main/ui/launcher_toast.cpp` | 198 | 8 | 280x22 (38 multi-line) at top 4, radius 6, font 12, designed for 320 and scaled by `width/320`, never down (:18-26, :57-64): 4x (1120 px wide) on a 1280 canvas | V |
| `main/ui/launcher_media_osd.cpp` | 279 | 16 | fixed 190x82 box on `lv_layer_top()` of the default display, icon 30x24 at (14,10), title 76x18 at (48,12), value 48x18 at (124,12), bar 154x10 at (18,50) (:13-14, :39-106) | V |
| `main/ui/ui_loading.cpp` | 208 | 2 | flex overlay on `lv_layer_top()` (:93) | V |
| `ext_components/cp0_lvgl/src/cp0_statusbar.c` + `.h` | 234 + 50 | (renderer) | 104x40 clock pill 16 px from the right, 8 px from the top, `CP0_STATUSBAR_HEIGHT 56`; scaled 25..100 % only (`cp0_statusbar.h:23`, :52-56). Shared with full-screen apps (drift checker `docs/dev/deck/check_statusbar_drift.py`) | V |
| `ext_components/cp0_lvgl/src/ui_app_page.cpp` | 1,084 | ~12 | standard page: top bar 20 px (`ui_app_page.hpp:109`), container 320x150 at (0,20) (`ui_app_page.cpp:88-90`), top bar 320 wide (:824), bottom bar 320x20 at y 150 (:1157-1158), title Montserrat 14 (:357) | V |
| Not active on the Pi deck | | | lock screen `ui_screensaver.cpp` (the Pi uses `native_screensaver`, `ui_screensaver.cpp:551-576`), `ui_low_battery.cpp` (no battery gauge) | V |

Native shell total: about 90-100 literal layout sites in 7 files; about 2,200 lines would be touched (the
`ui_app_page.cpp` sites stay, because native pages bypass the standard top bar and container). Already
adaptive: the terminal page (report 021, `ui_app_st.cpp:88-92`).

### 1.2 How Settings pages are built (V)

- **Entry.** `UISettingTreePage` (`settings_page.hpp:17-35`) is an `AppPage` with `kTouchList = true` (:20). Its
  constructor builds the whole menu as a `Tree<SettingEntry>` (`settings_page.cpp:625-806`), then creates the root roller
  (`:833-842`).
- **`SettingEntry`** (`settings_tree_types.hpp:165-251`): a label, an optional `page_factory`, `icon_enabled` + a toggle
  API (`SettingApiReadFlag` / `ReadFlagTimeStart` / `Activate`, :31-35), an optional async API, a `PageType`
  (`Normal`, `NextPageNeeded`, `FullCustom`, :154-158), a status read policy, an `activation_gate`, `selected_index`.
  Children with no factory are the options of a value page.
- **Three page levels, all absolute-positioned on 320x150:**
  - L1 `LvSettingRoller` (`settings_menu_roller.hpp`, 620 lines): a centre-highlight roller; container 320x150 (:457),
    highlight bar 312x21 at (4,66) (:470-471), rows 320x21 in a flex column with `LV_SCROLL_SNAP_CENTER` (:484-527),
    label centred on x 60 (:40-47), fonts 18/16/12 bold by distance from the centre (:133-186), page slide by 320 px
    (:590-593).
  - L2 `LvSettingRollerPage2` (`settings_submenu_page.cpp/.hpp`, 1,194 lines): a right-hand panel x 120 w 200, bar at
    y 66, status icons for toggles with async or direct polling, `lv_msgbox` warnings (`settings_submenu_page.hpp:30-45`,
    `.cpp:243-260`).
  - L3 `LvSettingValuePage3Base` (`lvgl_components.hpp:7938-8537`, about 600 lines): a value roller, 17 metrics
    (:7955-7973), fonts 16/12/10 (:8049-8094), "ok:set" hint (:8502-8511). Subclasses implement only
    `initial_selection()` and `activate_selected()` (for example `settings_touch_page.cpp:34-46`): their logic is
    already separable from the view.
  - `FullCustom` pages draw their own UI in the same 320x150 container.
- **Helpers.** `DComponens::LvglComponensBase` (`lvgl_components.hpp:95-436`) gives the async dispatch timer, async task
  registry and lifetime tokens. **About 7,440 lines of that header are LVGL examples that are never compiled**
  (`#if defined(LVGL_COMPONENTS_ENABLE_EXAMPLES)` at :497, never defined anywhere, V grep). Each page has its own
  `create_label(parent, text, x, y, ...)` helper. There is no shared list or row widget.
- **Fonts.** `settings_fonts::sans/cjk_sans/mono(size)` load FreeType fonts at any pixel size through `cp0_fonts()`
  (`settings_fonts.hpp:13-40`, `cp0_font_service.cpp:20-48`). The sizes used are 8-18 px.

### 1.3 Input handling in Settings today (V)

- **Keys, navigation.** `LV_EVENT_KEY` on the object focused in the page's input group (roller `handle_key_event`,
  `settings_menu_roller.hpp:553-587`: Up/Down move and wrap, Enter/Right open, Esc/Left leave). The group is the page's
  own (`ui_app_page.cpp:233-235`), set on every keypad indev by `cp0_lvgl_start_app_page` (:1193-1206); levels move
  focus between objects (`settings_menu_roller.hpp:86-111`).
- **Keys, text.** `LV_EVENT_KEYBOARD` on the active screen with the full `key_item` (UTF-8, mods, press/release),
  inside `KBD_INPUT_CONTEXT_TEXT` plus `cp0_keyboard_set_lvgl_keypad_intercept(1)`, set and restored by hand by each
  page (Wi-Fi `settings_wifi_page.cpp:1005-1015`, BT agent `settings_bluetooth_page.cpp:1564-1575`, BT alias
  :2483-2495, Apps `settings_apps_page.cpp:866-877`). Dispatch: `cp0_keyboard_lvgl_input.c:90-141` (screen event first,
  then the group unless intercepted).
- **Touch.** It never reaches a Settings widget. `begin_page` puts the compat indev in LIST mode with the highlighted
  row at compat y 96, rows 21 px (`native_ui.cpp:769-772`). The display layer then turns gestures into keys
  (`cp0_lvgl_dpi_scaled.c:427-472`): each 21 px of drag = Up/Down, a tap on the highlighted row = Enter, a tap on
  another row = that many Up/Down, a swipe in from the left edge = Esc. LVGL always sees "released" (:503-508). The Apps
  page inverts the drag (`settings_apps_page.cpp:382`, reset :346). Dialog buttons can therefore only be "tapped" as
  Enter.
- **Hints.** Footers spell out keys ("TAB:switch  ALT:show  OK:connect", `settings_wifi_page.cpp:690`; "OK:set  BS:del
  ESC:cancel", `settings_bluetooth_page.cpp:2283`): 34 such strings over the Settings files (V grep).

### 1.4 The special flows

- **sudo prompt** (`ext_components/cp0_lvgl/src/cp0_sudo_async.cpp:285-353`): a full-size overlay on `lv_layer_top()`
  of the default display, a 240x116 box, Montserrat 14 and 10, keys through `LV_EVENT_KEYBOARD` read on release. It
  never sets the TEXT context (report 020). Used by Settings > Apps for dpkg (`settings_apps_page.cpp:1151`) and by
  compat apps built on cp0_lvgl (Store).
- **Wi-Fi** (`settings_wifi_page.cpp/.hpp`, 2,168 lines): one class with four views (List, HiddenSsid, Password,
  Connecting, `.hpp:79`). Five visible rows of 22 px at y 30 (`.hpp:32-52`). The password panel is absolute
  (`.cpp:584-626`), with a display-only `lv_textarea` fed from a `std::string` buffer (`create_hidden_input`,
  :634-663; focus state set by hand, :609). Left Alt shows the password. The async scan/connect state machine lives in
  the view class (`.hpp:98-128`).
- **Bluetooth pairing / PIN** (`settings_bluetooth_page.cpp:1590-1699`): an overlay with a 300x128 dialog; the title
  depends on the BlueZ agent method (confirmation, authorization, passkey 6 digits only `0-9`, PIN up to 16,
  :1619-1649); the hint is "Enter: Confirm / ESC: Reject".
- **Bluetooth alias** (same file, `LvSettingBluetoothAliasPage3`, 35 methods between :365 and :2728): absolute layout
  (15 metrics, `.hpp:247-262`), about 140 lines of hand-made UTF-8 cursor and key handling (:2590-2728).
- **Date & Time** (`settings_rtc_page.cpp/.hpp`, 2,032 lines): an L2 section with Info (FullCustom), Network Time (a
  toggle with direct status polling), Set Manually (an L2 gated while NTP is on, `settings_page.cpp:746-751`) holding
  Year 2000-2099 (100 rows), Month, Day (rebuilt per month, :310-327), Hour, Minute value pages, and a "Save?" Yes/No
  confirm; a factory-warning `lv_msgbox` (`settings_rtc_page.cpp:1430-1455`). On the Pi it writes through timedate1, no
  sudo (`settings_hw_profile.hpp:38-42`).
- **Touch** (`settings_touch_page.cpp`, 138 lines): the section is rebuilt from the app registry on every visit
  (`settings_page.cpp:245-251`); each app gets an L3 value page Off / Swipe / Swipe + Fire (:63-95), saved per app in
  config (`touch_settings.cpp:42-64`). Settings, terminals, SSH, Calculator and IP Panel are excluded.
- **Apps** (`settings_apps_page.cpp/.hpp` 1,305 lines + `apps_backend` 534 + `apps_status_model` 518): a 6-row x 17 px
  list (`.cpp:26-28`, `.hpp:40`), an edit panel for the source URL (text context), a message panel, async dpkg jobs
  through the sudo prompt.

### 1.5 Per-page table (deck-visible pages first)

Difficulty: L = the generic tree renderer covers it; M = a small custom page; H = a large custom page with text
input, dialogs and async state.

| Settings page (path) | Files | LOC (cpp + hpp) | Layout style | Input style | Text | On the deck | Migration |
|---|---|---|---|---|---|---|---|
| Root menu | `settings_menu_roller.hpp` | 620 | absolute + flex column, centre snap | KEY on group; LIST touch | - | yes | L (tree renderer) |
| Sections (Screen, Wi-Fi, Bluetooth, Launcher, Touch, Date & Time, Set Manually, System) | `settings_submenu_page.*` | 1,194 | absolute right panel, toggle status icons, msgbox | KEY; LIST touch | - | yes | L-M (async toggle status, gates, power warnings) |
| Value page base | `lvgl_components.hpp:7938-8537` | ~600 | absolute value roller | KEY Up/Down/Enter/Esc | - | yes | L (choice page) |
| Screen > Brightness (levels; On / Off 10 s on gpio, T4) | `settings_brightness_page.*` | 737 | value base + status label | KEY | - | yes | L (logic in `activate_selected`, rollback) |
| Screen > DarkTime | `settings_screen_timeout_page.*` | 532 | value base + status label | KEY | - | yes | L |
| Touch > app > choice | `settings_touch_page.*` | 138 | value base | KEY | - | yes | L |
| Launcher (tile show/hide toggles) | `settings_adapter.cpp:321-346` | (model) | L2 toggles | KEY | - | yes | L |
| Date & Time (Info, NTP, manual values, Save?) | `settings_rtc_page.*` | 2,032 | value base + status + msgbox | KEY | - | yes | M (Info page; 100-row Year list wants a stepper, design) |
| System > Reboot / Shutdown | `settings_confirmation_page.*`, `settings_boot_action_policy.*` | 55 + 125 | value base Yes/No | KEY | - | yes | L (confirm dialog) |
| Wi-Fi > Networks, Join Hidden | `settings_wifi_page.*` | 2,168 | absolute, 4 panels, msgbox | KEY + KEYBOARD (TEXT) | password, SSID | yes | H |
| Bluetooth > Paired, Scan, pairing dialog | `settings_bluetooth_page.*` (+ 2 x 42-line subclasses) | ~2,650 | absolute, 4 rows x 22, 300x128 dialog | KEY + KEYBOARD | PIN / passkey | yes | H |
| Bluetooth > Alias | same file | ~450 of it | absolute | KEY + KEYBOARD | text | yes | M |
| Apps | `settings_apps_page.*` (+ backend, status model) | 1,305 (+1,052 model) | absolute 6 x 17 rows, panels | KEY + KEYBOARD; LIST inverted; sudo | source URL | yes | H |
| User (account info) | `settings_system_page.cpp` (`LvSettingSystemInfoPage3`) | part of 1,776 | absolute table, 24 metrics | KEY | - | yes | L-M (info rows) |
| System > Storage, Licenses, About (board row, T4) | `settings_static_info_page.*` | 510 | absolute text lines, 9 metrics | KEY scroll | - | yes | L-M |
| sudo prompt (cp0_lvgl) | `cp0_sudo_async.cpp:285-353` (+ keys ~700) | part of 1,013 | absolute 240x116 on top layer | KEYBOARD | password | yes (Apps) | M |
| Speaker | `settings_volume_page.*` | 894 | value base | KEY | - | no (stock only) | keep legacy |
| Ethernet info, Software update | `settings_system_page.cpp` | part of 1,776 | absolute, msgbox, progress | KEY | - | no | keep legacy |
| Battery info / calibrate | `settings_battery_*` | 595 + 241 | absolute | KEY | - | no | keep legacy |
| Developer > ADB guide | `settings_adb_guide_page.*` | 1,149 | absolute | KEY | - | no | keep legacy |

Deck-visible Settings code: about 15k lines (page classes only, without `settings_page.cpp`, the adapter and the
Apps models), of which an estimated 5-6k lines are view and input code to replace (I);
the rest is models and async plumbing to keep and, where it sits inside a view class, to extract.

---

## 2. Settings in the compat window today, and what changes natively

### 2.1 Today (V)

1. A home tile calls `app::launch` (`launch.h:162-180`): `begin_page_launch()` arms the Esc watchdog and the hold-Esc
   return (`launch.cpp:120-126`), then `native_ui::begin_page(false, true)` sets LIST touch mode and `enter_compat()`
   shows the native toolbar screen and switches the default LVGL display to compat (`native_ui.cpp:754-788`,
   `cp0_lvgl_dpi_scaled.c:674-687`).
2. The page is built on the compat display, 320x170: AppPage top bar 20 px plus a 320x150 container.
3. `flush_compat` upscales each dirty area by the integer scale into the window (`cp0_lvgl_dpi_scaled.c:111-134`):
   2x at (0,0) on the deck (640x340, toolbar 140 px); 1x at (80,25) on the 480x320 3A+ (toolbar 100 px from y 220,
   brief 013 and `cp0_fb_output.c:115-123`). Settings text there is 10-18 px on a 6.1 px/mm panel: 1.6-3 mm.
4. Touch: LIST mode as in section 1.3. Toolbar: Esc / arrows / Enter inject keys (`native_ui.cpp:465-497`); Esc also
   feeds the Esc state used by hold-to-exit; a toolbar Esc destroyed mid-press always sends its release (:486-493).
5. Esc safety: holding Esc shows "Hold ESC 3s to return home" and forces home at 3 s
   (`esc_hold_hint_controller.cpp:21-44`, :80-96); the watchdog calls `_Exit(75)` at about 3.5 s if the UI is stuck
   (`esc_ui_watchdog.cpp:63-92`); `begin_page` clears a stuck Esc first (`native_ui.cpp:757-759`).

### 2.2 Native, touch-native Settings

| Concern | Today (compat) | Native responsive |
|---|---|---|
| Display and launch | `begin_page(false, true)`: compat, toolbar, LIST | `begin_page(true)`: native display, no toolbar, POINTER. The page derives from `AppPageRoot`, or disables the 320-wide top bar and resizes the container as the Calculator does (`ui_app_calculator.cpp:181-185`) |
| Touch | gestures turned into keys; LVGL never sees a press | the native pointer indev delivers presses straight to the widgets (`touch_read_native`, `cp0_lvgl_dpi_scaled.c:342-350`): taps, drags, LVGL scrolling, sliders, switches. LIST mode is no longer used by the shell; keep it in the display layer for compat-hosted pages until nothing asks for it |
| Focus and group | one object focused per level; arrows handled by the roller | widgets join the page group; a tap focuses (LVGL click-focus), so touch and keys share one focus. Lists handle Up/Down themselves (LVGL groups move only on NEXT/PREV); the focus ring is drawn only for `LV_STATE_FOCUS_KEY` (keyboard use), not after a tap |
| What replaces the toolbar | Esc / arrows / Enter buttons | header **Back** button (page back, same as Esc); rows are tapped (Enter); lists scroll by drag (arrows); dialogs and forms get real buttons (primary = Enter, cancel = Esc). A **Home** affordance is a Controller decision: a header button, or a long press on Back (the touch twin of hold-Esc) |
| Shared top bar | 20 px AppTopBar inside 320x170 | the page header carries the title and the shared status icons (`native_ui::add_status_icons`, the `cp0_statusbar` renderer), sized by tokens. `cp0_statusbar_render_scaled` stops at 100 % (`cp0_statusbar.h:52-56`): the Large class needs it raised, a change shared with apps (drift checker) |
| Esc-hold watchdog | toolbar Esc and physical Esc both feed `cp0_esc_state` | unchanged for physical keys (still armed per page). The touch Back and Home call the page or `go_back_home` **directly, never by injecting Esc**, so they cannot leave a stuck Esc. Any widget that does inject Esc (the on-screen keyboard's Esc, F1) must follow the toolbar precedent: release on `PRESS_LOST` and `DELETE` |
| Hold-Esc ribbon | toast on the compat top layer | the same toast on the native top layer, sized by tokens (today it would be 4x wide on 1280 px, `launcher_toast.cpp:24-26`) |
| Text contexts | each page sets TEXT + intercept by hand | the shared text-field widget sets `KBD_INPUT_CONTEXT_TEXT` + intercept on focus and restores both on blur and on delete; it also publishes the text type (text / number / password). The `key_item` path stays, so the physical keyboard and the on-screen keyboard (which injects `key_item`s) work unchanged |
| Dialogs | `lv_msgbox` inside 320x150, default theme, Montserrat 14 | one dialog widget sized by tokens (`dialog_max_w`), centred in the area above the keyboard inset |
| On-screen keyboard (020) | overlay over the lower compat window, occlusion rect, gesture suspension | a native shell widget; the page body shrinks by the keyboard height and the focused field scrolls into view (section 3.4). No occlusion rect and no gesture suspension for native pages |

---

## 3. The responsive framework

### 3.1 (a) Screen classes and breakpoints

The UI only ever sees the logical canvas (`APPLAUNCH_LOGICAL`, landscape after the blit rotation,
`cp0_lvgl_dpi_scaled.c:743-774`). Structure (columns, panes, where actions go) follows the **logical pixel size**,
because text legibility is pixel-bound on low-density panels. Sizes (targets, spacing) follow **density** (px/mm).

| Class | Rule (logical w x h, a = w/h) | Targets | Panel, density | Structure (proposal, Controller to confirm) |
|---|---|---|---|---|
| Compact | h < 400, a > 1.25 | 480x320 (Pi 3A+), 1280x400 bars | Luckfox 3.5", 79x49 mm, **6.1-6.5 px/mm** (V overlay `width-mm=49,height-mm=79`, 021 add. 2) | single pane; T3 preset pinned |
| Standard | 400 <= h < 600, 1.25 < a < 1.5 | 640x480 (deck) | Waveshare 2.8", **11.3 px/mm** (V 020/021) | single pane; deck preset pinned |
| Wide | 400 <= h < 600, a >= 1.5 | 800x480 (HyperPixel 4.0), 854x480 | HyperPixel 86x52 mm (I spec), **9.2 px/mm** (I) | two-pane Settings likely |
| Square | 0.8 <= a <= 1.25 (h >= 600) | 720x720 (HackBerry CM5, Waveshare 4") | 4", **10.0 px/mm** (I) | single pane, more rows, larger grid |
| Large | h >= 600, a > 1.25 | 1280x720 (uConsole), 1024x600, 1280x800 | uConsole 5", **11.6 px/mm** (I), no touch (V 021 add. 5); 7" 800x480 is 5.2 px/mm (I) | two-pane; keyboard-first on the uConsole |
| Portrait | a < 0.8 | none today | - | best effort (narrow single pane); not an official target |

**Portrait and landscape-only.** Every target is viewed in landscape or square and the backend pre-rotates portrait
buffers (021 add. 7/8, `cp0_fb_output.h:7-14`). Landscape-only (plus square) is acceptable as the official scope. The
class rules still compute a Portrait class at no cost, so a portrait logical canvas degrades to the narrow single pane
instead of breaking.

**Pinned presets.** When the canvas is 640x480 with no board profile (or the deck's profile) the Standard tokens are
the literal numbers of today's deck `Layout`; when it is 480x320 the Compact tokens are the T3 numbers. Every other
size computes its tokens. This keeps both shipped boards pixel-identical through phases 1 and 4.

### 3.2 (b) Design tokens

**Density source** (needs a decision): a new optional `APPLAUNCH_PANEL_MM=WxH` in `board.conf`, written by
`install.sh` for known boards (it already writes the profile, brief 013), then the fbdev `var.width/height` (mm; KMS
fills them from the connector, I), then a default of 11.3 px/mm (the deck). Exported to apps (section 3.6).

**Touch targets.** List rows 7 mm, primary buttons and toolbar keys 9 mm, with pixel floors (40 and 48 px) for
low-density panels. That gives rows of 43 / 79 / 64 / 70 / 81 px and primary targets of 55 / 102 / 83 / 90 / 104 px on
the five targets (Compact / Standard / Wide / Square / Large).

Proposed starting table (px). The Standard and Compact columns reproduce today's numbers where they exist. All of it
is a proposal for the Controller's design pass, not a decision.

| Token | Compact 480x320 | Standard 640x480 (deck, pinned) | Wide 800x480 | Square 720x720 | Large 1280x720 |
|---|---|---|---|---|---|
| header / status bar height | 40 (T3) | 56 (V) | 56 | 64 | 64 |
| status icons scale | 72 % (T3) | 100 % (V) | 100 % | 100 % | 115 % (needs >100 %) |
| spacing S / M / L | 5 / 10 / 14 (M = T3 pad) | 8 / 16 / 20 (M = V pad) | 8 / 14 / 20 | 8 / 16 / 24 | 10 / 16 / 24 |
| radius S / M / L | 6 / 14 / 20 (M, L from T3) | 8 / 18 / 28 (M, L from V) | 8 / 16 / 24 | 8 / 18 / 28 | 10 / 20 / 28 |
| list row (7 mm) | 43 | 79 | 64 | 70 | 81 (no touch: 70) |
| primary target (9 mm) | 55 | 102 | 83 | 90 | 104 |
| stock-app toolbar | 100 (T3) | 140 (V) | 140 | 140 | 140-160 |
| home tile icon | 88 (T3) | 128 (V) | 128 | 128 | 160 (icons are 100 px PNGs stretched, V `native_ui.cpp:424-426`) |
| text title / heading / body / caption | 24 / 20 / 18 / 14 | 32 / 28 / 24 / 18 | 32 / 26 / 22 / 18 | 32 / 28 / 24 / 20 | 36 / 30 / 26 / 20 |
| dialog max width | 440 | 560 | 600 | 600 | 720 |
| keyboard height cap | 60 % of h | 280-320 px (F1 rule) | 50 % | 40 % | 45 % |

**Fonts** (V config `linux_x86_cross_cp0_config_defaults.mk:53-83`; `lvgl_config.h:154`):
- Builtin Montserrat **8 to 48, every even size**, plus 28 compressed, are already enabled. The compiled objects are
  about 1.0 MB (23 objects in `build/.../src/font`, V), already paid. Every size in the table above exists:
  **nothing to add, no flash or RAM cost.**
- FreeType is enabled with a 512-glyph cache (:82-83) and the TTFs are in the bundle (`APPLaunch/share/font`: Montserrat
  Medium/Bold, DejaVu, NotoSansCJK, JetBrainsMono, V listing). Settings already uses it at any size.
- Rule: builtin Montserrat for shell chrome up to 48 px (exact pixel identity on the deck); FreeType for user text that
  can hold non-ASCII (SSIDs, BT names, app names) and for anything above 48 px (screensaver clock, already FreeType). A
  new FreeType size costs one font object plus shared cache, roughly tens to hundreds of KB (I).
- No LVGL transform scaling of text (blurred, slow). ThorVG is on (`:42-44`) if scalable SVG icons are wanted later.
- LVGL's own DPI (`LV_DPI_DEF 130`, `lvgl_config.h:22`; no `lv_dpx` use in the UI, V grep) is left alone: widgets set
  their styles from tokens, so the default theme's DPI-based paddings never decide a size. Changing the display DPI
  would move the deck's theme-styled widgets.

### 3.3 (c) Layout primitives and the layout service

- **LVGL primitives:** flex rows and columns with grow, `LV_PCT` widths, `LV_SIZE_CONTENT` heights, min/max width
  styles for dialogs, scroll containers with snap, grid descriptors only for form label/field pairs (stacked on
  Compact). `lv_obj_set_pos` only for overlays (keyboard, toast, dialogs' backdrop).
- **Layout service** in `ext_components/cp0_lvgl` (so apps built on cp0_lvgl and the sudo prompt can use it):

```c
/* cp0_ui_metrics.h - pure C, no LVGL: unit-tested on the PC, usable by apps */
typedef enum { CP0_UI_COMPACT, CP0_UI_STANDARD, CP0_UI_WIDE, CP0_UI_SQUARE, CP0_UI_LARGE, CP0_UI_PORTRAIT } cp0_ui_class_t;
typedef enum { CP0_TOK_SPACE_S, CP0_TOK_SPACE_M, CP0_TOK_SPACE_L, CP0_TOK_RADIUS_S, CP0_TOK_RADIUS_M, CP0_TOK_RADIUS_L,
               CP0_TOK_HEADER_H, CP0_TOK_ROW_H, CP0_TOK_TARGET, CP0_TOK_TOOLBAR_H, CP0_TOK_TILE_ICON,
               CP0_TOK_TILE_MIN_W, CP0_TOK_DIALOG_MAX_W, CP0_TOK_STATUS_PCT, CP0_TOK_KEY_H, CP0_TOK__COUNT } cp0_ui_token_t;
typedef enum { CP0_TEXT_TITLE, CP0_TEXT_HEADING, CP0_TEXT_BODY, CP0_TEXT_CAPTION, CP0_TEXT_SYMBOL, CP0_TEXT__COUNT } cp0_ui_text_t;
typedef struct {
    int w, h, rot, ppmm_x100, touch;      /* logical canvas, rotation, density, touch device present */
    cp0_ui_class_t cls; int preset;       /* preset: 0 computed, 1 deck, 2 T3 compact */
    int tok[CP0_TOK__COUNT]; int text_px[CP0_TEXT__COUNT];
} cp0_ui_metrics_t;
int cp0_ui_metrics_compute(int w, int h, int ppmm_x100, int touch, const char *board, cp0_ui_metrics_t *out);
int cp0_ui_metrics_from_env(cp0_ui_metrics_t *out);           /* apps: APPLAUNCH_SCREEN_* or screen.state */
static inline int cp0_ui_mm(const cp0_ui_metrics_t *m, int tenths_mm, int floor_px);
```

```cpp
// C++ LVGL side, used by all native code
namespace ui {
const cp0_ui_metrics_t &metrics();                 // native display, computed once at start
int px(cp0_ui_token_t);                            // token lookup
const lv_font_t *font(cp0_ui_text_t, Face = Face::Chrome);   // builtin <= 48 px, FreeType for Face::User or > 48
int bottom_inset();                                // on-screen keyboard height, 0 when hidden
void on_inset_changed(std::function<void()>);      // the page scaffold relayouts its body
}
```

- **Display manager hook:** the compat window placement moves out of `cp0_fbo_compat_origin`
  (`cp0_fb_output.c:115-123`) into the layout service, through a new
  `cp0_display_configure_compat(scale, x, y)` called once before the first compat page. `APPLAUNCH_COMPAT_SCALE` keeps
  its override role (`cp0_lvgl_dpi_scaled.c:779-789`).

### 3.4 (d) Shared widgets (pages carry no geometry)

| Widget | Behaviour per class | Replaces |
|---|---|---|
| `Page` scaffold | header (Back, title, status icons), scrollable body, optional action bar; single pane, or two panes on Wide/Large (section list left, detail right); body height = screen - header - keyboard inset | AppTopBar + 320x150 container, roller levels |
| `Section` + `NavRow` | full-width row, label + current value + chevron, row height token | roller rows |
| `ToggleRow` | label + switch; binds to the existing `SettingApi` toggle callbacks, including async and direct status reads | L2 status icons |
| `ChoicePage` / `ChoiceRow` | a list of options with a check mark; binds to `initial_selection()` / `activate(index)` | `LvSettingValuePage3Base` and its 7 subclasses |
| `SliderRow` | brightness and volume levels | value lists of percentages |
| `StepperRow` | numeric value with - / + and typed input (Year 2000-2099) | 100-row value lists |
| `TextFieldRow` | label + field; type `Text`, `Number`, `Password` (mask + show toggle); owns the UTF-8 buffer and cursor, sets and restores TEXT context + intercept, asks for the keyboard, scrolls itself above it | per-page hidden inputs and key handlers |
| `InfoRow` / `InfoTable` | key/value pairs, wraps on Compact | absolute info tables (User, About, Storage) |
| `Dialog` / `Confirm` | title, body, 1-3 buttons (primary = Enter, cancel = Esc); max width token; sits above the keyboard; body scrolls | `lv_msgbox`, BT agent overlay, sudo box |
| `Toast` | token-sized, top centre, multi-line | `launcher_toast` 320-based scaling |
| `Progress` / `Busy` | bar or spinner; disables inputs | update/apps progress |
| `Segmented` (tab strip) | 2-4 options in one row | Paired / Scan switches, Touch Off / Swipe / Fire |
| `OnScreenKeyboard` | section 3.4.1 | F1 phase 1 component |

Keyboard parity is a widget responsibility: every widget handles Up/Down/Left/Right/Enter/Esc itself (lists move
focus row by row, sliders and segments take Left/Right), so a page built only from widgets is fully usable with the BT
keyboard and with touch.

#### 3.4.1 On-screen keyboard as a shell widget (scope addition)

- **Component:** `cp0_lvgl` (as F1 decided), a custom button matrix: QWERTY, digits/symbols page, one-shot Shift and
  Caps, a number pad (3 x 4 + backspace + Enter) for `Number` fields, an echo line (masked for `Password`). The look is
  the Controller's design (F1 phase 1 item 1).
- **Size per class:**
  - key pitch = min((w - 2 margins - 9 gaps) / 10, 10 mm);
  - key height = the 6-7 mm token, then capped so the whole keyboard (echo line + 4 rows) stays under the class
    height cap (table above);
  - centred when the width cap applies (Large).
  - Deck: about 300 px, matching F1's 280-320 px rule. 480x320: 48 x 36 px keys (7.9 x 5.9 mm), about 190 px total.
    1280x720: keys capped at about 116 px wide, about 320 px total.
  - The number pad is 3 key widths wide, centred.
- **Placement with native Settings:** bottom of the native display, on the top layer. `ui::bottom_inset()` becomes
  the keyboard height; the scaffold shrinks its body and calls `lv_obj_scroll_to_view_recursive(field)`, so **the
  focused field is always above the keyboard**. This replaces 020's occlusion rect for launcher pages; the rect and
  the gesture suspension stay only for compat apps (Store search, F1 phase 2) and any launcher page left in the compat
  window (SSH form unless migrated, decision D7).
  - A dialog with a field (BT PIN) re-centres in the space above the keyboard; on Compact (about 90 px left) it
    collapses to title + field, and the keyboard's Enter/Esc act as its buttons. Controller design.
- **Hooks:**
  - a `TextFieldRow` declares its type; on focus it sets the TEXT context and `cp0_keyboard_set_text_hint(type)`
    (F1 phase 1 item 5) and calls `ui::osk::request(field)`;
  - the controller shows the keyboard only when the policy says no physical keyboard (F1 phase 0 `input.state`:
    `keyboards=0`, with the asleep-BT N-minute rule), the Settings choice is Auto, and no desktop session owns the
    screen;
  - it hides on blur, on page change, on lock/screensaver, and when a keyboard appears (releasing latched modifiers);
    tapping the field re-shows it (no hide key, F1 rule);
  - keys are injected as press + release `key_item`s with `utf8` (F1 phase 1 item 4), so text fields, the sudo prompt
    and old pages all receive them through the existing path;
  - its Esc follows the toolbar rule (always release).
- **Settings entry** "On-screen keyboard: Auto / Off" is one more tree node (a `ChoicePage`): no custom page.

### 3.5 (e) A page declared once: before and after (Settings > Bluetooth > Alias)

Before (real code, abbreviated; `settings_bluetooth_page.cpp`):

```cpp
// :2223-2299 render() - absolute positions on the 320x150 container
create_label(ComponensObj, "Bluetooth Name", 8, 8, CP0_ENUM_CAST_INT(LayoutMetric::ScreenW) - 16, 0x58A6FF,
             settings_fonts::sans(13, LV_FREETYPE_FONT_STYLE_BOLD));
create_label(ComponensObj, "Name:", CP0_ENUM_CAST_INT(LayoutMetric::AliasLabelX), 38,
             CP0_ENUM_CAST_INT(LayoutMetric::AliasLabelWidth), 0xCCCCCC, settings_fonts::sans(12));
alias_input_ = lv_textarea_create(ComponensObj);
lv_obj_remove_style_all(alias_input_);
lv_obj_set_pos(alias_input_, CP0_ENUM_CAST_INT(LayoutMetric::AliasTextX), 32);
lv_obj_set_size(alias_input_, CP0_ENUM_CAST_INT(LayoutMetric::ScreenW) - CP0_ENUM_CAST_INT(LayoutMetric::AliasTextX) -
                              CP0_ENUM_CAST_INT(LayoutMetric::AliasTextRightInset), 28);
/* ... 25 more style calls: font 14, letter space, colours, border, radius 3, paddings, cursor part ... */
create_label(ComponensObj, saving_ ? "Setting alias..." : "OK:set  BS:del  ESC:cancel",
             8, CP0_ENUM_CAST_INT(LayoutMetric::ScreenH) - 14, /* ... */ settings_fonts::sans(10));
// :2483-2495 TEXT context + keypad intercept entered and restored by hand
// :2590-2647 LV_EVENT_KEY handler and :2650-2728 LV_EVENT_KEYBOARD handler: ~140 lines of
//            UTF-8 cursor moves, backspace, Esc = leave, Enter = save, Fn+Z/C cursor aliases
```

After (sketch; the async save, BlueZ call and error state move unchanged into `BluetoothAliasModel`):

```cpp
// settings/native/bluetooth_alias_page.cpp - no coordinates, no fonts, no key handling
void build_bluetooth_alias_page(ui::Page &page, BluetoothAliasModel &model)
{
    page.title("Bluetooth name");                       // Back, title, status icons; two-pane on Wide/Large
    ui::Section &form = page.section();
    ui::TextFieldRow *name = form.text_field({
        .label = "Name",
        .initial = model.alias(),
        .type = ui::TextType::Text,                     // keyboard layout; Number -> number pad; Password -> masked
        .max_bytes = CP0_BT_NAME_MAX - 1,
        .on_submit = [&model](std::string v) { model.save(std::move(v)); },   // Enter, keyboard Enter or Save
    });
    form.message(model.error());                        // bound label, hidden while empty
    page.actions({{"Save", ui::Role::Primary, [&model, name] { model.save(name->text()); }}});
    page.busy_while(model.saving());                    // spinner, inputs disabled, Back still works
    page.initial_focus(name);                           // TEXT context, keyboard request, scrolled above the keyboard
}
```

Pages with no custom code at all: the tree renderer maps every `SettingEntry` to a widget.

```cpp
void render_section(ui::Page &page, NodeIter node)                     // replaces LvSettingRoller + RollerPage2
{
    page.title(node->label);
    ui::Section &s = page.section();
    for (auto child = node.begin(); child != node.end(); ++child) {
        if (child->icon_enabled && child->has_api())        s.toggle(child->label, ToggleBinding{child}); // same API
        else if (auto *builder = native_pages().find(child)) s.nav(child->label, [=] { page.push(*builder, child); });
        else if (child.number_of_children() > 0)             s.nav(child->label, current_choice(child),
                                                                   [=] { page.push(choice_page, child); });
        if (child->activation_gate) s.last().gate(child->activation_gate);                       // blocked dialog
    }
}
```

Value pages keep their logic: `initial_selection()` / `activate_selected()` of Brightness, DarkTime, Touch, RTC and
Confirm move into small `ChoiceBinding` classes that both renderers can call. Hardware gating stays where it is
(the tree is built the same way), so `settings_hw_profile.hpp` keeps hiding items without any change.

### 3.6 (f) Contract for stock apps, full-screen apps and later responsive apps

**Stock apps (unchanged canvas).** The vfb stays 320x170 (`applaunch_vfb.h:22-23`). Placement rule, computed by the
layout service:
1. `scale` = the largest integer with `320*scale <= W` and `170*scale + toolbar_min <= H`, where `toolbar_min` = the
   9 mm target. `APPLAUNCH_COMPAT_SCALE` can lower it.
2. Toolbar height = clamp(remaining, toolbar_min, toolbar_max).
3. The window is centred horizontally, and vertically in the area above the toolbar; the remaining space is black.
4. Deck and 480x320 keep their pinned placement (top-aligned 2x + 140 px; 1x at y 25 + 100 px).
5. Results:
   - 800x480: 2x, 640x340 at x 80;
   - 720x720: 2x, 640x340, plus 380 px of spare height (a bigger d-pad layout is a design option);
   - 1280x720: 3x, 960x510, toolbar about 160 px (today's rule gives 4x with a 40 px toolbar, report 021).
   - Using the side space (a right-hand rail) instead of a bottom bar on wide screens is a Controller design option.

**Size-query API for responsive apps (to publish in P5; the launcher side is cheap in P1):**
- Environment exported to every child the launcher starts: `APPLAUNCH_SCREEN_W`, `APPLAUNCH_SCREEN_H`,
  `APPLAUNCH_SCREEN_ROTATE`, `APPLAUNCH_SCREEN_PPMM` (x100), `APPLAUNCH_SCREEN_CLASS`, plus the resolved `APPLAUNCH_FB`
  and touch values (021 asked for the touch export). Today nothing is exported (V grep: no `setenv`; the children only
  inherit `board.conf` variables when they are set).
- `$XDG_RUNTIME_DIR/applaunch/screen.state`, written by atomic rename like F1's `input.state`:
  `v=1 w=640 h=480 rot=0 ppmm=1130 class=standard compat=0,0,640,340 scale=2 toolbar=140 kb_inset=0 gen=G`. Apps
  watch it for the keyboard inset (`gen` changes).
- `cp0_ui_metrics.h` (section 3.3), versioned with `sdk_version.txt`, so a native app computes the same tokens with
  `cp0_ui_metrics_from_env()`.
- VFB2 header fields (bundle with F1 phase 2 and the 64 KiB header needed for 16 KB-page kernels, report 021):
  `host_w`, `host_h`, `scale`, `ppmm_x100`, `canvas_mode`.
- `.desktop` keys:
  - `X-Canvas=320x170` (default);
  - `X-Canvas=fill`: a responsive compat app gets a vfb as large as the window area and a 1:1 blit (the shim already
    reports the header's size to `FBIOGET_VSCREENINFO`, I);
  - `X-Fullscreen=true` (unchanged; the app reads the environment or the state file);
  - `X-MinSize=WxH` (021: hide or grey the tile on smaller screens).

---

## 4. Verification without a person at the board

**SDL is not the route (V).**
- The SDL build compiles `native_ui` down to no-ops (`native_ui.cpp:7`, :792-806) and swaps every cp0 platform source
  for `src/sdl` (`ext_components/cp0_lvgl/SConstruct:78-89`). An SDL launcher shows only the stock 320x170 UI, at
  `LV_SDL_VIDEO_WIDTH/HEIGHT` default 320x170 (`sdl_lvgl_display.c:39-41`).
- WSL today has no SDL2 dev files (`pkg-config sdl2`: not found), and installing packages is an approval gate.

**Proposal: a headless render harness** (`projects/APPLaunch/tools/render-harness/`).
- **Precedent:** `tests/low_battery_ui/run_tests.sh:31-119` already compiles all of LVGL 9.5 with the host compiler,
  creates a display with no hardware (`test_low_battery_ui.cpp:568-569`), stubs `cp0_signal_*` with eventpp handlers
  (:576-600), and snapshots (:110). That script needs SDL2 only for its second backend.
- **Host toolchain (V, `wsl` query today):** g++/gcc 13.3.0, freetype2 26.1.20, libpng 1.6.43. Missing: sdl2,
  libinput, xkbcommon, libudev, dbus-1, gio-2.0. So the harness must not link cp0 services (stubs instead) and must not
  need SDL. The PNG encoder is in LVGL's lodepng (`lodepng.c:7164-7182`, V).
- **Pieces:**
  1. **The real display manager on a memory framebuffer.** Compile `cp0_lvgl_dpi_scaled.c` and `cp0_fb_output.c` (the
     latter already unit-tested on the PC, `tests/test_fb_output.c`) with a test-only branch
     (`APPLAUNCH_FB=mem:WxHxBPP`, about 30-40 lines) that skips `open`/ioctl/`mmap`. Native flush, compat 2x/1x blit,
     toolbar, overlay rect, external blit, rotation and RGB565 all run exactly as on the board, so a frame equals a deck
     framebuffer capture (the 3A+ rotated 16 bpp path can be rendered too).
  2. **Key backend:** a harness `cp0_keyboard_inject` / input context / queue (precedent `keyboard_backend.c`). Touch
     through a FIFO fed with `input_event`s as `APPLAUNCH_TOUCH_DEV` (I: the open and the ABS fallback work on a
     FIFO), or a direct hook.
  3. **Service stubs and fixtures:** config get/set in memory; Wi-Fi scan list, BT devices and agent requests, apps
     registry, sudo, timedate, all in the documented wire formats (`docs/cp0_lvgl.en.md`).
  4. **Deterministic time:** `lv_tick_set_cb` on a fake clock advanced by the runner until timers and animations are
     idle.
  5. **Scene scripts:** `size 480x320 ppmm 610`, `launch Settings`, `key DOWN`, `tap 120 200`, `shot settings_root`.
  6. **Outputs:** `out/<WxH>/<scene>.png`, one contact sheet per scene with all sizes side by side (one image read for
     the Controller), and a diff against `golden/` (640x480 and 480x320 baselines rendered from the code before each
     refactor) that fails on any changed pixel.
- **Sizes:** 480x320, 640x480, 800x480, 720x720, 1280x720, plus 1024x600 and a rotated 320x480 RGB565 buffer.
- **Cost:** savvy-heavy, 1-2 runs, about 900-1,300 lines (runner 400, stubs and fixtures 300-500, build script 150,
  diff and contact sheet 150, display seam 40). First LVGL compile about 1-2 min, scenes seconds (I).
- **Risks:**
  - the device draws with NEON (`LV_DRAW_SW_ASM_NEON`, config :96-97) and the host does not: possible 1-LSB blend
    differences (I), so goldens compare harness to harness, and the deck truth stays `deck.py shot`;
  - stubs drift from the real services (fixtures reviewed with each page);
  - the pages start worker threads, so the stubs reply synchronously and the runner pumps the async dispatch timers;
  - the harness proves layout, not touch feel or speed: the user still tests physically once per phase.
- **Optional later:** the same objects linked against SDL for an interactive window, if the user approves installing
  `libsdl2-dev` in WSL.

---

## 5. Migration plan

| Phase | Content | Tier | Size | Depends on | User tests physically |
|---|---|---|---|---|---|
| **P1a** Render harness | section 4; goldens of today's home, toolbar + stock app, Calculator, screensaver, compat Settings at 640x480 and 480x320 | savvy-heavy | ~10 new files, 900-1,300 lines, 1-2 runs | T1-T4 merged | nothing |
| **P1b** Layout service | `cp0_ui_metrics` + tests; density from `APPLAUNCH_PANEL_MM`/fb mm/default; classes and pinned presets; compat placement moved into the shell (`cp0_display_configure_compat`); environment + `screen.state` export | savvy-heavy | ~8 files, 500-800 lines, 1 run | P1a, decisions D4, D5 | nothing (deck and 3A+ identical by golden diff) |
| **P1c** Home grid, status bar, toolbar on tokens | `native_ui.cpp` view split from platform code; presets for Wide/Square/Large after the Controller's design | savvy-careful | 3-4 files, 250-450 lines, 1 run | P1b, Controller design | deck and 3A+: home grid, toolbar with a stock app, Esc hold, touch corners: unchanged |
| **P2a** Widgets + native Settings host | widget library, page scaffold (single / two-pane), tree renderer (root, sections, toggles, gates, warnings), `ChoiceBinding` extraction (Brightness incl. gpio On/Off, DarkTime, Touch, Launcher, Date & Time manual, Reboot/Shutdown); runtime switch `APPLAUNCH_SETTINGS_UI=native|compat` (deck default compat until D2) | savvy-heavy | ~14 files, 2,000-2,800 lines, 2 runs | P1b, Controller Settings design (D1) | 3A+ (and the deck with the switch on): every section by touch only and by keyboard only, toggles, brightness, DarkTime, Touch choices still applied to Snake/Tank, Launcher toggles, manual time, Reboot cancel, hold Esc from a deep page |
| **P2b** On-screen keyboard (F1 phase 1 re-scoped) | keyboard widget + controller + `TextFieldRow` integration + inset/scroll; press+release injector, text hint, Auto/Off entry | savvy-heavy | ~6 files, 800-1,200 lines, 1-2 runs | P2a, F1 phase 0 (other worker) | no keyboard: field never hidden at 480x320 and 640x480; USB in/out hides/shows; on-screen Esc never restarts the launcher |
| **P3a** Info pages | User, Storage, Licenses, About (board row), Date & Time Info | savvy-medium | 4 files, 400-600 lines, 1 run | P2a | read each page on both boards |
| **P3b** Wi-Fi | list, password, hidden network, connecting, power warning; extract the scan/connect state machine from the view | savvy-heavy | 3-4 files, 600-900 lines, 1-2 runs | P2b | join with password (keyboard and on-screen), hidden SSID, forget |
| **P3c** Bluetooth | paired/scan lists, pairing dialog (confirm, authorize, PIN, 6-digit passkey on the number pad), alias | savvy-heavy | 3-4 files, 800-1,100 lines, 2 runs | P2b | pair a keyboard with no keyboard attached (PIN/passkey), rename alias |
| **P3d** Apps + sudo prompt (+ SSH form if D7) | Apps page; sudo prompt sized by the metrics on the native display and opted into the keyboard (compat apps keep today's 240x116 box); SSH form | savvy-medium (heavy if the sudo flow fights back) | 4-6 files, 600-900 lines, 1-2 runs | P2b | add a source, install and remove an app with the sudo password typed on screen |
| **P3e** Deck switch and cleanup | native Settings default on the deck after D2; delete the legacy roller renderer and the 7,440 lines of dead LVGL examples (D9) | savvy-light/medium | -3k to -10k lines, 1 run | P3a-d, D2 | full Settings pass on the deck |
| **P4** Calculator, screensaver, toast, media OSD, loading, Esc ribbon | onto tokens; deck and 3A+ pinned | savvy-careful | 6 files, 300-500 lines, 1 run | P1b | Calculator, screensaver dim/wake, volume/brightness OSD, Hold-Esc ribbon |
| **P5** App contract | publish `cp0_ui_metrics.h` in the SDK, VFB2 fields with F1 phase 2, `.desktop` keys, `HOSTING-APPS.md`; first responsive app (Mesh Hop) is an apps-repo task | savvy-medium | ~6 files, 300-500 lines + docs, 1 run | P1b, F1 phase 2 | none in the launcher |

**Deck pixel identity.**
- Home, toolbar, Calculator, screensaver and toasts: kept identical by the pinned presets and the golden diff (P1, P4).
  It costs almost nothing.
- Settings cannot be pixel-identical once it leaves the compat window: FreeType at 2x size does not match pixel-doubled
  text.
- The cheap way to keep the deck unchanged during P2-P3 is the runtime switch (deck on compat Settings).
- Running two Settings renderers for long is the expensive part: models must be extracted from view classes that the
  legacy renderer still uses. **A single deliberate Settings change on the deck at the end of P3 is cheaper overall**
  (decision D2).

**The four specific items.**
- **Settings > Touch gesture mapping:** kept. It still drives stock apps and the compat-hosted games
  (`native_ui.cpp:669-675`, :765-772). Its page becomes a list of apps with an Off / Swipe / Swipe + Fire choice (or a
  segmented row). LIST mode is no longer used by the shell after P2; keep it in the display layer until a later
  cleanup.
- **Hardware-profile hiding** (`settings_hw_profile.hpp:13-84`, plus the runtime checks for backlight kind and
  `APPLAUNCH_BOARD`): unchanged, because it shapes the tree that the native renderer reads. Tree construction moves to
  a function shared by both renderers. Stock-only pages (Speaker, Ethernet, Battery, ADB, Software update) are never
  built on the Pi and stay on the legacy renderer.
- **Brightness On/Off (T4):** the same entries ("On", "Off 10 s") and the same `activate_selected` logic with the
  countdown status (`settings_page.cpp:598-613`, `settings_brightness_page.hpp:61-65`) become a `ChoiceBinding`. A
  sysfs backlight becomes a slider or a choice list (Controller design).
- **About board row:** the About info page keeps its rule (shown only when `APPLAUNCH_BOARD` is set,
  `settings_page.cpp:790-797`) and gets a "Board" info row.

---

## 6. Out of scope, and the transitional rule

**Out of scope, and why.**
- **The Store**: an external compat app (`@appstore_exec`, `builtin_app_registry.cpp:76-77`), from a submodule that
  must not change (report 020).
- **Stock CardputerZero apps**: prebuilt binaries drawing a 320x170 framebuffer through the vfb shim.
- **The other native and launcher-hosted apps:** Snake, Tank, IP Panel, SSH form, CLI and Python terminals. The
  terminal is already adaptive; the others are 320x170 compat pages.

All of them work as they are inside the scaled window, and making them responsive gains less than Settings. They
benefit from the same contract later (P5).

**Transitional rule (all phases).**
1. Anything that is not a migrated shell page goes through the unchanged compat path: `begin_page(false)` or
   `run_external`, then `enter_compat` (`native_ui.cpp:650-702`, :781-788). It gets the 320x170 compat display, the
   vfb, the integer-scaled window and the toolbar.
2. The compat display is never resized. The shell only decides the window's scale, position and toolbar through the
   layout service, once at start.
3. Native shell objects are always created under `DefaultDisplayScope(cp0_display_native())`
   (`native_ui.cpp:150-163`). Compat pages never see the native metrics.
4. On app exit `show_home()` restores native mode (`launch.cpp:177-185`). A stock app launched from a responsive shell
   behaves exactly as today, only placed by the new rule on new sizes.

---

## 7. Decisions, blockers, effort

### Decisions for the user (with a recommendation)

| # | Decision | Recommendation |
|---|---|---|
| D1 | Settings look per class: keep the centre-highlight roller look, or move to a conventional touch list with a Back header; single pane everywhere, or two panes on Wide/Large | Controller design pass. My recommendation: touch list + Back header, two panes on Wide and Large |
| D2 | Deck Settings: keep the compat Settings on the deck until later, or accept one deliberate change | keep compat during P2-P3 (runtime switch), accept native on the deck at P3e, then delete the legacy renderer |
| D3 | During migration, may a native Settings open an unmigrated custom page in the compat window? | dev builds only; release native Settings on a board when all its visible pages are migrated |
| D4 | Density source | `APPLAUNCH_PANEL_MM=WxH` in `board.conf` (`install.sh` writes it for known panels), then fbdev mm, then 11.3 px/mm |
| D5 | Touch target sizes | rows 7 mm, primary buttons and toolbar 9 mm, floors 40/48 px |
| D6 | F1 sequencing: build F1 phase 1 now on compat Settings (occlusion rect, gesture suspension), or on the responsive shell | on the responsive shell (P2b): avoids throwaway work on Settings. The occlusion rect moves to F1 phase 2 (compat apps). Cost: the on-screen keyboard arrives later than in the 0.5.0 plan |
| D7 | SSH form (a launcher text form): migrate to native in P3d, or leave it in compat and keep the occlusion-rect keyboard for it | migrate (small: `ui_app_ssh_view.cpp` 248 lines) |
| D8 | Stock-app window on large or square screens: bottom toolbar, side rail, or a bigger d-pad in the spare space | Controller design; the rule in 3.6 works with any of them |
| D9 | Delete the legacy roller renderer (and the dead LVGL examples) after D2 | yes. Only the stock CardputerZero build would need them, and it is not a target of this fork (I) |
| D10 | When to publish the app contract (`cp0_ui_metrics.h`, VFB2, `.desktop` keys) | export the environment and `screen.state` in P1 (internal); publish the API in P5, after Settings proves it (as 020 D9) |
| D11 | Allow the shared status bar above 100 % for the Large class (renderer shared with apps) | yes, with the apps' copies updated in the same change |
| D12 | Portrait logical canvases | best effort only; not an official target |

### Blockers

- The Controller's visual design: home grid for 800x480, 720x720 and 1280x720; the Settings pattern (D1); the
  keyboard look; the large-screen toolbar (D8).
- T1-T4 are uncommitted (V `git status`): they must be merged before P1a, because they are the golden baseline.
- F1 phase 0 (multi-keyboard enumeration, other worker) must land before P2b.
- Panel sizes for the HyperPixel 4.0, HackBerry 720x720 and uConsole 5" are spec values (I); a probe or ruler
  measurement is needed for exact tokens.
- Pi 5, HackBerry and uConsole are later releases (012 F2; the uConsole needs glibc >= 2.38 and has no touch, 021
  add. 5). Until then the Wide, Square and Large classes are verified only in the harness.

### Effort (rough, worker run + Controller review)

| Phase | Tier | Tokens (approx.) |
|---|---|---|
| P1a harness | savvy-heavy | 0.5-0.8M |
| P1b layout service | savvy-heavy | 0.3-0.5M |
| P1c home/toolbar (+ Controller design) | savvy-careful | 0.25-0.4M (+0.1M) |
| P2a widgets + Settings host (+ design) | savvy-heavy | 0.7-1.1M (+0.15M) |
| P2b on-screen keyboard | savvy-heavy | 0.5-0.8M |
| P3a info pages | savvy-medium | 0.15-0.25M |
| P3b Wi-Fi | savvy-heavy | 0.4-0.6M |
| P3c Bluetooth | savvy-heavy | 0.5-0.8M |
| P3d Apps + sudo (+ SSH form) | savvy-medium | 0.35-0.55M |
| P3e deck switch + cleanup | savvy-light/medium | 0.1-0.2M |
| P4 Calculator, screensaver, toasts | savvy-careful | 0.25-0.4M |
| P5 app contract | savvy-medium | 0.2-0.3M |
| **Total** | | **about 4.5-7M** |

No phase needs savvy-fable unless the async extraction in P3b/P3c resists (then one fable run for that page).

### Risks

- **Keyboard parity.** Touch-native widgets must stay fully usable from the BT keyboard. Mitigation: key handling in
  the widgets, plus harness key-sequence scenes.
- **Async lifetime.** The Wi-Fi, BT and Apps view classes hold their state machines (generation tokens, dispatch
  queues, explicit use-after-free warnings such as `settings_page.cpp:304-317`). Extracting them is the delicate part.
  Mitigation: model tests before the view rewrite.
- **CPU on the Zero 2 W (I).** Native Settings renders up to 307k pixels per full frame instead of 54k (compat 320x170
  doubled in the blit). Mitigation: no shadows or opacity animations, partial redraws, short scroll animations.
  Measure in P2a on the deck with the switch on.
- **Two renderers in flight** (D2/D3).
- **Harness stubs drifting** from the real services.

## Open questions

1. D1-D12 above, especially D1 (Settings pattern), D2 (deck change) and D6 (F1 sequencing): the last one changes the
   0.5.0 scope.
2. Should 0.5.0 still ship the on-screen keyboard (F1) for the 3A+, if the responsive Settings is not ready in 0.5.0?
   (With D6 = shell, the keyboard ships with P2b.)
3. Panel physical sizes: is the user willing to measure the HyperPixel, HackBerry and uConsole active areas, or are the
   spec values fine?
4. May the Controller install `libsdl2-dev` in WSL later for an interactive viewer? It is not needed for the PNG
   harness.
5. Does the fbdev emulation on the 3A+ and the Pi 5 report the panel's mm in `var.width/height`? This decides whether
   D4 needs `board.conf` at all (one `fbset -i`-style probe line).
6. Should Large-class screens without touch (uConsole) use a keyboard-first variant of the widgets (smaller rows, no
   9 mm floor)? The tokens support it through `touch = 0`; it is a design call.
