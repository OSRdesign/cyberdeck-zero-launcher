# App design checklist (`app-design-qa`)

Every launcher design requirement that applies to an app, with how to check it from the app's code and its headless
scenes (no board). `apps-dev` goes through it before a hand-off; the `app-design-qa` agent
([`agents/app-design-qa.md`](../agents/app-design-qa.md)) runs it and returns a verdict. Owner decision of 2026-11-12
([`decisions.md`](../decisions.md)): advisory while an app is built; at a publish an unresolved **FAIL** stops it until it
is fixed or the owner waives it in writing. The scenes it reads are defined in
[`app-design-scenes.md`](app-design-scenes.md).

Only rules with a primary source are here. A layout choice that no item covers is the app's design, decided case by
case with the owner: the QA reports it as a question, never as a FAIL. Rules without a primary source are listed at the
end, under [Needs an owner ruling](#needs-an-owner-ruling).

## Legend

| Severity | Meaning |
| --- | --- |
| **FAIL** | blocks the publish until fixed or waived in writing by the owner |
| **WARN** | does not block; fix it in the app's next change |
| **INFO** | a pattern or a fact to know; no action |

| Short name | Source |
| --- | --- |
| AC | [`docs/APP-CONTRACT.md`](../../APP-CONTRACT.md) (normative app contract), section numbers |
| LV | [`docs/cp0_lvgl.en.md`](../../cp0_lvgl.en.md), section names |
| DEC | [`docs/dev/decisions.md`](../decisions.md), entry date |
| PK | [`checklists/app-packaging.md`](app-packaging.md), item numbers |
| AG | apps repo [`docs/APP-GUIDELINES.md`](https://github.com/OSRdesign/cyberdeck-zero-apps/blob/main/docs/APP-GUIDELINES.md), section numbers (§13 = its final checklist) |
| IP | apps repo [`docs/ICON-PALETTE.md`](https://github.com/OSRdesign/cyberdeck-zero-apps/blob/main/docs/ICON-PALETTE.md) |
| AJ | apps repo [`docs/APP-JSON.md`](https://github.com/OSRdesign/cyberdeck-zero-apps/blob/main/docs/APP-JSON.md) |
| HL | apps repo [`apps/_common/headless/README.md`](https://github.com/OSRdesign/cyberdeck-zero-apps/blob/main/apps/_common/headless/README.md) (scene commands, layout checks) |
| Owner (notes) | an owner decision recorded in the Controller's project notes, not in a repository file; the date says when |

Paths in "How to check": `<app>` = `apps/<id>` in the apps worktree, `<src>` = `<app>/src`, `<root>` = `<app>/root`,
`<scenes>` = the scene folder handed to the QA. A shot is `<scenes>/<scene>/<WxH>/<shot>.png`, the harness's checks of
that size are in `<scenes>/<scene>/<WxH>/report.txt`. "Non-LVGL" = an app that does not link `cp0_lvgl`: a terminal app
(`Terminal=true`, kind B of AG §1) or a wrapped upstream program (kind C).

## The header: what must be where

The header band is the launcher's: the toolkit draws it in every app exactly as on the launcher's own pages. Its height
is the size's `header` token (56 px at 640x480, 480x640, 480x800 and 800x1280; 40 px at 480x320 and 320x480).

| Canvas | Left | Esc | Right |
| --- | --- | --- | --- |
| Landscape and square: 480x320, 640x480, 800x480, 1024x600, 1280x720, 480x480, 720x720 | the page title in gold `#F0B400`, at the home grid's title place and size: the app's name at its root, a page name or path deeper | the toolkit's Esc, a 9 mm toolbar key, placed `SPACE_M` left of `status_strip_left()` (the strip's SSH slot, always kept free) | the launcher's status strip: clock pill, Wi-Fi bars, Bluetooth glyph, a battery on boards that have one, the SSH slot |
| Portrait 480 px wide: 480x640, 480x800 (narrow header) | the title at 18 px, only the last part of a path | as landscape | the strip at 72 % |
| Narrow portrait: 320x480 | **nothing: no title** | at the left edge (`SPACE_M`), TARGET wide (44x30 px at 6.1 px/mm), its tap area over the whole bar | the strip |
| Portrait tablet: 800x1280 | the whole path, as landscape | as landscape | the strip, as landscape |

Nothing else is ever in the band: no app chip, counter, level, volume, clock, device or Bluetooth icon, mode badge or
second button. State the user must see all the time goes in the body: the status line under the header
(`Page::set_caption`) or a strip of the app's own inside the body (Cassette's info strip).

Sources: `cp0_ui_roller.h` (`cp0_ui_roller_page_t`: `esc_*`, `title_last_only`, `title_hidden`, `esc_hit_pad`),
`cp0_ui_metrics.h` (`cp0_ui_narrow_header()`, `cp0_ui_narrow_portrait()`); LV "Status strip and title"; AG §3 (the
v0.9.4 row: narrow portrait shell); [PDA-PROFILE.md](../../PDA-PROFILE.md) "Large screens: the tablet". Shipped
examples, Cassette 0.3 in the apps repo, `apps/cassette/docs/img/`: `recorder-landscape.png` (480x320),
`size-800x480.png`, `size-720x720.png`, `real-pi3a-portrait.png` (480x800, 18 px title), `size-320x480.png` (no title,
Esc at the left). The Esc is not centred by rule: it sits left of the strip's reserved slot, which puts it near the
middle at 480 px wide and right of the middle at 800x480.

## 0. Evidence

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-01 | FAIL | The QA scene set is complete for the app: every page, every size the app supports, the special scenes its features need, each `report.txt` present. An item whose evidence is missing is not a PASS. | SC; AG §7 layer 2 | Compare `<scenes>` with SC "What to render" and the app's `.desktop` (`X-MinSize`, `X-Portrait`, `X-Background`) and pages | never |

## 1. Header and status strip

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-02 | FAIL | Every screen's header is a `cp0::ui::Page` laid out by `cp0_ui_roller_page(m, cp0::ui::status_strip_left(), ...)` with the title factory `cp0::ui::add_title`, and has `cp0::ui::StatusStrip::add(page->header())`. | LV "Status strip and title"; AC §9 "Not copy-in"; AG §2 "Header, title, status strip", §13 #4; PK 3 | `grep -rn "StatusStrip::add(\|add_title\|status_strip_left()" <src>`; every screen root goes through that page (or the app's shell) | non-LVGL (D-06) |
| D-03 | FAIL | The header band holds only the title, the Esc and the status strip: the app draws nothing else there (no clock, Wi-Fi, Bluetooth, battery or SSH glyph, no counter, level, volume, device icon, mode chip, badge or extra button). | LV "never draw your own clock or icons"; AC §9; AG §3 "must not draw its own clock or icons"; DEC 2026-11-12 (the Vinyl drafts) | Code: no object created in or moved to `header()` other than the strip (`grep -rn "header()" <src>`); every hit of `LV_SYMBOL_WIFI`, `LV_SYMBOL_BLUETOOTH`, `LV_SYMBOL_BATTERY`, `LV_SYMBOL_VOLUME`, `strftime`, `"%H:%M"` in UI code is read: none may draw in the band (the body may use them). Shots: in the top `header` rows of every shot at every size only the elements of the header table above | non-LVGL (D-06) |
| D-04 | INFO | State the user must see all the time (counter, level, output device, mode) sits in the body: the status line under the header (`Page::set_caption`) or an own strip inside the body. | `cp0_ui_roller.h` (the status line "laid over the top left of the body"); Cassette's info strip | Shots | - |
| D-05 | FAIL | An LVGL app keeps no copy of the strip, the `cp0::ui` widgets or `cp0_statusbar.[ch]`. | AC §9; LV; AG §3; [verification.md](verification.md) D; [release.md](release.md) step 2 | `git ls-files <app> \| grep -E "cp0_statusbar\|cp0_ui_status_strip\|cp0_ui_widgets"` is empty; `python docs/dev/deck/check_statusbar_drift.py` exits 0 | non-LVGL |
| D-06 | FAIL | A non-LVGL app that draws its own bar uses an unchanged copy of `cp0_statusbar.[ch]`, fed from `status.state` (with `ssh`, `ssh_active`, `ssh_bright`). | AC §5b, §9; [HOSTING-APPS.md](../../HOSTING-APPS.md) "Full-screen apps" | `check_statusbar_drift.py` exits 0 and lists the copy | LVGL apps; terminal apps |
| D-07 | FAIL | Header controls stay left of `status_strip_left()`; nothing is placed from a fixed strip width (a battery widens the strip; the SSH slot is always reserved). | LV "Status strip and title"; DEC 2026-10-30; AG §2, §13 #29 | No numeric strip width or right-anchored header object in `<src>`; 640x480 shot: the Esc ends `SPACE_M` left of the empty SSH slot | non-LVGL |
| D-08 | FAIL | The toolkit's header Esc is kept unchanged: a 9 mm toolbar key (`cp0_ui_toolbar_key_px`), the same in the launcher and every app; at 320x480 TARGET wide at the left edge, the whole bar its tap area. | DEC 2026-10-18 ("the header Esc stays 9 mm"); `cp0_ui_metrics.h` `CP0_UI_TOOLBAR_KEY_MM_X10`; [ROADMAP.md](../ROADMAP.md) v0.9.4; AG §2 ("keep it") | No code hides, moves, restyles or resizes the Esc; `expect esc` after the first shot of `qa` and `qa-small` passes at every size (`report.txt` gives its size) | non-LVGL (D-09) |
| D-09 | FAIL | A wrapped full-screen app has a close control that works by touch, and holding Esc 3 s still ends it. | AG §10 C; HOSTING-APPS "Full-screen apps" (viz1090's close button) | README "Keys and touch"; a shot with the control | LVGL apps; terminal apps |
| D-10 | FAIL | The title is the toolkit's (`add_title` through `Page`, text by `Page::set_title`): never an own label in the band, never re-coloured or resized. Landscape and square: gold, left; 480 px wide portrait: 18 px, last part of a path; 320x480: no title at all. | LV "Status strip and title" (`title_px`, gold `#F0B400`, narrow header); `cp0_ui_roller.h` `title_last_only`, `title_hidden`; the header table | No style call on the title, no own title label; shots: 320x480 shows no text in the band but the Esc's and the strip's, 480x800 a small title | non-LVGL |
| D-11 | WARN | Titles fit before the Esc: the app's name at the root, a short page name deeper (the toolkit cuts a long one with dots; only user data such as a file name may end in dots). | LV ("a long title is cut with dots sooner"); [native-settings-page-howto.md](../native-settings-page-howto.md) §1 (`page_title`) | Shots at 480x320 and 640x480 | non-LVGL |
| D-12 | WARN | The strip is added without a source of its own (it reads the launcher's feed, then sysfs) unless the README says why. | AG §3 ("must not ... pass its own source without a reason") | `StatusStrip::add(` has one argument | non-LVGL |
| D-13 | WARN | The app gives the status strip no action of its own: a long press on it is the launcher's switcher gesture (G2), which the toolkit's helper sends. | AC §13 "Gestures" | No event callback on the strip's `canvas()` or on the right part of the header | non-LVGL |
| D-14 | WARN | The app is built at the current `apps/_common/LAUNCHER_REF`, so its header matches the launcher's (a change to the strip's drawing needs a re-pin and a rebuild of every app). | AG §3 "What reaches a published app without a rebuild", §13 #31 | `<root>/usr/share/doc/<id>/build-info` (or `--version`) against `LAUNCHER_REF` | non-LVGL |

## 2. Sizes, rotation and the contract

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-15 | FAIL | Nothing assumes one screen: no 640x480, 320x170, `/dev/fb0`, `/dev/input/eventN` or 11.3 px/mm in layout or device code; canvas from `APPLAUNCH_CANVAS`, `screen.state` or the framebuffer, density from `ppmm`. | AC §1, §3; PK 3; AG §2 "Sizes", §13 #3 | `grep -rnE "\b(640\|480\|320\|170)\b\|/dev/fb[0-9]\|/dev/input/event[0-9]\|1130\|11\.3" <src>`; read each hit (a test fixture or a notice text is fine) | - |
| D-16 | FAIL | Control sizes come from the tokens and widgets (`cp0_ui_metrics_get()`, `cp0_ui_roller_page()`, `cp0::ui`); an own drawing is sized in mm (`cp0_ui_mm_px`) or scaled from the tokens. | AG §2 "Sizes: tokens, never pixels"; AC §4 | `grep -rnE "lv_obj_set_(size\|width\|height\|pos)\([^)]*[0-9]{2,}" <src>`: a constant must be scaled (a token, a fit factor) | - |
| D-17 | WARN | Spacing and radii come from the tokens (`CP0_TOK_SPACE_S/M/L`, `CP0_TOK_RADIUS_S/M/L`). | AC §4; AG §2 | `grep -rnE "set_style_(pad\|radius\|margin)[a-z_]*\([^)]*[0-9]{2,}" <src>` | - |
| D-18 | FAIL | Every supported size renders without a layout failure: nothing cut by the edge, nothing interactive under the header, every row and target at its minimum. | AG §7 layer 2; HL "Layout checks" | Every `report.txt` of the QA set is all PASS; the contact sheets (`<scenes>/sheets/`) looked at | - |
| D-19 | WARN | The layout is chosen by the canvas's shape and room (`h > w`, the tokens), never by a named size. | AG §2 "Portrait layout" | `grep -rnE "(w\|width\|h\|height) *== *[0-9]{3}" <src>` | - |
| D-20 | FAIL | `X-MinSize` (if any) equals `AppSpec`'s minimum and the registry's `min_size`; the README's Requirements names it. | AC §6; AG §2, §5, §13 #2, #23; PK 3 | `.desktop` against `AppSpec` (`tests/test_core.cpp`); `python3 tools/check_registry.py` | no `X-MinSize` |
| D-21 | WARN | `X-MinSize` is declared only when the UI cannot fit a smaller screen, with the smallest size its layouts were tested at; there is no global minimum (a new app declares none by default). | DEC 2026-11-12; HOSTING-APPS "Full-screen apps"; AG §0, §2 | The README or the task says why it cannot fit smaller; the QA set renders at that size | no `X-MinSize` |
| D-22 | FAIL | With `X-MinSize`, a smaller screen shows the "needs WxH" notice and a way out (an older launcher ignores the key). | AC §6; AG §2, §13 #9; HL "notice" check | `notice` scene under the minimum: PASS, the notice the only thing to press | no `X-MinSize` |
| D-23 | FAIL | `X-Portrait=WxH` only when the layout passes at its portrait canvases (480x640 and 480x800; also 320x480 when the value is 320x480); equal to `AppSpec`'s portrait size; the landscape layouts unchanged by it. | AC §6; AG §2 "Portrait layout"; PDA-PROFILE | The portrait sizes in the QA set all PASS; `tests/test_core.cpp` equality; landscape shots before and after the change are the same | no `X-Portrait` |
| D-24 | FAIL | A native app is `X-Fullscreen=true`; no new interactive app as a 320x170 compat app. | AC §6, §11; AG §1 C, §10, §13 #2 | The `.desktop` | terminal apps |
| D-25 | FAIL | Started without the contract (ssh, an older launcher) the app logs one line, uses its own probe and does not crash. | AC §7; AG §3 "Started without the launcher", §13 #8 | `nocontract` scene PASS; the line in its `<WxH>.log` | - |
| D-26 | FAIL | The app never writes `screen.state`, `input.state`, `status.state` or `board.state` and never creates `launcher.sock`. | AC §2; AG §3 | `grep -rn "screen.state\|input.state\|status.state\|board.state\|launcher.sock" <src>`: reads only | - |
| D-27 | FAIL | A wrapped full-screen app takes the framebuffer, rotation, depth and touch device from the contract (`APPLAUNCH_SCREEN_FB`, `_ROTATE`, `_TOUCH`, `APPLAUNCH_TOUCH_*`) and finds a touch screen by its capabilities, retrying every second. | AC §3 "Panel"; AG §1 C; DEC 2026-10-04 | `grep` the shim for `/dev/fb0`, `event[0-9]`, the variables | LVGL apps (the toolkit does it) |

## 3. Input

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-28 | FAIL | Every function is reachable by keys and by touch: a touch-only board and a keyboard-only user each reach all of them. | AG §2 "Touch and keyboard: both complete", §12, §13 #5 | README "Keys and touch": both columns filled for every function; the scenes drive the pages by `key` and by `tap` | - |
| D-29 | FAIL | The keys follow the launcher: Up / Down move, Enter or Right open, Esc or Left go back, Tab between fields. | AG §2; LV "Lists"; HOSTING-APPS "Keys" | Key handlers in `<src>`; `key` lines of the scenes | terminal apps |
| D-30 | FAIL | Esc (the key or the header's button) goes back one level; at the root a short Esc quits the app: no minimise, no trap; a quit question only where the owner accepted it (Mesh Hop). | AG §2 (owner 2026-10-13), §11; DEC 2026-10-14 D5; AC §13 "Messages" | A scene: Esc on a sub-page `expect noquit`, at the root `expect quit` | - |
| D-31 | FAIL | SIGTERM, SIGINT and SIGHUP end the app cleanly, holding Esc 3 s works, no child process is left. | AG §2, §6 "Clean exit", §13 #7 | `cdk::run()` or own handlers; jobs in their own process group (`_common/async_run`) | - |
| D-32 | FAIL | Letter shortcuts compare the physical key code (W, E, R, T share evdev codes with the arrows) and are never the only way to a function. | AG §2; DEC 2026-10-05 | Key handler code; README table | no letter shortcuts |
| D-33 | WARN | Touch sizes: 7 mm is the recommended minimum, not a requirement (owner ruling R-4, 2026-11-12): page rows and fields ROW (7 mm), buttons, action bars and dialog buttons TARGET (7 mm); 5 mm `CompactList` rows where a tap only selects. QA reports every control below it with its measured size and why the UI cannot reach it; the owner decides usability, and an accepted case is recorded as a waiver. | DEC 2026-10-18 (5 mm and 7 mm), 2026-10-14; AC §4; HL "size" check | The smallest row and button of every `report.txt` | terminal apps |
| D-34 | FAIL | No drag-to-move: a drag only scrolls or pans; flags, handles and markers are never dragged (their position is set by keys, steppers or a tap at a marker), items are reordered by move keys or buttons; no action is drag-only. | Owner rule from Cassette, 2026-11-08 (Cassette `docs/USER-GUIDE.md`: "Flags and handles are not dragged"; [report 059](../reports/059-music-player-study.md) section 10, question 9: "no dragging on small screens"); AG §2 ("drag = scroll; no gesture-only action") | Handlers that move objects on `LV_EVENT_PRESSING`; `drag` / `dragon` lines of the scenes; README | (see ruling R-1) |
| D-35 | FAIL | No gesture-only, hover-only or long-press-only action. A 3 s hold may open a box that lists every action (each with its key shown in grey), announced by a line ("Hold 3 s: options" without a keyboard, "Ctrl+O: options" with one); a hold never changes what a tap or a drag does; Esc closes the box. | AG §2, §14 "Options by a 3 s hold" (owner 2026-10-24) | Hold handlers; a shot of the box lists all actions; the hint line in a shot | no hold action |
| D-36 | FAIL | Three fingers held still, Super (tap, double tap, hold), Alt+Tab and `KEY_F14` are the launcher's: the app gives them no meaning (one- and two-finger gestures stay the app's). | AC §13 "Obligations", "Gestures"; AC §14 ("Reserved: KEY_F14"); AG §3 | `grep -rn "KEY_LEFTMETA\|KEY_RIGHTMETA\|KEY_F14\|KEY_TAB" <src>`; touch code counting contacts | - |
| D-37 | WARN | The key map is shown: the README's "Keys and touch" table lists every function with its key and its touch action; a hold box shows each action's key. | AG §8, §14 | README table against the key handlers | - |
| D-38 | FAIL | The on-screen keyboard shows only by the shared rule (`cp0_app_osk_allowed()` on `input.state`, through `_common/osk_policy`): never summoned by a button, key or gesture; it hides within about 1 s when a keyboard connects. | AC §10; owner policy of 2026-10-10 (physical keyboard first, the fallback keyboard only when none is connected, never by hand); native-settings-page-howto §3; AG §2 "On-screen keyboard", §13 #6 | No `osk::request` or keyboard button in `<src>` outside `_common`; `keyboard` scene: `keyboards 0` then `expect osk on`, `keyboards 1` then `expect osk off` | no text field |
| D-39 | FAIL | Text input uses `cp0::ui::TextField` and `Dialog`; the edited field stays visible above the keyboard and nothing of the page lies under it; a pick list or a preset is offered where it can replace typing. | AG §2 "On-screen keyboard"; HL "keyboard" check | `keyboard` scene: the check PASS | no text field |
| D-40 | WARN | An app with text fields shows the keyboard rule's state in its Settings ("allowed: no", "a keyboard is connected": the Markdown Notes pattern). | AG §14 "The on-screen keyboard" | A Settings shot | no text field |
| D-41 | FAIL | An upright portrait app with text fields lets the writing band show where its keyboard would (pin v0.8.11 or later, `_common/osk_policy`), follows the keyboard's inset, keeps the field above the band and nothing under it; no opt-in key, no band of its own. | AC §3 "The band in an app"; LV "The PDA writing zone"; PDA-PROFILE "Apps with the band"; AG §2 "Portrait layout" | `portrait-band` scene at 480x800 and 480x640: `expect band shown`, the band check PASS | no `X-Portrait`; no text field; `X-Portrait` under 480 px wide (no band at 320x480) |
| D-42 | WARN | Pointer navigation reaches the app: it reads every keyboard (`cp0_keyboard_set_read_all_keyboards(1)`, as `_common/platform.cpp`), and an app with an options menu opens it on Ctrl+O. | AC §10b | `grep -rn "read_all_keyboards" apps/_common <src>`; the options menu's key | non-LVGL |

## 4. Lists

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-43 | WARN | Data and app lists (about 8 rows or more) use `cp0::ui::CompactList` or a list of the app's own drawn with the `cp0_ui_list.h` helpers; `cp0::ui::Roller` for few big items and choice lists. | DEC 2026-10-14; LV "Lists: Roller or CompactList"; AG §2 "Lists", §13 #26 | `grep -rn "CompactList\|Roller" <src>`; an exception the owner accepted (AG §11, Mesh Hop) counts | no list |
| D-44 | WARN | The compact density (5 mm) only for lists where a tap only moves the highlight; a list whose rows act uses Comfortable (7 mm). | DEC 2026-10-14, 2026-10-18; LV; AG §2 | `ListDensity::Compact` / `CP0_UI_LIST_COMPACT` against what a tap on a row does; `report.txt` row sizes | no list |
| D-45 | FAIL | Every list row carries its affordance: Open (chevron), Edit (value, then chevron), Action (white dot), ActionRunning (blue dot `#5AAAFF`), Info (grey value, no marker); no row left `Unspecified`. | DEC 2026-10-16; owner feedback of 2026-10-16 (touch-only boards have no focus highlight); LV "Row affordances"; AG §2 "Row kinds", §13 #27 | Every `ListRow::item(` has its fifth argument (or sets `affordance`); shots: a marker on every acting row | no `CompactList` or own list |
| D-46 | WARN | A running action is the same row switched Action to ActionRunning through the live refresh (`set_rows()`, `update_row()`, `set_value()`). | LV "Row affordances"; AG §2 | Code | no action row |
| D-47 | FAIL | Only the marker says "tappable": no blue, underlined or accent-coloured label. | DEC 2026-10-16 ("the Action label is not coloured"); AG §2 "Row kinds" | Label colour calls on list rows; shots | no list |
| D-48 | WARN | A list of the app's own uses the `cp0_ui_list.h` helpers (`cp0_ui_list_has_chevron()`, `_has_dot()`, `_label_rgb()`, `_value_rgb()`, `_dot_rgb()`, `_trail_w()`, `_chevron_y()`), keeps the trailing width only on marker rows and never cuts a value under the marker. | LV "Row affordances"; AG §2 | Code; shots | no own list |
| D-49 | INFO | A bar of buttons and a tile grid carry no row marker. The tile grid to copy is Home Assistant's: a tile one `row` high, its icon the action, its name the details page, a corner chevron only when that page holds more. | AG §2 "Not lists" | - | - |
| D-50 | FAIL | Status is never shown by colour alone: an Ok, Warn or Bad tone comes with a word or a shape. | AG §2 "Lists", §13 #26; LV ("Muted ... never the only signal of status") | Shots of every state; `ValueTone` uses | - |
| D-51 | WARN | Live data refreshes with `set_rows()` (no flicker, scroll and selection kept), `reset_rows()` only for new content; a detail view follows the item's identity, not its position in the list. | LV "Lists"; AG §2; DEC 2026-10-04 | Code | no live list |

## 5. Fonts and CJK

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-52 | FAIL | Text uses the launcher's fonts (Montserrat and Font Awesome from `/usr/share/APPLaunch/share/font`, through `cp0_file_path_c`); the package bundles no copy of a font the launcher ships. | LV "Status strip and title" ("Apps do not bundle them"); AG §2 "Header, title, status strip" | `find <root> -iname "*.ttf" -o -iname "*.otf" -o -iname "*.ttc" -o -iname "*.woff*"` against `projects/APPLaunch/APPLaunch/share/font/` | non-LVGL |
| D-53 | WARN | Text sizes come from the text tokens (`CP0_TEXT_TITLE`, `_HEADING`, `_BODY`, `_CAPTION`) snapped to the built-in sizes (even, 8 to 48 px); text sized in mm is never smaller than the deck's CAPTION (1.6 mm). | AC §4 (`text=`); `cp0_ui_metrics.h` (`CP0_UI_FONT_MIN_PX`, `_MAX_PX`); AG §2 "Portrait layout" (text sizes) | `grep -rnE "font_px\|montserrat_[0-9]+\|[^a-z]px *= *[0-9]{2}" <src>` | non-LVGL |
| D-54 | FAIL | A font the app bundles itself (a display or handwriting face) has its licence text under `/usr/share/doc/<id>/` and a credit in the README. | AG §4 ("Third-party licences") | `<root>/usr/share/doc/<id>/`; README credits | no bundled font |
| D-55 | INFO | No rule yet for CJK text (ruling R-3). Facts: the launcher ships Noto Sans CJK and Alibaba PuHuiTi in `share/font` ([OPEN_SOURCE_COMPONENTS.md](../../OPEN_SOURCE_COMPONENTS.md)); RSS Reader uses them as a fallback chain ([report 037](../reports/037-notes-app.md)). | - | - | - |

## 6. Colours, tokens and the icon

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-56 | FAIL | The header and the toolkit's widgets keep the toolkit's colours (black ground, white text, gold title `#F0B400`, selection bar `#2B2B2B`, accent `#5AAAFF`, muted `#9A9A9A`, roller chevrons `#FF6A3D`): the app restyles none of them. | AG §2 "Header, title, status strip" ("Colours and fonts come from the widgets"); LV; `cp0_ui_roller.h`; `cp0_ui_list.h` | No `lv_obj_set_style_*` on the header, title, Esc, list, roller or dialog objects; header band of the shots against the launcher's | non-LVGL |
| D-57 | WARN | Status colours keep their meaning everywhere in the app: green OK, amber warning, red bad or error, accent blue running or focus. | LV (`ValueTone`, the Action dot); IP rule 1 ("green means OK") | Shots | - |
| D-58 | INFO | The body's own look (panels, artwork, an accent of its own) is the app's design, chosen with the owner (Cassette's cream deck, owner choice of 2026-11-08); no item covers it beyond D-50 and D-57. | Owner (notes) 2026-11-02: layout and design are decided case by case | - | - |
| D-59 | FAIL | The icon is a 100x100 square RGBA PNG with transparent corners; `<app>/icon.png` and `<root>/usr/share/APPLaunch/share/images/<id>.png` are the same file; `Icon=share/images/<id>.png`. | IP "Canonical size"; PK 3; AJ "Files next to app.json" | Pillow: size, mode, corner alpha 0; `cmp` of the two files; the `.desktop` | - |
| D-60 | WARN | The icon follows the palette rules: one hue, not green and not within about 30 degrees of another app's or a launcher tile's (or told apart by silhouette and lightness), its own silhouette, at most two flat colours (ink `#14181C` or white), strokes of 9 px or more, no text, inside the 5 px margin, ink coverage 45-65 %, distinct at 32 px and in greyscale; its row in the palette table. | IP "Rules", "Choosing a new hue", "How to make a new icon"; AG §13 #24, #25 | The palette table row; the review sheet (`tools/icons/out/sheet.png`) or a 32 px and greyscale render; coverage from the alpha channel | - |

## 7. Notices and dialogs

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-61 | FAIL | Dialogs: Cancel is Esc, the primary button is Enter, both are also buttons (`cp0::ui::Dialog`, one to three buttons). | AG §2 "Touch and keyboard"; native-settings-page-howto §3 (Dialog) | Dialog code; a dialog shot | no dialog |
| D-62 | FAIL | A failure is a message with the fix: missing hardware says what is missing and how to plug it, a permission failure names the fix (the group, the command), offline shows a short reason and the cached data; never a bare error code, never a crash. | AG §6 "Behaviour", §13 #12 | Scenes of the error states (no device, no permission, offline) show the texts | - |
| D-63 | FAIL | Long work shows a busy state with a cancel (Esc) and a timeout; nothing slow runs on the LVGL thread. | AG §6, §13 #10 | A busy shot; `grep -rnE "\bsystem\(\|popen\(\|sleep\(\|usleep\(" <src>` on UI paths | no long work |
| D-64 | WARN | The launcher's toast (`cp0_app_toast`) is for a notice meant for later (it shows only when the launcher is in front); feedback inside the app is drawn by the app (the status line, a notice line). | AC §14 (`toast`); AG §3 | `grep -rn "cp0_app_toast" <src>` and what each call says | no toast |
| D-65 | FAIL | Requests to the launcher (`beep`, `toast`, `volume`, `brightness`, `dim`) run off the LVGL thread, wait about 200 ms at most, fail silently and remember a failure; sound options only when `board.state` has `sound=1` and `requests` lists `beep`; a missing state key is its default (absent = unknown, never 0). | AC §5c, §14 "Degrade rules"; AG §3, §13 #30 | `grep -rn "cp0_app_client_call\|cp0_app_beep\|cp0_app_toast\|cp0_app_board_read" <src>` and the thread each runs on | no request |

## 8. Background, multitasking, dimming, volume keys

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-66 | FAIL | The multitasking choice is made and written in the README: `X-Background=freeze` (the default), `run` (work must go on; why) or none (why); declared only with `X-Fullscreen=true` and in a build that answers the protocol; the `lifecycle` scene passes (hide and show answered within 300 ms, `expect nowrite`, `expect repaint`, close quits). | AC §6, §13; DEC 2026-10-14 D1; AG §3 "Multitasking", §13 #28 | `.desktop`; README; `lifecycle` `report.txt` | terminal apps |
| D-67 | FAIL | A pause must not lie: an app whose measurement a freeze would falsify stays without `X-Background` until it can cancel or mark that measurement. | AG §3 "Multitasking" | What the app measures against its `X-Background` | no timed measurement |
| D-68 | WARN | A `run` app does only its own work while hidden (`cdk::hidden()`): no rendering, no new polling, timers at their usual rate or lower; exclusive hardware is kept with `run` or released on hide. | AC §13 "Obligations"; AG §3 | Code | not `run` |
| D-69 | WARN | State is saved on `close` and as the user goes (the launcher may close a hidden app at any time). | AC §13; AG §3 | Code | no state |
| D-70 | FAIL | The volume, mute and brightness keys are the launcher's: the app does not act on them (the step would double). | AC §14 "The volume, brightness and mute keys inside an app"; AG §3 "Sound, volume and brightness", §13 #29 | `grep -rn "KEY_VOLUME\|KEY_MUTE\|KEY_BRIGHTNESS" <src>` | - |
| D-71 | FAIL | A volume or brightness level the app shows comes from `status.state` (`volume`, `mute`, `brightness_pct`, polled with the status) and is drawn in the body, never in the header band; the app never sets the system volume or the backlight itself (`pactl set-sink-volume`, `amixer`, sysfs); a media app changes the volume through `launcher.sock` `volume`. | AC §5b, §14; AG §3 "Showing the level", §9 | `grep -rn "pactl\|amixer\|/sys/class/backlight" <src>`; shots | no level, no volume control |
| D-72 | WARN | `dim allow` only where `board.state` `requests` lists `dim`, sent again after every show; not from an app used with a pointer only; `deny` while the light must stay. | AC §14 (`dim`) | `grep -rn "dim allow\|dim deny" <src>` | no dim request |
| D-73 | INFO | Only the five keys of D-70 are the launcher's; media keys such as play / pause and next reach the app like any key. | AC §14 | - | - |

## 9. Descriptor and packaging

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-74 | FAIL | [`app-packaging.md`](app-packaging.md) items 1-5 pass (layout, `app.json`, descriptor, `.desktop`, icon path, licence notice, registry); they are not repeated here. | PK; AJ | `python3 tools/check_descriptors.py`, `python3 tools/check_registry.py`, `python3 tools/test_registry_tools.py`; the app's `test_core` manifest checks; `dpkg-deb -I` / `-c` in the hand-off | - |
| D-75 | FAIL | The `.desktop` file keeps its name across versions (the tile keeps its place on the home grid). | AG §5 ("never rename it"); HOSTING-APPS "Tile position" | `git log --follow --name-status <root>/usr/share/APPLaunch/applications/` | first version |

## 10. Licence and credits

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-76 | FAIL | Everything third-party the package ships (code, fonts, data, icons) has its licence text under `/usr/share/doc/<id>/` (`copyright` in Debian format, a `LICENSE.<component>` per bundled component) and a README credit with author, licence and source URL; GPL parts name the source URL; libraries opened at run time are named too; `app.json` `license` is the SPDX expression of what the package ships. | AG §4; PK 4; [verification.md](verification.md) E; AJ (`license`, `depends`) | `<root>/usr/share/doc/<id>/` against the bundled files; README "Credits and licences" | nothing third-party |
| D-77 | FAIL | "CyberdeckZero Launcher" is written as one word; no stock-device name, logo or binary prefix in the tile name, icon, README or package; the branding check is clean without new allow-list entries. | AG §4, §13 #15, #22 | `sh tools/branding/check-branding.sh` in the apps worktree | - |

## 11. Screenshots for the docs

| ID | Sev | Rule | Source | How to check | N/A when |
| --- | --- | --- | --- | --- | --- |
| D-78 | WARN | The README shows the real UI (harness renders or board captures, no mock-ups) at the sizes the app is tested at: 640x480, 480x320 and 320x480 when it runs there, 720x720 or 800x480 when the layout differs; at least the main page and one form. | AG §8 "Screenshots" | README images against the QA sizes | - |
| D-79 | WARN | Store screenshots are `<app>/screenshots/NN-name.png` (numbered: the sorted order is the Store's), each shown or linked in the README. | AG §8; AJ "Files next to app.json" | `ls <app>/screenshots`; README links | no Store screenshots |
| D-80 | WARN | Screenshots are taken again when a layout changes, the header included (a re-pin moves the Esc and the title); 320x170 pictures are stale. | AG §8, §3 (the SSH indicator row) | Image dates and pins against the last layout change | - |

## Needs an owner ruling

Requirements asked for in the brief of this checklist (2026-11-12) for which no primary source was found, or whose
source is narrower than the brief. Until the owner rules, the item stays as written above.

| # | Question | What exists |
| --- | --- | --- |
| R-1 | Does "no dragging" bind every app at every size? | The recorded words are Cassette's (trim flags and handles, "too small screens", 2026-11-08) and report 059 question 9 ("no dragging on small screens"). Klondike Solitaire 0.1.0 drags cards on 640x480 and larger, with a tap-to-move path as well. D-34 fails a drag-only action and dragged handles, flags or markers; a drag that only adds to a tap path is not failed. |
| R-2 | Must an app show its key map on screen (a legend, a help page), or is the README table enough? | Only the README's "Keys and touch" table (AG §8) and the keys in a hold box (AG §14) are required (D-37). |
| R-3 | How should apps show CJK text (which font, a fallback chain, or not at all)? | No rule (D-55). The launcher ships the fonts; RSS Reader has a fallback chain; report 059 asks the question for Vinyl. |
| R-4 | **Ruled 2026-11-12 (owner):** 7 mm is a minimum recommendation, not a requirement. If the UI cannot allow it, the owner decides usability. D-33 and D-44 are WARN, and QA lists each smaller control with its measured size and the reason (games such as Mine Field and Calculator keys are typical cases). | resolved |
| R-5 | Does "black ground" (AG §2: "Colours and fonts come from the widgets (black ground ...)") apply to the body, or only to the header and the widgets? | Cassette's cream body was the owner's choice (2026-11-08). D-56 applies the colours to the header and the toolkit's widgets only. |
| R-6 | Should `app-design-qa` also review design drafts (mock-ups) before an app is built? | The Vinyl header mistakes were in drafts; the owner's decision of 2026-11-12 limits the QA to code and headless screenshots. |
