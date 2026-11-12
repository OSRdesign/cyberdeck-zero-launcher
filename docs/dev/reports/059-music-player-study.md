# 059: a native music player for the CyberDeck Zero family (study)

Date 2026-11-12. This is a read-only study: no product code was written, nothing was installed, no board or repo checkout was changed, and nothing was committed or pushed.

**Owner request (verbatim):** "a music player, basically something based on the work we did for cassette; it should work with all major available codecs, output can be wired or bluetooth, and contains all industry standards music player features."

**Sources read:**
- Apps repo `origin/main` d1cf53f (Cassette 0.3.1, `apps/_common`, `docs/`, `README.md`). I extracted it with `git archive` into a scratch folder. The local checkout, which is 31 commits behind, was not touched.
- Launcher working tree `main` = `github/main` 9bcfcd07: `docs/APP-CONTRACT.md`, `docs/OPEN_SOURCE_COMPONENTS.md`, `docs/dev/{DEV-GUIDE,decisions,ROADMAP}.md`, reports 026, 046 and 058, `docs/cp0_lvgl.en.md`, the media OSD, screensaver and media-key sources in `projects/APPLaunch/main/ui/`, and the keyboard and media-key headers and the miniaudio use in `ext_components/cp0_lvgl/`.
- Report 058 (Cassette and Bluetooth) and its section 12 (HackBerry P0, 2026-11-11).
- Web sources, listed at the end, all read on 2026-11-12.

**Tags (as in 058):**
- **[F]** fact, with its evidence next to it.
- **[I]** my inference.
- **[U]** unverified: still to measure.

**Board access on 2026-11-12: none.**
- Every board IP (HackBerry .214, RPI52 .160, zero8 .184, Pi 3A+ .170, Pi 4B .167, Pi 5 .175, tablet .183) refused or timed out on TCP port 22, retried three times over about an hour.
- HackBerry and RPI52 did not answer ping either, while the router (.1) did.
- So this report has **no new board measurement**. Board facts come from report 058 section 12, launcher docs and memory notes. Every CPU and RAM figure for our boards is an estimate [I], and P0 (section 9) measures them.
- **Correction to the brief:** the HackBerry is a **CM5 (Pi 5 class, Cortex-A76)**, not a Pi Zero 2 W class board (058 section 2; `board.state` example `board=hackberry-cm5`). The Zero 2 W boards are the deck (zero7.local) and zero8.

---

## 0. Summary of findings and recommendation

1. **[F] Cassette already contains about 30% of a player's engine.** That is roughly 9-10k of its 31.5k source lines:
   - the libpulse layer loaded at run time, with `DONT_MOVE`, the identity guard, Bluetooth profile awareness and the launcher facts;
   - ALSA and sound-server playback sinks, and the output chooser;
   - the libFLAC and libopus decoders with sample-exact seek;
   - a WAV reader, the meter, and the fakes and tests.

   Nothing of the recorder, the tape deck or the UI carries over.
2. **[F] "All major codecs" splits into two sizes of dependency.**
   - **Small decoders.** MP3 (libmpg123: 349 kB installed, depends only on libc) and Vorbis (libvorbisfile: 86 kB, plus libvorbis 220 kB) add under 1 MB on top of Cassette's libFLAC and libopus.
   - **FFmpeg.** It brings AAC/M4A, ALAC, WMA, APE, WavPack, TTA, Musepack, DSF and trackers, but Debian's `libavcodec61` (14.2 MB) pulls 34 libraries (x264, x265, aom, svt-av1, codec2, librsvg...). Six of them alone come to about 53 MB installed.
3. **Recommendation (decode, D2/D3):** option **B+**.
   - Small dedicated decoders are hard dependencies (MP3, FLAC, Opus, Vorbis, WAV/AIFF).
   - FFmpeg (`libavformat`/`libavcodec`/`libswresample`) is loaded at run time as the wide net for everything else, and packaged as **Recommends** (installed by default, optional for the app).
   - The desktop images very likely have FFmpeg already [I, U]. The Lite boards stay slim if the user skips it.
4. **[F] Debian builds FFmpeg with `--enable-gpl`**, so the system `libavcodec` is GPL-2+ as a whole. `fdk-aac` is in Debian non-free and is not used by Debian's FFmpeg build.
   - An MIT app that dlopens system libraries is fine: the repo already credits GPL `librtlsdr` for viz1090.
   - The app ships no codec code of its own.
5. **Output (D5):** copy Cassette's libpulse layer for boards with a sound server, and use ALSA `default` on the Lite boards.
   - Bluetooth works only where PipeWire or PulseAudio runs (HackBerry, Pi 5s, tablet), as in Cassette.
   - [F] Debian's `libspa-0.2-bluetooth` 1.4.2 links SBC, aptX (libfreeaptx), LDAC, LC3 and Opus, and **not fdk-aac** [I: so no AAC over A2DP on Debian's build].
6. **[F] One A2DP device at a time** on the HackBerry (058 P6). The output chooser must show "call quality" when a headset has no music profile. Cassette's code and the launcher's `bt_audio_dev ...:hfp` already carry that.
7. **[F] Background playback exists only where multitasking is on.** That is canvases whose short side is 480 px or more: deck, Pi 3A+ DSI, Pi 4B, Pi 5s, HackBerry, tablet. On 480x320-class screens (zero8), the player owns the panel and Esc ends playback.
8. **[F] Headset and keyboard Play/Pause/Next keys reach no app today.**
   - The launcher takes only the five volume, mute and brightness keys from media-key devices.
   - Apps do not read media-key devices.
   - While an app is hidden, the launcher grabs the keyboards.
   - Recommendation (D9): the player reads the media-key devices itself, read-only and never grabbed. Launcher-side forwarding becomes a later generic capability.
9. **[F] The launcher's volume keys act on the default sink only.** A player sending to an explicitly chosen Bluetooth sink that is not the default would see the volume keys change another device. The design must pick (D11).
10. **[F] The panel never dims while a fullscreen app is in front** unless the app sends `dim allow`, which works on battery boards with backlight levels only. A "screen off while playing" for the app in front is a launcher gap.
11. **MVP (D4): folder first.**
    - Contents: file browser, queue = folder, now playing with tags and cover art, wired and Bluetooth output with pause on disconnect, gapless, resume, background, media keys.
    - The library database (SQLite, as Deck Atlas does) comes in P2.
    - Estimated 10-14 worker-days for P1, 10-15 for P2, 6-10 for P3.
12. **Risks to measure first (P0):**
    - RSS with FFmpeg mapped against the 25 MB app guideline;
    - JPEG cover decode memory, since a 3000x3000 picture is 27-36 MB while decoding;
    - whether headset buttons produce key events on the HackBerry;
    - first-scan time on a Zero 2 W SD card.
13. **Naming:** an app titled exactly "Music" jumps into the launcher's built-in tile block (report 046), so pick another title or accept it (D1).

---

## 1. Codec and container scope

### 1.1 "All major codecs" as tiers

| Tier | Formats | Decoder (recommended) | Licence | Why this tier |
|---|---|---|---|---|
| **T1 always** (hard dependency, small) | MP3 (MPEG-1/2 Layer III, also Layer I/II) | libmpg123 (`libmpg123-0t64`) | LGPL-2.1 | most common file type; gapless (LAME), ID3 and cover art built in [F mpg123 API] |
| T1 | FLAC (native and Ogg FLAC) | libFLAC (`libflac14` / `12t64` / `12`), Cassette's `FlacDecoder` | BSD-3-Clause | lossless standard; already a Cassette dependency |
| T1 | Opus (Ogg) | libopus + Cassette's own Ogg reader (`ogg_opus.cpp`, no libopusfile) | BSD-3-Clause | already a Cassette dependency; R128 and output gain in the header |
| T1 | Ogg Vorbis | libvorbisfile (`libvorbisfile3` + `libvorbis0a` + `libogg0`) | BSD-3-Clause | still common in game rips and Linux libraries |
| T1 | WAV (PCM, WAVE_FORMAT_EXTENSIBLE, 32-bit float), AIFF/AIFC (PCM) | own reader (Cassette's `WavReader` takes tags 1 and 0xFFFE PCM; float and AIFF are new, about 200 lines) | own MIT | no library needed |
| **T2 when FFmpeg is installed** (Recommends) | AAC-LC / HE-AAC in MP4 (.m4a, .m4b, .mp4), ADTS .aac | FFmpeg native `aac` decoder via libavcodec, MP4 demux via libavformat | GPL-2+ as Debian builds it | iTunes and phone libraries; the main reason for FFmpeg |
| T2 | ALAC (.m4a) | FFmpeg `alac` | as above | Apple lossless |
| T2 | WMA (v1/v2, Pro, Lossless) | FFmpeg `wmav1/wmav2/wmapro/wmalossless` | as above | old Windows libraries |
| T2 | Monkey's Audio (.ape), WavPack (.wv), TTA, Musepack (.mpc), TAK [U] | FFmpeg `ape`, `wavpack`, `tta`, `musepack7/8`, `tak` | as above | lossless niches; APE "extra high" is CPU-heavy (1.4) |
| T2 | DSD (.dsf, .dff) to PCM | FFmpeg `dsd_lsbf/msbf` decoders (DSD to PCM conversion, not native or DoP) | as above | rare; accepted only as PCM conversion (D14) |
| T2 | Trackers (MOD, XM, S3M, IT) and chiptunes (NSF, SPC, VGM, GBS) | libavformat's `libopenmpt` and `libgme` demuxers (Debian links both into `libavformat61` [F deps]) | BSD-3 (openmpt), LGPL-2.1+ (gme) | come free with T2; no own code |
| **No** | MIDI (.mid) | would need FluidSynth plus a soundfont (tens of MB) | LGPL / various | not "music files" for most users; D14 |
| No | SACD ISO, DTS-CD, multichannel down-mix tuning, video files | - | - | out of scope (D14) |

**Containers and metadata (same tiers):**

| Item | T1 source | T2 source | Notes |
|---|---|---|---|
| Playlists M3U / M3U8 / PLS | own parser (about 150 lines) | - | relative paths, UTF-8 (M3U8) and Latin-1 (M3U) [I] |
| CUE sheets (single image + .cue) | own parser and virtual tracks (about 300 lines) | - | P2/P3, section 4 |
| ID3v1, ID3v2.2/2.3/2.4 (text, TXXX, APIC, USLT) | libmpg123 (`mpg123_id3`, `MPG123_PICTURE` stores APIC [F]) | libavformat | 2.3 UTF-16 and Latin-1 fall-back: libmpg123 converts to UTF-8 [I] |
| Vorbis comments and FLAC PICTURE | Cassette's Vorbis-comment parser plus libFLAC metadata | libavformat | Opus too (OpusTags) |
| MP4 atoms (©nam, ©ART, aART, trkn, covr, `----:com.apple.iTunes:*`) | - | libavformat (`AVFormatContext.metadata`, attached picture stream) | no own MP4 parser in T1 |
| APEv2 (APE, WavPack, MPC) | - | libavformat | |
| Embedded cover art | mpg123 APIC, FLAC/Vorbis PICTURE | libavformat attached pic | decode with stb_image (built in, as in RSS Reader / Deck Atlas); memory caveat in 1.5 |
| ReplayGain | `REPLAYGAIN_*` Vorbis comments, ID3 TXXX, Opus `R128_*` and header output gain | MP4 `replaygain_*` freeform atoms via libavformat | apply in float with Cassette's limiter as clip guard |
| Gapless info | LAME/Info tag (libmpg123, `MPG123_GAPLESS` "default on" [F]); Opus pre-skip and end trim (Cassette [F]); FLAC/Vorbis exact | `iTunSMPB` and edit lists (libavformat `skip_samples`) [I] | |

### 1.2 Package names and sizes (Debian 13 trixie arm64 [F packages.debian.org]; Ubuntu 26.04 where it differs)

| Package (trixie) | Version | Download / installed | Depends (beyond libc) | Ubuntu 26.04 |
|---|---|---|---|---|
| libmpg123-0t64 | 1.32.10-1+deb13u1 | 139.9 kB / 349 kB | none | same name [U version] |
| libvorbisfile3 | 1.3.7-3 | 19.7 kB / 86 kB | libogg0, libvorbis0a (81.8 kB / 220 kB) | same [I] |
| libopusfile0 (not needed: Cassette reads Ogg itself) | 0.12-4+b3 | 39.3 kB / 155 kB | libogg0, libopus0, **libssl3t64** | same [I] |
| libflac14 (already a Cassette dependency) | 1.5.x | - | - | libflac14 1.5.0+ds-5 [F] |
| libfaad2 (AAC alternative, not recommended) | 2.11.2-1 | 158.7 kB / 544 kB | none | |
| libfdk-aac2t64 | 2.0.3-1, **non-free** [F search] | - | - | multiverse [I] |
| libsndfile1 (not needed) | 1.2.2-2+deb13u1 | 181.2 kB / 615 kB | flac, lame, mpg123, ogg, opus, vorbis | |
| libwavpack1 (only if WavPack were T1) | 5.8.1-1 | 81.0 kB / 223 kB | none | |
| libgme0 | 0.6.3-7+b2 | 117.6 kB / 343 kB | libstdc++ | |
| libopenmpt0t64 | 0.7.13-1+b1 | 727.7 kB / 2,139 kB | libmpg123, libvorbis(file), libstdc++ | |
| libtag2 (TagLib 2, C++; not recommended) | 2.0.2-2 | 303.1 kB / 1,351 kB | libstdc++ | |
| libsoxr0 (resampler, optional) | 0.1.3-4+b2 | 53.6 kB / 149 kB | libgomp1 | |
| libsqlite3-0 (library database, P2) | trixie | already a dependency of Deck Atlas | | same |
| **libavcodec61** | 7:7.1.5-0+deb13u1 | **5,268.5 kB / 14,200 kB** | **34**: libaom3, libdav1d7, librav1e0.7, libsvtav1enc2, libvpx9, libx264-164, libx265-215, libcodec2-1.2, librsvg2-2, libcairo2, libjxl0.11, libmp3lame0, libopus0, libvorbis0a, libva2, libzvbi0t64 ... | **libavcodec62** 7:8.0.1-3ubuntu2 [F] |
| **libavformat61** | 7:7.1.5-0+deb13u1 | 1,113.6 kB / 3,120 kB | **18**: libbluray2, libchromaprint1, libdvdnav4, libdvdread8t64, libgme0, libgnutls30t64, libopenmpt0t64, librabbitmq4, librist4, libsrt1.5-gnutls, libssh-4, libxml2, libzmq5 ... | libavformat62 [I] |
| libswresample5, libavutil59 | 7:7.1.5 | small [U] | | libswresample6, libavutil60 [I] |
| Raspberry Pi archive build | `libavcodec61` 8:7.1.4-0+deb13u1+rpt1 (epoch 8: replaces Debian's on Pi OS) [F aptpeek] | | | |

**What FFmpeg costs on a Lite image [I, measure in P0-a].** Six of the libraries FFmpeg pulls already add 53 MB installed and 19 MB to download:

| Package | Installed |
|---|---|
| libavcodec61 | 14.2 MB |
| libavformat61 | 3.1 MB |
| libx265-215 | 9.3 MB |
| librsvg2-2 | 5.6 MB (plus cairo, pango, gdk-pixbuf) |
| libsvtav1enc2 | 4.3 MB |
| libcodec2-1.2 | 16.5 MB |

With aom, vpx, x264, rav1e, jxl, gnutls and the X11/cairo chain, I expect **100-200 MB installed** on a trixie Lite board.

The desktop images (HackBerry, Pi 5s) probably have it already:
- Pi OS desktop ships the full `rpicam-apps`, which links libav [I].
- `rpicam-apps-lite` is built without libav ([web, unverified forum answers]).
- The tablet is [U].

T1 costs about **0.7 MB installed** on top of Cassette's dependencies.

### 1.3 Licences, compared with Cassette and the repo policy

- **Cassette is MIT** [F `app.json`], and so is every native app.
- Repo policy:
  - "System libraries are not shipped: apt installs them", and every library loaded at run time is named in `depends`/`recommends`, the README credits and the package `copyright` file [F apps README, PUBLISHING.md].
  - GPL system libraries are already in use: viz1090 links `librtlsdr` (GPL-2.0-or-later) [F README libraries table].
  - The launcher's `OPEN_SOURCE_COMPONENTS.md` covers the launcher, not apps.
- **Licence of each library** (table in section 8.2):
  - libmpg123: LGPL-2.1.
  - libvorbis, libogg, libFLAC, libopus: BSD-3-Clause.
  - libsqlite3: public domain.
  - libpulse, libasound: LGPL-2.1+.
  - FFmpeg as Debian builds it: GPL-2+, because of `--enable-gpl` [F debian/rules].
  - libfaad2: GPL-2+ [I, upstream COPYING].
  - fdk-aac: the FDK licence, non-free in Debian [F].
- [I] The app stays MIT. It dlopens system libraries and copies no codec source. FFmpeg being GPL in Debian's build affects only how it is credited (the copyright file says "GPL-2.0-or-later as built by Debian"), as for viz1090.

### 1.4 CPU cost on a Pi Zero 2 W (4x Cortex-A53, 1 GHz)

**Measured anchors (Cassette P0-e on zero8, 2026-11-08):**
- FLAC -5 **encode** of mono speech: 171x real time, 0.6% of a core.
- Opus 24-64 kbps **encode**: 10-13x real time, 7-10% of a core.

**External anchor:** a Pi Zero W (ARM11 1 GHz, much slower per clock than the A53) plays MP3 at "<20%" CPU (Pimoroni forum, unverified).

No primary benchmark for decoding on the A53 was found (the Rockbox codec comparison page is behind bot protection).

**Estimates per stereo 44.1 kHz stream on one core [I, P0-b measures them]:**

| Format | Typical file | Decode, % of one Zero 2 W core [I] | Notes |
|---|---|---|---|
| FLAC 16/44.1 | 900 kbps | 1-3 | decoding is cheaper than the 0.6% mono encode measured, per sample [I] |
| FLAC 24/96 | 3 Mbps | 3-6 | the server may also resample to 48 kHz (below) |
| WAV / AIFF | - | <1 | I/O only |
| MP3 320 kbps (mpg123, NEON) | | 3-6 | |
| Ogg Vorbis q6 | | 4-8 | |
| Opus 128 kbps | | 3-6 | decoding is several times cheaper than encoding (the measured encode was 7-10% mono) |
| AAC-LC 256 kbps (FFmpeg) | | 4-8 | HE-AAC (SBR) about 2x |
| ALAC 16/44.1 | | 2-4 | |
| WavPack normal / TTA | | 3-6 | |
| APE -c2000 / -c5000 / extra high | | 10-20 / 40-80 / may not keep up [I] | warn on APE "extra high" |
| DSF DSD64 to PCM | | 15-30 | |
| **Added by the system** | | | |
| PipeWire resampling 44.1 to 48 kHz (default quality) | | 3-8 [U] | avoided when the graph runs at 44.1 kHz |
| SBC / SBC-XQ encode (A2DP) | | 2-5 [U] | in PipeWire |
| LDAC 990 kbps encode | | 10-20 [U] | in PipeWire; also stresses the 2.4 GHz combo chip |
| 10-band EQ (biquads, float) | | <1 [I: 10 x 2 ch x 44,100 x 5 MAC = 4.4 M MAC/s] | |
| Crossfade | | +1 decode during the fade | |

Conclusion [I]: every T1/T2 format except APE extra high plays on one Zero 2 W core with large margin. The real limits are RAM, SD I/O and cover-art decoding.

### 1.5 Memory

| Item | Estimate | Note |
|---|---|---|
| Decoder state | mpg123 and vorbis well under 1 MB; libFLAC under 1 MB [I] | |
| FFmpeg mapped | **+8-20 MB RSS** after dlopen of libavformat/libavcodec and their ~60 dependencies (relocated data, touched text) [U] | guideline: **native app VmRSS under 25 MB** on the Zero 2 W [F APP-GUIDELINES]; P0-b measures `ffmpeg -i x.m4a -f null -` max RSS against `mpg123 -t` |
| Decoded ring | 4 s of S16 stereo 44.1 kHz = 706 kB; F32 = 1.4 MB | Cassette's SPSC ring |
| Cover art while decoding | 600x600 RGB 1.1 MB; 1000x1000 3 MB; **3000x3000 27 MB (RGB) / 36 MB (RGBA)** | stb_image cannot scale while decoding. Cap the size, or decode with DCT scaling (libjpeg-turbo `scale_denom` via `libturbojpeg` dlopen, or LVGL TJPGD 1/2-1/8 for baseline JPEG); keep thumbnails of 96-320 px cached on disk |
| Library database | SQLite: 0.5-1 kB per track with indexes, so 10k = 5-10 MB and 100k = 50-100 MB on disk; RAM = bounded page cache (4-8 MB) [I] | lists are paged queries, never the whole library in RAM |
| Zero 2 W budget | 512 MB, about 414 MB usable, launcher about 20 MB [F APP-GUIDELINES]; the launcher closes hidden apps below 64 MB (frozen) or 32 MB (running) MemAvailable [F contract 13] | |

---

## 2. Decode architecture

### 2.1 Options

**(A) FFmpeg only** (libavformat/libavcodec/libswresample dlopened, every format through it)

- Pros:
  - one decoder path with all formats, tags, cover art, chapters, gapless (`skip_samples`), and HTTP/ICY streams through `avio`;
  - libswresample for rate and format conversion;
  - the least own code: about 900 lines for the wrapper and the tag mapping [I].
- Cons:
  - the Lite-board install grows by 100-200 MB [I];
  - RSS rises by 8-20 MB [U], against a 25 MB guideline;
  - cold dlopen of about 70 libraries from SD: 0.1-0.5 s [U];
  - library scanning through `avformat_open_input` probing is slower than a tag-only reader [I];
  - VBR MP3 seeking uses the Xing TOC (approximate) unless the file is scanned;
  - two SONAMEs (61 trixie, 62 Ubuntu 26.04) to try, plus the Pi archive's epoch-8 rebuild;
  - the GPL build (only a credit matter).

**(B) Small dedicated decoders** (mpg123, vorbisfile, libFLAC, libopus, own WAV/AIFF; nothing else)

- Pros:
  - under 1 MB of dependencies;
  - low RSS and fast start;
  - each library's own seek index: mpg123's frame index gives accurate VBR seeks [F `MPG123_INDEX_SIZE`];
  - gapless from mpg123 and Cassette's Opus reader;
  - Cassette code reused for FLAC, Opus and WAV.
- Cons:
  - **no AAC/M4A/ALAC/WMA/APE**, which fails "all major codecs";
  - adding AAC means faad2 (GPL-2+) plus an own MP4 demuxer and atom-tag reader (about 800-1,200 lines, a risky parser [I]).

**(B+) = B for T1, plus A as an optional wide net for T2 (recommended)**

- The `Decoder` seam already exists in Cassette (`make_decoder()` by magic bytes). Add `Mp3Decoder`, `VorbisDecoder`, `WavAiffReader` and `FfmpegDecoder`.
- Order: the T1 decoder by magic, else FFmpeg when loaded, else "This format needs the FFmpeg libraries (libavcodec61)".
- Tags and cover art follow the same order (mpg123 ID3, Vorbis comments, FLAC metadata, else libavformat).
- Pros:
  - the main path (MP3/FLAC/Opus/Vorbis/WAV) works on every board with tiny dependencies;
  - AAC and the rest work wherever FFmpeg is present (installed by default through Recommends);
  - RSS stays low until a T2 file is opened (dlopen on first need).
- Cons:
  - two decode paths to test: fakes plus an ABI check for each table, as Cassette does;
  - a T2 file costs the FFmpeg RSS once loaded (not unloaded).

**(C) Wrap an existing engine** (mpv/libmpv, GStreamer, cmus, MOC)

- Pros: a mature engine with gapless, ReplayGain, EQ (mpv `af=`), streams and MPRIS (mpv-mpris).
- Cons:
  - libmpv drags FFmpeg plus libplacebo, Lua and more (bigger than A);
  - GStreamer needs plugins-good/bad/libav, about 50+ MB, plus the GLib main loop in the app;
  - cmus and MOC are terminal programs (driving them via `cmus-remote` is fragile);
  - our Bluetooth guarantees (DONT_MOVE, identity guard, profile awareness, pause on disconnect) live in Cassette's libpulse layer and would have to be re-done through the engine's options;
  - licensing: mpv is GPL/LGPL depending on the build.
- Not recommended.

**(D) mpd as a background daemon, the app as a client**

- Pros:
  - background playback independent of the app (survives app close and the launcher's memory rule);
  - a mature database, gapless, ReplayGain, crossfade, streams, MPRIS via mpDris2;
  - phone remotes (MPDroid-type clients).
- Cons:
  - **a system service per user**: install, config file, port 6600, `mpd.service`/socket activation, which is against "one app = one deb, no system set-up" [I];
  - mpd's own output config duplicates our output chooser (`audio_output` blocks), and Bluetooth sinks come and go;
  - its database holds tag data for the whole library in RAM [U; unreliable forum figures, not quoted];
  - Debian's `mpd` package pulls FFmpeg, libmpdclient and more (as A, or more);
  - the app becomes a protocol client (libmpdclient, BSD) and loses control of the DONT_MOVE and identity guards;
  - two processes to debug on the owner's boards.
- Not recommended for a hobby app. Keep it as the answer if "keep playing after closing the app" becomes a hard requirement on boards without multitasking (D7).

### 2.2 Trade-offs at a glance

| Criterion | A FFmpeg | B small | **B+** | C mpv/GStreamer | D mpd |
|---|---|---|---|---|---|
| Formats | all | MP3/FLAC/Opus/Vorbis/WAV | all where FFmpeg is installed; T1 everywhere | all | all |
| Lite install | +100-200 MB [I] | +0.7 MB | +0.7 MB (+FFmpeg if Recommends taken) | +150 MB or more [I] | +FFmpeg and mpd |
| RSS on Zero 2 W | +8-20 MB [U] | lowest | lowest until a T2 file | high [I] | daemon + client |
| Start time | 0.1-0.5 s dlopen [U] | fast | fast | slower [I] | daemon already up |
| Background while other apps run | `X-Background=run` | same | same | same | daemon: yes, even after app exit |
| Gapless / crossfade | yes / own mixer | yes / own mixer | yes / own mixer | yes / mpv no crossfade [I] | yes / yes |
| VBR MP3 seek | TOC (approximate) unless scanned | mpg123 frame index (accurate) | accurate for MP3 | engine's | engine's |
| HTTP radio, ICY | built in (avio) | own: libcurl + mpg123 feed API | FFmpeg if present, else own | built in | built in |
| Reuse of Cassette's BT guards | yes | yes | yes | partial | no |
| Own code | least | medium (no AAC) | medium | glue, but foreign threading | client UI only |

### 2.3 Recommendation

**B+.**
- T1 decoders as `Depends`: `libmpg123-0t64, libvorbisfile3, libflac14 | libflac12t64 | libflac12, libopus0`.
- FFmpeg as `Recommends: libavformat61 | libavformat62, libavcodec61 | libavcodec62, libswresample5 | libswresample6, libavutil59 | libavutil60`. Apt installs Recommends by default [F APP-JSON.md "apt installs the package by default but the app must work without it"; whether Settings > Apps' installer passes `--no-install-recommends` is [U], P0-a].
- Everything is dlopened, as Cassette does, so the binary has no new `NEEDED` entry.
- Not in the first build:
  - libsoxr (resampling stays the server's or ALSA plug's job);
  - taglib (mpg123, libFLAC, Cassette and libavformat cover tags);
  - libopusfile (Cassette's reader is enough).

---

## 3. Output path

### 3.1 Route per board class

| Board class | Route | Reuse |
|---|---|---|
| Sound server present (HackBerry, RPI52, Pi 5 .175 [I], tablet PulseAudio) | **libpulse** stream to an **explicit sink** (`bt:<MAC>` or a named card) or the **default sink** in "Automatic" mode | Cassette `src/core/pulse/*`, `PulsePlaybackSink`, `pulse_worker`, `bt_output`, `launcher_facts` |
| No sound server (deck, zero8, Pi 3A+, Pi 4B [I]) | **ALSA `default`** (dmix shared with the launcher's beeps [I]); the card list for USB DAC choice; no Bluetooth | Cassette `AlsaPlaybackSink` (today `hw:` becomes `plughw:` [F `sink.cpp:30`]), `choose_output()` |

Direct `hw:` (exclusive, bit-perfect) is not in the MVP:
- it blocks every other sound;
- ALSA's plug converter falls back to linear interpolation when the card lacks the rate [I];
- a "Direct to card" option is a later item (D13).

### 3.2 Wired outputs per board [058, memory; I where marked]

| Output | Boards | Notes |
|---|---|---|
| 3.5 mm jack (bcm2835 headphones, PWM) | Pi 3A+ [F 058 `aplay -l`], Pi 4B [I] | fixed-rate firmware path, audible noise floor [I]; volume via amixer |
| HDMI | Pi 4B, Pi 5s (RPI52 has only an HDMI card: [F memory]) | only with a monitor attached; the launcher leaves HDMI to "default output" [F Cassette output.hpp] |
| USB DAC / headset | every board (deck PCM2902, zero8 UC02, Jabra) | 44.1/48 kHz only on cheap cards [F memory: UC02 48 kHz only]; rate switching: the server resamples; on ALSA `default`, dmix runs at 48 kHz [I] |
| I2S HAT | none today | `default` card via device tree; no own code |
| Tablet internal speakers / jack | FZ-M1 (HDA Intel) [I] | PulseAudio |
| Case speaker of the HackBerry | **Bluetooth** (XWF-M18) [F 058] | so the HackBerry has no wired speaker unless USB |

### 3.3 Bluetooth A2DP

**Codecs in the stack:**
- Debian trixie `libspa-0.2-bluetooth` 1.4.2-1 depends on `libsbc1`, `libfreeaptx0`, `libldacbt-enc2`, `libldacbt-abr2`, `liblc3-1` and `libopus0`, **not** `libfdk-aac` [F packages.debian.org].
- So the stack offers SBC, SBC-XQ, aptX/aptX HD (and LL), LDAC, FastStream, LC3 (LE Audio only, no BT 5.2 radio here), and PipeWire's own Opus 05 codecs. **No AAC** [I from the dependencies].
- The HackBerry showed `a2dp-sink` (SBC) and `a2dp-sink-sbc_xq` for the Evolve 65 [F 058 P6].
- WirePlumber picks the codec by default: "Enabled A2DP codecs (default: all)" [F WirePlumber bluetooth.conf]. The player does not choose a codec (D5).
- Ubuntu 26.04 (tablet) has PipeWire 1.6.2 packages [F], but the tablet runs PulseAudio [058].

**Bitrates and latency [web, varying by device]:**

| Codec | Bitrate | Latency measured |
|---|---|---|
| SBC | 328 kbps | 173 ms (one headset, rtings); 275 ms (PipeWire user table) |
| SBC-XQ | bitpool 94 = 551 kbps (PipeWire README) | 286 ms |
| aptX | 384 kbps | 166 ms |
| aptX HD | 576 kbps | - |
| aptX LL | - | 34 ms |
| LDAC | 330/660/990 kbps | - |
| A2DP generally | - | 100-200 ms (Collabora, Unity, in 058); ~300 ms on phones with SBC (SoundGuys) |

- For music playback latency matters only for the counter. Cassette already rewinds the position by `delay()`, which includes the device latency [F PULSE.md], and the player reuses that.

**Device switching on the fly:**
- The player opens a new stream on the chosen sink and closes the old one: a gap of 0.1-0.5 s while an A2DP sink wakes from suspend [I].
- Streams carry `DONT_MOVE`, so the server never moves them silently [F Cassette].

**Headset connects or disconnects mid-track (phone behaviour):**
- **Disconnect of the playing sink**: the stream ends (`Killed`), and Cassette's Player already pauses "at the frame heard" with a notice [F PULSE.md, "Disconnect during playback"]. Keep that, so the music never falls back to the speaker.
- **Connect**: no automatic resume. In "Automatic" mode, the next PLAY or the next track opens on the new default sink (D8).
- Wired: a USB DAC unplug ends the ALSA stream (`-ENODEV`) and pauses. The Pi jack has no jack-detect [I].

**One A2DP device at a time** [F 058 P6]:
- A second Bluetooth audio device gets call profiles only.
- Show "call quality" in the output chooser (Cassette `music_available` / `hint_profile` [F]; launcher `bt_audio_dev=...:hfp` [F contract 5b]).

**Call mode (HFP) interruption:**
- The board is not a phone, so HFP opens only when an app records from the headset (Cassette).
- If that happens while the player plays to the same headset, the sink drops to 16 kHz mono [F 058].
- Recommendation: show "call quality" and keep playing (D19 is folded into D8). No forced A2DP.

### 3.4 Volume

**[F]** The launcher's volume keys act on `@DEFAULT_SINK@` via `pactl`, else `amixer`, and "an app must not act on them too" [contract 14].

| Output | What the volume keys do |
|---|---|
| Default sink (Automatic) | change exactly what plays |
| An explicit sink that is not the default (for example a BT headset chosen while the default is HDMI) | **change the other device** [I, a real trap] |
| ALSA `default` on a USB DAC without a mixer | nothing (`sound=0` or no volume) |

Recommendation (D11):
- Play to the default sink unless the user picks another.
- When the chosen sink is not the default, offer an in-app stream volume (`pa_context_set_sink_input_volume`), or ask the server to make the chosen sink the default. The second is a policy change and needs owner design.
- In-app software volume (Cassette `dsp/gain`) only for outputs without a mixer.
- Show the level from `status.state` `volume`/`mute`, because the launcher draws no OSD over fullscreen apps [F contract 5b and 14].
- **AVRCP absolute volume:** with PipeWire's `bluez5.hw-volume` default including `a2dp_source` [F WirePlumber docs], headset volume buttons change the sink volume directly. That is consistent with the launcher keys on the default sink.

### 3.5 Sample rate, bit depth, resampler, bit-perfect

- Open the stream at the **track's rate and channels**, and let the server resample (Cassette rule [F]).
- **24-bit:** Cassette's pulse path is S16 only ("24-bit takes are converted by the Player") [F PULSE.md]. The player needs S32 or F32 streams. libpulse supports `s32le`/`float32le` (the formats are already in `pulse_stream.cpp` [F]). Small change.
- PipeWire resampler quality: its default [I]. The graph rate can follow the stream (44.1 kHz) when `default.clock.allowed-rates` lists it [I]. That is a system setting, not the app's.
- **Bit-perfect: no** (D13). It needs exclusive `hw:` or server tuning, volume at 100%, and no beeps or other streams. Not worth it on this hardware.
- **Rate change between tracks:** gapless needs the stream format kept.
  - Same format: continue the same stream.
  - Different rate: drain and reopen (a gap of 50-150 ms [I]), and crossfade only between equal formats.
  - Optional later: `PA_STREAM_VARIABLE_RATE` + `pa_stream_update_sample_rate` (pipewire-pulse support [U]).

### 3.6 AVRCP media keys and metadata push

- **[F]** BlueZ creates a `<device> (AVRCP)` input node on our boards (`XWF-M18-M28-M38 (AVRCP)` in the cp0_lvgl presence fixture). Headset buttons arrive there as `KEY_PLAYCD`/`KEY_PAUSECD`/`KEY_PLAYPAUSE`/`KEY_NEXTSONG`/`KEY_PREVIOUSSONG` [I] **when no AVRCP player is registered**.
- [I] BlueZ hands passthrough commands to a registered player instead of uinput. PipeWire's `bluez5.dummy-avrcp-player` "Default: false" [F WirePlumber example config] would swallow them if enabled. P0-c checks this on the HackBerry.
- **Metadata to the headset or car display:** register an MPRIS object with `org.bluez.Media1.RegisterPlayer`. The object "must implement at least org.mpris.MediaPlayer2.Player as defined in MPRIS 2.2" [F BlueZ doc].
  - Reuse the sd-bus dlopen layer of Mesh Hop or mmWave Radar (libsystemd0, already a pattern [F]).
  - Once registered, play/pause come as MPRIS method calls, not keys.
  - Effort M, tier P2 (D10).

---

## 4. Feature inventory (industry standard)

**"Who has it"** is from general knowledge of the products [I, not re-verified on their sites; Musicolet's list was read: krosbits.in/musicolet, 2026-11-12]. Abbreviations: PA Poweramp, VLC, fb2k foobar2000, MC Musicolet, WA Winamp, cmus, ncm ncmpcpp (mpd), RB Rockbox on iPod.

**Effort:** S = up to half a worker-day, M = 0.5-1.5 days, L = 2+ days.

**Tiers:** MVP = P1, 1.0 = P2, later = P3+, no.

| # | Feature | Who has it | Effort | Dependency | Tier |
|---|---|---|---|---|---|
| 1 | File and folder browser mode (no library), `~/Music` + mounted media | VLC, fb2k, MC, cmus, RB, PA | S-M | none | **MVP** |
| 2 | Library scan and database: artist / album artist / album / genre / year / composer views | PA, VLC, fb2k, MC, WA, cmus, ncm, RB | L | SQLite (dlopen, `libsqlite3-0`) | 1.0 |
| 3 | Incremental rescan (mtime/size), background, idle I/O priority | PA, fb2k, MC, mpd, RB | M | SQLite | 1.0 |
| 4 | Search (title/artist/album, prefix and substring) | all | S-M | SQLite `LIKE` (FTS5 later [I]) | 1.0 |
| 5 | Queue: play folder or album, next/previous, **play next, add to queue, reorder, remove** | all; MC multiple queues | M | none | MVP basic; 1.0 editing |
| 6 | Playlists: create, edit, import/export M3U/M3U8/PLS | all | M | none | 1.0 (MVP: open an M3U from the browser) |
| 7 | Shuffle (track; album later) and repeat (off/all/one) | all | S | none | **MVP** |
| 8 | Gapless | PA, fb2k, cmus, mpd, RB, WA (plugin) | M | decoder gapless info (1.1) | **MVP** for same-format tracks |
| 9 | Crossfade (fixed seconds; none inside albums) | PA, fb2k (plugin), WA, mpd, RB | M | own mixer | 1.0 |
| 10 | ReplayGain track/album, preamp, clip guard | PA, fb2k, cmus, mpd, RB, VLC | S | tags (1.1), Cassette limiter | 1.0 |
| 11 | Graphic EQ (10 bands, presets); parametric later | PA, VLC, WA, MC, RB | M (<1% CPU, 1.4) | own biquads | 1.0 |
| 12 | Bass boost / loudness | PA, RB | S | own | later |
| 13 | Sleep timer (minutes, end of track, end of queue) | PA, MC, VLC Android, RB | S | none | 1.0 |
| 14 | Resume: last track and position at start (MVP); per-file position for long files and audiobooks (1.0) | PA, VLC, RB (bookmarks), MC [U] | S | settings / DB | MVP / 1.0 |
| 15 | Speed control with pitch kept | PA, VLC, RB | M | time-stretch: libavfilter `atempo` or own WSOLA | later |
| 16 | A-B loop | VLC, RB, PA [U] | S | none | later |
| 17 | CUE sheets (image + cue, virtual tracks) | fb2k, PA, RB, VLC, cmus | M | own parser | later (1.0 if the owner uses image rips) |
| 18 | Album art on now playing; thumbnails in lists; disk cache | all GUI players | S (now playing) / M (cache, lists) | stb_image (built in) or libturbojpeg (1.5) | MVP now playing; 1.0 lists |
| 19 | Lyrics: embedded USLT/LYRICS, `.lrc` synced display | PA, MC, fb2k (plugin), RB | M | none | later |
| 20 | Tag editing | PA, MC, fb2k | L (risk of corrupting files) | writer per format | **no** (D14) |
| 21 | Ratings, play counts, last played | PA, fb2k, MC, RB | S on the DB | SQLite | 1.0 play counts; ratings later |
| 22 | Smart / automatic playlists (most played, recently added) | fb2k, PA, MC | M | SQLite | later |
| 23 | Scrobbling (ListenBrainz token; Last.fm needs an API key) | PA (via app), fb2k, cmus, mpdscribble, RB | M | libcurl (dlopen, as Home Assistant) | later |
| 24 | Podcasts / audiobooks: M4B chapters, resume per book | VLC, PA (partial), RB | M | libavformat chapters | later |
| 25 | Internet radio: HTTP/HTTPS, ICY title, M3U/PLS station files | VLC, WA, fb2k, mpd | M | FFmpeg avio, else libcurl + mpg123 feed | later (P3) |
| 26 | Background playback while other apps run | all | S (`X-Background=run`) | launcher multitasking | **MVP** |
| 27 | Media keys: keyboard and headset (AVRCP) buttons | all | S-M | own evdev reader (3.6, 6.4) | **MVP** |
| 28 | Now playing on the lock screen and status bar | PA, MC (Android) | L, needs a launcher capability (6.3) | launcher | later |
| 29 | AVRCP metadata to the headset or car; MPRIS | phones; Linux players via MPRIS | M | sd-bus (dlopen) | 1.0 |
| 30 | Visualiser: VU/peak (Cassette meter) / spectrum (FFT) | WA, fb2k, PA, ncm, RB | S / M | own / kissfft (BSD) | VU MVP-optional; spectrum later |
| 31 | USB stick: detect mount, browse, scan | RB (SD), PA | M | `/proc/mounts` watch; automount exists only on desktop images [I] | 1.0 |
| 32 | Multi-language tags (UTF-8, ID3 UTF-16/Latin-1) with a CJK font fallback | all | S | NotoSansCJK / PuHuiTi from the launcher (`settings_fonts::cjk_sans()` [F]) | **MVP** |
| 33 | Large libraries: 10k tested (1.0), 100k (later) | fb2k, mpd, RB | M | SQLite, paged lists | 1.0 / later |
| 34 | Output chooser: wired / Bluetooth / default, with quality hint | PA, VLC, fb2k | S (reuse) | Cassette | **MVP** |
| 35 | Software volume / preamp | PA, VLC, fb2k | S | Cassette `dsp/gain` | MVP only for outputs without a mixer |
| 36 | Hi-res / 24-bit kept; bit-perfect | PA, fb2k (exclusive), RB | S / L | S32/F32 stream | 1.0 / **no** |

**Scan time and RAM for large libraries on the Zero 2 W [I, measure in P0-f]:**
- First tag scan: open plus about 64 kB read and parse per file, 10-30 ms per file on SD. So 10k files take **3-10 min**, and 100k files take **30-100 min**.
- The scan must run in the background, resume after interruption, and use `ioprio` idle with `nice` +10.
- Incremental rescan (stat only): **5-20 s per 10k files**.
- Cover art is extracted only on demand (not during the scan) and cached as thumbnails.
- RAM stays under 10 MB for the database because lists page through queries.

---

## 5. What Cassette gives for free, and code sharing

### 5.1 Reuse map (apps `origin/main` d1cf53f; line counts by `wc -l`)

| Cassette module | Lines | For the player | Change needed |
|---|---|---|---|
| `src/core/pulse/` (pulse_abi, pulse_lib, pulse_api, pulse_stream, real_pulse, pulse_probe, bt_output, launcher_facts) | about 3,400 | **as is** | add S32/F32 playback formats; capture parts unused |
| `src/core/app/pulse_worker.*` | 240 | as is | |
| `src/core/playback/sink.*`, `pulse_sink.*`, `output.*` | about 630 | adapt | keep the stream open between tracks (no drain), format switch |
| `src/core/playback/player.*` | 846 | partial | thread model, position-minus-delay, pause by drop; drop the 5x scan and take specifics |
| `src/core/codec/` (codec_abi, codec_libs decoders, decoder.hpp, decoded_source, flac_decoder, opus_decoder, ogg_opus reader) | about 2,300 | as is / adapt | Opus: always output 48 kHz for music; add MP3 / Vorbis / FFmpeg tables and decoders on the same seam |
| `src/core/alsa/` playback parts (alsa_api, real_alsa, card) | about 600 of 1,400 | as is | `default` (dmix) option besides `plughw:` |
| `src/core/dsp/` meter, gain, limiter | about 500 | as is | ReplayGain clip guard = the limiter |
| `fileops`, `ring`, `takes/json`, `takes/atomic_file` | about 800 | as is | settings and state files |
| UI: `ui/meter.cpp` (LED meter), `fit.cpp` (size classes), `widgets.cpp` (Cassette style) | about 880 | **design decision** (section 10) | |
| Tests: fake_alsa, fake_pulse, fake_sink, fake_decoders, codec and pulse ABI checks, `check.hpp` | | as is | |
| **Not reusable:** capture, writer, journal, repair, takes naming, trim/split/merge, deck (tape state machine), countdown, monitor, recorder pages, handwriting, load animation | about 21,000 | - | |

**Totals:**
- About 9,000-10,000 of Cassette's 31,510 source lines are reusable, which is **about 30% of Cassette**.
- [I] For a player of 20-25k lines, that covers **35-40% of the engine and back ends** (output, Bluetooth, FLAC/Opus/WAV, meters, settings), and **0%** of the UI, library, queue, playlists, MP3/Vorbis/FFmpeg decoders, tags and cover art.

### 5.2 Copy, or extract a shared `audio` folder

**[F] Packaging facts:**
- One app = one deb, and packages do not depend on each other (no cross-app dependency rule exists, and every app ships its own binary [APP-GUIDELINES section 5]).
- Shared code is **source**: `apps/_common/*.cpp` are copied into **every** app's build (`build_app.sh:185`: `cp "$COMMON"/*.hpp "$COMMON"/*.cpp`).
- So audio code put in `_common/` would be compiled into every app.

**Options:**
- (a) **Copy now:**
  - Copy the modules into `apps/<player>/src/core/` with a header line naming the origin (Cassette at d1cf53f).
  - Pro: zero risk to the published Cassette.
  - Con: fixes must be made twice.
- (b) **Extract `apps/_audio/` now:**
  - The opt-in folder is copied only for apps that ask for it (a small `build_app.sh` change, like `cp0_config.mk`).
  - Pro: one copy.
  - Con: a refactor of a published app before the player exists, plus re-testing Cassette (9 suites and Bluetooth board tests).

**Recommendation (D6): (a) in P1/P2, then (b) in P3**, in one PR that moves both apps once the player's needs (F32, gapless stream) are known, with both apps' suites green.

---

## 6. Launcher integration

**Rule:** generic capabilities only, never app-specific code in the launcher [F DEV-GUIDE:13-14, decisions.md:126].

### 6.1 What exists and the player can use now [F APP-CONTRACT]

| Capability | Use in the player |
|---|---|
| `X-Background=run` (section 13) | keep decoding and playing while hidden. `cdk::hidden()` skips UI work. Answer `hide` within 300 ms. Save the queue and position as you go: the launcher may `close` a hidden app, and kills `run` apps below 32 MB MemAvailable. `oom_score_adj` is 700 while hidden |
| The switcher shows the app's last frame | the now-playing screen is visible in "Open apps" without any launcher change [I] |
| `status.state` `volume`, `mute`, `bt_audio_dev` (with `a2dp`/`hfp`), `bt` | level display; Bluetooth quality hint; paired-but-off devices dimmed (Cassette T8 code) |
| `board.state` `sound`, `sound_kind`, `bt_audio`, `bt_hfp`, `ram_mb`, `arch` | "Bluetooth audio is not set up on this board"; memory class (cache sizes, scan batch) |
| `launcher.sock` `volume get/set/up/down/mute`, `dim allow/deny`, `toast` | `dim allow` while playing on battery boards; `volume` only behind an explicit in-app system-volume control (contract: "apps that do not play media have no business here", so a media player may) |
| Volume keys inside apps | the launcher's job; the app must not act on them (3.4) |
| Media OSD (`launcher_media_osd.cpp`: `show_level`, `show_mute`) | **launcher pages only**; nothing is drawn over a fullscreen app, so the player draws its own level from `status.state` |
| Wake from suspend | the launcher repaints itself, but "an external app that owns the panel is not repainted after a suspend" [F ROADMAP v0.9.1]. The player should pause on suspend and repaint on wake (a jump of `CLOCK_BOOTTIME` minus `CLOCK_MONOTONIC`, the launcher's own test) |

### 6.2 Gaps found

| # | Gap | Evidence | Effect on the player |
|---|---|---|---|
| G1 | Transport keys (Play/Pause/Next/Prev/Stop) reach no app | media-key devices pass only the five volume, mute and brightness keys [F contract 14, `cp0_keyboard_presence.h`]; apps do not set `read_media_keys` [F `_common/platform.cpp:88`]; keyboards are grabbed by the launcher while it is in front with hidden apps [F contract 13]; a hidden app's keyboards are paused [F] | no headset or keyboard media control unless the player reads the devices itself |
| G2 | No screen-off for the app in front on mains-powered boards | `dim` only on battery boards with levels, and only to 10% [F contract 14]; "today the panel never dims while an app is in front" | the panel stays lit while music plays in front (power, OLED wear) |
| G3 | No "now playing" in the launcher's status strip or lock screen | no such key or verb | the user sees playback state only in the switcher card |
| G4 | No multitasking below 480 px on the short side | contract 13 | zero8 class: no background playback |
| G5 | Volume keys follow the default sink only | contract 14 | trap with an explicit non-default sink (3.4) |

### 6.3 Generic launcher proposals (for the coordinator; none needed for the MVP)

| # | Proposal (generic) | Size | Tier |
|---|---|---|---|
| L1 | **Media transport forwarding:** an app with `.desktop` `X-MediaKeys=true` (or a `launcher.sock` `media claim`) receives `KEY_PLAYPAUSE/NEXTSONG/PREVIOUSSONG/STOPCD/PLAYCD/PAUSECD` from every keyboard and media-key device, also while hidden, over its lifecycle socket (`key <code> <value>`). The last claimer wins | M | later (the MVP uses its own evdev reader) |
| L2 | `launcher.sock` `screen off\|on` for the app in front (backlight to 0, any key or touch wakes; ends when the launcher takes the panel), on any board with a backlight | S-M | later |
| L3 | `launcher.sock` `media state playing\|paused\|stopped` + `media title <text>`: the launcher shows a generic glyph in its own status strip and the text on its lock screen, for any app | M-L | later |
| L4 | cp0_lvgl option: pass transport keys to the app's own key handler (a toolkit change, rebuild and re-pin of the app) | S | alternative to the own evdev reader |

### 6.4 MVP choice without launcher changes

- The player opens the media-key devices itself: `cp0_kbd_presence_parse_media()` gives the list [F header], or it scans `/proc/bus/input/devices` for nodes with `KEY_PLAYPAUSE`.
- It reads them read-only, never grabbed, in a thread that runs also while hidden.
- There is no double action:
  - the launcher ignores transport keys from those devices [F];
  - the app's keyboard backend does not read them (they are not keyboards) [F].
- Not covered: a Play key on a keyboard's **main** node while the player is hidden (the launcher grabs keyboards then). Accepted until L1.

### 6.5 Resources, priority, blanking, suspend

**Priority:**
- No real-time scheduling (needs RTKit or root).
- Decode-ahead ring of 2-4 s.
- Server target `tlength` of about 100-250 ms for music (Cassette uses 60 ms for scan responsiveness).
- Library scanner at `nice` +10 with `ioprio_set(IOPRIO_CLASS_IDLE)`, which is allowed for non-root [I].
- No governor change (Pi default).

**Blanking:**
- In front: send `dim allow` on battery boards and `dim deny` when paused for a long time [I].
- Hidden: the launcher's own screensaver and lock (black after `dark_time`, default 30 s in config [F `ui_screensaver.cpp`]) blanks the panel while music plays. That is phone-like on boards with multitasking.

**Suspend (tablet power key):**
- Playback stops with the device.
- On wake: stay paused, repaint.

---

## 7. Boards matrix

| Board | CPU / RAM | Screen | Sound stack | Wired out | BT for audio | Background (multitasking) | MVP must guarantee | Limits |
|---|---|---|---|---|---|---|---|---|
| **Deck** zero7.local (Pi Zero 2 W) | A53 1 GHz x4 / 512 MB | 640x480 | none [058 I] | USB card / headset | no (no server) | yes (640x480) | T1 formats; USB output; background; folder browsing; resume | RAM (25 MB RSS budget); SD scan speed; FFmpeg install size; launcher 0.9.4 there |
| **zero8** (Pi Zero 2 W) | same | 480x320 panel, PDA 320x480 | none [F 058] | USB (UC02, 48 kHz only) | no | **no** (short side 320) | the same in foreground; portrait layout | no background; small screen |
| **Pi 3A+** | A53 1.4 GHz / 512 MB | 480x800 portrait DSI (PDA) | none [F 058] | **3.5 mm jack** (bcm2835), USB, HDMI | no (BT 5.0 radio present) | yes (short side 480) | jack output via ALSA `default`; portrait | RAM; PWM jack quality |
| **Pi 4B** | A72 / RAM [U] | 800x480 | [U] (Lite assumed) | jack, HDMI, USB | only with a server | yes | as Pi 3A+ | [U] stack |
| **Pi 5 RPI53** (HyperPixel 4) | A76 2.4 GHz / [U] | 480x800 portrait | PipeWire likely [058 U] | HDMI (when attached), USB | yes [I] | yes | BT and USB; portrait | no jack |
| **Pi 5 RPI52** | A76 / 4 GB | 800x480 DSI | PipeWire + WirePlumber [F memory] | **HDMI only**, no USB card | yes (Cypress BT 5.0) | yes | BT output; T2 via FFmpeg [U present] | needs a USB DAC or BT to be heard |
| **HackBerry** CM5 | A76 / about 2 GB [I] | 720x720 square | PipeWire 1.4.2 + WP 0.5.8 + libspa-bluetooth [F 058] | USB; the case speaker is **BT** | yes; **one A2DP at a time** [F] | yes (2-touch: Alt+Tab) | BT speaker or headset; square layout; AVRCP keys [U P0-c] | Bluetooth limits of 058 |
| **FZ-M1 tablet** (amd64) | Core m5-6Y57 / 3.8 GB | 1280x800 (PDA 800x1280) | PulseAudio [058] | HDA speakers and jack [I] | via PulseAudio's module [U] | yes | amd64 package; FFmpeg 62 SONAMEs; large layout | Ubuntu names; PulseAudio, not PipeWire |

**MVP guarantee (all boards):**
- MP3, FLAC, Opus, Vorbis and WAV play on the wired route the board has.
- Background where multitasking exists.
- T2 formats and Bluetooth where the system provides them, with a clear notice where it does not:
  - "Bluetooth audio is not set up on this board";
  - "This format needs the FFmpeg libraries".

---

## 8. Risks, open questions, licences and patents

### 8.1 Risks

| Risk | Likelihood / impact | Mitigation |
|---|---|---|
| FFmpeg RSS breaks the 25 MB guideline on the Zero 2 W | medium [U] / guideline breach, OOM pressure with other apps | load only for T2 files; P0-b measures; on `ram_mb` ≤ 512 show the RSS cost in the README |
| Cover art decode spikes (27-36 MB for 3000 px) | high on real libraries [I] / OOM or stutter | cap the dimensions; scaled JPEG decode (libturbojpeg or TJPGD); disk thumbnails; decode on a worker |
| First scan of a big library on SD takes tens of minutes | certain for 100k [I] | folder-first MVP; background, resumable, idle-priority scan; progress shown |
| Headset buttons do not reach uinput (a registered dummy AVRCP player) | low with WirePlumber defaults [I] / no headset control | P0-c; else MPRIS registration (D10) |
| Volume keys change another device than the one playing | certain with an explicit non-default sink [F/I] | D11 |
| The player is closed while hidden (memory rule, OOM) | medium on 512 MB boards / music stops | save the queue and position as you go; resume on the next start |
| Stream moved silently to another sink | certain without DONT_MOVE [F 058 P5] | Cassette's DONT_MOVE and identity guard (reuse) |
| One A2DP device at a time | certain on the HackBerry [F] | quality hint; no forcing |
| Apt pulls FFmpeg on the Lite boards through Recommends (100-200 MB, minutes of install) | certain if the installer keeps Recommends [U] | D3; README says how to skip it; the app degrades |
| Pi archive's FFmpeg (epoch 8, rpt) differs from Debian's | low [I] | dlopen by SONAME only; the ABI check covers the struct fields we use |
| VBR MP3 duration and seek accuracy | medium for FFmpeg / low for mpg123 | MP3 via mpg123 (frame index, `mpg123_scan` on demand) |
| APE "extra high" too slow on the Zero 2 W | low (rare files) | underrun counter, notice |
| Name "Music" moves the tile into the built-in block | certain [F 046] | D1 |
| Boards were unreachable for this study | - | P0 before P1 |

### 8.2 Licence and patent table

| Component | Licence | How used | Patent note |
|---|---|---|---|
| Player code | MIT (as Cassette) | ours | - |
| libmpg123 | LGPL-2.1 | dlopen, Depends | MP3: US patents expired April 16, 2017 (last one US 6,009,399; Fraunhofer/Technicolor licensing ended April 23, 2017); EU 2012 [web, secondary sources] |
| libvorbis / libvorbisfile / libogg | BSD-3-Clause | dlopen, Depends | royalty-free design |
| libFLAC, libopus | BSD-3-Clause | dlopen, Depends (already Cassette's) | royalty-free |
| FFmpeg libav* (Debian/RPi build) | **GPL-2.0-or-later** as built (`--enable-gpl`; `--enable-version3` only in the "extra" flavour; no `--enable-nonfree`, no fdk-aac) [F debian/rules 7.1.5] | dlopen, Recommends | AAC: the Via LA pool still licenses AAC including AAC-LC [F via-la.com]; "last baseline AAC patent 2028" is a secondhand claim [U]. We ship no AAC code: Debian main ships FFmpeg's native decoder. WMA: reverse-engineered decoders [I], patent status [U] |
| libfaad2 | GPL-2.0-or-later [I] | not used | AAC as above |
| fdk-aac (`libfdk-aac2t64`) | FDK licence, **non-free** in Debian [F] | not used | |
| libopenmpt / libgme | BSD-3 / LGPL-2.1+ | through libavformat | - |
| libsqlite3 | public domain | dlopen (P2), Depends | - |
| libpulse, libasound | LGPL-2.1+ | dlopen (Cassette pattern) | - |
| libsystemd (sd-bus) | LGPL-2.1+ | dlopen (P2, MPRIS/AVRCP) | - |
| libcurl | curl licence | dlopen (P3 radio, scrobbling) | - |
| stb_image 2.30 | public domain or MIT | built in (as RSS Reader) | - |
| kissfft (spectrum, later) | BSD-3-Clause | built in | - |
| miniaudio / dr_libs | Unlicense or MIT-0 | **not needed**: the launcher has miniaudio 0.11.25 [F], but apps do not compile the audio service [F `cross_config_defaults.mk`] | - |

All of them are named in the package `copyright` file and the README credits table (repo rule).

### 8.3 Open questions (for the coordinator or the owner)

1. Does Settings > Apps install `Recommends`? APP-JSON.md says apt does by default; the installer code was not traced [U].
2. Is FFmpeg present on the Pi OS desktop images (HackBerry, RPI52) and on the tablet [U]?
3. Which boards does the owner want for the first release? (Cassette started with the HackBerry for Bluetooth and the deck for USB.)
4. Does the owner have AAC/M4A, ALAC, CUE-image or very large libraries? That changes the T2 and CUE priorities.

---

## 9. Phased plan, tests and decisions

Style: the Controller checks code and deploys over ssh; the owner runs short physical tests from a protocol; no deck-verifier by default.

### P0: probes (controller 0.5 day; owner about 45 min)

| # | Probe | Board | Needs |
|---|---|---|---|
| P0-a | `dpkg -l \| grep -E 'libav(codec\|format)\|mpg123\|vorbisfile'`; `apt-get -s install libavformat61 libavcodec61 libswresample5 libmpg123-0t64 libvorbisfile3` (simulation; read the package count and the sizes) | zero8, Pi 3A+, HackBerry, RPI52, tablet | read-only |
| P0-b | Decode CPU and RSS: `/usr/bin/time -v ffmpeg -hide_banner -i <file> -f null -` and `mpg123 -t <file>` for MP3 320k, AAC 256k, FLAC 16/44 and 24/96, Opus 128k, Vorbis q6, ALAC, APE -c2000; record %CPU, max RSS and `speed=` | HackBerry (already has FFmpeg? [U]) and **zero8** | owner's word to **install `mpg123` and `ffmpeg` temporarily on zero8** (Cassette P0-e precedent), plus a rollback list; a 10-file test set the owner copies |
| P0-c | AVRCP: with the Evolve 65 (and the XWF speaker) connected, read the `(AVRCP)` node with a small evdev dump (python3 `struct` read of `/dev/input/eventN`, no install); press play, pause, next, previous, volume; also `pactl subscribe` for absolute-volume changes | HackBerry | owner present (headset buttons) |
| P0-d | A2DP codecs offered: `pactl list cards` (profiles `a2dp-sink-*`) and `ls /usr/lib/aarch64-linux-gnu/spa-0.2/bluez5/` | HackBerry, RPI52 | read-only |
| P0-e | Cover decode: time and peak RSS of stb_image on 600/1000/3000 px JPEGs (a small arm64 test binary copied to /tmp, removed after) | zero8 | owner's word for the test binary |
| P0-f | Scan cost: `time find ~/Music -type f \| wc -l` (cold cache after a reboot) and a tag-read pass over N files with the P0-e tool | zero8 with an owner-provided folder | test folder |

### P1: MVP (about 10-14 worker-days)

Scope:
- Folder browser (`~/Music` + mounts) with queue = folder or M3U.
- Now playing: title/artist/album, embedded cover art, position, seek, previous/next, shuffle and repeat.
- T1 decoders, plus T2 via FFmpeg when present.
- Gapless for same-format tracks.
- Output chooser: wired via ALSA `default`, server sinks, Bluetooth with the quality hint, pause on disconnect.
- Software volume only without a mixer.
- Resume last track and position.
- `X-Background=run`.
- Media keys via the own evdev reader.
- CJK fallback font.
- Packaging for arm64 and amd64: Depends for T1, Recommends for FFmpeg, copyright and README credits.

Code: copy of the Cassette modules (D6); unit suites with fakes (decoders, pulse, ALSA), plus headless scenes at the sizes the coordinator designs.

**Board tests P1 (owner, about 30 min per board):**

| # | Test | Pass |
|---|---|---|
| B1 | Open the app, browse `~/Music`, play a folder of MP3s | sound on the default output; title and art shown |
| B2 | FLAC 24/96 and Opus and Vorbis files | play; time matches the file length |
| B3 | AAC/M4A file with FFmpeg present / absent | plays / clear notice, no crash |
| B4 | Gapless album (live album, MP3 LAME and FLAC) | no click or gap between tracks |
| B5 | Seek in a VBR MP3 to 75% | lands within 1 s of the target |
| B6 | Super (hide) while playing; open two other apps; come back | music continued; the screen shows the right track |
| B7 | Bluetooth headset chosen; switch the headset off mid-track | playback pauses; nothing comes out of the speaker; notice |
| B8 | Headset (and keyboard) play/pause/next buttons, in front and hidden | they act once |
| B9 | Volume keys while playing (default sink) | the level changes; the app shows it |
| B10 | Quit and restart | resumes on the same track and position (paused) |
| B11 | Lite board (zero8 or deck): USB card output | plays; no Bluetooth entries; the server notice shown once |
| B12 | Portrait (Pi 3A+ or zero8 PDA) and square (HackBerry) | layout per the coordinator's design; nothing off-screen |

### P2: 1.0 features (about 10-15 worker-days)

- SQLite library with scan, incremental rescan, views, search, play counts.
- Playlists: create/edit, M3U import/export; queue editing.
- ReplayGain, crossfade, sleep timer, per-file resume for long files, 10-band EQ.
- MPRIS + BlueZ `RegisterPlayer` (metadata to the headset).
- USB stick scan; art thumbnail cache.
- Board tests B13-B22 in the same style (scan 1k files timed on the Zero 2 W, search speed, crossfade, EQ CPU, sleep timer, headset display).

### P3: polish (about 6-10 worker-days)

- Visualiser: Cassette VU, then spectrum.
- `.lrc` lyrics, CUE sheets.
- Internet radio (FFmpeg avio or libcurl + ICY).
- Speed control, A-B loop.
- ListenBrainz (opt-in).
- Extract `apps/_audio` shared with Cassette (D6).
- Generic launcher proposals L1-L3 if the owner wants them (separate launcher tasks).

### Decisions for the owner (recommendation on each line)

1. **D1 Name and id:** owner's choice. Avoid the exact title "Music" (report 046 tile-order quirk) unless that is wanted.
2. **D2 Decode architecture:** B+ (small decoders + FFmpeg wide net). Rec: yes.
3. **D3 FFmpeg packaging:** `Recommends` (61|62 alternatives), loaded at run time, the app works without it. Rec: yes.
4. **D4 MVP scope:** folder-first; library database in P2. Rec: yes.
5. **D5 Output API:** libpulse when a server runs, ALSA `default` otherwise; the codec choice is left to WirePlumber; no direct `hw:` in the MVP. Rec: yes.
6. **D6 Code sharing:** copy Cassette's modules now; extract an opt-in `apps/_audio` in P3. Rec: yes.
7. **D7 Background:** `X-Background=run`; below 480 px short side the app owns the panel (no daemon). Rec: yes.
8. **D8 Device events:** pause when the playing output disconnects; never move to another sink; "Automatic" follows the default sink at PLAY or next track; keep playing at "call quality" with a hint. Rec: yes.
9. **D9 Media keys:** the player reads media-key devices itself in the MVP; launcher forwarding (L1) later. Rec: yes.
10. **D10 AVRCP metadata / MPRIS** via BlueZ `RegisterPlayer`: P2. Rec: yes.
11. **D11 Volume:** system volume (launcher keys) on the default sink; in-app stream volume when an explicit non-default sink plays; software volume only without a mixer. Rec: yes.
12. **D12 Library store:** SQLite (`libsqlite3-0`, dlopen); folders `~/Music` + chosen mounts; incremental, idle-priority scan. Rec: yes.
13. **D13 Hi-res:** keep 24-bit (S32/F32 streams), the server resamples, no bit-perfect mode. Rec: yes.
14. **D14 Out of scope:** MIDI, native DSD/DoP (DSF as PCM via FFmpeg only), SACD ISO, tag editing, video. Rec: yes.
15. **D15 Later tier:** internet radio, podcasts, scrobbling, speed, smart playlists (P3+). Rec: yes.
16. **D16 Screen while playing in front:** `dim allow` on battery boards now; a generic "screen off for the app in front" launcher verb (L2) later. Rec: yes.
17. **D17 Screen sizes:** the same classes as Cassette (480x320, 320x480, square, large) and no 320x170 on main; the coordinator's design call. Rec: yes.
18. **D18 Lite boards:** wired only until report 058's D2 (sound server on Lite boards) is decided. Rec: yes.
19. **D19 P0 permissions:** temporary `mpg123` + `ffmpeg` install on zero8 (rollback listed), a test binary in /tmp, and a test music folder from the owner. Rec: yes.

---

## 10. UI questions for the coordinator (no screens designed here)

1. **Screens needed:**
   - Now playing;
   - Browser (folders; later artists/albums/songs/genres/playlists);
   - Queue;
   - Search (P2);
   - Settings (output, ReplayGain, gapless/crossfade, EQ, sleep timer, library folders);
   - Output chooser;
   - EQ (P2);
   - Notices.

   Which are separate pages and which are overlays?
2. **Visual language:** Cassette's cream tape-deck style is the owner's favourite (memory). Should the player share Cassette's look (a "deck" family), use the plain launcher widgets (`Page`, `CompactList`, `Roller`), or have a skin of its own? The design is case by case per app (owner rule).
3. **What must fit 480x320 and 320x480** (the smallest canvases for fullscreen apps on main)?
   - Minimum now playing: art, title, artist, time bar, transport.
   - Is cover art hidden or shrunk at 320 px width?
   - Is 320x170 supported at all (only on the small-screen fork)?
4. **Portrait** (PDA 480x800, 320x480, 800x1280 on the tablet): stacked art over controls? Is the PDA writing-zone band relevant? (It appears only with text fields: search.)
5. **Square** (HackBerry 720x720) and **large** (1280x800) layouts: two panes (list + now playing)?
6. **Album art sizes per class:**
   - now-playing art box;
   - list thumbnails (yes/no: `CompactList` rows are single-line text, label and value, so thumbnails need a list of the app's own);
   - the thumbnail cache sizes follow from this (proposal 96 / 240 / 480 px).
7. **List-row affordance rule** ([F] `RowAffordance`: chevron = Open, value + chevron = Edit, white dot = Action, blue dot = running, grey = Info). Proposed mapping to confirm:
   - folder/album = Open;
   - track = Action, with the playing track as ActionRunning;
   - settings rows = Edit.
8. **Transport on touch versus keyboard:**
   - key map (Space play/pause, arrows seek/skip, `,` `.` previous/next as in Cassette, R repeat, S shuffle?);
   - long-press behaviours;
   - the 9 mm header Esc rule stays.
9. **Queue editing gestures without dragging** (owner rule from Cassette: no dragging on small screens): move up/down keys, "play next" and "add to queue" actions.
10. **Volume display:** the app draws its own level from `status.state` (the launcher draws nothing over apps). Where, and how long?
11. **Notices:** wording and placement for:
    - "This format needs the FFmpeg libraries";
    - "Bluetooth audio is not set up on this board";
    - "call quality";
    - "output disconnected: paused".
12. **Reusable Cassette widgets:** the LED meter (`ui/meter.cpp`, VU option), the output picker rows (`page_settings.cpp` Bluetooth rows with the dimmed paired-but-off devices), `fit.cpp` size classes (`square_screen()`), and the notice line. Which carry over visually?
13. **Icon:** hue and silhouette per ICON-PALETTE (an own hue, not Cassette's sand; a note? a record?).
14. **CJK and long titles:** scrolling (marquee) labels on now playing, or wrap or cut with dots? (LVGL `LV_LABEL_LONG_SCROLL_CIRCULAR` exists [I].)

---

## 11. Not verified

- **No board was reachable** on 2026-11-12 (all seven IPs closed on port 22; HackBerry and RPI52 did not answer ping). So the following were not measured or checked:
  - CPU, RSS and scan figures (all [I]);
  - installed packages and FFmpeg presence;
  - SPA codec plugins;
  - AVRCP key behaviour;
  - the deck's launcher and OS state.
- The Rockbox codec performance page was blocked by bot protection; no primary A53 decode benchmark was found.
- AAC patent end dates (2028/2031 figures from a forum quote of Wikipedia) [U]; WMA patent status [U].
- Raspberry Pi archive FFmpeg (`+rpt1`) configure flags were not read (Debian's were).
- Whether Settings > Apps' installer honours `Recommends` (documented in APP-JSON.md, code not traced).
- PipeWire `bluez5.dummy-avrcp-player` default: from WirePlumber master's example config ("Default: false"), not from the HackBerry's installed files.
- Pi 4B RAM and sound stack; RPI53 audio; tablet Bluetooth audio.
- The feature-matrix "who has it" column is general knowledge, not re-checked per product.

---

## Sources

Code and docs (local, read 2026-11-12):
- Apps `origin/main` d1cf53f:
  - `apps/cassette/{app.json, README.md, docs/PULSE.md, root/usr/share/doc/cassette/copyright, root/.../cassette.desktop}`;
  - `src/core/{codec/decoder.hpp, codec/codec_libs.hpp, playback/{player,sink,output,deck}.hpp, playback/sink.cpp, pulse/launcher_facts.hpp, pulse/pulse_stream.cpp, wav.cpp}`;
  - `apps/_common/{README.md, build_app.sh, platform.cpp, scons/cross_config_defaults.mk}`;
  - `docs/{APP-GUIDELINES.md, APP-JSON.md, PUBLISHING.md}`, `README.md` (libraries table).
- Launcher 9bcfcd07:
  - `docs/APP-CONTRACT.md` (sections 5b, 5c, 6, 13, 14), `docs/OPEN_SOURCE_COMPONENTS.md`, `docs/dev/{DEV-GUIDE,decisions,ROADMAP}.md`;
  - `docs/dev/reports/{026,046,058}`, `docs/cp0_lvgl.en.md` (Lists, Row affordances);
  - `projects/APPLaunch/main/ui/{launcher_media_osd.*, app_media.hpp, ui_screensaver.cpp}`;
  - `ext_components/cp0_lvgl/{include/keyboard_input.h, include/cp0_keyboard_presence.h, include/settings_fonts.hpp, src/cp0/cp0_lvgl_audio.cpp, src/cp0/cp0_audio_system_sound_player.cpp, tests/test_keyboard_presence.c}`.
- Report 058 (scratchpad `bt-study.md`, `bt-section12.md`); memory notes on Cassette, boards, Pi 5b, x86 port.

Web (read 2026-11-12):
- Debian packages, trixie arm64:
  - https://packages.debian.org/trixie/arm64/libavcodec61
  - https://packages.debian.org/trixie/arm64/libavformat61
  - https://packages.debian.org/trixie/arm64/libspa-0.2-bluetooth
  - https://packages.debian.org/trixie/arm64/libmpg123-0t64
  - https://packages.debian.org/trixie/arm64/libvorbisfile3
  - https://packages.debian.org/trixie/arm64/libvorbis0a
  - https://packages.debian.org/trixie/arm64/libopusfile0
  - https://packages.debian.org/trixie/arm64/libfaad2
  - https://packages.debian.org/trixie/arm64/libsndfile1
  - https://packages.debian.org/trixie/arm64/libwavpack1
  - https://packages.debian.org/trixie/arm64/libgme0
  - https://packages.debian.org/trixie/arm64/libopenmpt0t64
  - https://packages.debian.org/trixie/arm64/libtag2
  - https://packages.debian.org/trixie/arm64/libsoxr0
  - https://packages.debian.org/trixie/arm64/libx265-215
  - https://packages.debian.org/trixie/arm64/librsvg2-2
  - https://packages.debian.org/trixie/arm64/libsvtav1enc2
  - https://packages.debian.org/trixie/arm64/libcodec2-1.2
  - fdk-aac search: https://packages.debian.org/search?keywords=fdk-aac&searchon=names&suite=trixie&section=all
- Debian FFmpeg build flags: https://sources.debian.org/src/ffmpeg/7:7.1.5-0+deb13u1/debian/rules/
- Ubuntu 26.04 packages:
  - https://packages.ubuntu.com/search?keywords=libavcodec&searchon=names&suite=resolute&section=all
  - https://packages.ubuntu.com/search?keywords=libspa-0.2-bluetooth&searchon=names&suite=resolute&section=all
  - https://packages.ubuntu.com/search?keywords=libflac1&searchon=names&suite=resolute&section=all
- Raspberry Pi archive FFmpeg: https://aptpeek.dev/raspberrypi/trixie/main/armhf/libavcodec61/8:7.1.4-0+deb13u1+rpt1
- rpicam-apps-lite without libav (forum, unverified): https://redlib.hbubli.cc/r/raspberry_pi/comments/1hpd0px/rpicam_missing_libav_codec
- mpg123 API flags: https://www.mpg123.de/api/group__mpg123__init.shtml
- BlueZ Media API (RegisterPlayer): https://raw.githubusercontent.com/bluez/bluez/master/doc/org.bluez.Media.rst
- WirePlumber:
  - configuration: https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/bluetooth.html
  - example config: https://raw.githubusercontent.com/PipeWire/wireplumber/master/src/config/wireplumber.conf.d.examples/bluetooth.conf
- PipeWire SBC-XQ README: https://git.progress-linux.org/packages/graograman/pipewire/tree/spa/plugins/bluez5/README-SBC-XQ.md
- Codec latency table (user gist): https://gist.github.com/mrintrepide/700f7065aba9974ee23aa6e3fe3688c1
- Headphone latency: https://www.rtings.com/headphones/learn/latency
- MP3 patents and licensing end:
  - https://www.soundandvision.com/node/117203
  - https://macrumors.com/2017/05/15/mp3-format-terminated
  - https://hiphop-n-more.com/?p=219043
- AAC pool: https://www.via-la.com/licensing-2/aac/
- AAC expiry claim (forum): https://hydrogenaud.io/index.php/topic,121109.0.html
- Pi Zero W MP3 CPU (forum): https://forums.pimoroni.com/t/raspberry-pi-zero-audio-amp-shim-slow-audio-output/28410
- Musicolet features: https://krosbits.in/musicolet/
- From 058 (latency): Collabora 2025 LE Audio article, Unity Vivox profile comparison, SoundGuys Android latency (URLs in report 058).

---

## 13. P0 read-only probe results

Date 2026-11-12, about 12:15 board time. Two boards were reachable: **HackBerry** (192.168.68.214) and **zero8** (192.168.68.184). The ssh helper `atlas_ssh.py` was used with strict host keys (both keys matched `known_hosts`) and the password from the environment.

**What was done:** read-only commands only:
- `dpkg`/`apt-cache` queries and `apt-get -s` simulations;
- `pactl`/`wpctl`/`bluetoothctl show` reads;
- the launcher's state files;
- `/proc` and `/sys` reads;
- one 50 MB `dd` read of an existing library to `/dev/null`;
- a python3 `ctypes.CDLL` load of installed libraries to read `VmRSS`;
- on the HackBerry, two `ffmpeg -f null` decodes of existing Cassette takes (nothing was written to disk).

Nothing was installed, paired, started, stopped or changed, and no file was left on either board. Raw output is in the scratchpad: `059-probe-hackberry.txt`, `059-probe2-hackberry.txt`, `059-probe-zero8.txt` and `059-probe3-zero8.txt`.

### 13.1 Board facts

| Item | HackBerry | zero8 |
|---|---|---|
| Model | Raspberry Pi Compute Module 5 Rev 1.0 [F] | Raspberry Pi Zero 2 W Rev 1.0 [F] |
| Kernel | 6.18.60+rpt-rpi-2712, **16 KiB pages** [F `getconf PAGESIZE`] | 6.18.60+rpt-rpi-v8, 4 KiB pages [F] |
| OS | Debian 13 trixie, Pi OS **desktop** (pi-gen stage4, 2025-10-01) [F] | Debian 13 trixie, Pi OS **Lite** (stage2, 2026-10-06) [F] |
| RAM | **4,048 MB** total, 3,677 MB available; swap 2 GB zram, unused [F] | 415 MB total, **261 MB available** with the launcher running; swap 415 MB zram, unused [F] |
| CPU | 4 cores, `ondemand`, 1.5-2.4 GHz (1.6 GHz at the time); 36.7 °C, `throttled=0x0` [F] | 4 cores, `ondemand`, 0.6-1.0 GHz; 36.5 °C, `throttled=0x0` [F] |
| Storage | root on **NVMe** (TS256GMTE400S, 235 GB, 210 GB free); eMMC 14.6 GB not mounted [F] | SD card 14 GB, **9.9 GB free** [F] |
| Read speed hint (`dd iflag=direct`, 50 MB) | **393 MB/s** (NVMe) [F] | **23.2 MB/s** (the file was only 30 MiB) [F] |
| Audio hardware (`aplay -l`) | 2 x vc4-hdmi only; no jack, no USB card [F] | 1 x vc4-hdmi only; **no USB card plugged in at the time** [F] |
| Sound server | PipeWire 1.4.2 + WirePlumber 0.5.8 + pipewire-pulse, all active; PulseAudio API 15.0.0 on PipeWire [F] | **none**: `pactl`/`wpctl` are not installed and no user units are active [F] |
| Default sample spec | float32le 2ch 48 kHz; default sink `bluez_output.9E_0C_81_E6_5D_1A.1` (s16le 2ch 48 kHz, SUSPENDED) [F] | - |
| Bluetooth radio | hci0, manufacturer 0x0131 (Cypress), HCI version 9 (BT 5.0), powered [F] | hci0, manufacturer 0x000f (Broadcom), HCI version 8 (BT 4.2), powered [F] |
| Launcher | **0.10.1**, `board=hackberry-cm5`, 720x720 `class=square`, `sound_kind=bluetooth`, `bt_audio=pipewire`, `bt_hfp=1`, `bt_le_audio=0`, `ram_mb=4048`, battery gauge [F `board.state`, `screen.state`] | **0.10.0**, `board=pi3a-luckfox35`, `sound=0`, `ram_mb=415`, `backlight=levels`; screen 320x480 `class=portrait` (PDA trial, `APPLAUNCH_ROTATE=90`, fb1 16 bpp) [F] |
| Multitasking | on: short side 720, no `APPLAUNCH_MULTITASK` in the launcher's environment [F env; I rule of contract 13] | **off**: short side 320, no override [F env; I rule] |
| State files | `/run/user/1000/applaunch/{board,status,screen}.state` [F] | same [F] |
| Cassette installed | **0.3.0**; Depends: libasound2t64, libopus0, libopusenc0, libflac14 \| 12t64 \| 12, **libpulse0** (5,139 kB) [F] | **0.2.0** (older, before Bluetooth); no libpulse0 (4,499 kB) [F] |

**Changes to the boards matrix (section 7):**
- **HackBerry RAM is 4 GB, not "about 2 GB" [I].**
- The HackBerry root is on NVMe, so SD scan speed is no concern on that board.
- zero8 currently has **no USB sound card attached**. Section 3.2's "zero8 UC02" was the Cassette P0 set-up, and the card must be plugged in for B11.
- zero8 runs the `pi3a-luckfox35` profile in portrait 320x480 and launcher 0.10.0. So the 320x480 portrait layout (D17) is the one this board shows today.

### 13.2 Installed codec and audio libraries

| Package | HackBerry | zero8 |
|---|---|---|
| libmpg123-0t64 1.32.10 | installed | **not installed** (candidate 1.32.10-1+deb13u1) |
| libvorbisfile3 1.3.7 / libvorbis0a / libogg0 | installed / installed / installed | **not installed** / installed / installed |
| libflac14 1.5.0+ds-2, libopus0 1.5.2, libopusenc0 | installed | installed (Cassette) |
| libopusfile0, libfaad2, libmad0, libsndfile1, libtag2 2.0.2, libturbojpeg0 2.1.5 | all installed | none installed |
| libsqlite3-0 3.46.1 | installed | installed |
| **ffmpeg + libavcodec61/libavformat61/libavutil59/libswresample5** | **installed**, Pi archive build `8:7.1.5-0+deb13u1+rpt2` | **not installed** |
| GStreamer 1.26.2 (base/good/bad/libav), VLC 3.0.24, rpicam-apps (full) | installed | only rpicam-apps-lite (no libav) |
| libpulse0, pipewire, wireplumber, libspa-0.2-bluetooth | 17.0 / 1.4.2 / 0.5.8 / 1.4.2-1+rpt4 | none |
| libasound2t64 | 1.2.14 | 1.2.14 |

**Results:**
- [F] **FFmpeg is present on the Pi OS desktop image (HackBerry).** This answers open question 8.3-2 for the HackBerry; RPI52 and the tablet are still [U]. The full `rpicam-apps` is installed beside it, consistent with the inference in 1.2.
- [F] The Lite image (zero8) has no FFmpeg, and `rpicam-apps-lite` pulls no libav. This confirms the Lite assumption.
- [F] The Pi archive's FFmpeg build is configured with `--enable-gpl --enable-libgme --enable-libopenmpt` (`ffmpeg -buildconf`), with no `--enable-nonfree` and no fdk-aac. This matches Debian's flags (8.2), now checked on the Pi build itself.
- [F] **Audio decoders in the Pi FFmpeg:** aac, aac_fixed, ac3, alac, ape, dsd_lsbf/msbf, flac, mp3/mp3float, opus/libopus, tak, truehd, tta, vorbis, wavpack, wmalossless, wmapro, wmav1/wmav2. **TAK is present**, which closes the [U] in the T2 table of 1.1. Musepack is present as `mpc7` and `mpc8` (codecs musepack7/8), and there is no `libfdk_aac` [F, second query].

### 13.3 Package costs (apt-cache and `apt-get -s` on each board's current state)

**Sizes per package:** the [F] numbers on both boards match section 1.2 for the Debian versions. The Pi archive versions differ slightly:

| Package | Installed size |
|---|---|
| libmpg123-0t64 | 349 kB |
| libvorbisfile3 | 86 kB |
| libopusfile0 | 155 kB (still needs libssl3t64) |
| libflac14 | 447 kB |
| libavcodec61 (Pi `8:7.1.5`, rpt2) | **14,328 kB** |
| libavformat61 | 3,120 kB |
| libswresample5 | 237 kB (needs libsoxr0) |
| libavutil59 | 1,092 kB (needs libva/libvdpau/libx11/**OpenCL ICD**) |
| libsqlite3-0 | 1,854 kB |
| libtag2 | 1,351 kB (`libtag1v5` has no candidate on trixie) |
| libturbojpeg0 | 542 kB |
| mpg123 (CLI) | 1,136 kB, plus jack, portaudio, openal and pulse |

**Simulated installs on zero8 (Lite).** The total sums the `Installed-Size` of every package the simulation would install [F]:

| Simulation on zero8 | Packages | Installed | Download |
|---|---|---|---|
| **T1: libmpg123-0t64 + libvorbisfile3** | **2** | **435 kB** | 163 kB |
| FFmpeg libs (avformat, avcodec, swresample, avutil), `--no-install-recommends` | **79** | **104 MB** | 38.7 MB |
| FFmpeg libs, **with Recommends** (apt's default) | **110** | **462 MB** | 120.5 MB |
| `ffmpeg` CLI, `--no-install-recommends` | 151 | 385 MB | 118.5 MB |
| `apt-get -s install ffmpeg` (the brief's command, with Recommends) | **185** | **644 MB** | - |
| `mpg123` CLI, `--no-install-recommends` (for the P0-b test) | 19 | 9.8 MB | 3.3 MB |

- On the HackBerry, every simulation installs **0 packages**: everything is already there.
- The Recommends chain pulls `va-driver-all`, `mesa-va-drivers`, `mesa-vulkan-drivers`, `libgl1-mesa-dri` and `libllvm19`, and also GTK3 and `pocketsphinx-en-us` for the CLI. That is most of the jump from 104 MB to 462 MB [F package list; I which Recommends pull which].

**What these numbers confirm or change:**
- **T1 cost confirmed:** about 0.7 MB was estimated, and 0.44 MB was measured, because libvorbis0a and libogg0 are already on the Lite image.
- **FFmpeg on a Lite board, "100-200 MB" [I]:**
  - **confirmed at the low end, 104 MB, only without Recommends**;
  - **contradicted with Recommends: 462 MB and 110 packages**.
- **Packaging consequence for D3** [I]: if the player's package says `Recommends: libavcodec61...` and the user installs with plain `apt install`, apt installs FFmpeg's own Recommends recursively too. That is about half a GB on a 14 GB SD card, and long minutes on a Zero 2 W.
  - [F launcher code] The launcher's apt **upgrade** path passes `--no-install-recommends` (`settings_apt_platform.cpp:186`, `cyberdeckzero-update`).
  - How a **fresh app install** from Settings > Apps treats Recommends was still not traced (8.3-1 stays [U]).
  - Either way, a README line such as "to add AAC/ALAC/WMA: `sudo apt install --no-install-recommends libavformat61 libavcodec61 libswresample5`" is needed. D3 needs a second look: Recommends only if the installer passes `--no-install-recommends`, else `Suggests`.

### 13.4 Memory after loading the libraries (python3 `ctypes.CDLL`, `VmRSS` delta)

| Load | HackBerry (16 KiB pages) | zero8 (4 KiB pages) |
|---|---|---|
| python3 baseline | 10.8 MB | 8.5 MB |
| T1: mpg123 + vorbisfile + FLAC + opus (+ sqlite) | +0.58 MB (+0.83 MB with sqlite) | FLAC + opus + sqlite **+0.62 MB** (+0.91 MB with libasound); mpg123/vorbisfile not installed |
| + libavutil + libswresample | +1.8 MB | not installed |
| + **libavcodec61** | +15.0 MB | not installed |
| + **libavformat61** | **+23.8 MB over T1**, 140 shared objects mapped | not installed |
| `ffmpeg -i take.flac -f null -` (CLI, max RSS of the child) | **60 MB** | - |

**What this confirms or changes:**
- [F] The T1 decoders cost under 1 MB of RSS, which confirms "lowest" for option B.
- [F] **FFmpeg mapped costs about 24 MB of RSS on the HackBerry**, above the "+8-20 MB" estimate in 1.5.
  - [I] Part of that comes from the 16 KiB page size, since every touched page is four times bigger than on the Zero 2 W. The Zero 2 W figure is probably lower, perhaps 10-16 MB, but it **was not measured** because FFmpeg is not installed there.
  - Even at 10-16 MB, a T2 file would put the player at or over the 25 MB guideline on zero8. That strengthens "dlopen FFmpeg only on the first T2 file" (B+).
- [F] On zero8, MemAvailable was 261 MB with only the launcher running, far above the launcher's 64 MB and 32 MB close thresholds (contract 13). Multitasking is off there anyway, so the close rule does not apply on zero8; the 25 MB guideline is the limit that matters.

### 13.5 Decode speed (HackBerry only, Cassette's own mono takes)

| File | Length | Wall | User CPU | Speed |
|---|---|---|---|---|
| FLAC mono speech, 113 kbps | 15.6 s | 0.13 s (including ffmpeg start-up) | 0.09 s | about 170x real time [F] |
| Opus mono, 49 kbps | 49.9 s | 0.18 s | 0.20 s | about 250x [F]; ffmpeg reported `speed=646x` |

These are mono, low-bitrate takes on a Cortex-A76, so they say nothing about stereo music on the A53. The CPU table in 1.4 stays [I] until P0-b runs on zero8.

### 13.6 Bluetooth audio (HackBerry)

**SPA codec plugins** [F]: `libspa-0.2-bluetooth` 1.4.2-1+rpt4 depends on libfreeaptx0, liblc3-1, libldacbt-abr2/enc2, libopus0 and libsbc1, and **not fdk-aac**. Its plugin directory `/usr/lib/aarch64-linux-gnu/spa-0.2/bluez5/` holds:
- `libspa-codec-bluez5-{sbc, aptx, ldac, lc3, opus, opus-g, faststream, g722}.so`, plus `libspa-bluez5.so`;
- **no AAC plugin**.

This **confirms** section 3.3 (no AAC over A2DP) on the Pi build. G722 is the extra plugin; it is not used for A2DP music [I].

**Connected device, read only** [F `pactl list cards`]:
- `XWF-M18-M28-M38` (form factor "headset", the case speaker) is connected.
- Its card offers `a2dp-sink` (**SBC**, priority 18) and `a2dp-sink-sbc_xq` (SBC-XQ, priority 17). The **active profile is `a2dp-sink`, codec SBC**.
- No HFP profile is offered on this device.
- `status.state` shows `bt_audio_dev=70_BF_92_53_6B_8B:headset:0;9E_0C_81_E6_5D_1A:speaker:1:a2dp`: the Jabra is paired and off, and the speaker is connected with A2DP.
- `bluez5.auto-connect = [ hfp_hf hsp_hs a2dp_sink hfp_ag hsp_ag a2dp_source ]`.

**WirePlumber overrides** [F]: a grep for `dummy-avrcp`, `hw-volume` and `codecs` in `/usr/share/wireplumber/wireplumber.conf.d`, `/etc/wireplumber` and `~/.config/wireplumber` found nothing. So the upstream defaults apply. [I] `bluez5.dummy-avrcp-player` is false, so passthrough keys should reach uinput. P0-c still has to confirm this with key presses.

**AVRCP input node** [F `/proc/bus/input/devices`, `B: KEY=` bitmask decoded]:
- `XWF-M18-M28-M38 (AVRCP)` is event10.
- It advertises `KEY_PLAYCD` (200), `KEY_PAUSECD` (201), `KEY_NEXTSONG` (163), `KEY_PREVIOUSSONG` (165), `KEY_STOPCD` (166), `KEY_REWIND` (168), `KEY_FASTFORWARD` (208), `KEY_VOLUMEUP/DOWN` and `KEY_MUTE`, but **not `KEY_PLAYPAUSE` (164)**.
- This refines 3.6: the player must treat PLAYCD and PAUSECD as play and pause (or as a toggle), not only PLAYPAUSE.
- Whether presses actually arrive is still [U] (P0-c, owner present).

**Keyboard media keys** [F]:
- The HackBerry's own keyboard has a separate `ZitaoTech HACKBerryPiQ20 Consumer Control` node (event3) that advertises `KEY_PLAYPAUSE`, `NEXTSONG`, `PREVIOUSSONG` and `STOPCD`.
- That is a second media-key source for the evdev reader of 6.4.
- zero8 has only the Goodix touch panel and the HDMI CEC inputs: no keyboard and no media keys at the time.

### 13.7 Test music

There is **no music library on either board** [F `find`]:
- HackBerry: only Cassette takes in `~/Recordings` (WAV, FLAC, Opus, mono speech); `~/Music` exists and is empty.
- zero8: Cassette takes (WAV, FLAC) and the launcher's own `startup.mp3` and WAV sounds in old bundle folders; no `~/Music`.

P0-b, P0-e and P0-f need an owner-provided test set (8.3-4, D19).

### 13.8 [U] items: status after this round

| Item (section) | Status |
|---|---|
| FFmpeg on desktop images (1.2, 8.3-2) | **[F] yes on HackBerry**; RPI52 and tablet still [U] |
| FFmpeg install size on Lite (1.2, 2.2, 8.1) | **[F] 104 MB / 79 pkgs without Recommends, 462 MB / 110 pkgs with** (zero8) |
| FFmpeg RSS (1.5, 2.1) | **[F] +24 MB on HackBerry (16 KiB pages)**; Zero 2 W still [U] |
| libswresample5 / libavutil59 size (1.2) | **[F] 237 kB / 1,092 kB** |
| TAK in FFmpeg (1.1) | **[F] present** |
| A2DP codecs, no AAC (3.3, P0-d HackBerry) | **[F] confirmed** (SBC active, SBC-XQ offered); RPI52 not probed |
| dummy-avrcp-player default (3.6, 11) | [F] no local override; key delivery still [U] (P0-c) |
| AVRCP key codes (3.6) | [F] advertised set (no PLAYPAUSE); delivery [U] |
| HackBerry RAM (7) | **[F] 4 GB** |
| Settings > Apps honours Recommends (2.3, 8.3-1) | upgrade path uses `--no-install-recommends` [F]; fresh install still [U] |
| Decode CPU on A53 (1.4), cover decode (1.5), scan time (4) | still [U]; need P0-b/e/f (changes below) |
| PipeWire resample / SBC encode CPU (1.4) | still [U] |
| Ubuntu 26.04 versions, tablet, Pi 4B, RPI53 (1.2, 7) | still [U] (boards not probed) |

### 13.9 Changes that need approval for a second round

1. **zero8 temporary installs** (P0-b), with a rollback by `apt-get purge` of exactly the packages listed by the simulation:
   - `libmpg123-0t64 libvorbisfile3` (2 packages, 435 kB);
   - `mpg123` CLI (`--no-install-recommends`, 19 packages, 9.8 MB);
   - FFmpeg: `ffmpeg --no-install-recommends` (151 packages, 385 MB). Or, to measure only the library RSS, the libraries alone with `--no-install-recommends` (79 packages, 104 MB), loaded with the same python3 `ctypes` probe. The CLI would be needed for `-f null` decode timings.
2. **Test binary in /tmp on zero8** (P0-e: stb_image decode time and peak RSS of 600, 1000 and 3000 px JPEGs; a P0-f tag-read pass), removed afterwards.
3. **Test music set from the owner**, copied to `~/Music-test` on zero8 (and HackBerry), deleted afterwards. Contents: about 10 files covering MP3 320k (LAME, gapless pair), AAC 256k M4A, ALAC, FLAC 16/44 and 24/96, Opus 128k, Vorbis q6, APE -c2000, plus 3 JPEG covers and, for P0-f, a folder of several hundred to 1,000+ files.
4. **USB sound card plugged into zero8** (the UC02) for the wired output check (B11 preparation); owner action.
5. **Owner-present AVRCP test on the HackBerry** (P0-c):
   - read event10 (the speaker) and the Jabra's AVRCP node with a python3 evdev read;
   - press play, pause, next and previous on the speaker and on the Jabra;
   - also `pactl subscribe` for absolute volume.
   This needs the Jabra powered on by the owner; no pairing changes.
6. **Optional:** the same read-only probe on RPI52 (192.168.68.160) and the tablet for FFmpeg presence and SPA codecs (P0-a/P0-d), once they are online.

---

## 14. P0 round 2 results (zero8)

Date 2026-11-12, 12:47-13:41 board time, on **zero8** (192.168.68.184, Raspberry Pi Zero 2 W Rev 1.0, Pi OS Lite trixie, launcher 0.10.0). Access was through `atlas_ssh.py` (strict host key, password from the environment, sudo through stdin).

**Owner approvals used (2026-11-12):**
- temporary package installs on zero8, with an exact rollback;
- the 20 MP3 files of the owner's "playlist SKI" folder as source music, transcoded on the PC.

**Not done (not approved):** no Bluetooth change, pairing, connect or disconnect; no config or service change; no compiled test binary (so the stb_image probe P0-e is still open); no USB sound card test; no HackBerry test; nothing on any other board.

**One system action beyond the brief's list:** `sync; echo 3 > /proc/sys/vm/drop_caches` before each cold measurement (5 times). It empties the kernel page cache only; it changes no file or setting.

**Raw output** (scratchpad): `059-r2-pre-zero8.txt`, `059-r2-before-zero8.txt`, `059-r2-pkgs-before-zero8.txt`, `059-r2-apt-install-zero8.log`, `059-r2-after-install-zero8.txt`, `059-r2-new-pkgs-zero8.txt`, `059-r2-stage-*.txt`, `059-r2-res-*.txt`, `059-r2-rollback-sim-zero8.txt`, `059-r2-purge-zero8.log`, `059-r2-rollback-zero8.txt`, `059-r2-cleanup-zero8.txt`, `059-r2-testset-check.txt`. Scripts: `059-r2-make-set.sh`, `059-r2-tag.py`, `059-r2-m.py` (measurement wrapper), `059-r2-bench.sh`, `059-r2-scan.py`, `059-r2-rss.py`, `059-r2-seekacc.py`.

### 14.1 Method

- Every measurement ran **alone**, as the normal user, **with the launcher running** (pid 826 the whole time). Before each run, the wrapper refused to start if the board was throttled now, at 75 °C or more, below 60 MB MemAvailable, or if the launcher had gone. It never refused.
- **CPU** for ffmpeg runs comes from `-benchmark` (user + system time of the transcode itself, without the process start-up). For the other tools it comes from `wait4()` of the child process.
- **"% of one core at 1x"** = CPU seconds / audio seconds x 100. It is the share of **one** of the four A53 cores at 1.0 GHz.
- **ffmpeg 7.1 runs its command line as a pipeline of threads** (demuxer, decoder, filter, muxer), so its numbers include that overhead. A stream-copy run of each file (`-c:a copy`, same pipeline without the decoder) measured the overhead; "decoder only" below subtracts it. The decoder-only figure is still an upper bound for an app that calls libavcodec directly [I].
- **Peak RSS**: `/usr/bin/time` is not installed on zero8 (package `time`), so I did not add it. The wrapper uses `getrusage` for ffmpeg, and `VmHWM` sampled every 50 ms for the small tools, because `getrusage` also counts the forked python copy before `exec` (about 11 MB).
- **Vorbis** was timed with `oggdec -o /dev/null` instead of `ogg123`, which would need an audio output through libao.
- `/tmp` on zero8 is a 208 MB **tmpfs in RAM** [F `df`], so the test files went to `~/music-test-059` on the SD card. Only the small state lists went to `/tmp`.

### 14.2 Test set

Built in WSL (Ubuntu 24.04, FFmpeg 6.1.1, LAME 3.100, mutagen) with `nice -n 15`. It stays on the PC for later rounds:

`C:\Users\osrde\AppData\Local\Temp\claude\8c60edbf36263009\32e17302-f7b8-43d3-a6e1-6f6de8834722\scratchpad\059-testset\` (`full\` 906 MB, `covers\`, `board\` 99 MB, `scan\` 19 MB).

**The four tracks** (from the owner's folder; all sources are MP3 at 44.1 kHz):

| Id | Track | Length | Source | Loudness (mean / peak dBFS) |
|---|---|---|---|---|
| S (short) | Sister Sledge, "Thinking Of You (Dimitri From Paris Remix) (Snippet)" | 2:35 | 128 kbps CBR | -16.2 / -0.1 |
| L (long) | Flight Facilities, "Clair De Lune (feat. Christine Hoberg)" | 7:42 | 128 kbps CBR, 500x500 cover | -13.6 / -0.1 |
| LOUD | Lisa Mitchell, "Neopolitan Dreams (Lanny May's Neo Neo Mix)" | 6:34 | 320 kbps CBR | -10.5 / 0.0 |
| QUIET | Oxford, "You and me" | 5:38 | 320 kbps CBR | -17.5 / 0.0 |

**Ten formats of each track** (`full\<id>\`), plus one CJK file:
- MP3 original (copy);
- FLAC 16/44.1 (level 5);
- FLAC 24/48 (**upsampled** from the MP3 decode with soxr: the extra bits and bandwidth carry nothing);
- WAV 16/44.1;
- Ogg Vorbis q5 (libvorbis);
- Opus 128 kbps (libopus, 48 kHz);
- AAC-LC 256 kbps M4A (FFmpeg's native `aac` encoder, `faststart`);
- ALAC 16/44.1 M4A;
- LAME CBR 128 and LAME VBR V2, with ID3v2 tags (v2.3 UTF-16 on the CBR files, v2.4 UTF-8 on the V2 files) and an embedded **baseline** JPEG cover: 300, 600 or 1000 px (scaled from L's 500x500 cover, so 600 and 1000 are upscaled);
- `S_cjk-title.mp3`: Japanese title, Chinese artist, Korean album, ID3v2.3 UTF-16, 300 px cover.

All formats are transcodes of lossy MP3s. That is fine for decode cost and tags, not for listening tests.

**Scan set** (`scan\`): 1,000 MP3s of 1.5 s (quiet 220 Hz tone, LAME CBR 64 mono, about 12 kB of audio each) in 100 album folders under 40 artist folders, 17.0 MB in all.
- Tags: title, artist, album, track n/10, year, genre on every file; album artist on a third of the albums, disc number on a seventh, a ReplayGain TXXX on a fifth.
- ID3v2.3 and v2.4 alternate per album; a 300 px APIC cover on 24 albums (240 files); an ID3v1 tag too on 10 albums (100 files).
- Names mix accents and CJK.

**Copied to zero8:** one 121.4 MB tar (under the 150 MB limit), unpacked to `~/music-test-059` (118 MB):
- S in MP3 original, CBR 128, V2, Vorbis, Opus and AAC, plus the CJK file;
- 90 s excerpts (30-120 s) of S as WAV, FLAC 16/44.1, FLAC 24/48 and ALAC (the full lossless files would not fit the limit);
- L as V2 MP3 and AAC (full length, for the seek and 1x tests);
- `Q90_orig320.mp3`: a 90 s stream copy of QUIET's 320 kbps MP3;
- the three covers and the scan set.

### 14.3 Packages (temporary)

| Item | Result |
|---|---|
| Free space before | 9.9 GB (`df`: 10,295,768 kB available); MemAvailable 258 MB; 44.0 °C, `throttled=0x0` [F] |
| Simulation `apt-get -s install --no-install-recommends mpg123 flac vorbis-tools opus-tools ffmpeg` | **165 packages**, no upgrade of an installed package, no removal [F] |
| Package with a service | only `x11-common` (init script); its unit is **masked** and stayed inactive [F `systemctl`] |
| Install (same command, `-y`) | exit 0, no `E:` or `W:` line; **120 MB downloaded in 43 s** (2.8 MB/s), dpkg 13:01:21-13:03:28, so about **2 min 50 s** in all; apt: "400 MB of additional disk space" [F] |
| New packages (diff of `dpkg -l`) | **exactly the 165 simulated**, no version change of any installed package [F]; the list is `059-r2-new-pkgs-zero8.txt` |
| Disk while installed | 9.2 GB free (9,663,216 kB, with the test files on the card); the 1.5 GB floor was never near |

The 165 include `libmpg123-0t64`, `libvorbisfile3`, `libavcodec61`/`libavformat61`/`libswresample5`/`libavutil59` (Pi build `8:7.1.5-0+deb13u1+rpt2`), and the CLI chain: `libavfilter10`, `libavdevice61`, SDL2, Mesa, LLVM 19, Vulkan, X11 client libraries. The library-only set measured in 13.3 (79 packages, 104 MB) is what a player would pull.

### 14.4 Decode cost per format (zero8, one core at 1.0 GHz)

**T1 decoders through their own command-line tools** (single thread, process start included; these are the libraries the player would load):

| Format (file) | Tool (library) | Audio | CPU | Real-time factor | **% of one core at 1x** | Peak RSS (VmHWM) |
|---|---|---|---|---|---|---|
| MP3 128 CBR (S original) | `mpg123 -t` (libmpg123, NEON64 decoder) | 155 s | 1.24 s | 125x | **0.80** | 2.5 MB |
| MP3 VBR V2, about 172 kbps (S) | same | 155 s | 1.27 s | 122x | 0.82 | 2.5 MB |
| MP3 320 CBR (Q90) | same | 90 s | 0.84 s | 107x | 0.93 | 2.4 MB |
| MP3 VBR V2, 159 kbps (L, 7:42) | same | 462 s | 3.59 s | 129x | 0.78 | 2.6 MB |
| FLAC 16/44.1 (S90) | `flac -t` (libFLAC, **with** the MD5 check) | 90 s | 0.59 s | 153x | **0.65** | 2.3 MB |
| FLAC 24/48 (S90) | same | 90 s | 0.81 s | 111x | 0.90 | 2.3 MB |
| Vorbis q5 (S) | `oggdec` (libvorbisfile) | 155 s | 3.24 s | 48x | **2.09** | 2.4 MB |
| Opus 128 (S) | `opusdec --rate 48000 --float` (libopusfile + libopus 1.5.2) | 155 s | 5.61 s | 28x | **3.62** | 4.7 MB |
| Opus 128 (S) | same, 16-bit output with dither (the default) | 155 s | 7.37 s | 21x | 4.75 | 4.7 MB |

[F all rows; two passes agreed within 2%.] `opusdec` spends about 1.7 s of its time in the kernel; the tool, not libopus, is the likely cause [I]. So 3.6% is an upper bound for libopus.

**FFmpeg 7.1.5 (Pi build) command line**, `-threads 1 -i FILE -map 0:a -f null -`, second pass:

| Format (file) | Decoder | Audio | CLI CPU | Copy overhead | **Decoder only, % at 1x** | Real-time factor (decoder only) |
|---|---|---|---|---|---|---|
| MP3 128 CBR (S original) | `mp3float` | 155 s | 2.49 s | 0.17 s | **1.50** | 67x |
| MP3 CBR 128 LAME (S) | `mp3float` | 155 s | 2.39 s | 0.17 s | 1.43 | 70x |
| MP3 V2 (S) | `mp3float` | 155 s | 2.44 s | 0.17 s | 1.47 | 68x |
| MP3 320 CBR (Q90) | `mp3float` | 90 s | 1.55 s | 0.13 s | 1.57 | 64x |
| MP3 V2 (L) | `mp3float` | 462 s | 7.28 s | 0.53 s | 1.46 | 69x |
| Vorbis q5 (S) | `vorbis` (native) | 155 s | 3.25 s | 0.35 s | **1.87** | 53x |
| Opus 128 (S) | `opus` (native) | 155 s | 4.25 s | 0.21 s | **2.60** | 38x |
| **AAC-LC 256 (S)** | `aac` (native) | 155 s | 2.45 s | 0.18 s | **1.46** | 69x |
| AAC-LC 256 (L) | `aac` | 462 s | 7.46 s | 0.53 s | 1.50 | 67x |
| FLAC 16/44.1 (S90) | `flac` | 90 s | 0.96 s | 0.15 s | 0.90 | 111x |
| FLAC 24/48 (S90) | `flac` | 90 s | 1.17 s | 0.27 s | 1.01 | 99x |
| WAV 16/44.1 (S90) | `pcm_s16le` | 90 s | 0.19 s | 0.05 s | 0.16 | 634x |
| **ALAC 16/44.1 (S90)** | `alac` | 90 s | 2.12 s | 0.04 s | **2.31** | 43x |

[F] Every ffmpeg process also peaks at **50-59 MB RSS**, whatever the format (`-benchmark` maxrss 50-52 MB; `getrusage` 56-59 MB).

**Results:**
- [F] **The most expensive format measured costs 2.6% of one core** (Opus via FFmpeg; 3.6% through opusdec). MP3 through libmpg123 costs **0.8%**.
- [F] **libmpg123 decodes MP3 at half the CPU of FFmpeg's `mp3float`** (0.8% against 1.5%), at 2.5 MB process RSS against 50+ MB.
- [F] ALAC (2.3%) costs more than AAC-LC (1.5%) and more than FLAC (0.9%) in FFmpeg.
- [U] Not measured: HE-AAC, WMA, APE, WavPack, DSF and the trackers (no such files in the set).

### 14.5 Playing at 1x: resampling and EQ

L's V2 MP3 (and its AAC) decoded to the null sink at real time (`-re -t 30`), one run each. "Transcode" = ffmpeg's `-benchmark` CPU over the 29.5 s of output; the whole process, start-up included, is in brackets.

| Chain (30 s at 1x) | Transcode CPU | **% of one core** | Added by the filter | RSS (sampled avg / peak) |
|---|---|---|---|---|
| MP3 V2 decode only | 0.76 s | **2.6** (5.2 with start-up) | - | 50.5 / 51.8 MB |
| + `aresample=48000` (swr, default quality) | 1.24 s | 4.2 | **+1.6** | 50.6 / 52.0 MB |
| + `aresample=48000:resampler=soxr` | 1.77 s | 6.0 | **+3.4** | 50.9 / 52.3 MB |
| + 10-band EQ (ten `equalizer` peaking biquads, 31 Hz-16 kHz) | 1.68 s | 5.7 | **+3.1** | 50.6 / 52.0 MB |
| + 10-band EQ + swr to 48 kHz | 2.11 s | 7.2 | +4.6 | 50.7 / 52.1 MB |
| AAC-LC 256 decode only | 0.76 s | 2.6 | - | 51.3 / 52.7 MB |
| Launcher alone (same 30 s window, before the runs) | - | 2.2 | - | 7.2 MB |

[F all rows.] Two more facts:
- [F] Real-time decoding costs more per second than the batch runs (2.6% against 1.5% for the same MP3). [I] The cause is the per-frame thread wake-ups and the `ondemand` governor, which ran the core at 600-800 MHz at the end of these light runs.
- [I] An app that decodes into its own ring buffer in batches would sit between the two figures.

### 14.6 Memory

**Libraries loaded with python3 `ctypes`** (same method as 13.4, two runs, warm cache) [F]:

| Load step | VmRSS delta | Shared objects mapped | Load time (warm) |
|---|---|---|---|
| python3 baseline | (8.9 MB) | 7 | - |
| T1: libmpg123 + libvorbisfile + libFLAC + libopus | **+0.50 MB** | 13 | 5 ms |
| + libavutil + libswresample | +2.0-2.2 MB | 31 | 11 ms |
| + libavcodec61 | +14.2-14.4 MB | 101 | 113 ms |
| + libavformat61 | **+22.4-22.5 MB** | 139 | 181 ms |
| + iterating all 747 codecs and 359 demuxers | +23.1-23.2 MB | 139 | - |

**Command-line start-up** [F]:
- `ffmpeg -version` **cold** (after a cache drop): **4.6 s** wall, 1.1 s CPU, 50 MB RSS.
- `ffmpeg -version` / `ffprobe -version` warm: **0.8 s wall, all of it CPU**. [I] The cause is the dynamic linking of the CLI's whole chain (libavfilter, libavdevice, SDL2, Mesa...), which an app that dlopens only libavformat/libavcodec does not pay.
- `mpg123 --version`: 4 ms.

**What this means:**
- [F] **FFmpeg mapped costs +22.5 MB on the Zero 2 W**, practically the same as the HackBerry's +23.8 MB. **The 16 KiB page-size explanation of 13.4 was wrong**, and the [I] "10-16 MB on the Zero 2 W" is contradicted.
- [I] A player process with FFmpeg loaded would sit at about 25-30 MB RSS (an LVGL app base of a few MB, plus 22.5 MB). That is **at or over the 25 MB guideline**. With T1 only, it stays far below.
- [F] The T1 decoders cost 0.5 MB mapped and 2.3-2.6 MB as whole processes.
- [F] MemAvailable on zero8 stayed between 255 and 296 MB throughout, with the launcher running. RAM was never tight.

### 14.7 Seek

**Seek cost** [F, three repetitions, medians]:

| Seek | Method | Wall (whole process) | Same command from the start | Seek adds |
|---|---|---|---|---|
| VBR MP3, 7:42, to 450 s, decode 5 s | `ffmpeg -ss 450 -i` | 1.04 s | 0.90 s (`-ss 0`) | **about 0.13 s** |
| AAC M4A, 7:42, to 450 s, decode 5 s | `ffmpeg -ss 450 -i` | 0.89 s | 0.88 s | **about 0.02 s** (MP4 sample table) |
| VBR MP3 to frame 17,227 (about 450 s), decode 191 frames | `mpg123 -k 17227 -n 191` | 0.146 s | 0.046 s (`-k 0`) | **0.10 s** (frame scan through 9 MB) |

The ffmpeg walls are mostly its 0.8 s start-up.

**Seek accuracy.** Each seek's PCM was located in a full decode from the start (normalised cross-correlation on the PC; every match had ncc = 1.00000 and at most 1 LSB difference) [F]:
- `ffmpeg -ss 450` on the **LAME VBR MP3** (Xing/LAME header present): **sample-exact** (0 samples off).
- `ffmpeg -ss 450` on the AAC M4A: sample-exact.
- `mpg123 -k 17227`: starts at frame 17,227 x 1,152 **minus 1,105 samples**, which is exactly the LAME encoder delay. So it is sample-exact in mpg123's gapless timeline; the app maps frames to time with that offset.
- [F] Gapless: ffmpeg's full decode of the V2 MP3 has exactly the WAV's length (81,547,652 bytes), so the LAME delay and padding are removed. The AAC decode is 927 samples longer than the WAV [F]: priming or padding left over from FFmpeg's encoder [I].

This contradicts section 2.1's "VBR MP3 seeking uses the Xing TOC (approximate)" **for this file**. [I] FFmpeg 7.1 refines the seek at an extra cost of about 0.1 s. A VBR MP3 without a Xing or LAME header is still [U].

### 14.8 Cover art and tag reads

**Cover art** (embedded APIC) [F, two repetitions]:

| Size | Extract (stream copy) | Decode to YUV (`mjpeg`) | Decode + convert to RGB24 | Process peak RSS (RGB run) | libmpg123 extract (`mpg123-id3dump -p`) |
|---|---|---|---|---|---|
| 300x300 (13 kB) | 10 ms | 11-12 ms | 15 ms (66 ms first run) | 52.3 MB | 38-42 ms (2.5 MB file) |
| 600x600 (41 kB) | 13 ms | 14 ms | 26-28 ms | 54.4 MB | 39-42 ms (3.3 MB file) |
| 1000x1000 (79 kB) | 33 ms | 34 ms | 53-54 ms | **59.3-59.5 MB** | 105 ms (9.2 MB file) |

- Times are ffmpeg's transcode time (wall), so they include its pipeline; each process also pays the 0.8 s start-up. The extracted 1000 px JPEG is byte-identical to the embedded one [F `cmp`].
- [I] The process grows by 7 MB between the 300 and 1000 px RGB runs: the 3 MB RGB frame, the 1.5 MB YUV frame and the scaler buffers. That is consistent with the 1.5 table (1000x1000 RGB = 3 MB).
- **stb_image was not measured** (compiled binaries not approved). A 3000x3000 cover was not tested. P0-e stays open.
- [F] `mpg123-id3dump` reads the **whole file** (system time grows with file size: 105 ms for 9.2 MB). [I] An app calling `mpg123_meta_check()` after the first frame would not.

**Tag read per file:**
- [F] **`ffprobe` costs 0.78-0.84 s per file** (WAV 1.05 s), with about 0.72 s of CPU and a 56-58 MB peak, whatever the format. That is its start-up; the tag parsing itself does not show.
- [F] **CJK tags:** ffprobe and libmpg123 (`mpg123-id3dump`) both print the ID3v2.3 UTF-16 title, artist and album correctly as UTF-8:

  ```
  Title: 夜明けのスキャット（テスト曲）
  Artist: 测试艺术家
  Album: 테스트 앨범 059
  ```

  This confirms the [I] in table 1.1 ("libmpg123 converts to UTF-8").

### 14.9 Library scan timing (1,000 files, 100 albums)

"Cold" means right after `sync; echo 3 > /proc/sys/vm/drop_caches` (dentries, inodes and pages dropped); "warm" means run again at once. The python numbers are the script's own loop time, without interpreter start-up.

| Method | Cold | Warm | Per file (cold) | **10k files (cold, linear)** | **100k files** |
|---|---|---|---|---|---|
| `os.walk` + `stat` (what an incremental rescan does) | 0.17 s | 0.05 s | 0.17 ms | **1.7 s** | 17 s |
| Stdlib ID3v2 reader: open, header, tag body, 7 text frames, 4 kB after the tag (Xing/Info for the duration), last 128 bytes (ID3v1) | 2.22 s | 0.28 s | **2.2 ms** | **22 s** | 3.7 min |
| `mpg123-id3dump` (libmpg123), one process over all files through `xargs` (whole-file parse) | 2.49 s | 0.57 s | 2.5 ms | 25 s | 4.2 min |
| **`ffprobe` once per file** (the heavy fallback) | **803.6 s** (99% CPU) | 80.0 s for 100 files | **0.80 s** (warm too) | **2.2 hours** | **22 hours** |
| mutagen | not installed on zero8: not measured | | | | |

[F all measured rows; the extrapolations are linear: I.]

**Caveats** [I]:
- The scan files are small (17 kB on average) and were written in one go, so they lie close together on the card. That is the **best case**.
- Real 3-10 MB files are scattered, and the kernel reads at least 128 kB ahead per file (`read_ahead_kb=128` [F]). A guess for a real library is **3-6 ms per file cold**: 10k files in 0.5-1 min, 100k in 5-10 min. It stays [U] until a real library is scanned.
- Large embedded covers (hundreds of kB in the ID3 tag) make the tag read longer unless the reader skips the APIC frame by its size.

### 14.10 Thermal and clock

- [F] **159 measured runs, all with `throttled=0x0` before and after.**
- [F] Temperature 44.0 °C at the start; **45.1-53.2 °C** during the runs. The peak came at the end of the 15 minutes of back-to-back `ffprobe` processes (one core at 100%). The batch decodes reached 52.6 °C, and the 1x runs cooled the SoC to 45 °C.
- [F] The ARM clock was 1,000 MHz under load. `ondemand` went down to 600-800 MHz in light phases.
- [I] No heatsink question for a music player: one decode stream is a few percent of one core.

### 14.11 Playback path (step 4)

- [F] **No sound server on zero8**: no `pipewire`, `pipewire-pulse`, `wireplumber` or `pulseaudio` process, and `pactl`/`wpctl` are not installed.
- [F] ALSA has one card, `vc4-hdmi` (MAI PCM i2s-hifi-0). `default` resolves to it, there is no `asound.conf` or `.asoundrc`, and `/sys/class/drm/card1-HDMI-A-1/status` is `disconnected`. No USB card was plugged in.
- So the only device is HDMI without a monitor, not an internal output, and **nothing was played**. The volume was not touched.
- B11 (USB card output) still needs the UC02 plugged in (13.9-4).

### 14.12 Rollback and proof

**Steps:**
1. `apt-get -s purge` of exactly the 165 new packages. The simulation would purge **the same 165 and nothing else**, with no `Inst` or `Remv` line [F].
2. `apt-get purge -y` of that list: exit 0 [F].
3. `apt-get -s autoremove --purge` afterwards would remove **0 packages**, so autoremove had nothing to do and was not run [F].
4. The 162 `.deb` files this session downloaded were deleted from `/var/cache/apt/archives`, keeping the 25 that were there before. The file list is identical to the one before [F].
5. apt's backups of its `ucf` hash file had rotated twice during the purge (`/var/lib/ucf/hashfile.1`, `.2` appeared).
   - The current `hashfile` was byte-identical to the pre-session one (`cmp`).
   - I moved the rotated copies back to their original names and dates (`hashfile`, `hashfile.0`) and removed the extra one.
6. All test files (`~/music-test-059`, logs) and the `/tmp` lists were deleted.

**Proof** [F, `059-r2-rollback-zero8.txt`, `059-r2-cleanup-zero8.txt`]:

| Check | Before (13:00) | After (13:40) | Result |
|---|---|---|---|
| `dpkg -l` | 672 lines, md5 `299733f9c86387689c0c6ce3a2358ca8` | 672 lines, md5 `299733f9c86387689c0c6ce3a2358ca8`, `diff` empty | **identical** |
| `apt-mark showmanual` | 246 packages | `diff` empty | **identical** |
| Every path under `/etc /usr /var/lib /var/cache /opt` (96,675 paths) | list saved | `cmp` of the sorted lists | **identical** |
| `/var/cache/apt/archives` file list | 25 files | `diff` empty | **identical** |
| Root file system used | 3,507,140 kB | 3,507,464 kB | **+324 kB**: apt, dpkg and journal log lines in `/var/log` (they record the install and the purge) |
| `/tmp` (tmpfs) | 0 kB used | 0 kB used | identical |
| Home folder | 19 entries | the same 19 entries | identical |
| MemAvailable | 258 MB | 256 MB | baseline (the page cache differs) |
| Launcher | pid 826, running | **pid 826, still running** (1 h 30 min up), 2.4% CPU, no failed systemd unit | unchanged |
| Temperature, throttle | 44.0 °C, `0x0` | 48.9 °C, `0x0` | normal |

**Remaining differences:**
- [F] `/etc/ld.so.cache` was rewritten by `ldconfig` during the purge (same path and the same library set). [I] Its content is identical, but it was not byte-compared, because no copy was taken before.
- [I] The same holds for regenerated caches such as the man-db index.
- [F] Swap use went from 22 to 27 MB: pages the kernel moved to zram under the dpkg load. It is not a setting.

### 14.13 What the numbers confirm or change

**Earlier estimates:**

| Estimate (section) | Was | Measured on zero8 | Verdict |
|---|---|---|---|
| MP3 decode (1.4) | 3-6% of a core [I] | **0.8%** libmpg123, 1.5% FFmpeg | **changed: 4-7x lower** |
| FLAC 16/44.1 (1.4) | 1-3% [I] | 0.65% libFLAC (with MD5), 0.9% FFmpeg | confirmed at or below the low end |
| FLAC 24-bit (1.4) | 3-6% for 24/96 [I] | 0.9-1.0% for 24/48 | lower (24/96 not measured) |
| Vorbis (1.4) | 4-8% for q6 [I] | 2.1% libvorbisfile, 1.9% FFmpeg (q5) | **changed: about half** |
| Opus 128 (1.4) | 3-6% [I] | 2.6% FFmpeg, 3.6% libopus via opusdec | confirmed (low end) |
| AAC-LC 256 (1.4) | 4-8% [I] | **1.5%** | **changed: 3-5x lower** |
| ALAC (1.4) | 2-4% [I] | 2.3% | confirmed |
| WAV (1.4) | under 1% | 0.16% | confirmed |
| Resampling 44.1 to 48 kHz (1.4) | 3-8% for PipeWire [U] | FFmpeg swr +1.6%, soxr +3.4% | lower for FFmpeg's resamplers; **PipeWire's own still [U]** |
| 10-band EQ (1.4) | under 1% [I] | **+3.1%** with FFmpeg's `equalizer` filter chain | **changed: about 3x higher**, still small; [I] an own float biquad loop should be cheaper |
| "Every format except APE extra high plays with large margin" (1.4) | [I] | worst measured 2.6% (7.2% with EQ and resampling) | **confirmed** |
| FFmpeg RSS (1.5, 2.1, 13.4) | +8-20 MB; Zero 2 W "10-16 MB" [I] | **+22.5 MB** | **changed: above the range**; the page-size theory was wrong |
| FFmpeg cold dlopen (2.1) | 0.1-0.5 s [U] | 0.31 s warm (ctypes); the CLI takes 4.6 s cold | warm confirmed; cold [I] 1-3 s for the app's smaller set |
| VBR MP3 seek with FFmpeg (2.1, 2.2) | approximate (TOC) [I] | sample-exact on a LAME VBR file, +0.13 s | **changed for LAME files**; headerless VBR [U] |
| mpg123 VBR seek (2.1) | accurate (frame index) | sample-exact, +0.10 s at 450 s | confirmed |
| First tag scan (4) | 10-30 ms per file; 10k in 3-10 min [I] | 2.2 ms per file cold (best case); 10k in about 22 s | **changed: about 10x faster**; real libraries still [U] |
| Incremental rescan (4) | 5-20 s per 10k [I] | 1.7 s per 10k cold | **changed: faster** |
| Library scanning through FFmpeg probing (2.1) | "slower than a tag-only reader" [I] | `ffprobe` per file: 0.80 s, 360x the stdlib reader | **confirmed, and far worse when it is a process per file**; in-process probing [U] |
| libmpg123 UTF-8 conversion of ID3 (1.1) | [I] | correct for UTF-16 CJK | confirmed |
| FFmpeg install on a Lite board (1.2, 13.3) | 385 MB / 151 packages for the CLI | 400 MB / 165 packages with mpg123, flac, vorbis-tools, opus-tools; 2 min 50 s | confirmed |

**Decisions:**
- **D2 (decode architecture B+): confirmed and strengthened.**
  - CPU is no argument either way: every codec is at or under 2.6% of one core.
  - Memory is the argument. FFmpeg costs +22.5 MB the moment it is loaded, against +0.5 MB for all four T1 libraries. So the most common formats (MP3, FLAC, Opus, Vorbis) must not depend on FFmpeg on the Zero 2 W.
  - libmpg123 is also half of FFmpeg's CPU for MP3.
  - The seek-accuracy argument for mpg123 (2.1) is weaker than written: FFmpeg was sample-exact on the LAME file.
- **D3 (FFmpeg as Recommends, loaded at run time): unchanged in substance, and the RSS cost is now known for the Zero 2 W.**
  - Loading it only on the first T2 file is necessary, not just nice: +22.5 MB, about 0.3 s warm, seconds cold.
  - Round 1's packaging question stays (Recommends pulls 462 MB through apt's default, 13.3).
  - [I] On `ram_mb` ≤ 512 boards, the README can name the cost: "AAC/ALAC add about 23 MB of memory and 100 MB of disk".
- **D4 (folder-first MVP, library database in P2): the numbers remove the scan-time risk, not the reason for the order.**
  - A native tag scan reads 1,000 files in about 2.2 s cold on the Zero 2 W (10k in under half a minute in the best case).
  - So a first scan is not the multi-minute problem section 4 feared, and the P2 database is less risky than estimated.
  - The MVP order (folder first) still stands on effort and UI grounds; that is the owner's and coordinator's call.
  - **Hard rule from these numbers: the scanner must never spawn `ffprobe` (or any FFmpeg process) per file.** At 0.80 s of full CPU per file, cold or warm, 10k files would take 2.2 hours and 100k files 22 hours. T2 files (M4A and the rest) need an in-process reader: libavformat already loaded, or an own MP4 atom reader; its cost per file is [U].
- **D18 (Lite boards wired only until 058 D2): unchanged.**
  - zero8 has no sound server, and no USB card was plugged in, so nothing was played.
  - The numbers remove CPU as an objection to a sound server on Lite boards. Decode + soxr + EQ is 7% of one core, and MemAvailable stays at 255-296 MB with the launcher.
  - [U] PipeWire's own CPU and RSS on the Zero 2 W were not measured.
- **D13 (hi-res kept):** FLAC 24/48 decodes at 0.9-1.0%, so 24-bit costs nothing that matters. Confirmed.
- **D11, D5, D7, D8, D9, D10:** not touched by this round.

### 14.14 Still open, and the next probe

1. **stb_image cover decode** (P0-e): time and peak RSS for 600, 1000 and **3000 px** JPEGs, and a scaled decode (libturbojpeg `scale_denom` or TJPGD). It needs the owner's word for a small arm64 test binary in `/tmp`. The 3000 px case is the biggest memory risk left (27-36 MB in 1.5).
2. **A real library scan:** a few hundred real 3-10 MB files with large covers, cold, with the stdlib reader, to replace the 3-6 ms [I] per file.
3. **In-process libavformat probe cost per file** (without the CLI start-up): needed only if FFmpeg is the tag reader for T2 files during a scan.
4. **USB card output** (B11 preparation) with the UC02 plugged in, and the ALSA `default` path at 44.1 kHz on a 48 kHz-only card (dmix or plug resampling CPU).
5. HE-AAC, WMA, APE and WavPack decode cost (transcodes from the same set), and a VBR MP3 without a Xing header for seek accuracy.
6. P0-c (AVRCP keys on the HackBerry, owner present) and P0-d on RPI52, as listed in 13.9.
