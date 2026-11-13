# 060: Vinyl P2 plan (study)

Date 2026-11-13. A read-only study: no product code was written, nothing was installed, no board or repository was
changed, nothing was committed or pushed.

**Sources read:**
- Report 059, all of it.
- The P1 app, worktree `C:\CLAUDE\zero7\apps-vinyl`, branch `vinyl-p1` @ 5591e26: `apps/vinyl/src` (every header, and
  `engine.cpp`, `tag_reader.cpp`, `vinyl_app.cpp` where cited), `README.md`, `tests/BOARD-TESTS.md`,
  `docs/decisions.md`, `app.json`, `vinyl.desktop`.
  - The worktree holds **uncommitted changes of another worker**: `VolumeUnit`, `volume_level_text`, `step_db`, and
    "Later (not built)" in `decisions.md`. This plan assumes they land.
- The full-UX canvas (`Main.dc.html` and 19 boards, scratchpad `vinyl-ux/project`).
- Launcher `main` @ 0d4baacc: the design-QA checklist, its scene set and agent, `APP-CONTRACT.md` sections 13-14,
  `DEV-GUIDE.md`, `OPEN_SOURCE_COMPONENTS.md`, `LICENSES/`, the `ROADMAP.md` Vinyl rows, and the Settings > Apps
  installer (`projects/AppStore/main/backend/appstore_native.cpp`).
- The memory notes; Cassette `dsp/`; the Mesh Hop sd-bus layer; the cp0_lvgl SQLite loader.

**Tags (as in 058 and 059):** **[F]** fact, with its evidence; **[I]** inference; **[U]** unverified.

**Board access 2026-11-13:**
- HackBerry, zero8, Pi 3A+, the deck and RPI52 answered; the Pi 4B (.167) timed out twice.
- Read-only commands only: `dpkg-query`, `apt-config dump`, `free`, `/proc` and `/sys` reads, `ls`, `grep`, the
  launcher's state files, and the memory map of the Vinyl running on the HackBerry.
- Two python3 runs on zero8 used SQLite **in memory** at `nice 10`: FTS5 checks, and timings with 1k, 10k and 30k
  synthetic tracks. Nothing was written to disk. MemAvailable was 260 MB before and 261 MB after; 44 °C.
- Raw output and scripts: scratchpad `060-tools/`.

---

## 0. Summary

1. **Scope.** P2 adds:
   - the Menu key (Equalizer, Sleep timer, Playback, Library, Output, About);
   - the SQLite library with search, track actions and playlists;
   - EQ, crossfade, ReplayGain, and the owner's gain boost and "Volume steps";
   - AVRCP/MPRIS metadata, USB scanning and folder art.
2. **Order: five releases.**
   - P2a Menu + Playback + Sleep + About (0.2.0);
   - P2b Library (0.3.0);
   - P2c Playlists (0.4.0);
   - P2d EQ + crossfade (0.5.0);
   - P2e metadata, USB and art (0.6.0).
   The float DSP stage that ReplayGain, the boost, EQ and crossfade share comes first, with the Playback page.
3. **Size: 21-27 worker-days** (059 said 10-15). The extra comes from five screen classes per page, a QA scene set per
   page and the owner's two added settings.
4. **[F] A 0.1.0 release blocker sits in the installer.**
   - Settings > Apps runs `dpkg --install` when Depends are met, else `apt-get -y install <deb>` without
     `--no-install-recommends` (`appstore_native.cpp:1807-1827`).
   - Every probed board has `APT::Install-Recommends "1"`.
   - So on a fresh Lite board, Vinyl's `Recommends` pulls about 462 MB of FFmpeg and its recommends (110 packages,
     059 13.3).
5. **[F] SQLite 3.46.1 with FTS5 is on every board.**
   - The trigram tokenizer with `remove_diacritics 1` matches "beyonce" to "Beyoncé" and CJK runs of three characters or
     more.
   - On zero8, FTS5 search takes 0.3-0.5 ms, where a 3-column `LIKE` takes 60 ms (10k tracks) and 188 ms (30k).
6. **[F] P1 code to fix before big libraries:**
   - the whole queue is rewritten to `state.json` every 5 s while playing (`vinyl_app.cpp:1485`);
   - the MP3 tag reader reads whole ID3 tags, covers included (`tag_reader.cpp:512-515`);
   - ReplayGain tags (TXXX) are ignored.
7. **Memory.** [F] P1 on the HackBerry uses 59 MB RSS with FFmpeg loaded (PSS 42 MB). The Zero 2 W figure is [U]; P2a
   measures it first.
8. **[I] CPU is no limit.**
   - Own 10-band EQ: about 0.6-1.5 % of one Zero 2 W core (FFmpeg's chain measured +3.1 %).
   - Limiter: about 0.1 %.
   - Crossfade: +0.8-2.6 % during the fade.
9. **Main risks:**
   - the Track actions hold (D-35) and the EQ page keys (D-29);
   - dB text in a 52-80 px value box;
   - scan I/O against playback on SD;
   - no USB automount on Lite images [F];
   - an AVRCP player registration that moves headset keys off uinput.

**Owner decisions (recommendation after each):**
- **D20** FFmpeg libraries as `Suggests` (not `Recommends`) for 0.1.0, with the apt command in the notice and the README.
  **Yes.**
- **D21** Publish 0.1.0 before P2, once D20 is applied and design QA is READY. **Yes.**
- **D22** P2 as five releases in the order P2a, P2b, P2c, P2d, P2e. **Yes.**
- **D23** Gain boost 0 to +4 dB, per output, applied at every volume level, with a look-ahead limiter. **Yes.**
- **D24** "Volume steps" as one global setting; never finer than the output's own control step; holding "-" or "+"
  repeats. **Yes.**
- **D25** ReplayGain from tags only: Off / Track / Album, default Track; untagged files at 0 dB. **Yes.**
- **D26** Playlists are M3U8 files in `~/Music/Playlists`; the library only indexes them. **Yes.**
- **D27** The library scan keeps running when the Library page is left; it pauses above 75 °C. **Yes.**
- **D28** Vinyl scans mounted drives only and never mounts one; automount is a generic launcher item. **Yes.**
- **D29** Crossfade offered on every board, default Off, never inside an album, only between tracks of the same format.
  **Yes.**
- **D30** One EQ for all outputs; on the EQ page Left/Right pick a band and Up/Down set it (an exception to D-29).
  **Yes.**
- **D31** Track actions open with the 3 s hold box ("Hold 3 s: options"), with Ctrl+O, and with letter keys on a row.
  **Yes.**
- **D32** Menu and Playback show only built features: no dimmed rows, and the "Speed (later)" row is dropped. **Yes.**
- **D33** AVRCP/MPRIS metadata comes last in P2; it moves to P3 if no owner device has a display. **Ask the owner.**

---

## 1. Starting point: what P1 gives P2

| Item | P1 state [F, file] | Consequence for P2 |
|---|---|---|
| Engine | One thread: decode 2,048 frames, then `convert_pcm()` (integer, one gain), then write. Gapless on the same stream when the format is equal (`engine.cpp:485-559`). | A float stage goes between decode and convert. The integer path stays, bit-exact, when every stage is off. |
| Volume | `OutputVolumeModel` per output; `volume_model(lo, hi, native_step, step_db)`, `VolumeUnit` and `volume_level_text()` (uncommitted). | "Volume steps" needs the setting, a picker, hold-to-repeat and a value box that fits dB text. No new model. |
| Tags | Own readers (ID3, FLAC, Ogg, MP4, WAV, AIFF). TXXX ignored. The MP3 path reads the whole ID3 tag, up to 24 MB (`tag_reader.cpp:472, 512-515`). No APEv2, no ASF. | Add the ReplayGain fields, a scan mode that skips pictures by their size, and APEv2. |
| Covers | Embedded only, stb_image. Not decoded over 1,200 px. Allocator ceiling 24 MB (`cover.hpp:29-30`). | Folder art, a cover cache and a scaled JPEG decode (P2e). |
| Browse | `~/Music` and the mounts under `/media`, `/run/media` and `/mnt`, at most 5,000 entries. The M3U parser drops missing files. | Folders stays the first row of the Library hub. |
| Queue | Paths, play order, shuffle, repeat; move up, move down, remove, clear. | Add play next, add to queue, insert, and save as a playlist. |
| State | `state.json` holds the queue (up to 5,000 items), the position, the output and the levels. Rewritten every 5 s while playing (`vinyl_app.cpp:1485-1555`). | [I] 450 kB per write, about 320 MB an hour on SD. Split it: `queue.json` written on change, a small `state.json`. |
| Background tags | `TagJobs` runs at nice +10 with a RAM cache of up to 20,000 entries. | Shrink the cache once the database exists. |
| Media keys | Own evdev reader; PLAYCD and PAUSECD act as one toggle. | An AVRCP registration (P2e) sends headset keys over D-Bus instead, so both paths are needed. |
| Layout | The Menu key is omitted, its widths kept in comments (`layout.cpp:110, 154, 262`). A dimmed sleep key on square and large (`layout.cpp:220-221, 259-260`). | P2a places the Menu key in the five classes and wires or replaces the sleep key (a D-81 item goes). |
| UI code | `vinyl_app.cpp` 1,568 lines, `vinyl_view.cpp` 1,513 lines; lists drawn by the app. | New pages go into `ui/pages/*`. |

**Canvas items out of date [F]:**
- `Main.dc.html` tags "About, keys, licences" as *built*, but 0.1.0 has no About page.
- Its access table still gives 480x320 the "Vol chip ... strip". The owner replaced that with the volume group.
- Playback-480x320 says "Crossfade needs a faster board"; 3c contradicts it.
- LibrarySettings says the scan "stops when you leave the page" (D27).
- Playback has a "Speed (later)" row, a D-81 item.

---

## 2. Work breakdown

### 2.0 Why this split

The brief proposed four steps: P2a Menu/Playback/Sleep/About; P2b Library and playlists; P2c EQ, crossfade, boost, steps
and metadata; P2d polish. This plan makes three changes:

1. **The gain boost and "Volume steps" move into P2a.**
   - They are rows of the Playback page P2a draws.
   - The boost needs the same float stage and limiter as ReplayGain.
   - Later would mean dimmed Playback rows for two releases (D-81).
2. **Playlists become their own step (P2c).**
   - They need no database (D26), only P2b's Track actions.
   - P2b is already the largest step.
3. **AVRCP/MPRIS goes last (P2e).** None of the owner's Bluetooth devices shows metadata (XWF-M18, Evolve 65; 059 13.6,
   B8), so it may slip to P3 (D33).

Every step ends with:
- unit tests (fakes, in-memory SQLite);
- headless scenes;
- a design-QA run (advisory while building, blocking at publish);
- a short owner protocol on the HackBerry and one Zero 2 W (zero8 at 320x480 or the deck at 640x480);
- a release.

### 2.1 Steps at a glance

| Step | Contents | Worker-days | Depends on | Owner |
|---|---|---|---|---|
| **P2a** 0.2.0 | Menu key (5 classes) and Menu page. Playback: ReplayGain, gain boost, Volume steps, positions in long files. Sleep timer with fade; About and keys; settings store; float DSP stage and look-ahead limiter; state split. | **5-6** | D20/D21 settled; drafts 5.2 #1-6 | D23-D25, D32; B29-B36 |
| **P2b** 0.3.0 | SQLite store and background incremental scan. Hub; Artists, Albums, Songs and Genres pages; Search; Track actions (play, play next, add to queue, info); Track info; Library settings; play counts. | **7-9** | P2a list view and settings | D27, D28, D31; B37-B45 |
| **P2c** 0.4.0 | Playlists page and edit; save the queue; add to playlist; rename, delete; M3U/M3U8 import and export. | **3-4** | P2b Track actions | D26; B46-B50 |
| **P2d** 0.5.0 | 10-band EQ with presets (EQ page in 5 classes); crossfade. | **3-4** | P2a DSP stage | D29, D30; B51-B55 |
| **P2e** 0.6.0 | BlueZ RegisterPlayer (MPRIS object); optional session MPRIS. Scan on mount; "drive removed" pause. Folder art, cover cache, scaled JPEG decode; 100k tuning. | **3-4** | P2b | D33; B56-B61 |
| **Total** | | **21-27** | | |

### 2.2 P2a: Menu, Playback, Sleep timer, About (0.2.0)

**Scope:**
- **Menu key**, plus keys M and T (sleep timer) by physical key code (D-32):
  - the seventh transport key at 480x320;
  - the bottom-row icon key on portrait;
  - the sixth transport key on square, replacing the dimmed sleep key;
  - a bottom-row key on large, in place of Output;
  - Browse | Queue | Menu at 640x480 (`layout.cpp:154`).
- **Menu page** with built rows only (D32): Sleep timer, Playback, Output, About.
- **Playback page:** ReplayGain (Off/Track/Album), Gain boost (0 to +4 dB, for the output in use), Volume steps
  (Normal/1/0.5/0.1 dB), Remember position in long files.
- **Sleep timer:** Off, 15, 30, 60 min, This track, This album or playlist; a 10 s fade, then pause.
- **About and keys.**
- **The state split** (section 1).

**Files** (under `apps/vinyl/src`):

| New / changed | What |
|---|---|
| `core/settings/settings.{hpp,cpp}` | `~/.config/vinyl/settings.json`: `replaygain`, `boost_db` per output key, `volume_steps`, `remember_long`, later `crossfade_s`, `eq`, `library`. Atomic write; unknown keys kept. |
| `core/dsp/chain.{hpp,cpp}` | The float stage: int to float, then gain (ReplayGain, boost, fade), [EQ in P2d], software volume (outputs without a mixer only), limiter, TPDF dither to S16 or plain S32. A bypass keeps P1's integer path. |
| `core/dsp/limiter.{hpp,cpp}` | The look-ahead limiter (3e). Cassette's `dsp/limiter` (no look-ahead) is the base. |
| `core/dsp/replaygain.{hpp,cpp}` | Tag values to a gain: mode and peak guard. |
| `core/tags/tag_reader.*` | `TrackTags` gets the ReplayGain gains and peaks. Parse TXXX and Vorbis `REPLAYGAIN_*`, Opus `R128_*_GAIN` and MP4 `----:com.apple.iTunes:replaygain_*`. |
| `core/engine/engine.*` | `set_dsp()` per track, applied at the heard boundary (the `Seg` list); `fade_out(ms)`; `stop_after(Track \| Album)`. |
| `core/sleep/sleep_timer.*` | A pure state machine with an injected monotonic clock. |
| `core/state/positions.*`, `core/state/resume.*` | `positions.json` (LRU of 500: files of 20 min or more, and `.m4b`); `queue.json` written on change; `state.json` small. |
| `ui/pages/settings_list.*` | One list view for the Menu, Playback, Sleep, picker and Info pages, with the `cp0_ui_list.h` helpers (D-45, D-48). |
| `ui/pages/about_page.*` | About and keys. |
| `layout/layout.cpp`, `ui/vinyl_view.cpp`, `ui/vinyl_app.cpp` | The Menu key; page routing; the volume group's dB text and hold-to-repeat. |

**Reuse:** the worktree's volume model; `util/json` and `util/atomic_file`; Cassette `dsp/limiter`, `gain` and `pcm`;
the P1 page shell, notices and mini player.

**Tests:**
- `test_dsp`:
  - the bypass is bit-exact with `convert_pcm`;
  - gain within ±0.01 dB;
  - a full-scale square wave at +4 dB never passes the ceiling;
  - the latency equals the look-ahead;
  - dither has a zero mean and stays under 1 LSB RMS.
- `test_replaygain`: TXXX v2.3/v2.4, Vorbis, Opus R128 (+5 dB), MP4 freeform, the peak guard, each mode.
- `test_sleep` (fake clock): the fade starts at T-10 s; the stop comes at the track boundary, or at an album change.
- `test_positions`, `test_settings`, `test_state_split` (an old `state.json` migrated), and `test_volume` with 1, 0.5
  and 0.1 dB steps clamped to the native step.
- Scenes:
  - `qa-menu` (Menu, Playback, pickers, timer set and running, About) and `qa-menu-small` (480x320 and 320x480 at
    `ppmm 630`);
  - `np-db` (the volume group in dB mode, every class);
  - `lifecycle` (timer running while hidden).

| # | Do (owner, about 25 min) | Expected |
|---|---|---|
| B29 | Every layout: tap the Menu key, then press M; Esc | The Menu opens (Sleep timer, Playback, Output, About); Esc goes back to now playing. |
| B30 | Sleep timer "This track" in a track's last minute; then 15 min, left running during B31-B35 | The last 10 s fade, then pause at the next track's start. The 15-minute timer fades and pauses on time, also while hidden (HackBerry). |
| B31 | ReplayGain Track, then Off, on LOUD and QUIET (tags written by the Controller on copies) | Track: about equally loud. Off: LOUD clearly louder (about 7 dB). |
| B32 | Pi 3A+ jack at 100 %: boost +4 dB, play LOUD; then switch to the USB DAC | Louder, no crackle on peaks; the DAC keeps 0 dB (per output); the journal shows `dsp: boost +4 dB, limiter max -x dB`. |
| B33 | Volume steps 1, 0.5, then 0.1 dB; hold "+" for 3 s | The group shows dB values; on the Sharkoon the finest step is 1 dB; holding repeats. |
| B34 | Play a 1-hour mix to 20:00, play another track, come back | The mix resumes at 20:00; a 4-minute track starts at 0:00. |
| B35 | Menu > About and keys | Version 0.2.0, the licences, the keys. |
| B36 | Controller: `grep VmRSS /proc/$(pgrep -x vinyl)/status` on zero8, playing an MP3, then an M4A | Both values recorded (budget in section 4). |

### 2.3 P2b: Library (0.3.0)

**Scope:**
- the SQLite store and the background scan (3a);
- the hub: Folders, Artists, Albums, Songs, Genres, Search (Playlists in P2c);
- artist page (albums by year), album page (disc and track order), Songs (paged), Genres;
- Search with result sections;
- Track actions (Play, Play next, Add to queue, Track info) and the Track info page;
- Library settings (folders, Rescan now, last scan, progress);
- play counts.

**Files:**

| New / changed | What |
|---|---|
| `core/db/sqlite_lib.*` | dlopen of `libsqlite3.so.0`: the symbol pattern of cp0_lvgl's `cp0_map_sqlite.c`, plus `bind_null`, `changes`, `last_insert_rowid`, `interrupt`; RAII statements. |
| `core/library/schema.*`, `sortkey.*`, `library.*` | DDL v1 and migrations; sort keys; paged view queries, search, counts, stats. |
| `core/library/scanner.*`, `roots.*` | Walker, folder diff, scan-mode tag reads, batched commits, progress, cancel, thermal pause, nice and `ioprio` IDLE; roots from the settings; mounts and UUIDs. |
| `core/tags/tag_reader.cpp` | Scan mode: skip the APIC, PIC, GEOB and FLAC PICTURE bodies. An APEv2 reader (about 150 lines). |
| `core/queue/queue.*` | `insert_next()`, `append()`. |
| `ui/pages/library_*` | Hub, lists, album, search (`cp0::ui::TextField` with `_common/osk_policy`), action sheet, track info, settings, folder picker, scan strip. |

**Reuse:** P1 `browse` (Folders, and the folder picker's choose mode), `tag_reader`, `tag_jobs` (covers), `queue`, the
P2a list view.

**Tests:**
- `test_sortkey`: articles, diacritics, natural numbers, kana, Hangul.
- `test_scanner` (a temporary fixture tree): the first scan; an unchanged rescan reads no tags (counted); a touched file
  is read again; a deleted file goes; an offline root is hidden, then restored; cancel and resume.
- `test_library`: every view and paging on 1k synthetic rows.
- `test_search`: trigram, the prefix fallback for 1-2 characters, CJK, diacritics.
- `test_tags_scan`: a file with a 2 MB APIC is read in under 64 kB.
- Scenes:
  - `qa-library` / `qa-library-small`: hub, artists, artist, album, songs, genres, search (with and without results),
    actions, track info, settings, scanning, scan paused;
  - `qa-empty-library` and `qa-cjk-library`;
  - `keyboard` (D-38, D-39) and `portrait-band` at 480x800 and 480x640 (D-41).

| # | Do (owner and Controller, about 30 min) | Expected |
|---|---|---|
| B37 | zero8: the 059 scan set (1,000 MP3s, 100 albums) copied to `~/Music/scan`; open Library | A progress line during the first scan; done in under 30 s; "1,0xx tracks". |
| B38 | Close and open Vinyl | The rescan finds nothing new in under 3 s; no tag is read again (journal). |
| B39 | Play a FLAC, then "Rescan now" over a fresh copy of the scan set | No dropout (journal `underruns 0`). |
| B40 | Artists > an artist > an album > a track; Songs; Genres | Lists open at once; disc and track order; CJK names draw. |
| B41 | Search "fen", "夜明け", "beyonce", "ab" | Results while typing, in under 0.5 s; case and accents ignored; 2-character queries match the start of a title. |
| B42 | A song row: hold 3 s, and Ctrl+O; then Play next, Add to queue, Track info | The sheet lists the actions with their keys; the queue changes as chosen; Track info shows the tags and the file. |
| B43 | HackBerry: a USB stick with music (desktop automount); scan; pull it; plug it in again | Its tracks appear; pulled, they leave the views without errors; plugged in, they come back. |
| B44 | Play one track 5 times to the end | Track info shows 5 plays and the last-played time. |
| B45 | Controller: database size and RSS after B37 | About 1 MB for 1k tracks; RSS within the budget. |

### 2.4 P2c: Playlists (0.4.0)

**Scope:**
- the Playlists page (New playlist, names with track counts);
- edit: Move up/down, Remove, Add tracks, Rename, Export M3U;
- Save the queue (a text field); Add to playlist (Track actions);
- import an `.m3u`/`.m3u8` from Folders;
- delete after a question;
- missing entries dimmed, never dropped.

**Files:**
- `core/playlist/m3u.*`: replaces P1's `parse_m3u`, which stays as a wrapper.
  - Reads UTF-8 with or without a BOM, CRLF, Latin-1 `.m3u` bytes, backslashes and drive letters, `file://` URLs and
    `#EXTINF`; `http` lines are skipped.
  - Writes M3U8 with `#EXTINF:sec,Artist - Title`, with relative paths on the same drive.
- `core/playlist/playlists.*`: `~/Music/Playlists`, atomic writes, indexed in the database.
- `ui/pages/playlist_*`.

**Reuse:** the P1 queue buttons and the "Clear the queue?" dialog pattern, P2b Track actions, the P2a list view.

**Tests:**
- `test_m3u`: Windows, Latin-1, relative, absolute, missing and URL entries; a round trip.
- `test_playlists`: edit operations, name collisions, an interrupted write.
- Scenes: `qa-playlists` and `qa-playlists-small` (list, edit, save, add-to picker, delete question, empty), and
  `keyboard` for Save and Rename.

| # | Do | Expected |
|---|---|---|
| B46 | Queue > Save queue as playlist "Test", with a keyboard, then without one (zero8) | `~/Music/Playlists/Test.m3u8` exists; listed with its count. |
| B47 | Edit "Test": move a track up, remove one, add one from Search | The order holds after a restart. |
| B48 | Import a Windows `.m3u` (backslashes, Latin-1 accents) from Folders | Entries resolve; accents are right; missing files show dimmed. |
| B49 | Export "Test" to the USB stick; open it on the PC | It plays in a PC player (relative paths). |
| B50 | Delete "Test" | It asks first; Cancel keeps it. |

### 2.5 P2d: Equalizer and crossfade (0.5.0)

**Scope:**
- **EQ:** 10 bands (31 Hz to 16 kHz), ±12 dB in 1 dB steps; presets, Reset, On/Off; "Band 4 kHz: +3 dB" as drawn.
- **Crossfade:** Off / 2 / 4 / 6 / 8 / 10 s, in the Playback page.

**Files:**
- `core/dsp/biquad.*`: RBJ peaking filters, TDF-II, double coefficients.
- `core/dsp/eq.*`: bands, presets, headroom.
- `core/dsp/crossfade.*`: the equal-power mixer.
- `engine.cpp`: a second decoder and the mix.
- `ui/pages/eq_page.*`.

**Tests:**
- `test_eq`: the response at each band centre within ±0.2 dB (a sine sweep); the bypass bit-exact; headroom; presets.
- `test_crossfade` (fake decoders): the equal-power sum; the same-format, album, unknown-length and manual-skip rules;
  a seek during the fade.
- `vinyl --bench-dsp`: a hidden flag that prints the CPU of 60 s of audio through the chain.
- Scenes `qa-eq` and `qa-eq-small`.

| # | Do | Expected |
|---|---|---|
| B51 | EQ "Bass boost", then Reset, then Off | A clear bass change; Reset is flat; Off sounds as in 0.4.0. |
| B52 | Change bands by keys and by touch at 320x480 (zero8) and 720x720 | Every band reachable; its value shows; no tap lands on a neighbour band. |
| B53 | Controller: `vinyl --bench-dsp` on zero8 | EQ under 2 % of one core; the full chain (crossfade, EQ, limiter) under 6 %. |
| B54 | Crossfade 6 s, shuffle over two albums; then one album in order | Tracks of different albums overlap 6 s; within an album the changes stay gapless. |
| B55 | Crossfade on, press Next mid-track | An immediate change, no fade. |

### 2.6 P2e: Bluetooth metadata, USB sticks, cover art (0.6.0)

**Scope:**
- **Metadata:** `org.bluez.Media1.RegisterPlayer` on the system bus for each adapter, when `board.state` `bt_audio` is
  not `none`. The object carries the MPRIS Player properties and Play, Pause, PlayPause, Next, Previous and Stop.
  Optional: the session name `org.mpris.MediaPlayer2.vinyl` (`/run/user/1000/bus` exists on every probed board [F]).
- **USB:** scan on mount; "Drive removed. Paused." when the playing file's drive goes.
- **Cover art:** `folder.jpg`, `cover.jpg` and `front.jpg/png`; a cover cache; a scaled decode of baseline JPEGs over
  1,200 px.
- **Tuning** at 100k tracks.

**Files:**
- `core/dbus/sdbus_lib.*`: about 150 of the 1,071 lines of Mesh Hop's `bluez_backend.cpp`.
- `core/mpris/mpris_player.*`.
- `core/library/mount_watch.*`: POLLPRI on `/proc/self/mounts`.
- `core/art/folder_art.*`, `cover_cache.*`.
- `third_party/tjpgd/`: ChaN's TJpgDec, about 1.1k lines, scaled 1/2-1/8 decode.

**Tests:**
- `test_mpris` (fake bus): properties from snapshots; methods to transport actions; registration again after a bluetoothd
  restart.
- `test_mount_watch`, `test_folder_art`, `test_cover_cache`.

| # | Do | Expected |
|---|---|---|
| B56 | HackBerry, XWF speaker: play/pause and next, Vinyl in front and hidden | Each press acts once; the journal shows `mpris: Play from bluez`. |
| B57 | A device with a display, if the owner has one | Title and artist show and follow the track. |
| B58 | Controller stops bluetoothd for 10 s, then starts it | Vinyl registers again; the keys work. |
| B59 | Unplug the playing USB stick; plug it in again | "Drive removed. Paused.", no cascade of errors; Play continues once it is back. |
| B60 | A folder with `cover.jpg`; an MP3 with a 3000 px baseline JPEG | Both covers show. |
| B61 | Controller: a 100k synthetic database on the HackBerry | Views and search under 100 ms; the size recorded. |

---

## 3. Architecture of the hard parts

### 3a. SQLite library

**The library [F].** All five boards have `libsqlite3-0 3.46.1-7+deb13u2`, built with `ENABLE_FTS5`,
`DEFAULT_CACHE_SIZE=-2000`, `SECURE_DELETE` and `THREADSAFE=1`, without ICU. It is loaded with dlopen, as the cp0_lvgl
map engine does. Public domain; added to `depends` in 0.3.0.

**Schema v1 [I].** Folders are split from file names, which keeps rows and the scan diff small.

```sql
create table meta(key text primary key, value text);
create table roots(id integer primary key, path text unique, label text, uuid text, online int, last_scan int);
create table folders(id integer primary key, root_id int, rel text, mtime int, unique(root_id, rel));
create table artists(id integer primary key, name text, sort text);
create table albums(id integer primary key, title text, sort text, artist_id int, year int, art text,
                    unique(sort, artist_id));                    -- artist_id: album artist, else artist
create table genres(id integer primary key, name text, sort text);
create table tracks(id integer primary key, folder_id int, name text, mtime int, size int,
  title text, sort text, artist_id int, album_id int, genre_id int, track int, disc int, duration_ms int,
  codec text, rate int, bits int, channels int, kbps int, rg_tg real, rg_tp real, rg_ag real, rg_ap real,
  has_art int, unique(folder_id, name));
create table playlists(id integer primary key, path text unique, name text, mtime int, n int, ms int);
create table playlist_items(playlist_id int, pos int, track_id int, path text, primary key(playlist_id, pos));
create table stats(track_id integer primary key, plays int, skips int, last_played int, rating int);
create virtual table fts using fts5(title, artist, album, album_artist, genre, content='',
                                    tokenize='trigram remove_diacritics 1');
-- indexes: tracks(album_id, disc, track), tracks(artist_id), tracks(genre_id), tracks(sort),
--          albums(sort), albums(artist_id, year), artists(sort)
```

**Pragmas:**
- `journal_mode=WAL` with `synchronous=NORMAL`: consistent after a power cut, possibly without the last batch; the
  owner's boards do get unplugged.
- `secure_delete=OFF`: the build turns it on, doubling the writes of deletes [F option, I effect].
- `cache_size=-2048` where `ram_mb` ≤ 512, else `-8192`; `temp_store=MEMORY`.

**Measured on zero8, in memory** [F `sqlbench.py`; the synthetic strings are short]:

| Tracks | Insert (python) | DB without FTS | FTS5 trigram | 3-column `LIKE`, no match | Title `LIKE` | FTS match | Songs page at OFFSET 5000 | Album tracks |
|---|---|---|---|---|---|---|---|---|
| 1,000 | 0.13 s | 0.31 MB | 0.03 MB | 4.7 ms | 1.5 ms | 0.3 ms | 0.5 ms | 0.1 ms |
| 10,000 | 1.46 s | 2.78 MB | 1.16 MB | **59.8 ms** | 15.2 ms | **0.4 ms** | 2.6 ms | 0.1 ms |
| 30,000 | 4.53 s | 8.32 MB | 3.49 MB | **188.4 ms** | 45.1 ms | **0.5 ms** | 2.6 ms | 0.2 ms |

**Real libraries [I]:** names and paths 1.5-2x longer, so about 0.45-0.6 kB per track plus 0.2-0.3 kB of FTS.

| Tracks | Database | RAM | First scan, cold: 2.2 ms per file [F 059] / 3-6 ms real [I] | Rescan at 0.17 ms per file [F 059] |
|---|---|---|---|---|
| 1k | 0.7-0.9 MB | 2-3 MB page cache | 2.2 s / 3-6 s | 0.2 s |
| 10k | 7-9 MB | 2-3 MB | 22 s / 0.5-1 min | 1.7 s |
| 100k | 70-90 MB | 2-3 MB (8 MB on 4 GB boards) | 3.7 min / 5-10 min | 17 s |

**Tag reading:**
- Own readers only. Never ffprobe: 0.80 s per file, so 2.2 hours for 10k files [F 059 14.9].
- The P1 MP3 path reads covers too. [I] A 300 kB cover at zero8's 23.2 MB/s [F 059] adds 13 ms per file, or 2.2 min
  per 10k files. The scan mode skips picture frames by their size.
- MP4 already has its own reader [F]. APE, WavPack and Musepack get APEv2. WMA keeps only the file name in P2.
- The scanner never loads FFmpeg, which costs +22.5 MB RSS [F 059 14.6].

**Incremental scan:**
1. `readdir` per root (using `d_type`, no symlinks, a `(dev, ino)` loop guard, hidden folders skipped).
2. Per folder, compare `(name, mtime, size)` with `select ... where folder_id=?`. Memory stays bounded; a global path
   map would take about 12 MB at 100k tracks [I].
3. Read tags only for new or changed files. Delete a row only when its root is online and `stat` returns ENOENT.
4. One transaction every 500 files or 2 s, with the FTS rows inside it. [I] About 20 fsyncs per 10k files, 0.1-0.6 s
   on SD.

**Priority:**
- The scanner thread gets `setpriority(..., 19)` and `ioprio_set(IOPRIO_CLASS_IDLE)`.
- [F] The SD boards use `mq-deadline`; [I] it honours I/O priority classes since Linux 5.14.
- [F] The NVMe boards use `none` (no priority, but 393 MB/s).
- B39 checks for dropouts.

**Heat and drives that go away:**
- The scan pauses above 75 °C and resumes below 70 °C.
- It keeps going when the page is left (D27), and resumes at the next start after a quit.
- EIO or ENOENT under a root whose mount has left `/proc/self/mounts` marks the root offline: its rows are hidden, not
  deleted.
- A drive is matched again by its `/dev/disk/by-uuid` UUID (no root rights).
- Playback never waits for the database.

**Views:**
- Artists = album artists, else the track artist.
- Albums are keyed by sort key and album artist, so CD1/CD2 folders join.
- Songs come in pages of 40 (`LIMIT/OFFSET`, 2.6 ms at offset 5,000 [F]; a keyset cursor at 100k).
- Genres lead to albums, then tracks.
- All queries run on a worker (D-63): a cold SD page fault can take tens of ms [I].

**Search:**
- FTS5 trigram, contentless. [F zero8] "beyonce" finds "Beyoncé", "motley" finds "Mötley Crüe", "明けの" matches, case
  is ignored.
- [F] Queries under 3 characters match nothing ("艺术": 0 rows); they use an index range on the sort keys instead.
- 250 ms debounce, 50 results per section.
- `LIKE` alone would serve about 10k tracks; at 100k it would take about 630 ms [I].

**Sorting and CJK:**
- There is no ICU in SQLite [F]. `libicu76` is installed [F], but its versioned symbols (`ucol_open_76`) make dlopen
  fragile [I].
- So the sort keys are built in code:
  - Latin case and diacritics folded, a leading "The " moved;
  - numbers padded (P1's `natural_less`);
  - katakana folded to hiragana;
  - Hangul in code-point order, which is dictionary order [I];
  - Han in code-point order, not pinyin (ICU is a P3 option).
- Text draws with Noto Sans CJK (D-55).

**Album art:** embedded first, else a folder file. P1 caps decoding at 1,200 px (4.3 MB RGB).

| Side | RGB565 raw | JPEG q85 [I] | Use |
|---|---|---|---|
| 96 px | 18.4 kB | 3-5 kB | list thumbnails: **not in the canvas** (rows are text) |
| 240 px | 115 kB | 12-20 kB | sleeves of 148-224 px (480x320 to 640x480) |
| 480 px | 461 kB | 35-55 kB | 720x720 and large sleeves |

**Recommendation [I]:**
- cache one size per board (the sleeve side) as raw RGB565 in `~/.cache/vinyl/art/`, LRU-capped at 32 MB on SD and
  128 MB on NVMe;
- covers over 1,200 px get a TJpgDec scaled decode (baseline JPEG; progressive ones keep the drawn sleeve).

### 3b. Equalizer

**Filters:** 10 RBJ peaking biquads at the ISO octave centres, Q 1.41.
- Float samples, double coefficients and state: [I] the 31 Hz band's poles sit near the unit circle, where float32 state
  adds noise.
- A band at 0 dB is skipped; Off is a bypass.

**CPU on the A53 at 1 GHz [I]:**
- 10 bands x 2 channels x 44,100 = 882k biquad samples a second.
- At 12-16 cycles each that is **1.1-1.4 % of one core** in scalar code, 0.6-0.8 % with NEON.
- FFmpeg's ten filters measured +3.1 % [F 059 14.5].
- Scalar first; NEON only if B53 shows more than 2 %.

**Place:** decode → float → ReplayGain → **EQ** → crossfade mix → software volume (outputs without a mixer only) → boost
→ limiter → dither → S16/S32 → the sink, then the hardware or sink volume.
- The EQ always works before any volume.
- Headroom = −max(0, highest band) dB, applied before the filters, so boosts never clip. The lost loudness comes back
  with the volume or the boost.
- No preamp control in P2 [I].

**Presets [I]:** Flat, Bass boost, Bass cut, Treble boost, Vocal, Loudness, Rock, Pop, Jazz, Classical, Custom.

**Per output:** no. One EQ in P2 (D30); per-output profiles in P3.

**Memory:** 320 B of filter state and one 16 kB buffer.

### 3c. Crossfade

**How:**
1. When track A's decoder reaches total minus fade, the engine opens track B (already named by `set_next`).
2. It mixes the two with equal-power cos/sin curves.
3. B's segment starts where the mix starts, so "Started" is reported when B is heard.

**Conditions [I].** A crossfade happens only when all of these hold; otherwise P1's gapless change applies:
- the formats are equal (P1 reopens the stream on a rate change);
- A's length is known;
- the change is not a manual skip;
- repeat one is off;
- with "albums stay gapless" on (the default), the two tracks are on different albums.

**CPU [F 059 14.4]:** one more decoder during the fade only, in % of one Zero 2 W core: FLAC +0.65, MP3 +0.8,
Vorbis +2.1, Opus +2.6-3.6.
- Worst case, Opus through FFmpeg on both sides plus EQ: about 7 % batch, about 12 % with the real-time overhead
  (059 14.5) [I].
- So every board can crossfade; the draft's "needs a faster board" is contradicted (D29).

**Memory:**
- a second T1 decoder: under 1 MB (whole tools peaked at 2.3-2.6 MB [F 059]);
- FFmpeg is mapped once (+22.5 MB, not twice), plus a codec context [U, 0.5-2 MB];
- a 16 kB mix buffer.

**Seek during a fade:** the fade ends, and the side the seek lands in keeps playing.

**Default:** Off. Gapless is untouched when Off; with crossfade on, albums stay gapless.

### 3d. ReplayGain

| Source | Tags |
|---|---|
| ID3v2 | `TXXX:REPLAYGAIN_TRACK_GAIN/_PEAK`, `REPLAYGAIN_ALBUM_GAIN/_PEAK` (any case) |
| Vorbis, FLAC, Ogg FLAC | the same names as comments |
| Opus | `R128_TRACK_GAIN` / `R128_ALBUM_GAIN`: Q7.8 dB relative to −23 LUFS, so RG = value/256 + 5 dB [I, the −18 LUFS RG2 reference]. The header gain is already applied (`opus_decoder.cpp:49` [F]). |
| MP4 | `----:com.apple.iTunes:replaygain_*` (P1 reads `ilst`; freeform items to add) |
| APEv2 (P2b) | `REPLAYGAIN_*` |
| Not used | `iTunNORM`, the LAME header's radio gain |

**Modes:** Off, Track, Album (Album falls back to Track when there is no album gain). Default Track, as drawn. "Auto"
belongs to P3.

**Clipping:** gain = min(RG, −20·log10(peak)) when a peak tag exists. The limiter catches the rest whenever the net
digital gain is above 0 dB.

**Untagged files:** 0 dB [I].

**No analysis.** [I] An R128 scan decodes the whole file: about 2 s of CPU per 4-minute track on the Zero 2 W, about
6 hours for 10k tracks. Tags come from PC tools (rsgain, foobar2000).

**With the volume model:** ReplayGain is digital and sits before the output volume, so the levels and percents do not
move. Modern masters get −6 to −10 dB [I; 059's LOUD has a mean of −10.5 dBFS]. So ReplayGain plus a +4 dB boost
rarely ends above 0 dB net.

**Display:** NP-480x480 and NP-800x480 draw "Gapless, RG -3.2 dB". 480x320 and 640x480 are the coordinator's call.

### 3e. Software gain boost with limiter

**Gain:** 0 to +4 dB in 1 dB steps, at the end of the float chain, per output (D23). Stored under the
`output_volume_key`, as the software level is.

**Limiter [I]:**
- 5 ms look-ahead (220 frames at 44.1 kHz), linked stereo, a sliding-window peak (monotonic deque, amortised O(1));
- attack = look-ahead, release 150 ms;
- ceiling −1.0 dBFS sample peak, leaving room for the DAC's inter-sample overshoot.
- About 20-30 cycles per frame, so about 0.1 % of a Zero 2 W core; 2 kB of buffer.
- The 5 ms is added to the delay of the heard position.
- It runs only when the net digital gain is above 0 dB.

**Presentation (for the coordinator):**
- a Playback row such as "Gain boost: +2 dB (Headphones)", naming the output in use;
- offered on every output (a quiet Bluetooth speaker gains too);
- applied at every level: [I] a boost that started only at the top step would make that step jump 4 dB;
- whether the volume group marks an active boost: 5.2.

**The owner's case [F decisions.md]:** the Pi jack tops at +4.00 dB (raw 400) and is still much quieter than a USB DAC.
[I] With LOUD (peaks at 0.0 dBFS) the limiter takes up to 4 dB off the peaks, so a dense master gains 2-3 dB perceived
and a dynamic one the full 4 dB.

### 3f. "Volume steps" and the dB display

**The model:** the worktree already has the step parameter (never finer than min(1 dB, step) nor the control's own
step), `levels_db()` and `volume_level_text(m, level, VolumeUnit)` [F, uncommitted]. Still needed:
- the setting itself;
- rebuilding the model when it changes, keeping the nearest dB;
- the text.

**Level counts over a 60 dB window:** 30 (Normal), 61 (1 dB), 121 (0.5 dB), 601 (0.1 dB). So holding "-" or "+" must
repeat: after 0.5 s at 10 a second, at 30 a second after 2 s. D-35 allows a hold that repeats its own tap. The `-` and
`=` keys auto-repeat already.

**Text:**
- [I] "-22.5 dB" is about 70-80 px at 18 px.
- [F decisions.md and the NP drafts] The value boxes are 52 px (800x480), 60 px (640x480, 320x480), 68 px (480x480) and
  80 px (480x320) wide.
- So the dB mode needs a value-box design per class: drop " dB", a smaller font, or a wider box (5.2).
- One decimal is enough at 0.1 dB steps. [I] The worktree's "two decimals under 0.5 dB" would show "-22.40".

**Coarse controls:**
- ALSA direct: the native step wins. The Sharkoon stays at 1 dB; the Pi jack (0.01 dB) and the Apple adapter (0.5 dB)
  follow the setting.
- Sinks: continuous. [I] PipeWire splits a volume into a hardware part and a software part, so 0.1 dB works even on a
  1 dB card.
- Bluetooth: [I] the 128 AVRCP steps quantise the request, and the group shows the read-back nearest level (P1
  behaviour).

**With the boost:** whether "+4.0 dB" with a +4 dB boost reads "+8.0 dB" is for the coordinator (5.2).

### 3g. Sleep timer, positions, playlists, track actions, Track info

**Sleep timer:**
- a controller timer on `CLOCK_MONOTONIC`;
- minute modes: at T−10 s the engine gets `fade_out(10 s)`, a −60 dB ramp linear in dB, per sample in the float stage;
  then pause, and the gain is reset at the next Play;
- "This track": no next track is set, and the queue moves to the next track, paused;
- "This album or playlist": stops at an album change, or at the queue end for a playlist queue;
- it survives hiding (X-Background=run); on zero8 at 320x480 it runs while Vinyl is in front;
- not kept across a restart [I].

**Positions in long files:**
- files of 20 min or more, `.m4b`, or the genre Audiobook or Podcast;
- `positions.json`, LRU of 500, saved on pause, track change, exit and every 30 s;
- works without the database.

**Playlists:** M3U8 files in `~/Music/Playlists` (D26).
- Save the queue = its play order.
- Import resolves each entry against the source folder, rewrites it relative to the Playlists folder when on the same
  drive, and keeps missing entries, shown dimmed.
- Export writes `<name>.m3u8` to a chosen drive's root (relative paths) or to `~/Music` (5.2).

**Track actions:**
- as TrackActions-480x320: Play (Enter), Play next (N), Add to queue (A), Add to playlist (P), Track info (I);
- album and artist rows: Play all, Shuffle, Play next, Add to queue, Add to playlist;
- D-35 allows a hold only as the 3 s options box with its hint line and Ctrl+O (D31).

**Track info:** Info rows.
- Tags: title, artist, album, album artist, year, genre, track/disc.
- File: length, format, rate/bits, bitrate, file size, path.
- Playback: ReplayGain gains and peaks, plays, last played.

### 3h. AVRCP and MPRIS metadata

**What exists [F]:**
- On all five boards `bluetooth.conf` lets root send `org.mpris.MediaPlayer2.Player` and `org.bluez.MediaPlayer1`
  calls (lines 16 and 23).
- The user is in the `bluetooth` group, and `libsystemd0 257.13` is installed.
- Mesh Hop and mmWave Radar already dlopen sd-bus (`bluez_backend.cpp:81-124`).
- The XWF speaker's AVRCP node sends PLAYCD, PAUSECD, NEXT and PREVIOUS (059 13.6). The Evolve 65 has volume buttons
  only (B8).

**Library choice:**
- **sd-bus through dlopen:** proven in two apps; no build dependency; about 150 lines of table code plus about 400 for
  the object [I].
- libdbus: more code (its own main-loop glue).
- Raw protocol: no.

**What it needs:**
- `sd_bus_open_system`.
- `sd_bus_add_object` at `/org/vinyl/player`, answering Properties Get/GetAll and the Player interface:
  - PlaybackStatus; Metadata (`xesam:title`, `xesam:artist`, `xesam:album`, `mpris:length`); Position; the Can* flags;
    LoopStatus; Shuffle;
  - Play, Pause, PlayPause, Stop, Next, Previous.
- `org.bluez.Media1.RegisterPlayer` on each adapter, repeated after a bluetoothd restart (NameOwnerChanged).
- `PropertiesChanged` on track and state changes only; no CPU between events [I].

**Risks:**
1. [I, BlueZ design] With a player registered, passthrough keys arrive as method calls, not as uinput keys. Both paths
   must feed the one transport-press module.
2. Holds (the owner's scan ruling) need a press and a release, and MPRIS has neither: headset holds are lost.
3. Whether the keys reach uinput today (P0-c) is still [U].
4. With the owner's devices it brings no visible gain (no display). [I] The play status may make one-button headsets send
   the right command.

**Recommendation:** keep it as D10 says, last in P2, system bus only, 1.5-2 days. Move it to P3 if there is no device
with a display (D33).

---

## 4. Boards matrix for P2

**Boards on 2026-11-13 [F probes]:**

| Board | SoC / RAM / MemAvailable | Storage | Screen now | Sound | Launcher | FFmpeg libs |
|---|---|---|---|---|---|---|
| zero8 | Zero 2 W / 415 MB / 245 MB | SD, mq-deadline | 320x480 PDA, 6.3 px/mm | ALSA, Sharkoon DAC plugged | 0.10.1 | installed for tests |
| deck zero7 | Zero 2 W / 414 MB / 255 MB | SD | 640x480, 11.3 px/mm | ALSA, **no card plugged now** | 0.10.1 | installed for tests |
| Pi 3A+ | A53 1.4 GHz / 415 MB / 267 MB | SD | 480x800 portrait | jack (bcm2835) | **0.9.4** | installed for tests |
| Pi 4B | not reachable | | 800x480 [059] | jack | | |
| RPI52 | Pi 5 / 4 GB / 3.7 GB | NVMe | 480x800 portrait | PipeWire, HDMI only (`sound=0`) | 0.10.1 | installed (desktop image) |
| HackBerry | CM5 / 4 GB / 3.7 GB | NVMe | 720x720 | PipeWire, Bluetooth, USB DAC | 0.10.1 | installed |
| Tablet | amd64 / 3.8 GB [059] | SSD | 1280x800 | PulseAudio [059] | | [U] |

**Features by board [I]:**
- **Everywhere:** library and search, EQ, the limiter and boost, crossfade (default Off), the sleep timer, Volume steps,
  art.
- **Differences:**

| Feature | Zero 2 W / Pi 3A+ (SD, 415 MB) | Pi 5 / HackBerry (NVMe, 4 GB) | Tablet |
|---|---|---|---|
| SQLite cache, commit batch | 2 MB, 500 files | 8 MB, 1,000 files | 8 MB |
| Sleep timer while hidden | deck and Pi 3A+ yes; zero8 at 320x480 no (no multitasking) | yes | yes |
| AVRCP / MPRIS | no (`bt_audio=none`) | yes (`bt_audio=pipewire`) | [U] PulseAudio |
| Scan on USB plug | only drives already mounted: Lite has no automount (`/media` empty, no gvfs [F]) | yes, gvfs automounts to `/media/osrde` [F] | [U] |
| Cover cache cap | 32 MB | 128 MB | 128 MB |
| T2 formats | if FFmpeg is installed (+22.5 MB RSS) | installed | [U] |

**Memory budget on a Zero 2 W [I unless marked]:**

| Part | RSS |
|---|---|
| Base without FFmpeg: LVGL, mapped fonts, framebuffers (320x480 at 16 bpp, or 640x480 at 32 bpp), sleeve images | [U], estimated 15-22 MB |
| FFmpeg once loaded | +22.5 MB [F 059] |
| libsqlite3 mapped / page cache | +0.3 MB [F 059] / ≤ 2 MB |
| Scanner buffer (scan mode) / DSP buffers | ≤ 64 kB / < 0.1 MB |
| Second decoder during a crossfade | < 1 MB (T1), 0.5-2 MB (FFmpeg) |
| Cover decode, transient | ≤ 4.3 MB at the 1,200 px cap |
| Tag RAM cache (P1: up to 20,000 entries, about 4-6 MB) | about 500 entries with the database: < 0.2 MB |

**HackBerry reference [F]** (Vinyl 0.1.0 running for 41 min, FFmpeg loaded): RSS 59.2 MB, PSS 42.1 MB, private
anonymous 16 MB; libavcodec 3.7 MB, libavformat 2.8 MB, the binary 2.4 MB, `/dev/fb0` 2.0 MB, Noto Sans CJK 0.9 MB;
2.5 % CPU on average.

**Minimum guarantees [I targets; B36, B45]:**
- Zero 2 W: under 25 MB RSS without FFmpeg, under 50 MB with it.
- No dropout while scanning.
- Search under 0.5 s at 10k tracks.
- A first scan of 1k files under 30 s.

---

## 5. Menu and Library UX hooks

### 5.1 Canvas screens per step

| Step | Boards implemented |
|---|---|
| P2a | The Menu key on NP-480x320, NP-320x480, NP-480x480, NP-800x480 and NP-640x480; Menu-480x320 and Menu-320x480; SleepTimer-480x320; Playback-480x320 (its rows change, see 5.2). |
| P2b | Library-480x320 and Library-320x480; Search-480x320; TrackActions-480x320; LibrarySettings-480x320. |
| P2c | Playlists-480x320, PlaylistEdit-480x320, SavePlaylist-480x320. |
| P2d | EQ-480x320, EQ-320x480. |
| P2e | none drawn. |

**A size rule to confirm.** P1 already draws the 640x480 list pages as the 480x320 drawing scaled 4/3 [F decisions.md].
If the coordinator extends that rule to every new list page, and scales the 320x480 drawing for 480x640 and 480x800,
those sizes need no new boards. Square and large still need a ruling: full screen, or the right pane of the large
layout.

### 5.2 Not drawn yet: needed before each step starts

"Rule" means covered by the scaling rule above.

| # | Missing | Step | Boards |
|---|---|---|---|
| 1 | About and keys (version, licences, key table) | P2a | 1 + rule |
| 2 | Playback as built: Gain boost (names the output), Volume steps, ReplayGain choices; "Speed (later)" removed; "Gapless On" kept as Info or dropped (D32) | P2a | 1 + rule |
| 3 | The choice picker for every setting (one pattern, the chosen row marked) | P2a | 2 |
| 4 | The volume group in dB mode (value box 52-80 px), and whether a boost is marked | P2a | 5 |
| 5 | A running sleep timer on now playing (the sleep key's mark?) | P2a | 3-5 |
| 6 | Menu on square and large | P2a | 2 |
| 7 | Artists, artist, Albums, album, Songs, Genres pages (row contents, headings) | P2b | 2 + rule |
| 8 | Track info | P2b | 1 + rule |
| 9 | Empty states: no music, no results, drive offline, no library yet | P2b | 2 |
| 10 | Scan progress at each size; "paused: the board is hot"; scan errors | P2b | 2 + rule |
| 11 | Music folders page (list, add through the browser's choose mode, remove) | P2b | 1 + rule |
| 12 | Search at 320x480, with the writing band at 480x800 and 480x640 (D-41), and with the on-screen keyboard (D-38, D-39) | P2b | 3 |
| 13 | The 3 s hold hint line for Track actions (D-35); the sheet at 320x480 | P2b | 2 |
| 14 | The album and artist actions sheet | P2b | 1 |
| 15 | Add-to-playlist picker, rename, delete question, import action, export target, dimmed missing entries, empty list | P2c | 5-6 + rule |
| 16 | EQ at 640x480, 480x480/720x720, 800x480 and 480x800; the preset picker; EQ Off | P2d | 5 |
| 17 | Crossfade picker with "albums stay gapless" | P2d | 1 |
| 18 | Notices: "Drive removed. Paused.", "3 files could not be read", "Playlist saved", "Not enough space" | P2b-P2e | 1 |

**Total: about 40-45 boards, or about 25 with the scaling rule.**

---

## 6. Tests, design QA gates, release plan

### 6.1 Tests per step

| Step | Unit tests | Scenes | Board tests |
|---|---|---|---|
| P2a | dsp, replaygain, sleep, positions, settings, state_split, volume steps | qa-menu, qa-menu-small, np-db, lifecycle | B29-B36 |
| P2b | sortkey, scanner, library, search, tags_scan | qa-library(-small), qa-empty-library, qa-cjk-library, keyboard, portrait-band | B37-B45 |
| P2c | m3u, playlists | qa-playlists(-small), keyboard | B46-B50 |
| P2d | eq, crossfade, `--bench-dsp` | qa-eq(-small) | B51-B55 |
| P2e | mpris (fake bus), mount_watch, folder_art, cover_cache | qa-notices | B56-B61 |

All suites stay `-Werror`, with the ASan/UBSan and TSan builds (`SAN=` in `tests/Makefile`). The real-library fixtures
run in WSL as in P1.

### 6.2 Design QA items most at risk

| Item | Risk in P2 | Mitigation |
|---|---|---|
| D-01 scene set | About 14 new pages x 7 sizes, plus 480x320 and 320x480 at `ppmm 630` | Split scenes per step from the first build. |
| D-33 touch | EQ at 320x480: 10 bands in about 292 px = 28 px = 4.4 mm; the volume group in dB mode | List each under R-4 with its size; bands also by keys. |
| D-35 hold-only | "A hold opens Track actions" | The 3 s box with its hint line and Ctrl+O; letter keys (D31). |
| D-29 keys | EQ page Left/Right | An owner exception with the owner's words in decisions.md (D30). |
| D-32 letters | E (EQ) and T (timer): "W, E, R, T share evdev codes with the arrows" | Physical key codes. |
| D-38 to D-41 text fields | Search, Save, Rename | `TextField`, `osk_policy`, the keyboard and portrait-band scenes. D-40: the keyboard rule's state as a row in Library settings or About. |
| D-45 affordance | Every new list | One list view: Menu rows Edit; choices Action with the chosen one ActionRunning; Info; hub rows Open. |
| D-81 dead controls | Rows of later steps, "Speed (later)", the sleep key until P2a | Show built rows only (D32). |
| D-55 CJK | Library lists, search, playlists, Track info | The P1 fallback; `qa-cjk-library`. |
| D-62 / D-63 | No SQLite, drive removed, scan errors; slow work | Fix texts; workers; progress with Esc. |
| D-66 to D-69 | Timer and scan while hidden | `lifecycle`; save on close. |

### 6.3 Release plan

**Publish 0.1.0 before P2: yes (D21).**
- P1 is complete, ran B1-B28 on six boards, and stands on its own.
- It exercises the registry flow with the arm64 and amd64 `.deb`s before the big releases.
- **Preconditions:**
  1. D20 applied;
  2. design QA run 4 READY;
  3. the uncommitted volume work committed;
  4. the package built with `make_registry.py --only vinyl --out <folder>`, `deb-info.txt` handed to QA, then
     published with `--no-build`;
  5. a pull request to the apps `main`;
  6. the ROADMAP row updated.
- 0.2.0 must read 0.1.0's `state.json`.

**Versions:**
- 0.2.0 (P2a);
- 0.3.0 (P2b, adds `libsqlite3-0` to `depends`);
- 0.4.0 (P2c);
- 0.5.0 (P2d);
- 0.6.0 (P2e, adds `libsystemd0`).

Each library is named in the README credits and the `copyright` file (D-76).

**Recommends, answered [F code and config]:**
- The installer (`appstore_native.cpp:1803-1828`) reads `Pre-Depends` and `Depends` with `dpkg-deb -f` and checks them
  with `dpkg-checkbuilddeps -d`.
  - Met: it runs `dpkg --install`, so no Recommends are installed.
  - Not met: it runs `apt-get -y install <deb>` without `--no-install-recommends`.
- Every probed board has `APT::Install-Recommends "1"`.
- The upgrade helper (`cyberdeckzero-update:1010-1089`) and `install.sh:1488` do pass `--no-install-recommends`.
- [I] So a fresh Lite board without libmpg123 and libvorbisfile gets FFmpeg plus its recommends: about 110 packages,
  462 MB installed, a 120 MB download (059 13.3). A board that has the small decoders gets no FFmpeg at all.

**D20:**
- The FFmpeg libraries go to `suggests`. The tools know only `--recommends` (`build_deb.py:193, 231`); adding a
  `--suggests` field is about 10 lines.
- The tier-2 notice gives `sudo apt install --no-install-recommends libavformat61 libavcodec61 libswresample5`. Its
  current "Install them from Settings, Apps" cannot work: Settings > Apps installs registry apps, not Debian libraries
  [I].
- Optional: a generic launcher task adding `--no-install-recommends` to the installer's apt path. Mesh Hop and mmWave
  Radar recommend `bluez`, which every board has [F].

---

## 7. Risks, open questions, licences

### 7.1 Risks

| Risk | Likelihood / impact | Mitigation |
|---|---|---|
| A 462 MB FFmpeg pull through Recommends | certain on the apt path [F] / disk, minutes | D20 before 0.1.0 |
| The whole queue written every 5 s | certain [F] / SD wear (≈320 MB an hour at 5,000 items [I]) | the state split in P2a |
| ID3 covers read during the scan | certain for files with covers [F] / slow first scan | scan mode (P2b) |
| Scan I/O starving playback on SD | low with idle I/O [I] / dropouts | `ioprio` IDLE (mq-deadline [F]); B39 |
| Zero 2 W RSS over 25 MB with FFmpeg | certain for T2 files [F 059] | on-demand loading (P1); README |
| Track actions hold breaks D-35 | high if drawn as "a hold" | D31 |
| EQ keys against D-29 | certain without a ruling | D30 |
| dB text overflows the value box | certain at 52-68 px | 5.2 #4 |
| No USB automount on Lite | certain [F] | D28; a launcher automount item |
| AVRCP registration takes keys off uinput | certain once registered [I] | one transport path; D33 |
| FTS5 misses 1-2 character queries | certain [F] | prefix fallback |
| Uncommitted volume work in the worktree | present [F] | merge before P2a |
| Pi 3A+ still on launcher 0.9.4 | present [F] | update it before its P2 tests |

### 7.2 Open questions

1. Does the scaling rule extend to all new list pages, and where do the Menu pages go on the large layout? (coordinator)
2. Where does Export M3U write: a chosen drive's root, or `~/Music`? (owner)
3. Does the owner have a car unit, headset or speaker with a display? (D33)
4. Should the scan also run while Vinyl is hidden and paused? [I: yes, at idle priority]
5. Is a preamp row wanted for ReplayGain? [I: not in P2]
6. Pi 4B: RAM and state not probed.

### 7.3 Licences, patents, privacy

| Component | Licence | Use | Step |
|---|---|---|---|
| SQLite (`libsqlite3-0`) | public domain | dlopen, Depends | P2b |
| libsystemd (sd-bus) | LGPL-2.1-or-later | dlopen, Depends | P2e |
| Own DSP (RBJ cookbook formulas, limiter, crossfade) | MIT (Vinyl) | built in | P2a, P2d |
| Cassette `dsp/` pieces | MIT (same author) | copied, origin noted | P2a |
| TJpgDec (ChaN) | BSD-style permissive [I: keep its notice] | vendored, listed in `copyright` | P2e |
| stb_image 2.30 | public domain or MIT | already built in | P1 |

- **DSP:** no new library; all own code.
- **Patents:** none expected for biquad EQ, look-ahead limiting, crossfading, M3U or ReplayGain [I]. MP3's US patents
  ended in 2017 (059 8.2). AAC stays FFmpeg's.
- **Privacy:** the database, play counts, positions and playlists stay on the board. No network and no scrobbling in
  P2. Deleting `~/.local/share/vinyl` and `~/.cache/vinyl` resets them; the README file table must grow.

---

## 8. P3 and later (ordered)

1. Extract a shared `apps/_audio` with Cassette (059 D6).
2. Lyrics: USLT and `.lrc` synced display (a tap on the cover, per the map).
3. CUE sheets.
4. Visualiser: the Cassette VU first, then a spectrum (kissfft, BSD-3).
5. Internet radio (FFmpeg avio, or libcurl with ICY).
6. Audiobooks and podcasts: M4B chapters, speed with pitch kept.
7. A-B loop.
8. Smart playlists (recently added, most played).
9. Scrobbling (ListenBrainz, opt-in).
10. Per-output EQ profiles, ReplayGain Auto, ICU collation, an ASF tag reader.
11. Launcher items L1-L3 of 059, and USB automount on Lite images.

---

## 9. Not verified

- **Boards not probed:** the Pi 4B (unreachable). The tablet and RPI53 were not in the host list or were offline.
- **Vinyl's RSS on a Zero 2 W:** it was not running on zero8, the deck or the Pi 3A+. B36 measures it.
- **Real libraries:** database size, scan time with big covers, and EQ and limiter CPU on the A53 are [I] until B37, B45
  and B53.
- **System behaviour:** mq-deadline's handling of I/O priority classes on 6.18, and PipeWire's hardware/software volume
  split, are [I].
- **AVRCP:** delivery of the speaker's keys to uinput (P0-c) is [U]; BlueZ's forwarding to a registered player is [I].
- **The Settings > Apps install of the Vinyl `.deb` itself** on a board without the small decoders was not simulated
  (all probed boards now have them). The 462 MB is 059's simulation of the FFmpeg libraries with Recommends.

## Sources

- **Launcher** `main` @ 0d4baacc:
  - `projects/AppStore/main/backend/appstore_native.cpp:1792-1840`;
  - `projects/APPLaunch/main/ui/settings/apps_backend.cpp:82-95, 418-434`;
  - `projects/APPLaunch/pizero2w/cyberdeckzero-update:1010-1089` and `install.sh:1488`;
  - `ext_components/cp0_lvgl/src/map/cp0_map_sqlite.c`;
  - `docs/OPEN_SOURCE_COMPONENTS.md:84`;
  - the checklists and docs listed at the top.
- **Apps worktree** `apps-vinyl` @ 5591e26, plus its uncommitted changes:
  - `apps/vinyl/{app.json, README.md, docs/decisions.md, tests/}`;
  - `src/core/{engine, decode, volume, queue, library, tags, state, art, codec}`;
  - `src/ui/vinyl_app.cpp:1480-1556`;
  - Cassette `dsp/limiter.hpp`;
  - Mesh Hop `bluez_backend.cpp:1-124`;
  - `tools/build_deb.py:193, 231`;
  - `docs/APP-JSON.md:41-67`.
- **Canvas:** scratchpad `vinyl-ux/project/*.dc.html`.
- **Probes:** scratchpad `060-tools/` (`out-*.txt`, `sqlbench.py`, `fts2.py`), and the HackBerry memory map.
- **Report 059:** sections 1.4, 1.5, 3.6, 4, 8.2, 13 and 14.


---

## 15. Owner decisions on this plan (2026-11-13)

Taken in the conversation after the report was written. Where an answer changed the plan, this section wins over the sections above.

| # | Decision | Owner's answer |
|---|---|---|
| D20 | FFmpeg packaging | Changed: the FFmpeg libraries are a hard `Depends` (not Suggests): "let's make it universal and add FFmpeg as a dependency the route to optional, suggested is complicating the thing, let's make it work with all most common codecs out of the box". Built in Vinyl 0.1.0 (commit 22b68e2). With it: the launcher's app installer must stop installing recommends (owner: "yes do the installer fix"; launcher change, branch `installer-no-recommends`, ships with launcher 0.10.2). |
| D21 | Publish 0.1.0 before P2 | OK |
| D22 | Five steps P2a-P2e, 21-27 worker-days | OK |
| D23 | Gain boost per output with a look-ahead limiter | OK |
| D24 | "Volume steps": one global setting, never finer than the output's own step, hold to repeat | Explained to the owner, then OK (the global setting applies to every output; an output whose hardware step is coarser keeps its own step; hold to repeat makes fine steps usable) |
| D25 | ReplayGain from tags only (Off, Track, Album; default Track) | OK |
| D26 | Playlists are M3U8 files in `~/Music/Playlists` | OK |
| D27 | Scan keeps running after leaving the Library | **No.** "d27 no we need to have SD wear in mind as you shown so scan at launch and when opening a folder from the library but not out of the library." So: the scan runs at app launch and when a folder is opened from the Library; it stops when the user leaves the Library; no periodic or background scans; database writes are batched per folder. |
| D28 | Vinyl never mounts drives | **Changed.** "vinyl should show drives and offer to mount them if they are not already, meaning unmount the drives vinyl mounted at exit and offer an unmount 'eject' option." So: Vinyl lists removable drives, offers to mount an unmounted one, unmounts at exit only the drives it mounted itself, and offers an Eject action. Needs a study of how an app may mount (udisks2/polkit on Lite images, or a launcher capability): part of P2e, to be designed before it starts. |
| D29 | Crossfade on every board, default Off, never inside an album | OK |
| D30 | One EQ for all outputs; arrow keys on the EQ page | The exception meant is the page-arrow rule (D-29: Left goes back, Right acts like Enter), not the drag rule: the EQ page needs Left/Right to choose a band and Up/Down to change its gain, as Now playing needed its transport arrows. The EQ page has no dragging. Accepted with that explanation (owner asked what the exception was). |
| D31 | Track actions by the 3 s hold, Ctrl+O and letter keys | OK |
| D32 | No dimmed rows for unbuilt features | OK |
| D33 | AVRCP/MPRIS metadata | Stays in P2 (P2e): "yes I have in my car" (a car stereo that shows the track title). |

New items asked for in the same message: a **music server connection study** (report 061, to be scheduled: Subsonic/OpenSubsonic, Jellyfin, DLNA/UPnP, MPD, SMB/NFS shares; streaming, caching, SD wear, credentials) and a **removable media mounting study** inside P2e (D28).
