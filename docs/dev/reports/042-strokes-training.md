# 042 - PDA profile: Strokes training (S6, brought forward) and the writing zone on demand

Status: built 2026-10-25 on branch `pda-s3-zone` (worktree `launcher-s3`, after report 041 / `0.8.8-pda.6`), from the
owner's brief after the board test of `0.8.8-pda.5` on the Pi 3A+ (11 of 26 letters refused: b, p, s, e, m, o, q, r,
y, z) and three owner decisions that came in while it was built (sections 2 and 7). Committed on the branch, not
pushed; no board touched. Bundle `0.8.8-pda.7` (section 10).

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
