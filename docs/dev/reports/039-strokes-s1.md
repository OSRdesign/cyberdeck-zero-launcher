# 039 - Strokes S1: the recogniser core (hand-off)

Status: built 2026-10-25 for phase S1 of the PDA profile (report 038, sections 5 and 8, owner defaults of section 12:
profile "PDA", input method "Strokes"). Worktree `launcher-strokes`, branch `pda-s1-strokes` from `github/main` = v0.8.7
(7bfb4b3). New files only: `tools/strokes/` and this report; `SConstruct`, `Kconfig`, `ext_components` and
`tools/dev/check-branding.sh` untouched. Nothing committed, no board touched.

Markers: **M** = measured today on the PC (WSL Ubuntu 24.04, Intel i5-12450H, gcc 13.3, `-O2`); **S** = our source
(file:line); **I** = inferred, to check on a board. Cost unit: one worker session.

**Real stylus accuracy is not measured.** Every accuracy number in this report comes from synthetic strokes generated
from the table's own templates (section 5). They show that the code does what it should under the modelled noise;
they do not predict how well the owner's handwriting will be read. Section 10 is the capture that will.

**Second pass, same day (section 13):** the glyph table was rebuilt from the reference chart of the real alphabet
(93 glyphs + an optional section of 14, instead of 42), with the punctuation and extended sets, X as two strokes and
dead accents. Sections 1-12 describe the first cut; where they disagree on the table, the modes, the accuracy or the
capture labels, section 13 is current.

## 1. Summary

- **Built:** a C99 recogniser library with no LVGL dependency (`include/strokes.h`, five source files, 27.7 KB of
  aarch64 code, no allocation after start-up, no globals), a plain text glyph table of 42 glyphs and 47 variants with a
  user overlay format, a synthetic stroke generator with an evaluation command, a capture tool (live evdev or evtest
  text), a corpus replay harness, a benchmark, 45 unit tests (1106 checks) that also run under ASan + UBSan, an
  end-to-end capture test and a name check. Plain Makefile for WSL; zero warnings with `-Wall -Wextra` and more,
  enforced with `-Werror` in `make check` (M).
- **Synthetic accuracy (M, default generator = report 038's settings, seed 1, 200 samples per glyph):** 99.44 % correct,
  0.48 % rejected, 0.08 % wrong; letters 99.2 %, digits 99.9 %, gestures 99.7 %, period 100 %. Seeds 2-5: 99.45-99.54 %.
  Random scribbles: 98.2 % rejected. Report 038's regression gate (at least 98 % correct, at most 2 % rejected) passes
  with room; harsher settings give 82.2 % correct, 14.8 % rejected, 2.9 % wrong.
- **Speed (M + I):** 12.6 us per stroke on the PC for the whole path; 0.13-0.25 ms estimated on the Pi Zero 2 W and
  Pi 3A+ (report 038's budget: under 1 ms). `build/aarch64/strokes-bench` measures it on a board in one command.
- **Deviations from 038 that the numbers asked for (section 4):** a bounded slant tolerance (+-15 degrees) and an
  aspect tolerance (+-20 %) in the matcher, a speed-aware drop-out rule, a few explicit gates for the confusable pairs,
  and two columns more in the corpus format. Report 038's matcher exactly as written scores 88.2 % on the same data.
- **Next:** the owner's capture session (section 10: about 500 strokes, 45-60 minutes), then S3 (section 9) and S6.

## 2. What was built

| Path | Content |
|---|---|
| `tools/strokes/include/strokes.h` | public API: table, engine, one-shot classify, overlay line helper, names |
| `tools/strokes/src/strokes_geom.c` | resampling, smoothing, hook trimming, features, chain code, normalisation, bounded-rotation score |
| `tools/strokes/src/strokes_table.c` | glyph table parser, gates, outputs, overlays (`replace`, `remove`), atomic adds |
| `tools/strokes/src/strokes_match.c` | gates, aspect variants, best variant per glyph, acceptance / rejection, candidates |
| `tools/strokes/src/strokes_capture.c` | clean-up: tap, dwell, too short, decimation, fine path, hooks, smoothing |
| `tools/strokes/src/strokes_engine.c` | frames in, drop-out healing, result queue, mode machine, `strokes_classify`, `strokes_template_line` |
| `tools/strokes/src/strokes_internal.h` | internal types and thresholds |
| `tools/strokes/share/glyphs.txt` | the shipped table (our own shapes); `share/`, not `data/`: the root `.gitignore` ignores every directory named `data` |
| `tools/strokes/share/overlay-example.txt` | the user overlay format by example (variant, replace, remove) |
| `tools/strokes/src/{sample,synth,report,evparse,tools_common}.[ch]` | PC tools: samples and corpus format, generator, per-glyph report, evdev / evtest parser |
| `tools/strokes/src/{eval,replay,capture,bench}_main.c` | `strokes-eval`, `strokes-replay`, `strokes-capture`, `strokes-bench` |
| `tools/strokes/tests/` | `test_*.c` (7 suites), `e2e.sh`, `check_names.sh` |
| `tools/strokes/Makefile`, `README.md` | build, design, formats, tools, how S3 uses it |

Size: library 2207 lines, tools 2866, tests 2001 (M, `wc -l`).

## 3. Decisions

| # | Decision | Reason |
|---|---|---|
| D1 | Own recogniser from scratch, $1-style front end, Protractor-style cosine score, no rotation normalisation | owner decision via report 038; direction- and start-specific shapes need ordered points |
| D2 | N = 32 points for the matcher, a 64-point fine path for clean-up | 038's N; 64 keeps hooks and corners visible for trimming |
| D3 | Uniform scaling by the vector's length for the score, the stroke's square box for the gates | both keep the aspect: `i`, space, enter and `l` stay apart (tests `lines_stay_apart`) |
| D4 | Gates per variant, written in the table or derived from the template (start / end within 0.40, line or not, closed or open); the parser refuses a template that fails its own gates | authoring mistakes show at load time; most lines need no gates at all |
| D5 | Line hysteresis: line glyphs need straightness >= 0.90, other glyphs <= 0.975 | a wobbly space is still a space, a nearly straight j can still be a j |
| D6 | Explicit gates only where the synthetic confusions asked: c (`turn=ccw:170 corners=0-1`), e (`corners=1-5`), k and v (`corners=1-3`) | the pairs c / k, c / e, l / c, u / v were ambiguous; with the gates the wrong rate fell to under 0.1 % |
| D7 | Accept when score >= 0.80 and margin >= 0.04 (score = 1 - angle / 90 degrees) | the balanced point of the sweep (section 5.4); 0.85 is the stricter option (98.4 % correct, 99.5 % of scribbles rejected) |
| D8 | A tap is a stroke whose points stay within 1.5 mm and lasts at most 250 ms; still and longer is a dwell, rejected; 1.5-3 mm is too short, rejected | a jittery stylus inflates the path length of a tap, not its extent |
| D9 | The period is a tap in the `punct` set; the `tap` glyph (set `any`) is the punctuation shift; caps lock is two shift gestures, not a glyph | 038's 42 entries: 26 + 10 + space, backspace, enter, shift, tap, period |
| D10 | Output: UTF-8 text (case applied) or a named key with its Linux code; the US-layout key mapping stays in S3 (the on-screen keyboard's table) | no keyboard layout in the core; one mapping for the OSK and Strokes |
| D11 | Results are final heal_ms after the lift; the engine queues them and the caller polls from a timer | a drop-out can only be healed if the lift is not final at once |
| D12 | A table add is atomic (the table is copied, the source applied to the copy) | a broken user overlay can never damage the shipped table |
| D13 | Glyph table and corpus are plain text, parsed by our own code | report 038: no JSON arrays in the C core |
| D14 | The trademark check is a new script in `tools/strokes/tests` (pattern written with a character class), not a change of `check-branding.sh` | brief: new files only; S3 adds the word to `check-branding.sh` |
| D15 | Thresholds stay first guesses until real strokes exist; every one is a `strokes_config` field and a `--cfg` key | S6 sets them from the owner's corpus with `strokes-replay --sweep` |
| D16 | The table's numbers are parsed and written by our own code, always with a '.', never with `strtod` / `printf("%f")` | the launcher may set a locale whose decimal separator is a comma; the test runs in WSL's C locale only (no comma locale installed), the code has no locale dependency by construction |

## 4. Deviations from report 038

| Report 038 says | S1 does | Why (M, default generator unless noted) |
|---|---|---|
| no rotation normalisation; Protractor's rotation search not needed | still none, but the cosine is maximised over a slant of up to +-15 degrees (Protractor's closed form, bounded) | a writer's slant costs score linearly: without it 97.14 % correct and 2.79 % rejected, with it 99.44 / 0.48. Lines keep their own direction gates (+-35 degrees), so `i`, enter and space stay apart |
| uniform scale, aspect kept | the same, plus an aspect tolerance: the cleaned path is also tried 20 % wider and narrower, before resampling | per-axis scale noise of 0.8-1.2 (038's generator) was the main cause of rejections: without it 98.18 / 1.74. Stretching after resampling did not help (b stayed at 85 %), stretching before it did (b 96.5 %) |
| drop-out: a lift then a press within 50 ms and 3 mm continues the stroke | within 50 ms and within 3 mm plus the distance the pen covers at its speed before the lift (x 1.25, at most 15 mm more) | at 60-150 mm/s the pen moves 2-7 mm during a 30 ms drop-out. With a drop-out in every stroke, the plain rule heals about a third (36.3 % correct, 63.0 % wrong because split), the speed-aware rule 98.1 / 1.6 / 0.4 |
| a 3-point moving average or a 1€ filter | distance decimation (0.35 mm) of the raw points, then one [1 2 1] pass on the 64-point path | decimation removes the jitter of a slow or resting pen; the smoothing pass itself changes little (99.43 % without it) |
| (not in 038) | landing and lift hooks are trimmed (at most 8 % of the path and 2.5 mm, turning back by more than 100 degrees) | with a hook at both ends of every stroke: 99.30 % with trimming, 96.95 % without |
| features: start / end cell, line class, closedness, turning sign, chain code for diagnostics | all of them, as gates written per variant or derived; turning with a magnitude (`turn=ccw:170`); a corner count (the brief's "turn count"); the chain code is also an optional gate (`dirs=`) | section 3, D4-D6 |
| glyph table columns id, box, out, gates, template; overlay appended; `replace` | the same columns (the column is called set: letters, digits, punct, any), plus `tap` as a template, `U+XXXX`, `MODE_SHIFT`, `MODE_CAPS`, `MODE_PUNCT`, `KEY_TAB`; `replace <id> <set>` and `remove <id> <set>` are directive lines | a directive line keeps the glyph lines identical in the shipped table and the overlays; `remove` lets a user turn off a glyph he never writes |
| corpus line `glyph<TAB>ppmm<TAB>t_ms,x,y ...` | `glyph<TAB>box<TAB>ppmm<TAB>t_ms,x,y ... ^t_ms` | the box decides the set (digits vs letters), and the lifts with their times are what the drop-out rule needs at replay |
| `Graffiti` added to `check-branding.sh` | `tools/strokes/tests/check_names.sh` (in `make check`); `check-branding.sh` unchanged | the brief allowed new files only; the word list change belongs to S3 |

## 5. Accuracy on synthetic data (not real stylus)

### 5.1 Settings

Default generator (`strokes-eval` defaults, report 038's list; `synth_default` in `src/synth.c`): density 8.54 px/mm;
glyph size (longer side) uniform 8-16 mm; independent per-axis scale 0.80-1.20; rotation +-12 degrees; shear
+-0.15; smooth wobble 4 % of the size; corner rounding with probability 0.5, cutting up to 12 % of each side (full
reversals not rounded); Gaussian jitter sigma 1.0 % of the size per point; report rate 60-120 Hz; mean pen speed
60-150 mm/s, slower at both ends; a landing and a lift hook each with probability 0.3, 2-6 % of the size; each end
truncated with probability 0.2 by up to 4 % of the path; one drop-out with probability 0.1, 10-40 ms rounded to whole
frames; taps 40-180 ms within 0.3 mm; coordinates rounded to whole pixels; seed 1; 200 samples per glyph (8400).

Hard generator (`--hard`): size 6-20 mm, axis scale 0.70-1.30, rotation +-20, shear +-0.25, wobble 7 %, rounding
p 0.8 up to 20 %, jitter 2 %, speed 60-250 mm/s, hooks p 0.5 up to 10 %, truncation p 0.4 up to 8 %, drop-out p 0.3
up to 48 ms; the rest as the default.

Recogniser: the defaults (`strokes_config_default`): min_score 0.80, min_margin 0.04, slant tolerance 15 degrees,
aspect tolerance 20 %, heal 50 ms / 3 mm + reach at pen speed, tap < 1.5 mm and < 250 ms, minimum stroke 3 mm,
smoothing and hook trimming on.

### 5.2 Results per glyph class (M)

| Set | Glyphs | Samples | Correct | Rejected | Wrong (incl. split) | Hard: correct / rejected / wrong |
|---|---|---|---|---|---|---|
| letters | 26 | 5200 | 99.2 % | 0.7 % | 0.1 % | 80.2 / 16.4 / 3.4 % |
| digits | 10 | 2000 | 99.9 % | 0.1 % | 0.0 % | 84.9 / 13.8 / 1.3 % |
| punct (period) | 1 | 200 | 100 % | 0 % | 0 % | 100 / 0 / 0 % |
| any (space, bksp, enter, shift, tap) | 5 | 1000 | 99.7 % | 0.3 % | 0.0 % | 83.7 / 11.8 / 4.5 % |
| **all** | **42** | **8400** | **99.44 %** | **0.48 %** | **0.08 %** | **82.24 / 14.83 / 2.93 %** |

- Weakest glyphs (default): b, e and v 96.5 %, p 97.5 %, w 98.0 %, r 98.5 %; all others 99-100 %. Most frequent:
  e rejected as e (4), v rejected (4), b rejected as s (3), p with no candidate (3), e read as c (2), v read as u (2).
- Weakest glyphs (hard): x, r, 8, v, w, b, m, p rejected 12-25 %; the wrong answers there are mostly split strokes
  (drop-outs at up to 250 mm/s), then w, o and v read as u (31), 1 as enter, y and z as t (4 and 3).
- Random scribbles (4-8 random vertices, the same noise): 98.2 % rejected (2000). Random walks in the fuzz test: at
  least 90 % rejected (a random shape can be a real glyph, so 100 % is not the goal).
- Seeds 2, 3, 4, 5: 99.51, 99.46, 99.45, 99.54 % correct; 0.40-0.51 % rejected; 0.04-0.08 % wrong.

### 5.3 Stress and ablation (M, all 42 glyphs, 200 each; correct / rejected / wrong %)

| Generator change (default recogniser) | Result | Recogniser change (default generator) | Result |
|---|---|---|---|
| none (default) | 99.44 / 0.48 / 0.08 | none (default) | 99.44 / 0.48 / 0.08 |
| rotation +-20 | 98.06 / 1.69 / 0.25 | **report 038 as written** (no slant or aspect tolerance, plain heal) | **88.24 / 5.60 / 6.17** |
| rotation +-30 | 91.23 / 7.36 / 1.42 | no slant tolerance | 97.14 / 2.79 / 0.07 |
| jitter 2 % | 98.21 / 1.63 / 0.15 | no aspect tolerance | 98.18 / 1.74 / 0.08 |
| jitter 3 % | 91.58 / 7.85 / 0.57 | plain heal (3 mm from the lift point) | 93.42 / 0.42 / 6.17 |
| a drop-out in every stroke | 98.07 / 1.55 / 0.38 | no hook trimming | 99.01 / 0.87 / 0.12 |
| report rate 30-60 Hz | 98.02 / 1.43 / 0.55 | no smoothing | 99.43 / 0.49 / 0.08 |
| pen speed 150-300 mm/s | 96.44 / 2.50 / 1.06 | min_score 0.75 | 99.49 / 0.42 / 0.10 |
| small glyphs 5-8 mm | 97.89 / 1.85 / 0.26 | min_score 0.85 | 98.39 / 1.52 / 0.08 |
| large glyphs 15-25 mm | 99.68 / 0.31 / 0.01 | min_margin 0.02 | 99.55 / 0.36 / 0.10 |
| deck density 11.3 px/mm | 99.42 / 0.50 / 0.08 | min_margin 0.08 | 98.45 / 1.49 / 0.06 |
| 640x480 4" density 7.9 px/mm | 99.40 / 0.52 / 0.07 | hard + min_score 0.75 | 86.37 / 10.24 / 3.39 |
| no noise at all | 100 / 0 / 0 | hard + aspect tolerance 0.3 | 83.02 / 14.11 / 2.87 |

- Density does not matter (all lengths are in mm); size matters only below 8 mm.
- Fast strokes on a 60 Hz panel lose detail (a 40 mm w at 300 mm/s is 8 frames): w then reads as u.

### 5.4 Threshold sweep (M, default generator; correct / rejected / wrong %)

| min_score | margin 0.00 | margin 0.02 | margin 0.04 | margin 0.08 |
|---|---|---|---|---|
| 0.70 | 99.7 / 0.1 / 0.20 | 99.6 / 0.2 / 0.13 | 99.5 / 0.4 / 0.11 | 98.5 / 1.4 / 0.07 |
| 0.80 | 99.6 / 0.3 / 0.15 | 99.5 / 0.4 / 0.10 | **99.4 / 0.5 / 0.08** | 98.5 / 1.5 / 0.06 |
| 0.85 | 98.5 / 1.4 / 0.13 | 98.5 / 1.4 / 0.10 | 98.4 / 1.5 / 0.08 | 97.5 / 2.4 / 0.06 |
| 0.90 | 90.7 / 9.2 / 0.10 | 90.6 / 9.3 / 0.07 | 90.6 / 9.3 / 0.06 | 89.8 / 10.1 / 0.05 |

On the hard data the same thresholds trade 82.2 / 14.8 / 2.9 (0.80, 0.04) against 87.7 / 8.3 / 4.1 (0.70, 0.04): a
lower threshold accepts more and guesses more. Real strokes will sit between the two generators (I); S6 picks the
thresholds from the owner's corpus with `strokes-replay --sweep`.

## 6. Performance

| Measure (M, `strokes-bench --n 1000 --reps 20`, 33 raw points per stroke on average) | PC |
|---|---|
| whole engine path per stroke (frames, healing, clean-up, features, 3 aspect variants x 47 templates, modes) | 12.6 us |
| clean-up + features | 4.2 us |
| resampling of the variants, gates, matching | 6.5 us |

Estimate for the Cortex-A53 boards (I), two methods:
1. The factor used before for this PC: the deck is "about 20 times slower than this PC at most" (report 015) and an
   A53 "perhaps 10-20 times slower" (report 037): 0.13-0.25 ms per stroke on the Pi Zero 2 W (1.0 GHz) and the Pi
   3A+ (1.4 GHz, so nearer the low end).
2. Operation count: the matcher does 3 x 47 x 32 x 4 = 18 000 multiply-adds and the clean-up about 400
   `hypotf` / `atan2f` calls and a few thousand more operations; at roughly one operation per cycle for the in-order
   A53 and 50-100 cycles per libm call, 0.06-0.1 ms at 1 GHz.

Both are far under report 038's 1 ms budget, and recognition runs once per stroke, 50 ms after the lift. The static
`build/aarch64/strokes-bench` (built and checked as an aarch64 ELF, not run: no board) prints the real number on a
board. Memory: the engine is 34 KB (2048 raw points and 8 queued results), a table of 47 variants about 25 KB; the
clean-up uses 17 KB of stack.

## 7. Tests (M)

`make check`: build with `-Werror`, unit tests, the same tests under ASan + UBSan (`-fno-sanitize-recover=all`, leak
detection on), the regression gate, the end-to-end capture test with the plain and the sanitised tools, the name check:
all pass, 0 warnings.

| Suite | Tests | Checks | What |
|---|---|---|---|
| geometry | 8 | 185 | resampling (equidistant along the path, degenerate inputs), smoothing, normalisation, line and shape features, chain codes, hook trimming, the bounded-rotation score |
| table | 9 | 187 | the shipped table (all 42 ids and sets), 26 kinds of bad input with message and line number, atomic adds, overlay variants, replace (also with a new output), remove and re-add, tap redefinition, outputs (`U+XXXX`, keys, modes, multi-byte), deterministic load, `strokes_template_line` round trip, the example overlay, numbers independent of the C locale |
| match | 6 | 151 | cell and implicit gates, line / closed / turn / corners / dirs gates, every template recognised as itself (score > 0.93), lines and the L kept apart (i, space, bksp, enter, shift, 1, short and slanted lines), rejection (clockwise circle, scribble, ambiguous pair, low score, no candidate, thresholds), sets |
| engine | 9 | 52 | tap and period, dwell, too short, drop-out healing (near, far, late, fast pen, plain rule), poll timing and the heal window, the mode machine (shift, caps, keys, digits, punctuation, rejection), queue overflow, odd event orders, NaN and huge coordinates, the clock going back, 5000-frame strokes, density independence, determinism |
| tools | 8 | 76 | corpus write / read round trip with drop-outs, corpus errors, replay healing, generator determinism and settings, evdev parser (multitouch, second finger, drop-out grouping, `SYN_DROPPED`, single touch, rotations), evtest text round trip, report counts |
| fuzz | 4 | 408 | 200 000 random engine events (NaN, infinities, clock jumps), 3000 random walks with random drop-outs (results well-formed, at least 90 % rejected), 400 mutated glyph tables, 20 000 random corpus / evtest lines and evdev events |
| accuracy | 1 | 47 | the synthetic gate at seed 1, 100 per glyph: >= 98 % correct, <= 2 % rejected, <= 0.5 % wrong, every glyph >= 85 %; >= 95 % of 1000 scribbles rejected (M: 99.57 / 0.40 / 0.02, 98.4 %) |
| **total** | **45** | **1106** | |

End to end (`tests/e2e.sh`): 840 synthetic strokes are written as evtest text, read back by `strokes-capture`, written
as a corpus and replayed by `strokes-replay`; the result equals `strokes-eval`'s direct evaluation of the same
strokes (99.29 / 0.60 / 0.12 %). Not testable on the PC: real evdev ioctls (`--evdev`, `--list`, `EVIOCGRAB`,
`EVIOCSCLOCKID`; WSL has no evdev) and real styluses.

## 8. Limits and risks

- **Unknown real accuracy.** The templates and the thresholds are first guesses; the synthetic data comes from the same
  templates. A writer whose shapes differ from ours (start point, direction, retracing) gets rejections until S6
  trains his own variants.
- **Weak pairs** in the synthetic data: b / d / p / r retrace variants, e / c, v / u, w / u when fast. The explicit
  gates on c, e, k and v (section 3, D6) are tuned on synthetic strokes and may need loosening on real ones.
- **Shapes that are our own reading** of the Palm-era alphabets and may not match the owner's memory: k (an angle
  bracket), t (a right angle), x (two diagonals joined by the right side), y (with a retrace), q (circle and tail), 4
  (two variants), and the retrace variants of b, d, p, r. Section 10 asks.
- **Drop-out rule:** a new stroke started within 50 ms of a lift and within reach is glued to the previous one. People
  cannot lift and start a new glyph that fast (I), but a bouncing stylus can; the capture will show.
- **Panels under 60 Hz:** a two-frame drop-out is longer than 50 ms; raise `heal_ms` there.
- **One contact, single strokes only**; no punctuation beyond the period, no accents, no command stroke (S6).
- **The core is not thread-safe**; S3 calls it from the display thread only.

## 9. What S3 needs

- Move `include/strokes.h` and `src/strokes_*.c` into `ext_components/cp0_lvgl` (after 029 lands); keep `tools/strokes`
  for the tools and tests (or point them at the new place). Ship `share/glyphs.txt` as
  `/usr/share/APPLaunch/strokes/glyphs.txt`; load `~/.config/applaunch/strokes-user.txt` after it and log (do not
  fail) when it is refused.
- A second consumer of the raw touch frames (report 038 section 2.4: the hook is single-subscriber today): contact 0
  only, logical px, `CLOCK_MONOTONIC` us, `strokes_down` with the box under the press, `strokes_move`, `strokes_up`.
  Frames that start outside the boxes are not the zone's.
- `cfg.ppmm` from the layout service (the app area's density).
- A timer (the display tick) calling `strokes_poll(e, now)` until it returns 0; results go to the OSK's US table ->
  `cp0_keyboard_inject_key()` (characters), `strokes_key_linux_code()` (keys), the mode line (modes), the red ink flash
  (rejections). `strokes_set_mode(e, 0)` when the zone opens, `strokes_cancel(e)` when it closes.
- A version line: `STROKES_VERSION` in `--version`, since each portrait app pins its own copy (report 038 risk 3).
- Add the trademark word to `tools/dev/check-branding.sh` (docs allow-listed) and run `make -C tools/strokes check` in
  the PC checks.
- A dev switch that appends the zone's real strokes to a corpus file (`corpus_write` format) for S6.

## 10. The owner's stylus capture: what to write, how many samples

Goal: the first real corpus, to measure accuracy and to set the thresholds (heal, tap, score, margin) in S6. Board:
the Pi 3A+ with the 4.3" DSI held in portrait; the owner's capacitive stylus. Setup as in `tools/strokes/README.md`
("Capturing a corpus"): stop the launcher, run `strokes-capture --evdev ... --size 800x480 --rotate 90 --panel-mm
56x94 --table glyphs.txt`, check the orientation with one L (`dirs 26`).

Write each glyph **about 12-15 mm tall** (the letters box of the future zone is 32 x 18 mm), at a natural speed, in
one area of the panel, with a short pause between strokes. The tool prompts each glyph and prints what it read.

| Session | Command (labels) | Samples | Time | What it measures |
|---|---|---|---|---|
| A. taps, rests, lines | `--labels tap --reps 20`; `--labels "space bksp enter shift i" --reps 10`; `--labels dwell --reps 10` (rest the stylus about 1 s) | 80 | 5 min | tap size and duration, jitter at rest, line straightness, sample rate |
| B. letters | `--labels "a b c d e f g h i j k l m n o p q r s t u v w x y z" --reps 10` | 260 | 20-25 min | per-glyph accuracy and the shapes |
| C. digits | `--labels "0 1 2 3 4 5 6 7 8 9" --reps 10` | 100 | 8 min | the digits set (the box column is set to digits by the label) |
| D. natural | `--order rounds --reps 2` over "t h e q u i c k b r o w n f o x j u m p s o v e r t h e l a z y d o g" | 70 | 5 min | the same letters written as in a word, faster and less careful |
| E. finger (optional) | session B with a finger, `--reps 3` | 78 | 6 min | how much the stylus helps |

About 510 strokes (590 with the finger) in 45-60 minutes; the minimum useful corpus is 5 of each letter and digit (about 250 strokes).
Send the `.txt` files back; `strokes-replay --errors --sweep` on them gives the real accuracy, the confusions and the
thresholds for S6. Questions to answer while writing (or in a note with the files):
1. Do you write b, d, p, r with the stem down and back up, or with the stem up from the bottom? (The table has both;
   the unused one can go.)
2. Are k, t, x, y, q, g and 4 as you write them? If not, write them your way in session B (the tool records what you
   write; mark the glyph in the note).
3. Did a stroke ever break in two (the tool prints "2 contact(s)")? Which glyphs, how often?
4. Which stylus (tip size, passive or active)?

## 11. Owner questions (default in bold)

1. **Keep the slant (+-15 degrees) and aspect (+-20 %) tolerances and the speed-aware drop-out rule (section 4)**, or
   report 038's stricter matcher?
2. Thresholds: **balanced (score 0.80, margin 0.04)** or strict (0.85: fewer guesses, more rewrites)?
3. A space, backspace or enter after a pending one-shot shift: **keeps the shift for the next letter** or cancels it?
4. A non-punctuation stroke after the punctuation shift: **rejected and the shift ended** (the letter must be written
   again) or read as a letter?
5. **Capture session as in section 10**, before S3 starts?

## 12. Sources

- **S:** report 038 sections 2.4, 4, 5, 8, 10-12; reports 015 (line 38) and 037 (line 109) for the PC-to-A53 factor;
  `projects/APPLaunch/pizero2w/install.sh:512-536` (the `pi3a-wsdsi43` profile: 800x480, `APPLAUNCH_PANEL_MM=94x56`,
  touch in buffer orientation); `tools/multitasking-spike/README.md` (the launcher's user service `cyberdeckzero`, no
  evdev in WSL); `tools/dev/check-branding.sh` (unchanged).
- **M:** `make check`, `strokes-eval` (defaults, `--hard`, the `--gen` / `--cfg` variants of section 5.3, seeds 1-5,
  `--sweep`, `--garbage`), `strokes-bench --n 1000 --reps 20`, `aarch64-linux-gnu-size` of the library objects, all in
  WSL on the PC; the per-run outputs are reproducible with the commands above (deterministic generator).
- **References read, not copied:** the $1 and $P pages (New BSD, University of Washington), the Protractor paper
  (Y. Li); report 038's licence table applies unchanged (xstroke GPL-2.0 and Tomoe LGPL not read; EdgeWrite's method not
  used). Not legal advice.

## 13. Second pass: the real alphabet from its reference chart (2026-10-25)

The first table (sections 1-12) was our approximation from descriptions; several shapes differ from the real
alphabet. This pass rebuilds it from the reference chart, adds the layers the chart has (punctuation set, extended
set, accents, X in two strokes, caps lock) and re-measures. Same worktree and branch, still uncommitted; only
`tools/strokes/` and this report changed.

### 13.1 Reference and method

- **Reference:** the chart of the classic Palm Graffiti alphabet published on Wikimedia Commons as the file
  'Palm Graffiti gestures.png' (577 x 1216 px; four panels: normal alphanumeric gestures, punctuation mode "tap to
  enter, backspace to exit", extended character mode "prefix with" a down-right arrow, accents). It was the only
  source used for the shapes. The image is not in the repository and nothing of it is copied; its licence was not
  checked here (not legal advice). The name of the alphabet appears only in this report, as before.
- **Tracing (M):** each glyph was read on 5-8x enlargements of the chart with a coordinate grid; the start is the end
  without an arrow head, the end is the arrow head; a dashed line is a second stroke. Key points (5-25 per glyph, with
  the sharp corners, cusps and retrace reversals marked) went through a Catmull-Rom spline and were normalised to
  0..100 on the longer side, keeping the chart's aspect ratio. Every trace was drawn back over the chart and checked
  by eye glyph by glyph before it became a template (tooling in the session scratch directory, not in the repo).
- **One template per glyph:** the chart shows one way to write each glyph, so the table has one variant each (the
  first cut had second variants for b, d, p, r and 4; they are gone, the chart's way is the stem down and back up).
- **Straight diagonal lines** (enter, the extended prefix, the second stroke of X, `\`, `/`; `,` is 45 degrees in the
  chart already) are drawn at 50-53 degrees in the chart because its cells are taller than wide. A line template is a
  direction (gate +-35 degrees), so the templates use 45 degrees, the middle between the chart's cells and the wider
  than tall writing box of S3 (32 x 18 mm). Both were measured (13.4): with the chart's angles i, 1, `/` and the
  prefix lose 5-10 points to each other.

### 13.2 What changed

| Path | Change |
|---|---|
| `share/glyphs.txt` | rebuilt: 93 glyphs, 93 templates (25 letters, 10 digits, 6 gestures, 33 punctuation, 19 extended); header states the tracing source; no explicit gates (all derived from the templates) |
| `share/glyphs-extra.txt` | new optional section, 14 glyphs: the rarer extended characters (`÷ ø § µ ∫ ¢`) and panel 4 (`ç`, `æ`, six dead accents); leaving the file out switches them off |
| `share/overlay-example.txt` | example 3 now removes `pound` (x is no longer a letters glyph) |
| `include/strokes.h` | version 0.2.0; ext set, ext and accent modes, accents (13.3) |
| `src/strokes_table.c` | set `ext`; outputs `MODE_EXT`, `ACCENT_*`; candidate rule of the gestures per set; UTF-8 helpers |
| `src/strokes_match.c` | candidate rule and tap lookup per set |
| `src/strokes_engine.c` | mode machine (prefixes, backspace leaving a prefix, rejection keeps it, dead accents and their composition table, Latin-1 upper case); 13.10: the punctuation and extended modes stay on |
| `src/strokes_geom.c` | hook trimming: the turn back must lie within the hook length (13.4: the tails of q, g, v were cut off) |
| `src/{eval,replay,capture,bench}_main.c`, `sample.[ch]`, `report.c`, `tools_common.c` | ext set in the tools (mode per set, labels, per-set report, bench skips the prefix sets) |
| `tests/` | new engine tests (extended prefix and X, accents), prefix lines, optional section, unique ids, hook rule, fuzz invariants of the new modes; 47 tests, 1400 checks (first cut: 45, 1106) |
| `README.md` | licence paragraph (traced from the public chart, our own drawings, no Palm data), shipped set, modes, formats, limits |

**API changes (all source-compatible; every 0.1.0 call keeps its signature):**

| Change | Detail |
|---|---|
| `STROKES_SET_EXT = 4`, set name `ext` | the extended set; `STROKES_SET_ANY` keeps its value 3 |
| `STROKES_MODE_EXT = 8u`, `STROKES_MODE_ACCENT = 16u` | the extended prefix; a dead accent pending |
| `enum strokes_accent`, `strokes_result.accent`, `strokes_accent_name()` | grave, acute, circumflex, tilde, diaeresis, ring; the accent pending after each result (new struct field: the layout changes, rebuild callers) |
| table tokens | set `ext`; outputs `MODE_EXT`, `ACCENT_GRAVE`, `ACCENT_ACUTE`, `ACCENT_CIRCUMFLEX`, `ACCENT_TILDE`, `ACCENT_DIAERESIS`, `ACCENT_RING` |
| behaviour: gestures | `any` glyphs are candidates in the letters and digits sets only; after a prefix only the backspace gesture (in the punctuation set the gestures' lines are glyphs: a line right is `-`, up is `!`, down-left is `,`) |
| behaviour: prefixes | a rejection keeps the prefix (first cut: ended it); backspace leaves it and types nothing (`STROKES_MODE` result, key none) |
| behaviour: case | upper-casing covers the Latin-1 letters (`ç` -> `Ç`, `ÿ` -> `Ÿ`), not only a-z |
| `strokes_classify` | accepts `STROKES_SET_EXT`; fills `accent` for an accent glyph |
| tools library | `replay_mode_for_set()`; `tools_label_set` also looks in the ext set |

### 13.3 Mode design (judgement calls on the chart, owner can change them)

| Chart | Decision | Why |
|---|---|---|
| punctuation "tap to enter, backspace to exit" | **superseded by 13.10 (owner, 2026-10-25): the mode stays on.** First pass: a one-glyph prefix; a rejected stroke keeps it; the backspace stroke leaves it without deleting; a tap inside it is the period | "backspace to exit" only makes sense if the mode survives a failed stroke |
| extended mode "prefix with" a line down-right | the same rule as punctuation (13.10); a tap inside it is the bullet | one rule for both modes |
| X drawn as a solid down-right line and a dashed down-left line | X = the extended prefix, then `x` of the ext set (shift and caps apply) | the dashed line is a second stroke (as in caps lock); the first stroke is exactly the prefix |
| the extended `×` is the same down-left line | `×` gets no stroke | X and `×` would be the same two strokes; X is the priority panel |
| caps lock "up, up again" | two shift gestures; a third turns it off | as the first cut |
| accents panel (`Ç Æ` and `À Á Â Ã Ä Å`) | dead accents written in the letters box, before the letter (a e i o u y n); `ç` and `æ` are letters of their own | the six accent strokes are shown on A; in the ext set three of them would equal ext glyphs (Å = ø, Ä = ÷, Ç = ¢), in the punctuation set four (À = `` ` ``, Ã = `~`, Ä = `%`, Æ = `[`); in the letters set none collides (they are letters written backwards or retraced lines); a prefix composes into one precomposed character, so nothing has to be deleted. A letter without that accent is typed plain; a key drops the accent; backspace drops it without deleting |
| labels | `‘ ’ “ ”` (U+2018/19/1C/1D), the extended minus as `–` (U+2013), the extended tap as `•` (U+2022), `Ø` typed as `ø` (shift gives `Ø`) | see 13.6 |

### 13.4 Accuracy on synthetic data (M; still not real stylus)

Same generator and recogniser settings as section 5 (default = report 038's settings, seed 1, 200 samples per
glyph; `--hard` as in 5.1), now 93 glyphs (18 600 samples). The generator draws from the new templates, so these
numbers guard the code; they do not say how close the templates are to the owner's hand.

| Set | Glyphs | Default: correct / rejected / wrong % | Hard: correct / rejected / wrong % |
|---|---|---|---|
| letters | 25 | 99.1 / 0.7 / 0.1 | 79.8 / 17.7 / 1.8 |
| digits | 10 | 99.8 / 0.2 / 0.0 | 83.7 / 14.6 / 0.9 |
| punct | 33 | 99.8 / 0.2 / 0.0 | 83.0 / 14.0 / 1.8 |
| ext | 19 | 99.9 / 0.1 / 0.0 | 84.0 / 13.8 / 1.3 |
| any (gestures) | 6 | 98.8 / 1.1 / 0.0 | 79.2 / 16.8 / 2.1 |
| **all (glyphs.txt)** | **93** | **99.55 / 0.37 / 0.08** | **82.20 / 15.18 / 2.62** |
| all with glyphs-extra.txt | 107 | 99.58 / 0.35 / 0.07 | 82.65 / 14.92 / 2.43 |
| first cut, for comparison | 42 | 99.44 / 0.48 / 0.08 | 82.24 / 14.83 / 2.93 |

- Seeds 2-5 (default): 99.56, 99.50, 99.59, 99.49 % correct; 0.37-0.44 % rejected; 0.05-0.09 % wrong. Random
  scribbles: 98.5 % rejected (2000; 98.3 % with the optional section). Unit-test gate (seed 1, 100 per glyph): 99.48 /
  0.48 / 0.03 %, scribbles 98.6 % rejected.
- **Regression gate:** unchanged and true: at least 98 % correct and at most 2 % rejected (`make eval`, `make check`);
  the unit test also keeps wrong <= 0.5 % and every glyph >= 85 % (q is the lowest at about 91 %).
- The letter X is two strokes: the prefix (98.0 %) and then `x` (99.5 %), so about 97.5 % for the pair (I, product).
- Threshold sweep (default): unchanged in shape: 0.80 / 0.04 gives 99.6 / 0.4 / 0.08; 0.85 / 0.04 gives 98.8 / 1.1 /
  0.07; 0.90 gives 92.6 / 7.3 / 0.06.
- Speed: 10.7 us per stroke on the PC (first cut 12.6), 4.0 us of it clean-up: the letters box compares 31 templates
  (25 letters + 6 gestures), the first cut 31 too (26 + 5); about 0.11-0.21 ms estimated on the A53 boards. Library
  size: 29.8 KB of aarch64 code (first cut 27.7); `make aarch64` builds with `-Werror`.

**Two changes that the real alphabet asked for, with and without them (M, default unless noted):**

| Variant (final table) | Default | Hard | What moves |
|---|---|---|---|
| final (new hook rule, 45-degree diagonals) | 99.55 / 0.37 / 0.08 | 82.20 / 15.18 / 2.62 | |
| the first cut's hook rule | 98.92 / 0.57 / 0.51 | 83.10 / 14.13 / 2.77 | q 44.5 % (76 read as o), g 88.0 % (10 read as c): the trimmer cut the short end strokes (the tail of q, the bar of g) as if they were lift hooks |
| the chart's own diagonal angles (50-53 degrees) | 99.18 / 0.72 / 0.10 | 82.45 / 14.93 / 2.62 | i 89.5 %, 1 93.5 %, `/` 93.5 %, the prefix 93.5 %: 37-40 degrees between the line glyphs instead of 45 |

The hook change: a landing or lift hook is now trimmed only when the turn back happens within the hook length (the
farthest point from the end so far, then closer again, with the body leaving at more than 100 degrees). A short
first or last stroke of the glyph itself that turns further on is kept. It costs 0.9 points of correct strokes under
the harsh generator (longer hooks there are less often trimmed) and gains 0.6 points at the default settings; the
hook unit test keeps both cases. Nothing else of the matcher or the thresholds was changed, and the table has no
explicit gates: gates tried on o and q (corners, closed) turned rejections into wrong answers (o read as u) and were
dropped.

**Per glyph (M; default and hard, seed 1, 200 each; split strokes make up the rest to 100 %; * = optional section,
measured with it loaded):**

| Glyph | Set | Default: ok / rej / wrong % | Most frequent miss | Hard: ok / rej / wrong % |
|---|---|---|---|---|
| `a` | letters | 100.0 / 0.0 / 0.0 | - | 93.0 / 6.0 / 0.0 |
| `b` | letters | 99.0 / 1.0 / 0.0 | ~b x1 | 66.5 / 31.0 / 2.0 |
| `c` | letters | 100.0 / 0.0 / 0.0 | - | 97.5 / 2.5 / 0.0 |
| `d` | letters | 100.0 / 0.0 / 0.0 | - | 75.0 / 25.0 / 0.0 |
| `e` | letters | 98.0 / 2.0 / 0.0 | ~c x2 | 74.5 / 18.5 / 7.0 |
| `f` | letters | 99.5 / 0.5 / 0.0 | ~s x1 | 89.0 / 10.0 / 0.0 |
| `g` | letters | 98.0 / 1.5 / 0.5 | ~g x3 | 81.5 / 13.0 / 5.5 |
| `h` | letters | 99.0 / 0.5 / 0.0 | (split) x1 | 85.0 / 13.5 / 1.0 |
| `i` | letters | 99.0 / 1.0 / 0.0 | ~i x2 | 58.0 / 27.0 / 11.5 |
| `j` | letters | 100.0 / 0.0 / 0.0 | - | 91.5 / 7.0 / 0.0 |
| `k` | letters | 99.5 / 0.5 / 0.0 | ~k x1 | 75.5 / 22.5 / 1.5 |
| `l` | letters | 100.0 / 0.0 / 0.0 | - | 95.0 / 3.5 / 0.5 |
| `m` | letters | 99.5 / 0.5 / 0.0 | ~m x1 | 73.0 / 25.0 / 1.5 |
| `n` | letters | 100.0 / 0.0 / 0.0 | - | 81.5 / 18.5 / 0.0 |
| `o` | letters | 100.0 / 0.0 / 0.0 | - | 70.0 / 27.5 / 2.0 |
| `p` | letters | 99.0 / 1.0 / 0.0 | ~p x2 | 76.5 / 21.0 / 0.0 |
| `q` | letters | 91.5 / 5.5 / 3.0 | ~q x9 | 64.5 / 28.5 / 6.5 |
| `r` | letters | 98.0 / 2.0 / 0.0 | ~r x4 | 67.0 / 31.5 / 0.5 |
| `s` | letters | 100.0 / 0.0 / 0.0 | - | 92.5 / 6.5 / 0.0 |
| `t` | letters | 100.0 / 0.0 / 0.0 | - | 93.5 / 6.0 / 0.0 |
| `u` | letters | 99.5 / 0.5 / 0.0 | ~u x1 | 87.5 / 12.0 / 0.0 |
| `v` | letters | 99.0 / 0.5 / 0.0 | (split) x1 | 82.5 / 16.0 / 1.0 |
| `w` | letters | 99.5 / 0.5 / 0.0 | ~w x1 | 71.5 / 27.0 / 1.0 |
| `y` | letters | 99.0 / 1.0 / 0.0 | ~y x2 | 62.5 / 37.5 / 0.0 |
| `z` | letters | 100.0 / 0.0 / 0.0 | - | 91.0 / 6.5 / 2.5 |
| `0` | digits | 100.0 / 0.0 / 0.0 | - | 97.5 / 2.5 / 0.0 |
| `1` | digits | 99.5 / 0.5 / 0.0 | ~1 x1 | 59.5 / 32.0 / 4.0 |
| `2` | digits | 100.0 / 0.0 / 0.0 | - | 90.0 / 8.0 / 2.0 |
| `3` | digits | 99.5 / 0.5 / 0.0 | ~3 x1 | 85.0 / 11.5 / 2.5 |
| `4` | digits | 100.0 / 0.0 / 0.0 | - | 89.0 / 9.5 / 0.0 |
| `5` | digits | 100.0 / 0.0 / 0.0 | - | 85.5 / 14.0 / 0.5 |
| `6` | digits | 100.0 / 0.0 / 0.0 | - | 89.0 / 10.0 / 0.0 |
| `7` | digits | 100.0 / 0.0 / 0.0 | - | 94.0 / 6.0 / 0.0 |
| `8` | digits | 99.0 / 1.0 / 0.0 | ~0 x1 | 74.5 / 25.0 / 0.0 |
| `9` | digits | 100.0 / 0.0 / 0.0 | - | 73.0 / 27.0 / 0.0 |
| `space` | any | 99.5 / 0.5 / 0.0 | ~no-candidate x1 | 75.5 / 22.5 / 0.0 |
| `enter` | any | 96.0 / 3.5 / 0.0 | ~enter x6 | 72.0 / 19.5 / 6.5 |
| `shift` | any | 99.5 / 0.5 / 0.0 | ~n x1 | 79.0 / 18.5 / 0.0 |
| `bksp` | any | 99.5 / 0.0 / 0.0 | (split) x1 | 77.5 / 21.0 / 0.0 |
| `tap` | any | 100.0 / 0.0 / 0.0 | - | 100.0 / 0.0 / 0.0 |
| `ext` | any | 98.0 / 2.0 / 0.0 | ~t x1 | 71.5 / 19.0 / 6.0 |
| `question` | punct | 99.0 / 0.5 / 0.5 | rparen x1 | 76.0 / 19.5 / 4.0 |
| `period` | punct | 100.0 / 0.0 / 0.0 | - | 100.0 / 0.0 / 0.0 |
| `comma` | punct | 98.0 / 1.5 / 0.5 | ~bksp x1 | 72.5 / 21.0 / 5.0 |
| `apos` | punct | 99.5 / 0.5 / 0.0 | ~apos x1 | 63.0 / 18.0 / 16.5 |
| `backtick` | punct | 100.0 / 0.0 / 0.0 | - | 85.5 / 12.0 / 0.5 |
| `hyphen` | punct | 100.0 / 0.0 / 0.0 | - | 63.0 / 33.0 / 0.0 |
| `underscore` | punct | 100.0 / 0.0 / 0.0 | - | 90.5 / 7.0 / 0.5 |
| `dquote` | punct | 100.0 / 0.0 / 0.0 | - | 84.0 / 15.5 / 0.5 |
| `colon` | punct | 100.0 / 0.0 / 0.0 | - | 88.0 / 11.5 / 0.0 |
| `lparen` | punct | 100.0 / 0.0 / 0.0 | - | 91.0 / 7.0 / 0.0 |
| `rparen` | punct | 100.0 / 0.0 / 0.0 | - | 95.5 / 3.5 / 1.0 |
| `lbrace` | punct | 99.5 / 0.5 / 0.0 | ~lbrace x1 | 78.5 / 19.0 / 2.0 |
| `rbrace` | punct | 99.5 / 0.5 / 0.0 | ~rbrace x1 | 78.5 / 19.0 / 2.5 |
| `lbracket` | punct | 100.0 / 0.0 / 0.0 | - | 85.0 / 14.5 / 0.5 |
| `rbracket` | punct | 100.0 / 0.0 / 0.0 | - | 86.5 / 12.0 / 0.5 |
| `less` | punct | 100.0 / 0.0 / 0.0 | - | 89.5 / 8.5 / 1.0 |
| `greater` | punct | 100.0 / 0.0 / 0.0 | - | 91.0 / 8.0 / 0.0 |
| `semicolon` | punct | 100.0 / 0.0 / 0.0 | - | 92.5 / 6.5 / 1.0 |
| `at` | punct | 100.0 / 0.0 / 0.0 | - | 96.5 / 2.0 / 0.0 |
| `hash` | punct | 100.0 / 0.0 / 0.0 | - | 80.0 / 20.0 / 0.0 |
| `dollar` | punct | 100.0 / 0.0 / 0.0 | - | 94.0 / 5.5 / 0.0 |
| `percent` | punct | 100.0 / 0.0 / 0.0 | - | 71.5 / 28.0 / 0.0 |
| `caret` | punct | 100.0 / 0.0 / 0.0 | - | 94.5 / 5.0 / 0.0 |
| `amp` | punct | 100.0 / 0.0 / 0.0 | - | 75.0 / 24.5 / 0.5 |
| `star` | punct | 100.0 / 0.0 / 0.0 | - | 90.0 / 9.0 / 0.0 |
| `excl` | punct | 99.5 / 0.5 / 0.0 | ~excl x1 | 66.0 / 24.5 / 4.0 |
| `tilde` | punct | 100.0 / 0.0 / 0.0 | - | 79.0 / 20.5 / 0.0 |
| `plus` | punct | 100.0 / 0.0 / 0.0 | - | 86.0 / 13.0 / 0.0 |
| `equals` | punct | 100.0 / 0.0 / 0.0 | - | 92.5 / 7.5 / 0.0 |
| `backslash` | punct | 99.5 / 0.5 / 0.0 | ~backslash x1 | 65.0 / 23.0 / 9.5 |
| `bar` | punct | 100.0 / 0.0 / 0.0 | - | 82.5 / 15.0 / 1.5 |
| `slash` | punct | 99.0 / 1.0 / 0.0 | ~slash x2 | 66.5 / 20.5 / 7.5 |
| `tab` | punct | 100.0 / 0.0 / 0.0 | - | 91.0 / 8.0 / 0.5 |
| `x` | ext | 99.5 / 0.5 / 0.0 | ~x x1 | 78.0 / 16.5 / 4.5 |
| `bullet` | ext | 100.0 / 0.0 / 0.0 | - | 100.0 / 0.0 / 0.0 |
| `lsquo` | ext | 100.0 / 0.0 / 0.0 | - | 90.5 / 5.0 / 3.5 |
| `rsquo` | ext | 100.0 / 0.0 / 0.0 | - | 90.0 / 7.5 / 1.0 |
| `ldquo` | ext | 100.0 / 0.0 / 0.0 | - | 85.0 / 14.0 / 0.0 |
| `rdquo` | ext | 100.0 / 0.0 / 0.0 | - | 85.0 / 14.5 / 0.5 |
| `degree` | ext | 100.0 / 0.0 / 0.0 | - | 96.5 / 3.5 / 0.0 |
| `ext_plus` | ext | 100.0 / 0.0 / 0.0 | - | 88.5 / 10.5 / 0.0 |
| `ext_minus` | ext | 100.0 / 0.0 / 0.0 | - | 70.5 / 25.5 / 0.0 |
| `pound` | ext | 100.0 / 0.0 / 0.0 | - | 89.5 / 8.0 / 0.0 |
| `iexcl` | ext | 99.5 / 0.0 / 0.0 | (split) x1 | 75.0 / 16.5 / 8.5 |
| `ext_equals` | ext | 100.0 / 0.0 / 0.0 | - | 91.0 / 8.0 / 0.5 |
| `szlig` | ext | 99.5 / 0.5 / 0.0 | ~szlig x1 | 72.5 / 26.5 / 0.0 |
| `iquest` | ext | 100.0 / 0.0 / 0.0 | - | 92.5 / 6.0 / 0.0 |
| `copy` | ext | 100.0 / 0.0 / 0.0 | - | 95.5 / 2.5 / 0.5 |
| `trade` | ext | 100.0 / 0.0 / 0.0 | - | 76.5 / 22.5 / 1.0 |
| `reg` | ext | 99.5 / 0.5 / 0.0 | ~reg x1 | 60.0 / 39.5 / 0.0 |
| `euro` | ext | 99.5 / 0.5 / 0.0 | ~euro x1 | 78.5 / 17.0 / 4.0 |
| `yen` | ext | 100.0 / 0.0 / 0.0 | - | 81.0 / 18.0 / 0.0 |
| `ccedil` * | letters | 100.0 / 0.0 / 0.0 | - | 97.5 / 2.0 / 0.0 |
| `aelig` * | letters | 98.0 / 2.0 / 0.0 | ~aelig x4 | 80.5 / 16.0 / 3.5 |
| `acc_grave` * | letters | 100.0 / 0.0 / 0.0 | - | 91.5 / 7.0 / 0.0 |
| `acc_acute` * | letters | 100.0 / 0.0 / 0.0 | - | 89.0 / 9.0 / 0.0 |
| `acc_circ` * | letters | 100.0 / 0.0 / 0.0 | - | 92.5 / 7.5 / 0.0 |
| `acc_tilde` * | letters | 100.0 / 0.0 / 0.0 | - | 81.5 / 16.5 / 0.0 |
| `acc_uml` * | letters | 99.5 / 0.5 / 0.0 | ~acc_uml x1 | 67.5 / 31.5 / 0.0 |
| `acc_ring` * | letters | 100.0 / 0.0 / 0.0 | - | 91.5 / 8.0 / 0.5 |
| `divide` * | ext | 100.0 / 0.0 / 0.0 | - | 68.5 / 30.5 / 0.0 |
| `oslash` * | ext | 100.0 / 0.0 / 0.0 | - | 95.0 / 4.5 / 0.0 |
| `section` * | ext | 100.0 / 0.0 / 0.0 | - | 87.0 / 12.0 / 0.5 |
| `micro` * | ext | 99.5 / 0.5 / 0.0 | ~micro x1 | 71.0 / 27.5 / 0.0 |
| `integral` * | ext | 100.0 / 0.0 / 0.0 | - | 91.5 / 8.5 / 0.0 |
| `cent` * | ext | 100.0 / 0.0 / 0.0 | - | 94.5 / 4.5 / 0.0 |

### 13.5 Confusable pairs under the real alphabet

| Pair | Why | Synthetic evidence (M) |
|---|---|---|
| q / o | the chart's q is an o plus a point and a tail a third of the width; nothing else differs | default q 91.5 % (5.5 % rejected, 3 % read as o); hard q 64.5 %, 10 read as o |
| e / c, g / c, `€` / `©` | e and g are a c with an inner detail (the middle point, the bar) | hard: e -> c 12, g -> c 11, `€` -> `©` 5 |
| line glyphs 45 degrees apart: i / enter / prefix, 1 / enter / prefix, `'` / `,` / `\`, `!` / `/`, x / backspace (ext set) | a slanted line sits between two directions | default 96-99.5 %; hard 58-78 % with 4-16 % wrong (apos -> comma 10, backslash -> apos 10, slash -> excl 9, x -> bksp 9) |
| `¡` / `¿`, `‘` / `¿` | a line up against a short left stroke then up | hard: iexcl -> iquest 10, lsquo -> iquest 5 |
| `?` / `)`, `'` / `(` `)` | the first half of `?` is a `)`; a curved `'` is a half circle | hard: question -> rparen 8, apos -> rparen 9 |
| b / d / p / r (retraced stem) | the stem's retrace dominates; they differ in the bumps only | default 98-100 %; hard 66.5-76.5 %, rejected rather than confused |
| same shape, different set (by design, never confused) | o = 0 = `@`; a = `^`; s = `$` = `∫`; z = `=` = ext `=`; e = `{` = `€`; c = `©`; m = `™`; r = `®`; n = `"` = `“`; 3 = `}`; 8 = `&`; k = `+` (flatter) = ext `+`; l = £ (4 is a flatter l); `%` = `÷` = diaeresis; `` ` `` = grave; `~` = tilde; `ø` = ring; `¢` = `ç` | the mode or the box decides; within each set every pair above is a different shape |

### 13.6 Ambiguities of the chart (not guessed silently)

1. **X and `×`:** the same two strokes; X wins, `×` has no stroke (13.3).
2. **Punctuation and extended prefixes:** one glyph or locked until backspace? Decided by the owner on 2026-10-25:
   locked until backspace or a box change (13.10).
3. **How accents are written:** decided by the owner on 2026-10-25: dead keys written before the letter, in the
   letters box, one letter each (13.3, 13.10).
4. **Labels in the extended panel:** the single and double quotes could be the low-9 forms (`‚ „`) instead of `’ ”`;
   the minus could be `−` (U+2212) or a plain `-` instead of `–`; the tap could be a period instead of `•`; `Ø` is
   shown in capitals like the letters, so it types `ø` and shift gives `Ø`.
5. **Diagonal angles:** 50-53 degrees in the chart, 45 in the templates (13.1, 13.4); the owner kept 45 degrees on
   2026-10-25.
6. **Caps lock off:** the chart shows two up strokes for caps lock but not how to leave it; a third up stroke does.
7. **Which end is the start** where both ends look alike: `ß` (both ends at the bottom-left; we start at the stem's
   foot), and the retraced pairs `` ` `` `|` `:` `;` `_` and the grave and acute accents (we start at the plain end,
   the arrow being the end).
8. **Loop directions** read from the smoothness of the line, not from arrows: k and ext / punct `+` (clockwise loop on
   the left), `*` (counter-clockwise on the right), `%`, `÷`, diaeresis (two clockwise loops), `¥` and the y's
   bottom loop (clockwise).
9. **Not separable at stylus-noise level within one set:** none are identical, but q / o and the line pairs of 13.5
   come close; real strokes will tell.

### 13.7 What the owner must check on a real stylus (the ten traces we are least sure of)

1. **y:** down, round, up to top-right, back down the same line below the base, a small clockwise loop, out
   up-right. Is the descender a retrace of the right arm, and is the loop at its bottom?
2. **q:** an o, then at the top a point and a tail to the right. Is it a point (cusp) or a tiny loop, and is the tail
   as short as drawn (a third of the width)?
3. **g:** a c, then up into the middle to a point and a bar out to the right. In and back out, or one bar inward?
4. **k:** down-left diagonal, a loop on the left, down-right diagonal. Is the loop clockwise?
5. **b, d, p, r:** stem down, then back up the same stem before the bumps (the chart has no second way). Is the stem
   ever written upwards from the bottom?
6. **h:** stem down, back up a third, then the arch. Up the stem or a fresh start?
7. **v:** the short flick to the right at the top, the only thing that tells v from u.
8. **ß:** starts at the foot of the stem, up, two bumps, ends at the bottom-left where it started.
9. **`` ` `` (and the grave accent), `|`, `:`, `;`, `_`:** lines out and back; which end is the start.
10. **`*` and `+`:** which way the loop turns, and `+` being a flatter k.

Also worth a look: the 45-degree diagonals (enter, the prefix, `\`, `/`) on a real stylus (kept by the owner).

**Capture (section 10) with the new table:** the letters session now writes X as two samples, `ext` then `x`
(labels `"... w ext x y z"`); a punctuation session would use the ids of 13.2 (`question period comma apos backtick
hyphen ...`), an extended one `bullet lsquo rsquo ...`; the capture and replay tools put each in its prefix.

### 13.8 Contact sheets of the traced templates (outside the repository)

Rendered from the shipped files with arrows (green start, red end, blue direction ticks; grey = optional section),
one sheet per chart panel in the chart's order, for comparison with the chart:

- `C:\Users\osrde\AppData\Local\Temp\claude\C--CLAUDE-zero7\5aaea98f-a498-4726-8890-7bd33adbcf64\scratchpad\strokes-sheet-1.png` (letters, digits, gestures; X as prefix + dashed x, caps lock as two shifts)
- `C:\Users\osrde\AppData\Local\Temp\claude\C--CLAUDE-zero7\5aaea98f-a498-4726-8890-7bd33adbcf64\scratchpad\strokes-sheet-2.png` (punctuation)
- `C:\Users\osrde\AppData\Local\Temp\claude\C--CLAUDE-zero7\5aaea98f-a498-4726-8890-7bd33adbcf64\scratchpad\strokes-sheet-3.png` (extended; `×` shown as having no stroke)
- `C:\Users\osrde\AppData\Local\Temp\claude\C--CLAUDE-zero7\5aaea98f-a498-4726-8890-7bd33adbcf64\scratchpad\strokes-sheet-4.png` (accents)

### 13.9 Tests and checks (M)

`make check` (build with `-Werror`, unit tests, the same under ASan + UBSan, `make eval` with the gate, the
end-to-end capture test with the plain and the sanitised tools, the name check): all pass, 0 warnings. 47 tests,
1400 checks; the end-to-end test now replays 1860 strokes of every set (99.35 / 0.54 / 0.11 %, identical through the
three paths). `make names` passes: the trademark word appears only in this report.

### 13.10 Owner decisions of 2026-10-25 and the mode change

Decisions taken by the owner on 2026-10-25:

1. **Punctuation and extended modes stay on** as on the real handheld: entered by a tap / the line down-right, they
   last until the backspace gesture (leaves the mode, deletes nothing) or the box / zone changes, instead of one
   glyph (13.3, 13.6 item 2).
2. **Diagonals stay at 45 degrees** in the templates (13.1, 13.4).
3. **Accents are dead keys** written before the letter, in the letters box (13.3).

Details decided with the change (judgement calls, the owner can change them):

| Case | Behaviour | Why |
|---|---|---|
| a glyph or key of the mode's set (`.` `/` `-` tab, `©` `•` ...) | typed; the mode stays (`r.mode` keeps `PUNCT` / `EXT`) | the decision |
| a rejected stroke | the mode stays | as before |
| the backspace gesture | leaves the mode, deletes nothing (`STROKES_MODE` result, key none); a second one is a real backspace | the chart's "backspace to exit" |
| space, enter, shift inside the mode | not candidates there, as before: their lines are glyphs of the set (`-`, `!`, `,` ...) | unchanged; leaving the mode is backspace only |
| a stroke starting in the other box | leaves the mode at the press (before its result; `strokes_get_mode` shows it at once), then reads the stroke in that box's set: a tap there enters its own punctuation mode | "box changes"; the mode belongs to the box it was entered in |
| zone opens / closes | the UI calls `strokes_set_mode(e, 0)` (as before); a mode set there belongs to the box of the next stroke | no new call needed |
| one-shot shift | used by the first character in the mode (`©` and punctuation ignore case) | as before |
| X (the extended prefix, then `x`) | `x` is a plain letter: it ends the extended mode | otherwise every X would need a backspace after it; the only a-z character of the punct and ext sets |
| dead accent | still one letter: applied to the next letter, then cleared; backspace cancels it; a box change leaves it alone (a digit drops it anyway) | the decision |

API: no signature changed and nothing was added. `strokes_get_mode()` already reads the current flags without
polling or a timeout; its header comment now says so, and that a press in the other box clears `PUNCT` / `EXT` at
once. The pending accent's kind is still only in `strokes_result.accent` (no getter; the flag is in the mode).
Version string unchanged (0.2.0, not released).

Tests (M): `make check` passes with 0 warnings, ASan + UBSan included; 48 tests, 1424 checks (13.9: 47, 1400). The
existing mode tests now expect the mode to stay; a new engine test covers several glyphs in a row in both modes,
backspace leaving without deleting (then a real backspace), a rejected stroke keeping the mode, the tab key keeping
it, the box change (at the press, by a digit, by a letter, a tap in the other box) and the one-letter accent.
Accuracy is unchanged (the tools set the mode per sample): `make eval` 99.55 / 0.37 / 0.08 % (gate pass), unit-test
gate 99.48 / 0.48 / 0.03 %, scribbles 98.5 % rejected, end-to-end 1860 strokes 99.35 / 0.54 / 0.11 % through the
three paths. `make aarch64` builds.
