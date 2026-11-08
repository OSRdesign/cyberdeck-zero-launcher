# 058 - Sound recorder app (study)

Date 2026-11-08. Research and design only: no product code, no app, no board touched, nothing installed or run on a
board. The owner's request (his words): "next app I'd like is a sound recorder. It will use any sound input hardware
connected to our board, mainly cheap USB soundcards or USB-C headsets. Start a study on what sound format we could use,
even if not MIT." This report gives the facts on the capture stack, the hardware, audio quality, file formats,
licences, an architecture and a phased plan with costs, and a probe to run on a board. The look the owner wants
(inspired by a classic portable tape recorder) is the controller's design work; this report only states the technical
constraints the UI must respect (5.9) and a short trademark note (6.5).

**Update 2026-11-08 (same day).** The owner accepted D1-D19 with changes (section 1): the app is **Cassette**
(`cassette`), it records only while in front (no background recording for now), and it shows the card's own gain and
AGC controls instead of forcing AGC off. Probe P0 ran on the zero8 and the HackBerry the same day: results in "P0
results" after section 1; the UI direction chosen is in "UI direction". The rest of the study text is kept as written,
with notes where P0 changed it.

**What I read.**
- Launcher `github/main` @ `28817a50` (2026-11-07): reports 054, 056 and 057 (format; 057 from branch `study-reticulum`),
  report 035 (board facts and launcher requests), `docs/APP-CONTRACT.md` (sections 5c, 13, 14),
  `projects/APPLaunch/pizero2w/README.md` (sound per board, desktop variant) and `install.sh` (audio group, WirePlumber
  drop-in), `projects/APPLaunch/main/ui/board_facts.cpp`, `projects/APPLaunch/main/ui/multitask/multitask_model.hpp`
  (close and Esc-hold timings), `ext_components/cp0_lvgl/src/cp0_capabilities.c` (sound detection),
  `src/cp0/cp0_audio_capture.cpp`, `src/cp0/cp0_lvgl_soundcard.cpp`, `ext_components/Miniaudio` (vendored miniaudio
  0.11.25), the cross sysroot `SDK/github_source/static_lib_v0.0.7` (which audio libraries it holds).
- Apps `origin/main` @ `906cbf7` (2026-11-08): `docs/APP-GUIDELINES.md` (sections 0, 3, 4, 5, 6, 7, 12, 14),
  `apps/_common/README.md` (shared build: no per-app libraries), `apps/mmwave-radar` (`app.json`, `src/core/port_sys.hpp`:
  the injectable `Sys` seam and fakes), Home Assistant `README.md` (one worker thread, `dlopen("libcurl.so.4")`), Mesh Hop
  `docs/BLE.md` (`dlopen("libsystemd.so.0")` instead of a per-app link), Deck Atlas `src/ui/worker.*`.
- Upstream and standards, read 2026-11-08 (Sources): alsa-lib PCM and plugin documentation, Linux 7.4-rc1
  `sound/usb/{mixer,proc,card}.c`, systemd v262 udev rules, Raspberry Pi documentation (power, USB) and pi-gen, the
  C-Media CM108AH datasheet Rev 2.1, the StarTech USBCAUDIO2 datasheet, miniaudio, libsndfile, FLAC (RFC 9639), Ogg Opus
  (RFC 7845), Opus (RFC 6716), libopusenc and libFLAC headers, FFmpeg `wavdec.c`, Debian trixie package pages and
  `copyright` files, EBU R 68, EBU Tech 3285/3306/3341, ITU-R BS.1770, the Opus, Via LA and Vectis licensing pages,
  Fraunhofer's mp3 page, the Android media formats page, WirePlumber 0.5.19 documentation.

**Markers.** **V** = read in a cited source or in our code; **I** = inferred or estimated by me; **U** = unverified: no
source found, or not checkable without a board (collected in section 10). Nothing was measured for the original
study; the P0 section holds the later board measurements (V there means measured on the board). **Cost
unit:** one worker session (ws), about 0.4 M tokens (as in reports 054, 056 and 057).

## 0. Summary

**Verdict: feasible as one native `cp0_lvgl` app, MIT, that captures through ALSA, writes a crash-safe Broadcast WAV
file as its primary format, and offers FLAC (lossless) and Ogg Opus (small) as options, all with permissive or LGPL
libraries that Debian 13 already ships.** Nothing needs GPL code inside the app; AAC is the only common format that
would need a non-free or GPL path, and it is not needed.

1. **The boards' audio stack (V, I, U).** Our appliance boards (deck, zero8, Pi 3A+, Pi 4B) most likely run Raspberry
   Pi OS Lite on Debian 13 (I): no sound server, plain ALSA (Lite has no PipeWire: V for Bookworm, U for trixie). The Zero 2 W has no
   audio of its own; every input is a USB card. Desktop boards (Pi 5, HackBerry) run Pi OS desktop, where PipeWire
   replaced PulseAudio in 2023 (V); the FZ-M1 tablet's XFCE desktop installs PulseAudio (V, our README). The first Pi OS
   user is in the `audio` group (V, pi-gen), so `/dev/snd` opens without a session.
2. **Capture API (recommendation).** ALSA directly, through `libasound.so.2` opened at run time (`dlopen`, the house
   pattern for libcurl and libsystemd): no change to the shared build, `Depends: libasound2t64` (LGPL-2.1+, V). Direct
   ALSA gives what a recorder needs and miniaudio hides: the xrun count (miniaudio recovers overruns silently, V), the
   device's native formats, `-ENODEV` on unplug, period and buffer sizes. Mixer controls through the same library.
   miniaudio (public domain or MIT-0, V; already vendored in the launcher) stays useful for its decoders in playback.
3. **The hardware (V).** A typical cheap USB card (C-Media CM108AH): USB 2.0 full speed, USB Audio Class 1.0, one mono
   microphone input, 16-bit ADC at 48 or 44.1 kHz, a +22.5 dB microphone boost pin, a 4.5 V microphone bias, an analog
   mic-to-headphone path (sidetone) that is muted by default. A USB-C headset dongle (Realtek ALC4030 in the StarTech
   example): high-speed UAC, 48 kHz 16-bit microphone input, playback up to 384 kHz 32-bit, 2.5 W, CTIA and OMTP TRRS.
   Linux names the controls predictably: `Mic Capture Volume`, `Mic Capture Switch`, `Auto Gain Control`, and
   `Mic Playback Volume` for the sidetone (V, kernel `mixer.c`).
4. **USB on the Zero 2 W (V, U).** One OTG port; Raspberry Pi warns that models before the Pi 4 have a controller with
   "a higher software processing overhead", that "most USB sound devices are full speed", to prefer multi-TT hubs and to
   avoid USB 3.0 hubs for full-speed devices (V). Older reports tie crackling USB audio to the `dwc_otg` FIQ code (forum
   and issue reports, U on our kernels). This is the first thing P0 must measure.
5. **Formats (recommendation).** Primary: **WAV with a BWF `bext` chunk** (PCM at the device's native rate and depth),
   written so that a power cut loses at most a few seconds: periodic header updates and a repair pass at the next start;
   **RF64** or auto-split past 4 GiB (6.2 h of 48 kHz stereo 16-bit). Options: **FLAC** (libFLAC, BSD-3-Clause, V;
   frames self-synchronising, STREAMINFO totals "0 = unknown" until finalised, V) and **Ogg Opus** (libopus/libopusenc,
   BSD-3-Clause, V; 14.4 MB per hour at 32 kb/s). **MP3** export is possible with LAME (LGPL, V; patents' licensing
   programme ended 2017, V). **AAC: no** (FDK AAC is in Debian non-free, V; Debian's FFmpeg binaries are GPL-2+, V; the
   Via LA AAC pool is active in 2026, V).
6. **Crash safety is the core design point (V, I).** ext4 commits every 5 s by default and, in the default ordered mode,
   data blocks are "not guaranteed to be in any consistent state after a crash" (V). FFmpeg plays a WAV whose data size
   is 0 or `0xFFFFFFFF` to the end of the file but stops at a stale positive size (V, `wavdec.c`). So: write in large
   blocks on a writer thread, `fdatasync` every 5-10 s, update the WAV header at each sync, keep a small recording
   journal, and repair any unfinished file when the app starts.
7. **Meters (V, I).** Sample peak and RMS per ALSA period cost almost nothing (I); true peak (oversampling per
   ITU-R BS.1770-5, V for the version) is optional. VU ballistics are 300 ms (IEC 60268-17, secondary source); PPMs integrate in
   5 or 10 ms and fall 20 dB in 1.7 s or 24 dB in 2.8 s (secondary source). EBU R 68 aligns at 18 dB below full scale
   (V). LVGL refreshes every 33 ms (V, render configuration), so meters can update at up to 30 Hz; the audio thread hands
   over the peak of every interval so no peak is lost between frames.
8. **Background recording (V).** `X-Background=run` is the only mode that can keep recording while hidden (`freeze` is
   `SIGSTOP`: the capture would overrun). The launcher can still close a hidden app at any time (`close`, then SIGTERM
   3 s later, SIGKILL 3 s after that; below 32 MB MemAvailable a running hidden app is closed too) and holding Esc 3 s
   sends SIGTERM then SIGKILL 3 s later (V). Multitasking is off on screens whose short side is under 480 (V), so on
   480x320 boards a recording runs only while the app is in front. **Owner decision (D10): no background recording for
   now**; the app owns the screen while recording and `X-Background=run` is left for later.
9. **Licences (V).** Fully permissive: miniaudio (public domain or MIT-0), libFLAC, libopus, libopusenc, libopusfile,
   libogg, libvorbis, WavPack (BSD-3-Clause), ALAC (Apache-2.0), PipeWire's client library (MIT). LGPL (dynamic link from
   Debian, replaceable): alsa-lib, libsndfile, LAME, mpg123, libpulse. GPL programs usable only as separate processes:
   `arecord`/alsa-utils, the `flac` and `metaflac` tools, SoX, Debian's FFmpeg. Non-free: FDK AAC.
10. **Effort (I).** MVP 0.1 (probe, core with WAV, UI with meters, playback, packaging, docs): **6-8.75 ws (2.4-3.5 M
    tokens)**; with FLAC and Opus and polish: **8-11.75 ws**; with trimming, a metadata editor, sharing
    and the desktop-server path: **10.75-16.5 ws** (section 7). P0 is done (except the stereo-music encoder test), so
    its share is spent.

**Recommendation (updated after P0).** Build 0.1 as in section 7 with the writer design P0 asks for: a capture thread
that only fills a RAM ring, a separate writer thread, periodic sync and a repair step on launch (P0 results).

**Top three risks.** (1) USB full-speed audio on the Zero 2 W's single OTG port: P0 found capture itself light and
clean, but SD-card write bursts cause overruns when one thread both reads and writes (V); the two-thread writer must be
re-tested. (2) Data loss on power cuts or unplugging (the owner has unplugged boards by accident): P0 measured it (V);
mitigated by the write strategy of 4.4 and a repair step. (3) Sound servers on desktop boards holding the card: P0-f
confirmed the EBUSY case and the fallback through the server (V).

**Owner questions:** section 11 (all answered).

## 1. Decisions for the owner

**All accepted by the owner on 2026-11-08.** The last column is the final choice; changes against the study's
recommendation are in bold.

| # | Decision | Options | Final choice (owner, 2026-11-08) |
|---|---|---|---|
| D1 | Start | run probe P0 now (section 8); park | run P0: **done** on the zero8 and the HackBerry, 2026-11-08 (P0 results), except the stereo-music encoder test |
| D2 | Name and app id | a generic name (6.5); never a manufacturer's or model name | **Cassette**, app id **`cassette`** |
| D3 | Primary recording format | WAV/BWF; FLAC; Ogg Opus | WAV with a `bext` chunk (BWF), RF64 past 4 GiB: no codec, no CPU, the simplest crash repair, every player (4.9) |
| D4 | Compressed options | none; FLAC; Ogg Opus; both | both, as settings (0.2): FLAC for lossless, Opus for long speech recordings (4.1) |
| D5 | MP3 | no; export only (LAME, LGPL, run-time loaded); record directly | not in 0.1; export later only if wanted for old players (6.3) |
| D6 | AAC / M4A | no; FDK AAC (non-free); FFmpeg subprocess (GPL build) | no (6.3) |
| D7 | How libraries reach the app | (a) Debian `Depends` + `dlopen` at run time; (b) per-app link flags in the shared build; (c) vendored source compiled in; (d) command-line tools as subprocesses | (a), the house pattern (libcurl, libsystemd, SQLite); (c) only for miniaudio's single-file decoders (public domain or MIT-0) |
| D8 | Capture API | ALSA direct; miniaudio; PipeWire or PulseAudio client; `arecord` subprocess | ALSA direct (2.2): xrun count, native formats, unplug detection |
| D9 | Desktop boards with a sound server | record through the server's ALSA plugin when it holds the card; ask WirePlumber to release it (device reservation); tell the user | `hw:` first; on `EBUSY` record through the server and say so; a notice while the app holds the card directly. **Confirmed by measurement** (P0-f) |
| D10 | Background recording | yes (`X-Background=run`); no (the app owns the screen while recording) | **no background recording for now**: multitasking stays off for the app, it owns the screen while recording; `X-Background=run` left for later |
| D11 | Sample rate | the device's native rate, 48 kHz preferred; 44.1 kHz; a fixed rate with resampling | native, 48 kHz first, 44.1 kHz when the device offers only that; never resample in the capture path |
| D12 | Channels | follow the device; force mono; force stereo | follow the device; an optional "mono = left" setting for stereo devices (both test devices were mono only, P0) |
| D13 | Bit depth | the device's native depth; always 24-bit; 32-bit float | native (24-bit when offered); no float WAV |
| D14 | Where files go | `~/Recordings`; an app data folder; a USB stick | `~/Recordings`; a USB stick as a later "copy to" target |
| D15 | File names | date-time first; take number first; user prefix | `<prefix>_<take>_<date>-<time>.wav`, a take counter that never repeats, `nodate` while the clock has not been set since boot |
| D16 | Metadata | BWF `bext` + iXML in the WAV; Vorbis comments in FLAC/Opus; a sidecar file | in the file (`bext` + padded iXML; Vorbis comments for FLAC/Opus); no sidecar |
| D17 | Monitoring | the card's hardware sidetone; software monitoring through the same card; none | the hardware sidetone when the card has one; software monitoring later |
| D18 | Signal processing defaults | DC blocker, high-pass filter, limiter, AGC | DC blocker on; high-pass and limiter off by default. **Revised: the app does not force the card's AGC off.** It reads the card's own mixer controls (`Auto Gain Control`, `Mic Capture Volume`, mute), shows them in Settings as rows the user can change, leaves the card as found until the user changes it, and remembers the choice per card |
| D19 | Screen sizes | `X-MinSize`, `X-Portrait` | the controller's design decision; technical notes in 5.9 |

## P0 results (zero8 and HackBerry, 2026-11-08)

Run with the owner present. zero8 (Zero 2 W class, launcher running): C-Media/TI PCM2902 card `08bb:2902` and the USB-C
headset "UC02". HackBerry CM5 (trixie desktop, PipeWire + WirePlumber): headset UC02. Method: `arecord` runs of 2-5
minutes, `vmstat`/`ps` samples, offline analysis. All rows V (measured) unless marked I. Not done: the stereo-music
encoder test and encoding while capturing.

**Capture, noise floor and devices (V).**

| Item | Result | What it changes |
|---|---|---|
| Capture CPU | `arecord` 0.14-0.40 % of a core (1.12 % at 256-frame periods); system 94-98 % idle, not throttled, no USB errors | capture itself is no risk on the Zero 2 W |
| Noise floor, 48 kHz, quiet room (PCM2902) | -70.6 dBFS at 0 dB gain, -68.1 at +17.9 dB, -65.4 at +23.8 dB; AGC on or off made no difference (loud input not tested); DC offset about 0 | DC blocker stays as a cheap safety, not a fix for a seen fault |
| Real-time priority | `ulimit -r` is 0 | no real-time priority available: rely on buffers |
| Mixer (PCM2902) | `Mic Capture Volume` 0-16 = 0 to +23.81 dB; AGC present (on as found); sidetone controls present (off as found) | D18 rows: input level and auto gain read from the card |
| Mixer after reboot | resets (PCM2902: level 0, AGC on) | the app reapplies the user's saved level and AGC choice per card |
| Card index | changes across reboots (1 before, 0 after on the zero8; 2 on the HackBerry) | find the card by name or `/dev/snd/by-id`, never by index |
| Headset UC02 | USB ID `0000:0000`, maker, product and serial all "UC02" (not unique); mono S16 at 48 kHz only; `Mic Capture Volume` 0-127 (0 to +23.81 dB); kernel disabled dB reporting | identity by name + by-id path only; two such headsets cannot be told apart except by USB path (I) |
| Headset level | full level (+23.8 dB), normal speech: peaks -7 to -17 dBFS; at +13 dB peaks near -29 dBFS | the headset mic is weak: software gain on top of the card level, a level meter and a visible target; "max" is not a safe default for hotter devices |

**Overruns and SD-card writes (V).**

| Period/buffer (frames) | Run | Overruns |
|---|---|---|
| 6000/24000 (default) | 4.5 min | 0 |
| 1024/4096 | 300 s, launcher up | 1 (>= 144 ms) |
| 1024/4096, 2048/8192, 4096/16384 | 120 s each | 0 |
| 256/1024 | 120 s | 1 (>= 40 ms) |
| 1024/4096 + 100 MB SD write (about 12 MB/s) | during the write | 2.6 s + 2.9 s (about 6 s lost) |
| default 500 ms buffer + 100 MB SD write | during the write | 523 ms + 22 ms (about 2 s lost) |

Likely cause (I): `arecord`'s single thread blocks in `write()` while the system flushes to the card. **Writer design:**
a capture thread on `hw:` with 1024-frame periods and at least 500 ms of buffer only fills a RAM ring and feeds the
meters; a separate writer thread drains a ring of at least 8 s (about 750 KB at 48 kHz mono); avoid 256-frame periods;
no real-time priority; count overruns and show a visible "dropped" marker. This replaces the 20 ms (960-frame) period
proposed in 2.3. The SD-write test is to repeat with the real two-thread writer.

**Crash safety: power pull, owner pulled the cable (V).**

| Run | Recorded | Survived | Lost |
|---|---|---|---|
| A, plain `arecord` (PCM2902) | about 55 s | 30.4 s | about 25 s (matches the usual 30 s dirty-page flush, I) |
| B, `arecord` + `sync` every 10 s (UC02) | not logged | 60.3 s | about 6 s (within the sync interval) |

Both files kept `arecord`'s placeholder header sizes (RIFF `0x80000024`, data `0x80000000`) and ended cleanly; the file
system came back read-write with no error or recovery message. **So:** sync every 10 s bounds the loss as planned (4.4);
a **repair step on launch** must rewrite the real sizes. The cost of a 10 s sync is to measure with the real writer.

**Unplug during recording, UC02 (V).** `arecord` stopped by itself with `read error: File descriptor in bad state`
(no hang); the file kept 22.75 s, ended cleanly, header with placeholder sizes. On replug: same card name and the same
`/dev/snd/by-id/usb-UC02_UC02_UC02-00` path. **So:** treat that read error (and `-ENODEV`) as "device lost", finalise the
file, and find the device again by its by-id path.

**Desktop sound server, HackBerry (V).**

| Case | Result |
|---|---|
| PipeWire idle | does not hold the card (stream `closed` 0, 3 and 8 s after the last use); direct `hw:` records fine |
| Through ALSA `default` or `pw-record` | works, same level as `hw:` |
| `hw:` while a PipeWire client records | fails at once with `Device or resource busy` |
| PipeWire client while `hw:` is held | starts without an error but records silence (empty files) |

**So (D9 confirmed):** open `hw:` first; on `EBUSY` record through the server and say so; show a notice while Cassette
holds the card directly, because other apps get silence meanwhile. Device reservation was not needed and not tested.

**Encoder timings on the zero8 (V).** Input: 60.3 s of mono 48 kHz speech; flac 1.5.0, opus-tools 0.2 (libopus 1.5.2).

| Encoder | Real-time factor | CPU (share of one core) | Size |
|---|---|---|---|
| flac -0 | 196x | 0.5 % | 28 % of PCM |
| flac -5 | 171x | 0.6 % | 27 % |
| flac -8 | 74x | 1.4 % | 27 % |
| opus 24 kbps | 10.1x | 9.9 % | 2.9 % |
| opus 64 kbps | 13.5x | 7.4 % | 8.2 % |

**So:** FLAC is essentially free; Opus 24-64 kbps costs 7-10 % of a core on mono speech. Not yet measured: stereo music
(expected about twice the CPU, I) and encoding while capturing and writing.

## UI direction (owner's choice, 2026-11-08)

- **Look:** a cassette recorder, **option A (cream portable)**, chosen over option B (black).
- **Screens:** recording (cassette window with turning hubs, counter, level bars, transport keys); settings, with the
  card's own controls as rows (input level, auto gain) per D18; recordings list.
- The mockups (480x320 and 320x480) live in the design canvas, not in this repo. The constraints of 5.9 still apply.

## 2. Capture stack (question 1)

### 2.1 The audio stack on our boards

| Board | Image | Sound server | Sound hardware | Source |
|---|---|---|---|---|
| Deck (Zero 2 W) | Pi OS Lite, Debian 13 (I) | none (I; Lite: V for Bookworm) | none built in: "The Pi Zero 2W has no audio output"; every input is a USB card | our README (V) |
| zero8 (Zero 2 W, 480x320) | as the deck (I) | none (I) | none built in | I |
| Pi 3A+ | Pi OS, Debian 13 (I) | none (I) | 3.5 mm jack: output only (`bcm2835 Headphones`), HDMI | our README (V) |
| Pi 4B | Pi OS (I) | U | 3.5 mm jack output, HDMI; no analog input (I) | U |
| Pi 5 + HyperPixel | Pi OS desktop (labwc), Debian 13 | PipeWire likely (Pi OS desktop default, V for Bookworm); our README found no `pactl` (V), which does not exclude PipeWire | HDMI only (`vc4-hdmi`) | our README (V); server U |
| HackBerry CM5 | Pi OS desktop, Debian 13 | PipeWire likely (as above); the case speaker is a Bluetooth sink | U | our README (V); server U |
| FZ-M1 tablet (amd64) | Armbian (Ubuntu 26.04 base), XFCE desktop variant | PulseAudio (Armbian's XFCE module installs it, V) | HDA Intel PCH (`Master` control); an internal microphone (U) | our README (V) |

The launcher's own audio (V): it finds a board's output by looking for a non-HDMI ALSA card with a playback PCM and a
`Master`, `Speaker`, `PCM` or `Headphone` playback volume (`cp0_capabilities.c`), else a PulseAudio or PipeWire sink
through `pactl list short sinks`; the volume keys then work through `amixer` or `pactl`. A USB card with a `Speaker`
control therefore counts as the board's output, so with a USB headset plugged in the volume keys probably move the
headset's level (I; when a hot-plugged card is noticed is U). The launcher's interface sounds are off by default and
play through miniaudio (PulseAudio, then ALSA backends) (V, report 035). The toolkit's `cp0_audio_capture.cpp` records
48 kHz stereo 16-bit through miniaudio to `/tmp/rec.tmp.wav` (V); it is not in the apps' build (`_common` builds
`cp0_lvgl` "with the filesystem service only", V) and its fixed format and `/tmp` file do not suit a recorder.

Permissions (V, U): pi-gen adds the first user to `audio` (`stage2/01-sys-tweaks/01-run.sh`, V); `install.sh` adds the
user to `audio` on boards with a non-HDMI card of their own (V; on the deck, without a card at install time, it does
not, but the Pi OS user is already a member). The launcher runs as a user service with lingering on appliance boards
(V, report 057), so it probably has no logind seat session and relies on the group, not on `uaccess` ACLs (I). Check
with `id -nG` in P0.

### 2.2 Options

| Option | What | For | Against | Licence, packaging |
|---|---|---|---|---|
| **A. ALSA direct** (recommended) | `snd_pcm_*` and `snd_mixer_*` from `libasound.so.2`, opened with `dlopen` behind a small function table | every board, with or without a server; native formats; xrun count; `-ENODEV` on unplug; period/buffer control; the mixer in the same library | we write the format handling (S16_LE, S24_3LE, S24_LE, S32_LE) and the device probing; on a desktop board the card may be busy (2.4) | alsa-lib LGPL-2.1+ (V); `Depends: libasound2t64` (trixie 1.2.14, V); our declarations of its public ABI, checked against the real headers in an integration test (Home Assistant's `check_curl_abi.cpp` pattern, V) |
| B. miniaudio | single header; ALSA, PulseAudio, JACK backends loaded at run time (V); "Encoding (WAV only)" (V) | already vendored in the launcher (0.11.25; upstream 0.11.26 of 2026-10-31, V); decoders for WAV, FLAC, MP3 (V); one API for playback too | overruns are recovered inside the backend and only logged at debug level (V, `ma_device_read__alsa`), so the app cannot count gaps; its WAV encoder writes the header at the end (no crash safety, I); on Linux it prefers PulseAudio over ALSA (V) | public domain or MIT-0 (V); vendored, no `Depends` |
| C. PipeWire client | `libpipewire-0.3` | the native path on desktop boards | absent on the appliance boards (no server); a second code path | MIT (V); `libpipewire-0.3-0t64` 1.4.2 on trixie (V) |
| D. PulseAudio simple API | `libpulse-simple` | works with PulseAudio and PipeWire's pulse server (I) | absent on appliance boards; adds latency | LGPL-2.1 (V) |
| E. `arecord` subprocess | `arecord -t raw` piped into the app, which meters and writes | zero linking; cheapest for P0 and a fallback | one more process; xruns only as text on stderr; format probing by parsing; stopping cleanly needs care | alsa-utils GPL-2+ (I: not read), a separate program (allowed, 6.1); installed on Pi OS Lite (U) and by `install.sh` on PCs (V) |
| F. JACK | a JACK server | low latency | not installed; a server to run; no gain for a recorder | not considered |

On a desktop board the same option A records through the server: ALSA's `default` PCM goes to PipeWire or PulseAudio
when their ALSA plugins are installed (`pipewire-alsa` 1.4.2 on trixie, V; installed on our desktop boards: U). That
keeps one API; the server then hides xruns and adds latency (I).

### 2.3 ALSA facts that matter

| Topic | Fact | Source |
|---|---|---|
| `hw:` | "a raw communication without any conversions"; the card may be a number or its ID string | V, alsa-lib plugins |
| `plughw:` | the plug plugin over hw: "converts channels, rate and format on request" | V |
| `default` | on a plain ALSA system, `defaults.pcm.card 0` (V, `alsa.conf`): on the deck card 0 may be HDMI or nothing, so `default` is not the USB microphone; the app always names the card (I) | V, I |
| Sharing | `dsnoop` lets several programs read one capture device; a plain `hw:` device has one user at a time | V (dsnoop), I |
| Ring buffer | the device writes into a ring buffer; the application reads from it; a late reader gets an overrun | V |
| Overrun | state `SND_PCM_STATE_XRUN`, reads return `-EPIPE`; `snd_pcm_recover()` is the recommended way out | V |
| Unplug | `-ENODEV` (or `-ENOTTY`) on physical removal; state `SND_PCM_STATE_DISCONNECTED` "does not accept any I/O calls" | V |
| Transfer | `snd_pcm_readi()` copies; `mmap` gives pointers into the ring buffer ("zero-copy") | V |
| USB stream facts | `/proc/asound/cardN/stream0` lists each altsetting's format, channels, rates, bits and endpoint sync type; `usbid` and `usbbus` give the USB identity | V, kernel `proc.c` |

Proposed parameters (I, to tune in P0): open `hw:CARD=<id>,DEV=0`; the device's native format; period 20 ms (960 frames
at 48 kHz) so that each period feeds one meter frame at 30 Hz; buffer 500 ms or more (25 periods), a wide margin on a
Zero 2 W whose CPU also renders the UI in software; `snd_pcm_readi()` (the data rate is under 300 KB/s, so `mmap` buys
nothing, I). A capture thread at normal priority: real-time scheduling needs `RLIMIT_RTPRIO`, which a Debian user does
not have by default (I; `ulimit -r` in P0), and a large buffer makes it unnecessary (I). **After P0:** 1024-frame
periods with a buffer of at least 500 ms, no real-time priority (`ulimit -r` is 0, V), and a separate writer thread
(P0 results).

On every overrun: count it, recover, take the length of the gap from the stream's timestamps, and write a marker (an
iXML note or a `cue ` point) at that sample so the user can find it (I).

### 2.4 Living with a sound server (desktop boards)

- WirePlumber suspends an idle ALSA node after `session.suspend-timeout-seconds`, "by default ... `5` seconds"; "a
  value of `0` disables suspend ... and will leave the ALSA device busy" (V, WirePlumber 0.5.19). So the card is free
  most of the time, but busy while any client uses it and for 5 s after (I).
- When the server holds the card, opening `hw:` fails with "busy" (I, U). Options: (1) record through the server's
  ALSA plugin (`default` or `pipewire` PCM), the simplest; (2) ask the server to release the device through the D-Bus
  device-reservation protocol `org.freedesktop.ReserveDevice1`, which WirePlumber can honour (an `alsa.reserve` setting
  appears in a mirror of its documentation, secondary source; defaults U); (3) tell the user to stop the other audio program.
- The launcher's desktop variant stops the display manager; PipeWire runs as the user's systemd services and may still
  be up while the launcher runs (I, U). P0-f on the HackBerry: PipeWire releases the card when idle; `hw:` gives
  "busy" while a PipeWire client records (V, P0 results).
- Detection: a PulseAudio-compatible server answers `pactl info`, which the launcher already runs with a timeout
  (V, `board_facts.cpp`); `board.state` `sound_kind=server` says a server sink exists (V, contract 5c).

Recommendation: try `hw:` first; on "busy" with a server present, record through `default` and show that the server is
in the path (D9).

### 2.5 Finding devices, naming them, hot-plug and removal

- **List.** `/proc/asound/cards` (index, ID, name), `/proc/asound/cardN/id`, `stream0` and `usbid` (V); a capture
  device has `/dev/snd/pcmC<N>D<M>c` (the trailing `c`) and `/proc/asound/cardN/pcm<M>c` (I, standard naming). The
  launcher reads `/proc/asound/cards` the same way (V).
- **Capabilities.** Open `hw:` briefly and ask `snd_pcm_hw_params_any()` for formats, channel counts and rates (I); for
  USB cards `stream0` already lists them without opening the device (V). Probing takes the device for a moment: never
  probe a card another program is recording from (I).
- **Stable identity.** Card numbers change across boots and plugs (V, a common report; I). udev creates
  `/dev/snd/by-id/usb-<serial>-<interface>` and `/dev/snd/by-path/<path>` links to `controlC<N>` (V, systemd
  `60-persistent-alsa.rules`), and ALSA accepts the card ID string (`hw:CARD=Device,DEV=0`, V). Remember a device by USB
  vendor:product + serial + by-path, never by card number (guidelines lesson "the device, not the port", V). Cheap
  cards often have no serial and the same ID string (`Device`), so two identical cards are told apart by USB path only
  (I).
- **Hot-plug.** Watch `/dev/snd` with `inotify` for `pcmC*D*c` appearing and disappearing (I), with a 1-2 s poll of
  `/proc/asound/cards` as the fallback (I). libudev would work too (LGPL, the launcher builds with it, V) but adds a
  library for little gain. Wait until the card's ID and `stream0` are readable before probing (I).
- **Removal during a recording.** The read returns `-ENODEV` (V). The app stops the capture, writes what is in the ring
  buffer, finalises the file (header, journal entry), and says "Microphone disconnected; the recording up to here is
  saved" (I). A later plug of the same device can offer "continue as a new take" (I).
- **Start-up power.** Plugging a USB device can dip a marginal supply; the guidelines record board resets right after a
  device connected (Mesh Hop, cause not known, V). The app does no heavy work on connect (guidelines lesson, V).

### 2.6 What cheap USB cards and USB-C headsets offer

| Item | Cheap USB card (CM108AH) | USB-C headset dongle (Realtek ALC4030, StarTech USBCAUDIO2) | Apple USB-C to 3.5 mm adapter |
|---|---|---|---|
| USB speed, class | full speed, USB Audio Class 1.0 (V) | 480 Mb/s (V); UAC2 (I) | UAC2/UAC3 descriptors reported (U) |
| Microphone input | one input, `MICIN` (mono, V) | "microphone input, with 48kHz and 16-bit sampling" (V); "Stereo ADC" in one line (V) | U |
| Rates, depth | "48K/44.1KHz ... for both playback and recording"; 16-bit ADC (V) | playback 44.1-384 kHz, 16/24/32-bit (V) | a kernel bug reports no 44.1 kHz (U) |
| Gain | ADC gain stages up to +22.5 dB in 16 steps and a `BOOST` pin "+22.5 dB boost enable" (V, block diagram) | U | U |
| Microphone bias | `VBIAS` 4.5 V, "small driving capability" (V) | U | U |
| Sidetone | headset mode has a mixer unit; the analog path "with default mute" (V) | U | U |
| Sync | "adaptive mode with internal PLL" (V) | U | U |
| Power | jumper: 100 mA or 500 mA, bus-powered (V) | 2.5 W (V) | U |
| TRRS | 3.5 mm jacks per board design (I) | "OMTP and CTIA" (V) | U |

ALSA control names (V, Linux 7.4-rc1 `sound/usb/mixer.c`): a feature unit's volume or mute is named after its input
terminal ("Mic") and gets " Capture" when its output terminal is the USB stream, else " Playback", then " Volume" or
" Switch". So a card shows `Mic Capture Volume` and `Mic Capture Switch` for the recording gain, `Mic Playback Volume`
and `Mic Playback Switch` for the microphone-to-headphone path (sidetone), `Speaker Playback Volume` for the output, and
boolean `Auto Gain Control` when the card implements AGC. UAC2 adds `Input Gain Control` and `Input Gain Pad Control`.
The kernel corrects the resolution of some cards' `Mic Capture Volume` with quirks (V). Hardware boost wired to a pin is
not a control at all (I). The app reads dB ranges from the control (`snd_mixer_selem_get_capture_dB_range`, I) and shows
dB, not raw steps.

TRRS (I): CTIA (most phones today) and OMTP swap ground and microphone; a headset of the wrong standard records nothing
or hum. A plain 3.5 mm microphone (TRS) on a TRRS jack may need the bias; a dynamic microphone needs none and is very
quiet on a 16-bit card without a preamp.

### 2.7 USB on the boards

| Fact | Source |
|---|---|
| "Raspberry Pi Zero boards have one micro USB on-the-go (OTG) port" | V, Pi USB documentation |
| Models before the Pi 4 have "only a basic level of support for certain devices, which presents a higher software processing overhead"; one root port, 480 Mb/s shared | V |
| "most USB sound devices are full speed"; limits on "simultaneously active low- and full-speed devices"; "Small numbers ... will cause no issues" | V |
| A full-speed device behind a USB 3.0 hub does not work on models before the Pi 4; use a USB 2.0 hub | V |
| Single-TT hubs can be unreliable with many full/low-speed devices; prefer multi-TT | V |
| Zero 2 W: recommended supply 2 A; USB peripheral current "Limited by PSU, board, and connector ratings only"; Pi 3A+ the same; Pi 4B 1.2 A; Pi 5 1.6 A with a 5 A supply, else 600 mA | V, power documentation (updated 2026-10-12) |
| Crackling USB audio linked to the `dwc_otg` FIQ split-transaction code on older kernels; workarounds `dwc_otg.fiq_fsm_mask` values; mixed results | forum and GitHub issue reports 2014-2018 (U on our kernels) |
| Pi 4 (VL805) and Pi 5 (RP1) use xHCI controllers without that code | I |

## 3. Audio quality facts for a recorder (question 2)

### 3.1 Gain staging and clipping

- Set the level at the card (its `Mic Capture Volume`, boost or AGC), not in software: digital gain after the ADC adds no
  headroom and raises the card's noise with the signal (I). A peak above full scale in the ADC is lost for good; a
  limiter after it cannot repair it (I).
- 16-bit gives about 96 dB of range, more than a cheap card's analog noise floor (I), so 16-bit at a sensible level is
  enough for these cards; 24-bit matters only for cards with better converters (I).
- Broadcast practice aligns at 18 dB below full scale ("an alignment level which is 18 dB below the maximum possible
  coding level", EBU R 68, 2000, V); SMPTE uses -20 dBFS (secondary source). A recorder's meter marks this level and the
  user aims speech peaks around -12 to -6 dBFS (I).
- Clip indication: count samples at or within 0.1 dB of full scale; flag a clip after 2-3 consecutive ones (I), keep a
  per-take clip count and the time of the last clip.

### 3.2 DC offset, high-pass filter, limiter, AGC

- **DC offset.** Cheap ADCs can show a DC offset (I; P0 measured about 0 on the PCM2902, V). A first-order DC
  blocker (corner about 5-10 Hz) removes it inaudibly and keeps meters honest (I). Applied before the meters and the
  file (D18).
- **High-pass filter.** An optional 80 or 100 Hz second-order filter against handling noise and wind (I); off by default.
- **Limiter.** Useful only after a digital gain stage or when reducing 24-bit to 16-bit; off by default (I).
- **AGC.** If the card has `Auto Gain Control` (V, control name), show it as a Settings row; the app leaves it as found
  until the user changes it and remembers the choice per card (D18, revised by the owner); a software AGC is not in 0.1.

### 3.3 Clock drift and xruns on USB isochronous audio

- The card's crystal sets the sample rate; the host only receives packets (I). A file written from the stream is
  therefore internally consistent; its length in real time drifts from the board's clock by the crystal's error, for
  example 50 ppm = 0.18 s per hour (I). This matters only when the recording is synchronised with another device's
  recording (video): record the start time in BWF `TimeReference` and accept the drift (I).
- The CM108AH uses adaptive synchronisation with an internal PLL (V); USB-C dongles more often use asynchronous
  endpoints (I).
- An overrun is a gap in the recording. With a 500 ms buffer on a lightly loaded Zero 2 W it should be rare (I); the app
  must count and mark each one (2.3). P0 measured it: rare in plain capture, but seconds long during SD-card write
  bursts with a single-threaded writer (V, P0 results).

### 3.4 Monitoring

| Way | Latency | Notes |
|---|---|---|
| Hardware sidetone (`Mic Playback Volume/Switch`) | zero (analog path, V for the CM108AH mixer unit) | only on cards with the mixer unit; muted by default (V); the app sets it on and off and restores the user's value on exit (I) |
| Software monitoring through the same card | 2-3 periods plus the playback buffer: about 40-80 ms on a Zero 2 W (I) | both directions share the card's clock, so no drift (I); feedback risk with open headphones; more CPU and wake-ups; a later phase (D17) |
| Through the server (desktop boards) | the server's quantum, tens of ms (I) | only where a server runs |

The launcher's volume keys act on the playback side of the first non-HDMI card with a playback control (2.1), so they
set the monitoring and playback level, as the guidelines want ("for media apps only", `volume` requests, V); the app
does not act on those keys itself (V, guidelines section 3).

### 3.5 Metering standards

| Meter | Definition | Source |
|---|---|---|
| Sample peak | largest absolute sample value per interval, in dBFS | I |
| True peak | the waveform's peak between samples, estimated by oversampling; BS.1770-5 (11/2023) is the in-force version | V (version); 4x oversampling at 48 kHz: secondary source |
| RMS | root mean square over a window (300 ms is common for speech, I); whether a full-scale sine reads 0 or -3 dBFS is a convention to fix (AES17 uses 0, U: not read) | I |
| VU | rise time 300 ms to 99 %, overshoot 1-1.5 %, fall time 300 ms; ANSI C16.5-1942, BS 6840, IEC 60268-17 | secondary source (Wikipedia) |
| PPM Type I (DIN) | integration 5 ms, return 20 dB in 1.7 +-0.3 s | secondary source |
| PPM Type IIa (BBC) and IIb (EBU) | integration 10 ms, return 24 dB in 2.8 +-0.3 s | secondary source |
| Digital PPM (IEC 60268-18) | integration under 5 ms, return as Type I | secondary source |
| Loudness | EBU Tech 3341 v4.0 (2023-11-21): momentary, short-term and integrated loudness with EBU R 128; windows 400 ms and 3 s (I: from the standard family, not read in this study) | V (version), I |
| Alignment | -18 dBFS (EBU R 68) | V |

A field recorder typically shows a fast peak meter with peak hold and a clip lamp; a VU or PPM look is a design choice
(the controller's). Any ballistics can be computed from the same per-block data (3.6).

### 3.6 Computing meters on a Zero 2 W

- **Data per block (I).** On the capture thread, for each 5-10 ms sub-block of a period: per channel the sample peak, the
  sum of squares, the clip count; optionally a 4x true-peak estimate. One pass over 48,000 or 96,000 samples per second
  is a few million simple operations: well under 1 % of one Cortex-A53 core (I). A 4x oversampling FIR of 48 taps (12
  per phase) is about 4.6 M multiply-adds per second per channel at 48 kHz: about 1-2 % of a core (I); optional.
- **Hand-over (I).** The capture thread accumulates "peak since last read" and "sum of squares since last read" per
  channel in atomics or a tiny lock-free buffer; the LVGL timer (30 Hz at most: LVGL's refresh period is 33 ms in our
  render configuration, V; the device build is assumed the same, I) takes the values and resets them, so a peak shorter
  than a frame is never missed. Ballistics (VU, PPM fall-back, peak hold) run in the UI timer on these values (I).
- **What the UI can have, per channel (I):** sample peak (dBFS) per frame; RMS over 300 ms; a VU or PPM value; peak hold
  (value and age); clip flag and count; overrun count; a level history for a scrolling strip (one min/max pair per
  20-50 ms); the recording's elapsed time, size, remaining time (5.4).
- **Redraw cost (I).** The meters themselves are small areas. Software rendering of large animated areas on the deck is
  the real cost (report 056 estimated a 10 Hz radar plot at under 20 % of one core, I): see 5.9.

### 3.7 Pre-roll, auto-split, silence detection, normalisation

- **Pre-roll (I).** Keep the last N seconds in RAM while armed; on Record, write them first. 5 s of 48 kHz stereo 16-bit
  is 960 KB. The meters run while armed (needed anyway to set the level), so arming costs only the capture.
- **Auto-split (I).** Close the file and open the next at a size or time limit (before 4 GiB for WAV, or every hour),
  between two blocks, with no gap: the next file starts at the next sample. Also used for FAT32 USB sticks (4 GiB - 1
  file limit, I).
- **Silence detection, voice-activated recording (I).** Threshold in dBFS on the RMS, a hang time (2-5 s) before
  pausing, and the pre-roll buffer to keep the first syllable. Pausing creates a new take or a marker (design choice).
- **Normalisation on playback (I).** Playback gain from the file's peak (or loudness) so quiet recordings are audible;
  non-destructive (the file is not changed). A destructive "normalise and save a copy" is a later editing feature.

## 4. File formats (question 3)

### 4.1 Summary table

Licence classes: **P** = permissive (MIT, BSD, Apache, public domain), **L** = LGPL (dynamic link from Debian,
replaceable), **G** = GPL (separate process only), **N** = non-free. Sizes: see 4.2. CPU on a Cortex-A53: no benchmark
found that applies (searches 2026-11-08), so every figure is "to measure" in P0-e. P0-e later measured FLAC and Opus
on mono speech on the zero8 (P0 results).

| Format | Lossless | Encoder / decoder library (Debian 13 package) | Class | Patents 2026 | Size per hour (speech 16 kHz mono / music 48 kHz stereo) | CPU on the deck | Power-cut robustness | Seek and trim | Metadata | Players |
|---|---|---|---|---|---|---|---|---|---|---|
| **WAV / BWF** (RIFF) | yes | own writer (about 300 lines); any decoder | P (ours) | none | 115 MB / 691 MB (16-bit) | none | good with periodic header updates and repair (4.3) | sample-accurate, trivial | `bext` (BWF), iXML, `LIST INFO` | everything (V for Android) |
| **RF64 / BW64** | yes | own writer; libsndfile writes RF64 (V) | P / L | none | as WAV, no 4 GiB limit | none | as WAV (the `ds64` chunk holds the sizes) | as WAV | as BWF | fewer players (FFmpeg reads it, V; others U) |
| **FLAC** | yes | libFLAC (`libflac14` 1.5.0): BSD-3-Clause (V) | P | none known (I) | about 40-70 % of PCM (I, to measure) | to measure; decoding is light ("asymmetric in favor of decode speed", V) | frames self-synchronising with CRCs (V); STREAMINFO totals and MD5 "0 = unknown" until finalised (V); repairable | frame-accurate, seek table; lossless trim needs re-encoding the edge frames (I) | Vorbis comments | Android 3.1+ (V); Windows, macOS, browsers mostly (U) |
| **Ogg Opus** | no | libopus (`libopus0` 1.5.2) + libopusenc (`libopusenc0` 0.2.1) + libopusfile (`libopusfile0` 0.12): BSD (V) | P | royalty-free grants from Xiph, Broadcom, Microsoft (V); a Vectis pool for **hardware devices** since 2023 (V) | 14.4 MB at 32 kb/s / 43-58 MB at 96-128 kb/s | to measure | Ogg pages with CRCs (I; RFC 3533 not read); players must handle a missing end-of-stream page (V, RFC 7845); libopusenc buffers up to 2 s (decision delay) and pages up to 1 s (V) | page granularity, sample-exact cuts through pre-skip and end trimming (V) | Vorbis comments (OpusTags) | Android 5.0+ in `.ogg` (V); Apple patchy (U); browsers (I) |
| **Ogg Vorbis** | no | libvorbis/libvorbisenc 1.3.7: BSD-3-Clause (I) | P | none known (I) | larger than Opus at speech rates (I) | to measure | as Ogg | page granularity | Vorbis comments | Android all (V) |
| **MP3** | no | LAME (`libmp3lame0` 3.100; upstream 4.0 of July 2026, V): LGPL-2+ (V); mpg123 (`libmpg123-0t64` 1.32.10): LGPL-2.1 (V); miniaudio's MP3 decoder (P) | L (encoder) | licensing programme ended 2017-04-23 (V); exact last expiry U | 28.8 MB at 64 kb/s mono / 57.6 MB at 128 kb/s | to measure | frames self-contained apart from the bit reservoir (I); missing Xing/LAME tag gives wrong VBR duration (I) | frame (26 ms at 44.1 kHz, I) | ID3 | everything (V for Android) |
| **AAC (M4A)** | no | FDK AAC (`libfdk-aac2t64` 2.0.3, **non-free**, V); FFmpeg's encoder: Debian binaries are GPL-2+ (V) | N or G | Via LA pool active, licences "needed by manufacturers or developers of end-user encoder and/or decoder products" (V) | about MP3 / 2 at equal quality (I) | to measure | MP4 needs its index at the end: a cut file is unplayable without repair (I) | sample-accurate with care | MP4 atoms | everything (V for Android) |
| **WavPack** | yes (or hybrid) | `libwavpack1` 5.8.1: BSD-3-Clause (V) | P | none known (I) | about FLAC (I) | to measure | blocks with CRCs (I) | block | APEv2 | few players (I) |
| **ALAC** | yes | Apple's reference code: Apache-2.0 (V); in libsndfile (V) | P | none known (I) | about FLAC (I) | to measure | in MP4/CAF: index needed (I) | | MP4 atoms | Apple devices (I) |

### 4.2 Sizes per hour and limits

PCM (I, computed; MB = 10^6 bytes):

| Stream | Bytes per second | Per hour | WAV reaches 4 GiB after |
|---|---|---|---|
| 16 kHz mono 16-bit | 32,000 | 115.2 MB | 37.3 h |
| 48 kHz mono 16-bit (CM108AH, headset dongle) | 96,000 | 345.6 MB | 12.4 h |
| 44.1 kHz stereo 16-bit | 176,400 | 635.0 MB | 6.8 h |
| 48 kHz stereo 16-bit | 192,000 | 691.2 MB | 6.2 h |
| 48 kHz mono 24-bit | 144,000 | 518.4 MB | 8.3 h |
| 48 kHz stereo 24-bit | 288,000 | 1036.8 MB | 4.1 h |

Opus (Opus "sweet spots", RFC 6716, V: "16-20 kbit/s for WB speech", "28-40 kbit/s for FB speech", "48-64 kbit/s
for FB mono music", "64-128 kbit/s for FB stereo music"): 24 kb/s = 10.8 MB/h, 32 kb/s = 14.4 MB/h, 64 kb/s =
28.8 MB/h, 128 kb/s = 57.6 MB/h (I, computed). Example (I): 8 GB free holds 23 hours of 48 kHz mono WAV or about 555
hours of 32 kb/s Opus.

### 4.3 Power cuts and unplugging: what each format leaves behind

| Format | State after a power cut while recording | What players do | Repair at the next start |
|---|---|---|---|
| WAV, header updated every N s | audio up to the last flush; the header's sizes cover up to the last update (N s behind) | FFmpeg: a positive size stops playback at the declared end; a size of 0 or `0xFFFFFFFF` is read to the end of the file with a warning (V, `wavdec.c`); other players U | set the RIFF and `data` sizes from the file length, cut a partial sample frame (I) |
| WAV, never updated (a naive writer) | header sizes 0 | as above: FFmpeg reads to the end; others may refuse (U) | as above |
| RF64 | as WAV, sizes in `ds64` | U | as WAV |
| FLAC | frames up to the last flush; STREAMINFO says 0 samples and MD5 0 ("unknown", V); maybe a cut last frame | decoders sync on frame headers (V, the streamable subset exists for this); the cut last frame fails its CRC (V) | drop the cut frame, count samples, rewrite STREAMINFO (and the seek table) in place (I) |
| Ogg Opus | pages up to the last flush; no end-of-stream page | "implementations need to be prepared to deal with truncated streams" (V); duration from the last granule position (V) | optional: set the last page's EOS flag (I) |
| MP3 | frames up to the last flush | plays to the cut (I) | write or fix the Xing/LAME tag for VBR (I) |
| MP4/M4A | no index: unplayable without repair (I) | | complex: one more reason not to record AAC |

The second risk is the file system (V): ext4's default `commit` is 5 s and in ordered mode "All data are forced directly
out to the main file system prior to its metadata being committed"; still, the journal page says data blocks are "not
guaranteed to be in any consistent state after a crash". Explicit `fdatasync` bounds the loss (I).

### 4.4 Write strategy (recommendation)

1. **Threads (I).** Capture thread -> lock-free ring buffer (10 s, a few MB) -> writer thread (encoding when FLAC or
   Opus) -> file. The capture thread never touches the disk, the UI never waits.
2. **Blocks (I).** Write in 64-256 KB blocks (one to a few seconds of PCM), so the SD card sees few, large writes.
3. **Sync (I).** `fdatasync` every 10 s (configurable 5-30 s); after each sync rewrite the WAV header (RIFF and `data`
   sizes, one 4 KB page at the start of the file) and sync again. Worst case: the last 10 s, plus whatever the card
   itself had not committed. FLAC: rewrite nothing until the end (repair handles it). Opus: libopusenc flushes pages by
   itself (up to 1 s, V); sync the file on the same 10 s timer.
4. **Journal (I).** On Record, write `~/Recordings/.journal/<take>.json` atomically (format, rate, channels, device,
   start time, path); delete it after a clean finish. At start-up, any journal entry left means an unfinished file:
   repair it (4.3), mark it "recovered" in the list, keep the original bytes until repaired.
5. **Free space (I).** `statvfs` before and during recording; stop cleanly, finalise and say why when free space falls
   under a reserve (for example 200 MB plus one minute of recording), so the system stays usable.
6. **Size limits (I).** Auto-split before 4 GiB (or write RF64 when the user wants one file; libsndfile has
   `SFC_RF64_AUTO_DOWNGRADE` for "files under 4 GB", V, a pattern to copy).
7. **SD wear (I).** The write volume is the recording itself (345.6 MB/h for 48 kHz mono WAV); the extra writes are one
   4 KB header page and one journal commit per sync interval (360 per hour at 10 s), which the card's controller spreads.
   Preallocating with `fallocate` would cut metadata updates but leaves zeros after a crash that repair must cut (I); not
   in 0.1. The card's endurance is not known (U).

### 4.5 Metadata

- **BWF `bext` (V, field list from libsndfile's `SF_BROADCAST_INFO`, "Strongly (!) based on EBU 'bext' chunk"):**
  description (256), originator (32), originator reference (32), origination date (10) and time (8), time reference
  (64-bit sample count; since midnight by BWF convention, I), version, UMID (64), loudness value, loudness range, max true
  peak, max momentary and short-term loudness (version 2, EBU Tech 3285 v2 of 2011-05-20, V), coding history. Field
  recorders put scene, take and notes in **iXML** (an XML chunk: PROJECT, SCENE, TAKE, TAPE, NOTE, CIRCLED among its
  fields, secondary source; current spec version U: ixml.info's certificate had expired on 2026-11-08). libsndfile can
  add any chunk with `sf_set_chunk()` before the audio (V); our own writer does the same.
- **Editing later (I).** Chunks before `data` cannot grow without rewriting the file, so reserve space: a `bext` chunk of
  fixed size and a padded iXML (or `JUNK`) chunk of 4-8 KB before `data`. Notes edited after recording then rewrite in
  place.
- **FLAC and Opus:** Vorbis comments (`TITLE`, `DATE`, `COMMENT`, free fields such as `SCENE`, `TAKE`) (I). MP3: ID3v2 (I).
- **Time (V, I).** "no RTC on the boards: do not trust the clock at boot" (guidelines section 6, V): write
  `origination_date/time` only when the clock is synchronised (for example `/run/systemd/timesync/synchronized` exists,
  I, U on our images), else leave them empty and name the file `nodate` (D15).

### 4.6 Seeking and trimming in the app

| Format | Seek | Lossless trim |
|---|---|---|
| WAV | byte offset = sample x frame size (I) | copy a byte range into a new file; or truncate the end in place (I) |
| FLAC | frame by frame; the seek table speeds it up (V: seek points, placeholders) | decode and re-encode the edge frames only, or the whole file (fast enough offline, I) |
| Ogg Opus | page by page by granule position (V) | cut at pages and set pre-skip / end trimming for sample accuracy (V) |
| MP3 | frame by frame (I) | at frame boundaries; the bit reservoir makes a few frames after a cut glitch unless handled (I) |

### 4.7 In-app playback decoders

| Decoder | Formats | Licence, packaging | Notes |
|---|---|---|---|
| miniaudio (`dr_wav`, `dr_flac`, `dr_mp3` built in) | WAV, FLAC, MP3 (V) | public domain or MIT-0, vendored (V) | no dependency; can also play through ALSA (its device I/O can be compiled out, I) |
| libopusfile (`libopusfile0`) | Ogg Opus | BSD-3-Clause (I: Xiph's usual licence; Debian file not read) | run-time loaded |
| libFLAC | FLAC | BSD-3-Clause (V) | if FLAC writing already loads it |
| libsndfile | WAV, RF64, FLAC, Ogg Vorbis, Ogg Opus, MP3 (V; its Debian build depends on libFLAC, LAME, mpg123, libogg, libopus, libvorbis, V) | LGPL-2.1+ (V), run-time loaded | one library for every format, read and write; the alternative to the BSD set |

Decoding cost (I): WAV is a copy; FLAC "Real-time decode performance is easily achievable on even modest hardware"
(V); Opus decoding is several tens of times real time on a Pi 3 class CPU (FFmpeg's decoder: 28-33x on a Pi 3, a 2019
FFmpeg patch note, secondary source; libopus U). A waveform overview for a long file is computed once on a worker
thread and cached (I).

### 4.8 Phones and PCs

Android (V, platform media formats page): FLAC decode from Android 3.1, Opus decode from 5.0 in `.ogg` (Opus encode from
10), MP3 and WAV everywhere (WAV: 8- and 16-bit PCM listed), Vorbis everywhere, AAC everywhere. Apple: Ogg Opus support
in Safari and the Files app is reported partial or recent (U); FLAC on iOS U. Windows, macOS and Linux desktops play
WAV and MP3 natively and FLAC and Opus in VLC and browsers (I, U per platform). So: WAV and MP3 are the universal
exchange formats, FLAC for archives, Opus for long speech.

### 4.9 Recommendation

- **Record:** WAV with `bext` (+ reserved iXML), native rate and depth, crash-safe writing (4.4), auto-split or RF64 past
  4 GiB. **Options:** FLAC (lossless) and Ogg Opus (speech 24-32 kb/s mono, music 96-128 kb/s stereo) (D4).
- **Convert or export in the app (later):** WAV -> FLAC, WAV -> Opus, and WAV -> MP3 if the owner wants it (D5). All
  with the same run-time-loaded libraries, on a worker thread at idle priority (guidelines lesson on heavy work, V).
- **Never:** AAC/M4A (D6), anything that needs the GPL FFmpeg build inside the app.

## 5. Architecture (question 4)

### 5.1 Options

| Option | What | For | Against |
|---|---|---|---|
| **A. One native app with threads** (chosen) | capture thread, ring buffer, writer/encoder thread, meters, LVGL UI; records only while in front (D10) | one process (13-15 MB per native app, guidelines V); the house pattern (Home Assistant: one worker thread, LVGL polls its view, V); simplest lifecycle | a crash of the UI ends the recording (the file is repaired at the next start) |
| B. App + recording service | a small daemon (systemd user unit) records; the app is a remote control | recording survives the UI, independent of multitasking | two processes and a protocol; a new service type, like the Reticulum service of report 057; more to test |
| C. App driving `arecord` | the app runs `arecord` through `async_run` and reads its raw output for meters and files | no ALSA code in 0.1 | text-parsed errors, no xrun count, one more process; fine for P0, not for the product |

### 5.2 Threads and data flow (option A)

```
USB card --ALSA hw:--> [capture thread] --SPSC ring (10 s)--> [writer thread] --> WAV / FLAC / Opus file
                            |  per-block peak, sum of squares, clips, xruns (atomics)       |  fdatasync, header, journal
                            v                                                               v
                     [LVGL thread] 30 Hz timer: reads and resets meter values; state from writer (time, size, errors)
```

- LVGL is touched only on the LVGL thread (house rule, V: workers post with `lv_async_call` under `lv_lock()` in
  `async_run`, or the UI polls a snapshot as Home Assistant does every 200 ms, V). The audio threads never take the LVGL
  lock: a lock shared with rendering could stall the capture for a frame or more (I).
- Commands (start, stop, new take, change device) go from the UI to the threads through a small queue (I).
- Device changes and hot-plug: an `inotify` watcher in the capture thread's poll loop, or a third light thread (I).
- `SIGTERM`, `SIGINT`, `SIGHUP` (handled by `cdk::run()`, V) and the launcher's `close` both lead to: stop capture, drain
  the ring buffer, finalise the file, exit. All of it must fit in 3 s (5.5); WAV finalises in milliseconds, FLAC in
  milliseconds, Opus must encode up to 2 s of buffered audio (libopusenc's decision delay, V): about a few hundred ms on
  the deck (I, to measure).

### 5.3 CPU and memory budget on the Zero 2 W (I)

| Item | Estimate | Guideline |
|---|---|---|
| Capture 48 kHz mono 16-bit, 20 ms periods | 50 wake-ups/s, under 1 % of a core | idle CPU "at most a few percent" (V) |
| Meters (peak, RMS) | under 1 % | |
| WAV writing | under 1 %; 96 KB/s to the card | |
| FLAC encoding | P0-e, mono speech: 0.6 % of one core at level 5 (V) | |
| Opus encoding, 48 kHz stereo, complexity 10 | not measured; mono speech at 24-64 kbps took 7-10 % of one core (V, P0-e); stereo possibly about twice that (I) | |
| UI: meters at 30 Hz, small areas | a few % (I) | |
| UI: large animations (reels) | could reach tens of % (I): 5.9 | |
| Memory: binary + LVGL | 13-15 MB per native app (V) | VmRSS under 25 MB (V) |
| Ring buffer 10 s at 48 kHz stereo 32-bit, pre-roll 5 s | about 4 MB + 1 MB | |
| libFLAC or libopusenc state | under 1 MB each (I) | |

### 5.4 Files, names, takes, metadata, settings

- **Folder:** `~/Recordings` (D14), created on first use; settings in `~/.config/cassette/` written atomically
  (guidelines section 6, V); the journal in `~/Recordings/.journal/`.
- **Free space and remaining time (I):** `statvfs` of the folder; remaining = (free - reserve) / bytes per second; for
  FLAC use the running compression ratio of the current take; shown for the current format and, if useful, the others.
- **Names (D15, I):** `REC_0042_2026-11-08_1432.wav`; the take counter is stored in the settings and never reused, so a
  clock jump cannot create a duplicate name; `nodate` instead of the date while the clock is not synchronised.
- **Metadata (D16):** in the file (4.5); the app's own list (durations, clip counts) is a cache rebuilt from the files.
- **Settings the app needs (technical list, presentation is the controller's):** input device; sample rate (from the
  device's list); channels (device / mono-left / mono-mix); bit depth (device's list); format (WAV, FLAC, Opus) and its
  quality (FLAC level, Opus bitrate); input gain, boost and AGC (only the controls the device has, left as found until
  the user changes them, remembered per card, D18); software gain on top of the card level (P0: weak headset mic); DC blocker,
  high-pass; limiter; monitoring (sidetone on/off and level); pre-roll length; auto-split (size or time); voice-activated
  threshold and hang time; file name prefix; sync interval (advanced).

### 5.5 Background recording and the launcher lifecycle

| Fact | Source |
|---|---|
| `freeze` = `SIGSTOP` of the process group after `hide`: a stopped capture overruns at once | V (contract 13), I |
| `run` = keeps running, draws nothing, ignores input; "work that must go on: logging a serial port, decoding radio, sampling" | V |
| Answer `hide` within 300 ms; no framebuffer writes while hidden (the helper does it) | V |
| At most 4 resident apps; a 5th launch closes the least recently used **frozen** app | V |
| Hidden apps: MemAvailable read every 2 s; under 64 MB a frozen app is closed, **under 32 MB a running one too** | V |
| `close <n>`: SIGTERM 3 s later, SIGKILL 3 s after that | V |
| Esc held 3 s: SIGTERM now, SIGKILL 3 s later | V (`multitask_model.hpp`) |
| A hidden app's `oom_score_adj` is raised to 700 (`run`) or 900 (`freeze`) | V |
| Multitasking is on only where the canvas's short side is 480 or more (640x480 and up, portrait 480x640/480x800); off on 480x320 unless `APPLAUNCH_MULTITASK=1` | V |

**Owner decision (D10): no background recording for now.** The app does not declare `X-Background=run`; it owns the
screen while recording. It must still finish the file within the 3 s of `close` and SIGTERM and write the journal so
that an OOM kill or a power cut is repaired at the next start (I). The notes below are kept for when background
recording is revisited (I): declare `X-Background=run`; while hidden, skip all UI work (`cdk::hidden()`, V) but keep
capture and writing; the launcher shows no indicator for a hidden recording app, a gap the design would have to cover
(for example a toast through `launcher.sock`: toasts "show when the launcher is in front again", V).

Power and thermal (I): recording WAV is a light load; Opus encoding is the heaviest part and runs at normal priority on
one thread; the screen and Wi-Fi are larger loads than the recorder. A board that resets on device connect (2.5) is a
power problem that no app setting fixes.

### 5.6 Recording state machine (sketch, I)

`NoDevice -> Idle (device open, meters off) -> Armed (meters, pre-roll) -> Recording <-> Paused -> Finalising -> Armed`;
`DeviceLost` from Armed/Recording/Paused (file finalised); `Recovering` at start-up when the journal lists unfinished
takes. Every transition is logged with a `[cassette]` tag (guidelines section 6, V).

### 5.7 Packaging (draft)

`app.json`: `arch: ["arm64", "amd64"]`; `depends: "libfreetype6, libinput10, libxkbcommon0, libasound2t64"` plus
`libflac14` and `libopusenc0, libopus0, libogg0` when FLAC and Opus are in (the library loaded at run time is listed,
as Deck Atlas lists `libsqlite3-0`, V); `permissions: {"microphone": true, "additional_hardware": true}` (both keys
exist, V); `license: "MIT"` (plus `MIT-0 OR Unlicense` for vendored miniaudio, I); `categories: ["Tools"]` or
`["Multimedia"]` (I). The amd64 tablet runs an Ubuntu 26.04 base: package names there (`libflac14`?) need a check (U);
`dlopen` with a list of SONAMEs (`libFLAC.so.14`, then `.12`) tolerates both (I).

### 5.8 What can be tested on the PC

A `Sys`-style seam for audio, as mmWave Radar has for ports (V: "the detector and the link use only this interface, so
tests run them on fakes"):

| Fake | Use |
|---|---|
| Signal generator device | sines at known dBFS (a -20 dBFS sine must read -20.0 dBFS sample peak and -23.0 dBFS RMS with the -3 dB convention, I), square waves for clipping, silence, DC offset, pink noise, stereo with different channels |
| File replay device | real recordings from P0 as fixtures (golden meter traces) |
| Fault injection | an overrun at sample N, `-ENODEV` at sample N, a slow disk (writer back-pressure), a full disk, `SIGTERM` mid-take |
| Fake `/proc/asound` and `/dev/snd` trees | enumeration, identity, hot-plug order (the launcher's `APPLAUNCH_ASOUND_ROOT` test hook is the precedent, V) |
| Golden files | WAV/BWF headers byte for byte; truncated files and their repaired versions; FLAC with zero STREAMINFO totals; Ogg without EOS; checked with `ffprobe`, `flac -t` and libsndfile on the PC (I) |
| Headless scenes | every page at the sizes the design picks, a `lifecycle` scene (hide while recording, `expect nowrite`, close: `expect quit` within the budget), `nocontract` |

Needs a board (I): the owner's USB cards and headsets (formats, controls, sidetone), overruns under load on the Zero 2 W
through hubs, power cuts during recording (the real test of 4.3/4.4), CPU and temperature of FLAC and Opus encoding,
desktop boards with PipeWire, the tablet's built-in microphone, playback through the USB headset.

### 5.9 Constraints the UI must respect (for the controller's design)

| Constraint | Value | Source |
|---|---|---|
| Meter update rate | up to 30 Hz (LVGL 33 ms); one fresh value per ALSA period (20 ms proposed) | V (render config), I |
| Meter latency | one period (20 ms) plus one frame (33 ms) | I |
| Meter data available | per channel: sample peak, RMS, VU/PPM value, peak hold, clip count, overruns, level history (3.6) | I |
| Channels | 1 on most cheap devices; 2 on some; meters must handle both | V (CM108AH), I |
| Controls the hardware offers | depends on the device: input gain (dB range from the control), capture mute, AGC, boost (often a pin, not a control), sidetone level and switch; some devices have none: the UI must work with no gain control at all | V (kernel names), I |
| Volume keys | the launcher's (playback and monitoring level); the app must not act on them | V |
| Recording while hidden | not in 0.1 (D10): the app owns the screen while recording | owner decision |
| Stop and save | must finish within 3 s of `close` or SIGTERM; a power cut loses up to the sync interval (10 s proposed) | V, I |
| Animation cost | software rendering on the deck: animate small areas; a large rotating graphic (reels) redrawn every frame may cost tens of % of a core: limit its frame rate or area, measure on the deck | I |
| Clock | no RTC: show "time not set" rather than a wrong date | V (guidelines) |
| Busy device | on desktop boards the card may be held by the sound server: the UI needs a "through the sound server" state and a notice while Cassette holds the card directly | V (P0-f) |
| Card controls | Settings rows for the card's own input level and auto gain (D18); a level meter with a visible target, since a weak mic needs software gain | V (P0), owner decision |
| Dropped audio | a visible "dropped" marker when an overrun happened | I (P0) |
| Text input | file names and notes need the on-screen keyboard rule; offer generated names so typing is never required (guidelines "Never make typing the only way in", V) | V |

## 6. Licensing (question 5)

This section is a reading of licence texts and project rules for the owner's decision, **not legal advice**.

### 6.1 What our rules allow

- **Launcher: MIT only** (owner rule in the brief). Nothing of this app goes into the launcher.
- **Apps repository (V, APP-GUIDELINES):** our code MIT; "Credit upstream: name every program, library, dataset and
  protocol you use"; "ship each licence text under `/usr/share/doc/<app>/`"; "GPL parts: give the source URL in the
  description and the README"; `depends` lists "only what the binary links dynamically ... plus tools you run"; section
  12: do not build what "is not licensable under MIT, or under a licence you can ship and credit". Mesh Hop ships BSD and
  Unlicense parts beside MIT (V, report 057).
- **So (I):** permissive libraries can be loaded from Debian or vendored with their notice; LGPL libraries are fine
  loaded from Debian's shared objects (replaceable by the user, nothing of theirs compiled into our binary); GPL code
  only as a separate, unmodified upstream program run as a subprocess; non-free and patent-pool formats need the owner's
  explicit decision.

### 6.2 Components

| Component | Licence (where read) | Debian 13 package | How it would be used | What must ship | Risk |
|---|---|---|---|---|---|
| alsa-lib | LGPL-2.1+ (Debian `copyright`, V) | `libasound2t64` 1.2.14 (V) | `dlopen("libasound.so.2")` | `copyright` entry (credit) | none beyond LGPL hygiene: no static copy |
| miniaudio | public domain (Unlicense) or MIT-0, "Choose whichever you prefer" (`LICENSE`, V) | none | vendored single header (decoders) | its notice (optional for MIT-0; keep it) | none |
| libFLAC | BSD-3-Clause (Debian `copyright`: `include/FLAC/*`, `src/libFLAC/*`; MD5 public domain, V); the `flac` tools are GPL-2+ (V, README) | `libflac14` 1.5.0 (V) | `dlopen("libFLAC.so.14")` | `copyright` entry | none |
| libopus | BSD-3-Clause (some files BSD-2-Clause) (V) | `libopus0` 1.5.2 (upstream 1.6.1 of 2026-01-13, V) | through libopusenc | `copyright` entry | the Vectis pool targets hardware devices (6.3) |
| libopusenc | BSD-3-Clause, parts BSD-2-Clause (V) | `libopusenc0` 0.2.1 (upstream 0.3 of 2026-01-03, V) | `dlopen` | `copyright` entry | none |
| libopusfile, libogg, libvorbis | BSD-3-Clause (I: Xiph licences, Debian files not read) | `libopusfile0` 0.12, `libogg0` 1.3.5, `libvorbisenc2` 1.3.7 (V) | `dlopen` if used | `copyright` entries | none |
| libsndfile | LGPL-2.1 or 3 (V, site); Debian: LGPL-2.1+ with Apache-2.0 ALAC parts (V) | `libsndfile1` 1.2.2 (V) | alternative to the BSD set: `dlopen` | `copyright` entry | none beyond LGPL hygiene |
| LAME | LGPL (site, V); Debian: LGPL-2+ (V) | `libmp3lame0` 3.100 (V) | MP3 export only, `dlopen` (D5) | `copyright` entry | none (patents, 6.3) |
| mpg123 | LGPL-2.1 (V) | `libmpg123-0t64` 1.32.10 (V) | not needed (miniaudio decodes MP3) | | |
| WavPack | BSD-3-Clause (V) | `libwavpack1` 5.8.1 (V) | not planned | | |
| ALAC | Apache-2.0 (GitHub, V) | inside libsndfile (V) | not planned | | |
| PipeWire client | MIT (`COPYING`, V) | `libpipewire-0.3-0t64` 1.4.2 (V) | not planned (option C) | | |
| libpulse | LGPL (client library; server parts may be GPL, V) | `libpulse0` 17.0 (V) | not planned (option D) | | |
| alsa-utils (`arecord`, `amixer`) | GPL-2+ (I: not read) | `alsa-utils` 1.2.14 (V) | P0 only; `amixer` already used by the launcher (V) | | |
| FFmpeg (Debian build) | "the resulting binaries are licensed under GPL v2+" (Debian `copyright`, V) | `ffmpeg` 7.1.5 (V) | not used; at most a user-installed tool | | GPL if linked |
| FDK AAC | Fraunhofer licence, Debian **non-free** (V) | `libfdk-aac2t64` 2.0.3 (V) | not used (D6) | | non-free, patents |
| SoX | GPL (I: not read) | `sox` 14.4.2 (V) | not used | | |

### 6.3 What "not MIT" means here, case by case (owner decides)

| Case | Examples | Allowed by the guidelines? | Owner decision |
|---|---|---|---|
| Permissive, from Debian or vendored | libFLAC, libopus, libopusenc, miniaudio | yes: credit + licence text | none beyond D7 |
| LGPL, loaded from Debian | alsa-lib (needed for any capture), libsndfile, LAME | yes: credit; dynamic only (I) | whether LGPL libraries are acceptable at all (alsa-lib has no permissive equivalent on Linux, I) |
| GPL, as a separate program | `arecord`, `flac`, SoX, FFmpeg | yes as a separate program: source URL in the description and README (V) | not needed for the recommendation |
| Patent-pool formats | **AAC**: Via LA licences "end-user encoder and/or decoder products" in 2026 (V); **Opus**: royalty-free grants (V), but a Vectis pool (Dolby and Fraunhofer at launch, V; NTT later, secondary source) launched 2023-01-16 for "hardware devices", not "stand-alone software" (V), with enforcement against a device maker in 2025 (secondary source); **MP3**: Technicolor's licensing programme ended 2017-04-23 (V); Fedora shipped MP3 encoding after Red Hat Legal's review in May 2017 (secondary source); the exact last patent expiry is disputed (U) | no rule in the guidelines | a hobby project distributing software is outside the Opus pool's stated scope (I); AAC: avoid (D6); MP3: acceptable in 2026 (I) |
| Non-free | FDK AAC | not in the guidelines; Debian non-free (V) | no (D6) |

### 6.4 Packaging consequences

- **`Depends` vs vendored (D7).** Run-time loading keeps the shared build unchanged (the `_common` build "has no per-app
  libraries or include paths", V; Mesh Hop's BLE study chose `dlopen` for that reason, V) and keeps LGPL code out of our
  binary. Vendoring libFLAC or libopus sources would need build changes and security follow-up (I). The cross sysroot
  holds the run-time objects (`libasound.so.2`, `libFLAC.so.14`, `libopus.so.0`, `libsndfile.so.1`, `libmp3lame.so.0`,
  `libpipewire-0.3.so.0` and more) but no development headers (V), which also favours `dlopen` with our own declarations
  checked by an integration test (I).
- **Missing library at run time (I):** the app runs without the optional ones (FLAC or Opus greyed out with the package
  name to install); without `libasound.so.2` it says so and exits cleanly.

### 6.5 Trademark and trade dress (the tape-recorder look)

Not legal advice. The guidelines' **Affiliation** rule says never to imply that the project or an app "is made,
endorsed or sponsored by the maker of the stock device; no logos or names of it in your tile, icon, README or package
description" (V); the same care applies to any other manufacturer (I). The name NAGRA is a registered trademark (a US
registration for tape recorders filed in 1963 is listed under Kudelski S.A., secondary source; current status U), and a
product's distinctive appearance can be protected as trade dress (I). So (I): name the app generically (D2); do not use
the Nagra name, logo, model names or typefaces anywhere (app, icon, `app.json`, README, screenshots, commit messages
about the design); do not copy photographs, panel artwork or exact layouts of a real recorder; build every graphic from
scratch, in the project's own palette, as "a portable tape recorder" in general rather than one model.

## 7. Effort, phases, tests (question 6)

### 7.1 Phases

All costs are estimates (I), in worker sessions of about 0.4 M tokens; the controller's design passes and reviews are
extra.

| Phase | Content | Size | Cost (ws) | Owner time on boards | Exit |
|---|---|---|---|---|---|
| P0 probe | section 8: inventory, recordings under load, unplug and power-cut tests, mixer controls, encoder timings, a desktop board's server | S | 0.25-0.5 | 45-60 min | **done 2026-11-08** (P0 results) except the stereo-music encoder test; go: capture is clean, writer design adjusted |
| P1 core | `core/`: audio `Sys` (ALSA by `dlopen`, fakes), devices and identity, hot-plug, capture thread, ring buffer, meters and ballistics, DC blocker/HPF, WAV/BWF/RF64 writer with periodic header, journal and repair, naming, free space, settings; unit tests and golden files | M-L | 2-2.75 | | |
| P2 UI and meters | the controller's design: record page with meters, device and settings pages, recordings list, recovered-take notice, sizes; scenes, `lifecycle` scene | M-L | 2-3 (custom drawing of the tape-recorder look is the variable) | 15 min | |
| P3 playback | decoders (miniaudio `dr_wav`/`dr_flac`), output to the chosen card, transport, waveform overview cache, normalised playback gain | M | 1.25-1.75 | 10 min | |
| Packaging, docs (0.1) | `.desktop`, `app.json`, icon, README, BOARD-TESTS, amd64; docs-writer rows | S | 0.5-0.75 | 20 min (board protocol on 2 boards) | 0.1 published |
| P4 compressed formats | FLAC (libFLAC) and Opus (libopusenc, libopusfile) writers and readers by `dlopen`, repair for both, convert WAV -> FLAC/Opus | M | 1-1.5 | 10 min | |
| P5 polish | close/SIGTERM budget, auto-split, pre-roll, voice-activated mode, sidetone control (background recording, `X-Background=run`, left for later by D10) | M | 1-1.5 | 15 min | 0.2 |
| Later: trimming | lossless cut for WAV and FLAC, page cut for Opus | M | 0.75-1.25 | 10 min | |
| Later: metadata editor | `bext`/iXML and Vorbis comments, in place (4.5) | S-M | 0.75-1 | 5 min | |
| Later: sharing | copy to a USB stick (FAT32 split), MP3 export (LAME), a share over the network (design open) | M | 0.75-1.5 | 10 min | |
| Later: desktop-server path | record through PipeWire/PulseAudio when the card is busy, or device reservation (2.4) | S-M | 0.5-1 | 10 min | |

**Totals (I):** MVP 0.1 (P0 + P1 + P2 + P3 + packaging and docs) **6-8.75 ws (2.4-3.5 M tokens)**; with P4 and P5
**8-11.75 ws**; with every later item **10.75-16.5 ws (4.3-6.6 M tokens)**. For comparison: mmWave MVP 2.25-3 ws
(report 056), Reticulum MVP 4.5-6 ws (report 057). P0 is now spent (except the stereo-music encoder test, a few
minutes on a board), so the remaining MVP cost is P1-P3 plus packaging and docs; P1 grows slightly for the
two-thread writer, the repair step and the per-card mixer settings (I).

### 7.2 What can be tested where

| On the PC | Needs a board |
|---|---|
| Everything in `core/` against fakes (5.8): meters, ballistics, writers, repair, journal, naming, enumeration from fake trees, hot-plug order, fault injection; golden files checked with independent decoders; headless scenes and the lifecycle scene; the ABI check of the run-time-loaded functions against real headers in WSL | the owner's cards and headsets; overruns on the Zero 2 W through the adapter and hub with Bluetooth and Wi-Fi busy; power cuts; CPU and temperature of encoders; PipeWire on desktop boards; the tablet's microphone and PulseAudio; playback through the USB headset; the volume keys with a USB card |

### 7.3 Open unknowns

Collected in section 10. The ones that decided the plan (overruns, power cut, the owner's devices, PipeWire) were
measured in P0 (P0 results). Still open: stereo-music encoding, encoding while capturing, the two-thread writer under
SD-write stress, the cost of a 10 s sync.

### 7.4 Recommendation, with reasons

1. **P0 done** (D1): capture is light; the writer design changes (P0 results).
2. **Build option A** with ALSA direct and WAV/BWF first: no codec, the simplest crash repair, universal files.
3. **Add FLAC and Opus in 0.2** with BSD libraries loaded at run time: small files without any licence question.
4. **Keep AAC out**, keep MP3 to an optional export.
5. **Design the UI for the constraints in 5.9** (cassette look, option A: "UI direction"), especially the animation
   cost on the deck.

## 8. Probe P0 (as proposed; run 2026-11-08, see P0 results)

The plan below is kept as proposed. In practice P0 ran on the zero8 (the owner chose the zero8 or the deck, the
lowest-powered boards) with a PCM2902 card and the UC02 headset, the desktop check on the HackBerry, and the real power
pull approved and done; the tablet check (P0-h) was not run.

On the deck (zero7), with the owner present, using his cheapest USB card and a USB-C headset (through a micro-USB OTG
adapter); the desktop check on the Pi 5 or the HackBerry; one line on the tablet. Every command is read-only for the
system except the recordings in `~/rec-probe` and mixer changes that are written down first and restored. No package
is installed without the owner's word (P0-e). No launcher restart.

**P0-a, inventory (each device plugged in turn; measure: card ID, USB id and speed, formats, rates, channels, controls).**

```sh
mkdir -p ~/rec-probe && cd ~/rec-probe
uname -r; head -3 /etc/os-release; id -nG; ulimit -r
lsusb; lsusb -t                      # speed (12M = full speed, 480M = high speed), hub in between
cat /proc/asound/cards
ls -l /dev/snd /dev/snd/by-id /dev/snd/by-path 2>/dev/null
for c in /proc/asound/card[0-9]*; do echo "== $c"; cat $c/id; cat $c/usbid $c/stream0 2>/dev/null; done
arecord -l; arecord -L | head -40
amixer -c <N> scontrols; amixer -c <N> contents > mixer-<dev>.txt   # control names, ranges, dB
systemctl --user is-active pipewire pipewire-pulse wireplumber pulseaudio 2>/dev/null
dpkg -l alsa-utils 'libasound2*' 'libflac*' libopus0 libopusenc0 'libsndfile*' pipewire-alsa 2>/dev/null | tail -n +6
arecord -D hw:CARD=<id>,DEV=0 --dump-hw-params -d 1 -f S16_LE -t raw /dev/null 2>&1 | tee hwparams-<dev>.txt
```

**P0-b, 10-minute recordings (measure: overrun lines, exact file size, arecord CPU, USB errors, throttling).** Four
runs per device: direct on the OTG adapter; through the owner's usual hub; through the hub while copying a 100 MB file
to the deck over Wi-Fi (`scp` from the PC); with the Bluetooth keyboard typing.

```sh
vcgencmd measure_temp; vcgencmd get_throttled
timeout 640 arecord -D hw:CARD=<id>,DEV=0 -f S16_LE -r 48000 -c <1|2> \
  --buffer-time=500000 --period-time=20000 -d 600 -v rec-<dev>-<run>.wav 2> rec-<dev>-<run>.log &
P=$!; for i in 1 2 3 4 5; do sleep 120; ps -o pcpu=,rss= -p $P; done; wait $P
grep -c -i overrun rec-<dev>-<run>.log; ls -l rec-<dev>-<run>.wav   # expected 44 + 600 x 48000 x 2 x channels bytes
journalctl -k --since "-12 min" --no-pager | grep -i -E 'usb|xhci|dwc|snd' | tail -20
vcgencmd measure_temp; vcgencmd get_throttled
```

Also: the DC offset and noise floor from 30 s of silence (copy the file to the PC and read it with Audacity or
`ffmpeg -af astats`), and whether the headset's two channels are identical (D12).

**P0-c, unplug during a recording (measure: arecord's error text, the file's header and length, playability).**

```sh
arecord -D hw:CARD=<id>,DEV=0 -f S16_LE -r 48000 -c 1 -v rec-unplug.wav 2> rec-unplug.log &
sleep 20   # the owner unplugs the device now
sleep 5; cat rec-unplug.log; ls -l rec-unplug.wav; xxd -l 48 rec-unplug.wav
ls /dev/snd; cat /proc/asound/cards     # card gone? replug: does the same ID come back, with which number?
```

**P0-d, a hard stop (measure: what a killed writer leaves).** `kill -9` stands in for a power cut at the process level;
a real power cut is done only if the owner wants to (he pulls the power after 30 s of recording; the board's file system
is then checked at boot as after any of his accidental unplugs).

```sh
arecord -D hw:CARD=<id>,DEV=0 -f S16_LE -r 48000 -c 1 rec-kill.wav 2>/dev/null & P=$!
sleep 30; kill -9 $P; sync; ls -l rec-kill.wav; xxd -l 48 rec-kill.wav
```

On the PC: `ffprobe`, VLC, Audacity and the phone on `rec-unplug.wav` and `rec-kill.wav`: which play, how long.

**P0-e, encoder timings (measure: real time factor, CPU, temperature).** Needs `flac` and `opus-tools`
(`sudo apt install flac opus-tools`, about 1 MB, only with the owner's word; else run on the Pi 4B first). A 10-minute
48 kHz stereo music WAV copied from the PC serves as the music test.

```sh
time flac -5 -f -o t.flac rec-<dev>-1.wav;  ls -l rec-<dev>-1.wav t.flac
time flac -8 -f -o t8.flac music48s.wav;    ls -l music48s.wav t8.flac
time opusenc --bitrate 32 rec-<dev>-1.wav t32.opus
time opusenc --bitrate 128 --comp 10 music48s.wav t128.opus
time opusenc --bitrate 128 --comp 5 music48s.wav t128c5.opus
vcgencmd measure_temp; vcgencmd get_throttled
```

**P0-f, desktop board with PipeWire (Pi 5 or HackBerry; measure: server present, who holds the card, busy errors).**

```sh
systemctl --user is-active pipewire wireplumber pipewire-pulse; wpctl status | head -40
arecord -D hw:CARD=<id>,DEV=0 -f S16_LE -r 48000 -c 1 -d 5 idle.wav        # with nothing else playing
pw-record --target <node> -  > /dev/null & Q=$!; sleep 2
arecord -D hw:CARD=<id>,DEV=0 -f S16_LE -r 48000 -c 1 -d 5 busy.wav; kill $Q   # expect "busy"?
arecord -D default -f S16_LE -r 48000 -c 1 -d 5 viaserver.wav                   # through pipewire-alsa?
```

**P0-g, mixer controls (measure: names, dB ranges, AGC, sidetone).** Write down every value first
(`amixer -c <N> contents > before.txt`), then `amixer -c <N> sset 'Mic' 50%` and back, `amixer -c <N> sget 'Auto Gain
Control'`, sidetone with headphones on: `amixer -c <N> sset 'Mic' playback unmute` and back; restore from `before.txt`.

**P0-h, tablet (one minute).** `arecord -l`, `pactl info | head -5`, `arecord -D default -d 5 t.wav` with the internal
microphone.

**Cleanup.** `rm -rf ~/rec-probe`; the mixer restored; packages from P0-e removed (`sudo apt remove flac opus-tools`)
if the owner wants.

## 9. Risks, ranked

1. **USB audio on the Zero 2 W's OTG port**: P0 saw clean capture at under 0.5 % of a core, but SD-card write bursts
   caused overruns of up to 2.9 s with a single-threaded writer (V). Mitigation: 1024-frame periods, a 500 ms+ buffer,
   a separate writer with an 8 s+ ring (P0 results); count and mark every gap; re-test under SD-write stress.
2. **Power cuts and unplugging**: measured in P0 (about 25 s lost without sync, about 6 s with a 10 s sync; header
   sizes left as placeholders; V). Mitigation: 4.4 (sync interval, header updates, journal) and a repair step on launch.
3. **Desktop boards' sound servers** hold the card: confirmed on the HackBerry (EBUSY; silence for other apps while
   `hw:` is held; V). Mitigation: D9 as confirmed, with a notice in the UI.
4. **Device variety**: controls missing, odd formats (S24_3LE, 2 identical channels), no serial numbers, wrong TRRS
   standard (I). Mitigation: probe and adapt to what each device reports; a fixture per owner device.
5. **The UI's cost on the deck**: a heavy animated look could starve the capture or make the UI slow (I). Mitigation:
   5.9; the audio threads never wait for the UI.
6. **Hidden recording without a visible indicator**: avoided for now, no background recording (D10); returns if
   background recording is revisited (5.5).
7. **Licence or patent missteps**: an AAC or GPL path slipping in (I). Mitigation: section 6, D5-D7.
8. **Privacy and the law**: recording people may need their consent depending on the country (I); the recording state
   must be obvious. Not ours to enforce; the README says so.

## 10. Not verified

Items settled by P0 on 2026-11-08 are marked "(P0: ...)".

- Raspberry Pi OS **trixie** Lite has no sound server (verified only for Bookworm, from the Pi announcement and a forum
  post); which server runs on the Pi 5 while the launcher runs (P0: PipeWire + WirePlumber on the HackBerry, and ALSA
  `default` records through it).
- (P0: a direct `hw:` open fails with "busy" while a PipeWire client records.) WirePlumber's `alsa.reserve` defaults and
  device-reservation behaviour remain untested.
- USB audio through hubs (single or multi TT) and with Bluetooth and Wi-Fi busy; crackles by ear (P0: no overruns or USB
  errors in plain capture on the zero8's OTG port; overruns only under SD-card write stress).
- Devices beyond the two tested (P0: PCM2902 card and UC02 headset, both mono 48 kHz; controls and identities in the P0
  results); TRRS standards.
- The Apple USB-C adapter's capture abilities (UAC2/UAC3, rates).
- What VLC, Audacity, Windows and phone players do with truncated WAV, FLAC and Ogg Opus files (FFmpeg's WAV rule is
  verified; P0: `arecord` leaves placeholder header sizes after a power pull and after an unplug).
- CPU cost of FLAC and Opus encoding of stereo music, and of encoding while capturing (P0: mono speech measured).
  Decoding cost on a Cortex-A53.
- Whether hot-plugged USB cards are picked up by the launcher's volume keys, and when.
- Whether `alsa-utils` is on Pi OS Lite trixie by default (P0: `RLIMIT_RTPRIO` is 0 on the zero8).
- The tablet: internal microphone, PulseAudio vs PipeWire under the appliance and desktop variants, Ubuntu 26.04
  package names (`libflac14`, `libasound2t64`).
- The exact last expiry of MP3 patents; AAC-LC patent expiry; the current status of the NAGRA registrations.
- Licences of alsa-utils, SoX, libopusfile, libogg, libvorbis (not read; Debian packages).
- The iXML specification's current version (site certificate expired on the day of reading).
- True-peak filter details (BS.1770-5 annex not read); AES17's RMS convention.
- The LVGL refresh period on the device build (33 ms read in the render harness configuration).
- `/run/systemd/timesync/synchronized` as the clock-set signal on our images.

## 11. Questions for the owner

All answered by the owner on 2026-11-08.

1. **P0 (D1):** answered: run on the zero8 or the deck (the lowest-powered boards); it ran on the zero8 with a PCM2902
   card and the UC02 headset.
2. **Use:** answered: anything (general purpose).
3. **Format policy (D3-D6):** answered: agreed.
4. **Libraries (D7):** answered: agreed.
5. **Background recording (D10):** answered: no, not for now; the app owns the screen while recording.
6. **Default rate and channels (D11, D12):** answered: agreed.
7. **Storage (D14):** answered: `~/Recordings`; a USB stick later.
8. **Name (D2):** answered: **Cassette** (`cassette`).
9. **Power-cut test:** answered: approved, and done in P0 on the zero8.
10. **Desktop boards:** answered: the HackBerry; P0-f ran there.

## Sources

All read on 2026-11-08 unless noted.

- ALSA: alsa-lib PCM interface, https://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html (states, `-EPIPE`,
  `snd_pcm_recover`, `-ENODEV`, mmap); PCM plugins, https://www.alsa-project.org/alsa-doc/alsa-lib/pcm_plugins.html
  (hw, plug, dsnoop); `alsa.conf` (`defaults.pcm.card 0`),
  https://github.com/alsa-project/alsa-lib/blob/master/src/conf/alsa.conf ; alsa-lib tag v1.2.16.1 (2026-06-12).
- Linux 7.4-rc1 (master, 2026-11): `sound/usb/mixer.c` (control names, quirks), `sound/usb/proc.c` (`stream%d`,
  `usbid`), `sound/usb/card.c`: https://github.com/torvalds/linux/tree/master/sound/usb
- systemd v262.2 (2026-11-05): `rules.d/60-persistent-alsa.rules`, `rules.d/78-sound-card.rules`,
  https://github.com/systemd/systemd/tree/main/rules.d
- Raspberry Pi documentation sources (`develop`, power page updated 2026-10-12): `power-supplies.adoc` and
  `usb-bus-on-raspberry-pi.adoc`, https://github.com/raspberrypi/documentation/tree/develop/documentation/asciidoc/computers/raspberry-pi
- Raspberry Pi, "Bookworm - the new version of Raspberry Pi OS" (October 2023, PipeWire replaces PulseAudio):
  https://www.raspberrypi.com/news/bookworm-the-new-version-of-raspberry-pi-os/ ; RetroPie forum (Lite has no
  PipeWire, secondary): https://retropie.org.uk/forum/post/291570
- pi-gen `master` (last commit 2026-11-05), `stage2/01-sys-tweaks/01-run.sh` (first user's groups):
  https://github.com/RPi-Distro/pi-gen
- dwc_otg USB audio reports (secondary): https://github.com/raspberrypi/linux/issues/575 ,
  https://github.com/raspberrypi/linux/issues/2363 , https://docs.picoreplayer.org/releases/pcp115b/
- C-Media CM108AH datasheet Rev 2.1 (27 pages):
  https://atta.szlcsc.com/upload/public/pdf/source/20190327/C371346_375F2440A66FD1C882296D07948AE54C.pdf
- StarTech USBCAUDIO2 datasheet (2019-12-11): https://media.startech.com/cms/pdfs/usbcaudio2_datasheet.pdf
- Kernel bugzilla 217501, Apple USB-C dongle and 44.1 kHz (not readable on the day, cited from a search result):
  https://bugzilla.kernel.org/show_bug.cgi?id=217501
- miniaudio 0.11.26 (2026-10-31), `LICENSE`, README and manual: https://github.com/mackron/miniaudio ,
  https://miniaud.io/docs/manual/index.html ; the launcher's vendored 0.11.25 (`ext_components/Miniaudio`).
- WirePlumber 0.5.19 ALSA configuration: https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/alsa.html ;
  PipeWire `COPYING` (MIT) and tag 1.6.11: https://gitlab.freedesktop.org/pipewire/pipewire ; PulseAudio `LICENSE`:
  https://gitlab.freedesktop.org/pulseaudio/pulseaudio/-/blob/master/LICENSE ; WirePlumber device reservation
  (secondary): https://gitlab.freedesktop.org/pipewire/pipewire/-/issues/191
- libsndfile 1.2.2 (2023-08-13): https://libsndfile.github.io/libsndfile/ , `command.html`, `formats.html`,
  `include/sndfile.h` (`SF_BROADCAST_INFO`, `sf_set_chunk`): https://github.com/libsndfile/libsndfile
- FLAC 1.5.0 (2025-02-11), README (licences), `include/FLAC/stream_encoder.h`: https://github.com/xiph/flac ; features:
  https://xiph.org/flac/features.html ; RFC 9639 (December 2024): https://www.rfc-editor.org/rfc/rfc9639.html
- Opus: licence and patent grants, https://opus-codec.org/license/ ; RFC 6716 (sweet spots):
  https://www.rfc-editor.org/rfc/rfc6716.html ; RFC 7845 (Ogg Opus, April 2016):
  https://www.rfc-editor.org/rfc/rfc7845.html ; libopus tag v1.6.1 (2026-01-13): https://github.com/xiph/opus ;
  libopusenc v0.3 (2026-01-03), `include/opusenc.h`, `src/opusenc.c` (delays): https://github.com/xiph/libopusenc
- Vectis Opus pool launch (2023-01-16): https://ipwatchdog.com/press/vectis-ip-launches-patent-pool-opus-codec/ ;
  enforcement against Acer (secondary, 2025-12-10):
  https://www.accessnewswire.com/newsroom/en/computers-technology-and-internet/vectis-opus-patent-pool-licensor-enforces-against-acer-before-the-1114727
- Via LA AAC licensing: https://www.via-la.com/licensing-2/aac/
- Fraunhofer IIS mp3 page (licensing programme ended 2017-04-23):
  https://www.iis.fraunhofer.de/en/ff/amm/consumer-electronics/mp3.html ; Phoronix on Fedora MP3 (secondary):
  https://www.phoronix.com/news/Full-MP3-Support-In-Fedora
- LAME (v4.0, July 2026; LGPL): https://lame.sourceforge.io/
- WavPack 5.9.0 (2026-01-18): https://github.com/dbry/WavPack ; ALAC (Apache-2.0): https://github.com/macosforge/alac
- FFmpeg `libavformat/wavdec.c` (master): https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/wavdec.c
- Debian trixie package pages (versions, dependencies): https://packages.debian.org/trixie/ (`libasound2t64`,
  `libflac14`, `libopus0`, `libopusenc0`, `libopusfile0`, `libogg0`, `libvorbisenc2`, `libmp3lame0`, `libmpg123-0t64`,
  `libsndfile1`, `libwavpack1`, `libpipewire-0.3-0t64`, `libpulse0`, `alsa-utils`, `pipewire-alsa`, `flac`,
  `opus-tools`, `lame`, `ffmpeg`, `sox`); fdk-aac search: https://packages.debian.org/search?keywords=fdk-aac ;
  `copyright` files: https://metadata.ftp-master.debian.org/changelogs/main/ (flac 1.5.0+ds-2, opus 1.5.2-2, libopusenc
  0.2.1-2, lame 3.100-6, mpg123 1.32.10-1+deb13u1, libsndfile 1.2.2-2+deb13u1, wavpack 5.8.1-1, alsa-lib
  1.2.14-1+deb13u1, ffmpeg 7.1.5-0+deb13u1)
- Linux ext4 documentation: https://www.kernel.org/doc/html/latest/admin-guide/ext4.html (`commit`, `data=ordered`),
  https://www.kernel.org/doc/html/latest/filesystems/ext4/journal.html
- EBU R 68 (2000-12-28): https://tech.ebu.ch/publications/r068 ; EBU Tech 3285 v2 (2011-05-20):
  https://tech.ebu.ch/publications/tech3285 ; EBU Tech 3306 v2.0 (June 2018, pointer to ITU-R BS.2088):
  https://tech.ebu.ch/publications/tech3306 ; EBU Tech 3341 v4.0 (2023-11-21): https://tech.ebu.ch/publications/tech3341
- ITU-R BS.1770-5 (11/2023): https://www.itu.int/rec/R-REC-BS.1770
- Secondary sources for meter ballistics: https://en.wikipedia.org/wiki/Peak_programme_meter ,
  https://en.wikipedia.org/wiki/VU_meter ; iXML: https://en.wikipedia.org/wiki/IXML and
  https://www.sonosax.ch/wp-content/uploads/2016/12/SX-R4-iXML.pdf
- Android supported media formats: https://developer.android.com/media/platform/supported-formats ; Apple Opus support
  (secondary, partial): https://bugs.webkit.org/show_bug.cgi?id=238546
- NAGRA trademark listing (secondary): https://trademarkdir.com/trademark/nagra-72170842 ; https://en.wikipedia.org/wiki/Nagra
- Opus decoder speed on a Pi 3 (secondary, 2019): https://ffmpeg.org/pipermail/ffmpeg-devel/2019-April/242365.html
- Our code and documents: launcher `github/main` @ `28817a50` (`docs/APP-CONTRACT.md`, reports 035, 054, 056, 057 (branch
  `study-reticulum`), `projects/APPLaunch/pizero2w/README.md`, `install.sh`, `projects/APPLaunch/main/ui/board_facts.cpp`,
  `projects/APPLaunch/main/ui/multitask/multitask_model.hpp`, `ext_components/cp0_lvgl/src/cp0_capabilities.c`,
  `src/cp0/cp0_audio_capture.cpp`, `src/cp0/cp0_lvgl_soundcard.cpp`, `projects/APPLaunch/tests/render/config/lvgl_config.h`,
  `ext_components/Miniaudio`, `SDK/github_source/static_lib_v0.0.7`); apps `origin/main` @ `906cbf7`
  (`docs/APP-GUIDELINES.md`, `apps/_common/README.md`, `apps/mmwave-radar/app.json`, `apps/mmwave-radar/src/core/port_sys.hpp`,
  `apps/home-assistant/README.md`, `apps/mesh-hop/docs/BLE.md`, `apps/deck-atlas/src/ui/worker.hpp`).
