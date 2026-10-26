# 042 - PDA profile: Strokes training (S6, brought forward) and the writing zone on demand

Status: built 2026-10-25 on branch `pda-s3-zone` (worktree `launcher-s3`, after report 041 / `0.8.8-pda.6`), from the
owner's brief after the board test of `0.8.8-pda.5` on the Pi 3A+ (11 of 26 letters refused: b, p, s, e, m, o, q, r,
y, z) and three owner decisions that came in while it was built (sections 2 and 7). Committed on the branch, not
pushed; no board touched. Bundle `0.8.8-pda.7` (section 10). Then, 2026-10-26: the glyph picker (section 12) and the
On-screen keyboard values Auto / On / Handwriting + keyboard with the strip's Keyb and the keyboard's cross (section
13); bundle `0.8.8-pda.8` (section 14).

## 1. Summary

- **Strokes training** (Settings > System > Strokes training, PDA profile only): a native page that keeps the writing
  zone shown and takes its strokes (nothing is typed), shows the glyph to write with its chart stroke as a guide (light
  ink, a green dot where it starts), judges four attempts per glyph (recognised / refused / read as another glyph:
  thumbnails framed in green / red / amber, a result line with a short flash), turns them into the owner's own
  templates in the user overlay, reloads the recogniser, and walks the glyphs: the ten refused letters first, the other
  letters, the digits, then gestures and punctuation (optional). Back, Skip, Redo, Reset all (confirmed), Finish (a
  re-test of each trained glyph once, then a summary: recognised before and after). It resumes where it stopped (the
  overlay's content is the progress).
- **Capture log**: every attempt goes to `~/.config/applaunch/strokes-training.txt` (the S1 corpus format, labelled
  with the glyph asked for, a comment line with what the board made of it, its size in mm and the time); the new PC
  tool `strokes-log` reports per glyph what the board recorded (confusions, sizes, before / after) and replays it.
- **The writing zone on demand, no soft buttons** (owner, after pda.6): the band is the key strip on top of the writing
  boxes and behaves like the fallback on-screen keyboard: shown where a text field (the terminal pages included) asks
  for input and the keyboard's policy allows it, hidden otherwise; the pages get the whole panel and leave the band's
  rows free through the keyboard's inset; `vkb=zone` only while it shows. Home / Menu / Apps / Keys are gone (the
  multitasking gestures do Home and the switcher).
- **Home grid**: with the whole panel back, both PDA portrait panels get three columns (section 7).
- **pda.8** (2026-10-26): the **glyph picker** on the training page (section 12); Settings > System > On-screen
  keyboard is **Auto / On / Handwriting + keyboard** (the last on the PDA profile only, its default; Off is gone, a
  stored Off reads as Auto); the strip loses Shift and gets **Keyb**, which puts the standard keyboard in the band's
  place until that keyboard's close **cross** (section 13).
- **Simulation** (the synthetic generator, `tests/test_train.c`): a writer whose ten letters are slanted and narrow is
  recognised 25.7 % by the shipped tables, 93.2 % after one training session of four strokes per letter; the chart's
  letters stay at 98.7 %.
- **Checks**: `check-all.sh` all PASS (158 goldens identical, every pre-existing golden other than the zone ones
  byte-identical), install tests 375 passed.

## 2. The writing zone on demand (owner decisions after pda.6)

Two messages: first "the zone only when needed, the soft-button row always visible", then the revision that replaced it:
"remove the soft-button row; the band is the key strip (always there while the band shows, no Keys toggle) on top of
the writing zone, shown exactly when the fallback keyboard would be, always on the training page". The first was
built half way and replaced; only the revision is in the branch.

```
480x800 (8.54 px/mm)                     shown                         hidden
+------------------------------+ 0       pages: the whole panel,       pages: the whole panel
| app area: the native display |         the band's rows left free     (480x800, as S2's portrait
| (the whole panel)            |         (the keyboard's inset, 318)   launcher)
+------------------------------+ 482
| Tab Ctrl Shift Alt < ^ v > Ent|  strip row 51 (8 + a 43 px key)
+------------------------------+ 533
| abc (310 px)    | 123 (146)  |  writing zone 267 (boxes 251 high)
+------------------------------+ 800
```

| | 480x800 (4.3" DSI, 8.54 px/mm) | 480x640 (3.5", 9.04 px/mm) |
|---|---|---|
| band shown (strip + writing zone) | 318 px at y 482 (51 + 267) | 240 px at y 400 (53 + 187) |
| writing boxes | 310 x 251 + 146 x 251 (unchanged from pda.6) | 310 x 171 + 146 x 171 (unchanged) |
| above the band while it shows / hidden | 482 / 800 | 400 / 640 |
| memory (RGB565) | native 800 lines (768 KB) + band 318 lines (305 KB) | |

- **Height rule**: `write_h = max(6 pad, min(round(h / 3), h - row_h - 400))` (unchanged formula; row_h is now the strip
  row above the writing zone, so 400 px of the panel stay above the band when it shows: 482 at 480x800, exactly 400 at
  480x640, the deck density's 480x640 too).
- **When it shows** (`cp0::ui::osk::set_zone`, the keyboard service's zone hook): where the fallback keyboard would show
  (a field requested it, `launcher_osk`'s policy allows it: no physical keyboard, Settings > On-screen keyboard Auto,
  no lock screen; `APPLAUNCH_OSK=force` forces it) and the field's screen is the one shown (the multitasking switcher
  hides it), or while a page pins it (the training page). A physical keyboard therefore hides it, as it hides the
  keyboard. Fullscreen turned apps keep the standard rule (the band is the launcher's).
- **Hiding** sends the strip's owed releases and turns its modifiers off, drops a stroke in progress, gives the rows back
  to the native display (redrawn), and the router then routes every touch to the app.
- **Removed as dead code**: the soft buttons and their layout, `cp0_zone_action_cb_t`, `cp0_zone_ui_set_strip_open`,
  the Keys toggle, `launcher_zone::bind(home)` / `close_strip`, the multitasking `toggle_switcher` / `switcher_home`
  helpers and their harness stubs, the keyboard policy's `zone` field (the service decides now), 6 goldens.

## 3. How the owner uses Strokes training (Pi 3A+, portrait)

1. Install `0.8.8-pda.7` (`--board pi3a-wsdsi43-portrait`, as pda.6).
2. Settings > System > **Strokes training** (after On-screen keyboard; the entry exists only with the writing zone).
   The band comes up and stays up on this page.
3. The page says **Draw b (1/4)**, shows b's chart stroke in grey with a green dot where the pen starts. Write b in the
   letters box **the way you write it** (digits in the digits box; punctuation without the tap before it). After each
   stroke a thumbnail appears: green = recognised, amber = read as another glyph (its name in the corner), red =
   refused; the line under them says it in words.
4. After the fourth attempt the page moves to the next glyph (p, s, e, m, o, q, r, y, z, then the other letters,
   the digits, gestures and punctuation). **Back** = the previous glyph again, **Skip** = leave this one, **Redo** = this
   glyph from its first attempt (its templates dropped), **Reset all** = every personal template dropped (asks first;
   the log is kept), **Finish** = the re-test.
5. Leave the page any time (Esc): the next visit resumes at the first glyph without templates.
6. **Finish**: each glyph trained in this visit once more ("Check b (1/12)"), then the **Summary**: per glyph
   "before x/4" (the training attempts, judged with the tables before that glyph's templates) and "after" (the
   re-test), the totals, and how many strokes were kept per glyph. **Train again** starts a new visit.
7. Write in the Keyboard test (or anywhere): the templates are in use at once.

Files on the board (the launcher's user; `$XDG_CONFIG_HOME` first):

| File | What |
|---|---|
| `~/.config/applaunch/strokes-user.txt` | the user overlay: one `# strokes-training <id> <set>: ...` block per trained glyph (its personal templates); hand-written lines kept; loaded at start and after each glyph |
| `~/.config/applaunch/strokes-training.txt` | the capture log: every attempt (train and re-test), appended |

For the analysis on a PC:

```sh
scp <user>@<board>:.config/applaunch/strokes-training.txt <user>@<board>:.config/applaunch/strokes-user.txt .
cd tools/strokes && make
build/host/strokes-log ../../strokes-training.txt                       # recorded per glyph, before / after, confusions,
                                                                        # then the replay with the shipped tables
build/host/strokes-log ../../strokes-training.txt --overlay ../../strokes-user.txt   # replay with the owner's templates
build/host/strokes-replay ../../strokes-training.txt --errors -v        # every miss with its candidates and features
```

## 4. The training session (`strokes_train.[ch]`, Strokes 0.3.0)

Plain C99 in the library (tested by `tools/strokes`, built into the launcher by `cp0_lvgl`); no clock, no file I/O.

- **Order**: the named letters first (the page passes `b p s e m o q r y z`), the other letters of the letters set
  (27: a-z but x, which is the extended set's second stroke, plus the optional section's c cedilla and ae), the digits,
  then the gestures (space, enter, shift, backspace, the extended prefix) and the punctuation: 74 glyphs. Taps (the tap
  prefix, the period) and accents have no shape and are not in it.
- **Judging**: `strokes_classify` in the glyph's set as it is written (a gesture in the letters box's set, a
  punctuation glyph in the punctuation set: without its tap), the recogniser's own path.
- **Templates kept** after the fourth attempt (`strokes_template_line`, the overlay format of S1):
  - a miss (refused or another glyph) when the user wrote it **at least twice the same way** (another attempt reads it
    as the glyph): a shape written once is a slip or a scribble (the render scene's scribble and its one d-shaped b are
    not kept);
  - a recognised attempt only when it adds a shape: its score to the shipped variants under 0.92;
  - none within 0.92 of an attempt already taken.
  Typically 0-3 lines per glyph (about 350 bytes each); the simulation's ten letters took 24 lines, 8.4 KB.
- **Added, not replacing**: a block adds variants to the glyph; the glyph's score is its best variant's, so the shipped
  shapes keep working. `replace <id> <set>` (personal only) exists in the session (`strokes_train_set_personal_only`)
  but not on the page, decided with the simulation's data (section 6): it brings the writer nothing (93.3 % against
  93.2 %, wrong answers 8 against 7) and costs the chart's strokes of those letters (98.7 % to 79.6 %).
- **Progress**: a glyph with a block is done; the session resumes at the first glyph without one; Back writes a done
  glyph again (its block replaced when its four attempts are in), Redo drops its block, Reset all drops every block.
- **Numbers**: per glyph, the last complete training (x/4 recognised) and the re-test; totals.
- **Capture log line**: `# training phase=train target=b set=letters attempt=2/4 verdict=wrong got=d score=0.975
  margin=0.199 reject=none size_mm=7.5x11.7 at=2026-10-25T21:04:00`, then the sample `b<TAB>letters<TAB>8.540<TAB>t,x,y
  ... ^t` (the zone's own points: drop-outs and lifts as they happened; `strokes-replay` replays it frame by frame).

The launcher's side: `cp0_zone_ui_set_capture` (finished strokes to the page, modes cleared after each, nothing typed),
`cp0_zone_ui_reload` (the tables again after the overlay was written: a temporary file renamed over it),
`cp0_zone_ui_table` / `config` / `overlay_path`, `strokes_table_template_points` (the guide).

## 5. Decisions taken where the brief was silent (the owner may change any)

| # | Decision | Why |
|---|---|---|
| T1 | The entry is System > Strokes training, after On-screen keyboard | the input entries are there; the rows counted from the end of System (Shutdown, Reboot, Keyboard test) stay where they were (the zone scenes count them) |
| T2 | A miss is kept only when written twice the same way | the brief: "accepted samples"; a scribble or a slip must not become a template (first render run: the scribble and a d-shaped b were kept) |
| T3 | Templates are added; personal only stays an unexposed option | section 6 data |
| T4 | The x of the extended set, the taps and the accents are not trained | x is two strokes (the prefix, then a line); a tap has no shape; accents are optional |
| T5 | The page's look: the zone's box colours for the guide and the thumbnails, the list tones (green / amber / red) for the verdicts, the Dialog's button colours, a 600 ms flash of the verdict colour behind the result line | existing tokens only; the brief asked for "a short coloured flash" |
| T6 | The log next to the overlay (`strokes-training.txt`); no log when there is no overlay file (`APPLAUNCH_STROKES_USER=""`) | the brief: "in the same config dir" |
| T7 | The band's order is the strip on top of the writing boxes, both at the bottom of the panel | the owner's revision ("the key strip ... on top of the writing zone") |
| T8 | The band shows by the keyboard's own policy: a physical keyboard hides it | the owner: "behaves exactly like the fallback on-screen keyboard" |

## 6. Simulation and tests

The simulation (`tools/strokes/tests/test_train.c`, part of `make check`): the writer's ten letters (b p s e m o q r y
z) are the chart's strokes turned 32 degrees clockwise and 0.62 as wide, written through the S1 synthetic generator
(its default noise, slant, drop-outs); a training session feeds four of their strokes per letter through the session
and reloads the tables after each glyph, as the page does; then fresh strokes (other seeds) are judged.

| | shipped tables | after the session (added) | personal only |
|---|---|---|---|
| the writer's ten letters (600 strokes) | 25.7 % (15 wrong) | **93.2 %** (7 wrong) | 93.3 % (8 wrong) |
| the chart's 25 letters (1000 strokes) | 98.7 % | **98.7 %** | 79.6 % |

Tests: `tools/strokes` `make check` (54 tests, 1885 checks, plain and ASan + UBSan; the eval gate unchanged: 99.43 /
0.44 %; e2e; `tests/log.sh` for strokes-log, plain and sanitised; names): the order and its groups, the guide points,
the overlay merge (variants added, `replace` = personal only, hand-written lines kept, resume, reset), the session
(attempts, verdicts, kept templates and near duplicates, the twice rule, back / redo / skip / finish, the re-test, the
numbers, the log line), the simulation. `cp0_lvgl` `test_zone` (the band's geometry shown / hidden at 480x800, 480x640
and the deck's density, the hit test and the router hidden and shown, why it shows), `test_ui_osk` (the policy without
the zone field), `test_ui_metrics` (three columns at 480x800 at 8.57 and 7.4 px/mm). App unit tests unchanged and
passing.

Render scenes (goldens at 480x640 and 480x800, all viewed):

- `zone_home` (the home grid, band hidden, vkb auto), `zone_home_pi4b` (new, 480x800 at 108x65 mm: three columns),
  `zone_text` (Settings with the band hidden; the Keyboard test's field focused shows it; written with strokes; new
  shot `zone_text_done`: Esc ends the editing, the band hides, vkb auto), `zone_strip` (the terminal shows the band,
  Ctrl armed, Ctrl+C, a pound sign), `zone_turned` (from the Keyboard test with the band, new shot `zone_turned_text`:
  a turned app, then the band back), `zone_switcher` (switcher and screensaver);
- `strokes_training` (new, 6 shots per size): the first glyph with its guide; three attempts (green, amber, red) and
  the result line; the Reset all dialog; the glyph done and the next one; the re-test; the summary. The scene also
  checks the overlay block and the capture log's lines (`expect_file`).
- Removed: `zone_home_strip`, `zone_strip_closed`, `zone_turned_home` (the Keys toggle and a home grid with the band).
- Every other golden (the 640x480 / 480x320 ones and the S2 `pda_*` ones) byte-identical; `check-all` compare: 158
  goldens identical.

## 7. The home grid's columns (owner request during the work)

The request: three columns on both the Pi 3A+ portrait (94x56 mm, 8.57 px/mm) and the Pi 4B portrait (108x65 mm,
7.4 px/mm), through a new `APPLAUNCH_GRID_TILE_MM` key set to 18 mm in the portrait profiles. Measured first: the two
columns of pda.4-pda.6 on the Pi 3A+ came from the 480x482 app area above the permanent band (report 041 D2), not from
the panel's density. With the band on demand the home grid has the whole 480x800 panel again, and the 20 mm rule gives
three columns at both densities (tiles 137x128 at 8.57 px/mm, 140x130 at 7.4: `test_ui_metrics`, goldens `zone_home`
and `zone_home_pi4b`). **The key was not added**: on these panels it would change nothing (18 mm also gives three
columns) and it would be a second, untested path through the layout service read before the launcher can set it. If
the owner still wants the knob, it is a small follow-up.

## 8. What the owner checks on the board (Pi 3A+, about 20 min)

1. Home grid: three columns, no band at the bottom.
2. Settings > System > Keyboard test (development channel) or the CLI: the band (strip + boxes) comes up with the text
   field, writing works; Esc / leaving the page hides it. With a Bluetooth keyboard connected it stays hidden.
3. Settings > System > Strokes training: train b p s e m o q r y z (four strokes each, as you write them), Finish,
   look at the summary; then write those letters in the Keyboard test.
4. Send `~/.config/applaunch/strokes-training.txt` and `strokes-user.txt` (scp, section 3): `strokes-log` shows why the
   letters failed and what the templates changed.

## 9. Risks and open points

1. **Real strokes are still unmeasured**: the training's thresholds (0.92 near duplicate, the twice rule) and the
   simulation come from synthetic strokes; the capture log is there to measure them.
2. **Conflicts between glyphs**: a template of b drawn like the chart's d makes the owner's d closer to b; the re-test
   and the summary show it, and `strokes-log --overlay` measures it; Redo / Reset all undo it. Training the conflicting
   glyph too usually settles it (its own templates win by score).
3. **480x640 is tight on the training page** (400 px above the band): the guide box is 72 px there (fine on the
   480x800 reference panel: 140 px).
4. **The band and a physical keyboard**: with a keyboard present the band does not come up for text fields (the
   keyboard's rule); the training page pins it anyway.
5. **The band while an app is in front**: the launcher's display is suspended; the band's state is updated when the
   launcher comes back (the field still editing: shown again, as `zone_turned` checks).
6. **No `APPLAUNCH_GRID_TILE_MM`** (section 7).

## 10. Bundle

`C:\Users\osrde\AppData\Local\Temp\claude\C--CLAUDE-zero7\5aaea98f-a498-4726-8890-7bd33adbcf64\scratchpad\pkg-out\pda\cyberdeckzero-launcher-0.8.8-pda.7-arm64.tar.gz`
with its `.sha256`: 57,245,873 bytes, sha256 `d5773554b8b1569f79c0c1ced32a05b153f02d52d11c73d407dcc8ad6013c221`;
development channel, the Store included; `payload/bin/cyberdeckzero` an aarch64 ELF reporting `0.8.8-pda.7` (and
holding the training page's strings), the tables in `payload/share/strokes/`. Built from this branch at `a7851db2`
(the code of this report; only reports and a comment in `strokes_train.h` came after) with
`APPLAUNCH_VERSION=0.8.8-pda.7 tools/dev/wsl-build.sh --arch arm64 --with-store` run inside WSL. pda.3-pda.6 stay
next to it. Not installed anywhere. The build log was in WSL's `/tmp`, which WSL emptied when the session closed, so
its warnings were not re-read; the new and changed files compile without a warning in the render harness and the unit
tests (`check-all` logs).

## 11. Files

`ext_components/cp0_lvgl`: `include/{cp0_zone.h, cp0_zone_ui.h, cp0_ui_osk.h, cp0_ui_widgets.hpp, strokes.h,
strokes_train.h (new)}`, `src/{cp0_zone.c, cp0_zone_ui.cpp, cp0_ui_osk.c, cp0_ui_osk_widget.cpp,
cp0/cp0_lvgl_dpi_scaled.c, strokes/strokes_table.c, strokes/strokes_train.c (new)}`, `tests/{test_zone.c,
test_ui_osk.c, test_ui_metrics.c}`.
`projects/APPLaunch`: `main/ui/{launcher_zone.cpp, launcher_zone.hpp, launcher_osk.cpp, launcher_osk.hpp, launch.cpp,
multitask/multitask_ui.cpp, multitask/multitask_ui.hpp, settings/settings_strokes_page.cpp (new),
settings/settings_strokes_page.hpp (new), settings/settings_keyboard_page.cpp, settings/settings_native_panel.cpp,
settings/settings_native_panel.hpp, settings/settings_page.cpp, settings/settings_page.hpp}`,
`tests/{test_app_launch_env.cpp, render/README.md, render/src/harness_main.cpp, render/src/harness_stubs.cpp,
render/scenes/zone_*.scene, render/scenes/strokes_training.scene (new), render/scenes/zone_home_pi4b.scene (new),
render/golden/480x640/*, render/golden/480x800/*}`, `pizero2w/README.md`.
`tools/strokes`: `Makefile, README.md, share/overlay-example.txt, src/log_main.c (new), tests/{test_train.c (new),
log.sh (new), test.h, test_main.c, check_names.sh}`. `docs`: `APP-CONTRACT.md`, `cp0_lvgl.en.md`, report 041 (section
13), this report.

## 12. The glyph picker (owner request of 2026-10-26, A)

The training walks the glyphs in an order; the picker lets the owner choose one. On the training page **Pick** opens
it (the glyph in progress keeps its block; its unfinished attempts are dropped):

- **Pick a glyph**: a grid of the letters and the digits, one cell per glyph (at least 36 px), and **Symbols** for a
  second page with the gestures and the punctuation (**Letters** goes back). Each cell is marked: grey = not trained
  (no block), green = trained, amber = hard (its latest rate under 75 %: the re-test's when there is one, else the
  training attempts'). The line under the title says so.
- A tapped glyph is trained as in the order (four attempts, the same rules for what is kept, its block replaced), then
  the picker comes back. **Picker** (in Back's place) returns without finishing it, **Redo** starts it again.
- **In order** resumes the order at the first glyph without a block; **Reset all** (confirmed) and **Finish** as on
  the training page. A picked glyph is in Finish's re-test like the others.
- The **summary** lists the glyphs that improved least first ("Improved least first:").
- The overlay keeps **one block per glyph**; its header carries the training numbers and the re-test writes a
  `# re-test a/b time` line into it, so the marks survive a restart. No template is kept beyond the table's limit
  (`STROKES_TRAIN_MAX_TEMPLATES`, 4096).

Library: `strokes_train_open_picker()`, `strokes_train_pick()`, `strokes_train_resume()`, `strokes_train_picked()`,
`strokes_train_mark()` (`STROKES_TRAIN_UNTRAINED / TRAINED / HARD`), phase `STROKES_TRAIN_PICK`; tests in
`tools/strokes/tests/test_train.c` (the marks from a block's header and re-test line, a pick, the picker back after
the fourth attempt, the re-test of a picked glyph, one block per glyph, the cap). Render: `strokes_training` gained the
shots `strokes_training_picker` (b amber: 2 of 4 recognised) and `strokes_training_picked` (e picked, written four
times, green). Commit `0fb5a087`.

## 13. On-screen keyboard: Auto, On, Handwriting + keyboard; Keyb and the cross (owner revisions of 2026-10-26, B)

Four owner messages, the last ones winning: a keyboard key in the band (built, then reverted: `7c20344c`), then a
setting value On plus Keyb and a cross, then On as the standard keyboard only with a new value for the band, then Off
removed. What is built:

| Settings > System > On-screen keyboard | no physical keyboard | a physical keyboard | `vkb=` | offered |
|---|---|---|---|---|
| **Auto** (0) | the standard keyboard for a text field | nothing (the keyboard types) | `auto` | every profile; the default elsewhere |
| **On** (2) | the standard keyboard | the standard keyboard too | `force` | every profile |
| **Handwriting + keyboard** (3) | the band (strip + writing boxes); Keyb: the standard keyboard with a close cross | nothing | `zone` while the band shows, else `auto` | the PDA profile only (`APPLAUNCH_PDA=1`: the writing zone exists); its default |

- **Off is gone** (owner: Auto covers the physical-keyboard case; the risk of locking oneself out without a keyboard is
  accepted). A stored 1 (Off of earlier versions) reads as **Auto** on every profile, the PDA one included (not as its
  default); it is not rewritten until the user picks a value. A stored 3 on a profile without the zone reads as Auto;
  any other value as the profile's default. `cp0_ui_osk_setting_read()` / `cp0_ui_osk_setting_default()` are
  unit-tested (`test_ui_osk`); the scenes `zone_old_off` and `osk_policy` (review only) start from a stored 1 and check
  it (`expect_mode 0`, the keyboard shown). `APPLAUNCH_OSK=off` (development) still turns every keyboard off.
- **Auto on the PDA profile** keeps the existing fallback keyboard: the standard keyboard, no band (`zone_osk_auto`).
  The user can type there: the keyboard has every key, the number pad for Number fields, the terminal's Alt and arrows.
- **On**: the standard keyboard only (no band, no Keyb, no cross), shown for every focused text field even with a
  physical keyboard; never under the lock screen nor in a desktop session (`zone_osk_on`).
- **The strip**: Shift removed, **Keyb** added before Tab: `Keyb Tab Ctrl Alt < ^ v > Enter`, widths 1.1 / 1 / ... /
  1.2 arrow keys (at 480 px: Keyb 51 px, Tab to the arrows 46-47, Enter 56; 46 px = 5.4 mm at 8.54 px/mm, 5.1 mm at 9.04 px/mm;
  every key at least 5 mm, `test_zone`). Upper case is the recogniser's own (the shift gesture, caps lock), or the
  keyboard's. Ctrl and Alt as before (sticky, a double tap locks).
- **Keyb** puts the standard keyboard in the band's place for this field **and the next ones** (Tab to the next field,
  other pages, the terminal) until its **cross**: one key wide at the right end of its top row (`LV_SYMBOL_CLOSE`), the
  arrows left of it; on the terminal's keyboard (Esc Tab Ctrl Alt) the utility keys narrow from 62 to 50 px at 480 px
  to keep the arrows. The cross brings the band back. Keyb does nothing on the Strokes training page (the page needs
  the band) and is drawn dimmed there, in its place (the coordinator's decision: the strip's layout stays; a disabled
  dialog button's look: background 40 %, label 50 %). Changing the setting forgets a Keyb choice.
- **In the terminal** the keyboard's keys reach the PTY exactly as a physical keyboard's (the scene's PTY log: `s`,
  `u`, `\x0d`): a sudo password is typed exactly with Keyb.
- **A physical keyboard** hides the band and the keyboard for text fields (the fallback rule), except with On.
- **Default on the PDA profile**: the launcher reads Handwriting + keyboard when nothing is stored and the writing zone
  exists, i.e. from the profile's `APPLAUNCH_PDA=1` that install.sh writes; install.sh does not write `osk_mode` into
  the user's settings, so a reinstall keeps the user's choice. Consequence: a pda.7 board where the owner had picked
  Auto explicitly (stored 0) shows the standard keyboard after the update, until Handwriting + keyboard is picked once.

Code: `cp0_ui_osk_setting_t` (Auto 0, On 2, Hand 3), the policy's `setting_on` (in `setting_off`'s place),
`cp0_ui_osk_layout_add_close()` and the key kind `CP0_UI_OSK_K_CLOSE` (sends nothing), `cp0::ui::osk::set_close()`,
`cp0_zone_ui_set_keyb_cb()`; `launcher_osk` (`mode()`, `choices()`, `set_mode()`), `launcher_zone` (the band is the
input only with Handwriting + keyboard, the launcher in front and no Keyb choice; `mode_changed()`), the Settings list
built from `launcher_osk::choices()`. Commits `9e75d51c` (code), `ae95958b` (render), `110b3aab` (docs).

Render (goldens at 480x640 and 480x800, all viewed): `zone_strip` adds `zone_strip_keyb` (the terminal's keyboard with
the cross) and `zone_strip_back` (the band again); new `zone_keyboard`: `zone_keyb_text` (Keyb on the Keyboard test),
`zone_keyb_numpad` (Tab: the Number field's pad, the cross still there), `zone_keyb_back`, `zone_osk_choice` (the three
values), `zone_osk_on`, `zone_osk_auto`; `pda_title_osk_choice` (the portrait launcher without the zone) shows the two
values Auto and On. Harness: `zonekey Keyb`, `tapkey CLOSE`, `tapbutton`, `expect_osk`, `expect_mode`.

What the owner checks on the board (about 10 min):

1. Settings > System > On-screen keyboard: Auto, On, Handwriting + keyboard (checked).
2. The CLI: the band comes up; **Keyb**: the keyboard with a cross at the top right; `sudo -v`, the password on the
   keyboard, Enter; the cross: the band again.
3. Keyboard test: Keyb on Text, Tab to Number: still the keyboard; the cross.
4. On: the keyboard even with the Bluetooth keyboard connected. Auto: no band, the keyboard only without the Bluetooth
   keyboard. Back to Handwriting + keyboard.

Risks and open points:

1. **Keyb on the training page**: dimmed and inert (decided by the coordinator; the 16 `strokes_training` goldens
   changed in that key's box only).
2. **No Shift on the strip**: upper case only by the shift gesture (or Keyb); Ctrl+Shift combinations need Keyb.
3. **480x640 terminal**: the terminal's keyboard takes 285 of the 640 rows (the band takes 240).
4. **A stored Auto from pda.7** (the default paragraph above): picked once, then it stays.
5. **No way back to the band with On**: by design (On = the standard keyboard only).

## 14. Checks and bundle 0.8.8-pda.8

- `check-all.sh` on the code of `9e75d51c`-`110b3aab` (then, for `176d2a5a`, the zone and training scenes rendered
  again and compared: only the 16 `strokes_training` goldens changed, in the Keyb key's box): cp0-unit, app-unit,
  strokes, render (74 scenes), compare (178 goldens identical), twice (1956 PNGs byte-identical), header (338),
  contract (509 checks), branding: all pass. (The Windows side of that run was stopped for low memory while it waited;
  the WSL side finished every step and its logs were read.)
- Install tests (`tests/test-install-board.sh` in a Linux clone of the branch at `110b3aab`): 375 passed, 0 failed.
- Goldens: 36 changed only in the strip row (Keyb, no Shift: y 408-452 at 480x640, 490-532 at 480x800) or, for
  `pda_title_osk_choice`, the list's second row (On instead of Off); 16 new; the other 110 byte-identical (every
  640x480 and 480x320 golden among them).

**Bundle `0.8.8-pda.8`**:
`C:\Users\osrde\AppData\Local\Temp\claude\C--CLAUDE-zero7\5aaea98f-a498-4726-8890-7bd33adbcf64\scratchpad\pkg-out\pda\cyberdeckzero-launcher-0.8.8-pda.8-arm64.tar.gz`
with its `.sha256`: 57,263,678 bytes, sha256 `5d2a3d858790bbc497a15ca7c2be8817b07c6d1cef90758508a5320c200501bc`;
development channel, the Store included; `payload/bin/cyberdeckzero` an aarch64 ELF holding `0.8.8-pda.8` (and the
strings "Handwriting + keyboard", "Keyb"). Built from this branch at `176d2a5a` (the Keyb dimmed on the training page
included) with `APPLAUNCH_VERSION=0.8.8-pda.8 tools/dev/wsl-build.sh --arch arm64 --with-store` run inside WSL, alone
(memory checked first); the build log has no warning in the files this round changed. pda.3-pda.7 stay next to it. Not
installed anywhere.
