# 061 - How Vinyl could play from a network music server (study)

Date 2026-11-17. A read-only study: no product code was written, nothing was installed, no board was touched, nothing
was committed or pushed. It answers the ROADMAP row "Vinyl: music server connection study" (owner request 2026-11-13,
report 060 section 15).

**Sources read:**
- Vinyl as published (apps repo `main` @ fdad829, 0.3.0 tree) and the parked 0.7.0 draft (worktree `apps-vinyl-p2d`,
  EQ and crossfade): `README.md`, `docs/decisions.md`, `src/core/{engine, decode, codec, library, queue, state}`
  headers, `track_decoder.cpp` (how each decoder opens its file), `library/schema.cpp`, `library/media_guard.hpp`.
- The Home Assistant app (`apps/home-assistant`): its libcurl loader, its credential store and its QR pairing.
- Launcher `github/main` @ 2f1e8467: `docs/APP-CONTRACT.md` (`status.state`), reports 059 and 060, the reports index.
- Primary sources on the web (section "Sources"): the OpenSubsonic and Subsonic API pages, Navidrome's API notes and
  `stream.go`, Jellyfin client guides and SDK references, the MPD protocol, Debian trixie package pages, libnfs
  `COPYING`, the systemd-creds manual, NFS export rules.

**Tags (as in 058 to 060):** **[F]** fact, with its evidence; **[I]** inference; **[U]** unverified.

**Assumptions about the owner's home (nothing was asked):** a NAS or a PC holds the music as files in folders; it is on
when the deck plays; the deck reaches it over home Wi-Fi; the owner can install a program on the NAS or PC once (most
NAS run Docker containers); listening away from home (through a VPN) is a later topic.

---

## 0. Summary

1. **Outcome.** Vinyl can play from a home music server with one new layer: a **streaming source under the decoders**
   (today every decoder opens a local path) and a **fetcher** that downloads ahead into RAM. Everything above it (queue,
   gapless, crossfade, EQ, ReplayGain, volume) stays as it is.
2. **Best first server type: the Subsonic family (OpenSubsonic), with Navidrome on the NAS or PC.** The server already
   knows the artists, albums, covers, lengths and ReplayGain values, so the deck reads no file to browse. It streams the
   original file (gapless, seekable) or a small Opus copy for weak Wi-Fi. A key or a separate account can be revoked
   when a deck is lost. The same server serves the owner's phone too.
3. **Second: Jellyfin**, only if the owner already runs it (its Quick Connect sign-in avoids typing a password on the
   deck). Navidrome 0.64 also answers Jellyfin clients, experimentally [F linuxiac].
4. **Skip for now:** DLNA (no access control, uneven quality, XML), MPD (it plays on another machine, not on the deck),
   SMB/NFS inside Vinyl (31 MB of Samba libraries on Lite boards, the deck reads every file to learn its tags, no
   revocable credential). A share mounted by the system already works as an ordinary Vinyl folder, with two caveats in
   5.6.
5. **SD wear.** Streaming lives in RAM (a 24 MB cap on the 512 MB boards). The SD card is written only for what the
   user asks to download, into an "offline" folder that the existing Library indexes. An optional SD stream cache with a
   size cap is an owner decision.
6. **Size.** Phase N1 (streaming layer + OpenSubsonic browse and play + sign-in) **10-13 worker-days**; N2 (downloads,
   quality setting, optional cache) **4-6**; N3 (Jellyfin) **3-5**. A P0 probe first (controller about 0.5 day, owner
   about 30 minutes).
7. **Main risks:** a decoder that waits for the network inside the playback thread (solved by gating, 5.3); servers
   that differ in details (API key support, Range on streams) [U]; transcoded streams that lose gapless or seeking
   [I]; a mounted share treated as a removable drive (5.6) [F code].

**Owner decisions (section 7, recommendation first):** M1 server family; M2 what runs at home; M3 SD cache; M4 stream
quality; M5 library integration; M6 unreachable server and the queue; M7 credentials; M8 sign-in entry; M9 self-signed
HTTPS; M10 DLNA; M11 SMB/NFS; M12 play counts back to the server.

---

## 1. Starting point: what Vinyl has that matters here

| Item | Vinyl today [F, file] | Consequence for a network source |
|---|---|---|
| Decoders open a path | `TrackDecoder::open(const std::string &path, ...)` (`track_decoder.hpp:65`). MP3: `mpg123_open(path)`; Vorbis: `ov_open_callbacks` over an fd; FLAC and Opus: Cassette's `FileOps` with `pread`; WAV/AIFF: `pread` on an fd; FFmpeg: `avformat_open_input(path)` (`track_decoder.cpp:167, 253, 379, 435`, `ffmpeg_decoder.cpp:66`). | A byte-source seam under all six readers. FLAC, Opus, Vorbis and WAV already read through callbacks or `pread`: small change. MP3 needs mpg123's reader-handle API, FFmpeg a custom AVIO context: medium. |
| One engine thread | Decodes 2,048 frames, then writes them to the output, on one thread (`engine.hpp`, `kPeriodFrames`). | A read that waits for the network stalls the sound. The engine must only decode bytes that are already there (5.3). |
| Gapless, crossfade | Gapless continues on the same stream when the next track has the same format; the P2d crossfade reads the outgoing and incoming decoders together when the track length is known (`engine.hpp` P2d note, draft 0.7.0). | Both need the next track's first seconds in RAM before the current one ends: the fetcher prefetches the next queue entry. |
| Queue and resume | Paths; `state.json` written on a change and at most once a minute while playing (decisions, P2a). | A track reference that is not a path (`src:<server>/<id>`) plus the few tags a queue row shows, kept with the entry. |
| Library | SQLite, roots are folders or drives (`roots`, `tracks.folder_id`, `schema.cpp`). Scan at launch and from the Library only (owner D27: "we need to have SD wear in mind"). | Server catalogues fit as pages of their own first; downloaded albums become ordinary files under `~/Music`, which the Library already indexes. |
| Gone media | Owner ruling B49: tracks of a drive that is gone leave the queue; a drive is a mount under `/media/<user>/x`, `/run/media/<user>/x` or `/mnt/x` that is listed and readable (`media_guard.hpp`). | A network share mounted at `/mnt/x` counts as a drive: a Wi-Fi blip would empty its part of the queue (5.6). A server needs its own rule (M6). |
| Network code in the repo | Home Assistant opens `libcurl.so.4` at run time (curl 8.14.1 on the boards), stores its token in `~/.config/home-assistant/credentials.json` (mode 0600, folder 0700, atomic write), pairs through a QR code and a short-lived local listener (`apps/home-assistant/README.md`, `curl_api.hpp`). Vinyl has its own JSON reader (`core/util/json`). | No new library is needed for HTTP or JSON; the credential pattern exists. |
| Wi-Fi state | The launcher writes `status.state` with `wifi=1` and `wifi_pct` (`APP-CONTRACT.md` section 2). | Vinyl can tell "Wi-Fi is off" from "the server does not answer". |

---

## 2. The options in plain words (for the owner)

Think of the music on your NAS or PC as a record collection in another room. The options differ in **who knows what is
in the collection** and **what travels over Wi-Fi**.

- **A file share (SMB or NFS).** Your NAS already has one. The deck opens the NAS's folders as if they were a USB stick.
  Nothing new to install at home. But the deck has to open every file itself to learn its title, artist, length and
  cover, over Wi-Fi, before the Artists and Albums pages can show it; and it always receives the full-size file. Login
  is a user name and password that cannot be revoked separately (SMB), or no login at all (NFS).
- **DLNA ("media server" on the NAS).** Many NAS and TVs have a switch for it. The NAS keeps a list of the music and the
  deck asks for pages of that list. No password: anyone on the home Wi-Fi can browse. How good the lists and search are
  depends on the NAS brand. The deck receives the original file.
- **A Subsonic-family music server (Navidrome, Gonic).** A small program, music only, that you install once on the NAS
  or PC (usually one Docker container). It reads your folders, builds the catalogue, and answers the deck's questions
  ("albums of this artist", "search for 'blue'", "cover at 300 pixels", "this track's ReplayGain") in a fraction of a
  second. It sends the original file, or a smaller copy when the Wi-Fi is weak. A lost deck is solved by deleting its
  key or its account on the server. Dozens of phone apps speak the same language, so the same server serves your phone.
- **Jellyfin.** A full media centre (films, series, photos, music) with the same kind of catalogue and streaming. If you
  already run it for films, the deck can use its music part. Installing it only for music is heavy.
- **MPD.** A music player that runs on another computer and is remote-controlled. The music comes out of **that**
  computer's speakers, not the deck. Good for a living-room hi-fi, not for the deck in your hands.

| | Install at home | What the deck must do | Weak Wi-Fi | Deck lost |
|---|---|---|---|---|
| File share | nothing (share exists) | read every file for tags and covers | always the full file | change the share password |
| DLNA | switch it on | ask for pages | full file | nothing to revoke (no login) |
| **Subsonic family** | **one small program (Docker)** | **ask for pages** | **original or small Opus copy** | **delete its key or account** |
| Jellyfin | a large program | ask for pages | original or small copy | remove the device's sign-in |
| MPD | a player with speakers | remote control only | not relevant | change the password |

---

## 3. Comparison of every option

Effort is **on top of the shared streaming layer** of section 5 (itself M, paid once by the first network source; the
system-mounted share needs no layer). S: up to 3 worker-days; M: 4-8; L: more than 8.

| | Subsonic family (OpenSubsonic) | Jellyfin | DLNA / UPnP AV | MPD | SMB inside Vinyl (libsmbclient) | NFS inside Vinyl (libnfs) | Share mounted by the system |
|---|---|---|---|---|---|---|---|
| **What it is** | HTTP API for music servers: Subsonic 1.16.1 plus OpenSubsonic extensions [F subsonic.org, opensubsonic]. Servers: Navidrome, Gonic, LMS, Ampache; Airsonic-Advanced stalled since 2023 [I] | HTTP API of the Jellyfin media centre | SSDP discovery, ContentDirectory (Browse, Search, DIDL-Lite XML), HTTP file URLs; AVTransport is the other role (the deck as a renderer that a phone pushes to) | Text protocol over TCP (port 6600 by default [I]) controlling a player daemon that plays on its own outputs [F mpd protocol] | Windows and NAS file sharing, read by Vinyl itself | Unix file sharing, read by Vinyl itself | An SMB or NFS share mounted by root (fstab, systemd automount); Vinyl sees a folder |
| **At home** | A server program on the NAS or PC (Navidrome: Docker or one binary; ffmpeg on the server for transcoding) | Jellyfin server with a music library | The NAS's media server switched on (MiniDLNA/ReadyMedia, the NAS brand's own, Plex, Jellyfin's plugin) [I] | MPD on a machine with speakers | A share (exists on most NAS) and a user with read access | An export allowing non-privileged ports (`insecure` on Linux): libnfs runs without root [F gvfs-list, exports(5)] | The share, plus a one-time root set-up on every board |
| **Auth** | OpenSubsonic API key (`apiKey`, revocable, separate from the password); else `u` + `t` = md5(password + salt) with a new salt each call; plain or hex password legacy [F opensubsonic, subsonic.org] | User + password (`AuthenticateByName`) or Quick Connect (a code approved on a signed-in device), giving an access token per DeviceId, sent as `Authorization: MediaBrowser ... Token="..."` [F gist, kotlin-sdk] | None: anyone on the LAN [I] | Optional `password` command, plaintext [F mpd protocol] | User + password (NTLMv2), no revocable token | None: trust by client address and UID [I] | Root-owned credentials file in `/etc` |
| **Stored on the deck** | The API key; else the password (M7); in `servers.json` 0600; a separate deck account advised | The access token only; DeviceId | The server's UDN | The password, 0600 | The password, 0600; a read-only user advised | Nothing | Nothing in Vinyl |
| **Browsing** | Server index: `getArtists`, `getArtist`, `getAlbum`, `getAlbumList2` (newest, random, A-Z...), `search3`, `getPlaylists`/`getPlaylist`, `getCoverArt?size=N`; folders via `getIndexes`/`getMusicDirectory` (real on Gonic, simulated Artist/Album on Navidrome) [F navidrome, gonic] | `/Items` queries by type and parent, album artists, `searchTerm`, playlists, `/Items/{id}/Images/Primary` with a size [I] | The server's container tree (often Music > Artist > Album, or folders); Browse pages by `StartingIndex`/`RequestedCount`; Search optional per server; `dc:title`, `upnp:artist`, `upnp:album`, `upnp:albumArtURI` [I] | `lsinfo` folders, `list`/`find`/`search` by tag, stored playlists, `albumart`/`readpicture` [F] | Folders and files only; tags, lengths and covers need Vinyl's scanner reading every file over Wi-Fi | As SMB | As SMB, through the existing Browser and scanner |
| **Playback** | `stream` (original with `format=raw`, or transcoded with `format`/`maxBitRate`) or `download` (original) [F]. Seek: Range on the original [U per server]; transcoded: `timeOffset` (Transcode Offset extension; Navidrome reads it [F stream.go]). RG: `replayGain` on every song (OpenSubsonic) [F] or the file's tags | `/Audio/{id}/universal`: direct or transcoded from the client's limits (`container`, `audioCodec`, `maxStreamingBitrate`, `startTimeTicks`) [F sdk]. Range on direct play [U]. Loudness: `NormalizationGain` from a LUFS scan (10.9+), track level [I] | HTTP GET of the item's URL, usually the original file; byte seek when `DLNA.ORG_OP` allows Range, time seek when it says so [F async-upnp-client, MS-UPMC]. RG from file tags only | Plays on the server; the deck plays nothing (MPD's httpd output is one live stream, no per-track control) [I] | File reads: gapless, RG, seek exactly as local | As SMB | As local, no new code |
| **Gapless** | Original: as local. Transcoded MP3: likely lost (no encoder header in a piped stream); Opus: probably kept [I] | As Subsonic [I] | As local when the original is served | n/a | Yes | Yes | Yes |
| **Offline fit** | Good: `download` gives the original; ids stable [I] | Good (download endpoint) [I] | Possible, but object ids may change after a server rescan [I] | None | Good (copy files) | Good | Good |
| **Effort** | **M**: one JSON client, pages that reuse the Library's row model, a sign-in page. Best documented | **M** (S-M after Subsonic): bigger API, a Quick Connect page | **L**: SSDP, SOAP, XML, server quirks, Search missing on some | M as a remote, but a different app; L as a source (files would still come from a share) | **M**, plus 31 MB of libraries on Lite boards; the network scan is slow | M (S after SMB) | **S** in Vinyl (tell network mounts from drives, docs); the root set-up is outside the app |
| **CPU / RAM, Zero 2 W** | libcurl and its TLS library loaded (a few MB [I]), a 24 MB buffer cap, JSON pages under 1 MB; under 2 % of a core besides decoding [I] | Same | Same plus libxml2 (about 2 MB [I]) | Negligible | libsmbclient and samba-libs loaded: [U], estimate 10-20 MB; SMB3 signing in software | Small (about 1 MB [I]) | Kernel client; page cache only |
| **Flaky Wi-Fi** | Original: resume with Range, play from the buffer; transcoded: restart at `timeOffset` | Same, with `startTimeTicks` | Range resume when allowed; multicast discovery fails on some access points [I] | The remote reconnects; the music is elsewhere | Session re-established; blocking calls must sit in the fetch thread [I] | libnfs reconnects [I] | NFS `hard` (the default) blocks readers until the server is back [F nfs(5)]; Vinyl's probes can hang; see 5.6 |
| **Risks** | Servers differ in details; API key support varies [U]; plain HTTP common on a LAN | Heavy server for music only; API churn (legacy auth headers end in 12.0) [F gist]; NormalizationGain is not ReplayGain | Brand quirks, no access control, no RG, unstable ids | Does not play on the deck | Large dependencies; password-only; licence wording [U] | Server set-up (`insecure`), no auth | Root set-up per board; hangs; media-guard interaction |
| **Library licence** | libcurl (curl licence, MIT-style) + Vinyl's JSON reader (MIT) | Same | libcurl + libxml2 or expat (MIT); libupnp (BSD-3-Clause [I]) only for the renderer role | libmpdclient (BSD-3-Clause [I]) or own text client | libsmbclient: GPL-3.0-or-later / LGPL-3.0-or-later, packaging pages disagree [U] | LGPL-2.1-or-later (library) [F COPYING] | None in Vinyl |

**Plex** (not a column): a proprietary server with an account at plex.tv; a client signs in with a PIN that the user
confirms on plex.tv and sends an `X-Plex-Token` [I forum]; Plex servers usually also offer DLNA. Worth a look only if the
owner already uses Plex.

### 3.1 Notes per option

**Subsonic family.**
- OpenSubsonic recommends API keys and calls MD5 token auth obsolete and broken (kept for compatibility); it also asks
  clients to warn on plain HTTP and recommends form POST so credentials stay out of URLs and server logs [F opensubsonic
  api-reference].
- Navidrome targets API 1.16.1 "with some exceptions", has no real folder browsing, does not count `stream` as a play
  (only `scrobble` with `submission=true`), and supports several libraries with per-user access [F navidrome].
- Navidrome's `stream` reads `id`, `maxBitRate`, `format`, `timeOffset` and sets `X-Content-Duration`; `download` takes
  `format` and `bitrate` [F stream.go].
- Navidrome 0.62 (June 2026) capped concurrent transcodes; 0.64 (September 2026) made piped FLAC transcodes seekable and
  added the experimental Jellyfin API [F alternativeto, linuxiac].
- An uncached transcode can be served chunked with no length and `Accept-Ranges: none`; a cached one has a length and
  Range [I, user report on symfonium forum].
- Whether Navidrome offers API keys is [U]: it is not in its API page nor in the 0.62/0.64 notes read. P0 checks
  `getOpenSubsonicExtensions`.
- Gonic: browses real folders and tags, transcodes with a cache, GPL-3.0, "lightweight and suitable for a raspberry pi"
  [F gonic]; partial OpenSubsonic support [I symfonium docs].
- The `c=` client name lets the server's admin set a transcoding per player (Navidrome "players") [I].

**Jellyfin.**
- `Authorization: MediaBrowser Client=..., Device=..., DeviceId=..., Version=..., Token=...`; one token per DeviceId;
  `X-Emby-Token`, `X-MediaBrowser-Token`, `api_key` deprecated, to be disabled in 12.0 [F gist].
- Quick Connect: initiate, show the code, poll the state every 5 s, then `AuthenticateWithQuickConnect` gives the token;
  a server may have it disabled (401) [F kotlin-sdk].
- The universal endpoint returns 503 with `Retry-After` while the server starts [F sdk].

**DLNA.** The control-point side (browse and play) is the useful one for Vinyl. The renderer side (AVTransport: a phone
app pushes a track to the deck) needs a UPnP device stack (libupnp), eventing, and a second owner of the queue: a
different feature, not a source.

**MPD.** No command returns a song's audio to the client (only pictures, comments and fingerprints) [F mpd protocol].
As a Vinyl source it would need the same files from a share, so it adds nothing over SMB/NFS. As a remote for a
living-room MPD it is a different app.

**SMB and NFS inside Vinyl.** Debian's FFmpeg is built without SMB or NFS (no libsmbclient or libnfs in libavformat61's
Depends) [F packages.debian.org], so Vinyl would load the libraries itself. libsmbclient0 is small, but it needs
samba-libs (30.9 MB installed) and 22 more packages (libicu76, libldap2, libavahi-client3...) [F packages.debian.org]; some
may already be installed [U]. libnfs14 needs only libc6 [F]. gvfs (`gio mount smb://...`) avoids root but needs
gvfs-backends with 31 dependencies, its daemons and a D-Bus session bus [F packages.debian.org; session bus on Lite
boards U].

**Mounted by the system.** The cheapest path and already usable: mount the share under a folder and add it in Vinyl's
Music folders. The kernel client does the reading. Two faults to fix first (5.6).

---

## 4. Libraries on aarch64 Debian 13 (trixie)

| Need | Package | Version, installed size | Depends | Licence | Use in Vinyl |
|---|---|---|---|---|---|
| HTTP(S) | `libcurl4t64` | 8.14.1 on the boards [F HA `curl_api.hpp`]; already installed on every board through `curl` [F HA README] | OpenSSL flavour [I] | curl licence (MIT-style) | **Yes**: `dlopen("libcurl.so.4")` as Home Assistant; Depends `libcurl4t64` |
| JSON | Vinyl's `core/util/json`; `libcjson1`; `nlohmann-json3-dev` (header only) | | | MIT | **Own reader** (no new library) |
| XML (DLNA only) | `libxml2` | already installed with Vinyl: a Depends of `libavformat61` [F] | | MIT | Only if DLNA is chosen |
| UPnP device stack | `libupnp17t64` | 1.14.20, 362 kB + `libixml11t64` [F] | libc6 | BSD-3-Clause [I] | Only for a renderer role: skip |
| MPD | `libmpdclient2t64` | 2.22, 237 kB [F] | libc6 | BSD-3-Clause [I] | Skip |
| SMB | `libsmbclient0` | 4.22.11, 281 kB + `samba-libs` 30.9 MB + 22 packages [F] | samba-libs, libtalloc2, libtevent0t64... | GPL-3.0-or-later / LGPL-3.0-or-later [U] | Only if M11 says so |
| NFS | `libnfs14` | 5.0.2, 445 kB [F] | libc6 | LGPL-2.1-or-later [F] | Only if M11 says so |
| SMB/NFS without root | `gvfs-backends` | 1.57.2, 2.6 MB + 31 dependencies (gvfs daemons, libsmbclient0, libnfs14, libgoa, libsoup-3.0, libgdata) [F] | D-Bus session | LGPL [I] | No |
| Network in FFmpeg | `libavformat61` (already a Depends) | built with gnutls, libssh, libsrt; no smb, no nfs [F] | | GPL as built | Not for servers: credentials would sit in URLs and nothing would be shared with the cache |

Everything Vinyl would add for N1 to N3 is libcurl, which the boards already have. The Depends line grows by
`libcurl4t64`; the binary links nothing new (run-time loading, as for SQLite and FFmpeg).

---

## 5. Architecture sketch

```
  Library pages / Browser / Queue (UI thread)
        |  rows, search, covers (async)            TrackRef: "/home/u/Music/a.flac" or "src:nav1/6f3c..."
        v
  SourceRegistry ---- LocalSource (files, SQLite: today's code)
        |       \---- SubsonicSource / JellyfinSource (HTTP, JSON)        ServerStore: servers.json (0600)
        v
  Fetcher thread: one network reader, RAM chunk store (cap), Range resume, prefetch of the next queue entry,
        |         optional SD cache, downloads into ~/Music/Offline
        v
  ByteSource (size, read_at, available_ahead)  <- FdByteSource for local files
        |
  Engine thread: gate on available bytes -> TrackDecoder::open(ByteSource&) -> DSP chain -> output
```

### 5.1 Track references and sources

- A **TrackRef** is a local path (unchanged) or `src:<source-id>/<item-id>`, with a **TrackMeta** carried beside it
  (title, artist, album, album artist, disc and track, duration in ms, codec suffix, bit rate, cover id, ReplayGain).
  Queue rows, Track info and `state.json` read TrackMeta for remote entries, so they show without the network and
  without TagJobs (which reads files by path).
- A **Source** has two faces, matching Vinyl's two browse models:
  - **FolderSource** (the Browser): `list(node)` returns folders, playlists and tracks. Local folders today; Subsonic
    `getMusicDirectory`, DLNA containers and SMB folders later.
  - **CatalogSource** (the Library): artists, albums of an artist, an album's tracks, songs, genres, `search(q)`,
    playlists, `cover(id, px)`. The SQLite library today; Subsonic ID3 calls and Jellyfin `/Items` for servers.
  All calls are asynchronous on a worker (the Library's `LibraryWorker` pattern) and return the same row model the
  Library pages draw (`lib_model`), so the pages can be reused with another model behind them [I].
- `open(TrackRef, quality) -> ByteSource` hands the engine its bytes; the source turns the ref into a URL and headers.

### 5.2 The byte source under the decoders

`TrackDecoder::open(ByteSource &, TrackInfo &)` replaces the path. A ByteSource offers `size()` (or unknown),
`read_at(offset, n)` and `available_ahead(offset)`. Per reader:

| Reader | Today | Change |
|---|---|---|
| FLAC, Opus (Cassette code) | `FileOps` + `pread` | a `FileOps` that reads a ByteSource: small |
| Vorbis | `ov_open_callbacks` over an fd | callbacks over a ByteSource: small |
| WAV / AIFF | `pread` on an fd | small |
| MP3 | `mpg123_open(path)` | mpg123's reader-handle API (`mpg123_replace_reader_handle` + `mpg123_open_handle`, or the 64-bit reader of 1.32) [I]: medium; P0 checks the symbols on the boards' libmpg123 |
| FFmpeg (tier 2) | `avformat_open_input(path)` | a custom AVIO context (`avio_alloc_context` with read and seek callbacks); Vinyl's hand-written FFmpeg ABI needs those functions and the `pb` field, checked by `tests/abi` for 61 and 62: medium |
| Tag reader, cover reader | fd + `pread` | the same seam, used only when the server gives no metadata |

### 5.3 The fetcher and buffer sizing

**Rule: the engine thread never waits for the network.** Before each decode period the engine asks
`available_ahead(decoder offset)`. Below the low mark it does not call the decoder; it holds at the heard frame (the
existing pause path) and reports **Buffering**; at the high mark it plays on. A decoder callback can still meet a hole
(a VBR spike, a FLAC seek that bisects): it waits at most a few seconds while the output plays its own buffer, then the
engine reports Buffering [I].

| Format | Rate | MB a minute | Audio in a 24 MB cap | in 64 MB | 4-min track |
|---|---|---|---|---|---|
| Opus 128 kb/s (transcoded) | 16 kB/s | 0.96 | 25 min | 67 min | 3.8 MB |
| MP3 320 kb/s | 40 kB/s | 2.4 | 10 min | 27 min | 9.6 MB |
| FLAC 16-bit 44.1 kHz (about 900 kb/s) | 112 kB/s | 6.75 | 3.6 min | 9.5 min | 27 MB |
| FLAC 24-bit 96 kHz (about 3 Mb/s) | 375 kB/s | 22.5 | 64 s | 2.8 min | 90 MB |

Proposal [I, P0 checks the RAM]:
- **Cap** by RAM: 24 MB on boards with 512 MB or less (MemAvailable about 260-270 MB on zero8 at boot [F memory note
  2026-11-14]), 64 MB up to 1 GB, 128 MB above.
- **Start** a track when 3 s of audio are in (bytes = bit rate x 3 s; the server gives the bit rate) or the whole file;
  **rebuffer** below 1 s ahead, **resume** at 8 s ahead.
- **Order:** the playing track to its end (or to the cap), then the next entry of the play order (shuffle included),
  whole if it fits, else its first 30 s; nothing further unless the user asked for "cache ahead" (N2).
- **Chunks** of 256 kB in a sparse map: a seek into a hole starts a Range request at that chunk; earlier chunks of the
  playing track are freed first when the cap is reached.
- **Connections:** connect timeout 5 s; abort below 1 kB/s for 15 s; retry after 1, 2, 4, 8, then every 30 s while
  playing, resuming at the first missing byte with a Range request. While paused the buffer is kept; a long pause may
  lose the connection, and play resumes with a Range request.
- **Wi-Fi.** The Zero 2 W has 2.4 GHz 802.11n only; the Pi 3A+, 4 and 5 have 5 GHz too [I, product briefs]. A 4-minute CD
  FLAC is 27 MB: at 20 Mb/s it arrives in about 11 s, so most of a track survives a drop. A 24/96 FLAC needs 3 Mb/s
  sustained just to keep up, which weak 2.4 GHz may not give: the reason for a "Wi-Fi saver" quality (M4).

### 5.4 Cache on the SD card

- **Why not cache every streamed track on SD:** three hours a day of CD FLAC is about 1.2 GB written a day, about 440 GB
  a year [I, from the table]. Cheap cards wear unevenly, and a full card slows down. Opus 128 cuts that by seven.
- **Policy proposed:** streaming in RAM only; the SD card is written for **downloads the user asks for** (an album or a
  playlist "for offline"), and optionally for a **capped stream cache** (M3).
- **Downloads** are original files written to `~/Music/Offline/<server>/<album artist>/<album>/<disc-track title>.<ext>`
  with their own tags and covers. The folder lies inside `~/Music`, so the existing root covers it: the scanner indexes
  it (one `scan_folder` after the download, D27's rule for a folder opened from the Library), the Artists and Search
  pages show it, and it plays without any network. No new library code [I]. Deleting a download needs an action of its
  own: Music folders' "Remove selected folder" only drops a folder's rows, never files (decisions, P2b).
- **Write pattern:** sequential writes in 1 MB blocks, no fsync per block (one at the end, then rename from `.part`),
  `posix_fadvise(DONTNEED)` after each block so the page cache does not evict the decoder's data [I].
- **Optional stream cache** (if M3 says so): `~/.cache/vinyl/stream/`, least recently played first out, a cap such as
  1 GB, never above 10 % of the free space, files kept only when they were played to the end; "cache ahead" fills it
  with the next N queue entries while on Wi-Fi.
- **Covers:** `getCoverArt?size=N` at the layout's size (a few tens of kB) in a RAM cache of about 20 decoded covers;
  downloads keep their embedded covers. No cover files on SD [I].

### 5.5 Gapless, crossfade, EQ, ReplayGain and seek

- **EQ, volume, limiter, sleep fade:** unchanged; they work on decoded samples.
- **Gapless:** unchanged for original files, provided the next track's start is in RAM before the current ends (the
  fetcher's order above). If it is late, the engine waits at the boundary: a gap, never a skip.
- **Crossfade:** needs both tracks in RAM and the length known. Original files give the length from the decoder; a
  transcoded stream may not (no length header), so either no crossfade or the server's duration (whole seconds) as the
  start point [I].
- **ReplayGain:** from the server when it gives it (OpenSubsonic `replayGain` on every song: track, album, peaks, base
  gain) [F]; Jellyfin's `NormalizationGain` maps to Track mode only [I]; otherwise the tags inside the original file
  (the tag reader over the ByteSource). A transcoded stream has no tags: server values only.
- **Seek:** original file: the decoder seeks, the fetcher fetches the missing chunk with Range (one round trip, well under
  a second on a LAN [I]). Transcoded: the engine reloads at the target with `timeOffset` (Subsonic) or `startTimeTicks`
  (Jellyfin); the engine already loads at a start position (`Engine::load(pos, path, start_ms, play)`).

### 5.6 Library integration and the owner's rulings

- **L1, server pages (N1):** the server's catalogue is browsed live; nothing is written to SD but the settings. Search is
  the server's (`search3`). Without the network these pages say so; downloads still play.
- **L2, offline albums (N2):** downloads are local files under `~/Music/Offline` (5.4) and join the local Library.
- **L3, catalogue sync (later, maybe never):** the server's tracks as rows in SQLite (schema v2: `roots.kind`,
  `tracks.uri`) for one Artists list and one Search across local and server. It costs a first sync (a few MB written for
  20,000 tracks [I]) and deltas; an offline server's rows would be hidden, as the owner's "a source that is gone is shown
  nowhere" ruling wants.
- **Unreachable server and the queue (M6):** the drive ruling (B49) removes a gone drive's tracks from the queue. A
  server blinks away far more often (Wi-Fi), so the proposal keeps its entries, pauses with a notice, and lets Next move
  to the next playable entry. This departs from B49 for servers only: the owner decides.
- **Mounted shares, two faults to fix even without any server work** [F code, I effect]:
  1. A share mounted at `/mnt/<name>` is a "drive" for `media_guard` (`/mnt/<label>`, depth 1). When the Wi-Fi drops,
     the mount is unreadable, the guard calls it gone and its tracks leave the queue. Fix: treat network file systems
     (`cifs`, `smb3`, `nfs`, `nfs4`, `fuse.*` in `/proc/self/mounts`) as network roots with the server rule of M6.
  2. An NFS mount is `hard` by default: "NFS requests are retried indefinitely", so a `stat` on it blocks until the
     server answers [F nfs(5)] (CIFS is `soft` by default and returns errors [F mount.cifs(8)]). `root_readable` and the
     scanner would hang their thread; if a page calls `stat` on the UI thread, the app freezes [U]. Advise
     `soft,timeo=50,retrans=2` (nfs(5) warns that soft can corrupt writes; Vinyl only reads) or a systemd automount with
     a mount timeout, and keep probes off the UI thread.

### 5.7 Credentials and sign-in

- **Store:** `~/.config/vinyl/servers.json`, mode 0600, folder 0700, written atomically: the Home Assistant pattern. Per
  server: kind, label, base URL, user, and one credential:
  - OpenSubsonic API key when the server offers it (revocable, not the password) [F opensubsonic];
  - else the password (M7), with a fresh salt and token computed for every request as the Subsonic page asks [F
    subsonic.org]; a separate account for the deck, so a lost deck is one account deleted on the server;
  - Jellyfin: the access token and the DeviceId; never the password (Quick Connect or a one-time typed password).
- **What the file protects:** other users of the board, not someone holding the SD card. The boards offer no secure key
  store that an app of the user can use [I]. `systemd-creds --user` (systemd 256 and later) keys on the host secret `/var/lib/systemd/credential.secret`, which
  lives on the same card without a TPM [F systemd-creds(1)]; a keyring daemon (Secret Service) does not run on Lite
  images [I]. So the real protection is a **revocable** credential, not the file.
- **Never in logs:** no URL with `t`, `s`, `p` or `apiKey` in Vinyl's journal; form POST where the server offers it
  (`formPost` extension), the `Authorization` header for Jellyfin [F opensubsonic, gist].
- **Transport:** HTTPS with the system's CAs. Plain HTTP on a LAN allowed with a one-time warning (OpenSubsonic asks
  clients to warn) [F]. Self-signed NAS certificates: M9.
- **Sign-in on the deck:** server address, user and password typed with the Bluetooth or USB keyboard (the primary input;
  the on-screen keyboard is the automatic fallback). Jellyfin Quick Connect needs only the address: the deck shows a
  code, the owner approves it on the phone. Later: Home Assistant's QR pairing could carry a phone form for the address
  and password (the password then crosses the LAN once in clear) (M8).

### 5.8 Offline and flaky Wi-Fi

- **States per server:** online; unreachable (DNS, connect or read timeouts); signed out (401/403, or Subsonic errors for
  a wrong user, password or key); server error.
- **Cause shown:** `status.state` `wifi=0` gives "Wi-Fi is off"; Wi-Fi on but no answer gives "the server does not
  answer".
- **While playing:** drops are absorbed by the buffer; an empty buffer means Buffering (held at the heard frame, plays on
  by itself when refilled); after 2 minutes without progress playback pauses with a notice, so music never starts by
  itself much later [I proposal].
- **At start:** remote queue entries are kept (M6); the first play tries the server; downloads always play.
- **Pages:** a server page that fails shows its reason in the list's empty state; the pages already loaded stay until
  the user leaves them [I proposal].

### 5.9 Error texts on narrow screens

The layout and look are the coordinator's design call; this section gives the constraints and draft texts only.
- On now playing at 480x320 the notice band holds two lines (38 px) (decisions, design QA run 3); 320x480 is narrower.
  Texts stay under about 60 characters; a server name longer than the room is replaced by "the server".
- Draft texts: "Wi-Fi is off. Paused." / "Can't reach <server>. Paused." / "Buffering..." / "<server> refused the
  sign-in. Check the user name and key." / "The certificate of <server> is not trusted." / "This track is no longer on
  <server>." / "Downloaded <album>. It plays without Wi-Fi."
- Open design questions for the coordinator: how Buffering shows (a word in the format line, or a lighter fill of the
  buffered range on the seek bar); where the servers enter the Library hub (a row per server, or one "Servers" row);
  the sign-in page at 480x320 with a keyboard up; how a remote entry looks in the queue while its server is unreachable.

### 5.10 Memory and CPU on the Pi Zero 2 W

| Item | Cost |
|---|---|
| Vinyl today | 59 MB RSS with FFmpeg loaded on the HackBerry [F 060]; the Zero 2 W figure is [U] |
| libcurl + TLS library | a few MB when loaded [I] |
| Stream buffer | 24 MB cap (5.3) |
| Covers in RAM | about 20 x 448 x 448 x 2 bytes = 8 MB at most [I] |
| JSON pages | under 1 MB (40-row pages, as the Library) [I] |
| CPU, network | under 1 % of a core for HTTP at FLAC rates; TLS in software (the Zero 2 W's A53 has no crypto extensions [I]) about 1-2 % at 3 Mb/s [I] |
| CPU, decoding | unchanged: 0.8-3.6 % of one core per codec [F 059 14.4] |

Total extra about 35 MB on the 512 MB boards, against 260-270 MB available at boot [I].

---

## 6. Recommendation and phases

| Phase | Contents | Worker-days | Depends on |
|---|---|---|---|
| **P0** probe | The list of section 9 on zero8 or the deck, the Pi 4B and the owner's server; no install on the boards beyond curl, which is there | controller 0.5, owner about 30 min | M1, M2 |
| **N1** | ByteSource seam in the six readers and the tag reader; fetcher with RAM chunks, Range resume and next-track prefetch; Buffering in the engine; TrackRef and TrackMeta in the queue and `state.json`; OpenSubsonic source (ping, extensions, artists, artist, album, album lists, search, playlists read-only, covers, `stream` raw); server pages on the Library's row model at the five screen classes; sign-in page and `servers.json`; the mounted-share guard fix (5.6); unit tests with a fake server, QA scenes, board protocol | **10-13** | P0; design boards for the server pages and the sign-in page |
| **N2** | Download album or playlist to `~/Music/Offline`; Stream quality (Original / Wi-Fi saver Opus) per server; optional capped stream cache with "cache ahead"; play counts back with `scrobble` (M12) | **4-6** | N1 |
| **N3** | Jellyfin source: Quick Connect and password sign-in, universal stream, NormalizationGain, pages on the same model | **3-5** | N1 |
| Later | Server playlists editing; catalogue sync (L3); internet radio on the same fetcher (059 D15); listening away from home through a VPN | | |

**What to skip and why:**
- **DLNA**, unless the owner's NAS offers nothing else and Navidrome cannot run there (M10): no access control, quality
  per brand, XML, L effort.
- **MPD**: it does not play on the deck.
- **SMB/NFS inside Vinyl** (M11): a system mount gives the same result with no code; libsmbclient adds 31 MB on Lite
  boards; the network scan is slow; no revocable credential.
- **The renderer role** (AVTransport, AirPlay, Spotify Connect): another owner of the queue; a different feature.

---

## 7. Decisions for the owner (recommended option first)

| # | Question | Options (recommended first) |
|---|---|---|
| **M1** | Which kind of server first? | **(a) Subsonic family (Navidrome or Gonic)**; (b) Jellyfin (only if already running); (c) DLNA from the NAS; (d) the NAS's SMB share as a folder |
| **M2** | What runs at home? | **(a) Navidrome on the NAS or PC (Docker), with ffmpeg for the Wi-Fi saver**; (b) the Jellyfin already there; (c) nothing new: DLNA or a share only |
| **M3** | May streaming write to the SD card? | **(a) No: RAM for streaming, SD only for albums you download**; (b) a capped SD stream cache (1 GB) on every board; (c) the cache only on boards with 2 GB of RAM or more or a USB disk |
| **M4** | Default stream quality | **(a) Original file, with a per-server "Wi-Fi saver" switch (Opus about 128 kb/s; may lose gapless on some servers)**; (b) Wi-Fi saver always on the Zero 2 W and Pi 3A+; (c) always the original |
| **M5** | How servers appear in the Library | **(a) Server pages of their own, downloads join the local Library**; (b) merge the server's catalogue into the local Library from the start (sync) |
| **M6** | When the server cannot be reached | **(a) Its tracks stay in the queue; playback pauses with a notice; Next moves to the next playable track (an exception to the B49 drive rule for servers)**; (b) treat it like a pulled drive: its tracks leave the queue |
| **M7** | What the deck stores | **(a) A server key or token (revocable); where the server has none, the password of a separate deck account, in a file only your user can read**; (b) your own password in that file; (c) ask for the password at each start |
| **M8** | How to sign in | **(a) Type on the deck's keyboard; Jellyfin's Quick Connect where available**; (b) also a phone form through a QR code (as Home Assistant); (c) a settings file edited over SSH only |
| **M9** | A NAS with a self-signed HTTPS certificate | **(a) Refuse it, as Home Assistant does; plain HTTP on the home network allowed with a warning**; (b) trust it on first use and remember its fingerprint |
| **M10** | DLNA | **(a) Skip**; (b) a browse-only DLNA source after N1; (c) also make the deck a DLNA renderer |
| **M11** | SMB / NFS | **(a) Not inside Vinyl: a share mounted by the system works as a folder (with the guard fix of N1 and a short guide)**; (b) SMB inside Vinyl with libsmbclient (31 MB more on Lite boards); (c) gvfs |
| **M12** | Play counts back to the server | **(a) Yes: a play that reached the end is reported with `scrobble`, the rule of the local play counts**; (b) no |

---

## 8. Out of scope (one paragraph each)

- **Roon:** proprietary; a "Roon Ready" device needs Roon's certification and SDK under licence. Not for a hobby app.
- **Spotify Connect:** only through librespot (unofficial), needs a Premium account, and Spotify's terms forbid
  unofficial clients [I]. The deck would be a receiver, not a player of the owner's files.
- **AirPlay:** shairport-sync makes the deck a receiver that a phone pushes to; a system daemon with root install and a
  sound server; a separate app or launcher feature, not a Vinyl source.
- **Snapcast:** synchronised multi-room audio; the deck would be one speaker of a server's stream; no library or queue
  of its own.
- **MPD as a remote:** a "living-room remote" app could control an MPD hi-fi; a different app from a player.

---

## 9. Honest unknowns, and what a P0 probe needs (list only, nothing was run)

Unknowns:
1. Does the owner's Navidrome (version) offer API keys, Range on raw streams, Transcode Offset, `replayGain` values?
2. Real Wi-Fi throughput and drop pattern of a Zero 2 W in the owner's rooms (2.4 GHz only), and the effect of Wi-Fi
   power saving on bursts.
3. The boards' libmpg123 reader-handle symbols, and FFmpeg's AVIO functions and `pb` offset in Vinyl's hand ABI (61 and
   62).
4. Gapless across Navidrome Opus and MP3 transcodes on a gapless album.
5. Vinyl's RSS on a Zero 2 W with a 24 MB buffer, and MemAvailable while it streams.
6. TLS cost on the A53 at FLAC rates (HTTPS to the NAS).
7. SD writes during a download (bytes written, no stalls in playback).
8. For M11 only: the installed size of libsmbclient0 and its dependencies on a Lite board; a session D-Bus on Lite
   boards (gvfs); whether the NAS allows NFS from non-privileged ports.
9. For M10 only: does the NAS's DLNA server answer SSDP from the deck over Wi-Fi (multicast on the access point)?
10. For N3 only: Jellyfin version, Quick Connect enabled, album-level NormalizationGain.

P0 needs (read-only on the boards, about 30 minutes of the owner):
- zero8 or the deck (Zero 2 W) and the Pi 4B, Vinyl stopped; the owner's NAS or PC with Navidrome (or a test instance
  on a PC) and a deck account; 10 test albums incl. one gapless album, one 24/96 FLAC, one with ReplayGain tags.
- `curl` (present on the boards): `ping.view`, `getOpenSubsonicExtensions.view` (lists `apiKeyAuthentication`,
  `transcodeOffset`, `formPost`?), `getAlbum.view` (are `replayGain`, `bitDepth`, `samplingRate` there?), `curl -I` and a
  Range request on `stream?format=raw` (206, `Accept-Ranges`), a transcoded `stream?format=opus&maxBitRate=128`
  (length header or chunked).
- Throughput: `curl -o /dev/null` of a 100 MB file from the NAS in three rooms; a 30-minute download of a large file
  with `-w` timings every 10 s while the owner walks around (stall lengths); `iw dev wlan0 get power_save` read only.
- `nm -D` on the boards' `libmpg123.so.0` for the reader-handle symbols; Vinyl's `tests/abi` for AVIO.
- `/proc/meminfo` and `/sys/block/mmcblk0/stat` before and after a 200 MB download to `/tmp` (tmpfs) and to the SD card.
- `top -b -d 1` during an HTTPS download at 3 Mb/s with `--limit-rate`.
- A listening check by the owner: the gapless album as Opus transcode (decoded on the PC with ffmpeg is enough for the
  first look).

---

## 10. Not verified

- Navidrome's API key support, its Range behaviour on raw streams and its Transcode Offset advertisement: not in the pages
  read [U].
- Jellyfin's Range on direct streams, album-level NormalizationGain and the device list's revoke wording [U/I].
- libsmbclient's licence: Fedora's metadata says GPL-3.0-or-later AND LGPL-3.0-or-later, other packagers differ; Samba's
  own `COPYING` was not read [U].
- The BSD-3-Clause licences of libupnp and libmpdclient are from general knowledge; their copyright files were not read
  [I]. Neither is recommended.
- Every CPU, RAM, wear and throughput figure marked [I] is computed, not measured; P0 measures the ones that matter.
- No board was touched; no server was queried; no code was written.

## Sources

- OpenSubsonic: API reference (authentication, common parameters, security notes)
  https://opensubsonic.github.io/docs/api-reference/ ; `stream` https://opensubsonic.github.io/docs/endpoints/stream/ ;
  `download` https://opensubsonic.github.io/docs/endpoints/download/ ; extensions
  https://opensubsonic.github.io/docs/extensions/ ; Transcoding extension
  https://opensubsonic.github.io/docs/extensions/transcoding/ ; Child https://opensubsonic.github.io/docs/responses/child/ ;
  ReplayGain https://opensubsonic.github.io/docs/responses/replaygain/ (all read 2026-11-17).
- Subsonic API 1.16.1 (salt per call, `getCoverArt` size, `format=raw`): https://www.subsonic.org/pages/api.jsp
- Navidrome: Subsonic API compatibility https://www.navidrome.org/docs/developers/subsonic-api/ ; `server/subsonic/stream.go`
  https://raw.githubusercontent.com/navidrome/navidrome/master/server/subsonic/stream.go ; 0.62 notes
  https://alternativeto.net/news/2026/6/navidrome-0-62-adds-audio-similarity-redesigned-now-playing-ui-and-new-security-measures/ ;
  0.64 notes https://linuxiac.com/navidrome-0-64-adds-experimental-jellyfin-music-api-support/
- Transcoded stream headers (user report): https://support.symfonium.app/t/seeking-flac-transcodes/3144 and search
  summaries of the symfonium support forum; server status per OpenSubsonic extension:
  https://docs.symfonium.app/wiki/providers/subsonic-opensubsonic-media-provider-configuration/
- Gonic: https://github.com/sentriz/gonic ; Airsonic-Advanced status: search summaries (AUR, linuxserver forum, fork
  release notes), 2026-11-17.
- Jellyfin: authorization header and deprecations https://gist.github.com/nielsvanvelzen/ea047d9028f676185832e51ffaf12a6f ;
  Quick Connect and password flows https://kotlin-sdk.jellyfin.org/guide/authentication.html ; universal audio endpoint
  parameters https://webysther.github.io/jellyfin-sdk-python.github.io/api_10_11/docs/UniversalAudioApi ; NormalizationGain
  (community reports) https://support.symfonium.app/t/jellyfin-replay-normalization-gain/14084
- DLNA `DLNA.ORG_OP` flags: https://sources.debian.org/src/async-upnp-client/0.45.0-2/async_upnp_client/profiles/dlna.py ;
  https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-upmc/c12f5530-fa2d-4d9b-a960-18f4e9678100
- MPD protocol: https://mpd.readthedocs.io/en/latest/protocol.html
- Debian trixie arm64 package pages (versions, sizes, Depends), 2026-11-17: libavformat61, libsmbclient0, samba-libs,
  libnfs14, libupnp17t64, libmpdclient2t64, gvfs-backends (https://packages.debian.org/trixie/arm64/<package>).
- libnfs licences: https://raw.githubusercontent.com/sahlberg/libnfs/master/COPYING ; libsmbclient licence (packaging
  metadata): https://packages.fedoraproject.org/pkgs/samba/libsmbclient/
- NFS from userland and `insecure`: https://lists.gnome.org/archives/gvfs-list/2015-March/msg00000.html ;
  https://man7.org/linux/man-pages/man5/exports.5.html ; hard and soft mounts: https://man7.org/linux/man-pages/man5/nfs.5.html ,
  https://man7.org/linux/man-pages/man8/mount.cifs.8.html
- systemd-creds `--user`: https://manpages.debian.org/trixie/systemd/systemd-creds.1.en.html
- Plex sign-in (forum): https://forums.plex.tv/t/authenticating-with-plex/609370
- Repository files: apps `main` @ fdad829 and worktree `apps-vinyl-p2d` (`apps/vinyl/src/core/decode/track_decoder.{hpp,cpp}`,
  `engine/engine.hpp`, `library/schema.cpp`, `library/media_guard.hpp`, `docs/decisions.md`, `README.md`),
  `apps/home-assistant/{README.md, src/core/curl_api.hpp, src/core/store.cpp}`; launcher `github/main` @ 2f1e8467
  (`docs/APP-CONTRACT.md`, reports 059 and 060).
