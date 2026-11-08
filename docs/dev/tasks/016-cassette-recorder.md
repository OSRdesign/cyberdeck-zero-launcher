# 016 - Cassette: the sound recorder app (build plan, draft brief)

Status: DRAFT 2026-11-08, waiting for the owner's answers in "Questions for the owner". Inputs: report 058 (`reports/058-sound-recorder-study.md`:
decisions D1-D19 accepted on 2026-11-08, and the P0 results measured on the zero8 and the HackBerry), the design canvas (cassette
option A: recording, settings and recordings-list screens at 480x320 and 320x480, plus the variant for a card without a level
control), and the owner's rules below.
Owner: apps-dev (the app lives in the apps repo, `apps/cassette`). Verifier: the Controller-led protocol (the owner tests on the
zero8 or the deck, and on the HackBerry). Docs: docs-writer after acceptance.

## Rules that stay
- Name **Cassette**, id `cassette`, MIT, native fullscreen LVGL app on `cp0_lvgl`, category to confirm (Media if the Store has it, else Tools). No manufacturer name, logo or
  artwork anywhere. Everything is drawn from scratch.
- `core/` is LVGL-free and unit-tested on a PC; the UI is checked by headless scenes (the mmWave Radar and Deck Atlas pattern:
  `build.sh host`, `tests/scenes`, PNGs in the build folder). Every phase ends with a hand-off report and a numbered board
  test protocol for the owner.
- **No background recording** for now (D10): the app owns the screen while it records; leaving it stops the take safely.
  `X-Background` is not set. Revisit when multitasking is switched on.
- The recordings are private. Test fixtures are synthetic (generated tones, noise, hand-built truncated files). The P0 recordings
  in `sound-recorder-work` are never fixtures and never leave the PC.
- Screen sizes 480x320 and 320x480 (portrait) first, 640x480 gets extra room; `X-MinSize=480x320`, `X-Portrait=320x480`. The
  header, Esc and the narrow shell come from the toolkit; no hand-made narrow code (the mmWave Esc lesson).
- Text entry (take prefix, rename) follows the keyboard policy: a Bluetooth or USB keyboard first; the automatic fallback keyboard
  only when none is connected.
- Passwords, keys and tokens never appear in files. No regional rules anywhere.

## Architecture (decided in report 058; this is the shape the tasks build)
`core/` (no LVGL, one file pair per unit, a fake for every system call so each unit runs on a PC):
- `alsa_sys`: libasound through `dlopen` (the house pattern). Enumerates capture cards, finds a card by name or `/dev/snd/by-id`
  (never by index: the index changes across reboots and boards), lists the rates and formats the card offers, opens `hw:` first
  and falls back to the sound server on `EBUSY` (D9), classifies read errors (`File descriptor in bad state` = the card was
  unplugged, xrun = overrun), reads and sets the card's mixer controls (capture volume, capture switch, auto gain) only when
  the card has them.
- `ring`: single-producer single-consumer ring, at least 8 s of audio (about 750 KB at 48 kHz mono S16).
- `capture`: the capture thread. Period 1024 frames, buffer at least 500 ms, no real-time priority (not available), counts
  overruns and the frames lost, fills the ring, feeds the meter. Never blocks on the file system.
- `writer`: the writer thread. Drains the ring in 64-256 KB blocks, `fdatasync` every 10 s (setting 5-30 s), writes a BWF
  `bext` chunk and a padded iXML chunk before the audio, rewrites the header sizes after each sync, switches to RF64 or
  auto-splits at 4 GiB, checks free space (`statvfs`) and stops cleanly with a reason, keeps a journal file per take.
- `repair`: on launch, finds takes whose journal is still open and rewrites the real header sizes (the audio is intact: P0 showed
  plain recording loses the unsynced tail only, and the header keeps its placeholder), then marks the take "repaired".
- `meter`: peak and RMS per period, peak hold, the 15-step bar value, the PEAK lamp, the target zone (-18 to -6 dBFS), at 30 Hz.
- `gain`: software gain 0 to +24 dB on top of the card's level, clip counter, DC blocker on; high-pass, limiter and the card's auto
  gain are not applied unless the user asks (D18: the app shows the card's own controls and leaves them as found).
- `takes`: file names `<prefix>_<take>_<date>-<time>.wav` (D15), a take counter that never repeats, `nodate` until the clock has
  been set, the folder `~/Recordings` (D14), list, rename, delete, size and length from the file.
- `edit` (owner request 2026-11-08, replaces the planned "copy to"): **trim** (cut the start and the end of a take) and **split**
  (cut one take into two or more at chosen points). Sample-accurate and lossless: the audio bytes are copied without decoding
  (WAV now; FLAC and Opus later cut at frame boundaries), the BWF `bext` and iXML chunks are rewritten for each result (new
  length, the time reference moves with the cut). The result is written to a temporary file, synced, then renamed into place; the
  original is kept unless the user chooses "replace", and a crash at any point leaves either the original or the finished result,
  never half a file. A peak overview of the take (about 2 values per screen pixel, built once per take and cached next to it)
  feeds the trim page's waveform. Every result is a new take with its own number (D15).
- `settings`: per-card remembered level and auto-gain choice, re-applied at every start (the mixer resets at reboot), format,
  folder, prefix, sync interval, auto-split.
- `playback`: WAV playback of the app's own takes through ALSA (the card's playback side when it has one, else the default).
`ui/`: the three pages of the canvas (recording, settings, recordings list), the cassette widget (two hubs turning counterclockwise,
no tape drawn), the LED bar, counter, five keys, the CARD and SOFT steppers. The CARD stepper and the settings rows Input level and
Auto gain exist only when the card has those controls; without them SOFT takes the full width.

## Tasks
Tier names are the savvy-flow workers. "Check" is what runs on a PC without a board.

| # | Task | Tier | After | Check (PC, no board) |
|---|---|---|---|---|
| T1 | `ring`, `writer`, BWF/RF64 header, journal, sync policy, `repair` | careful | - | unit tests: truncated and killed-writer files (a forked writer killed with SIGKILL at random points, then repaired), placeholder header repair, the 4 GiB boundary on a mocked size, block and sync cadence on a fake clock, free-space stop |
| T2 | `alsa_sys`, `capture`, error classes, mixer discovery, `cassette-probe` (a CLI that lists cards, controls and tries `hw:` then the server) | careful | - | fake ALSA: EBUSY fallback, unplug mid-read, xrun counting, a card without a level control, two cards, index change; every case scripted |
| T3 | `meter`, `gain`, DC blocker, target-zone logic | medium | T1 interface | synthetic tones and noise: dBFS accuracy within 0.1 dB, peak hold, clip count, a weak input shows below the target |
| T4 | `takes`, `settings`, file naming, rename, delete, list | medium | T1 | naming and `nodate` rules, counter never repeats after delete, per-card settings round trip, folder with a damaged file |
| T5 | `playback` (WAV, seek, pause), the five-key state machine | medium | T1, T2 | state machine table test (REC, PAUSE, STOP, PLAY, REW from every state), playback of a generated file on a fake sink |
| T5b | `edit`: trim and split of WAV takes, the peak overview, replace or keep the original, crash-safe write | careful | T1, T4 | unit tests: a trim and a split byte-compared against a reference built from the source samples (sample-exact at the edges), `bext`/iXML rewritten, the 4 GiB boundary, a writer killed at random points leaves the original or the result only, a damaged source is refused, disk-full stops cleanly |
| T6 | `ui/`: the four pages (recording, settings, list, trim and split), the cassette widget, the CARD/SOFT steppers, scenes at 480x320, 320x480, 640x480, the no-card-control variant | heavy | T3, T4, T5, T5b | headless scenes compared to the canvas boards by the orchestrator; no overlap at either narrow size; animation cost measured on the PC harness and budgeted for the Zero 2 W |
| T7 | packaging: `app.json`, `.desktop`, icon, `build.sh`, README with scenes, amd64 build | medium | T6 | package files test, arm64 and amd64 builds, `check-branding` |
| T8 | board test protocol and a board self-check mode | light | T7 | the protocol text, reviewed by the orchestrator |

T1 and T2 run in parallel, the rest follow the table. The orchestrator does the design review of T6 (the design stays mine) and the
icon, and merges nothing before the owner's board tests.

## Board tests for the owner (protocol written in T8)
B1 zero8 or deck: 10-minute recording with the card, then stop, list, play back. B2 unplug the card during a recording: the take
keeps its audio, the app says what happened, the card comes back on replug. B3 power pull during a recording on the Zero 2 W: after
boot the app finds the take, repairs it, and at most the last sync interval of audio is missing. B4 CPU and temperature while
recording with the screen animated (the cassette hubs). B5 HackBerry: the card held by the sound server, the app falls back to the
server and says so. B6 zero8 in portrait. B7 a card without a level control (the owner's other card, if one is available). B8 trim a take and split
another one, play the results, check the originals are kept, and pull the power during a save (the original or the result survives).

## Phase 2 (0.2) - after the 0.1 board tests
FLAC and Ogg Opus as settings (libFLAC, libopusenc loaded at run time, D4/D7); Vorbis comments; stereo music encoder timing and
encode-while-capturing on the zero8; background recording once multitasking is switched on; hot-plug
re-open without leaving the screen; trim and split for FLAC and Opus takes.

## Questions for the owner
1. On the recording screen, what do the keys do? Proposed: REC starts a take (latched), PAUSE pauses, STOP ends it and keeps the
   take, PLAY plays the last take, REW rewinds the take being played (hold = faster). The recordings list plays any take, and has RENAME, TRIM (opens the trim and split page) and DELETE.
2. Playback output: the card's own playback side (headset earphones) when it has one, else the default output: agreed?
3. Rename: the take's name is edited on the list screen with the keyboard policy above (a keyboard if connected, else the fallback
   keyboard). OK?
4. Cap: a take is limited by free space only; RF64 past 4 GiB (D3). Is an hours-long take acceptable on the SD card's wear, given the
   figures in the report (about 345.6 MB per hour at 48 kHz mono)?
5. Icon: a cassette in the launcher's amber on dark, drawn by the orchestrator, unless the owner wants another idea.
6. Trim and split: the original is kept by default and the result is a new take; "replace" (the result takes the original's place) is a
   second choice with a confirmation box. Agreed? And a split at several points at once, or one point at a time?
