# 062: VHS, the camera and photo app (plan)

Date 2026-11-13. A read-only plan: no product code was written, nothing was installed, no board or repository was
changed, nothing was committed or pushed. It began as a study of the owner's webcam ideas and was turned into the plan of
**VHS** on the owner's direction of the same day; the study's content is kept and reorganised.

**Updated 2026-11-14:** the owner's decisions (section 17) and the P0 results with proposed revisions (section 16).

**Owner direction (2026-11-13, verbatim):** "start a plan for "VHS" our Camera and photo app. we only have one camera
available and you tested it. app should allow still photos and video recording. similar to cassete it should allow to
choose sound input list "scenes" as short movie, allow codecs selection with expected performance ad for slow device post
recording transcoding. it should also have playback windowed and full screen (so out of launcher rules in that case)
with on tap overlayed controls."

**Earlier owner ideas (the webcam conversation of 2026-11-13),** now the "later" list (section 14): QR scanning (Wi-Fi,
URLs, pairing) and document scanning into Notes; a camera server with motion clips, a baby-monitor mode and a Home
Assistant tile; geotagged photos with Deck Atlas's GPS; a magnifier view using the zoom.

**Interpretation to confirm (D2):**
- (a) **One supported camera:** the UGREEN FineCam 1080P CM969 (USB a108:2240). No design for several cameras or CSI
  cameras (later).
- (b) **Photos and video recording are the core** of 1.0. QR, motion, server, geotag and magnifier are later items.
- (c) **A sound input chooser like Cassette's:** the camera's own USB mic, every input Cassette's backend lists, and
  Bluetooth headsets through the sound server where one runs. Cassette's input list, guards and silence detection are
  reused, with care for this mic's volume-range warning.
- (d) **Scenes:** the list of recordings. Each scene is a short movie (thumbnail, duration, size, codec, date); photos are
  listed too. Actions: play, convert, delete, rename, copy.
- (e) **Codec selection with expected performance:** a picker per board, each codec and preset with a badge (OK /
  borderline / too slow for live), CPU and disk-per-hour hints from the measurements, and a recommended default.
- (f) **Post-recording transcoding for slow devices:** record cheaply (MJPEG copy, big files), convert later on the
  board in a queue.
- (g) **Playback windowed** (in a page, under the launcher's header rules) **and full screen** (the whole screen, out of
  the header rules, the owner's words) **with controls overlaid on tap**.
- (h) From the study: capture design, orientation switch and unplug, power advice, storage and SD wear, time-lapse (a
  recording mode, P3), camera controls, launcher requests, privacy indicator.

**Sources read:**
- The memory note `project_webcam_app_ideas.md` (camera capabilities, every benchmark of 2026-11-13); the scripts
  `webcam-bench.sh` (RPI52), `webcam-bench-z8.sh` (zero8), `webcam-bench-p4.sh` (Pi 4B); the files in `webcam-test/`
  (three photos, one clip), whose headers I parsed.
- Reports 059 and 060.
- Apps repo `origin/main` d5216dc (equal to GitHub `main`, checked with `git ls-remote`), extracted with `git archive` into
  the scratchpad folder `062-apps/`: Cassette, Vinyl, Deck Atlas, Home Assistant, Markdown Notes, `_common`,
  `docs/APP-JSON.md`, `docs/APP-GUIDELINES.md`. The stale local checkout's working tree was not used.
- Launcher `main` d54f2205 (0.10.2 notes): `docs/APP-CONTRACT.md`, `docs/dev/checklists/app-design.md` and
  `app-design-scenes.md`, `docs/dev/decisions.md`, `docs/OPEN_SOURCE_COMPONENTS.md`, `LICENSES/`, the toolkit's camera
  service (`cp0_lvgl_camera.cpp`, `cp0_camera_frame_codec.*`, `Kconfig`), `app_display_order.cpp`, `desktop_app_loader.cpp`.
- Memory notes: multi-board display facts, Cassette, the SSH indicator, the launcher SDK (035), state snapshots.

**Tags (as in 058 to 060):** **[F]** fact, with its evidence; **[I]** my inference; **[U]** unverified, still to measure.

**Board access on 2026-11-13 (about 21:20 to 21:45):**
- Reachable: **RPI52** (Pi 5, .160) and **Pi 4B** (.167). Read-only commands only:
  - `dpkg-query`, `apt-cache show`, reads of `/sys` and `/proc`, `nm -D` on installed libraries;
  - `v4l2-ctl --list-formats*` on the codec nodes, `ffmpeg -encoders` / `-devices` (queries, no encode);
  - the launcher's `board.state`.
  Scripts and raw output: scratchpad `062-probe.sh`, `062-probe2.sh`, `062-probe-out/`.
- Not reachable: HackBerry (.214), zero8 (.184), Pi 3A+ (.170), RPI53 (.175), the tablet (.183), the deck (`zero7.local`
  did not resolve). Hosts .155 and .163 answer but are not in `known_hosts`; the helper refused them before any login.
- The camera was plugged into neither reachable board; every camera fact comes from the memory note of the same day.

---

## 0. Summary

1. **[F] The one camera is an MJPEG camera:** MJPEG up to 1920x1080 at 30 and 60 fps, YUYV only at 640x480 and 640x360,
   no still mode (a photo is a video frame, 2 MP at most), a portrait switch that re-enumerates the device, a USB mic
   card `CM969`, 15 frames to settle exposure.
2. **[I] Decoding MJPEG is the real CPU cost** of the live view and of MJPEG playback. Pi 4B anchor: 0.44 core at 720p30,
   0.91 core at 1080p30. A Zero 2 W needs about 1.1-1.3 and 2.3-2.7 cores for the same. So the Zero boards preview from
   640x360 and record 720p MJPEG by default.
3. **[F] The Pi 4B's VideoCore codec decodes MJPEG and H.264 and encodes H.264 in hardware**; **[I] the Zero 2 W and Pi
   3A+ probably have the same block** (the zero8 bench never tried it: P0-d is the most important probe). **[F] The Pi 5
   has no H.264 or JPEG hardware**, only HEVC decode; the CM5 HackBerry is the same chip [I].
4. **Capture:** own V4L2 code (about 400-600 lines). **Preview decode:** TurboJPEG through dlopen (`libturbojpeg0`,
   542 kB) with DCT scaling, so LVGL never scales.
5. **The codec picker (section 4)** shows each codec with a badge from the measurements:
   - live OK: MJPEG copy everywhere; x264 480p on the Zero class and Pi 3A+; hardware H.264 720p/1080p on the Pi 4B;
     x264 up to 1080p veryfast on the Pi 5;
   - borderline: x264 720p on the Pi 3A+, veryfast 720p on the Pi 4B;
   - too slow for live: x264 720p and up on the Zero class, x265 and medium presets everywhere.
   H.265 and AV1 are conversion targets on the Pi 4/5 class only.
6. **Storage:** MJPEG copy costs 4-13 GB per hour at 720p-1080p30 and 25 GB per hour at 1080p60 (measured); H.264 costs
   0.4-3.6 GB per hour. Converting a scene saves about 85-95 % of its size.
7. **Post-recording conversion (section 5)** runs inside VHS (in front, or hidden on multitasking boards), at nice 10
   on two cores, paused above 75 C. It keeps the original until the result is verified. Estimated minutes of work per
   minute of video (two cores):
   - Zero 2 W: about 1.5 (720p), 4 (1080p);
   - Pi 3A+: 0.6 (720p), 1.7 (1080p);
   - Pi 4B hardware: about 0.2-0.5;
   - Pi 5: 0.35-0.95.
   Hours-long jobs get a lower resolution suggested, or a PC.
8. **Sound input (3.6):** Cassette's input list and guards (DONT_MOVE, identity guard, digital-silence notice), the
   camera mic as the default, "No sound" as a choice. The mic's level is left as found (the kernel says its volume range
   "is probably wrong"). A/V sync uses the audio clock as master.
9. **Scenes (section 6):** one list of movies and photos: thumbnail, duration, size, codec, date, a lock mark. Actions:
   play, convert, rename (Cassette's prefix rules), delete (asks first), lock, copy to a mounted drive. Files live in
   `~/VHS/` with Cassette-style names.
10. **[F] Full screen is technically open to any `X-Fullscreen` app:** the launcher draws nothing over it, the 3 s Esc
    hold still ends it, the volume keys still act. The header band is drawn by the app itself (`cp0::ui::Page`), so
    the only obstacle is the design checklist (D-02, D-03, D-07, D-08, D-10, D-12). That needs **owner ruling R-7**
    (8.2). No launcher change is needed.
11. **Playback (section 8):**
    - FFmpeg for demux and decode (a hard Depends from P1);
    - hardware H.264 decode on the VideoCore boards;
    - Vinyl's output stack (output chooser, per-output volume, Bluetooth, media keys) and Vinyl's tap-and-drag seek bar;
    - MJPEG seeks to any frame; H.264 seeks to keyframes while scrubbing and exactly on release.
12. **[F] Geotagging cannot work while VHS is in front** (Deck Atlas writes `position.state` only while it is itself in
    front). **[F] The header band is the launcher's**, so the trustworthy "camera in use" glyph must be a launcher
    capability (L1).
13. **Launcher requests:** L1 camera/mic glyph, L2 screen off for the app in front, L3 power warning fact (**[F] the Pi
    4B shows active under-voltage, 0x50005, at idle today**), L4 location service, L5 Wi-Fi offer, L6 idle inhibit.
    None is needed for P1 or for full-screen playback.
14. **[F] "VHS" is not in the launcher's built-in tile list** (`app_display_order.cpp`; "Camera" and "Rec" are), so its
    tile sorts like any installed app. Id `vhs` (D1).
15. **Phases (section 13), the owner's order with two moves:**
    - P0 probes: 1 Controller day, about 60 owner minutes.
    - P1: live view, photo, MJPEG recording with the camera mic, scenes, windowed playback. 18-24 worker-days.
    - P2: codec picker, H.264 recording, input chooser, full-screen playback, and **basic camera controls moved here**.
      14-19 worker-days.
    - P3: **the convert queue first**, then time-lapse and the remaining controls. 12-16 worker-days.

---

## 1. Scope

### 1.1 Tiers

| Tier | Features | Why |
|---|---|---|
| **P1 (0.1.0)** | live view; photo (the camera's JPEG plus EXIF); **video recording as MJPEG copy with the camera's mic** (MKV + AAC); **scenes list** (movies and photos: play, delete, rename, lock); **windowed playback** (Vinyl's output stack); unplug and orientation switch handled; notices (no camera, busy, no permission, weak power, disk full) | the owner's P1; MJPEG copy costs no CPU on any board, so recording works everywhere from day one |
| **P2 (0.2.0)** | **codec picker** with badges; **H.264 recording** (hardware where present, x264 elsewhere) in fragmented MP4; **sound input chooser** (Cassette's list, Bluetooth via the server); **full-screen playback with tap overlay**; basic camera controls (exposure, white balance, zoom, power-line) | the owner's P2, plus the cheap controls every recording benefits from |
| **P3 (0.3.0)** | **post-recording convert queue**; **time-lapse** as a recording mode; the full controls page (every UVC control, per-camera memory) | the owner's P3; the queue first because it makes the Zero boards practical |
| **Later** (section 14) | QR scan, document scan, motion clips, LAN stream and Home Assistant, baby-monitor sound, geotag, magnifier, more cameras (other UVC, CSI) | each needs a probe, a launcher item or an owner privacy decision |
| **No** | cloud upload; RTSP or WebRTC; face or object recognition; OCR; background recording in 1.0; a virtual webcam; 4K and HDR | out of the direction or out of the boards' reach |

### 1.2 The camera [F memory, 2026-11-13]

| Property | Value |
|---|---|
| Identity | UGREEN FineCam 1080P CM969, USB a108:2240, UVC 1.10, USB 2.0 |
| Landscape modes | 1920x1080, 1280x960, 1280x720, 1024x576, 800x600, 640x480, 640x360 |
| Portrait | the switch re-registers the device; the same modes rotated (1080x1920...) |
| Formats | MJPEG to 1920x1080 at 30 and 60 fps; YUYV only 640x480 and 640x360 at 30 fps |
| Still mode | none (photo = a video frame) |
| Controls | brightness, contrast, saturation, hue, sharpness, white balance auto / temperature 2300-6500 K, exposure auto / absolute, zoom 100-400, power line 50 Hz |
| Microphone | USB audio card `CM969`; the kernel warns its volume range (3072) is "probably wrong"; the 2026-11-12 clip holds 48 kHz, 2 channels [F parsed] |
| Warm-up | about 15 frames (0.5 s) to settle exposure |
| [U] | autofocus, IR, activity LED, `bMaxPower`, the identity after the switch, whether frames carry Huffman tables |

VHS enumerates cameras by capability (2.1), so another UVC camera may work, but it is not supported or tested (D3).

---

## 2. Capture and live-view architecture

### 2.1 Capture options

| Option | Dependency | Verdict |
|---|---|---|
| **Raw V4L2** (`QUERYCAP`, `ENUM_FMT/FRAMESIZES/FRAMEINTERVALS`, `S_FMT`, `S_PARM`, `REQBUFS` mmap, `QBUF/DQBUF` with `poll()`, `STREAMON/OFF`, `QUERYCTRL/QUERYMENU/G_CTRL/S_CTRL`) | none (the kernel header `videodev2.h` at build time) | **recommended**: 400-600 lines behind a fakeable function table, like Cassette's `AlsaApi` |
| libv4l2 / libv4lconvert | `libv4l-0t64` (LGPL), installed on both probed boards [F] | not needed for MJPEG; its conversion is slower than TurboJPEG [I] |
| FFmpeg libavdevice | `libavdevice61`: not on the Pi 4B [F]; it pulls libavfilter, libavc1394, SDL... [F apt-cache] | no |
| GStreamer | on RPI52 only [F]; plugins absent on Lite | no |
| cp0_lvgl camera service | libcamera + libjpeg linked; `CONFIG_CP0_LVGL_INIT_CAMERA` default `n` [F]; IMX219 filter [F] | no: CSI only, upstream code awaiting an owner decision (decisions.md 2026-10-22) |

**Finding the camera.** [F] The Pi 4B lists 14 non-camera nodes (`bcm2835-codec-*`, `bcm2835-isp-*`, `rpi-hevc-dec`), RPI52
17 (`rpi-hevc-dec`, `pispbe-*`), and a UVC camera adds a metadata node. VHS keeps nodes with `V4L2_CAP_VIDEO_CAPTURE`,
without `V4L2_CAP_VIDEO_M2M`, offering MJPG or YUYV. It remembers the camera by USB identity, never by `/dev/video0`
(D-15). **[F] The user is in the `video` group on both probed boards**; when not, a notice names the fix (D-62).

### 2.2 MJPEG decode

| Decoder | Loading | Scaled decode | Licence | Verdict |
|---|---|---|---|---|
| **TurboJPEG API** (`libturbojpeg.so.0`) | dlopen; `tjInitDecompress`, `tjDecompress2`, `tjDecompressToYUVPlanes`, `tjGetScalingFactors` | M/8 in the IDCT | IJG + BSD-3 + zlib | **recommended**; Depends `libturbojpeg0` (542 kB, libc only [F]); installed on RPI52, **not on the Pi 4B** [F] |
| libjpeg API (`libjpeg.so.62`) | dlopen needs the struct layout by hand | yes, RGB565 out | same | possible (on both boards [F]); fragile ABI |
| stb_image 2.30 (Vinyl's copy) | built in | no; fails without DHT unless tables are inserted | MIT / PD | fallback only |
| FFmpeg `mjpeg` | libavcodec | no | GPL as built | for playback of MJPEG scenes only when TurboJPEG is busy; not the live view |
| VideoCore decoder (`bcm2835-codec-decode`) | V4L2 m2m: MJPG/H264 in, YU12/NV12/**RGB565**/RGBA out [F Pi 4B] | the ISP scales [I] | driver + Pi firmware | P2/P3 optimisation for the live view and MJPEG playback on VideoCore boards |

**Cost at 30 fps** (cores = CPU seconds per wall second). Anchor: the Pi 4B's FFmpeg MJPEG decode with conversion, 0.44
core at 720p30 and 0.91 at 1080p30 [F]. Scaled by CPU class (A53 1.0 GHz about 2.5-3x slower than A72 1.8 GHz, A53
1.4 GHz 1.8-2.1x, A76 2.4 GHz 2-2.5x faster) [I]:

| Stream | Pi 4B | Zero 2 W | Pi 3A+ | Pi 5 / CM5 |
|---|---|---|---|---|
| 640x360 | about 0.11 | **0.28-0.33** | 0.20-0.23 | 0.05 |
| 640x480 | about 0.15 | 0.37-0.45 | 0.27-0.31 | 0.06-0.07 |
| 1280x720 | 0.44 [F] | 1.1-1.3 | 0.8-0.92 | 0.18-0.22 |
| 1920x1080 | 0.91 [F] | 2.3-2.7 | 1.6-1.9 | 0.36-0.45 |

DCT scaling cuts the IDCT and colour work, not the Huffman decoding: a 1/4-scale decode of 1080p may still cost 40-60 %
of a full one [I]. P0-c measures TurboJPEG on zero8 and the Pi 3A+.

### 2.3 Live view per board (to confirm in P0-c)

| Board | Stream while previewing | Decode to the screen | Rate |
|---|---|---|---|
| deck, zero8 | MJPEG 640x360 (YUYV only if P0-e passes: 110 Mbit/s isochronous on the Zero's dwc2 port is a risk [I]) | 8/8 at 640x480; 6/8 = 480x270 at 480x320; 4/8 = 320x180 at 320x480 | 30 fps; while recording 720p, every 2nd or 3rd frame at 1/2 |
| Pi 3A+ | MJPEG 640x360; the recording stream while recording | 6/8 or 8/8; 720p at 3/8 | 30 fps; 10-15 while recording |
| Pi 4B | MJPEG 1920x1080 | 1/2 = 960x540 or 3/8 = 720x405 | 30 fps |
| RPI52, RPI53, HackBerry | MJPEG 1920x1080 | 3/8 = 720x405 (720x720), 1/2 (800x480) | 30 fps |
| tablet | MJPEG 1920x1080 | 5/8 = 1200x675 | 30 fps |

Rule: the camera mode and the M/8 factor that give the largest image fitting the box, shown 1:1; LVGL's image zoom is
never used for moving pictures.

### 2.4 Pipeline, frame dropping, latency

- **Threads:**
  - capture: `poll()`, `DQBUF` every frame, copy the JPEG (about 100 KB), requeue at once, hand the copy to the recorder
    and to a one-slot preview mailbox;
  - decode: takes only the newest frame (older ones are dropped, so the picture never lags);
  - LVGL: swaps the double buffer (RGB565 or XRGB8888 per `screen.state` `bpp`) and invalidates only the picture.
- **Buffers:** 3-4 mmap buffers; uvcvideo sizes each from the camera's maximum frame, typically width x height x 2 for
  MJPEG [I] (4 MB at 1080p), so 3 on the Zero class.
- **Warm-up:** 15 frames dropped after `STREAMON` before a photo or a recording's first frame.
- **Latency:** exposure and USB (33 ms), decode (5-30 ms), render and flush (10-30 ms): about 70-120 ms [I]; test B12.
- **Timestamps:** `CLOCK_MONOTONIC` in every `v4l2_buffer` (uvcvideo default; `hwtimestamps` is a module parameter [F]),
  the base of A/V sync.

### 2.5 Rotation and the toolkit

- [F memory] The Pi 3A+ and the Pi 5 with the HyperPixel needed the picture pre-rotated 90 degrees clockwise; the
  HackBerry none. [F contract 3] `APPLAUNCH_SCREEN_ROTATE` carries this and cp0_lvgl's backend rotates in its flush;
  zero8 in PDA mode rotates too [I].
- [I] A moving picture redraws its rectangle every frame: 130k-385k pixels rotated per frame, about 3-10 ms on an A53.
- The P1 spike measures preview fps with rotation on zero8 (PDA) and the Pi 3A+ before the layouts are frozen.

---

## 3. Recording

### 3.1 Measured encoders (2026-11-13, 10 s live, `ffmpeg -benchmark`; CPU includes FFmpeg's MJPEG decode)

| Board | MJPEG copy | x264 ultrafast | x264 veryfast | x264 medium | Hardware H.264 |
|---|---|---|---|---|---|
| zero8 (Zero 2 W) | 1080p30 37 MB/10 s at 3 % CPU; 1080p60 70 MB/10 s | 480p 0.95 core; 720p 3.0 (0.992x); 1080p 0.474x | - | - | not tried; expected [I] (P0-d) |
| Pi 3A+ | 1080p30 24 MB/10 s; 720p 12 MB/10 s; 2 % | 480p 0.76 (1.02x); 720p 1.22 (0.996x); 1080p 3.0 (0.90x) | - | - | not tried [I] |
| Pi 4B | 1080p60 71 MB/10 s; 0.2 % | 720p 0.93; 1080p 2.0 | 720p 1.9 (0.98x); 1080p 3.5 (0.74x) | 720p 3.3 (0.665x) | **720p 0.44, 1080p 0.91, real time** [F] |
| RPI52 (Pi 5) | 720p 15 Mbit/s; 0.2 % | 720p 0.43; 1080p 0.8 | 720p 0.7; 1080p 1.9 | 720p 2.4 (0.93x) | none [F] |
| HackBerry (CM5) | not measured (P0-l) | as RPI52 [I] | as RPI52 [I] | - | none [I] |
| tablet (Core m5) | not measured | [U] | [U] | - | VAAPI probably [U] |

Also on RPI52: x265 ultrafast 1080p 3.7 cores (0.91x); SVT-AV1 preset 10 720p 1.2 cores (speed not recorded).

### 3.2 The VideoCore hardware path

- [F Pi 4B] `bcm2835-codec-encode` (`/dev/video11`): YU12, NV12, YUYV, RGB565, RGB24, RGBA in; H264 or MJPG out.
  `bcm2835-codec-decode` (`/dev/video10`): H264 or MJPG in; YU12, NV12, RGB565, RGBA out. CMA 512 MB (355 MB free).
- Paths with almost no CPU [I]:
  - YUYV 640x480 straight into the encoder;
  - MJPEG to the hardware decoder, then the encoder (FFmpeg's `mjpeg_v4l2m2m` and `h264_v4l2m2m`).
- [I] Zero 2 W and Pi 3A+: same codec block; CMA and firmware share on 512 MB boards [U]. P0-d answers.
- VHS reaches it through libavcodec only, and detects it by node name in `/sys/class/video4linux/*/name`: a capability,
  not a board table.

### 3.3 Containers, writer, crash safety

| Video | Container | Audio |
|---|---|---|
| H.264 (x264 or hardware) | **fragmented MP4** (`+frag_keyframe+empty_moov+default_base_moof`, 2 s fragments, GOP 1 s) | AAC-LC 48 kHz 128 kbit/s (FFmpeg's native `aac`) |
| MJPEG copy | **MKV** (FFmpeg wrote this pair on 2026-11-12: `clip.mkv`, V_MJPEG 1280x720 + A_AAC [F parsed]) | AAC (PCM as an option) |
| Conversion targets H.265, AV1 | MP4 (H.265), MKV or MP4 (AV1) | AAC copied |

- Muxing and encoding through libavformat and libavcodec, loaded at run time with Vinyl's table pattern
  (`decode_libs`: `real_ffmpeg()`, ABI offsets checked by a test against the headers).
- Cassette's writer model:
  - a capture ring of 2-4 s of frames (7-15 MB at 1080p MJPEG) against SD stalls (Cassette's P0 saw SD bursts cause
    overruns [F memory]);
  - a writer thread doing `avio_flush` and `fdatasync` every 5 s;
  - a journal per scene (`.journal/<scene>.json`, `open` / `closed`) and a repair at the next start: an fMP4 trimmed to
    its last complete fragment, an MKV remuxed when its last cluster is cut.
- Stops: under 500 MB free or 2 minutes at the current rate; camera unplug or switch; hide (D18). Each stop finishes the
  file and shows a notice.

### 3.4 Bitrate and disk per hour (decimal GB)

| Mode | Evidence | Bitrate | Per hour |
|---|---|---|---|
| MJPEG 1080p30 | zero8 37 MB/10 s; Pi 3A+ 24 MB/10 s [F] | 19-30 Mbit/s | **8.6-13.3 GB** |
| MJPEG 1080p60 | zero8 70, Pi 4B 71 MB/10 s [F] | 56-57 Mbit/s | **25 GB** |
| MJPEG 720p30 | Pi 3A+ 12 MB/10 s; RPI52 15 Mbit/s; `clip.mkv` 26.6 Mbit/s [F] | 9.6-27 Mbit/s | 4.3-12 GB |
| MJPEG 640x360 / 480p | not measured | 3-6 Mbit/s [I] | 1.4-2.7 GB |
| H.264 hardware 720p / 1080p | the Pi 4B run's set rates 4 / 8 Mbit/s [F] | 4 / 8 | 1.8 / 3.6 GB |
| x264 CRF 26, 720p | sizes not kept | 1.5-4 [I] | 0.7-1.8 GB |
| x264 CRF 28, 480p | not kept | 0.8-2 [I] | 0.36-0.9 GB |
| x265 CRF 28 / AV1 CRF 35, 1080p | not kept | 1-3 [I] | 0.45-1.4 GB |
| AAC 128k / PCM 48k stereo | arithmetic | 0.128 / 1.536 | 0.06 / 0.69 GB |

MJPEG size follows the scene (the same 720p mode gave 9.6, 15 and 26.6 Mbit/s). VHS shows the rate it really writes and
the time left. P0-k records the H.264 sizes.

### 3.5 SD speed and wear

- 1080p60 MJPEG writes 7 MB/s, near the 10 MB/s floor of a U1 card; 1080p30 (2.4-3.7 MB/s) and all H.264 modes are
  comfortable. B14 runs 30 minutes.
- [I] A consumer 64 GB card is good for very roughly 20-60 TB of writes: an hour a day of 1080p MJPEG (5 TB a year) is
  fine; continuous MJPEG (35-105 TB a year) is not. Converting scenes and H.264 defaults keep writes low.

### 3.6 Sound input (Cassette's chooser)

- **The list [F Cassette]:**
  - ALSA capture cards (the camera's `CM969`, USB headsets and cards, a board's own input), found by name or `by-id`,
    never by index, `hw:` first, the server's `default` when the card is busy;
  - Bluetooth headsets as `bt:<MAC>` where a server runs (`board.state` `bt_audio=pipewire|pulseaudio`), with paired but
    off devices dimmed from `status.state` `bt_audio_dev`;
  - Cassette stores the choice as `name|usb path|vid:pid` or `bt:<MAC>` (`docs/SETTINGS.md` `card`).
- **The guards [F Cassette `pulse_api.hpp`]:** every stream `PA_STREAM_DONT_MOVE` with `node.dont-reconnect` /
  `node.dont-fallback`; the identity guard ends a stream moved to another device; the silence detector reports 3 s of
  exactly-zero samples (`SilenceDetected`), shown as a notice ("no sound from <input>").
- **VHS choices:** "Camera microphone" first when present (the default), then the others, then "No sound" (video only).
  P1 records from the camera mic only; the chooser comes in P2.
- **Bluetooth mics** record at 16 kHz (mSBC) or 8 kHz (CVSD) [F Cassette `bt_input.hpp`]; AAC at that rate, mono.
  Opening the mic switches the headset to its call profile (about 0.65 s, back to music about 1.9 s [F memory]).
- **The CM969 level:** the kernel's warning means its dB scale cannot be trusted. Cassette uses a driver dB range when
  reported and infers 0-23.81 dB otherwise; either may be wrong here [I]. VHS leaves the level as found (Cassette's
  revised D18), shows it as a percentage with no dB, and shows Cassette's LED meter while recording. P0-b reads
  `amixer -c CM969 contents`.
- **A/V sync, audio master:**
  - video frames carry monotonic timestamps; audio time is sample count / rate;
  - the camera's audio clock drifts from the board's, typically 20-100 ppm, 0.07-0.36 s per hour [I];
  - VHS keeps (monotonic, sample position) pairs from `snd_pcm_status` (or the pulse stream's timing) every few
    seconds, fits a line, and maps each video timestamp onto the audio timeline;
  - dropped frames leave a gap, never a slide; audio overruns become silence of the lost length (Cassette counts them);
  - Bluetooth capture latency (100-200 ms [I]) is in the server's reported latency; B13 claps to check.
  P0-f measures the drift.

### 3.7 Orientation switch, unplug, power

- **Switch [F]:** the device re-registers with rotated modes. VHS treats it as an unplug:
  1. `DQBUF` gives `ENODEV`;
  2. the scene is finished, with "Camera turned: recording stopped";
  3. inotify on `/dev` sees the new node;
  4. VHS reopens by USB identity in the new orientation.
  [U] Whether the product id or the `by-id` name changes between positions (P0-b): matching accepts both.
- **Unplug:** the same path; Cassette's unplug test (nothing lost up to the cut [F memory]) is the model.
- **Power:**
  - [F memory] On the Pi 3A+ the camera caused active under-voltage, and once failed to enumerate (`can't read
    configurations, error -22`) until the owner fixed the supply;
  - [F probe] the Pi 4B shows `throttled=0x50005` at idle without the camera;
  - [I, Raspberry Pi documentation, not re-read] a Pi 5 on a 3 A supply limits all USB to 600 mA; a Pi 4 about 1.2 A;
  - VHS reads the Pi's under-voltage alarm when it exists (the `rpi_volt` hwmon's `in0_lcrit_alarm` [I]; optional, not
    on amd64) and shows "Power too weak for the camera: use the board's own supply (2.5 A Zero 2 W and 3A+, 3 A Pi 4,
    5 A Pi 5) or a powered USB hub". A launcher fact (L3) would serve every app.

---

## 4. The codec picker (expected performance)

### 4.1 Badge rules (proposal, D9)

| Badge | Rule | In the picker |
|---|---|---|
| **OK** | measured live at 1.0x or faster, using at most half the board's CPU (2 of 4 cores), leaving room for the live view, audio and the UI | selectable for recording |
| **Borderline** | measured at 0.98-1.0x, or above half the CPU, or not measured but inferred close | selectable, with a line "may drop frames on this board" |
| **Too slow for live** | measured under 0.98x, or inferred far below | not offered for recording; offered as a conversion target with its time estimate (section 5) |
| **Unknown** | no measurement on this board class | not offered until a probe (HackBerry, tablet: P0-l) |

Each row also shows the CPU (cores) and the disk per hour. VHS checks itself during recording: when the encoder's input
queue keeps growing, it warns and offers to finish the scene and continue in MJPEG copy. The badge table ships as data
(per SoC class, from `/proc/device-tree/model` or the CPU part), so no board name is hard-coded in logic.

### 4.2 Per board (measured numbers; GB per hour from 3.4)

| Codec, preset | Zero 2 W (deck, zero8) | Pi 3A+ | Pi 4B | Pi 5 (RPI52, RPI53) | HackBerry, tablet |
|---|---|---|---|---|---|
| MJPEG copy 720p30 (4.3-12 GB/h) | **OK**, about 0 CPU; **default** | **OK** | OK | OK | Unknown (P0-l) |
| MJPEG copy 1080p30 (8.6-13.3 GB/h) | OK (3 %); the live view decodes only every 3rd frame | OK (2 %) | OK | OK | Unknown |
| MJPEG copy 1080p60 (25 GB/h) | Borderline (SD near its floor) | Borderline | OK (0.2 %) | OK | Unknown |
| H.264 x264 ultrafast 480p (0.4-0.9 GB/h) | **OK** (0.95 core, 1x) | **OK** (0.76, 1.02x); **default** | OK | OK | Unknown |
| H.264 x264 ultrafast 720p (0.7-1.8 GB/h) | Too slow (3.0 cores, 0.992x) | Borderline (1.22, 0.996x) | OK (0.93) | OK (0.43) | Unknown |
| H.264 x264 veryfast 720p | Too slow | Too slow [I] | Borderline (1.9, 0.98x) | OK (0.7) | Unknown |
| H.264 x264 ultrafast 1080p | Too slow (0.474x) | Too slow (0.90x) | Borderline (2.0 cores) | OK (0.8) | Unknown |
| H.264 x264 veryfast 1080p | Too slow | Too slow | Too slow (0.74x) | **OK (1.9 cores); default** | Unknown |
| H.264 x264 medium 720p | Too slow | Too slow | Too slow (0.665x) | Too slow (0.93x) | Unknown |
| **H.264 hardware 720p, 4 Mbit/s** (1.8 GB/h) | Unknown: **P0-d**; OK expected [I] | Unknown: P0-d | **OK (0.44 core); default** | none | none (HackBerry); VAAPI [U] |
| H.264 hardware 1080p, 8 Mbit/s (3.6 GB/h) | Unknown: P0-d | Unknown | OK (0.91 core) | none | as above |
| H.265 x265 ultrafast 1080p | Too slow | Too slow | Too slow [I] | Too slow (0.91x, 3.7 cores) | Unknown |
| AV1 SVT preset 10, 720p | Too slow [I] | Too slow [I] | Too slow [I] | Borderline (1.2 cores, speed [U]) | Unknown |

Defaults per board (D9): Zero 2 W MJPEG 720p30 (hardware H.264 720p if P0-d passes); Pi 3A+ x264 480p (or hardware);
Pi 4B hardware H.264 720p; Pi 5 x264 veryfast 1080p; HackBerry and tablet after P0-l. [U] Sustained thermal behaviour
of 1.9 cores on a Pi 5 for 30 minutes (P0-j).

H.265 and AV1 (D10): conversion targets on the Pi 4/5 class only (smaller archives), hidden on the Zero class and the
Pi 3A+ (encoding and playing them there are both too slow [I]).

---

## 5. Post-recording conversion (the convert queue, P3)

### 5.1 Why

On the Zero boards the only live codecs are MJPEG copy (4-13 GB per hour) and x264 at 480p. Recording MJPEG at 720p or
1080p and converting afterwards gives H.264 at a tenth of the size and a better picture than live 480p. On the Pi 4/5
class the queue also makes H.265 or AV1 archives. If P0-d finds hardware H.264 on the Zero 2 W, the queue becomes a
convenience there, not a necessity.

### 5.2 Time per minute of video

Method [I]: the live runs give c, the CPU seconds per video second at the same resolution, MJPEG decode included (zero8
1080p from 0.474x on about 3.5-4 busy cores). A conversion is CPU-bound and scales with the cores it uses. Minutes of
work per minute of video = c / cores. "Background" = 2 cores at nice 10; "Fast" = 4 cores.

| Board | Conversion (source MJPEG at the same resolution unless said) | c | Background | Fast |
|---|---|---|---|---|
| Zero 2 W | to x264 ultrafast 480p (from 640x480 MJPEG) | 0.95 | 0.5 | 0.25 |
| Zero 2 W | 720p MJPEG to x264 ultrafast 480p (decode 720p + scale + encode) | about 1.9 [I] | 1.0 | 0.5 |
| Zero 2 W | to x264 ultrafast 720p | 3.0 | 1.5 | 0.75 |
| Zero 2 W | 1080p MJPEG to x264 ultrafast 720p | about 4.5 [I] | 2.3 | 1.1 |
| Zero 2 W | to x264 ultrafast 1080p | about 7-8 [I] | 3.5-4 | 1.8-2 |
| Zero 2 W | hardware H.264 (if P0-d) | about 0.3-1 [I] | 0.15-0.5 | - |
| Pi 3A+ | to x264 ultrafast 480p / 720p / 1080p | 0.76 / 1.22 / about 3.3 | 0.4 / 0.6 / 1.7 | 0.2 / 0.3 / 0.8 |
| Pi 4B | hardware H.264 720p / 1080p | 0.44 / 0.91 | 0.2 / 0.5 | - |
| Pi 4B | x264 veryfast 720p / 1080p | 1.9 / about 4.7 | 1.0 / 2.4 | 0.5 / 1.2 |
| Pi 5 | x264 veryfast 720p / 1080p | 0.7 / 1.9 | 0.35 / 0.95 | 0.2 / 0.5 |
| Pi 5 | x264 medium 720p | about 2.6 | 1.3 | 0.65 |
| Pi 5 | x265 ultrafast 1080p | about 4.1 | 2.0 | 1.0 |
| Pi 5 | AV1 SVT preset 10, 720p | 1.2 | 0.6 | 0.3 |

Examples [I]: a 10-minute 1080p scene on zero8 takes about 40 minutes in the background to become 1080p H.264, or about
23 minutes as 720p. On a Pi 5 the same scene takes about 10 minutes (veryfast 1080p). P0-k and P0-m confirm the numbers
on files.

### 5.3 How the queue behaves (D13)

| Topic | Rule |
|---|---|
| Where it runs | inside VHS: a worker thread with libavcodec. It runs while VHS is in front, and while hidden on multitasking boards (`X-Background=run`, the contract's "work that must go on"). Leaving VHS pauses the queue; it resumes at the next start. No daemon (059 D7) |
| Priority | worker threads at nice 10 (`setpriority` per thread), I/O class idle (`ioprio_set`, allowed for non-root [I]); 2 threads by default, "Fast" (all cores) as a per-job choice; the live view and recording always win: starting a recording pauses the queue |
| Pause and resume | per job, at a frame boundary (the worker stops feeding the encoder) |
| Thermal | pause above 75 C, resume under 65 C (Vinyl's library scan pauses above 75 C, 060 D27 [F]); temperature from `/sys/class/thermal` |
| Power | on boards with a battery (`status.state` `battery`), an option "only while charging" (`battery_state=Charging`); default off |
| Disk | before a job: the output estimate (target rate x duration x 1.1) plus a 500 MB floor must be free, else the job waits with a notice |
| Output | written to `.<scene>.part.mp4` beside the source; renamed when complete and synced |
| Keep or replace | **Keep both by default.** "Replace original" deletes the source only after the result is complete, synced and verified (duration within 0.5 s, first and last frames decode), with one confirmation: Cassette's replace-original rule |
| Crash or reboot | the queue is a JSON file (`~/.config/vhs/queue.json`, atomic writes); at start a `.part` file is deleted and its job restarts from zero (resuming from the last fMP4 fragment is a later refinement) |
| Progress | frames done / total, the elapsed time and an ETA from the measured rate; the scene row shows "Converting 42 %" |
| A conversion that would take hours | above 60 minutes of estimated work the picker shows the time and suggests a lower resolution, or "Fast" on mains; above 4 hours it says so plainly and shows the PC alternative (the README's ffmpeg line, D14) |

---

## 6. Scenes (the recordings list)

| Field | Source |
|---|---|
| Thumbnail | MJPEG: the first frame at 1/8 (TurboJPEG, 240x135 from 1080p); H.264: the first keyframe through FFmpeg; photos: the JPEG at 1/8. Cached in `~/.cache/vhs/`, deleted with the scene |
| Duration, size, codec, resolution, date | from the container (libavformat) and the file; the date from the name or the file time (`nodate` until the clock is set) |
| State marks | locked; recording; converting (progress); repaired; converted copy |

- **Files (D7):** one folder `~/VHS/` (a Folder setting like Cassette's `~/Recordings`), mode 0700. Names follow
  Cassette's D15 pattern (`takes/naming.cpp`): `scene_<number>_<YYYYMMDD-HHMMSS>.mkv|.mp4`,
  `photo_<number>_<...>.jpg`; numbers never repeat (a counter file); `nodate` until the clock is synchronised. A
  converted copy keeps the name with a suffix (`scene_007_...-h264.mp4`).
- **Actions:**
  - play (Enter or a tap: windowed playback);
  - convert (opens the codec picker in conversion mode with the time estimate);
  - rename (Cassette's prefix rules: 24 characters, letters, digits, space, `-`, `_`);
  - delete (asks first, `Dialog`);
  - lock (a sidecar list; delete-all and auto-cleanup skip locked scenes);
  - copy to a mounted USB drive (a picker of mounted drives; VHS never mounts one: automount is a generic launcher item,
    060 D28);
  - photos: "Send to Notes" (copies the JPEG into `<vault>/attachments/` and appends a link to a dated note; [F] Notes
    0.2.1 shows images as links).
- **Order:** newest first; a filter Movies / Photos / All [I: a question for the coordinator].

---

## 7. Photo

- **No recompression:** the file is the camera's JPEG frame, plus an APP1 Exif segment, plus the standard Huffman tables
  (JPEG Annex K) when the frame has no DHT segment (many UVC cameras omit them; P0-a checks this one [U]).
- **[F] The study's photo sizes were FFmpeg re-encodes:** every test photo carries `COM Lavc61.19.101` or `Lavc61.3.100`,
  with different DQT tables per board. The camera's own 1080p frames are 60-125 KB (copy bitrate / fps).
- **Instant against switched (D6):** where the live view streams 1080p (Pi 4/5, CM5, tablet) a photo is the next good
  frame. On the Zero class and Pi 3A+ the live view streams 640x360, so a full photo is a mode switch (stop, `S_FMT`
  1920x1080, start, drop 15 frames, keep one, switch back): about 1-1.5 s [I] with a "Taking photo" state. A "Quick
  photo" setting takes the preview's frame instantly.
- **During a recording** the photo is the current recording frame (instant; at the recording's resolution).
- **Burst:** N consecutive frames (10 in 0.33 s at 30 fps), suffix `_b01`...
- **EXIF:** an own writer (about 300 lines; libexif is LGPL and 2.9 MB installed [F apt-cache]): Make and Model from the
  USB strings, Orientation, Software, DateTime and DateTimeOriginal (only with a synchronised clock), pixel dimensions;
  GPS later (section 14).

---

## 8. Playback

### 8.1 Two forms

| | Windowed | Full screen |
|---|---|---|
| Where | a page with the toolkit's header (title, Esc, status strip); the picture in the body | the whole canvas; no header band, no status strip |
| Controls | laid out below or beside the picture, or overlaid on tap (a coordinator question, 12) | **overlaid on tap**: play/pause, the seek bar (tap and drag, Vinyl's `SeekScrub`), elapsed and total time, the volume group (Vinyl's "-" / level / "+"), the output name, Close (back to windowed), the windowed/full toggle |
| Enter and leave | Enter or a tap on a scene; Esc goes back to the scenes | F key, the toggle button, or a double tap on the picture; **Esc or Close always returns to windowed**; the 3 s Esc hold ends the app |
| Overlay timing | - | shown on any tap or key, hidden after 3 s without input while playing, always shown while paused |

### 8.2 Full screen under the contract and the design checklist

**What the contract allows [F APP-CONTRACT]:**
- `X-Fullscreen=true`: "the launcher stops drawing, the app owns the whole panel until it exits (the hold-Esc watchdog
  still applies)" (section 6). The launcher draws nothing over a fullscreen app: no toast, no media OSD, no keyboard band
  (sections 10, 14).
- The header band and the status strip are drawn **by the app** (`cp0::ui::Page`, `cp0::ui::StatusStrip`, design QA
  D-02), so a page without them is technically just an LVGL screen without a `Page`.
- Esc: the launcher reads keyboards passively; only Esc held 3 s ends the app (SIGTERM). A short Esc reaches VHS.
- Gestures: three fingers held 1 s (G1) still open the switcher on multitasking boards; G2 (a long press on the strip)
  does not exist without a strip, which is acceptable since G1, Super and Alt+Tab remain (section 13).
- Volume keys: the launcher acts on them and draws nothing; the keys still reach the app, so the overlay shows the level
  from `status.state` for 2 s (D-70, D-71). Headset and keyboard play keys: Vinyl's media-key reader.
- Dimming: "today the panel never dims while an app is in front" unless the app sends `dim allow` (battery boards):
  VHS sends `dim deny` while playing.
- Rotation: the backend rotates per board, transparently. On a portrait canvas a landscape video is letterboxed
  (480x270 on 480x800). Filling it needs VHS to rotate the picture itself (an option "turn to fill"; the user turns the
  device). The launcher's "turned app" mode is chosen at launch, not per page, so it cannot serve playback alone.
- The switcher card shows the last frame: fine.

**What the design checklist says [F app-design.md]:** a full-screen page fails **D-02** (every screen's header is a
`Page` with title and strip), **D-03** (the band holds only title, Esc, strip), **D-07** (header controls left of the
strip), **D-08** (the toolkit's Esc unchanged), **D-10** (the title), **D-12** (the strip). D-28 (keys and touch), D-30
(Esc goes back), D-34 (no drag-only) and D-35 (no tap-only or hold-only action) still apply and can be met.

**Proposed owner ruling R-7 (D15):** "Full-screen video playback is exempt from the header-band rules (D-02, D-03,
D-07, D-08, D-10, D-12). Conditions:
- it is entered only by a deliberate action (a key, a button, a double tap);
- Esc and the overlay's Close always leave full screen (back to the windowed page); the 3 s Esc hold still ends the app;
- every overlay control has a key (Space play/pause, Left/Right seek, Up/Down or +/- volume through the launcher, F
  toggle, Esc leave), and the seek bar works by tap as well as drag;
- the overlay appears on any tap or key and shows volume changes from `status.state`;
- nothing else is drawn when the overlay is hidden."
Whether the live capture view may also go full screen is a separate question (12).

**Launcher requests for full screen:** none. The app already owns the panel; only the checklist needs the ruling.

### 8.3 Decode

- **FFmpeg** (`libavformat`, `libavcodec`, `libswresample`) for demux and decode: a hard Depends from P1 (decisions.md
  2026-11-13: Settings > Apps installs no recommends [F]; about 104 MB on a Lite board [F]).
- **MJPEG scenes:** TurboJPEG at the screen's scale (cheaper than FFmpeg's `mjpeg`, and the same code as the live view).
- **Hardware decode:**
  - H.264 on the VideoCore boards: `h264_v4l2m2m` decoder (`bcm2835-codec-decode` [F Pi 4B], Zero 2 W and 3A+ [I]);
  - HEVC: `rpi-hevc-dec` exists on the Pi 4B and RPI52 [F nodes]; the Raspberry Pi FFmpeg build uses it through
    `-hwaccel drm` [I]; P0-m checks;
  - Pi 5 / CM5: no H.264 hardware decode, software is fast enough [I];
  - tablet: VAAPI [U].
- **Expected decode cost at 30 fps (cores) [I; P0-m measures]:**

| Format | Zero 2 W | Pi 3A+ | Pi 4B | Pi 5 / CM5 |
|---|---|---|---|---|
| MJPEG 720p (at 1/2 scale) | 0.6-0.9 | 0.45-0.65 | 0.25 or hardware | 0.1 |
| MJPEG 1080p (at 1/4 scale) | 1.0-1.5: drops frames | 0.7-1.0 | 0.5 or hardware | 0.2 |
| H.264 480p, software | 0.4-0.7 | 0.3-0.5 | 0.15-0.25 | under 0.1 |
| H.264 720p, software | 1.2-2.0: borderline | 0.9-1.4 | 0.4-0.7 | 0.2-0.3 |
| H.264 1080p, software | 2.7-4.5: no | 2-3: no | 1-1.5 | 0.4-0.7 |
| H.264 hardware | about 0.1 if present [I] | as Zero | about 0.1 [I] | none |
| H.265 | too slow | too slow | hardware [I] | hardware [I] |
| AV1 (dav1d) | 480p at most [I] | 480p | 720p | 1080p |

  The display path (render, flush, rotation) comes on top.
- **Frame dropping on slow boards:** audio is the master clock; a video frame later than one frame period is dropped
  before decoding where possible (MJPEG: any frame; H.264: `AVDISCARD_NONREF` frames first, then decode-but-not-show).
  After 2 s of drops VHS shows "This board drops frames at this size" with a hint to convert to 480p.
- **A/V sync:** the picture shows when its timestamp is at or before the audio position heard (Vinyl's engine reports
  frames written minus the output's delay [F `engine.hpp`]).
- **Seeking:** MJPEG is all-intra, so any frame is exact and instant. H.264 (GOP 1 s in VHS's own files) seeks to the
  previous keyframe while scrubbing, then decodes forward to the exact frame on release (up to 30 frames: under 1 s even
  on a Zero 2 W at 720p [I]).
- **Scrubbing:** Vinyl's `SeekScrub` (tap or drag, one seek on release [F]), plus a preview picture while dragging (the
  nearest keyframe, or for MJPEG the exact frame at 1/8 scale), at most 5 updates per second, no sound.

### 8.4 Audio output (Vinyl's stack)

Copied from Vinyl [F headers]:
- `engine/outputs`: Automatic (the server's default sink, or ALSA `default`), Bluetooth `bt:<MAC>` with DONT_MOVE,
  wired sinks or `plughw:` cards; pause when the output disappears, never switch by itself;
- `volume/output_volume`: per-output dB windows, 1-2 dB steps, mute, software gain only without a mixer;
- `input/media_keys`: headset and keyboard play/pause, read-only, never grabbed;
- the playback engine's "pause by drop" and output-loss handling.
RPI52 has no wired output (`sound=0` [F]): playback there is silent unless a USB or Bluetooth output exists; VHS says so.

---

## 9. Launcher integration and requests

### 9.1 What VHS uses [F contract]

| Capability | Use |
|---|---|
| `X-Fullscreen=true`, `X-Portrait=320x480` | every class, the portrait layouts (D27) |
| `X-Background=run` | answer `hide` within 300 ms; after the ack: finish a recording, release the camera, pause playback, **keep converting**; reopen and warm up on `show`. Not `freeze`: a frozen app cannot finish its file, and **an app without `X-Background` is SIGSTOPped behind the "Close app?" sheet by the three-finger gesture**, mid-recording [F contract 13] |
| `board.state` `ram_mb`, `requests`, `sound`, `bt_audio` | buffer counts, beeps, playback output, Bluetooth inputs |
| `status.state` `volume`, `mute`, `bt_audio_dev`, `battery_state` | the level in playback, Bluetooth rows, conversion on charge |
| `launcher.sock` `beep`, `dim`, `volume` | shutter beep where `beep` is listed (off the LVGL thread, D-65); `dim deny` while playing or recording; the volume group sends `volume up/down` (a media app may, D-71) |
| Multitasking boundary | none below a 480-pixel short side: zero8 owns the panel; Esc at the root quits (D-30) |

### 9.2 Generic launcher requests proposed

| # | Request | Rationale | Size | When |
|---|---|---|---|---|
| **L1** | a camera and microphone in-use glyph in the status strip, from capture nodes held open by any of the user's processes (`/proc/<pid>/fd` to `/dev/video*` capture nodes and `/dev/snd/pcmC*D*c`, sampled every 2 s like the SSH indicator); `status.state` `camera=`, `mic=` | the band is the launcher's (D-03), so only it can show this; trustworthy for every app and for terminal tools; [F] the launcher runs as the user (`cyberdeckzero` process of `osrde` on RPI52), so it can read those fds | M (a slot policy question: share the SSH slot or a new one) | with VHS 1.0 |
| **L2** | `launcher.sock screen off\|on` for the app in front (059's L2) | time-lapse, long conversions, later monitor modes; Vinyl too | S-M | with P3 |
| **L3** | a power warning fact (`status.state` `power_low=1` from the Pi's under-voltage alarm) and a strip glyph | the Pi 3A+ camera case; the Pi 4B today; every USB-heavy app | S | any time |
| **L4** | a location service writing `position.state` for every app (or Deck Atlas with `X-Background=run`) | geotag (section 14), survey points | M-L | later |
| **L5** | a Wi-Fi offer from an app (a 0600 file plus `launcher.sock wifi offer`), confirmed on the launcher's own Wi-Fi page | QR Wi-Fi codes (later) | M | later |
| **L6** | `launcher.sock inhibit on\|off`: no idle suspend while recording or converting | the tablet, if it suspends on idle [U] | S | only if needed |

Not needed: a "camera attached" fact (VHS enumerates V4L2 itself and sees hot-plug), a Settings page for cameras (camera
settings are the app's; the launcher stays generic, decisions.md 2026-10-04).

---

## 10. Privacy and safety

| Item | Rule |
|---|---|
| In-app indicator | a red dot and REC / LIVE / TIME-LAPSE in the body (`Page::set_caption`, D-04), never in the band (D-03); a word and a shape, not colour alone (D-50) |
| System indicator | L1 |
| Permissions (`app.json`) | `"camera": true, "microphone": true, "additional_hardware": true` from P1 (the keys exist [F APP-JSON]); `"network"` only if a later network feature ships |
| Storage | `~/VHS/` mode 0700; thumbnails in `~/.cache/vhs/`, deleted with their scene |
| Delete and lock | delete asks first; locked scenes survive delete-all and cleanup |
| Network | no uploads, ever; the README's Privacy section says so, as Cassette's and Deck Atlas's do |
| Hidden app | the camera is released on hide in 1.0 |
| Location | none in 1.0; geotag later and off by default |
| Test material | synthetic frames and tones only (Cassette's privacy line), never real recordings of people |

---

## 11. Boards matrix

| Board | CPU, RAM, screen | Live view | Photo | Record default (live) | Playback | Convert | Notes |
|---|---|---|---|---|---|---|---|
| **deck** (Zero 2 W) | A53 1 GHz x4, 512 MB; 640x480, 11.3 px/mm; multitasking; no sound server | MJPEG 640x360 (0.28-0.33 core [I]) | 1080p switch (1-1.5 s) or Quick | MJPEG 720p30 + AAC; H.264 480p x264; hardware if P0-d | MJPEG 720p, H.264 480p smooth; 1080p drops [I] | 720p 1.5, 1080p 4 min per min [I] | unreachable today; one dwc2 USB port and a hub |
| **zero8** (Zero 2 W) | same, 415 MB; 480x320 (6.3 px/mm), PDA 320x480; no multitasking | as deck, plus PDA rotation | as deck | as deck | as deck | as deck; only while VHS is in front | unreachable today |
| **Pi 3A+** | A53 1.4 GHz x4, 512 MB; 480x800 portrait DSI (PDA) | MJPEG 640x360 | 1080p switch | x264 480p (0.76 core); MJPEG 720p/1080p; hardware if P0-d | H.264 720p borderline [I] | 720p 0.6, 1080p 1.7 | **needs a good 2.5 A supply** [F]; unreachable today |
| **Pi 4B** | A72 1.8 GHz x4, 905 MB; 800x480 per notes ([F] its `board.state` names `pi3a-wsdsi43-portrait` today: which panel is attached is [U]) | MJPEG 1080p at 1/2 (hardware decode later) | instant 1080p | **hardware H.264 720p** (0.44 core); 1080p 0.91 | hardware H.264 decode [I] | hardware 0.2-0.5 | [F] `throttled=0x50005` at idle today; `libturbojpeg0` absent |
| **RPI52** (Pi 5) | A76 2.4 GHz x4, 4 GB; 800x480 DSI; PipeWire; `sound=0` | MJPEG 1080p at 1/2 (about 0.4 core [I]) | instant 1080p | **x264 veryfast 1080p** (1.9 cores) | software, easy; HEVC hardware [I] | 0.35-0.95; H.265/AV1 targets | no hardware H.264 [F]; desktop image has FFmpeg, x264, TurboJPEG, zbar, ZXing, libexif [F] |
| **RPI53** (Pi 5, HyperPixel) | as RPI52; 480x800 portrait | as RPI52 plus rotation | instant | as RPI52 | as RPI52 | as RPI52 | unreachable today |
| **HackBerry** (CM5) | A76, about 2 GB [I]; 720x720; PipeWire | MJPEG 1080p at 3/8 | instant | Unknown until P0-l (as RPI52 [I]) | as RPI52 [I] | as RPI52 [I] | cooling and USB current [U]; unreachable today |
| **tablet** (amd64) | Core m5-6Y57, 3.8 GB; 1280x800 (PDA 800x1280); PulseAudio; Ubuntu | MJPEG 1080p at 5/8 | instant | Unknown until P0-l (VAAPI [U]) | [U] | [U] | built-in cameras [U] (unsupported, D3) |

### 11.1 Probes (P0; exact commands; the camera plugged into the named board)

| # | Board | Command | Needs |
|---|---|---|---|
| P0-a | zero8 or Pi 4B | `v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=MJPG --stream-mmap=4 --stream-skip=15 --stream-count=1 --stream-to=$HOME/f1080.mjpg` (also 1280x720, 640x360), then `python3 -c "d=open('f1080.mjpg','rb').read(); print(len(d), b'\xff\xc4' in d[:2000])"` | `v4l-utils` (on the Pi 4B and RPI52 [F]); files removed after |
| P0-b | any, **both switch positions** | `v4l2-ctl -d /dev/video0 --list-formats-ext -L`; `ls -l /dev/v4l/by-id/`; the a108 device's `idProduct` and `bMaxPower` in `/sys/bus/usb/devices/*/`; `cat /proc/asound/CM969/stream0`; `amixer -c CM969 contents`; `journalctl -k -b \| grep -i -E 'uvc\|volume range\|a108'` | read-only |
| P0-c | zero8, Pi 3A+ | test tool `camdec` (WSL build, copied to `/tmp`, removed after): TurboJPEG decode of 300 saved frames per mode at 8/8, 4/8, 2/8, 1/8 and Y-only, and stb_image; ms per frame, peak RSS | owner's word; `libturbojpeg0` on zero8 if absent |
| P0-d | zero8, Pi 3A+ | `cat /sys/class/video4linux/*/name`; `v4l2-ctl -d /dev/video11 --list-formats-out`; `grep Cma /proc/meminfo`; static ffmpeg: `ffmpeg -benchmark -f v4l2 -input_format yuyv422 -video_size 640x480 -framerate 30 -t 10 -i /dev/video0 -c:v h264_v4l2m2m -b:v 2M a.mp4` and `ffmpeg -benchmark -f v4l2 -input_format mjpeg -video_size 1280x720 -framerate 30 -c:v mjpeg_v4l2m2m -t 10 -i /dev/video0 -c:v h264_v4l2m2m -b:v 4M b.mp4` | owner's word (static ffmpeg as on 2026-11-13, deleted after) |
| P0-e | zero8 | `v4l2-ctl -d /dev/video0 --set-fmt-video=width=640,height=360,pixelformat=YUYV --stream-mmap=4 --stream-count=900` with the keyboard and hub attached (fps, drops) | nothing written |
| P0-f | Pi 4B or RPI52 | `t0=$(date +%s.%N); arecord -D hw:CARD=CM969 -f S16_LE -r 48000 -c 2 -d 600 $HOME/drift.wav; t1=$(date +%s.%N)`; samples against `t1 - t0` | 115 MB file, removed after |
| P0-h | Pi 3A+, RPI52 | camera streaming 1080p MJPEG 10 minutes (`--stream-count=18000`) while `vcgencmd get_throttled` runs every 5 s | the usual supply |
| P0-j | RPI52 | x264 veryfast 1080p for 30 minutes with the static ffmpeg; temperature and throttling every 30 s | owner's word |
| P0-k | Pi 4B, RPI52, zero8 | the existing bench scripts with `-t 60`, keeping `stat -c %s` of each output (H.264 sizes per preset) | owner's word |
| P0-l | HackBerry, tablet | `webcam-bench.sh` as on RPI52 (tablet: the distribution's `ffmpeg`, plus `-c:v h264_vaapi -vaapi_device /dev/dri/renderD128` [U]); `cat /sys/class/video4linux/*/name`; tablet: `ls /dev/video*` for built-in cameras | owner's word |
| P0-m | every board with P0-k's files | decode speed: `ffmpeg -hide_banner -benchmark -i <clip> -f null -` for H.264 480p/720p/1080p and MJPEG 720p/1080p; hardware: `ffmpeg -benchmark -c:v h264_v4l2m2m -i clip.mp4 -f null -` (VideoCore boards); HEVC: `ffmpeg -benchmark -hwaccel drm -i clip_hevc.mp4 -f null -` (Pi 4B, RPI52, with the Pi's own ffmpeg) | owner's word; clips removed after |

---

## 12. UI questions for the coordinator (no screens designed here)

1. **Pages:** Capture (live view with Photo / Video modes, later Time-lapse), Scenes, Photo review, Playback windowed,
   Playback full screen (overlay), Convert queue, Codec picker (record and convert modes), Input chooser, Settings
   (folder, prefix, Quick photo, resolution per mode, output for playback), Camera controls, notices and dialogs. Which
   are pages, which are sheets?
2. **Room for a 16:9 picture** (header 40 px at 480x320 and 320x480, 56 px elsewhere):

   | Canvas | Body | Picture at full width | Left over |
   |---|---|---|---|
   | 320x480 (no title) | 320x440 | 320x180 | 260 px below (a portrait camera: 248x440) |
   | 480x320 | 480x280 | 480x270 | 10 px: overlay controls, or a smaller picture (373x210 with a 107 px side bar) |
   | 480x480 / 720x720 | 480x440 / 720x664 | 480x270 / 720x405 | 170 / 259 px below |
   | 640x480 | 640x424 | 640x360 | 64 px band |
   | 800x480 | 800x424 | 754x424 | 46 px at the sides, or overlay |
   | 480x800 / 480x640 | 480x744 / 480x584 | 480x270 | 474 / 314 px below |

3. **The camera's switch against the screen's orientation:** always show what the camera sends (letterboxed), or offer
   "turn to fill"? What does the capture page show during the 1-2 s re-enumeration?
4. **Capture controls:** overlay keys on the picture (shutter, mode, scenes) or a band? Auto-hide? Is a full-screen
   capture view wanted too (it would need R-7 widened)?
5. **Windowed playback:** controls laid out, or the same tap overlay as full screen?
6. **The overlay in full screen:** which controls (8.1), their positions per class, the volume group from Vinyl, the
   status strip shown inside the overlay or not, the hide delay.
7. **Scenes list:** `CompactList` rows with a small thumbnail (an own list with the `cp0_ui_list.h` helpers, D-48) or a
   thumbnail grid (HA's tile pattern, D-49)? Row affordance (D-45): a scene opens playback (chevron or dot?); convert,
   rename, delete, lock and copy in the 3 s hold box (Ctrl+O, D-35). Movies / Photos filter?
8. **Codec picker:** one row per codec/preset with the badge (OK / borderline / too slow), CPU and GB per hour; "too
   slow" rows hidden in record mode or shown dimmed with the reason? The recommended default marked how?
9. **Convert queue page:** rows with progress, ETA, pause/resume, Fast/Background, Keep/Replace; the thermal and
   charging pauses shown how?
10. **Input chooser:** Cassette's rows (camera mic first, Bluetooth with "call quality", dimmed paired-off devices,
    "No sound") and Cassette's meter while choosing?
11. **Recording screen:** timer, size and time left, REC marker in the body, audio meter, a dropped-frames warning; what
    stays at 480x320?
12. **Proposed key map** (letters by physical key code, D-32; R shares its evdev code with LVGL's "right", decisions
    2026-10-05):

    | Key | Capture | Playback (both forms) |
    |---|---|---|
    | Space | photo, or start/stop in Video mode | play / pause |
    | Enter | as Space on the live view; open in lists | show the overlay |
    | Tab | next mode (Photo, Video) | - |
    | R | record start/stop in any mode | - |
    | Left / Right | - (D-29 keeps back/open) | seek 5 s (Shift 30 s): a media-page exception as Vinyl's (D-29) |
    | Up / Down, + / - | zoom (+/-), Up/Down free | volume through the launcher's keys or `volume up/down` |
    | F | - | full screen toggle |
    | S / I / C | Settings, Input, Camera controls | - |
    | Ctrl+O, hold 3 s | options box | options box |
    | Esc | back; quits at the root (stop and save a recording first: ask or not?) | full screen: to windowed; windowed: to scenes |

13. **Carry-over from Cassette and Vinyl:** header rules (REC in the body); row affordances; no drag-only (zoom by keys,
    seek by tap or keys too); the 3 s hold box; Cassette's notice line, steppers, LED meter, input rows; Vinyl's size
    classes, volume group, seek bar, output rows. Which visuals? Cassette and Vinyl are cream decks; does VHS get a
    cassette-tape look of its own (a VHS shell)? The design is case by case (owner rule).
14. **Notices:** "No camera found: plug the camera"; "Camera unplugged"; "Camera turned: restarting"; "Camera busy:
    another program uses it"; "No access to the camera: add your user to the video group"; "Power too weak for the
    camera"; "Disk almost full: recording stopped"; "No sound from <input>"; "This board drops frames at this size".
15. **The L1 glyph:** in the SSH slot (shared) or a new slot (shifting every Esc by about 49 px, as the SSH slot did)?
16. **Icon:** a free hue per ICON-PALETTE; a VHS cassette silhouette must differ from Cassette's (sand cassette).

---

## 13. Phases, efforts, tests, Design QA, reuse, licences

Style: the Controller checks the code and deploys; the owner runs short physical tests from a numbered list; no
deck-verifier by default (decisions.md 2026-11-12). The main path must work; polish is optional.

### 13.1 Order and the two moves (D24)

The owner's order is kept, with two changes:
1. **Basic camera controls move from P3 to P2** (exposure, white balance, zoom, power-line). They cost about 2 days,
   the `QUERYCTRL` code exists from P1, and every recording benefits from them (indoor white balance, 50 Hz flicker).
2. **Within P3 the convert queue comes first.** It is what makes the Zero boards practical; time-lapse can follow.
P0-d can reshape P2 and P3: with hardware H.264 on the Zero 2 W, live H.264 720p becomes possible there and the queue
loses urgency.

### 13.2 P0: probes (Controller about 1 day; owner about 60 minutes)

P0-a to P0-m (11.1). The owner plugs the camera, flips the switch and allows the temporary static ffmpeg and two test
tools (D28). Outcomes: D5 (TurboJPEG or hardware decode), D6 (Zero photo), D9 (defaults and badges), the P2/P3 shape.

### 13.3 P1: 0.1.0 (18-24 worker-days)

- **Spike (days 1-2):** preview fps and CPU on zero8 (PDA rotation), the Pi 3A+ and the Pi 4B.
- **Core** (LVGL-free, with fakes):
  - V4L2 layer, enumeration filter, capture thread, hot-plug watcher, identity;
  - TurboJPEG loader (DHT insertion, scale choice);
  - photo writer (EXIF, atomic write, Cassette naming and counter);
  - MJPEG recorder: MKV through libavformat, AAC through libavcodec, the camera mic through Cassette's ALSA capture,
    the A/V clock mapping, ring, writer, journal and repair;
  - scenes index and thumbnails;
  - playback engine: libavformat demux, TurboJPEG and FFmpeg decode, Vinyl's outputs, volume and media keys,
    `SeekScrub`;
  - the under-voltage reader (optional).
- **UI:** capture, scenes, photo review, windowed playback, settings, notices; layouts for the five classes after the
  coordinator's drafts and the draft review (R-6).
- **Packaging:** Depends `libstdc++6, libgcc-s1, libfreetype6, libinput10, libxkbcommon0, libturbojpeg0,
  libasound2t64 | libasound2, libpulse0, libavformat61 | libavformat62, libavcodec61 | libavcodec62, libavutil59 |
  libavutil60, libswresample5 | libswresample6`; permissions `camera`, `microphone`, `additional_hardware`;
  `X-Background=run`, `X-Portrait=320x480`, no `X-MinSize`; arm64 and amd64; the `copyright` file names every library
  opened at run time (Cassette 0.3.1 pattern).

| # | P1 board test (owner, about 25 minutes per board: zero8, Pi 3A+, Pi 4B, RPI52, HackBerry) | Pass |
|---|---|---|
| B1 | open VHS with the camera plugged | live picture within 2 s, upright, smooth |
| B2 | unplug and replug the camera | notice, then the picture returns |
| B3 | flip the switch | "Camera turned", portrait picture within about 3 s |
| B4 | photo by Space and by the on-screen shutter | in the scenes; opens on a PC at 1920x1080; EXIF date right with the clock set |
| B5 | photo on a Zero board (mode switch) | under 2 s; preview resumes |
| B6 | record 1 minute (MJPEG 720p) with the camera mic, clap at the start | plays in VHS and on a PC, clap in sync |
| B7 | unplug the camera while recording | the scene ends at the unplug and plays |
| B8 | scenes: newest first, rename, lock, delete (asks first) | as described; a locked scene cannot be deleted |
| B9 | windowed playback: play, pause, seek by tap, drag and keys; volume keys | works; the level shows in the body |
| B10 | Super or three fingers during playback and during a recording (multitasking boards) | playback paused, the recording finished and playable; back on return |
| B11 | portrait (Pi 3A+ 480x800, zero8 320x480) and square (HackBerry) | nothing off-screen; header per the band rules |
| B12 | film a phone stopwatch next to the screen | picture delay under 0.3 s |

### 13.4 P2: 0.2.0 (14-19 worker-days)

- Codec picker with the badge data; H.264 recording (hardware by capability, x264 through libavcodec) in fragmented MP4;
  the live encoder watchdog (fall back to MJPEG).
- Input chooser: Cassette's ALSA and Bluetooth list, guards, silence notice.
- Full-screen playback with the tap overlay, after ruling R-7.
- Basic camera controls (exposure, white balance, zoom, power-line), saved per camera and re-applied.

| # | P2 board test | Pass |
|---|---|---|
| B13 | each board's default H.264 for 1 minute, clap | plays on a PC; in sync |
| B14 | 30 minutes (Pi 4B hardware, RPI52 x264, zero8 MJPEG) | no frame warnings; clap in sync at the end; temperature noted |
| B15 | power pull during a recording (one board, owner's word) | repaired at the next start; plays to within about 5 s of the cut |
| B16 | a "too slow" codec is not selectable for recording; a borderline one warns | as described |
| B17 | record from a USB headset, then a Bluetooth headset (HackBerry), then "No sound" | each scene has the right sound or none; silence notice when the mic gives zeros |
| B18 | full screen: enter by F, button and double tap; overlay on tap; Esc and Close leave; 3 s Esc hold ends VHS | as described |
| B19 | white balance, exposure, zoom; unplug and replug | values come back |

### 13.5 P3: 0.3.0 (12-16 worker-days)

- Convert queue (section 5): 6-8 days.
- Time-lapse as a recording mode (section 14.1): 4-5 days.
- The full controls page (every UVC control, Reset): 2-3 days.

| # | P3 board test | Pass |
|---|---|---|
| B20 | convert a 5-minute MJPEG scene to H.264 on zero8 and the Pi 4B | the ETA was close; the result plays; the original kept |
| B21 | Replace original | the source goes only after the result is verified, with one confirmation |
| B22 | reboot during a conversion | at the next start the job restarts; no stray `.part` file |
| B23 | heat or a full disk (owner's word) | the queue pauses with a notice |
| B24 | time-lapse 5 s for 10 minutes | 120 frames and a movie that plays |

### 13.6 Design QA gates

- **Draft review (R-6)** before any UI is built.
- **P1:** advisory QA while building; at the 0.1.0 publish an unresolved FAIL blocks.
- **Scene set** (`app-design-scenes.md`): `qa` (640x480, 800x480, 480x480, 720x720, 480x800, 480x640), `qa-small` at
  `ppmm 630` (480x320, 320x480), `qa-empty` (no scenes), `qa-cjk` (scene names are user text: D-55), `nocontract`,
  `lifecycle`, `keyboard` (rename), error states (no camera, busy, no permission, power).
- **A fake camera** in the harness (a folder of synthetic JPEGs) and a fake decoder for playback scenes.
- **Items to watch:** D-03 (no REC chip in the band); D-29 (Left/Right in playback: the media-page exception);
  D-33 (overlay keys at 480x320); D-34 (seek and zoom without drag only); D-35 (the shutter is a tap; a hold may repeat
  for burst); D-50; **R-7 recorded in `apps/vhs/docs/decisions.md` with the owner's words before P2's full-screen
  scenes are judged**.

### 13.7 Reuse map (apps `origin/main` d5216dc; `wc -l`)

| From | Files | Lines | Use in VHS |
|---|---|---|---|
| Cassette | `core/alsa/*` | 1,782 | microphone capture |
| Cassette | `core/pulse/*` (pulse_api, pulse_stream, real_pulse, bt_input, launcher_facts...) | about 3,900 | Bluetooth inputs, guards, silence detector, launcher facts |
| Cassette | `core/takes/{atomic_file,json,naming,settings,recordings}` | 1,829 | names, counter, settings, listing |
| Cassette | `core/journal`, `core/repair`, `core/ring`, `core/fileops` | 893 | recording journal, ring, file seam |
| Cassette | `core/dsp/meter`, `ui/meter*` | 795 | input meter |
| Vinyl | `core/decode/{decode_libs,ffmpeg_decoder}` | 722 | FFmpeg loader pattern, ABI test |
| Vinyl | `core/engine/{engine,outputs}`, `core/volume/output_volume`, `core/input/media_keys`, `core/seek/seek_scrub`, `core/playback/*` | about 2,800 | playback output, volume group, media keys, seek bar |
| Vinyl | `core/layout/fit`, `core/launcher/launcher_link` | 585 | size classes, launcher requests off the LVGL thread |
| Vinyl | `third_party/stb` | - | fallback decoder |
| `_common` | platform, app, async_run, osk_policy, headless | - | as every app |

Copy now (059 D6 pattern); a shared `apps/_media` is a later question.

### 13.8 Licences and patents (VHS stays MIT and ships no codec code)

| Component | Licence | How used | Patent note |
|---|---|---|---|
| VHS code | MIT | ours | - |
| `videodev2.h` | GPL-2.0 WITH Linux-syscall-note | build-time header | - |
| libjpeg-turbo (`libturbojpeg0`) | IJG + BSD-3-Clause + zlib | dlopen, Depends | baseline JPEG patents expired [I] |
| stb_image 2.30 | MIT or public domain | built in (fallback) | - |
| FFmpeg libav* (Debian and Pi `rpt2` builds [F]) | GPL-2.0-or-later as built (059 8.2) | dlopen, Depends | H.264 and AAC pools still license products; VHS ships no codec code (059's position) [U on expiry] |
| x264, x265, SVT-AV1, dav1d (through libavcodec) | GPL-2.0+, GPL-2.0+, BSD-3-Clause-Clear, BSD-2 | through libavcodec | H.265 pools [U]; AV1 royalty-free by design |
| VideoCore codec (kernel driver, Pi firmware) | GPL-2.0 driver; Broadcom firmware licence | through libavcodec | the board maker's hardware licence [I] |
| alsa-lib, libpulse | LGPL-2.1+ | dlopen (Cassette) | - |
| Vinyl and Cassette modules | MIT (ours) | copied | - |

`app.json` `license` is `MIT`. Every library opened at run time is named in the README credits and the `copyright` file
(D-76).

---

## 14. Later items (kept from the study)

### 14.1 Time-lapse (P3, a recording mode)

- One camera JPEG every N seconds into `<series>/`, no decode or encode while running.
- Intervals 1, 2, 5, 10, 30 s and 1, 5, 10, 30 min; stop after a duration, a count or by hand.
- Under 30 s the camera keeps streaming; from 30 s it stops between shots and starts 1.5 s early for the warm-up (a
  streaming webcam draws about 1 W [I]). An option locks exposure and white balance after the first frame.
- At the end: an MJPEG MKV at 30 fps by copying the JPEGs (seconds, even on a Zero 2 W), listed as a scene; conversion
  through the queue.
- A dark screen needs L2. Storage at about 100 KB per frame [I]:

| Use | Interval | Duration | Frames | Size | Movie at 30 fps |
|---|---|---|---|---|---|
| 3D print | 5 s | 2 h | 1,440 | 144 MB | 48 s |
| Clouds | 2 s | 1 h | 1,800 | 180 MB | 60 s |
| Plants | 5 min | 14 days | 4,032 | 403 MB | 2 min 14 s |

### 14.2 Camera controls (basic P2, full P3)

- Every control read with `QUERYCTRL` and `QUERYMENU`; dependent controls follow `V4L2_CTRL_FLAG_INACTIVE`; absent
  controls are not shown (Cassette's CARD rule, owner 2026-11-08).
- Saved in `~/.config/vhs/cameras.json` by vendor:product plus serial (else `by-id`), only when changed, re-applied at
  every open (UVC cameras forget on re-enumeration [I]); Reset restores `default_value`.
- Ranges known: white balance 2300-6500 K, zoom 100-400 (digital [I]), power line 50 Hz; the rest [U] (P0-b).

### 14.3 QR and document scanning

| Library | Licence | On Pi OS trixie arm64 | Note |
|---|---|---|---|
| quirc | ISC | **not packaged** (`apt-cache show libquirc1`: nothing) [F] | vendor about 2,000 lines (the HA app vendors qrcodegen the same way) |
| ZXing-cpp 2.3.0 (`libzxing3`, 1,245 kB) | Apache-2.0 | on RPI52, not the Pi 4B [F] | **C API exported: 66 `ZXing_*` symbols** [F `nm -D`] |
| zbar 0.23.93 (`libzbar0t64`, 305 kB) | LGPL-2.1+ | on RPI52 [F] | depends on libx11-6, libdbus, libv4l, libjpeg [F] |

- Estimated 15-50 ms per 640x480 frame on a Zero 2 W [I]; P0 later. A fixed-focus webcam needs the code at 30-50 cm, so
  scan 1280x720 grey at 3-5 per second [U autofocus].
- Results: text and URLs to a note (no browser on the boards); Wi-Fi codes shown, joining needs L5.
- Document scan: one 1080p frame, corners set by keys or taps (never drag only), perspective warp, threshold, PNG into
  `<vault>/Scans/` with a note. No OCR.

### 14.4 Motion clips, LAN stream, Home Assistant, baby monitor

- Motion: a DC-only decode (1/8: 160x90 from 720p), a background average and a changed-pixel count: about 1-3 % of a Zero
  2 W core at 5-10 checks a second [I]. Clips from a RAM pre-roll of MJPEG frames (3-5 s).
- LAN stream: an own minimal HTTP server (`multipart/x-mixed-replace`, `/snapshot.jpg`), off by default, per session,
  LAN only, a 128-bit token, no TLS, 2-3 viewers, stops with VHS; needs the `network` permission. libmicrohttpd
  (LGPL, 336 kB + GnuTLS [F]) is more than needed.
- Home Assistant: its MJPEG camera integration on that URL, motion by an HA webhook (libcurl). [F] The HA app 0.3.0 lists
  "No ... cameras": a camera tile there (`/api/camera_proxy/<entity>`) is an HA-app task.
- Baby monitor: an audio endpoint beside the stream and a local level alarm; off by default.

### 14.5 Geotag

- [F] Deck Atlas reads NMEA from known USB receivers and writes `position.state` (tmpfs; `fix`, `lat`, `lon`, `alt_m`,
  `acc_m`, `utc`, `mono_ms`, believed while `pid` lives), **only while Atlas is in front** (no `X-Background`).
- So geotag needs Atlas in the background (preferred) or L4. VHS would only read the file (fix 2 or 3, under 5 s old)
  and write the EXIF GPS IFD; off by default; a `geotags.csv` could let Atlas show photo pins later.

### 14.6 Magnifier, more cameras

- Magnifier: zoom 4x by keys, freeze, contrast or invert on the preview; 1-2 days.
- Other UVC cameras, the tablet's built-in cameras, CSI cameras (libcamera): later (D3).

---

## 15. Risks, open questions, decisions

### 15.1 Risks

| Risk | Likelihood / impact | Mitigation |
|---|---|---|
| Live view or MJPEG playback too slow on the Zero 2 W with the PDA rotation | medium [I] | 640x360 preview, frame skipping, DCT scaling, the P1 spike; hardware decode if P0-d passes |
| No hardware H.264 on the Zero 2 W | medium [U] | MJPEG copy plus the queue; x264 480p live |
| Frames without Huffman tables | medium [U] | insert Annex K tables (P0-a) |
| Under-voltage resets the camera mid-recording | high on weak supplies [F] | journal and repair; notice; L3 |
| Switch or unplug while recording | certain sometimes [F] | finish on `ENODEV`; B7 |
| SD stalls and wear with MJPEG | medium [F Cassette P0] / [I] | ring and writer; conversion; H.264 defaults |
| Conversions of hours on the Zero 2 W, heat | certain for long 1080p scenes [I] | 720p suggestion, ETA, 75 C pause, PC alternative |
| Full-screen playback fails Design QA without the ruling | certain [F checklist] | R-7 before P2 |
| VmRSS above the 25 MB guideline while recording or playing (FFmpeg +22.5 MB on a Zero 2 W [F Vinyl], buffers) | certain [I] | D26; FFmpeg loaded only when needed |
| A/V drift; Bluetooth mic latency | medium [I] | audio-master mapping; P0-f; B13, B17 |
| CM969 mic level unknown in dB | certain [F] | percent plus meter |
| Six of eight boards unreachable for this plan | - | P0 before P1 |

### 15.2 Open questions

1. Which boards first (the Pi 4B and zero8 bracket the range)?
2. Will the Pi 4B's supply be replaced (under-voltage at idle today [F])? Which panel is on it now?
3. Is a full-screen capture view wanted too (R-7 would widen)?
4. Does VHS get a VHS-cassette look of its own (Cassette and Vinyl have theirs)?
5. Should Deck Atlas get `X-Background=run` before a later geotag?

### 15.3 Decisions for the owner (recommendation on each line)

1. **D1 Name and id:** VHS, id `vhs` ([F] not in the built-in tile list). Rec: yes.
2. **D2 Interpretation** (a)-(h) at the top of this plan. Rec: yes.
3. **D3 One camera:** the CM969 only; the code finds cameras by capability, others "may work, unsupported"; more UVC,
   tablet and CSI cameras later. Rec: yes.
4. **D4 Capture:** own V4L2 code; no libv4l, GStreamer, libavdevice or the toolkit's camera service. Rec: yes.
5. **D5 Decode for the live view and MJPEG playback:** TurboJPEG (Depends `libturbojpeg0`) with DCT scaling;
   stb_image fallback; VideoCore hardware decode later if P0 shows the need. Rec: yes.
6. **D6 Photos on the Zero class and Pi 3A+:** full resolution by mode switch (about 1-1.5 s) plus a Quick photo
   setting; instant 1080p elsewhere and during recordings. Rec: yes.
7. **D7 Files:** one folder `~/VHS/` (setting), Cassette-style names (`scene_`, `photo_`), `nodate` until the clock is
   set, mode 0700. Rec: yes.
8. **D8 Containers:** MKV + AAC for MJPEG, fragmented MP4 + AAC for H.264; FFmpeg as hard Depends from P1 (recording
   with sound and playback both need it). Rec: yes.
9. **D9 Codec picker:** the badge rules of 4.1, the table of 4.2, the defaults per board, the live encoder watchdog.
   Rec: yes.
10. **D10 H.265 and AV1:** conversion targets on the Pi 4/5 class only, never live, hidden on the Zero class and the
    Pi 3A+. Rec: yes.
11. **D11 Sound input:** Cassette's list and guards, camera mic first and default, "No sound" as a choice, the mic's
    level left as found and shown as a percentage. P1 camera mic only, the chooser in P2. Rec: yes.
12. **D12 A/V sync:** the audio clock as master, video timestamps mapped onto it. Rec: yes.
13. **D13 Convert queue:** inside VHS (in front, or hidden with `run`), no daemon; nice 10 and idle I/O on 2 threads,
    Fast optional; pause above 75 C, optional "only while charging"; disk check; keep the original by default, Replace
    after verification with one confirmation; a crashed job restarts from zero. Rec: yes.
14. **D14 Long conversions:** show the ETA; above 60 minutes suggest a lower resolution or Fast; above 4 hours show the
    PC alternative. Rec: yes.
15. **D15 Full-screen playback:** adopt ruling **R-7** as worded in 8.2 and record it in `apps/vhs/docs/decisions.md`
    and the checklist's rulings. Rec: yes.
16. **D16 Playback decode:** FFmpeg software, hardware H.264 on VideoCore boards and HEVC hardware on the Pi 4/5 where
    the Pi's FFmpeg supports it; audio-master frame dropping with a hint to convert; MJPEG exact seeks, H.264 keyframe
    seeks while scrubbing and exact on release. Rec: yes.
17. **D17 Audio output:** Vinyl's stack (outputs, per-output volume, Bluetooth, media keys, seek bar). Rec: yes.
18. **D18 Multitasking:** `X-Background=run`; on hide: finish recordings, release the camera, pause playback, keep
    converting; background recording later. Rec: yes.
19. **D19 Switch and unplug:** finish the file, notice, reopen by USB identity in the new orientation. Rec: yes.
20. **D20 Power:** read the Pi's under-voltage alarm when present and give supply or powered-hub advice; ask for L3.
    Rec: yes.
21. **D21 Scene actions:** play, convert, rename (Cassette's rules), delete (asks), lock, copy to a mounted drive;
    photos also "Send to Notes". Rec: yes.
22. **D22 Time-lapse** as a recording mode in P3, with the camera idle between shots of 30 s or more. Rec: yes.
23. **D23 Camera controls:** basic in P2, full in P3, saved per camera and re-applied. Rec: yes.
24. **D24 Phase order:** the owner's P1-P3 with basic controls moved to P2 and the queue first in P3 (13.1). Rec: yes.
25. **D25 Launcher requests:** L1 with VHS 1.0, L2 with P3, L3 any time, L4-L6 later; none for full screen. Rec: yes.
26. **D26 Memory:** accept VmRSS above the 25 MB guideline while recording, playing or converting on the Zero 2 W (as
    Vinyl does with FFmpeg); measure in P1. Rec: yes.
27. **D27 Screen sizes:** Vinyl's classes (Small, Medium, Portrait with `X-Portrait=320x480`, Square, Large), no
    `X-MinSize`; layouts are the coordinator's design. Rec: yes.
28. **D28 P0 permissions:** the temporary static ffmpeg and two test tools on zero8, Pi 3A+, Pi 4B, RPI52, HackBerry
    and the tablet (removed after, with a rollback list); a few saved frames; the owner flips the camera. Rec: yes.
29. **D29 Later items:** QR, document scan, motion, LAN stream and Home Assistant, baby monitor, geotag, magnifier,
    more cameras, each decided on its own after 1.0 (section 14). Rec: yes.

---

## 16. Addendum: P0 results (2026-11-14)

All probes ran on 2026-11-14 on the real boards with the camera plugged into the named board; temporary tools and files
were removed afterwards (`libturbojpeg0` 2.1.5 stays installed on zero8 and the Pi 3A+, owner OK). Tags: **[F P0-x]**
measured by that probe, **[F 11-13]** the benchmarks of 2026-11-13 (3.1), **[I]** inference, **[U]** still open.
Per-hour sizes marked "x360" are the measured MB per 10 s times 360 (arithmetic, decimal GB).

### 16.1 Probes

| # | Board(s) | What was run | Result | Against the plan |
|---|---|---|---|---|
| P0-a | zero8 | one camera MJPEG frame per mode (1080p, 720p, 480p), markers parsed | 4 DHT segments in every frame (`DB DB DB C0 C4 C4 C4 C4 SOS`); 235 / 129 / 51 KB per frame | **Pass**: the DHT [U] of 1.2 and the "frames without Huffman tables" risk (15.1) are gone |
| P0-b | Pi 3A+, both switch positions | `--list-formats-ext`, USB descriptors, `/proc/asound`, kernel log | position A: portrait MJPG only, 6 modes, 30 fps; position B: landscape MJPG 6 modes at 60 and 30 fps plus YUYV 640x480 / 640x360 at 30; a108:2240 in both; `bMaxPower` 200 mA; mic S16_LE stereo 16 kHz only | **Partly fails**: the mic is not 48 kHz (1.2); portrait is not "the same modes rotated"; no MJPEG 640x360 (2.3). `amixer` contents and the `by-id` names: not recorded |
| P0-c | zero8, Pi 3A+ | TurboJPEG 2.1.5, 300 frames per mode at 8/8, 4/8, 2/8, 1/8 and Y-only | table in 16.2 C4; Huffman floor on zero8 about 17 ms (1080p), 8 ms (720p) | **Pass** for D5; confirms the [I] of 2.2 that scaling saves little at 1080p. stb_image: not measured |
| P0-d | zero8, Pi 3A+ | static ffmpeg 7.0.2, 10 s each: `h264_v4l2m2m` from YUYV and from MJPEG, x264 ultrafast 480p, MJPEG copy | hardware H.264 works on both; cores in 16.2 C3 | **Pass**: 3.2's [I] becomes [F]; 1080p through the hardware encoder is too slow on the Zero 2 W (0.88x) |
| P0-e | zero8 | YUYV 640x360, 900 frames | steady 30.01 fps, no drops, `throttled=0x0` | **Pass**. Whether the keyboard and hub were attached: not recorded |
| P0-f | Pi 4B | `arecord` 600 s at 16 kHz stereo (the command's 48 kHz does not exist on this mic) | 9,600,000 frames (600.000 s of audio) in 600.277 s of wall time: at most +0.046 % (0.28 s per 10 min), `arecord` start/stop included | **Pass** for D12. The 20-100 ppm estimate of 3.6 is neither confirmed nor ruled out: the bound is coarser [I] |
| P0-h | Pi 4B (not a planned board); Pi 3A+ (replaced); RPI52 | Pi 4B: 1080p MJPEG 30 fps for 10 min, `get_throttled` every 5 s. Pi 3A+: **replaced by the new-supply test** (10 min 1080p MJPEG, 4 CPU burners from minute 5) | Pi 4B: 30.00 fps, 121 samples all 0x0, no under-voltage lines, max 55.5 C. Pi 3A+: no under-voltage; soft temperature limit only (16.2 C11) | **Partly done**: RPI52 soak with the camera not run |
| P0-j | RPI52 | x264 veryfast 1080p30 for 30 min, **synthetic `testsrc2` source** | 1.0x, 1.16 cores, 0 throttle samples, max 49.9 C, fan state 1, 2.4 GHz held | **Pass, partly**: no camera, so no MJPEG decode in the load [I] |
| P0-k | RPI52 (60 s quiet scene; 40 s **moving scene**); sizes also from P0-d (zero8, Pi 3A+) and P0-m (Pi 4B) | MJPEG copy and x264 / x265 / SVT-AV1 per preset, file sizes and cores | 16.2 C7, C8 | **Partly done**: x264 sizes depend on motion; x264 sizes on zero8 and the Pi 4B not measured |
| P0-l | HackBerry; tablet | HackBerry: system ffmpeg 7.1.5, 10 s runs | camera modes as position B; **no hardware H.264 encoder** (`h264_v4l2m2m` fails, -22); x264 veryfast 1080p 1.42 cores; max 58 C, no throttling | HackBerry **pass** ("as RPI52" [I] confirmed); **tablet not done** |
| P0-m | zero8, Pi 4B, RPI52 | `ffmpeg -f null` decode, software and hardware; HEVC with `-hwaccel drm` (Pi 4B, RPI52) | 16.2 C6 | **Partly done**: Pi 3A+, HackBerry and tablet not measured; zero8 1080p hardware decode aborted in the ffmpeg CLI at frame 209 |

### 16.2 What changes in the plan

C1. **Microphone (1.2, 3.3, 3.4, 3.6):** the camera's card offers **S16_LE stereo at 16 kHz only**, not 48 kHz
[F P0-b, P0-f]. The 48 kHz of the 2026-11-12 clip was FFmpeg's resample [I]. AAC is encoded from 16 kHz (or
resampled) [I]; PCM at 16 kHz stereo is 0.512 Mbit/s = 0.23 GB/h (arithmetic). The kernel still logs "Unlikely big
volume range 3072" [F P0-b].

C2. **Modes per switch position (1.2, 2.3, 7):** [F P0-b]

| Position | Formats and sizes | Rates |
|---|---|---|
| A (portrait) | MJPG only: 1080x1920, 960x1280, 720x1280, 600x800, 576x1024, 480x640 | 30 fps |
| B (landscape) | MJPG 1920x1080, 1280x960, 1280x720, 1024x576, 800x600, 640x480; YUYV 640x480, 640x360 | MJPG 60 and 30 fps; YUYV 30 fps |

- There is **no MJPEG 640x360** (2.3's Zero and Pi 3A+ preview stream) and **no YUYV in portrait**.
- The USB id stays a108:2240 in both positions; `bMaxPower` 200 mA [F P0-b] (1.2's [U] on both resolved).
- VHS must build its mode list from `ENUM_FMT` / `ENUM_FRAMESIZES` / `ENUM_FRAMEINTERVALS` at every open and pick
  from it, never assume one list [I].

C3. **Hardware H.264 encoder on the Zero 2 W and Pi 3A+ (3.1, 3.2, 4.2, 5.1, 5.2):** 3.2's [I] becomes
**[F P0-d]**: `bcm2835-codec-encode` (`/dev/video11`, H264 and MJPG out) on both, `h264_v4l2m2m` works; zero8 CMA
256 MB (113 MB free). Cores per wall second, 10 s live:

| Input -> hardware H.264 | Pi 3A+ | zero8 (Zero 2 W) |
|---|---|---|
| YUYV 640x480, 2 Mbit/s | 0.06 (2.4 MB/10 s) | 0.07 (2.5 MB/10 s), real time |
| MJPEG 640x480 | 0.46 | 0.56 |
| MJPEG 720p, 4 Mbit/s | 1.09 (5.0 MB/10 s), real time | 1.54 (5.0 MB/10 s), 0.994x |
| MJPEG 1080p, 8 Mbit/s | 2.7 (10.1 MB/10 s), 0.99x | 3.4, **0.88x: too slow** |
| for comparison: x264 ultrafast 480p | 0.75 | 1.22 |
| for comparison: MJPEG copy 720p | 0.16 (20 MB/10 s) | 0.26 (35 MB/10 s) |

- Pi 3A+: no throttling, max 49 C; zero8: max 38-42 C, no throttling [F P0-d].
- The cost of MJPEG input is FFmpeg's MJPEG decode, not the encoder [F P0-d]. 3.2's "MJPEG to the hardware
  decoder, then the encoder, almost no CPU" path is **not measured** [U].
- 5.2's "Zero 2 W hardware H.264 about 0.3-1 [I]": measured c = 1.54 at 720p from MJPEG, so about 0.8 minutes of
  work per minute of video on 2 cores (5.2's method) [I]; Pi 3A+ c = 1.09, about 0.55 [I]. 5.1's condition is met:
  the queue becomes a convenience on these boards.

C4. **TurboJPEG decode (2.2, 2.3):** [F P0-c], ms per frame (mean of 300); cores at 30 fps = ms x 0.03 (arithmetic):

| Mode | Board (frame size) | 8/8 | 4/8 | 2/8 | 1/8 | Y-only | Peak RSS |
|---|---|---|---|---|---|---|---|
| 1080p | zero8 (242 KB) | 39.2 | 26.0 | 22.0 | 17.2 | 23.5 | 79 MB |
| 720p | zero8 (131 KB) | 18.5 | 12.5 | 10.7 | 8.3 | 11.4 | 43 MB |
| 480p | zero8 (55 KB) | 6.8 | 4.7 | 4.1 | 3.2 | 4.2 | 19 MB |
| 1080p | Pi 3A+ (107 KB, darker scene) | 23.8 | 14.3 | 11.6 | 9.0 | 13.1 | 39 MB |
| 720p | Pi 3A+ (55 KB) | 10.9 | 6.6 | 5.4 | 4.2 | not measured | 21 MB |
| 480p | Pi 3A+ (22 KB) | 3.8 | 2.3 | 1.9 | not measured | not measured | 9 MB |

- The Huffman decode is a floor (zero8 about 17 ms at 1080p, 8 ms at 720p) [F P0-c]: **1080p live view is ruled out
  on the Zero 2 W** (4/8 = 0.78 core before display); 1080p there is for stills only (39 ms).
- **Zero 2 W live view: 720p at 4/8** (12.5 ms = 0.38 core) or 480p at 8/8 (6.8 ms = 0.20 core).
- **Pi 3A+:** 720p at 4/8 = 0.20 core; **1080p at 4/8 = 0.43 core is possible**, but on 107 KB frames; a brighter
  scene (zero8's 242 KB) costs more, so 720p at 4/8 is the safe default [I].
- These replace 2.2's FFmpeg-scaled cost table [I] for the TurboJPEG path. Pi 4B, Pi 5 and HackBerry: TurboJPEG not
  measured (FFmpeg decode figures in C6).

C5. **DHT and photo size (2.2, 7):** the camera's frames carry their Huffman tables [F P0-a]: no Annex K insertion is
needed; keep it as a cheap fallback for other cameras. The camera's own frames are 235 / 129 / 51 KB (zero8 scene) and
107 / 55 / 22 KB (Pi 3A+ darker scene) at 1080p / 720p / 480p [F P0-a, P0-c], not 7's 60-125 KB.

C6. **Playback decode (8.3; replaces its [I] table where measured):** `ffmpeg -f null`, as fast as possible.

| Format | zero8 (Zero 2 W) | Pi 4B | RPI52 (Pi 5) |
|---|---|---|---|
| MJPEG 720p, software | 17 ms/frame, 1 core, 58 fps max | 6.4x real time | 20x |
| MJPEG 1080p, software | 36 ms/frame = 28 fps: just under real time | 3.1x (1 core) | 8.6x |
| H.264 480p, software | 7x (3 cores) | not measured | not measured |
| H.264 720p, software | 3.0x | 8x | 41x |
| H.264 1080p, software | 1.45x (3.5 cores: at the limit) | 3.9x (3.4 cores) | 17x |
| H.264 hardware (`h264_v4l2m2m`) | 480p 9.4x at 0.08 s CPU per 10 s; 720p 4.2x at 0.11 s; 1080p 2x until the ffmpeg CLI aborted at frame 209 | 720p 6.4x, 1080p 2.8x at about 1 s CPU per 20 s clip | **none**: `-hwaccel drm` decodes H.264 in software |
| HEVC, software | not measured | 720p 12x, 1080p 5x | 1080p 13x (15.4 s CPU per 60 s clip) |
| HEVC hardware | not measured | `-hwaccel drm` (`rpi-hevc-dec`, `/dev/video19`): 720p 13x, 1080p 6x, about 3x less CPU than software | `-hwaccel drm`: 1080p 3.7 s CPU per 60 s clip |

- [F P0-m] **HEVC hardware works only through `-hwaccel drm`** on the Pi 4B and RPI52; `hevc_v4l2m2m` fails
  ("Invalid argument") on both and VAAPI fails on the Pi 4B (no libva). 8.3's "[I] P0-m checks" is answered.
- [F P0-m] zero8's 1080p hardware abort is an ffmpeg-side assertion ("Assertion pkt failed at ffmpeg_dec.c:710"),
  not a kernel error; whether VHS's own libavcodec loop hits it is [U].
- Hardware H.264 decode is practically free on the VideoCore boards [F P0-m], so D16 holds: hardware H.264 there,
  MJPEG in software (1080p MJPEG on the Zero 2 W only at a reduced rate or scaled).
- Pi 4B and zero8 temperatures: max 52.5 C and 50 C, `throttled=0x0` [F P0-m].

C7. **Encode costs, Pi 5 and HackBerry (3.1, 4.2):** cores per wall second, all about real time, no throttling
(RPI52 max 52.7-53.8 C, HackBerry max 58 C) [F P0-k, P0-l]:

| Encoder, preset | RPI52 quiet 60 s | RPI52 moving 40 s (crf 26; x265 crf 28) | HackBerry 10 s | RPI52 2026-11-13 [F 11-13] |
|---|---|---|---|---|
| MJPEG copy 720p | not recorded | not recorded | about 0 (15.8 MB/10 s) | 0.2 % |
| x264 ultrafast 720p | 0.30 | 0.40 | 0.42 | 0.43 |
| x264 veryfast 720p | 0.46 | 0.64 | 0.62 | 0.7 |
| x264 medium 720p | 0.47 | 1.59 (0.985x) | 1.58 (0.94x) | 2.4 (0.93x) |
| x264 ultrafast 1080p | 0.64 | 0.79 | 0.75 | 0.8 |
| x264 veryfast 1080p | 1.73 (0.996x) | 1.40 | 1.42 | 1.9 |
| x265 ultrafast 1080p | 2.3 | 2.5 | 2.5 (0.98x) | 3.7 (0.91x) |
| SVT-AV1 preset 10, 720p | 1.0 | 0.9 | 0.87 | 1.2 |

- **The HackBerry has no hardware H.264 encoder** [F P0-l]; x264 veryfast 1080p (1.42 cores) is fine there.
- x264 medium 720p moves from "too slow" (0.93x) to borderline on the Pi 5 (0.985x at 1.59 cores) [F P0-k].
- x265 ultrafast 1080p and SVT-AV1 720p now run in real time on the Pi 5 and HackBerry [F P0-k, P0-l]; D10 (never
  live, conversion targets only) is unchanged.
- Peak RSS 115-500 MB on the HackBerry runs [F P0-l].

C8. **Disk per hour (3.4), moving indoor scene, RPI52** [F P0-k]; real footage varies by several times; MJPEG copy is
15-100x larger than H.264 at the same size:

| Mode | Size | Per hour |
|---|---|---|
| MJPEG copy 720p | 82.9 MB / 40 s (2.07 MB/s) | **7.5 GB** (quiet scene 1.6 GB) |
| MJPEG copy 1080p | 158.5 MB / 40 s (3.96 MB/s) | **14.3 GB** (quiet scene 3.6 GB) |
| x264 ultrafast 720p, crf 26 | 8.7 MB / 40 s | 0.78 GB |
| x264 veryfast 720p, crf 26 | 1.9 MB / 40 s | 0.17 GB |
| x264 medium 720p, crf 26 | 3.8 MB / 40 s | 0.34 GB |
| x264 ultrafast 1080p, crf 26 | 17.9 MB / 40 s | 1.6 GB |
| x264 veryfast 1080p, crf 26 | 5.9 MB / 40 s | 0.53 GB |
| x265 ultrafast 1080p, crf 28 | 0.65 MB / 40 s | 0.06 GB |
| SVT-AV1 preset 10, 720p | 1.4 MB / 40 s | 0.13 GB |
| hardware H.264 720p / 1080p, 4 / 8 Mbit/s (rate-controlled, not motion-dependent) | Pi 4B 10 / 20 MB per 20 s [F P0-m]; zero8 and Pi 3A+ 5.0 MB/10 s at 720p, Pi 3A+ 10.1 MB/10 s at 1080p [F P0-d] | 1.8 / 3.6 GB (x360) |
| hardware H.264 640x480 from YUYV, 2 Mbit/s | 2.4-2.5 MB/10 s [F P0-d] | 0.86-0.9 GB (x360) |
| MJPEG copy 720p, other boards | zero8 35, Pi 3A+ 20, HackBerry 15.8 MB/10 s [F P0-d, P0-l] | 12.6 / 7.2 / 5.7 GB (x360) |

C9. **Pi 5 thermal (4.2's [U]):** [F P0-j] 30 minutes of x264 veryfast 1080p30 in real time at 1.16 cores, max 49.9 C,
no throttling, 2.4 GHz held, with the fan (synthetic source; the camera's MJPEG decode is not in this load [I]).

C10. **A/V drift (3.6):** [F P0-f] at most +0.046 % (0.28 s per 10 min) including `arecord`'s overhead: the audio clock
stays the master (D12 unchanged); long recordings keep remapping video time onto it, as 3.6 already plans.

C11. **Power (3.7, Pi 3A+ and Pi 4B):**
- Pi 3A+ old supply: active under-voltage with the camera (`0x50005`) [F 11-13].
- Pi 3A+ new supply, 10 min of 1080p MJPEG plus 4 CPU burners from minute 5: **no under-voltage** (bits 0 and 16
  never set, no dmesg lines, core 1.25-1.36 V); only the **soft temperature limit** (`0x80008` / `0x80000`) once
  60 C was reached at t = 395 s with the burners, max 62.8 C, clock alternating 1.2 / 1.4 GHz; no heatsink fitted
  [F P0-h replacement]. Advice: the new supply is fine for the camera; **a heatsink for sustained full load**.
- Pi 4B: 10 min 1080p MJPEG all `0x0`, max 55.5 C; the idle `0x50005` of 2026-11-13 did not recur [F P0-h].

C12. **Switch flip and USB reset (3.7, D19, test B3):** on the Pi 3A+ (dwc_otg) [F P0-b]:
- flipping the switch live made the camera drop and **fail to re-enumerate** ("no configurations -22");
- unbinding / binding `dwc_otg` through sysfs **OOPSes the kernel**, USB dead until reboot;
- a reboot with the camera already in the chosen position works.

Consequences [I]:

- never do either on the Pi 3A+ / Zero boards: not in VHS, not in tests; VHS never resets USB itself;
- the orientation path (3.7) must tolerate the camera coming back late or not at all: after a wait, a notice such
  as "Camera did not come back: unplug and replug it, or restart the board" (wording is the coordinator's);
- B3 on the Zero / Pi 3A+ boards expects that notice, not a portrait picture; set the position before boot there.
- A live flip on the xhci boards (Pi 4B, Pi 5, HackBerry) and a replug on dwc_otg: not tested [U].

### 16.3 Updated per-board defaults and badges (proposal for the owner)

**Proposal for the owner.** D1-D29 were accepted as recommended on 2026-11-14 (section 17) and their text is unchanged.
This table proposes revisions of **D5, D6 and D9** from the evidence above; where it differs from the D9 table in 4.2 it
would supersede it once the owner agrees. Badges use 4.1's rules (OK: real time at 2 cores or less; Borderline:
0.98-1.0x or above 2 cores; Too slow: under 0.98x). Numbers are cores.

| Codec, preset | zero8 / deck (Zero 2 W) | Pi 3A+ | Pi 4B | Pi 5 (RPI52, RPI53) | HackBerry (CM5) | tablet |
|---|---|---|---|---|---|---|
| MJPEG copy 720p30 | OK (0.26) | OK (0.16) | OK (0.2 % [F 11-13]) | OK | OK (about 0) | Unknown |
| MJPEG copy 1080p30 | OK (3 % [F 11-13]) | OK (2 % [F 11-13]) | OK | OK | OK [I] | Unknown |
| H.264 hardware 640x480 from YUYV | **OK (0.07); default** | OK (0.06) | not measured, OK expected [I] | none | none | Unknown |
| H.264 hardware 640x480 from MJPEG | OK (0.56) | OK (0.46) | not measured | none | none | Unknown |
| H.264 hardware 720p, 4 Mbit/s | Borderline (1.54, 0.994x) | **OK (1.09); default** | **OK (0.44 [F 11-13]); default** | none | none | Unknown |
| H.264 hardware 1080p, 8 Mbit/s | Too slow (3.4, 0.88x) | Borderline (2.7, 0.99x) | OK (0.91 [F 11-13]) | none | none | Unknown |
| x264 ultrafast 480p | OK (0.95 [F 11-13]; 1.22 [F P0-d]) | OK (0.75) | OK | OK | OK [I] | Unknown |
| x264 ultrafast 720p | Too slow (3.0, 0.992x [F 11-13]; kept from 4.2: 3 of 4 cores) | Borderline (1.22, 0.996x [F 11-13]) | OK (0.93 [F 11-13]) | OK (0.30-0.43) | OK (0.42) | Unknown |
| x264 veryfast 720p | Too slow | Too slow [I] | Borderline (1.9, 0.98x [F 11-13]) | OK (0.46-0.7) | OK (0.62) | Unknown |
| x264 ultrafast 1080p | Too slow (0.474x [F 11-13]) | Too slow (0.90x [F 11-13]) | Borderline (2.0 [F 11-13]) | OK (0.64-0.8) | OK (0.75) | Unknown |
| x264 veryfast 1080p | Too slow | Too slow | Too slow (0.74x [F 11-13]) | **OK (1.16-1.73); default** | **OK (1.42); default** | Unknown |
| x264 medium 720p | Too slow | Too slow | Too slow (0.665x [F 11-13]) | Borderline (1.59, 0.985x) | Too slow (1.58, 0.94x) | Unknown |
| x265 ultrafast 1080p (D10: conversion only) | Too slow [I] | Too slow [I] | Too slow [I] | Borderline (2.3-2.5) | Borderline (2.5, 0.98x) | Unknown |
| SVT-AV1 preset 10, 720p (D10: conversion only) | Too slow [I] | Too slow [I] | Too slow [I] | OK by the rule (0.9-1.0) | OK by the rule (0.87) | Unknown |

| Board | Live view (proposal) | Default recording (proposal) | Photo (D6 proposal) |
|---|---|---|---|
| zero8 / deck | MJPEG 720p at 4/8 (0.38 core) or 640x480 at 8/8 (0.20); YUYV 640x360 / 640x480 (no decode, P0-e passed) in position B | **hardware H.264 640x480 from YUYV** (0.07 core, about 0.9 GB/h) in position B; position A (no YUYV): hardware from MJPEG 480x640 [I: about 0.56 core, as 640x480]; MJPEG copy 720p as the "full quality, convert later" choice | 1080p by mode switch, as D6 (1080p decode 39 ms: stills only) |
| Pi 3A+ | MJPEG 720p at 4/8 (0.20 core); 1080p at 4/8 (0.43 on 107 KB frames) as an option [I] | **hardware H.264 720p, 4 Mbit/s** (1.09 cores, 1.8 GB/h) | instant 1080p when the live view streams 1080p; else mode switch [I] |
| Pi 4B | MJPEG 1080p at 1/2 (unchanged; TurboJPEG not measured there, `libturbojpeg0` absent on 2026-11-13) | hardware H.264 720p (unchanged, 0.44); 1080p (0.91) offered as OK | instant 1080p (unchanged) |
| Pi 5 (RPI52, RPI53) | MJPEG 1080p (unchanged; FFmpeg decodes 1080p MJPEG at 8.6x real time [F P0-m]) | x264 veryfast 1080p (unchanged; 30-minute thermal pass, C9) | instant 1080p (unchanged) |
| HackBerry | MJPEG 1080p at 3/8 (unchanged; decode not measured there) | **x264 veryfast 1080p** (1.42 cores) | instant 1080p |
| tablet | not measured | Unknown (P0-l not done) | not measured |

Proposed revisions:
- **D5:** unchanged in substance (TurboJPEG with DCT scaling, stb_image fallback); add the stream rule of C4: Zero 2 W
  live view at 720p 4/8 or 480p, never 1080p; Pi 3A+ 720p 4/8, 1080p 4/8 optional; the DHT insertion becomes a fallback
  (C5). VideoCore hardware MJPEG decode is still "later": P0 shows no need for the live view at 720p.
- **D6:** the Pi 3A+ may take instant 1080p photos when its live view runs at 1080p 4/8; the Zero class keeps the mode
  switch.
- **D9:** the badge table and defaults above (Zero 2 W: hardware H.264 640x480 from YUYV instead of MJPEG copy 720p;
  Pi 3A+: hardware H.264 720p instead of x264 480p; HackBerry: x264 veryfast 1080p), the mode list read from the camera
  (C2), and the per-hour figures of C8.

### 16.4 Open after P0

1. **HEVC hardware decode** works only through the drm hwaccel (C6); VHS uses libavcodec directly, so it needs the same
   `-hwaccel drm` setup through libav (`av_hwdevice_ctx_create` with the DRM type [I]); verify at build on the Pi 4B and
   a Pi 5.
2. **The tablet (FZ-M1, amd64) was not probed** (P0-l, P0-m): encode, VAAPI, decode and the camera there are [U].
3. **Pi 5 soak with the real camera not run** (P0-h on RPI52); P0-j used a synthetic source.
4. **H.264 sizes depend on motion** (C8): one lightly moving indoor scene; real footage varies by several times.
5. Not measured: playback decode on the Pi 3A+, HackBerry and tablet; stb_image decode; TurboJPEG on the Pi 4B, Pi 5 and
   HackBerry; the hardware MJPEG-decode stage before the encoder (C3); the camera mic's mixer (`amixer`).
6. zero8 1080p hardware H.264 decode aborted in the ffmpeg CLI (C6): check VHS's own decode loop in P2.
7. Switch flip on the xhci boards and replug on dwc_otg (C12): to see in the P1 tests (B2, B3).

---

## Not verified

- **Six of eight boards were unreachable** on 2026-11-13 evening (HackBerry, zero8, Pi 3A+, RPI53, tablet, deck).
- **The camera was on neither reachable board:** control ranges, DHT, the identity across the switch, the mic's channels
  and mixer, `bMaxPower`, autofocus, IR and LED are [U].
- **Hardware H.264 encode and MJPEG/H.264 decode on the Zero 2 W and Pi 3A+** are inferred from the Pi 4B and the shared
  driver family.
- **Every decode, playback, conversion, QR and motion cost** here is an estimate from the measured live runs [I]; the
  HackBerry and the tablet have no numbers at all (P0-l).
- **H.264 file sizes:** the benchmark outputs are gone (both reachable boards rebooted since; `/tmp/camtest` and
  `~/camtest` absent [F]).
- HEVC hardware decode through the Pi's FFmpeg (`-hwaccel drm`) was not checked; VAAPI on the tablet neither.
- The TurboJPEG symbol check printed nothing for exact names (versioned symbols, probably); the library exists on RPI52
  [F], API level 2.x by version [I].
- Raspberry Pi documentation facts (USB current limits, the hwmon alarm) were not re-read; patent expiry dates were not
  researched.

---

## Sources

Code and docs (local, read 2026-11-13):
- Apps `origin/main` d5216dc (scratchpad `062-apps/`):
  - Cassette: `app.json`, `README.md`, `docs/SETTINGS.md`, `src/core/alsa/{capture,open_policy,mixer,card}.hpp`,
    `src/core/{writer,journal}.hpp`, `src/core/codec/codec_libs.hpp`, `src/core/takes/{naming,settings,recordings}.hpp`,
    `src/core/pulse/{pulse_api,bt_input}.hpp`;
  - Vinyl: `app.json`, `docs/decisions.md`, `src/core/decode/decode_libs.hpp`, `src/core/layout/{fit,layout}.hpp`,
    `src/core/art/{cover.hpp,stb_cover.cpp}`, `src/core/engine/{engine,outputs}.hpp`,
    `src/core/volume/output_volume.hpp`, `src/core/input/media_keys.hpp`, `src/core/seek/seek_scrub.hpp`,
    `src/core/transport/transport_press.hpp`;
  - Deck Atlas: `app.json`, `README.md`, `src/core/{position_state,gps_link}.hpp`;
  - Home Assistant: `app.json`, `README.md`, `src/core/qr.hpp`; Markdown Notes: `app.json`, `README.md`;
  - `apps/_common/{README.md, LAUNCHER_REF}`, `docs/APP-JSON.md`, `docs/APP-GUIDELINES.md`.
- Launcher `main` d54f2205: `docs/APP-CONTRACT.md` (sections 2-8, 13, 14), `docs/dev/checklists/app-design.md`,
  `app-design-scenes.md`, `docs/dev/decisions.md`, `docs/OPEN_SOURCE_COMPONENTS.md`, `LICENSES/`,
  `ext_components/cp0_lvgl/{Kconfig, SConstruct, src/cp0/cp0_lvgl_camera.cpp, src/cp0/cp0_camera_frame_codec.*}`,
  `docs/cp0_lvgl.en.md`, `projects/APPLaunch/main/ui/{app_display_order.cpp, desktop_app_loader.cpp}`.
- Reports 059 and 060; memory notes `project_webcam_app_ideas.md`, `project_multi_board.md`,
  `project_sound_recorder_cassette.md`, `project_ssh_indicator.md`, `project_launcher_app_sdk.md`,
  `project_state_2026-11-08.md`.
- Scratchpad: the three bench scripts; `webcam-test/` (photo.jpg 145,072 B and z8-photo-1080.jpg 91,683 B, 1920x1080,
  `COM Lavc61`; z8-photo-480.jpg 19,427 B; clip.mkv 16,752,797 B, V_MJPEG 1280x720 + A_AAC 48 kHz 2 ch, 5.03 s,
  Lavf61.7.103).

Board probes (read-only, 2026-11-13), `062-probe-out/`:
- RPI52: Debian 13, launcher 0.10.2, `pi5-dsi7`, 4 GB, NVMe, PipeWire, `sound=0`, `throttled=0x0`; nodes 19-35
  (`rpi-hevc-dec`, `pispbe-*`); ffmpeg 7.1.5 `rpt2` (`libx264`, `h264_v4l2m2m` without a device, `aac`, `libopus`);
  `libturbojpeg0` 2.1.5, `libzbar0t64` 0.23.93, `libzxing3` 2.3.0 (66 C API symbols), `libexif12`, `libcamera0.7`,
  `v4l-utils`; the launcher process `cyberdeckzero` runs as `osrde`; `/tmp` tmpfs 2 GB.
- Pi 4B: Debian 13, launcher 0.9.4-autocheck.2, profile `pi3a-wsdsi43-portrait`, 905 MB, no sound server,
  `sound_kind=alsa`, `throttled=0x50005`; `bcm2835-codec` decode (MJPG, H264 to YU12, NV12, RGB565, RGBA) and encode
  (YUV, YUYV, RGB to H264, MJPG), `rpi-hevc-dec`; CMA 512 MB; `libavcodec61`, `libavformat61`, `libx264-164`,
  `libjpeg62-turbo` (no `libturbojpeg0`), `libexif12`, `v4l-utils`; no ffmpeg CLI, zbar or ZXing.
- `apt-cache show` sizes: libzbar0t64 305 kB, libzxing3 1,245 kB, libexif12 2,866 kB, libturbojpeg0 542 kB,
  libjpeg62-turbo 478 kB, libmicrohttpd12t64 336 kB, libavdevice61 362 kB (plus dependencies), libswscale8 621 kB,
  libx264-164 1,369 kB; `libquirc1`: no such package.


---

## 17. Owner decisions on this plan (2026-11-14)

The owner answered the decision list of 15.3 on 2026-11-14 with: **"go with your recommendations on VHS"**.

| Decisions | Answer |
|---|---|
| D1 to D29 | **Accepted as recommended**, each as worded in 15.3. This includes D1 (name VHS, id `vhs`), D2 (the interpretation points (a) to (h) at the top of the plan), D3 (one camera: the CM969), D8 (FFmpeg libraries as a hard Depends from the first release), D15 (**ruling R-7**: full-screen video playback is exempt from the launcher's header-band rules; Esc and the overlay's Close always leave; to be recorded in `apps/vhs/docs/decisions.md` and in the Design QA checklist's rulings), D24 (phase order with basic controls in P2 and the convert queue first in P3), D26 (memory above the 25 MB guideline while recording, playing or converting on the Zero 2 W) and D28 (P0 permissions: the temporary static ffmpeg and test tools on the named boards, removed afterwards with a rollback list, a few saved frames, the owner flips the camera). |

### 17.1 Cassette features carried over to VHS (owner, 2026-11-14)

The owner: "basically VHS is like cassette for videos (+ it can take photos)"; the Controller listed the Cassette features
that carry over (README of Cassette 0.3.1) and the owner placed each (the options are quoted):

| # | Decision | Owner's answer | Release |
|---|---|---|---|
| D30 | **Self-timer** for photos and video (Cassette's Record timer): Off / 3 / 5 / 10 s, a big countdown over the picture, a tap, Esc or the stop key cancels, no beep, a brief flash at zero for a photo. The owner asked for it: "timer to start recording/take photos 3 / 5 / 10 secs" | "Off/3/5/10 s in P1" | P1 |
| D31 | **Camera microphone controls** (Input level, SOFT gain up to +24 dB, Mic mute, Low cut 80 Hz, Limiter, the LED meter with its target zone) | "All of them in P2" (so P1 has no meter and no gain controls; the recording screen's meter of the P1 boards is drawn without the target zone until P2) | P2 |
| D32 | **Auto-split** of long scenes at 4 GiB (part files, Cassette's `_pN` rule, seamless to a frame or two; Off for ext4 folders) | "In P1, default 4 GiB" | P1 |
| D33 | **Copy a scene to a USB drive**, reusing the mount-on-request code of Vinyl 0.3.0 (udisks, the launcher's polkit rule) | "P1, reuse Vinyl's mount-on-request" | P1 |
| D34 | **Edit** (trim and split: several split points, no dragging, 'save as new files' by default, 'replace original' asks twice; for MJPEG in MKV the cuts need no re-encoding) | "Its own release after P3" | after P3 (0.4.0) |
| D35 | **Merge** scenes (pick in order, only the same codec, size and rate join, keep or delete originals) | "Together with Edit" | with D34 |
| D36 | **Live monitor** (hear the microphone in the headphones while filming) | "Not wanted" | dropped |
| D37 | **Background recording**: foreground only, as Cassette's D10 (the app owns the screen while recording); this replaces the plan's `X-Background=run` for recording | "Foreground only in P1" | P1, revisit on multitasking boards later |
| D38 | **Sound-on lamp** in P1 (a small lamp on the recording screen that lights while the camera microphone delivers a signal above a noise floor; no meter and no gain controls, those come in P2 with D31). The Controller offered it because D31 leaves P1 without any sound level display | "ok for sound on" | P1 |

Consequences for the plan (the Controller's reading; the phase lists in 13.3 to 13.5 stay as written until the build
brief): P1 gains the self-timer, auto-split and USB copy (about 3 worker-days together [I]) and loses nothing; P2 gains the
microphone controls with the sound input chooser (the plan already has the chooser there); P3 stays convert queue first,
then time-lapse; Edit and Merge are a release after P3. Still open: Cassette's scene look (the VHS look of its own, a design
call of the Controller), the 5x scan and 5 s jump keys in playback (carried over as proposed in 12 item 12), and the take
number page (carried over as a Settings page like Cassette's).

Next step: P0, the probes of 11.1 and 13.2, run board by board by the Controller with the camera plugged into the named board and the owner's presence where the plan says so; then the screens (coordinator's design) for the P1 pages, then the P1 build. The Design QA checklist gets ruling R-7 in its next revision.
