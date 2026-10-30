# 046 - Home grid app order: drag and drop on the grid, or an order list in Settings

Status: study written 2026-10-29 from the owner's question "how could the user reorder the apps on the home grid:
drag and drop on the grid, or a manual order list in Settings?". Launcher read at `main` 0e6fcadc. LVGL read at
`SDK/github_source/lvgl/lvgl_9_5` (the tree the launcher builds: `config_defaults.mk:7` `CONFIG_LVGL_9_5_SRC=y`).
Reading only: no product code changed, nothing committed, no board touched. Report number 045 is taken by the
CloudpilotEmu study (apps repo tag `archive/cloudpilot-study`, `ROADMAP.md:40`), so this is 046.

Owner's decisions recorded 2026-10-29 (D2, D3, D5, D6, D8, D9, D11, D12, D13 below). P1 (the model, the apply step,
Settings > Launcher "Reset app order") is implemented on branch `home-order`.

Markers: **S** = read in the source (file:line); **I** = inferred, needs a spike or a board. Cost unit: one worker
session (ws). Paths without a folder are in `projects/APPLaunch/main/ui/`.

## 1. Recommendation

**Option C: the order lives in one small model; the user changes it on the grid itself, with a "move mode" that works
with every input. Finger drag comes later, and only if the owner still wants it after trying move mode. No separate
order list in Settings: Settings only gets "Reset app order".**

- **Move mode (P2).** The user picks up a tile (decided 2026-10-29):
  - touch: hold it for **3 s**, with the hold progress bar the apps already use;
  - keyboard: **Ctrl+O**, the contract's options key, picks up the selected tile at once. There is no long Enter and
    no trackpoint-specific hold: the trackpoint's held click already sends Ctrl+O, and every pointer board also has a
    keyboard or touch.

  Then the arrows move it (clamped, no wrap), a tap on another tile places it there, Enter drops it, and Esc cancels.
  One code path covers every board: the deck (touch and a BT/USB keyboard), the HackBerry (pointer), the tablet and
  PDA portrait.
- **Finger drag (P3, optional).** The tile follows the finger and its neighbours reflow. LVGL 9.5 has what it takes,
  but it costs more than everything else together: scroll suppression, auto-scroll at the edges, a ghost tile on the
  top layer, a guard against the three-finger gesture, and a redraw cost to measure on the Pi Zero 2 W. Its only gain
  over move mode is feel.
- **Settings list (Option B, P4): not built** (D11, decided 2026-10-29). Its only unique value would be "order and
  show/hide in one place". Show/hide already exists in Settings > Launcher.
- **Cost of the main path (P1 + P2 + docs):** about 2.3-3.3 ws. P3 adds 1.5-2 ws. Option B alone would be about
  1.75-2.5 ws.

A premise of the brief to correct: **the deck has a touch panel.** It has board-profile touch axes
(`tools/boards/boards.json`, deck `APPLAUNCH_TOUCH_SWAP_XY=1`), the three-finger hold was measured on it
(`ext_components/cp0_lvgl/include/cp0_gesture_g1.h:48-49`) and report 038 section 2.1 lists it. Keyboard-only use still
matters, because the policy is "BT/USB keyboard first", and move mode covers it.

## 2. Decisions for the owner

| # | Question | Options | Recommendation |
| --- | --- | --- | --- |
| D1 | Approach | A drag and drop / B Settings list / C move mode on the grid, drag later | **C** (section 7) |
| D2 | Pick-up triggers | touch hold; Ctrl+O; long Enter; trackpoint held click | **decided 2026-10-29:** touch = hold a tile **3 s**, with the hold progress bar the apps already use; keyboard = **Ctrl+O at once**. No long Enter, no trackpoint-specific hold (the trackpoint's held click already sends Ctrl+O; the pointer boards also have a keyboard or touch) |
| D3 | Touch, after pick-up | tap the destination tile (it goes there) / drag only | **decided 2026-10-29:** arrows move (clamped), **a tap on a tile places**, Enter drops, Esc cancels; drag only in P3 |
| D4 | What a hold opens | move at once / a small menu (Move, Hide, ...) | **move at once**; a menu is a UI design of its own, later if wanted |
| D5 | New apps (install, first run of a Store app) | at the end / alphabetical among the rest / at the end with a "new" mark | **decided 2026-10-29: at the end, no mark** (today's documented rule, `docs/HOSTING-APPS.md:74-76`) |
| D6 | Can Settings and CLI move? | yes / pinned first | **decided 2026-10-29: yes, every tile moves**; they stay protected from hiding only |
| D7 | Where the order is stored | `config.json` chunk keys / own text file | **`config.json`, keys `home_order`...`home_order5`**, the `app_order` format (section 4.3) |
| D8 | Reset | Settings > Launcher row, with a confirmation / without / none | **decided 2026-10-29: a "Reset app order" row with a confirmation** ("Put the apps back in their default order?", Cancel / Reset) |
| D9 | Per board or shared | per board / synced | **decided 2026-10-29: per board** (each board has its own `~/.config`); copying the keys between boards works because the ids are the same |
| D10 | Finger drag (P3) | build now / after trying P2 / never | **decide after the P2 board test** |
| D11 | Settings order list (P4) | build / skip | **decided 2026-10-29: no Settings list** |
| D12 | The look of move mode (a visual decision) | the picked tile's style; the drop target's highlight; a caption or toast with the keys; animated or jumping neighbours; a first-time hint | **decided 2026-10-29: set by the orchestrator in the P2 brief.** The existing tokens to reuse are the selected tile (accent border) and the pressed colour (`native_ui.cpp:54-57`, `270-274`) |
| D13 | A Settings > Launcher row "Arrange apps" that opens the home grid already in move mode (to make the feature findable) | yes / no | **decided 2026-10-29: no row**; the hold on the grid is enough, and a README line covers discoverability |

## 3. How the home grid works today

### 3.1 Where the tiles come from

- **Built-in tiles,** in this table's order: Settings, Store, CLI, Python, SSH, IP Panel, Calculator, Snake, Tank. SSH
  and IP Panel are Linux only (S: `builtin_app_registry.cpp:73-95`). Only enabled tiles are added
  (`builtin_app_registry.cpp:168-175`, `app_registry.cpp:62-68`).
- **`.desktop` files** come from **one directory**, `/usr/share/APPLaunch/applications`
  (S: `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_filesystem.cpp:172`, `desktop_app_loader.cpp:159`). **There is no
  per-user directory.** A file is skipped when:
  - `X-MinSize` / `X-Portrait` does not fit the panel (`desktop_app_loader.cpp:202-222`);
  - its Exec is unsafe or a duplicate, or it shadows a built-in (`224-242`);
  - its tile is disabled (`303-309`).

  At most 128 are shown (`desktop_app_loader.hpp:14`).
- **The Settings tile** is a built-in page, always on. Settings and CLI are protected: they never get a toggle
  (S: `settings/settings_launcher_model.hpp:26-30`).
- **`APPLAUNCH_HIDE_TILES`** (board.conf) only sets the **default** of a configurable tile. The user's toggle in
  Settings > Launcher, once saved, wins (S: `model/tile_default_policy.hpp:4-6`, `app_registry.cpp:46-50`). The
  toggles cover built-ins and preinstalled apps. A Store-installed app is not configurable (always on): it goes away by
  being removed (S: `desktop_app_loader.cpp:291-302`, `settings/settings_launcher_model.hpp:41-45`).

### 3.2 The current order rule

`Launch::applications_load()` (S: `launch.cpp:293-297`, after `rebuild_builtin_apps()` `397-405`) builds the order in
three steps:

1. **Built-ins** are added in table order.
2. **`.desktop` apps** come next. They are sorted by file time, oldest first (`desktop_app_loader.cpp:53-62`), then by
   the saved **`app_order`** (`67-102`; model `model/desktop_app_order.hpp:7-20`):
   - a known app keeps its place, a new one goes last;
   - the keys of removed apps are kept (135 at most), so a reinstalled app returns to its old place.

   This is task 006's "tile keeps its place on upgrade" (`docs/dev/decisions.md:69-71`).
3. **A stable sort on a hardcoded label table** (`builtin_app_registry.cpp:177-187`, table `app_display_order.cpp:16-41`):
   - an app whose name matches the table goes to the table's position;
   - everything else keeps its step-2 order, after them.

   The match is **by normalised name** (`app_display_order.cpp:76-88`). A Store app called "Music", "Files" or "GPS"
   therefore jumps into the built-in block. This is an upstream leftover, harmless today, and the user order would
   override it.

So the grid is: the built-ins in table order, then our apps in install order. It is neither sorted by name nor in
directory order.

### 3.3 Layout, paging, scrolling

- **One vertical flex grid, no pages.** Row wrap, vertical scroll, **snap to row start**, no scrollbar
  (S: `native_ui.cpp:238-251`).
- **Columns and rows come from the layout service:**
  - deck 640x480 and Pi 3A+ 480x320: pinned presets of 3x2 (`ext_components/cp0_lvgl/src/cp0_ui_metrics.c:39,46`);
  - other sizes: computed from 20 mm tiles, 2-8 columns (`cp0_ui_metrics.c:260-326`);
  - 720x720 HackBerry: 3x3 (`tests/test_ui_metrics.c:505`);
  - PDA portrait 480x800 and 480x640: **3 columns** (`tests/test_ui_metrics.c:540-549`; the home grid gets the whole
    panel because the writing band is on demand).

  The tablet's 800x1280 and 1280x800 are computed: I did not run the numbers.
- **The order is one-dimensional, so it fits every column count.** A PDA-mode switch (3 columns) or a landscape tablet
  shows the same sequence.

### 3.4 Focus and selection

- **Keys** go through `LV_EVENT_KEYBOARD` on the home screen (S: `native_ui.cpp:196-216`):
  - arrows (and the F/X/Z/C aliases) move by 1 or by one row, on press and on repeat;
  - **Enter launches on release**;
  - **Esc and Ctrl+O do nothing on the home grid.**

  Selection is an index, `s_selected`, that **wraps** at both ends (`161-171`).
- **Touch:** `LV_EVENT_CLICKED` on a tile launches the tile whose **index was captured when the tile was built**
  (`180-183`, `275-276`).
- **Trackpoint or mouse** (Settings > Pointer navigation, installed on the home grid at `native_ui.cpp:731`):
  - motion = arrows; click = Enter;
  - **a held click (0.6 s) = Ctrl+O** (S: `ext_components/cp0_lvgl/src/cp0/cp0_pointer_reader.c:173-176`,
    `docs/APP-CONTRACT.md:406-417`).

### 3.5 Long-press today

**No tile handles a long press** (the only `LV_EVENT_LONG_PRESSED_REPEAT` is the compat toolbar's,
`native_ui.cpp:383`). Two facts matter for adding one:

- **In LVGL 9.5, `LV_EVENT_CLICKED` is still sent on release after a long press.** Only `SHORT_CLICKED` is skipped
  (S: `lv_indev.c:1528-1531`). Today a long press on a tile therefore launches the app on release. A pick-up must
  switch the tile to `LV_EVENT_SHORT_CLICKED` or swallow that click.
- **The pointer indev keeps LVGL's defaults:** long press 400 ms, scroll limit 10 px (S: `lv_indev.c:32,39`). cp0 does
  not change them (`cp0_lvgl_dpi_scaled.c:762-769`).

The system gestures near the grid:
- the status-strip long press (G2, 0.5 s) is on the strip, not on the grid;
- the three-finger hold (G1, 1 s after the third finger lands) works anywhere (section 5.4);
- study 026 left one-finger long presses to apps and to the launcher's own content (026 section 3.2).

### 3.6 What depends on a tile's position

- **The click callback's captured index** (`native_ui.cpp:275-276`). Reordering tiles in place with
  `lv_obj_move_to_index` without a rebuild would launch the wrong app. Either rebuild on drop (`refresh_apps()`,
  `681-695`) or read `lv_obj_get_index()` at click time.
- **`s_selected` is kept by index across a rebuild** (`693`). After a move it should follow the moved app.
- **The resident dots** (multitasking P-d) are matched **by Exec**, then placed per index after each rebuild
  (`308-340`). A rebuild after the drop keeps them right.
- **Not index-based:**
  - the switcher (cards most recent first, icons looked up by Exec, `native_ui.cpp:704-712`,
    `multitask/switcher_view.hpp:69`);
  - the automation RPC and `launcher.sock` (no tile verbs found);
  - the board tools (`tools/boards/board.py`: no tile taps).
- **I found no "tile missing" logic and no recents list for tiles.**
- **The render harness builds its own app list** (`tests/render/src/harness_launch.cpp:63-72`). The goldens do not
  change while no user order exists.

## 4. Where the order lives

### 4.1 The settings store

- **Where:** `~/.config/cardputerzero/config.json` (S: `ext_components/cp0_lvgl/src/cp0/cp0_lvgl_config.cpp:25-35`). It
  is per user and per board.
- **Format:** a flat key/value store. **64 entries at most, keys up to 63 characters, values up to 255**
  (`cp0_config_service.hpp:34-36`).
- **Atomic multi-key write:** `SetManyAndSave` (`cp0_config_service.cpp:145-185`). It **fails cleanly** when the store
  is full (`166`).
- **Silent failure on full:** plain `SetStr`/`SetInt` **silently drop a new key** when the store is full and still
  answer "ok" (`92-105`, `102`). This is an existing trap: order writes must use `SetManyAndSave`, as `app_order`
  already does (`desktop_app_loader.cpp:92-101`).
- **The budget is real:**
  - the Store's mirrored source list alone is 19 keys (`settings/settings_apps_registry_mirror.hpp:6-13,25-27`);
  - `app_order` uses 1-5 keys;
  - then `touch_<app>`, `app_*` toggles, `osk_mode`, `pda_mode`, `pointer_nav`, `dark_time`, `volume` and the rest.
- **Who writes it:**
  - the launcher (Settings, `app_order`);
  - the bundled Store (its `appstore.registry.*` copy).

  Whether a Store run and a launcher write can clobber each other was not checked (section 10).
- **How Settings > Apps and the Store change the set of apps:** they install or remove `.deb` packages that add or
  remove a `.desktop` file. The directory watcher (`launch.cpp:58`) or the 3 s icon poll (`361-372`) then calls
  `applications_reload()` (`323-328`). A changed Settings > Launcher toggle reloads through
  `app_registry_changed_cb` (`407-415`).

### 4.2 Rules the order must survive

| Event | Rule |
| --- | --- |
| No user order (every install today) | **exactly today's grid** (section 3.2); nothing is written |
| Upgrade or reinstall of an app | keeps its place: the id comes from the `.desktop` **file name**, which an upgrade keeps |
| New app (install, a new release's preinstalled app, Klondike-style) | **at the end** (D5), in today's fallback order among several new ones |
| Removed app | its id stays in the list (bounded), so a reinstall returns it to its old place; it takes no slot on the grid |
| Tile hidden (toggle or `HIDE_TILES` default) | keeps its slot in the list; shown again, it comes back where it was |
| `X-MinSize` hides an app on this board | as hidden; the same id on a bigger board keeps its place there |
| Same image on several boards | each board has its own list; an unknown id is ignored, a missing one is appended |
| A built-in tile enabled later | at its saved slot, because the first save stores **every registered tile**, hidden ones included (below) |
| Reset | the user list is cleared: today's rule again |

### 4.3 Proposed data model

- **Tile id = the tile's existing `config_key`.**
  - Built-ins have `app_Setting`, `app_Store`, `app_Python`, and so on (`builtin_app_registry.cpp:74-94`).
  - `.desktop` apps have `app_desktop_<name>_<fnv>`, made from the **file name only** (`desktop_app_loader.cpp:114-131`),
    which an upgrade keeps.

  Every tile already has one and they are unique. **`struct app` has no id field today** (`launch.h:111-135`): add one,
  set by the built-in appender and by the desktop loader.
- **Stored value:** an ordered list of the FNV-1a keys of those ids, as comma-separated hex. It goes in the
  **`home_order`, `home_order2` ... `home_order5`** chunks (27 keys per chunk, 135 at most): the exact format and helpers
  of `model/desktop_app_order.{hpp,cpp}` (`parse`, `format`, `split_chunks`). Normally 1-2 keys are used.
- **Apply:** after today's three steps, if a user list exists, put the visible tiles in its order (the `arrange()` rule
  of `desktop_app_order.cpp:69-106`):
  - known ids first, in the user's order;
  - unknown ids after them, in today's order;
  - a hash collision never hides a tile.
- **Save after a move:** the order is merged into the stored list by **filling slots**. Hidden and removed ids keep
  their places; the visible ids fill the visible slots in their new order. The first save takes the list from
  `launcher_app_registry_entries()` (`builtin_app_registry.cpp:129-143`), which includes hidden built-ins and disabled
  preinstalled apps. One `SetManyAndSave` per drop.
- **`app_order` stays as it is.** It remains the default order's memory, so Reset can restore today's grid exactly.
- **Pure model and unit tests:** a `model/home_order.{hpp,cpp}` next to `desktop_app_order`, with
  `test_home_order.cpp` registered in `tests/run_tests.sh` like `test_desktop_app_order` (`run_tests.sh:435-438`).

**Rejected alternatives:**
- **Overload `app_order`.** Built-ins would have to enter it, and Reset could not restore the install order.
- **Store a full grid with holes, iOS style.** It breaks when the column count changes (PDA mode, landscape tablet).
- **A text file** `~/.config/cardputerzero/home-order`, with readable ids and outside the 64-key cap. It is a fair
  option (D7), but it adds a second persistence path with its own atomic write, while the chunk format is already
  tested and documented (`NAMING.md:39`).

### 4.4 Migration

None. With no `home_order` key the grid is today's, byte for byte. A downgrade to an older launcher ignores the unknown
keys.

## 5. Option A: drag and drop on the grid

### 5.1 What LVGL 9.5 gives

- **Built in:**
  - `LV_EVENT_LONG_PRESSED` (sent only if no scroll started, `lv_indev.c:1426`);
  - `LV_EVENT_PRESSING` with `lv_indev_get_point`;
  - `LV_OBJ_FLAG_PRESS_LOCK` (`core/lv_obj.h:63`);
  - `lv_obj_move_to_index` / `lv_obj_swap` (`core/lv_obj_tree.h:91,101`): the flex layout reflows the neighbours, with
    no animation.
- **Not built in:** a sortable grid widget.
- **Scrolling can still start after a long press,** because the scroll handler runs on every pressing frame
  (`lv_indev.c:1414`). A drag must therefore clear `LV_OBJ_FLAG_SCROLLABLE` on the grid while it holds a tile
  (`native_ui.cpp:249`) and restore it on release (I: clearing the flag is enough to stop LVGL's scroll search; read in
  `indev/lv_indev_scroll.c:327`).

### 5.2 Touch design (P3)

1. **Pick-up.** A launcher timer of 3 s starts on the tile's PRESSED event, with the hold progress bar the apps already
   use. It is cancelled by release, PRESS_LOST or a scroll start (a second contact does not cancel it, decided 2026-10-30). LVGL's own 400 ms long press is not
   used, because `long_press_time` is per indev and would change every page.
2. **Drag.** The grid's scrolling is switched off. A **ghost** (a copy: background, icon, label) on `lv_layer_top()`
   follows the finger. The real tile stays in the flex row as the hole. When the finger passes the middle of another
   slot, `lv_obj_move_to_index(hole, k)` reflows the row.
3. **Edge.** Within one tile's height of the grid's top or bottom, a timer scrolls the grid by one row (snap kept) while
   the finger stays there (I: programmatic `lv_obj_scroll_to_y` with the flag cleared; to check).
4. **Drop.** On release: remove the ghost, `refresh_apps()`, select the moved app, save.
5. **Cancel.** Esc on a keyboard, the three-finger gesture taking the touch (PRESS_LOST), or a grid reload (install
   finished, icon changed) puts the tile back. The tile's next CLICKED must be swallowed (section 3.5).

Neighbour animation would need per-tile translate animations on top of the flex positions: optional (D12).

### 5.3 Keys and pointer: move mode

There is no drag without a pointer, so a keyboard user needs **move mode** anyway:
- Ctrl+O picks the selected tile at once (the trackpoint's held click sends Ctrl+O, so the pointer needs nothing of
  its own; there is no long Enter);
- the arrows move it one slot or one row, clamped (no wrap), the view following with `lv_obj_scroll_to_view`; a tap on
  a tile places it there;
- Enter drops; Esc puts it back.

This is P2. With it, P3 is only "the finger can also carry the tile".

### 5.4 Accidental pick-ups and the three-finger gesture

- **Scroll or pick-up:** LVGL decides first. Movement of more than 10 px is a scroll, and no long press follows
  (`lv_indev.c:1426`). A 3 s hold spares a user who rests a finger before scrolling.
- **Three-finger hold (G1):** it fires 1 s after the third finger lands (`cp0_gesture_g1.h:45-49`). It is on by default
  where multitasking is, at 640x480 and up (`docs/dev/ROADMAP.md:73`). A single-finger 3 s pick-up **cannot fire
  before** G1: the fingers land within a moment of each other, so G1 (1 s after the third) ends the press first.
  - G1's swallow ends the press with PRESS_LOST (`cp0_lvgl_dpi_scaled.c:459-473`), so the pick-up timer must still
    cancel on PRESS_LOST.
  - A second contact landing does not cancel the hold (decided 2026-10-30; the accessor for it was removed).
  - The HackBerry and the tablet report at most 2 contacts, so G1 never fires there (`ROADMAP.md:73`, report 028).
- **Reloads during a move:** an install job from Settings > Apps can finish while the user is on the home grid, and
  `refresh_apps()` cleans the grid (`native_ui.cpp:687`). Move mode must end first (drop in place).

### 5.5 Cost on the Pi Zero 2 W

None of this is measured: to measure in P3's first quarter session.

- **Memory:** one ghost (3 objects) and one timer: negligible.
- **Redraw:** each move frame redraws the ghost's old and new rectangles. On the deck a tile is about 192x180 px, and
  every icon is drawn **scaled** (`LV_IMAGE_ALIGN_STRETCH`, `native_ui.cpp:281`), from a 1 MB image cache
  (`config_defaults.mk:81`).
- **Slot changes:** each one reflows and redraws the whole grid area, about 640x424 on the deck. This happens once per
  slot change, not per frame.

I expect a usable frame rate for a ghost of one tile, with a short hitch at each reflow.

### 5.6 Effort

- **P2 move mode:** 1.25-1.75 ws.
- **P3 drag on top of P2:** 1.5-2 ws, including the perf probe and the G1 guard.
- **A alone, with the model:** about 3.5-4.75 ws.

## 6. Option B: an order list in Settings

### 6.1 Where it fits

- **Entry.** Settings > Launcher is a section rebuilt each time it opens (`settings/settings_page.cpp:230-235,836`;
  `settings/settings_adapter.cpp:319-347`, toggles for the configurable tiles). A first row "Arrange apps" would open a
  new **native page**. The steps are in `docs/dev/native-settings-page-howto.md`:
  - a value of `SettingsNativePage` (`settings/settings_page.hpp:64-80`);
  - a panel on a `cp0::ui::Roller`, like Settings > Apps.
- **The page's rows:** every tile in grid order, hidden ones included at their slot with a grey "hidden" value; the
  last row "Reset to default order".
- **Interaction (B2, pick and move):**
  - Enter or a tap on the highlighted row picks the app;
  - Up/Down, a finger drag of the list, or a tap on another row moves it, so the picked app stays in the highlight
    band while the others slide;
  - Enter drops and saves; Esc puts it back, and a second Esc leaves.
- **What the Roller lacks:** a "selection changed" callback. `RollerEvents` has only `activate`, `slide` and `back`
  (`ext_components/cp0_lvgl/include/cp0_ui_widgets.hpp:115-119`). On each change, two rows swap their text in place
  with `update_row` (`cp0_ui_roller_widget.cpp:631-650`: the drag moves the centre live). That is about 0.25 ws in the
  widget (I).
- **Keys the Roller already uses:** Left = back, Right = activate (`cp0_ui_roller_widget.cpp:599-627`). Move mode
  therefore uses Up/Down only.
- **B1 (no widget change):** Enter opens an action list per app (Move up, Move down, Move to first, Move to last,
  Cancel), the Settings > Apps pattern. Easy to build, slow for long moves.

### 6.2 The row-affordance rule

The rule (chevron = opens, grey = info, dot = action) is implemented in `CompactList` (`RowAffordance`,
`cp0_ui_widgets.hpp:381`). The Settings Roller draws a chevron only on value pickers (`settings/settings_native_host.cpp:
307-310`) and has no action dot.

An order list in the Roller would need either a dot option in the Roller (about 0.25 ws, also usable by Settings >
Apps) or `CompactList` (Comfortable) instead. **This is a visual decision for the owner.**

### 6.3 Hidden apps, `HIDE_TILES`, inputs, effort

- **Show/hide in the same list.** It would need B1's action list ("Show on home" / "Hide"), because Enter on a toggle
  row flips it. The Settings > Launcher toggles already do this. `HIDE_TILES` never needs a hand edit by a user: it is
  the board's default, and the toggle overrides it (section 3.1).
- **Store apps cannot be hidden today** (always on). Making them hideable is a separate decision, not needed here.
- **Inputs:** keys, touch and pointer (the pointer's arrows and click) all work, because the Roller already takes
  them.
- **Effort:** about 1-1.5 ws for the page and 0.25 for the Roller hook. **B alone, with the model: about 1.75-2.5 ws.**
  The SDL simulator keeps the legacy Settings pages, so the native page is absent there (howto section 1).
- **Weaknesses against the grid:** text rows without icons, a one-dimensional list mapped onto a 3-column grid, and
  only 3-5 rows visible on the deck and the Pi 3A+.

## 7. Option C: combined, and what to build first

1. **P1, the model.** It is the same for every option: id on `app`, `home_order` apply and save, Reset. Nothing
   visible changes until P2.
2. **P2, move mode on the grid.** Direct manipulation with icons, on every input, reusing the grid's own selection and
   the pointer's existing Ctrl+O. It is the smallest change that gives the owner's "drag and drop" result on all
   boards.
3. **Then decide (D10, D11).** P3 if the owner wants the finger to carry the tile. P4 only for an all-in-one
   order-and-visibility page.

**Cheap extras the model allows:**
- "Move to first" (pin a favourite) is a move: no separate pin state is needed;
- a per-board default order shipped by `install.sh` (a seeded `home_order`) is possible later;
- copying the keys gives the same order on another board.

**Folders and categories (out of scope)** are not blocked: a folder can be one more id in the same list (for example
`folder:<name>`), with its members stored elsewhere.

## 8. Plan

| Phase | Option | Worker tier | Content | Cost (ws) | Owner test |
| --- | --- | --- | --- | --- | --- |
| P1 | all | savvy-medium | `app` id field; `model/home_order` (apply, slot-filling merge, chunks, bounded list) + unit tests; apply after `launcher_sort_app_display_order`; `SetManyAndSave`; Settings > Launcher "Reset app order" row and confirmation; a render scene with a seeded order | 0.75-1 | with P2 |
| P2 | C, A | savvy-heavy | move mode in `native_ui.cpp`: pick-up (3 s touch hold with the apps' hold progress bar, cancelled on release, PRESS_LOST or a scroll; Ctrl+O at once), arrows clamped, tap a tile = place, Enter = drop, Esc = cancel; tiles launch on SHORT_CLICKED (or the click is swallowed); cancel on reload, launch and lock; selection follows the app; save (`launcher_home_order_save`, "cannot save" when it fails); the look from the P2 brief (D12); README line (D13); scenes at 640x480, 480x800, 720x720 | 1.25-1.75 | 10 min: deck (hold a tile 3 s, tap a destination; keyboard Ctrl+O, arrows, Enter; restart: order kept; install an app: it lands last) + one pass on the tablet or HackBerry (trackpoint held click = Ctrl+O) |
| P3 | A (optional) | savvy-heavy | finger drag: ghost on the top layer, grid scroll off while carrying, reflow by slot, edge auto-scroll, drop on lift, G1 guard (contact-count accessor in `cp0_lvgl`); perf probe on the Zero 2 W first | 1.5-2 | 10 min: deck (drag across a row and down past the edge) + tablet |
| P4 | B (dropped: D11) | savvy-medium | native "Arrange apps" page (Roller pick and move or action list), hidden tiles listed, Reset; Roller selection callback (and action dot if D12/6.2 says so) | 1-1.5 (+0.25) | 5 min on one board |
| P5 | all | savvy-light / docs-writer | README and HOSTING-APPS "Tile position" paragraph, release notes | 0.3-0.5 | - |

**Totals:**
- recommended main path, P1 + P2 + P5: **2.3-3.25 ws**;
- P3 adds 1.5-2;
- Option B instead, P1 + P4 + P5: 2-3;
- full Option A, P1 + P2 + P3 + P5: 3.8-5.25.

## 9. Risks and interactions

- **Long press launches the app.** CLICKED still arrives after a long press (section 3.5). This is the first thing P2
  must handle, or every pick-up would also start the app.
- **Config store full.** At 64 entries a new key cannot be added. `SetManyAndSave` reports it, and the move must then
  say "cannot save" instead of looking saved. `SetStr` would hide it (section 4.1).
- **Three-finger hold over a tile** ends the press before the 3 s pick-up (section 5.4). The pick-up timer must cancel
  on PRESS_LOST (a second contact does not cancel it, decided 2026-10-30).
- **Grid reload during a move** (Store or Settings > Apps job, icon poll, toggle) destroys the tiles. End move mode
  before `refresh_apps()`.
- **Stale indices.** In-place moves without a rebuild break the captured click indices and the resident dots
  (section 3.6). Rebuild on drop.
- **PDA mode.** The order is one-dimensional, so the 3-column portrait grid shows the same sequence. The writing band
  is on demand (`launcher_zone.hpp:1-18`) and never shows on the home grid, so it does not cover the bottom row during a
  move. A PDA-mode change restarts the launcher; the order is in `config.json`, so it survives.
- **The Store and Settings > Apps.** Settings > Apps lists apps **alphabetically** (`settings/apps_backend.cpp:
  257-260`) and the Store has its own order. Neither follows nor changes the home order; only "new app goes last"
  links them. Store apps whose names match the upstream label table are pulled into the built-in block today (section
  3.2). A user order overrides that; Reset brings it back.
- **New apps such as Klondike** land last until moved. With many installs the end of the grid can be far down. A
  "new" mark (D5) would be a visual addition.
- **Future themes.** The move-mode look should reuse the grid's colour constants (`native_ui.cpp:54-57`, more literals
  at `270-274`), so that the theme step (parked idea: one theme file replacing literals) picks them up. Do not add new
  literals.
- **The legacy carousel** (SDL simulator only) gets the order through the model, but no move mode.
- **Discoverability.** A hold on a tile is a convention, not something the user can see. Decided 2026-10-29 (D13): a
  README line, no Settings row; a first-time hint is a UI decision.

## 10. Not verified

- **No build, no board run, no measurement.** The frame rate of a ghost tile on the Pi Zero 2 W and the reflow hitch
  are estimates (section 5.5).
- **LVGL behaviour read but not exercised:**
  - that clearing `LV_OBJ_FLAG_SCROLLABLE` mid-press stops a scroll from starting;
  - that `lv_obj_scroll_to_y` still works with the flag cleared;
  - `move_to_index` reflow timing.
- **Whether a Store run can overwrite launcher keys** written to `config.json` meanwhile, or the reverse. The launcher
  loads the file once and never reloads it (no `Init` call in `main/`). The Store's own config code (`projects/AppStore`)
  was not read.
- **The tablet's computed grid** at 800x1280 and 1280x800 (columns and rows) was not computed.
- **The number of config entries on a real board today** (how close the boards are to 64). It can be read with
  `board.py` (state dump of `config.json`, `tools/boards/board.py:364,432`).
