# 037 - A Markdown notes app for the deck: what to build, what to drop

Status: study written 2026-10-24. Untracked in the launcher worktree (`launcher-c1`, v0.8.6 aef009b). Apps repo read at
32972fa (branch `docs-launcher-features`, pin `LAUNCHER_REF` = v0.8.5 d0cb456). LVGL 9.5 read in the WSL build tree
(`~/zero7/launcher-c1/SDK/github_source/lvgl/lvgl_9_5`). md4c fetched with `gh` into the session scratchpad, then
compiled and timed there (x86-64 and aarch64 cross; no board). No product code changed, nothing committed, no board
touched.

Markers: **S** = our source (file:line); **U** = upstream source (LVGL 9.5, md4c); **M** = measured today in the
scratchpad; **I** = inferred, to check on a board. Cost unit: one worker session.

## 1. Summary

- **What.** A native `cp0_lvgl` app that edits and reads a **vault**: a plain folder of `.md` files, by default
  `~/Notes` (the guidelines already name it as an example data folder, S: `APP-GUIDELINES.md:460`). The files stay
  Obsidian-compatible. Sync is not the app's job: the README explains Syncthing, rsync and git.
- **Editor: the stock `lv_textarea`, monospace, word-wrapped, no colours.** It is the cheapest editor that already does
  the hard parts: tap to place the cursor, Up/Down by display line, scrolling that follows the cursor, wrapping.
  - Syntax colouring is **LATER**. It needs a custom-drawn editor (about 1.5 sessions), because LVGL cannot colour
    inside a textarea, and label recolouring cannot escape `#`, which Markdown is full of (U: `lv_text.c:166-180`).
  - That custom editor is also the fallback if typing in a large note lags on the Zero 2 W.
- **Preview: md4c v0.6.0 (MIT), vendored**, rendered into one `lv_spangroup` per block, following RSS Reader's
  block-per-label pattern. In the preview, wikilinks can be tapped (follow, or create the note), and so can task
  checkboxes: md4c gives the source offset of each `[ ]` (U: `md4c.h:312`).
- **Index: no database.** One directory walk kept in memory (names, mtime, size). Full text, backlinks, tags and tasks
  come from an on-demand content scan in a worker (slice N2).
- **Slices.**
  - **N1 "write and read notes"** (about 3): vault, folders, open-by-name with create, editor, preview, wikilinks,
    tickable tasks, daily note, atomic autosave, tests and docs.
  - **N2 "find and link"** (about 2.5): full-text search, backlinks, outline, undo/redo, an in-app clipboard, tables,
    rename.
  - **N3 "organise"** (about 2.5): tasks page, tags, templates, find in note, read-only libraries, local images.
- **Dropped:** graph, embeds, canvas, plugins, live WYSIWYG, the system clipboard (there is none), GPS stamps, Send to
  Mesh Hop, Kiwix, rewriting links on rename, a properties editor.

## 2. Findings

### 2.1 Editor widget: three candidates

| | **`lv_textarea` driven by the app (recommended for N1)** | `cp0::ui::TextField` with `multiline` | Custom editor on a line buffer (Serial Console style) |
| --- | --- | --- | --- |
| What exists | Mesh Hop already drives one-line textareas from its own key bridge: `add_text`, `delete_char`, `cursor_left/right` (S: `mesh_app.cpp:1085-1108`, `:4628-4641`) | the field keeps a `std::string` and calls `lv_textarea_set_text` on the **whole** text at every key (S: `cp0_ui_text_field.cpp:221-227`); Up/Down do nothing in multi-line (`:366-369`) | `TermBuffer` is a ring of finished lines plus code-point wrapping (S: `serial-console/src/core/term.hpp`); the view joins the visible rows into one label (S: `terminal_view.cpp:368-386`) |
| Cursor, wrap, scroll | built in: tap places the cursor, `cursor_up/down` work, word wrap, follow-cursor scroll | the same display, but no Up/Down | **missing**: no cursor, no insertion in the middle, no editing; only wrapping and the visible-rows window |
| Colour | none (no per-run style; recolour is broken for `#`) | none | possible: draw the runs yourself |
| Large notes | every insert does `lv_label_ins_text` (realloc + full relayout, U: `lv_label.c:700-711`), then `set_cursor_pos` (U: `lv_textarea.c:284-300`): O(n) per key. Estimate 10-30 ms per key at 100 KB on an A53 (I) | worse: a full `set_text` per key | O(visible) per key |
| Selection | label selection exists (`LV_LABEL_TEXT_SELECTION=y` by default); the app sets start/end itself | no | to write |
| Cost | **about 0.55** with keys, shortcuts and autosave | not fit for a note | **about 1.5** (buffer, wrap cache, cursor, edits, draw callback, tests) |

**Decision.** Use `lv_textarea` in N1, with a size cap: edit up to 128 KB, read-only up to 1 MB, refuse above that.
Tune the cap after the deck test. The custom editor (with colours) is a contingency, not a plan.

### 2.2 Keys, on-screen keyboard, fonts, clipboard

- **Keys.** An app sees every key as a `key_item` (S: `keyboard_input.h:37-49`): the evdev code, the state
  (press, repeat or release), modifiers (Shift, Ctrl, Alt, Logo, `:18-24`) and the UTF-8 text. It switches the
  keypad path off while text is being typed (S: `terminal_view.cpp:243-256`).
  - Only Alt+Tab, Super, the 3 s Esc hold and the volume and brightness keys are the launcher's (S:
    `APP-GUIDELINES.md:297`, `_common/README.md` multitasking). Every Ctrl+letter is free.
  - Copy RSS Reader's `keys` / `key_bridge` (267 lines, unit-tested; US + AZERTY).
- **On-screen keyboard.** The editor calls `cp0::ui::osk::request(obj, Text, multiline=true)`, which gives the
  keyboard with Esc, Tab, Ctrl, **Alt** and the arrows, and it listens to the inset (S: `cp0_ui_widgets.hpp:709-734`).
  - Height (S: `cp0_ui_osk.h:8-14`):
    - 640x480: 300 px, which leaves the header plus about 5 lines of text;
    - 800x480: 50 %, about 7 lines;
    - 720x720: half the screen;
    - 1280x800: 45 %.
  - The rule is never summoned by hand. It shows when no keyboard is present; on the HackBerry it is forced
    (`vkb=force`, S: `APP-GUIDELINES.md:22`).
  - The one-shot Ctrl key on the keyboard sends Ctrl+letter, so shortcuts also work touch-only.
- **Fonts** (S: `projects/APPLaunch/APPLaunch/share/font`, read at run time):
  - Montserrat Medium and Bold;
  - JetBrainsMono Regular and Bold, for the source view;
  - DejaVu Sans, Mono and Serif;
  - Noto Sans CJK, which RSS Reader uses as a fallback chain (S: `rss_app.cpp:154-190`).
  - **No italic face.** FreeType's synthetic oblique (`LV_FREETYPE_FONT_STYLE_ITALIC`, a face transform, U:
    `lv_freetype.c:432-434`) is reachable through `cp0_fonts().get(name, size, style)`. How it looks is (I); check it in
    a harness shot.
- **Tabs.** LVGL has no tab handling (U: nothing for `\t` in `lv_text.c` or `lv_label.c`), so Obsidian's tab-indented
  lists would draw nothing.
  - Map `\t` to a visible `⇥` in the buffer, and back on save. That the glyph exists in JetBrains Mono is (I);
    otherwise use `»`.
  - Never rewrite the user's tabs.
- **Clipboard: none.** The launcher has no clipboard and no app asks for one (launcher report 035, line 249). An
  in-app clipboard is possible (N2). "Insert from clipboard" is dropped.
- **Position for a GPS stamp: none.** `board.state` and `status.state` carry no position (S:
  `APP-GUIDELINES.md:203-205`). No app uses gpsd. A **time** stamp is trivial: local time, with a warning when the clock
  looks unsynced (the boards have no RTC, S: `:468`).

### 2.3 Preview renderer and parser

- **RSS Reader's reader** (S: `page_reader.cpp:463-549`):
  - one wrapped `lv_label` per block in a scrolling column;
  - fonts per block type;
  - quotes drawn with a left border;
  - image tiles decoded off the UI thread (`image_decode.cpp` 286 lines + `image_codecs.c` 138, stb_image);
  - a cap of 400 blocks (S: `ui_logic.hpp:227`).
  - Its model holds **inline text only**: no bold, no italic, no links (S: `html_text.hpp:38-48`).
  - **Reuse:** the page pattern, the scroll keys, the cap, the fonts helper and later the image path.
  - **New:** inline runs. They are one `lv_spangroup` per paragraph (span fonts and colours), and
    `lv_spangroup_get_span_by_point` makes a link or a checkbox tappable (U: `lv_span.h:338`). Span, table and textarea
    are on in the LVGL Kconfig defaults.
- **Parser: vendor md4c, do not write our own.**

| | **md4c v0.6.0 (recommended)** | Own subset parser | cmark / cmark-gfm |
| --- | --- | --- | --- |
| Licence | MIT (U: `md4c.c` header), tag 7fc1815, 2026-09-17, still maintained (commits to 2026-10-19) | ours | BSD-2 |
| Size | `md4c.c` 7464 lines + `md4c.h`; **aarch64 -O2: 60.0 KB text** (M) | 600-900 lines + tests | many files; it builds an AST (memory several times the input, I); tables and tasks need the gfm fork |
| Features we need | CommonMark 0.31.2, plus flags for tables, task lists, strikethrough, **wikilinks** `[[t\|label]]`, permissive autolinks, no HTML (U: `md4c.h:386-411`) | each one is work; emphasis and list rules are hard to get right | yes |
| Speed / RAM | 100 KB synthetic note: **0.32 ms** per parse on the PC; peak RSS of the whole test program 1.7 MB (M). An A53 is perhaps 10-20 times slower: a few ms (I) | similar | more RAM |
| Cost | **0.3** with the block model and tests | 0.75-1 | 0.5 + RAM |

- **md4c details to handle:**
  - It is a SAX-style parser: the app builds its own small block list.
  - Entities arrive as `MD_TEXT_ENTITY`: decode the common ones and numeric references ourselves (about 30 lines),
    rather than vendoring the 74 KB `entity.c`.
  - **Frontmatter** is not Markdown. Cut it off before parsing (it would render as a rule or a setext heading), show it
    muted, and add its length to md4c's offsets.
  - Build: `third_party/md4c/` with its licence. A one-line `.c` file in `src/core` includes `md4c.c`, because
    `build_app.sh` copies `third_party/` without compiling it (S: `build_app.sh:179-180`, `main.SConstruct:18`).
- **Source-view colouring** (only with the custom editor, LATER) would use our own per-line lexer (about 150 lines),
  not md4c.

### 2.4 Files, names, safety

- **Vault location.**
  - `vault=` in `~/.config/<id>/settings.conf`, overridden by `$NOTES_VAULT` (tests, power users); the default is
    `~/Notes`, created on first start.
  - A folder picker in Settings comes in N2: typing a path on the on-screen keyboard is painful.
- **Walk.**
  - `.md` files only, with no depth limit for browsing.
  - Caps: 5000 notes, depth 12.
  - Every dot-folder is skipped (`.obsidian`, `.trash`, `.git`, `.stversions`), and symlinked folders are not
    followed.
- **Load.**
  - Must be valid UTF-8 (otherwise read-only, with a note).
  - A BOM and CRLF line ends are kept: detected on load, written back as found.
  - Tabs are mapped to `⇥` (2.2).
  - Sizes: 128 KB to edit, 1 MB to read.
- **Atomic save** (RSS's `write_file_atomic` without its gap: it does not fsync the folder, S: `fsutil.cpp:40-42`):
  1. write a hidden temp file `.<name>.~deck` in the same folder (sync tools and Obsidian ignore it, I);
  2. `fsync`;
  3. keep the old file's mode;
  4. `rename`;
  5. `fsync` the folder.
  - A save follows 1.5 s after the last key, and also happens on leaving, switching notes, quitting and SIGTERM.
- **Sync safety (core, 0.15).**
  - Before each save, `stat` the file. If its mtime or size changed since it was loaded (Syncthing pulled a change),
    ask: **Keep both** (ours is written as `Name (deck 2026-10-24 1530).md`) or **Reload**.
  - A note is always read from disk when it is opened.
  - Syncthing's own `*.sync-conflict-*.md` files simply show up as notes.
- **Names** (new note, later rename).
  - Spaces and Unicode are allowed.
  - Refused: `/ \ : * ? " < > | # ^ [ ]` (what breaks links or a Windows peer), a leading `.`, a trailing space or
    dot, Windows reserved names, and more than 120 bytes.
  - A collision, case-insensitive, is refused with "already exists: open it?".
- **Delete:** moved to `<vault>/.trash/`, as Obsidian's trash does, after a confirmation. No restore UI: the README says
  where the files are.
- **Multitasking.** `X-Background=freeze`. `AppSpec` has no hide callback (S: `_common/app.hpp:59-64`), so the 1.5 s
  autosave is the guard. A frozen app killed for memory loses at most that much typing.

### 2.5 Index and memory on the Zero 2 W (414 MB usable, app target VmRSS < 25 MB, S: `APP-GUIDELINES.md:464`)

| Item | Estimate |
| --- | --- |
| Native app baseline | 13-15 MB (S: guidelines section 1) |
| FreeType faces: Montserrat Medium/Bold at 2-3 sizes, the italic variants, JetBrains Mono, the fallback chain; 512-glyph caches | 1-2 MB (I) |
| Walk index: path, name, mtime, size per note | about 200 B each: 0.2 MB for 1000 notes |
| N2 content cache, keyed by (mtime, size): link targets, headings, open task lines, tags | about 1 KB per note: 1 MB for 1000 |
| Open note (textarea text + "saved" hash) | 2x the note: 0.25 MB at 128 KB |
| Undo log (N2) | capped at 256 KB |
| Preview: at most 400 spangroups | 0.5-1 MB (I) |
| **Total** | **about 16-20 MB (I, measure with `ps -o rss`)** |

- **CPU risks on the Zero:**
  - typing cost grows with the note size (2.1);
  - a preview of hundreds of spangroups takes perhaps 0.2-0.5 s to lay out (I), hence the 400-block cap and a "rest
    not shown" row;
  - the first full-text scan reads every note (5 MB, perhaps 1-3 s from the SD card, I) in a worker that can be
    cancelled; after that the page cache and the content cache answer.

## 3. Feature decisions

Rule (owner): keep a feature if it costs ≤ 0.5 or is core; N1 ≤ about 3 sessions.

| Feature | Decision | Slice | Cost | Why |
| --- | --- | --- | --- | --- |
| Vault = folder of `.md`; sync documented (Syncthing, rsync, git) | KEEP | N1 | 0.1 | core, and free compatibility |
| Notes list and folders (drill-down) | KEEP | N1 | 0.35 | core |
| Source editor | KEEP (`lv_textarea`) | N1 | 0.55 | core |
| Light syntax highlighting | LATER | E2 | 1.5 | needs a custom editor (2.1) |
| Preview toggle: headings, bold, italic, code, strike, lists, quotes, code blocks, links, rules | KEEP | N1 | 0.5 | core of reading on the deck |
| Simple tables in the preview (`lv_table`) | KEEP | N2 | 0.2 | cheap; rare in notes |
| Quick switcher (fuzzy by name, Create row) | KEEP | N1 | 0.15 | core; one page with search |
| Full-text search (snippets, open at the line) | KEEP | N2 | 0.5 | needs the scan worker |
| `[[wikilinks]]`: follow, create if missing; `[[a\|b]]`, `[[a#h]]`, relative `.md` links | KEEP | N1 | 0.2 | the Obsidian feel; md4c parses them |
| Backlinks (+ a Links page listing outgoing links) | KEEP | N2 | 0.45 | uses the content cache |
| `#tags` list (inline + frontmatter `tags:`) | LATER | N3 | 0.5 | needs the content cache |
| Daily note `YYYY-MM-DD.md` at the vault root (Obsidian's default, I) | KEEP | N1 | 0.05 | trivial; warns on an unsynced clock |
| Outline (headings jump list) | KEEP | N2 | 0.25 | cheap, useful in long notes |
| Tick a task: Ctrl+Enter in the source, a tap in the preview | KEEP | N1 | 0.15 | md4c gives the offset |
| One "all open tasks" page | LATER | N3 | 0.5 | content cache; tick from the list |
| Templates (`Templates/`, `{{date}}`, `{{time}}`, `{{title}}`) | LATER | N3 | 0.35 | nice, not core |
| Frontmatter properties | KEEP display only | N1 | 0.05 | shown muted, kept verbatim; **editor DROP** |
| Small local graph | DROP | - | 1.5+ | layout and drawing cost, little use at 640x480 |
| Images inline (local files, RSS's decoder) | LATER (optional) | N3 | 0.75 | memory and work; shown as a link before |
| Embeds / transclusion `![[note]]` | DROP | - | 1+ | recursion and layout; shown as a link |
| Canvas | DROP | - | many | a 2D editor |
| Plugins | DROP | - | many | no runtime, out of scope |
| Live WYSIWYG | DROP | - | 3+ | needs E2 plus inline rendering |
| Undo / redo: an op log of our own edits, typed runs merged, 200 steps / 256 KB, per open note | KEEP | N2 | 0.3 | ≤ 0.5; edits all pass through one function |
| Selection and an in-app clipboard (Shift+arrows, Ctrl+C/X/V; Cut/Copy/Paste line in More for touch) | KEEP | N2 | 0.5 | no system clipboard exists; drag-selection DROP (it fights scrolling) |
| Word wrap | KEEP | N1 | 0 | built into the textarea |
| Autosave + atomic write + changed-on-disk check | KEEP | N1 | 0.2 | core (2.4) |
| Large-note performance | KEEP caps | N1 | 0.05 | 128 KB edit / 1 MB read; E2 if it lags |
| Vault index: walk in memory, no database | KEEP | N1 | in the 0.35 | content cache in N2 |
| Timestamp insert (More, Ctrl+T) | KEEP | N1 | 0.05 | trivial |
| GPS stamp | DROP | - | - | no position an app can read (2.2) |
| Insert from clipboard | DROP | - | - | no clipboard |
| Send a selection to Mesh Hop | DROP | - | - | no app-to-app channel; `launcher.sock` has no such verb |
| Read-only Markdown libraries as extra vaults | LATER | N3 | 0.25 | a second path flagged read-only |
| Kiwix (ZIM) | DROP here | - | many | needs libzim and HTML; another app if ever |
| Rename note (no link rewrite, says so), new folder, vault picker | KEEP | N2 | 0.3 | rewriting links across files: **DROP** |
| Find in note (Ctrl+F) | LATER | N3 | 0.25 | not in the first two slices |

## 4. UI proposal (patterns of RSS Reader and Mesh Hop; layout to be approved by the owner)

**Pages.**
- **Home** (root; a short Esc quits):
  - *Open or create…* (Open, Ctrl+P);
  - *All notes* (Open, the count in grey);
  - *Recent* (Open: the 20 most recently modified, from the walk);
  - *Today's note* (Action dot);
  - *New note* (Action);
  - *Settings* (Open).
  - Later: *Tasks* and *Tags* (N3).
- **Folder** (`CompactList`, Comfortable):
  - a *New note here* Action row first;
  - a FOLDERS section (Open, the count);
  - a NOTES section (Open, the age in grey, newest first).
  - A note's options (Delete; Rename in N2) open with Ctrl+O, or a 3 s hold as in RSS and Mesh Hop. Since a hold is
    not allowed to be the only way, Delete is also in the editor's More menu.
- **Open / search:**
  - a `TextField` on top, with fuzzy name matches below;
  - a *Create "query"* Action row when there is no exact match;
  - in N2, a *Search inside notes for "query"* Action row, with result rows "Note: snippet" (Open).
  - At 640x480 with the on-screen keyboard only the field is visible, which the guidelines accept (S:
    `APP-GUIDELINES.md:166`). Enter hides the keyboard and shows the matches.
- **Preview** (the proposed default when an existing note is opened):
  - it scrolls by a drag, Up/Down, PgUp/PgDn and Home/End, as RSS does;
  - Tab and Shift+Tab move a ring over links and checkboxes, and Enter acts on the one ringed; a tap acts too;
  - an external URL shows in a dialog (there is no browser);
  - bar: **Edit**, **Links** (N2), **More**.
- **Editor:**
  - the title is the note's name, with a dot while unsaved;
  - the textarea fills the body;
  - a bottom bar of 9 mm toolbar keys: **Preview**, **Links** (N2), **Undo** (N2), **More**.
  - More holds: toggle task, insert date and time, outline (N2), cut / copy / paste line (N2), rename (N2), delete,
    note info.
  - While the on-screen keyboard is up, the bar hides (as in Serial Console). The keyboard's Esc hides the keyboard; the
    header Esc, or Esc on a physical keyboard, saves and goes back.
  - New note and Today open in the editor.
- **Settings (N1):**
  - Vault (Info: the path; N2: Edit through a folder picker);
  - Text size (Edit: small / medium / large, as RSS);
  - Open notes in (Edit: Preview / Editor);
  - Version (Info).
- **Every page:**
  - `Page` + `add_title` + `StatusStrip::add`;
  - no header controls, so nothing collides with `status_strip_left()`;
  - every list row tagged (Open / Edit / Action / Info, S: `APP-GUIDELINES.md:115-142`);
  - the header Esc button does what Esc does.

**Shortcuts (proposal; Obsidian's from memory, I).**

| Key | Does | Note |
| --- | --- | --- |
| Ctrl+P | Open / search page | Obsidian: command palette; VS Code: quick open. Obsidian's Ctrl+O (quick switcher) stays the deck's "options" key |
| Ctrl+N / Ctrl+D | New note / Today's note | |
| Ctrl+E | Preview ↔ Editor | as Obsidian (I) |
| Ctrl+S | Save now (autosave anyway) | |
| Ctrl+Enter | Toggle the task on the cursor line / follow the link under the cursor | |
| Ctrl+T | Insert date and time | |
| Ctrl+O | More (editor) / options (lists) | deck convention |
| Ctrl+L, Ctrl+Z / Ctrl+Y, Shift+arrows, Ctrl+C/X/V | Links page, undo/redo, selection, clipboard | N2 |
| Tab / Shift+Tab | editor: insert a tab / outdent the line; preview: next / previous link | lists: Tab as in other apps |
| Esc | back (saves); at Home: quit | keyboard up: hides it first |

## 5. Boards

| Board | What it costs | Risk |
| --- | --- | --- |
| Zero 2 W deck 640x480 + BT keyboard (reference) | nothing extra | typing lag in big notes; preview layout time; first scan (2.5) |
| Pi 3A+ 800x480, touch only | nothing extra if every action is in a bar or More (no shortcut-only feature) | about 7 lines visible while typing; tap-to-cursor precision on a 4.3" panel (I) |
| HackBerry 720x720, keyboard, keyboard rule forced | none | the on-screen keyboard takes half the screen even with a keyboard (launcher rule): about 10 lines left |
| Pi 5 | none | none |
| FZ-M1 1280x800 x86, touch only | `"arch": ["arm64", "amd64"]`, `build.sh cross --arch amd64`; nothing Pi-specific | none known |

## 6. Testing

- **Unit (host, no LVGL):**
  - the walk on temp fixtures (dot-folders, symlinks, caps);
  - load and save round trips **byte-identical** for CRLF, BOM, tabs and non-ASCII;
  - atomic save failures (read-only folder, disk full by `ulimit -f`, I) leaving the old file intact;
  - the changed-on-disk check;
  - name rules, fuzzy ranking, wikilink resolution (case, folders, `#heading`, `|alias`);
  - the md4c block model, task offsets and frontmatter.
- **Flows.** Copy RSS Reader's runner (S: `rss-reader/tests/flows/script_runner.cpp`, 814 lines: seeds, `ctrl-<letter>`
  keys, `keyboard on|off`, shots). Swap its fake network for `seed note <path> <fixture>` and add
  `expect file <path> contains "…"`.
  - About 8 flows at 640x480 and 800x480 (sheets also at 720x720 and 1280x800):
    1. create and type;
    2. Esc back and reopen;
    3. preview and tick;
    4. a wikilink that creates, then one that follows;
    5. Today;
    6. delete;
    7. a conflict on disk;
    8. typing with the on-screen keyboard (`keyboard off`).
- **Shared scenes:** `notice`, `nocontract`, `lifecycle` (S: `_common/headless/README.md`).
- **Board check (owner, about 10 min, deck).**
  - The N1 main path.
  - `tests/board/big-note.sh` writes a 100 KB note; type in it and judge the lag. This decides between the caps and
    E2.
  - `ps -o rss`.
  - One touch-only pass on the Pi 3A+.

## 7. Name, id, package

- **Title "Markdown Notes", id `markdown-notes`** (14 characters, under the 15-character `comm` limit, S:
  `APP-GUIDELINES.md:369`).
  - It is descriptive like "RSS Reader" and "Serial Console", and says the files are plain Markdown.
  - Nothing in it may suggest Obsidian, whose name appears only in the README's compatibility note.
  - Debian trixie has no package named exactly `notes` (W, packages.debian.org), but a generic id risks clashing later.
- **Category `Productivity`**: new, free-form. `make_registry.py` passes it through (S: `:111`), and the launcher
  parses it but shows it nowhere today (S: `apps_backend.cpp:247`).
- **`app.json`:**
  - `license` "MIT" (md4c is MIT; credit it in the README and the copyright file);
  - `depends` "libfreetype6, libinput10, libxkbcommon0";
  - `permissions` {} (no network);
  - `arch` ["arm64", "amd64"].
- **`.desktop`:** `X-Fullscreen=true`, `X-MinSize=640x480`, `X-Background=freeze`.

## 8. Slices

| Slice | Content | Cost |
| --- | --- | --- |
| **N1 Write and read notes** | Core and tests (0.7): walk, load and save (UTF-8, BOM, CRLF, tabs), atomic save, changed-on-disk check, names, settings and `$NOTES_VAULT`, fuzzy match, link resolution, daily name. md4c and the block model (0.3). Home, folder lists, open/create page, New, Today, Delete to `.trash` (0.5). Editor: textarea, keys, shortcuts, task toggle, time stamp, autosave, keyboard inset, More, caps (0.55). Preview: spangroups, links (follow and create), ticks, 400-block cap (0.5). Flows, scenes, BOARD-TESTS, README with the sync how-to, `app.json`, icon, amd64 (0.45) | **about 3.0**. If it runs over, Today, the time stamp and Delete move to N2 |
| **N2 Find and link** | Full-text search worker (0.5); content cache, Links page and backlinks (0.45); outline (0.25); undo/redo (0.3); selection and in-app clipboard (0.5); tables (0.2); rename, new folder, vault picker (0.3) | **about 2.5** |
| **N3 Organise** | Tasks page (0.5); Tags (0.5); templates (0.35); find in note (0.25); read-only libraries (0.25); local images (0.75, optional) | **about 1.85, 2.6 with images** |
| E2 (contingency) | custom editor with colours and O(visible) edits | 1.5, only if the deck test lags or the owner wants colours |

## 9. Dropped, in one line each

- **Graph, canvas, embeds, live WYSIWYG, plugins:** each costs one or more sessions for little use at 640x480.
- **System clipboard, insert from clipboard:** none exists.
- **GPS stamp:** no app-readable position.
- **Send to Mesh Hop:** no app-to-app channel.
- **Kiwix:** libzim plus HTML; another app.
- **Link rewriting on rename:** it touches many files and risks sync conflicts.
- **Properties editor:** frontmatter is shown and kept, not edited.
- **Drag-selection:** it fights scrolling.
- **Sync itself:** documented only.

## 10. Owner questions (default in bold)

1. **Name:** **"Markdown Notes", `markdown-notes`, category Productivity**; or "Notes" / `deck-notes`?
2. **Vault:** **`~/Notes`**, changed in Settings (N2) or in `settings.conf` / `$NOTES_VAULT` (N1); or
   `~/Documents/Notes`?
3. **Opening a note:** **Preview for existing notes, Editor for new notes and Today** (on a touch-only board the
   on-screen keyboard then never pops up just from opening); or always the editor, like Obsidian?
4. **No colours in the source view** in N1 (`lv_textarea`), with the custom editor only if the deck test lags: **yes**?
5. **Shortcuts:** **Ctrl+O stays the deck's "options / More", the switcher is Ctrl+P**; or Obsidian's Ctrl+O for the
   switcher?
6. **Delete:** **to `<vault>/.trash` after a confirmation, no restore page**; or delete for good?

## 11. Risks

- **Typing lag in large notes** (`lv_textarea` is O(n) per key). Contained by the caps and decided by the deck test;
  E2 is the way out.
- **Sync races:** a pull between our `stat` and our `rename` is still possible (a short window). "Keep both" covers the
  common case. Syncthing may also scan the hidden temp file (I).
- **The clock:** no RTC, so a daily note can get the wrong date before NTP. Warn when the year is older than the
  build's.
- **Synthetic italic** may look poor at small sizes (I); bold-only emphasis is the fallback.
- **The `⇥` tab mapping:** a real `⇥` typed by the user would become a tab on save; the glyph may be missing (I).
- **A frozen app killed for memory** loses up to 1.5 s of typing (no hide hook in `_common`). A hook would be a small
  shared change.
- **Obsidian edge cases:**
  - resolving duplicate names (we prefer the same folder, then the shortest path, I);
  - `aliases:` in frontmatter is ignored;
  - block references `^id` are ignored;
  - nested tags are shown flat (N3).
- **Partly verified only:**
  - md4c's speed on an A53;
  - the preview's layout time;
  - the fuzzy ranking on a real vault;
  - that the toolkit APIs read at v0.8.6 behave the same at the v0.8.5 pin (I: TextField, `osk`, fonts and lists
    predate 0.8.5).

## 12. Sources

- **S:**
  - apps repo 32972fa: `docs/APP-GUIDELINES.md`;
  - `apps/_common` (`app.hpp`, `osk_policy.hpp`, `build_app.sh`, `scons/`, `headless/README.md`);
  - `apps/serial-console/src/{core/term.hpp, ui/terminal_view.cpp}`;
  - `apps/rss-reader/src/{ui/page_reader.cpp, ui/rss_app.cpp, ui/keys.hpp, ui/key_bridge.hpp, core/html_text.hpp,
    core/fsutil.*}`, `tests/flows/script_runner.cpp`;
  - `apps/mesh-hop/src/ui/mesh_app.cpp`; every `app.json`.
  - Launcher aef009b: `ext_components/cp0_lvgl/include/{cp0_ui_widgets.hpp, cp0_ui_osk.h, keyboard_input.h,
    cp0_font_service.hpp}`, `src/cp0_ui_text_field.cpp`, `projects/APPLaunch/APPLaunch/share/font/`,
    `docs/dev/reports/035-board-state-and-app-sdk.md`.
- **U:**
  - LVGL 9.5: `widgets/textarea/lv_textarea.c`, `widgets/label/lv_label.c`, `widgets/span/lv_span.h`,
    `misc/lv_text.c`, `libs/freetype/lv_freetype*.c`; the SDK's `lvgl_9_5/Kconfig`.
  - md4c https://github.com/mity/md4c, `src/md4c.{c,h}` at master f9e5eb2 (tags v0.6.0 7fc1815, release-0.5.3).
- **M:** the scratchpad benchmark (`md4c-bench/bench.c`, `gen.py`). It parses synthetic notes of 10 KB and 100 KB
  (headings, emphasis, wikilinks, tasks, tables, fences) with tables, tasks, strikethrough, wikilinks, autolinks and
  no HTML. Built with gcc -O2 in WSL; the aarch64 size from `aarch64-linux-gnu-gcc -O2`.
- **W:** https://packages.debian.org (trixie, exact name `notes`: no match). Obsidian's own hotkey help did not list
  the defaults (https://obsidian.md/help/hotkeys), so the Obsidian shortcuts in section 4 are from memory (I).
