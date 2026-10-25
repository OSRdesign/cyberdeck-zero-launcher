# Strokes: the single-stroke handwriting recogniser of the PDA profile

Phase **S1** of the PDA profile ([report 038](../../docs/dev/reports/038-pda-profile-study.md), section 5; hand-off
[report 039](../../docs/dev/reports/039-strokes-s1.md)). A C99 library with no LVGL dependency that turns the raw
touch frames of one contact into characters, keys and mode changes, plus the PC tools that test it: a synthetic stroke
generator and evaluation command, a capture tool for real stylus strokes, a corpus replay harness and a benchmark.

Nothing here is linked into the launcher yet: S3 moves the library into `ext_components/cp0_lvgl` and draws the
writing zone (section "How S3 uses it"). Real stylus accuracy is **not measured** yet; every number below comes from
synthetic strokes.

| | |
|---|---|
| Library | `include/strokes.h`, `src/strokes_{geom,table,match,capture,engine}.c`, `src/strokes_internal.h` (about 30 KB of aarch64 code, no allocation after start-up, no globals) |
| Glyph table | `share/glyphs.txt` (shipped, read-only), `share/glyphs-extra.txt` (optional section: rarer extended characters, accents), `share/overlay-example.txt` (user overlay format); `share/` mirrors `/usr/share/APPLaunch/strokes/` (and the repo's `.gitignore` ignores every `data/` directory) |
| Tools | `strokes-eval` (synthetic accuracy), `strokes-replay` (corpus replay), `strokes-capture` (stylus capture), `strokes-bench` (time per stroke) |
| Tests | `tests/` (unit tests, ASan + UBSan run, end-to-end capture test, name check) |

## Rules that apply to this directory

- **Licence.** MIT, our own code. Ideas read, never copied: the resample-and-compare scheme published with the
  $1 recogniser (New BSD) and the closed-form rotation of Protractor (paper; its patents are listed as expired). No
  GPL code (xstroke) and no LGPL code (Tomoe) is read or linked; EdgeWrite's corner-sequence method (patented) is not
  used. No Palm or ACCESS code, file, image or data: the strokes in `share/glyphs.txt` and `share/glyphs-extra.txt`
  are those of the classic Palm one-stroke alphabet, traced by hand from its public reference chart (report 039 cites
  it; the chart itself is not in the repository). The templates are our own drawings of those strokes, in our own
  format.
- **Trademark.** The name of the Palm-era alphabet is a trademark. It must not appear in code, file names,
  identifiers, UI text, data or tests (report 038 section 5; `docs/dev` may cite it as the reports do).
  `make names` (`tests/check_names.sh`) fails when it does. The input method is called **Strokes**.

## Build and test (Linux or WSL)

```sh
cd tools/strokes
make            # library + tools + tests in build/host (-O2)
make test       # unit tests
make asan       # the tests under AddressSanitizer + UndefinedBehaviorSanitizer (build/asan)
make eval       # per-glyph synthetic accuracy with the regression gate (>= 98 % correct, <= 2 % rejected)
make bench      # time per recognition on this machine
make check      # all of the above with -Werror, the end-to-end capture test (plain and sanitised) and the name check
make aarch64    # static aarch64 tools for the boards (build/aarch64; aarch64-linux-gnu-gcc)
```

Plain C99 for the library (`-std=c99 -Wpedantic`); the tools add POSIX and Linux headers. Warnings: `-Wall -Wextra
-Wshadow -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Wcast-qual -Wvla`, zero of them (`make check`
builds with `-Werror`). The tests and tools run from `tools/strokes` (they read `share/glyphs.txt`; `STROKES_TABLE`
overrides).

## Design

```
raw frames of contact 0 (logical px, us)
  -> engine: press / move / lift; a lift then a press within heal_ms, close enough, continues the stroke (drop-out)
  -> lift + heal_ms: clean-up
       tap (moved < 1.5 mm, < 250 ms) | dwell (still, longer: rejected) | short (< 3 mm: rejected)
       decimate (0.35 mm), resample to 64, trim landing / lift hooks (the turn within 8 % of the path and 2.5 mm
       of an end), smooth [1 2 1]
  -> features of the path resampled to 32 points: start / end in a 3x3 grid of the square box around the stroke,
     straightness and direction (lines), closedness, net turning, corners, chain code
  -> matcher, every variant of every glyph of the set (letters / digits + the "any" gestures; in the punctuation or
     extended mode the punct or ext set + backspace):
       gates first; then the score = cosine of the centred, length-normalised 32-point vectors (points in order, so
       the direction counts), best over a slant of up to +-15 degrees (closed form) and over three aspect variants
       (the path stretched horizontally by 1/1.2, 1, 1.2 before resampling)
  -> accept the best glyph only if score >= 0.80 and it beats the runner-up by >= 0.04; otherwise reject
  -> modes: one-shot shift, caps lock, punctuation and extended modes, dead accents; letters upper-cased
  -> one strokes_result per stroke: char / key / mode / reject, confidence, margin, up to 4 candidates
```

- **Aspect awareness.** Scaling is uniform (the vector's length), never per axis, so `i` (a line down), space (a line
  right), enter (a diagonal) and `l` (the L shape) stay apart; lines also have hard direction gates. The aspect
  tolerance forgives a 20 % wider or narrower glyph, not a line turned into a shape.
- **Orientation.** No rotation normalisation: a glyph drawn upside down or in reverse does not match. Only a slant up to
  15 degrees is forgiven.
- **Rejection.** The score threshold, the margin rule and the gates make the recogniser refuse a stroke instead of
  guessing; S3 shows a rejected stroke (red ink flash) and types nothing.
- **Determinism.** No global state, no clock, no randomness: the same frames give the same results. The engine buffers
  at most 2048 raw points per stroke (beyond, it keeps every second point).

## API (`include/strokes.h`)

```c
strokes_table *t = strokes_table_new();
strokes_error err;
if (strokes_table_add_file(t, "/usr/share/APPLaunch/strokes/glyphs.txt", &err))      /* shipped table */
    log_line("%s:%d: %s", err.source, err.line, err.msg);
if (with_extras && strokes_table_add_file(t, "/usr/share/APPLaunch/strokes/glyphs-extra.txt", &err))
    log_line("%s:%d: %s", err.source, err.line, err.msg); /* optional section: rarer ext characters, accents */
if (strokes_table_add_file(t, user_overlay_path, &err))   /* atomic: a bad overlay leaves the table unchanged */
    log_line("overlay ignored: %s:%d: %s", err.source, err.line, err.msg);

strokes_config cfg;
strokes_config_default(&cfg);
cfg.ppmm = density;                         /* px per mm of the coordinates you feed */
strokes_engine *e = strokes_engine_new(t, &cfg);

/* raw frame consumer, contact 0 only */
strokes_down(e, STROKES_BOX_LETTERS /* or _DIGITS: the box of the press */, x, y, t_us);
strokes_move(e, x, y, t_us);
strokes_up(e, t_us);

/* from a timer (every display tick) and after each frame */
strokes_result r;
while (strokes_poll(e, now_us, &r)) {
    switch (r.kind) {
    case STROKES_CHAR:   /* type r.utf8 (case and a pending accent applied; may be non-ASCII: ©, é, ...) */ break;
    case STROKES_KEY:    /* r.key: strokes_key_linux_code(r.key) is KEY_SPACE / KEY_BACKSPACE / KEY_ENTER / KEY_TAB */ break;
    case STROKES_MODE:   /* show r.mode: _SHIFT, _CAPS, _PUNCT, _EXT, _ACCENT (r.accent: which one) */ break;
    case STROKES_REJECT: /* flash the ink; r.reject says why, r.cand[] what it resembled */ break;
    }
}
```

Other calls: `strokes_flush` (finalise at once: zone closing, end of a replay), `strokes_cancel` (drop the stroke in
progress), `strokes_busy`, `strokes_get_mode` (the mode flags now, no polling or timeout: for the mode line) /
`strokes_set_mode`, `strokes_classify` (one complete stroke, no
healing, no modes: evaluation and training preview), `strokes_template_line` (an overlay line from a user's stroke:
S6 training), `strokes_table_glyph_info`, and the `*_name` helpers (`strokes_accent_name` too). The engine and the
table are not thread-safe; use them from one thread.

Changes from 0.1.0 (S1 first cut): `STROKES_SET_EXT`, `STROKES_MODE_EXT`, `STROKES_MODE_ACCENT`, `enum
strokes_accent`, `strokes_result.accent`, `strokes_accent_name()`; the "any" gestures are candidates in the letters
and digits sets only (after a prefix only backspace, which leaves the prefix without deleting); a rejection after a
prefix keeps it; upper-casing covers the Latin-1 letters; the punctuation and extended modes stay on until
backspace or a box change (owner decision 2026-10-25, see Modes). Every 0.1.0 call keeps its signature.

## Glyph table

Plain text, one line per variant, fields separated by spaces or tabs, `#` at the start of a line for comments:

```
# id    set      out            gates                    template
a       letters  a              start=bl,bc end=br,bc    0,100 50,0 100,100
space   any      KEY_SPACE                               0,50 100,50
tap     any      MODE_PUNCT                              tap
```

| Field | Values |
|---|---|
| id | the glyph's name, up to 15 bytes, unique per set (`replace` and `remove` are reserved; the shipped ids are unique across sets, so a corpus label names one glyph) |
| set | `letters`, `digits`, `punct` (after the punctuation prefix), `ext` (after the extended prefix), `any` (gestures of the letters and digits boxes; after a prefix only the backspace gesture counts) |
| out | a character (1 to 7 UTF-8 bytes, e.g. `a`, `th`), `U+XXXX`, `KEY_SPACE`, `KEY_BACKSPACE`, `KEY_ENTER`, `KEY_TAB`, `MODE_SHIFT`, `MODE_CAPS`, `MODE_PUNCT`, `MODE_EXT`, `ACCENT_GRAVE`, `ACCENT_ACUTE`, `ACCENT_CIRCUMFLEX`, `ACCENT_TILDE`, `ACCENT_DIAERESIS`, `ACCENT_RING` |
| gates | optional `key=value` tokens before the template (next table) |
| template | two or more `x,y` points (up to 256; 0..100 by convention, y down; only the shape counts, not the size or position; decimals with a `.` whatever the locale), or the word `tap` |

Several lines with the same id and set are **variants**: the glyph's score is its best variant's. Gates belong to each
variant. A gate that is not written is derived from the template: the start and end must lie within 0.40 (in units of
the stroke's longer side) of the template's own start and end; a straight template (straightness >= 0.97) makes a line
glyph, any other template refuses near-perfect lines; a template that ends where it starts is closed, one that ends
far away is open. The parser refuses a template that fails its own gates.

| Gate | Meaning |
|---|---|
| `start=` / `end=` | cells of the 3x3 grid over the stroke's square box: `tl tc tr ml mc mr bl bc br`, rows `t m b`, columns `l c r`, `*`; comma-separated; each cell widened by 0.10 |
| `line=` | `right left up down upright upleft downright downleft` or a numpad digit: a line in that direction (straightness >= 0.90, within 35 degrees); `no`: not a near-perfect line (<= 0.975); `any` |
| `closed=` | `yes` (end within 0.32 of the start), `no` (at least 0.15 away), `any` |
| `turn=` | `cw` or `ccw`, optionally `:DEG` (default 90): net turning of at least DEG degrees in that sense |
| `corners=` | `N` or `N-M`: sharp turns (more than 65 degrees over three of the 32 points) |
| `dirs=` | numpad digits (`6` right, `3` down-right, `2` down, `1` down-left, `4` left, `7` up-left, `8` up, `9` up-right): each must appear in this order in the stroke's chain code, within 45 degrees |

### The shipped set (93 glyphs; optional section 14 more)

The strokes of the classic one-stroke alphabet as its reference chart draws them (report 039 section 13 cites the
chart and shows the traced templates): start point, direction, loops, corners and retraces included, one template per
glyph because the chart shows one way to write each. Letters type lower case; a shift gesture first makes the next
letter upper case. Contact sheets of the templates: report 039 section 13.

| Glyph | Stroke |
|---|---|
| a | up from bottom-left to a rounded top, down to bottom-right (no bar) |
| b | stem down, back up the stem, two bumps to the right, ending bottom-left going left |
| c | from top-right, counter-clockwise over the top and round to bottom-right |
| d | stem down, back up the stem, one bump to the right, ending bottom-left going left |
| e | from top-right, a counter-clockwise bowl to a point in the middle, a second bowl ending bottom-right |
| f | left along the top from top-right, then down the left side |
| g | a c, then up into the middle to a point, and a short bar out to the right |
| h | stem down, back up a third, arch to the right and down |
| i | straight down |
| j | down the right side, round the corner, left along the bottom |
| k | from top-right diagonally down-left, a clockwise loop on the left, diagonally on to bottom-right |
| l | down, then right |
| m | up from bottom-left, arch, down to a point two thirds down, up, arch, down to bottom-right |
| n | up from bottom-left, arch, down, round the bottom, up to top-right |
| o | from the top, counter-clockwise round, ending top-right |
| p | stem down, back up the stem, a bump ending half way down at the stem |
| q | an o, then at the top a point and a short tail to the right |
| r | a p, then a leg to bottom-right |
| s | from top-right over the top, across, round the bottom, ending bottom-left |
| t | right along the top, then down the right side |
| u | down, round the bottom, up to top-right |
| v | down to a point, up, a short flick to the right at the top |
| w | down, round, up to a point a third down, down, round, up to top-right |
| x | **two strokes**: the extended prefix (a line down-right), then a line down-left (glyph `x` of the ext set) |
| y | down, round to the right, up to top-right, back down the same line below the base, a clockwise loop, out up-right |
| z | right, diagonal down-left, right |
| 0 ... 9 | digits box: 0 as o; 1 down; 2 clockwise over the top, diagonal, right; 3 two bumps meeting at a point; 4 down, then right (a flat l); 5 left, down, a clockwise bump; 6 from top-right down and round into a loop; 7 right, diagonal; 8 an s from top-right, a clockwise loop, diagonally back up to the start; 9 left along the top, a counter-clockwise loop back up, straight down |
| space, bksp | a line right, a line left |
| enter | a line down-left |
| shift | a line up: one-shot shift; twice: caps lock; once more: off |
| tap | the punctuation prefix |
| ext | a line down-right: the extended prefix |

Diagonal lines are 45 degrees in the templates; the chart draws them at 50-53 degrees because its cells are taller
than wide (report 039 section 13.4 measures both).

**Punctuation** (after a tap; 33): the period is a tap; lines: `-` right, `'` down, `,` down-left, `\` down-right,
`/` up-right, `!` up; lines out and back: `` ` `` up-left and back down, `|` up and back down, `:` down and back up,
`;` down-left and back up, `_` right and back left; curves: `?` a hook over the top and down, `(` and `)` half
circles from the top, `[` an e written backwards (from the bottom), `]` a 3 written backwards, `<` and `>` from the
bottom to a point and back up, `#` down, up, down (from top-left), `~` the same from top-right, `%` down-right, two
clockwise loops, out up-right, `*` down-right, a counter-clockwise loop on the right, out down-left, `tab` up then
right; letter shapes: `"` n, `{` e, `}` 3, `@` o, `$` s, `^` a, `&` 8, `=` z, `+` a flat k.

**Extended set** (after the down-right line; 19, plus 6 in the optional section): `x` (second stroke of X), the
bullet a tap, `‘` up then right, `’` up then left, `“` an n, `”` an n mirrored (from bottom-right), `°` a clockwise
circle from the bottom, `+` a flat k, `–` (en dash) a line right, `£` an l, `¡` a line up, `=` a z, `ß` stem up from
bottom-left, two bumps back to the start, `¿` left then up, `©` c, `™` m, `®` r, `€` e, `¥` down-right, a clockwise
loop, out up-right. Optional: `÷` as %, `ø` a counter-clockwise circle from the bottom, `§` an s backwards, `µ` an m
backwards, `∫` an s, `¢` a c backwards. The chart's `×` has the strokes of X; X wins, `×` has no stroke.

**Accents** (optional section, letters box): `ç` a c backwards and `æ` an e backwards type their letter; the dead
accents wait for the next letter (a e i o u y n; other characters and keys drop them): grave up-left and back (as
`` ` ``), acute up-right and back, circumflex an a backwards, tilde an n backwards (as `~`), diaeresis two loops (as
`%`), ring a counter-clockwise circle from the bottom (as `ø`).

### User overlay

The same format, read after the shipped table (S3: `~/.config/applaunch/strokes-user.txt`; example:
`share/overlay-example.txt`). A glyph line adds a variant (its output must equal the existing one). Two directives:

- `replace <id> <set>`: drops the variants of that glyph that came from earlier files; the lines that follow in this
  file define it again (with any output). This is what the S6 training screen writes, with lines from
  `strokes_template_line()`.
- `remove <id> <set>`: drops the glyph; a stroke like it is then rejected.

A file is applied as a whole or not at all: one bad line refuses it, and the error names the file and line.
`strokes-eval --overlay FILE` and `strokes-replay --overlay FILE` show what an overlay does to its neighbours (a
rounder `v` makes some `u` ambiguous, for example).

## Modes

| Input | Effect |
|---|---|
| shift gesture | off -> one-shot shift -> caps lock -> off |
| a character | upper-cased (a-z and the Latin-1 letters) when shift or caps lock is on; consumes the one-shot shift (digits and punctuation too) |
| space, backspace, enter, tab | typed; leave shift and caps lock alone |
| tap | punctuation mode: strokes are read in the `punct` set (a tap there: the period) |
| line down-right | extended mode: strokes are read in the `ext` set (a tap there: the bullet) |
| in either mode | stays on across glyphs and keys (the `punct` tab) until the backspace gesture, which leaves it and deletes nothing (a second backspace is a real one); a rejection keeps it; a stroke that starts in the other box leaves it (at the press, so `strokes_get_mode` shows it at once); `strokes_set_mode` sets or clears it (a mode set there belongs to the box of the next stroke); the other gestures' lines are glyphs of the set there (a line right is `-` or `–`); one-shot shift is used by the first character; exception: a plain letter typed through the mode (the `x` of X) ends it |
| dead accent (optional section) | one letter only: waits for the next character: a e i o u y n get the accent (shift and caps apply), other characters and keys drop it; backspace drops it and deletes nothing; a prefix or shift in between keeps it |
| a rejection | changes nothing |

## Tunables (`strokes_config`, `--cfg KEY=VALUE` in the tools)

| Key | Default | Meaning |
|---|---|---|
| `ppmm` | 8.54 | px per mm of the coordinates (the tools take it from the corpus or the generator) |
| `heal_ms` | 50 | a lift then a press within this time ... |
| `heal_mm` | 3.0 | ... within this distance of the lift point continues the stroke ... |
| `heal_speed` | 1 | ... or within the distance the pen covers at its speed before the lift (x 1.25, at most 15 mm more) |
| `tap_max_mm`, `tap_max_ms` | 1.5, 250 | a tap; still and longer is a dwell (rejected) |
| `min_stroke_mm` | 3.0 | shorter strokes are rejected |
| `min_score` | 0.80 | score = 1 - angle / 90 degrees between the stroke's and the template's vectors |
| `min_margin` | 0.04 | the best glyph must beat the runner-up by this |
| `rot_tol` | 15 | slant forgiven, degrees (0 = report 038's no-rotation matcher) |
| `aspect_tol` | 0.2 | aspect change forgiven (0 = none) |
| `smooth`, `hook_trim` | 1, 1 | clean-up steps |

## Synthetic generator and `strokes-eval`

`strokes-eval` writes every glyph `--n` times (default 200) as a simulated stylus would and reports, per glyph and per
set, the share recognised, rejected and wrong, the drop-outs that were not healed ("split"), the most frequent
confusions, and optionally a threshold sweep (`--sweep`) and random scribbles (`--garbage N`). `--gate ACC,REJ`
exits 1 below the gate. The defaults follow report 038:

| Setting (`--gen KEY=VALUE`) | Default | `--hard` |
|---|---|---|
| `ppmm` | 8.54 | 8.54 |
| `size_min`, `size_max` (longer side, mm) | 8, 16 | 6, 20 |
| `scale_min`, `scale_max` (each axis) | 0.8, 1.2 | 0.7, 1.3 |
| `rot` (degrees, +-) | 12 | 20 |
| `shear` (+-) | 0.15 | 0.25 |
| `wobble` (smooth shape change, fraction of size) | 0.04 | 0.07 |
| `round_p`, `round_max` (corner rounding; full reversals are not rounded) | 0.5, 0.12 | 0.8, 0.2 |
| `noise` (Gaussian jitter per point, fraction of size) | 0.01 | 0.02 |
| `rate_min`, `rate_max` (report rate, Hz) | 60, 120 | 60, 120 |
| `speed_min`, `speed_max` (mean pen speed, mm/s; slower at both ends) | 60, 150 | 60, 250 |
| `hook_p`, `hook_min`, `hook_max` (per end) | 0.3, 0.02, 0.06 | 0.5, 0.02, 0.10 |
| `trunc_p`, `trunc_max` (per end) | 0.2, 0.04 | 0.4, 0.08 |
| `drop_p`, `drop_min`, `drop_max` (one drop-out, ms, rounded to whole frames) | 0.1, 10, 40 | 0.3, 10, 48 |
| `tap_min`, `tap_max`, `tap_jitter` (ms, ms, mm) | 40, 180, 0.3 | same |
| `quantise` (whole pixels) | 1 | 1 |

The generator derives its samples from the table's own templates, so its accuracy guards the code against
regressions; it says nothing about real handwriting. `--write FILE` saves the samples as a corpus, `--evtest FILE`
as evtest text (with `FILE.labels`), which is how the end-to-end test feeds the capture tool.

## Corpus format

One sample (one intended stroke) per line; `#` lines are comments (`strokes-capture` writes a header with the device,
density, size, rotation and date):

```
# strokes-corpus 1
a<TAB>letters<TAB>8.54<TAB>0,212,640 16.7,214,628 33.3,219,611 ... ^412.5
i<TAB>letters<TAB>8.54<TAB>0,250,500 10,250,510 20,250,520 ^25 55,251,540 65,251,550 ^70
```

- Fields: the expected glyph id (`?` when unknown), the box the stroke started in (`letters` or `digits`), the
  density in px/mm, then the points.
- A point is `t,x,y`: milliseconds since the sample's first press (up to 3 decimals) and logical pixels (up to 1
  decimal, y down). `^t` is a lift at time t; a point after a lift is a new press (a drop-out, healed or not at
  replay). A missing final lift is added at the last point.
- A label of the `punct` or `ext` set is replayed after its prefix (the shipped ids are unique across the sets). X is
  two samples: `ext`, then `x`.

## Capturing a corpus (`strokes-capture`)

On the reference board (Pi 3A+, Waveshare 4.3" DSI, written in portrait):

```sh
make aarch64                                           # on the PC: static build/aarch64/strokes-capture
scp build/aarch64/strokes-capture share/glyphs.txt share/glyphs-extra.txt <board>:
# on the board, with the launcher stopped so that it does not react to the strokes
systemctl --user stop cyberdeckzero
./strokes-capture --list                               # find the touch screen: /dev/input/eventN ... touch
./strokes-capture --evdev /dev/input/eventN --size 800x480 --rotate 90 --panel-mm 56x94 \
    --labels "a b c d e" --reps 10 --table glyphs.txt --out strokes-abcde.txt
systemctl --user start cyberdeckzero
```

- It prompts `[3/50] write: a` on the terminal (an ssh session works) and, after each stroke, prints the points,
  contacts, duration, size and what the recogniser made of it. The first stroke prints an orientation check: an L
  written down-then-right must read `dirs 26`; if not, change `--rotate` (90 or 270, by the side of the ribbon cable).
- `--size` maps the raw axes onto the panel's pixels in its native orientation (800x480 for the DSI), `--rotate` turns
  them clockwise into the writing orientation; `--panel-mm` is the panel in mm in the writing orientation (ppmm =
  480 / 56 = 8.57). Without them the raw units are written and `--ppmm` must be given.
- Contacts closer than `--group-ms` (150 ms) form one sample, so drop-outs stay inside it with their lift and press
  times: the replay decides whether the engine heals them. Wait a moment between strokes.
- Without `--labels` every sample is `?`. `--order rounds` asks a b c a b c instead of a a a b b b. `--grab` takes the
  device for itself instead of stopping the launcher. Ctrl+C ends a capture early; the file is complete up to there.
- The evtest route works anywhere evtest does: `evtest /dev/input/eventN > dump.txt`, then on the PC
  `strokes-capture --evtest dump.txt --labels-file order.txt --size 800x480 --rotate 90 --panel-mm 56x94 --out c.txt`.
- WSL has no evdev: `--evdev` and `--list` are compile-tested only; the evtest path is tested end to end
  (`make e2e`).

## Replaying a corpus (`strokes-replay`)

```sh
build/host/strokes-replay strokes-abcde.txt --errors --sweep
build/host/strokes-replay strokes-*.txt --cfg heal_ms=70 --overlay my-overlay.txt
```

Each sample goes through the engine frame by frame with its own timestamps and density (drop-out healing, clean-up,
gates, matcher, modes), then the per-glyph report as for `strokes-eval`. `-v` prints every sample with its score,
margin, healed drop-outs, corners, turning, chain code and candidates; `--errors` only the wrong ones; `--sweep` the
accuracy for a grid of `min_score` / `min_margin`, which is how S6 sets the thresholds from real strokes.

## Performance (`strokes-bench`)

On the PC (Intel i5-12450H, gcc 13 -O2): about **11 us per stroke** for the whole path (healing, clean-up, features,
3 aspect variants x the templates of the set (31 for letters and the gestures), modes), 4 us of it clean-up. Estimate
for the Cortex-A53 boards (Pi Zero 2 W at 1.0 GHz, Pi 3A+ at 1.4 GHz): 10-20 times the PC, the factor used by reports
015 and 037, so **0.11-0.21 ms per stroke**, once per stroke on the lift, against the 1 ms budget of report 038. An operation count gives the same order
(about 18 000 multiply-adds for the matcher and about 400 `hypotf`/`atan2f` calls). `build/aarch64/strokes-bench` on
a board prints the real number.

## How S3 uses it

- The library moves to `ext_components/cp0_lvgl` (after report 029's decoupling lands); the shipped table goes to
  `/usr/share/APPLaunch/strokes/glyphs.txt` (and `glyphs-extra.txt`, loaded when a setting asks for the rarer
  extended characters and the accents), the user overlay to `~/.config/applaunch/strokes-user.txt`.
- The zone subscribes to the raw touch frames (`cp0_touch_reader`, contact 0, `CLOCK_MONOTONIC`), not to the 30 Hz LVGL
  pointer, and calls `strokes_down/move/up` with the box of the press (letters box: `STROKES_BOX_LETTERS`, digits box:
  `STROKES_BOX_DIGITS`) and `cfg.ppmm` from the layout service. It calls `strokes_poll` from its timer.
- A `STROKES_CHAR` goes through the on-screen keyboard's US table (`cp0_ui_osk.c`) to `{KEY_*, Shift}` and
  `cp0_keyboard_inject_key()`; a `STROKES_KEY` uses `strokes_key_linux_code()`. The extended set and the accents type
  characters the US table has no key for (`©`, `é`, ...): S3 needs a text path for them (open question). `r.mode`
  and `r.accent` drive the mode line (Shift, Caps, Punct, Ext, the pending accent); Punct and Ext stay lit until
  backspace or a box change, so the line must show them for as long as `strokes_get_mode()` has them. A `STROKES_REJECT` flashes the ink red. `strokes_set_mode(e, 0)` when the zone opens,
  `strokes_cancel` when it closes.
- The ink is drawn from the frames as they arrive; recognition happens heal_ms (50 ms) after the lift.
- S3 adds the trademark word to `tools/dev/check-branding.sh` (report 038) and keeps `make check` of this directory in
  its own checks.

## Limits

- Real stylus accuracy is unknown until the owner's capture (report 039 section 10). The thresholds (heal 50 ms /
  3 mm, tap 1.5 mm / 250 ms, min_score 0.80, margin 0.04) are first guesses from synthetic strokes.
- Weakest in the synthetic test (report 039 section 13.4): q against o (the tail is a third of the width; about
  91 %), then enter, r, g and e (98 %). Under the harsh generator the straight lines (i, 1, `'`, `-`, `\`, `/`, `!`)
  and the retraced or looped shapes (b, r, y, `®`, `%`) lose most. The shipped table has no explicit gates: every
  gate comes from its template.
- One contact only; no multi-stroke glyph other than the two prefixes (X = the extended prefix + x, caps lock = two
  shifts).
- At report rates below 60 Hz a two-frame drop-out exceeds 50 ms and splits the stroke: raise `heal_ms` for such panels.
